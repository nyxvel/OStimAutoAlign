// OStim Auto Align
//
// OStim places every actor of a scene at the same spot and plays animations made for some other
// body: other heights, another schlong size and angle. Hands miss the shaft, the shaft misses the
// opening. OStim's align menu fixes that per actor pair and scene, by hand.
//
// This plugin does it automatically: when a scene starts it reads what the scene does (OStim's
// actions: handjob, vaginalsex, blowjob, kissing...), measures the body parts those actions use
// (hand, mouth, shaft, vagina/anus bones) while the animation loops, works out the smallest change
// that brings them together (one actor's offset and scale, the schlong's SOSBend) and hands it to
// OStim's alignment. OStim then stores and reapplies it like a hand-made alignment.

#include "Aligner.h"
#include "Body.h"
#include "OStimAPI.h"
#include "Scenes.h"
#include "Settings.h"

namespace
{
	void InitializeLog()
	{
		auto path = logger::log_directory();
		if (!path) {
			SKSE::stl::report_and_fail("Failed to find the SKSE log directory");
		}
		*path /= "OStimAutoAlign.log";
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
		auto log = std::make_shared<spdlog::logger>("global", std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::trace);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%H:%M:%S.%e] [%l] %v");
	}

	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		if (a_msg->type != SKSE::MessagingInterface::kDataLoaded) {
			return;
		}
		auto& settings = Settings::Get();
		settings.Load();
		spdlog::default_logger()->set_level(spdlog::level::from_str(settings.logLevel));
		if (!OStimAPI::Get()) {
			logger::error("OStim's thread API not found (OStim Standalone 7.5 or newer needed): disabled");
			return;
		}
		if (!Body::LoadShapes()) {
			settings.adjustBend = false;
		}
		Scenes::Init();
		Aligner::Init();
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	InitializeLog();
	const auto plugin = SKSE::PluginDeclaration::GetSingleton();
	logger::info("{} {} loading", plugin->GetName(), plugin->GetVersion().string("."));
	SKSE::Init(a_skse);
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	return true;
}
