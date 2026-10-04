#pragma once

// Data\SKSE\Plugins\OStimAutoAlign\settings.json. Every field is optional.
struct Settings
{
	bool  enabled = true;
	bool  npcThreads = true;        // also align scenes the player isn't in
	float settleSeconds = 1.2f;     // wait after a scene change before measuring
	float minSampleSeconds = 2.0f;  // sampling time = one animation loop, clamped to these
	float maxSampleSeconds = 4.0f;
	bool  adjustBend = true;        // change the schlong bend (SOSBend) when that fits better
	bool  adjustScale = true;       // change the moved actor's scale instead of lifting it
	float scaleMin = 0.9f;
	float scaleMax = 1.1f;
	float maxOffset = 15.0f;        // never move an actor farther than this (units)
	float minImprovement = 1.0f;    // apply only if the cost drops by at least this much
	bool  verify = true;            // re-measure after applying, undo if it got worse
	bool  respectManualAlignment = true;  // leave scenes alone that were aligned by hand in OStim's menu

	// Cost weights, in "units of misalignment": 1 unit of height offset, 1% of scale, 1 unit of
	// horizontal slide, one bend step away from the scene's own bend.
	float costHeight = 0.5f;  // lifting (floating a little is fine)
	float costSink = 1.0f;    // lowering into the floor
	float costScalePercent = 0.4f;
	float costSlide = 0.15f;
	float costBendStep = 1.0f;
	// The shaft should point along the canal / into the mouth / through the fist: each degree past
	// angleDeadzone costs 1/anglePerUnit.
	float angleDeadzone = 20.0f;
	float anglePerUnit = 15.0f;
	float secondaryWeight = 0.3f;   // weight of the scene's other actions (kissing during sex...)
	float engagedDistance = 6.0f;   // a part farther than this from its target counts as busy elsewhere

	std::string logLevel = "info";

	static Settings& Get();
	void             Load();
};
