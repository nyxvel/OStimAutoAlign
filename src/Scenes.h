#pragma once

// What a scene does, from OStim's own files: Data\SKSE\Plugins\OStim\scenes\**\<id>.json and
// ...\OStim\actions\*.json (for action aliases).
namespace Scenes
{
	struct Actor
	{
		int         sosBend = 0;
		float       offsetR = 0.0f;  // degrees, the scene's own rotation offset for this actor
		std::string intendedSex;
	};

	struct Action
	{
		std::string type;  // canonical action name (aliases resolved), lower case
		int         actor = 0;
		int         target = 0;
	};

	struct Scene
	{
		std::string         id;
		float               length = 0.0f;
		std::vector<float>  playbackSpeeds;
		bool                transition = false;
		std::vector<Actor>  actors;
		std::vector<Action> actions;
	};

	// Builds the scene index and reads the action aliases. Call once data is loaded.
	void Init();

	// nullptr if there is no such scene file.
	std::shared_ptr<const Scene> Get(std::string_view a_sceneID);
}
