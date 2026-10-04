#include "Body.h"

namespace Body
{
	namespace
	{
		constexpr const char* kNodeNames[kNodeCount] = {
			"NPC GenitalsBase [GenBase]",
			"NPC Genitals01 [Gen01]",
			"NPC Genitals02 [Gen02]",
			"NPC Genitals03 [Gen03]",
			"NPC Genitals04 [Gen04]",
			"NPC Genitals05 [Gen05]",
			"NPC Genitals06 [Gen06]",
			"NPC GenitalsScrotum [GenScrot]",
			"NPC R Hand [RHnd]",
			"NPC R Finger11 [RF11]",
			"NPC R Finger21 [RF21]",
			"NPC R Finger31 [RF31]",
			"NPC L Hand [LHnd]",
			"NPC L Finger11 [LF11]",
			"NPC L Finger21 [LF21]",
			"NPC L Finger31 [LF31]",
			"Mouth00",
			"MouthU",
			"MouthD",
			"MouthL",
			"MouthR",
			"VaginaB1",
			"VaginaDeep1",
			"NPC Anus00",
			"NPC Anus Deep1",
			"CME R Toe0 [RToe]",
			"NPC R Foot [Rft ]",
			"CME L Toe0 [LToe]",
			"NPC L Foot [Lft ]",
			"NPC L Breast",
			"NPC R Breast",
		};

		// [table][bend + 9][Gen01..Gen06], in GenBase space divided by GenBase's world scale
		using ShapeTable = std::array<std::array<Vec, 6>, 19>;
		std::array<ShapeTable, 2> g_shapes{};
		std::array<float, 2>      g_defaultTip{ 0.74f, 0.38f };  // x Gen01-Gen06 length, if the mesh can't be read
		bool                      g_shapesLoaded = false;

		Vec Rotate(const RE::NiMatrix3& a_m, const Vec& a_v)
		{
			return { a_m.entry[0][0] * a_v.x + a_m.entry[0][1] * a_v.y + a_m.entry[0][2] * a_v.z,
				a_m.entry[1][0] * a_v.x + a_m.entry[1][1] * a_v.y + a_m.entry[1][2] * a_v.z,
				a_m.entry[2][0] * a_v.x + a_m.entry[2][1] * a_v.y + a_m.entry[2][2] * a_v.z };
		}

		Vec RotateT(const RE::NiMatrix3& a_m, const Vec& a_v)
		{
			return { a_m.entry[0][0] * a_v.x + a_m.entry[1][0] * a_v.y + a_m.entry[2][0] * a_v.z,
				a_m.entry[0][1] * a_v.x + a_m.entry[1][1] * a_v.y + a_m.entry[2][1] * a_v.z,
				a_m.entry[0][2] * a_v.x + a_m.entry[1][2] * a_v.y + a_m.entry[2][2] * a_v.z };
		}

		Vec Apply(const RE::NiTransform& a_t, const Vec& a_v)
		{
			return Rotate(a_t.rotate, a_v) * a_t.scale + a_t.translate;
		}

		float Half(std::uint16_t a_h)
		{
			const std::uint32_t sign = (a_h & 0x8000u) << 16;
			std::uint32_t       exp = (a_h >> 10) & 0x1F;
			std::uint32_t       mant = a_h & 0x3FF;
			std::uint32_t       bits;
			if (exp == 0) {
				if (mant == 0) {
					bits = sign;
				} else {
					exp = 127 - 15 + 1;
					while (!(mant & 0x400)) {
						mant <<= 1;
						--exp;
					}
					mant &= 0x3FF;
					bits = sign | (exp << 23) | (mant << 13);
				}
			} else if (exp == 31) {
				bits = sign | 0x7F800000u | (mant << 13);
			} else {
				bits = sign | ((exp + 127 - 15) << 23) | (mant << 13);
			}
			return std::bit_cast<float>(bits);
		}

		Vec Lateral(const RE::NiTransform& a_genBase)
		{
			Vec l{ a_genBase.rotate.entry[0][0], a_genBase.rotate.entry[1][0], a_genBase.rotate.entry[2][0] };
			l.Unitize();
			return l;
		}

		// Chain frame at bone i of a 6-point chain: along the shaft, and across it in the bend plane.
		void Frame(const std::array<Vec, 6>& a_bones, int a_i, const Vec& a_lateral, Vec& a_along, Vec& a_normal)
		{
			a_along = a_bones[std::min(a_i + 1, 5)] - a_bones[std::max(a_i - 1, 0)];
			a_along.Unitize();
			a_normal = a_lateral.Cross(a_along);
			a_normal.Unitize();
		}

		// The visible shaft from the skinned genital mesh: where its centre line runs relative to the
		// bones (on some meshes, UBE's futa addon for one, the bones run along the top surface) and
		// how far the tip reaches past Gen06. Vertices are placed with their main bone, which is
		// close enough for centres. Offsets are stored / GenBase scale.
		void MeshShaft(RE::NiAVObject* a_root, ActorInfo& a_info)
		{
			const auto base = a_info.nodes[GenBase].get();
			if (!base || !a_info.HasShaft()) {
				return;
			}
			std::array<Vec, 6>               bones;
			std::array<RE::BSFixedString, 6> names;
			for (int i = 0; i < 6; ++i) {
				bones[i] = a_info.nodes[Gen01 + i]->world.translate;
				names[i] = kNodeNames[Gen01 + i];
			}
			const Vec   lat = Lateral(base->world);
			const float sb = base->world.scale;

			struct Found
			{
				std::array<Vec, 6> sum{};
				std::array<int, 6> count{};
				std::vector<Vec>   tip;
				int                total = 0;
			} best;
			RE::BSVisit::TraverseScenegraphGeometries(a_root, [&](RE::BSGeometry* a_geom) -> RE::BSVisit::BSVisitControl {
				auto&      data = a_geom->GetGeometryRuntimeData();
				const auto skin = data.skinInstance.get();
				const auto skinData = skin ? skin->skinData.get() : nullptr;
				const auto partition = skin ? skin->skinPartition.get() : nullptr;
				if (!skinData || !partition || partition->numPartitions == 0 || !skin->bones) {
					return RE::BSVisit::BSVisitControl::kContinue;
				}
				std::vector<int> genOf(skinData->GetBoneCount(), -1);  // skin bone -> 0..5
				bool             any = false;
				for (std::uint32_t b = 0; b < genOf.size(); ++b) {
					for (int i = 0; i < 6 && skin->bones[b]; ++i) {
						if (skin->bones[b]->name == names[i]) {
							genOf[b] = i;
							any = true;
						}
					}
				}
				const auto& first = partition->partitions[0];
				if (!any || !first.buffData || !first.buffData->rawVertexData) {
					return RE::BSVisit::BSVisitControl::kContinue;
				}
				const auto desc = *reinterpret_cast<const std::uint64_t*>(&first.vertexDesc);
				const auto stride = static_cast<std::uint32_t>(desc & 0xF) * 4;
				const auto flags = first.vertexDesc.GetFlags();
				if (!(flags & RE::BSGraphics::Vertex::VF_SKINNED) || stride == 0) {
					return RE::BSVisit::BSVisitControl::kContinue;
				}
				std::uint32_t posBytes = 0;
				for (int a = RE::BSGraphics::Vertex::VA_TEXCOORD0; a <= RE::BSGraphics::Vertex::VA_SKINNING && !posBytes; ++a) {
					if (flags & (1 << a)) {
						posBytes = first.vertexDesc.GetAttributeOffset(static_cast<RE::BSGraphics::Vertex::Attribute>(a));
					}
				}
				const bool fullPrec = posBytes ? posBytes >= 16 : (flags & RE::BSGraphics::Vertex::VF_FULLPREC) != 0;
				const auto skinOffset = first.vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_SKINNING);
				const auto raw = first.buffData->rawVertexData;
				const auto dyn = netimmerse_cast<RE::BSDynamicTriShape*>(a_geom);
				const auto dynData = dyn ? static_cast<const float*>(dyn->GetDynamicTrishapeRuntimeData().dynamicData) : nullptr;

				Found here;
				for (std::uint32_t v = 0; v < partition->vertexCount; ++v) {
					const auto vert = raw + static_cast<std::size_t>(v) * stride;
					const auto weights = reinterpret_cast<const std::uint16_t*>(vert + skinOffset);
					const auto indices = vert + skinOffset + 8;
					int        main = -1;
					float      mainW = 0.0f;
					for (int k = 0; k < first.bonesPerVertex && k < 4; ++k) {
						const float w = Half(weights[k]);
						if (w > mainW && indices[k] < genOf.size()) {
							mainW = w;
							main = indices[k];
						}
					}
					if (main < 0 || genOf[main] < 0) {
						continue;
					}
					Vec p;
					if (dynData) {
						p = { dynData[v * 4], dynData[v * 4 + 1], dynData[v * 4 + 2] };
					} else if (fullPrec) {
						const auto f = reinterpret_cast<const float*>(vert);
						p = { f[0], f[1], f[2] };
					} else {
						const auto h = reinterpret_cast<const std::uint16_t*>(vert);
						p = { Half(h[0]), Half(h[1]), Half(h[2]) };
					}
					const int  gi = genOf[main];
					const auto world = Apply(skin->bones[main]->world, Apply(skinData->GetBoneDataSkinToBone(main), p));
					here.sum[gi] += world;
					here.count[gi] += 1;
					here.total += 1;
					if (gi == 5) {
						here.tip.push_back(world);
					}
				}
				if (here.total > best.total) {
					best = std::move(here);
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			if (best.total < 50) {
				return;
			}
			// One offset for the whole shaft, from Gen01-05: their vertices sit around their bones.
			// Gen06's are the whole glans, several units ahead of the bone, where a small angle between
			// the bone and the chain would turn into a large, wrong offset.
			float normalSum = 0.0f, lateralSum = 0.0f;
			int   used = 0;
			for (int i = 0; i < 5; ++i) {
				if (best.count[i] < 5) {
					continue;
				}
				Vec along, normal;
				Frame(bones, i, lat, along, normal);
				const Vec d = best.sum[i] / static_cast<float>(best.count[i]) - bones[i];
				normalSum += d.Dot(normal) / sb * static_cast<float>(best.count[i]);
				lateralSum += d.Dot(lat) / sb * static_cast<float>(best.count[i]);
				used += best.count[i];
			}
			if (used > 0) {
				a_info.centerNormal.fill(normalSum / static_cast<float>(used));
				a_info.centerLateral.fill(lateralSum / static_cast<float>(used));
			}
			Vec along, normal;
			Frame(bones, 5, lat, along, normal);
			along = bones[5] - bones[4];  // the tip goes on along the last bone segment
			along.Unitize();
			const Vec centre6 = bones[5] + (normal * a_info.centerNormal[5] + lat * a_info.centerLateral[5]) * sb;
			if (best.tip.size() >= 10) {
				std::vector<float> reach;
				for (const auto& w : best.tip) {
					reach.push_back((w - centre6).Dot(along));
				}
				std::ranges::sort(reach);
				a_info.tipLength = reach[static_cast<std::size_t>(reach.size() * 0.98)] / sb;  // ignore a few stray vertices
			}
			a_info.centerKnown = true;
			logger::debug("shaft mesh: {} vertices on the genital bones, centre {:+.2f} across / {:+.2f} sideways of the bones, tip {:.2f} (x GenBase scale)",
				best.total, a_info.centerNormal[0], a_info.centerLateral[0], a_info.tipLength);
		}

		Vec Mean(std::initializer_list<Vec> a_points)
		{
			Vec sum;
			for (const auto& p : a_points) {
				sum += p;
			}
			return sum / static_cast<float>(a_points.size());
		}
	}

	bool ActorInfo::HasShaft() const
	{
		for (int n = GenBase; n <= Gen06; ++n) {
			if (!nodes[n]) {
				return false;
			}
		}
		return true;
	}

	bool Resolve(RE::Actor* a_actor, ActorInfo& a_info)
	{
		const auto root = a_actor ? a_actor->Get3D(false) : nullptr;
		if (!root) {
			return false;
		}
		a_info.handle = a_actor->GetHandle();
		a_info.root3D.reset(root);
		for (int n = 0; n < kNodeCount; ++n) {
			a_info.nodes[n].reset(root->GetObjectByName(RE::BSFixedString(kNodeNames[n])));
		}
		a_info.root = a_actor->GetPosition();
		a_info.heading = a_actor->GetAngleZ();
		const auto base = a_actor->GetActorBase();
		a_info.female = base && base->GetSex() == RE::SEX::kFemale;
		a_info.tipLength = -1.0f;
		a_info.centerNormal.fill(0.0f);
		a_info.centerLateral.fill(0.0f);
		a_info.centerKnown = false;
		MeshShaft(root, a_info);
		return true;
	}

	bool Refresh(ActorInfo& a_info)
	{
		const auto actor = a_info.handle.get();
		const auto root = actor ? actor->Get3D(false) : nullptr;
		if (!root || root == a_info.root3D.get()) {
			return false;
		}
		Resolve(actor.get(), a_info);
		return true;
	}

	void Read(const ActorInfo& a_info, Pose& a_pose)
	{
		a_pose.valid = 0;
		for (int n = 0; n < kNodeCount; ++n) {
			if (const auto& node = a_info.nodes[n]) {
				a_pose.t[n] = node->world;
				a_pose.valid |= 1ull << n;
			}
		}
	}

	bool LoadShapes()
	{
		const fs::path path = "Data/SKSE/Plugins/OStimAutoAlign/tng_bend_shapes.json";
		std::ifstream  in(path);
		const auto     j = in ? json::parse(in, nullptr, false) : json();
		if (j.is_discarded() || !j.is_object()) {
			logger::error("could not read {}: schlong bends can't be predicted", path.string());
			return false;
		}
		try {
			const char* names[2] = { "female", "male" };
			for (int t = 0; t < 2; ++t) {
				for (int bend = -9; bend <= 9; ++bend) {
					const auto& pts = j.at(names[t]).at(std::to_string(bend));
					for (int i = 0; i < 6; ++i) {
						g_shapes[t][bend + 9][i] = { pts.at(i).at(0).get<float>(), pts.at(i).at(1).get<float>(), pts.at(i).at(2).get<float>() };
					}
				}
				if (j.contains("tip_factor") && j["tip_factor"].contains(names[t])) {
					g_defaultTip[t] = j["tip_factor"][names[t]].get<float>();
				}
			}
		} catch (const std::exception& e) {
			logger::error("{}: {}", path.string(), e.what());
			return false;
		}
		g_shapesLoaded = true;
		return true;
	}

	std::optional<ShaftModel> FitShaft(const std::vector<Pose>& a_frames, const ActorInfo& a_info, int a_currentBend)
	{
		if (!g_shapesLoaded || a_frames.empty() || a_currentBend < -9 || a_currentBend > 9) {
			return std::nullopt;
		}
		std::array<Vec, 6> local{};
		int                count = 0;
		for (const auto& f : a_frames) {
			if (!f.Has(GenBase) || !f.Has(Gen06)) {
				continue;
			}
			const auto& b = f.t[GenBase];
			for (int i = 0; i < 6; ++i) {
				local[i] += RotateT(b.rotate, f.P(static_cast<Node>(Gen01 + i)) - b.translate) / b.scale;
			}
			++count;
		}
		if (count == 0) {
			return std::nullopt;
		}
		for (auto& p : local) {
			p /= static_cast<float>(count);
		}
		std::optional<ShaftModel> best;
		for (int t = 0; t < 2; ++t) {
			const auto& tab = g_shapes[t][a_currentBend + 9];
			const float tabLen = (tab[5] - tab[0]).Length();
			const float size = tabLen > 1e-3f ? (local[5] - local[0]).Length() / tabLen : 1.0f;
			float       err = 0.0f;
			for (int i = 0; i < 6; ++i) {
				err += (local[i] - tab[i] * size).Length() / 6.0f;
			}
			if (!best || err < best->fitError) {
				best = ShaftModel{ t, size, a_info.tipLength >= 0.0f ? a_info.tipLength : g_defaultTip[t] * tabLen * size, err,
					a_info.centerNormal, a_info.centerLateral };
			}
		}
		return best;
	}

	namespace
	{
		// Bone chain -> the visible centre line + tip.
		Chain Centre(const std::array<Vec, 6>& a_bones, const RE::NiTransform& a_genBase, const ShaftModel& a_model)
		{
			const Vec lat = Lateral(a_genBase);
			Chain     c{};
			for (int i = 0; i < 6; ++i) {
				Vec along, normal;
				Frame(a_bones, i, lat, along, normal);
				c[i] = a_bones[i] + (normal * a_model.centerNormal[i] + lat * a_model.centerLateral[i]) * a_genBase.scale;
			}
			auto last = a_bones[5] - a_bones[4];  // along the bones, not the offset points
			last.Unitize();
			c[6] = c[5] + last * (a_model.tipLength * a_genBase.scale);
			return c;
		}
	}

	Chain ShaftAt(const Pose& a_pose, const ShaftModel& a_model, int a_bend)
	{
		a_bend = std::clamp(a_bend, -9, 9);
		const auto&        b = a_pose.t[GenBase];
		const auto&        tab = g_shapes[a_model.table][a_bend + 9];
		std::array<Vec, 6> bones{};
		for (int i = 0; i < 6; ++i) {
			bones[i] = b.translate + Rotate(b.rotate, tab[i] * a_model.size) * b.scale;
		}
		return Centre(bones, b, a_model);
	}

	Chain ShaftNow(const Pose& a_pose, const ShaftModel& a_model)
	{
		std::array<Vec, 6> bones{};
		for (int i = 0; i < 6; ++i) {
			bones[i] = a_pose.P(static_cast<Node>(Gen01 + i));
		}
		return Centre(bones, a_pose.t[GenBase], a_model);
	}

	bool HasHand(const Pose& a_pose, bool a_right)
	{
		return a_right ? a_pose.Has(RHand) && a_pose.Has(RF11) && a_pose.Has(RF21) && a_pose.Has(RF31) :
		                 a_pose.Has(LHand) && a_pose.Has(LF11) && a_pose.Has(LF21) && a_pose.Has(LF31);
	}

	Vec HandPoint(const Pose& a_pose, bool a_right)
	{
		// grip centre: between the palm and the first finger joints
		return a_right ? Mean({ a_pose.P(RHand), a_pose.P(RF11), a_pose.P(RF21), a_pose.P(RF31) }) :
		                 Mean({ a_pose.P(LHand), a_pose.P(LF11), a_pose.P(LF21), a_pose.P(LF31) });
	}
}
