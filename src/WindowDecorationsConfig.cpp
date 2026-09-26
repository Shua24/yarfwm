// yarfwm -- the decoration appearance settings.
//
// Deliberately a SECOND config file, not a section of config.json. The two
// have different consumers and different failure modes: config.json drives
// window-management behaviour (Seat, Keybind), this drives what the titlebar
// looks like. A bad colour here must not be able to break a keybinding, and a
// bad keybinding must not be able to make a titlebar unreadable.
//
// The load path is the same shape as Config::load, and for the same reason:
// a missing file writes the embedded default once and loads it, an existing
// file is NEVER overwritten, and no failure ever propagates to the caller.
// The window manager starts with sane decorations even if this file is
// missing, malformed, or unreadable.
#include "WindowDecorationsConfig.hpp"
#include "default-decorations-config.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>

#include <json/json.h>

// The fallbacks, in one place, so "what does a missing key mean" has exactly
// one answer. These are also the values written to a fresh config file, and
// they are labwc's defaults where labwc has an equivalent
// (labwc src/theme.c:534,558-560,1640-1650).
static WindowDecorationsConfig default_values()
{
	WindowDecorationsConfig values;
	values.enabled = true;
	values.titlebar_height = 26;
	values.border_width = 1;
	values.button_width = 26;
	values.button_spacing = 0;
	values.padding = 0;
	values.titlebar_when_maximized = true;
	values.font = "JetBrains Mono 10";
	values.titlebar_active_color = 0xff203040u;
	values.titlebar_inactive_color = 0xff404040u;
	values.text_active_color = 0xffffffffu;
	values.text_inactive_color = 0xffccccccu;
	values.button_glyph_color = 0xffffffffu;
	values.border_active_color = 0xff5c8fb0u;
	values.border_inactive_color = 0xff404040u;
	return values;
}

uint32_t parse_decoration_color(const std::string &text, uint32_t fallback)
{
	if (text.size() != 8) {
		return fallback;
	}
	uint32_t value = 0;
	for (const char character : text) {
		uint32_t digit = 0;
		if (character >= '0' && character <= '9') {
			digit = static_cast<uint32_t>(character - '0');
		} else if (character >= 'a' && character <= 'f') {
			digit = static_cast<uint32_t>(character - 'a') + 10u;
		} else if (character >= 'A' && character <= 'F') {
			digit = static_cast<uint32_t>(character - 'A') + 10u;
		} else {
			return fallback;
		}
		value = (value << 4) | digit;
	}
	return value;
}

// Reads whichever keys are present over `values`. A key that is absent or of
// the wrong JSON type leaves the fallback in place, so a partial file is not
// an error.
static void apply(const Json::Value &root, WindowDecorationsConfig &values)
{
	if (!root.isObject()) {
		return;
	}
	if (root.isMember("enabled")) {
		values.enabled = root["enabled"].asBool();
	}
	if (root.isMember("titlebar_height")) {
		values.titlebar_height = root["titlebar_height"].asInt();
	}
	if (root.isMember("border_width")) {
		values.border_width = root["border_width"].asInt();
	}
	if (root.isMember("button_width")) {
		values.button_width = root["button_width"].asInt();
	}
	if (root.isMember("button_spacing")) {
		values.button_spacing = root["button_spacing"].asInt();
	}
	if (root.isMember("padding")) {
		values.padding = root["padding"].asInt();
	}
	if (root.isMember("titlebar_when_maximized")) {
		values.titlebar_when_maximized =
		    root["titlebar_when_maximized"].asBool();
	}
	if (root.isMember("font")) {
		values.font = root["font"].asString();
	}
	values.titlebar_active_color = parse_decoration_color(
	    root.get("titlebar_active_color", "").asString(),
	    values.titlebar_active_color);
	values.titlebar_inactive_color = parse_decoration_color(
	    root.get("titlebar_inactive_color", "").asString(),
	    values.titlebar_inactive_color);
	values.text_active_color =
	    parse_decoration_color(root.get("text_active_color", "").asString(),
				   values.text_active_color);
	values.text_inactive_color = parse_decoration_color(
	    root.get("text_inactive_color", "").asString(),
	    values.text_inactive_color);
	values.button_glyph_color = parse_decoration_color(
	    root.get("button_glyph_color", "").asString(),
	    values.button_glyph_color);
	values.border_active_color = parse_decoration_color(
	    root.get("border_active_color", "").asString(),
	    values.border_active_color);
	values.border_inactive_color = parse_decoration_color(
	    root.get("border_inactive_color", "").asString(),
	    values.border_inactive_color);
}

WindowDecorations::WindowDecorations() : values(default_values()) {}

WindowDecorations::~WindowDecorations() = default;

std::string WindowDecorations::user_config_path()
{
	const char *home = std::getenv("HOME");
	if (!home || !*home) {
		return std::string();
	}
	return std::string(home) + "/.config/yarfwm/window-decorations.json";
}

bool WindowDecorations::write_default_if_missing() const
{
	const std::string target = user_config_path();
	if (target.empty()) {
		return false;
	}

	std::ifstream existing(target);
	if (existing.good()) {
		// Never overwrite: the user's file is the source of truth once
		// it exists, exactly like config.json.
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

	std::ofstream output(target);
	if (!output.good()) {
		return false;
	}
	output << default_decorations_config_json;
	return output.good();
}

bool WindowDecorations::parse_file(const std::string &path)
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

	apply(root, values);
	return true;
}

void WindowDecorations::load(const std::string &path)
{
	const std::vector<std::string> candidates = {
	    path,
	    "./window-decorations.json",
	    user_config_path(),
	};

	for (const auto &candidate : candidates) {
		if (candidate.empty()) {
			continue;
		}
		if (parse_file(candidate)) {
			return;
		}
	}

	// Nothing usable: write the built-in default once, then load it.
	// If even that fails (read-only HOME, no HOME) the fallbacks in
	// default_values() are already in `values`, so the window manager
	// still runs with sane decorations.
	if (write_default_if_missing()) {
		parse_file(user_config_path());
	}
}
