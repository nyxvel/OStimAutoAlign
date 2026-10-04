#include "Settings.h"

Settings& Settings::Get()
{
	static Settings s;
	return s;
}

void Settings::Load()
{
	const fs::path path = "Data/SKSE/Plugins/OStimAutoAlign/settings.json";
	std::ifstream  in(path);
	if (!in) {
		logger::info("no {}, using defaults", path.string());
		return;
	}
	const auto j = json::parse(in, nullptr, false, true);
	if (j.is_discarded() || !j.is_object()) {
		logger::warn("{} is not valid JSON, using defaults", path.string());
		return;
	}
	const auto get = [&](const char* a_key, auto& a_value) {
		if (j.contains(a_key)) {
			try {
				a_value = j[a_key].get<std::remove_reference_t<decltype(a_value)>>();
			} catch (const std::exception& e) {
				logger::warn("settings: {}: {}", a_key, e.what());
			}
		}
	};
	get("enabled", enabled);
	get("npcThreads", npcThreads);
	get("settleSeconds", settleSeconds);
	get("minSampleSeconds", minSampleSeconds);
	get("maxSampleSeconds", maxSampleSeconds);
	get("adjustBend", adjustBend);
	get("adjustScale", adjustScale);
	get("scaleMin", scaleMin);
	get("scaleMax", scaleMax);
	get("maxOffset", maxOffset);
	get("minImprovement", minImprovement);
	get("verify", verify);
	get("respectManualAlignment", respectManualAlignment);
	get("costHeight", costHeight);
	get("costSink", costSink);
	get("costScalePercent", costScalePercent);
	get("costSlide", costSlide);
	get("costBendStep", costBendStep);
	get("angleDeadzone", angleDeadzone);
	get("anglePerUnit", anglePerUnit);
	get("secondaryWeight", secondaryWeight);
	get("engagedDistance", engagedDistance);
	get("logLevel", logLevel);
	logger::info("settings loaded from {}", path.string());
}
