#include "Config.hpp"
#include "default-config.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>

Config::Config() = default;

Config::~Config() = default;

std::string Config::user_config_path() const
{
	const char *home = std::getenv("HOME");
	if (!home || !*home) {
		return std::string();
	}
	return std::string(home) + "/.config/yarfwm/config.json";
}

bool Config::write_default_if_missing() const
{
	const std::string target = user_config_path();
	if (target.empty()) {
		return false;
	}

	std::ifstream existing(target);
	if (existing.good()) {
		return false;
	}

	const std::string directory =
	    target.substr(0, target.find_last_of('/'));
	if (directory.empty()) {
		return false;
	}

	std::error_code error;
	std::filesystem::create_directories(directory, error);
	if (error) {
		return false;
	}

	// Dynamic memory: the default configuration text is a static string
	// that lives in the executable image; it is streamed straight to the
	// new file.
	std::ofstream output(target);
	if (!output.good()) {
		return false;
	}

	output << default_config_json;
	return output.good();
}

bool Config::parse_file(const std::string &path)
{
	std::ifstream file(path);
	if (!file.is_open()) {
		return false;
	}

	Json::CharReaderBuilder builder;
	Json::Value root;
	std::string errors;
	if (!parseFromStream(builder, file, &root, &errors)) {
		return false;
	}

	layout = root.get("layout", Json::Value());
	input = root.get("input", Json::Value());
	outputs = root.get("outputs", Json::Value());

	workspaces.clear();
	const Json::Value &workspaces_value = root["workspaces"];
	if (workspaces_value.isArray()) {
		for (const auto &item : workspaces_value) {
			if (item.isString()) {
				workspaces.push_back(item.asString());
			}
		}
	}

	spawn_at_startup.clear();
	const Json::Value &spawn_value = root["spawn_at_startup"];
	if (spawn_value.isArray()) {
		for (const auto &item : spawn_value) {
			if (item.isString()) {
				spawn_at_startup.push_back(item.asString());
			}
		}
	}

	window_rules = root.get("window_rules", Json::Value());
	keybinds = root.get("keybinds", Json::Value());
	prefer_no_csd = root.get("prefer_no_csd", true).asBool();
	screenshot_path = root.get("screenshot_path", "").asString();
	return true;
}

bool Config::load(const std::string &path)
{
	const std::vector<std::string> candidates = {
	    path,
	    "./config.json",
	    user_config_path(),
	};

	for (const auto &candidate : candidates) {
		if (candidate.empty()) {
			continue;
		}
		if (parse_file(candidate)) {
			return true;
		}
	}

	// No usable configuration file exists yet: write the built-in default
	// to $HOME/.config/yarfwm/config.json once, then load it.
	if (!write_default_if_missing()) {
		return false;
	}
	return parse_file(user_config_path());
}
