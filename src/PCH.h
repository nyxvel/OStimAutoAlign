#pragma once

#ifndef NOMINMAX
#	define NOMINMAX
#endif

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/sinks/basic_file_sink.h>

// wingdi.h's GetObject -> GetObjectA macro breaks RE::BSScript::Variable::GetObject.
#ifdef GetObject
#	undef GetObject
#endif

using json = nlohmann::json;
namespace logger = SKSE::log;
namespace fs = std::filesystem;
using namespace std::literals;
