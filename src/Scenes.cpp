#include "Scenes.h"

namespace Scenes
{
	namespace
	{
		const fs::path kRoot = "Data/SKSE/Plugins/OStim";

		std::mutex                                              g_lock;
		std::unordered_map<std::string, fs::path>               g_files;    // lower-case id -> file
		std::unordered_map<std::string, std::string>            g_aliases;  // alias or name -> name
		std::unordered_map<std::string, std::shared_ptr<Scene>> g_cache;

		std::string Lower(std::string_view a_str)
		{
			std::string out(a_str);
			std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		std::optional<json> ReadJson(const fs::path& a_path)
		{
			std::ifstream in(a_path, std::ios::binary);
			if (!in) {
				return std::nullopt;
			}
			std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			if (text.starts_with("\xEF\xBB\xBF")) {
				text.erase(0, 3);
			}
			auto j = json::parse(text, nullptr, false, true);
			if (j.is_discarded()) {
				return std::nullopt;
			}
			return j;
		}

		std::shared_ptr<Scene> Parse(const std::string& a_id, const json& a_json)
		{
			auto scene = std::make_shared<Scene>();
			scene->id = a_id;
			scene->length = a_json.value("length", 0.0f);
			scene->transition = a_json.contains("destination") && a_json["destination"].is_string() && !a_json["destination"].get<std::string>().empty();
			if (a_json.contains("speeds") && a_json["speeds"].is_array()) {
				for (const auto& s : a_json["speeds"]) {
					scene->playbackSpeeds.push_back(s.is_object() ? s.value("playbackSpeed", 1.0f) : 1.0f);
				}
			}
			if (a_json.contains("actors") && a_json["actors"].is_array()) {
				for (const auto& a : a_json["actors"]) {
					Actor actor;
					if (a.is_object()) {
						actor.sosBend = a.value("sosBend", 0);
						actor.intendedSex = Lower(a.value("intendedSex", ""s));
						if (a.contains("offset") && a["offset"].is_object()) {
							actor.offsetR = a["offset"].value("r", 0.0f);
						}
					}
					scene->actors.push_back(std::move(actor));
				}
			}
			if (a_json.contains("actions") && a_json["actions"].is_array()) {
				for (const auto& a : a_json["actions"]) {
					if (!a.is_object() || !a.contains("type") || !a["type"].is_string()) {
						continue;
					}
					Action action;
					const auto type = Lower(a["type"].get<std::string>());
					const auto alias = g_aliases.find(type);
					action.type = alias != g_aliases.end() ? alias->second : type;
					action.actor = a.value("actor", 0);
					action.target = a.value("target", action.actor);
					scene->actions.push_back(std::move(action));
				}
			}
			return scene;
		}
	}

	void Init()
	{
		std::lock_guard lock(g_lock);
		std::error_code ec;
		for (const auto& entry : fs::directory_iterator(kRoot / "actions", ec)) {
			if (entry.path().extension() != ".json") {
				continue;
			}
			const auto name = Lower(entry.path().stem().string());
			g_aliases[name] = name;
			if (const auto j = ReadJson(entry.path()); j && j->contains("aliases") && (*j)["aliases"].is_array()) {
				for (const auto& alias : (*j)["aliases"]) {
					if (alias.is_string()) {
						g_aliases.try_emplace(Lower(alias.get<std::string>()), name);
					}
				}
			}
		}
		for (auto it = fs::recursive_directory_iterator(kRoot / "scenes", ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
			if (it->is_regular_file() && it->path().extension() == ".json") {
				g_files.try_emplace(Lower(it->path().stem().string()), it->path());
			}
		}
		logger::info("{} scene files, {} action names/aliases", g_files.size(), g_aliases.size());
	}

	std::shared_ptr<const Scene> Get(std::string_view a_sceneID)
	{
		const auto      id = Lower(a_sceneID);
		std::lock_guard lock(g_lock);
		if (const auto it = g_cache.find(id); it != g_cache.end()) {
			return it->second;
		}
		const auto file = g_files.find(id);
		if (file == g_files.end()) {
			return nullptr;
		}
		const auto j = ReadJson(file->second);
		if (!j) {
			logger::warn("could not read {}", file->second.string());
			return nullptr;
		}
		auto scene = Parse(std::string(a_sceneID), *j);
		g_cache[id] = scene;
		return scene;
	}
}
