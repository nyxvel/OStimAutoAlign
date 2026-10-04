#include "Aligner.h"

#include "Body.h"
#include "OStimAPI.h"
#include "Scenes.h"
#include "Settings.h"
#include "Solver.h"

namespace Aligner
{
	namespace
	{
		using Alignment = OStimAPI::ActorAlignmentData;

		std::string Lower(std::string_view a_str)
		{
			std::string out(a_str);
			std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		bool Same(const Alignment& a, const Alignment& b)
		{
			constexpr float e = 0.01f;
			return std::abs(a.offsetX - b.offsetX) < e && std::abs(a.offsetY - b.offsetY) < e && std::abs(a.offsetZ - b.offsetZ) < e &&
			       std::abs(a.scale - b.scale) < 0.001f && std::abs(a.rotation - b.rotation) < e && std::abs(a.sosBend - b.sosBend) < 0.5f;
		}

		json ToJson(const Alignment& a)
		{
			return { { "offsetX", a.offsetX }, { "offsetY", a.offsetY }, { "offsetZ", a.offsetZ }, { "scale", a.scale },
				{ "rotation", a.rotation }, { "sosBend", a.sosBend } };
		}

		Alignment FromJson(const json& j)
		{
			return { j.value("offsetX", 0.0f), j.value("offsetY", 0.0f), j.value("offsetZ", 0.0f), j.value("scale", 1.0f),
				j.value("rotation", 0.0f), j.value("sosBend", 0.0f) };
		}

		// ---- record of what this mod decided per scene + actors ----------------------------

		// 2: v1.1 (direction costs, moving preferred over bending). 3: v1.2 (the shaft's mesh centre
		// line instead of its bones, the middle of the mouth). Older entries are redone.
		constexpr int kRecordVersion = 3;

		std::mutex g_recordLock;
		json       g_records = json::object();

		fs::path RecordPath()
		{
			const auto dir = logger::log_directory();
			return dir ? dir->parent_path() / "OStim" / "autoalign.json" : fs::path();
		}

		void LoadRecords()
		{
			std::lock_guard lock(g_recordLock);
			std::ifstream   in(RecordPath());
			if (!in) {
				return;
			}
			const auto j = json::parse(in, nullptr, false);
			if (j.is_object() && j.contains("scenes") && j["scenes"].is_object()) {
				g_records = j["scenes"];
			}
			logger::info("{} recorded scenes in {}", g_records.size(), RecordPath().string());
		}

		void SaveRecordsLocked()
		{
			const auto path = RecordPath();
			std::error_code ec;
			fs::create_directories(path.parent_path(), ec);
			std::ofstream out(path);
			out << json{ { "info", "OStim Auto Align: what it did per scene + actors. Delete an entry (or the file) to measure again." },
					   { "scenes", g_records } }
					   .dump(1);
		}

		void Record(const std::string& a_key, const std::string& a_result, const std::vector<Alignment>& a_alignments, const json& a_extra = {})
		{
			std::lock_guard lock(g_recordLock);
			json            entry = { { "result", a_result }, { "version", kRecordVersion }, { "alignments", json::array() } };
			for (const auto& al : a_alignments) {
				entry["alignments"].push_back(ToJson(al));
			}
			if (a_extra.is_object()) {
				entry.update(a_extra);
			}
			g_records[a_key] = std::move(entry);
			SaveRecordsLocked();
		}

		std::optional<json> FindRecord(const std::string& a_key)
		{
			std::lock_guard lock(g_recordLock);
			if (g_records.contains(a_key)) {
				return g_records[a_key];
			}
			return std::nullopt;
		}

		// ---- main thread ------------------------------------------------------------------

		template <class F>
		auto OnMain(F a_fn, std::chrono::milliseconds a_timeout = 5000ms) -> std::optional<std::invoke_result_t<F>>
		{
			using R = std::invoke_result_t<F>;
			auto promise = std::make_shared<std::promise<R>>();
			auto future = promise->get_future();
			SKSE::GetTaskInterface()->AddTask([promise, fn = std::move(a_fn)]() mutable {
				try {
					promise->set_value(fn());
				} catch (...) {
					promise->set_exception(std::current_exception());
				}
			});
			if (future.wait_for(a_timeout) != std::future_status::ready) {
				return std::nullopt;
			}
			try {
				return future.get();
			} catch (const std::exception& e) {
				logger::error("main thread task failed: {}", e.what());
				return std::nullopt;
			}
		}

		// ---- jobs -----------------------------------------------------------------------

		std::mutex                                     g_jobLock;
		std::unordered_map<std::uint32_t, std::uint64_t> g_generation;

		std::uint64_t Bump(std::uint32_t a_thread)
		{
			std::lock_guard lock(g_jobLock);
			return ++g_generation[a_thread];
		}

		bool Current(std::uint32_t a_thread, std::uint64_t a_gen)
		{
			std::lock_guard lock(g_jobLock);
			return g_generation[a_thread] == a_gen;
		}

		struct Job
		{
			std::uint32_t                        thread = 0;
			std::string                          sceneID;
			std::uint64_t                        gen = 0;
			std::shared_ptr<const Scenes::Scene> scene;
			std::vector<Solver::Term>            terms;
			std::vector<RE::FormID>              ids;
			std::vector<std::string>             names;
			std::vector<Alignment>               alignments;
			std::vector<Body::ActorInfo>         actors;
			std::vector<int>                     bends;
			std::vector<int>                     sceneBends;
			std::string                          key;
			std::string                          why;  // why Prepare gave up
			float                                sampleSeconds = 2.0f;

			std::mutex                           frameLock;
			std::vector<std::vector<Body::Pose>> frames;
			int                                  rebuilds = 0;  // 3D rebuilds seen while sampling

			bool Alive() const { return Current(thread, gen); }
		};

		// Main thread: is the thread still on this scene, and who is in it.
		bool Prepare(Job& a_job)
		{
			auto& a_why = a_job.why;
			a_job.alignments.clear();
			a_job.ids.clear();
			a_job.names.clear();
			a_job.bends.clear();
			a_job.sceneBends.clear();
			a_job.actors.clear();
			const auto api = OStimAPI::Get();
			if (!api->IsThreadValid(a_job.thread)) {
				a_why = "thread ended";
				return false;
			}
			const auto now = api->GetCurrentSceneID(a_job.thread);
			if (!now || Lower(now) != Lower(a_job.sceneID)) {
				a_why = "scene changed again";
				return false;
			}
			const auto count = api->GetActorCount(a_job.thread);
			std::vector<OStimAPI::ActorData> data(count);
			api->GetActors(a_job.thread, data.data(), count);
			const auto& st = Settings::Get();
			if (!st.npcThreads && std::ranges::none_of(data, [](const auto& d) { return d.formID == 0x14; })) {
				a_why = "NPC scene (npcThreads is off)";
				return false;
			}
			a_job.actors.resize(count);
			for (std::uint32_t i = 0; i < count; ++i) {
				Alignment al{};
				api->GetActorAlignment(a_job.thread, i, &al);
				a_job.alignments.push_back(al);
				a_job.ids.push_back(data[i].formID);
				a_job.bends.push_back(static_cast<int>(std::lround(al.sosBend)));
				a_job.sceneBends.push_back(i < a_job.scene->actors.size() ? a_job.scene->actors[i].sosBend : 0);
				const auto actor = RE::TESForm::LookupByID<RE::Actor>(data[i].formID);
				if (!actor || !Body::Resolve(actor, a_job.actors[i])) {
					a_why = std::format("actor {:08X} has no 3D", data[i].formID);
					return false;
				}
				const char* name = actor->GetDisplayFullName();
				a_job.names.push_back(name && *name ? name : std::format("{:08X}", data[i].formID));
			}
			if (const auto speed = api->GetCurrentSpeed(a_job.thread); a_job.scene->length > 0.0f) {
				float playback = 1.0f;
				if (speed >= 0 && static_cast<std::size_t>(speed) < a_job.scene->playbackSpeeds.size()) {
					playback = std::max(a_job.scene->playbackSpeeds[speed], 0.1f);
				}
				a_job.sampleSeconds = a_job.scene->length / playback;
			}
			a_job.sampleSeconds = std::clamp(a_job.sampleSeconds, st.minSampleSeconds, st.maxSampleSeconds);
			return true;
		}

		bool Wait(const Job& a_job, float a_seconds)
		{
			const auto end = std::chrono::steady_clock::now() + std::chrono::duration<float>(a_seconds);
			while (std::chrono::steady_clock::now() < end) {
				if (!a_job.Alive()) {
					return false;
				}
				std::this_thread::sleep_for(50ms);
			}
			return a_job.Alive();
		}

		// Waits until the scene has settled: at least settleSeconds, the actors no longer being moved
		// into place, and the game not stalling (scene starts stall for seconds while OStim undresses
		// and loads animations). False if the scene changed or it never settled.
		bool WaitUntilSettled(const std::shared_ptr<Job>& a_job)
		{
			const auto& st = Settings::Get();
			if (!Wait(*a_job, st.settleSeconds)) {
				return false;
			}
			const auto  start = std::chrono::steady_clock::now();
			std::vector<RE::NiPoint3> last;
			auto        steadySince = std::chrono::steady_clock::now();
			while (std::chrono::steady_clock::now() - start < 20s) {
				const auto asked = std::chrono::steady_clock::now();
				const auto pos = OnMain([a_job]() {
					std::vector<RE::NiPoint3> out;
					const auto api = OStimAPI::Get();
					const auto count = api->GetActorCount(a_job->thread);
					std::vector<OStimAPI::ActorData> data(count);
					api->GetActors(a_job->thread, data.data(), count);
					for (const auto& d : data) {
						const auto actor = RE::TESForm::LookupByID<RE::Actor>(d.formID);
						out.push_back(actor ? actor->GetPosition() : RE::NiPoint3{});
					}
					return out;
				});
				const auto now = std::chrono::steady_clock::now();
				if (!a_job->Alive()) {
					return false;
				}
				const bool smooth = pos && now - asked < 200ms;
				bool       still = pos && pos->size() == last.size();
				for (std::size_t i = 0; still && i < last.size(); ++i) {
					still = (*pos)[i].GetDistance(last[i]) < 0.5f;
				}
				if (!smooth || !still) {
					steadySince = now;
				} else if (now - steadySince >= 1000ms) {
					return true;
				}
				if (pos) {
					last = *pos;
				}
				std::this_thread::sleep_for(250ms);
			}
			return false;
		}

		// Reads every actor's nodes about 30 times a second for a_seconds.
		std::vector<std::vector<Body::Pose>> Sample(const std::shared_ptr<Job>& a_job, float a_seconds)
		{
			{
				std::lock_guard lock(a_job->frameLock);
				a_job->frames.clear();
			}
			const auto end = std::chrono::steady_clock::now() + std::chrono::duration<float>(a_seconds);
			while (std::chrono::steady_clock::now() < end && a_job->Alive()) {
				SKSE::GetTaskInterface()->AddTask([a_job]() {
					// an actor whose 3D was rebuilt (OStim rescaling, re-equipping...) leaves our nodes
					// detached and frozen: pick up the new ones and start the sample over
					bool rebuilt = false;
					for (auto& info : a_job->actors) {
						rebuilt |= Body::Refresh(info);
					}
					std::vector<Body::Pose> frame(a_job->actors.size());
					for (std::size_t i = 0; i < frame.size(); ++i) {
						Body::Read(a_job->actors[i], frame[i]);
					}
					std::lock_guard lock(a_job->frameLock);
					if (rebuilt) {
						a_job->frames.clear();
						a_job->rebuilds += 1;
					}
					a_job->frames.push_back(std::move(frame));
				});
				std::this_thread::sleep_for(33ms);
			}
			std::this_thread::sleep_for(100ms);
			std::lock_guard lock(a_job->frameLock);
			return a_job->frames;
		}

		Solver::Input MakeInput(Job& a_job, std::vector<std::vector<Body::Pose>> a_frames, const std::vector<int>& a_bends)
		{
			Solver::Input in;
			in.terms = a_job.terms;
			in.actors = &a_job.actors;
			in.frames = std::move(a_frames);
			in.bends = a_bends;
			in.sceneBends = a_job.sceneBends;
			return in;
		}

		std::string Describe(const Alignment& a)
		{
			return std::format("x {:.2f} y {:.2f} z {:.2f} scale {:.3f} bend {:.0f}", a.offsetX, a.offsetY, a.offsetZ, a.scale, a.sosBend);
		}

		// Hands alignments to OStim (only the ones that differ from what it has).
		std::optional<bool> Apply(const std::shared_ptr<Job>& a_job, const std::vector<Alignment>& a_values)
		{
			return OnMain([a_job, a_values]() {
				if (!a_job->Alive()) {
					return false;
				}
				const auto api = OStimAPI::Get();
				for (std::uint32_t i = 0; i < a_values.size(); ++i) {
					Alignment cur{};
					api->GetActorAlignment(a_job->thread, i, &cur);
					if (!Same(cur, a_values[i])) {
						api->SetActorAlignment(a_job->thread, i, &a_values[i]);
					}
				}
				return true;
			});
		}

		void Run(std::shared_ptr<Job> a_job)
		{
			const auto& st = Settings::Get();
			auto&       job = *a_job;
			const auto  tag = std::format("[thread {}] {}", job.thread, job.sceneID);
			if (!WaitUntilSettled(a_job)) {
				logger::debug("{}: skipped (scene changed or never settled)", tag);
				return;
			}
			const auto prepared = OnMain([a_job]() { return Prepare(*a_job); });
			if (!prepared || !*prepared) {
				logger::debug("{}: skipped ({})", tag, prepared ? job.why : "game busy"s);
				return;
			}
			job.terms = Solver::TermsFor(*job.scene, static_cast<int>(job.actors.size()));
			if (job.terms.empty()) {
				logger::debug("{}: no action to align by", tag);
				return;
			}

			// what was done here before, and is the alignment still OStim's default?
			job.key = Lower(job.sceneID);
			for (std::size_t i = 0; i < job.ids.size(); ++i) {
				job.key += std::format("{}{:08X}", i ? "," : "|", job.ids[i]);
			}
			bool allDefault = true;
			for (std::size_t i = 0; i < job.alignments.size(); ++i) {
				const Alignment def{ 0, 0, 0, 1, 0, static_cast<float>(job.sceneBends[i]) };
				allDefault &= Same(job.alignments[i], def);
			}
			if (const auto rec = FindRecord(job.key)) {
				const auto result = rec->value("result", ""s);
				const int  version = rec->value("version", 1);
				bool       matches = rec->contains("alignments") && (*rec)["alignments"].size() == job.alignments.size();
				for (std::size_t i = 0; matches && i < job.alignments.size(); ++i) {
					matches = Same(job.alignments[i], FromJson((*rec)["alignments"][i]));
				}
				if (version < kRecordVersion && matches && result == "applied" && !allDefault) {
					// aligned by an older version: back to OStim's default, then measure again
					std::vector<Alignment> defaults;
					for (std::size_t i = 0; i < job.alignments.size(); ++i) {
						defaults.push_back({ 0, 0, 0, 1, 0, static_cast<float>(job.sceneBends[i]) });
					}
					logger::info("{}: aligned by an older version, resetting and measuring again", tag);
					if (const auto reset = Apply(a_job, defaults); !reset || !*reset || !WaitUntilSettled(a_job)) {
						return;
					}
					const auto again = OnMain([a_job]() { return Prepare(*a_job); });
					if (!again || !*again) {
						return;
					}
				} else if (version < kRecordVersion && allDefault) {
					logger::info("{}: measured by an older version, measuring again", tag);
				} else if (matches) {
					logger::debug("{}: already done ({})", tag, result);
					return;
				} else if (!allDefault) {
					logger::info("{}: alignment changed since this mod set it (by hand?), leaving it", tag);
					return;
				} else {
					logger::info("{}: OStim has no alignment any more for what this mod did before, measuring again", tag);
				}
			} else if (!allDefault && st.respectManualAlignment) {
				logger::info("{}: aligned by hand in OStim, leaving it", tag);
				return;
			}

			const auto& main = job.terms.front();
			logger::info("{}: {} ({} -> {}), measuring {:.1f} s", tag, main.action, job.names[main.a], job.names[main.b], job.sampleSeconds);
			for (std::size_t i = 0; i < job.actors.size(); ++i) {
				const auto& a = job.actors[i];
				if (a.centerKnown) {
					logger::info("{}: {}: shaft mesh centre {:+.2f} across / {:+.2f} sideways of its bones, tip {:.2f} past the end (x GenBase scale)",
						tag, job.names[i], a.centerNormal[0], a.centerLateral[0], a.tipLength);
				}
			}

			const auto enough = static_cast<std::size_t>(job.sampleSeconds * 18.0f);  // ~60% of 30/s
			auto       frames = Sample(a_job, job.sampleSeconds);
			if (job.Alive() && frames.size() < enough) {
				logger::info("{}: game stuttered ({} frames), measuring again", tag, frames.size());
				frames = Sample(a_job, job.sampleSeconds);
			}
			if (!job.Alive() || frames.size() < enough) {
				logger::info("{}: stopped while measuring ({}, {} frames)", tag, job.Alive() ? "too few frames" : "scene changed or ended", frames.size());
				return;
			}
			// the first and second half of a loop should agree; if not, the animation was still blending in
			{
				const auto  half = frames.size() / 2;
				std::string d1, d2;
				const float c1 = Solver::Measure(MakeInput(job, { frames.begin(), frames.begin() + half }, job.bends), d1);
				const float c2 = Solver::Measure(MakeInput(job, { frames.begin() + half, frames.end() }, job.bends), d2);
				if (c1 >= 0 && c2 >= 0 && std::abs(c1 - c2) > 2.0f) {
					logger::info("{}: unsteady ({:.1f} vs {:.1f}), measuring again", tag, c1, c2);
					frames = Sample(a_job, job.sampleSeconds);
					if (!job.Alive() || frames.size() < 10) {
						logger::info("{}: stopped while measuring again", tag);
						return;
					}
				}
			}

			// parts far apart means the animation isn't playing yet (scene start) or something is off
			for (int retry = 0;; ++retry) {
				std::string d;
				const float now = Solver::Measure(MakeInput(job, frames, job.bends), d);
				if (now < 150.0f) {  // ~24 units apart (costs grow with the square)
					break;
				}
				if (retry == 6) {
					logger::info("{}: body parts still far apart ({}), not aligning", tag, d);
					return;
				}
				if (retry == 0) {
					logger::info("{}: body parts far apart ({}), waiting for the scene to play normally", tag, d);
				}
				if (!Wait(job, 2.0f)) {
					return;
				}
				frames = Sample(a_job, job.sampleSeconds);
				if (!job.Alive() || frames.size() < enough) {
					return;
				}
			}

			const auto plan = Solver::Solve(MakeInput(job, frames, job.bends));
			if (!plan.valid) {
				logger::info("{}: the action's body parts can't be measured on these actors", tag);
				Record(job.key, "not measurable", job.alignments);
				return;
			}
			logger::info("{}: now cost {:.2f}: {}", tag, plan.baseline, plan.baselineDetail);
			const float gain = plan.baseline - plan.cost;
			const auto  bendText = plan.bendActor >= 0 ? std::format("bend {} -> {}; ", job.bends[plan.bendActor], plan.bend) : ""s;
			std::string moveText;
			float       farthest = 0.0f;
			for (const auto& m : plan.moves) {
				moveText += std::format("{} by ({:.2f}, {:.2f}, {:.2f}) scale x{:.3f}; ", job.names[m.actor], m.offset.x, m.offset.y, m.offset.z, m.scale);
				farthest = std::max(farthest, m.offset.Length());
			}
			logger::info("{}: best: move {}{}{}cost {:.2f}: {}", tag, moveText.empty() ? "nobody; "s : moveText, bendText,
				plan.hands.empty() ? "" : "hands " + plan.hands + "; ", plan.cost, plan.detail);
			if (!plan.keepBend.empty() && plan.bendActor >= 0 && plan.bend != job.bends[plan.bendActor]) {
				logger::info("{}: best keeping the bend: {}", tag, plan.keepBend);
			}
			if (farthest > 0.9f * st.maxOffset) {
				logger::info("{}: needs a move near the {:.0f}-unit limit, not trusting it", tag, st.maxOffset);
				Record(job.key, "too far", job.alignments, { { "cost", plan.baseline } });
				return;
			}
			if (gain < st.minImprovement) {
				logger::info("{}: gain {:.2f} < {:.2f}, leaving it", tag, gain, st.minImprovement);
				Record(job.key, "unchanged", job.alignments, { { "cost", plan.baseline } });
				return;
			}

			// into OStim's frame: x to the right of the thread centre, y forward
			auto updated = job.alignments;
			for (const auto& m : plan.moves) {
				auto&       al = updated[m.actor];
				const float sceneR = m.actor < static_cast<int>(job.scene->actors.size()) ? job.scene->actors[m.actor].offsetR : 0.0f;
				const float centerR = job.actors[m.actor].heading - (sceneR + al.rotation) * 0.0174532925f;
				const float c = std::cos(centerR), s = std::sin(centerR);
				al.offsetX += c * m.offset.x - s * m.offset.y;
				al.offsetY += s * m.offset.x + c * m.offset.y;
				al.offsetZ += m.offset.z;
				al.scale *= m.scale;
			}
			if (plan.bendActor >= 0) {
				updated[plan.bendActor].sosBend = static_cast<float>(plan.bend);
			}
			const auto applied = Apply(a_job, updated);
			if (!applied || !*applied) {
				return;
			}
			for (std::size_t i = 0; i < updated.size(); ++i) {
				if (!Same(updated[i], job.alignments[i])) {
					logger::info("{}: applied to {}: {}", tag, job.names[i], Describe(updated[i]));
				}
			}
			json extra = { { "cost_before", plan.baseline }, { "cost_planned", plan.cost } };
			Record(job.key, "applied", updated, extra);

			if (!st.verify) {
				return;
			}
			if (!WaitUntilSettled(a_job)) {
				return;
			}
			std::vector<int> bends = job.bends;
			if (plan.bendActor >= 0) {
				bends[plan.bendActor] = plan.bend;
			}
			std::string detail;
			float       measured = -1.0f;
			for (int retry = 0; retry < 7 && job.Alive(); ++retry) {
				const auto after = Sample(a_job, std::min(job.sampleSeconds, 3.0f));
				if (after.size() >= 10) {
					measured = Solver::Measure(MakeInput(job, after, bends), detail);
					if (measured >= 0.0f && measured < 150.0f) {
						break;  // a normal reading
					}
				}
				measured = -1.0f;
				if (!Wait(job, 2.0f)) {
					return;
				}
			}
			if (measured < 0) {
				logger::info("{}: could not check the result (the scene never played normally), keeping it", tag);
				return;
			}
			logger::info("{}: measured after: cost {:.2f}: {}", tag, measured, detail);
			extra["cost_measured"] = measured;
			if (measured > plan.baseline - 0.25f * gain || measured > plan.predicted + std::max(2.0f, 0.5f * gain)) {
				logger::warn("{}: not as planned (measured {:.2f}, planned {:.2f}, before {:.2f}), undoing", tag, measured, plan.predicted, plan.baseline);
				if (const auto undone = Apply(a_job, job.alignments); undone && *undone) {
					Record(job.key, "reverted", job.alignments, extra);
				}
				return;
			}
			Record(job.key, "applied", updated, extra);
		}

		void Schedule(std::uint32_t a_thread, std::string a_sceneID)
		{
			const auto gen = Bump(a_thread);
			if (!Settings::Get().enabled || !OStimAPI::Get()) {
				return;
			}
			auto scene = Scenes::Get(a_sceneID);
			if (!scene) {
				logger::debug("[thread {}] {}: no scene file", a_thread, a_sceneID);
				return;
			}
			if (scene->transition) {
				return;
			}
			auto job = std::make_shared<Job>();
			job->thread = a_thread;
			job->sceneID = std::move(a_sceneID);
			job->gen = gen;
			job->scene = std::move(scene);
			std::thread([job]() {
				try {
					Run(job);
				} catch (const std::exception& e) {
					logger::error("[thread {}] {}: {}", job->thread, job->sceneID, e.what());
				}
			}).detach();
		}

		class EventSink : public RE::BSTEventSink<SKSE::ModCallbackEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* a_event, RE::BSTEventSource<SKSE::ModCallbackEvent>*) override
			{
				if (!a_event) {
					return RE::BSEventNotifyControl::kContinue;
				}
				const std::string_view name = a_event->eventName.c_str() ? a_event->eventName.c_str() : "";
				const auto             thread = static_cast<std::uint32_t>(std::lround(a_event->numArg));
				if (name == "ostim_thread_scenechanged") {
					logger::debug("[thread {}] scene changed to {}", thread, a_event->strArg.c_str() ? a_event->strArg.c_str() : "");
					Schedule(thread, a_event->strArg.c_str() ? a_event->strArg.c_str() : "");
				} else if (name == "ostim_thread_end") {
					Bump(thread);
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	void Init()
	{
		LoadRecords();
		static EventSink sink;
		SKSE::GetModCallbackEventSource()->AddEventSink(&sink);
		logger::info("listening for OStim scene changes");
	}
}
