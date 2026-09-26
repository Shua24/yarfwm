#ifndef WINDOW_DECORATIONS_CONFIG_HPP
#define WINDOW_DECORATIONS_CONFIG_HPP

#include <cstdint>
#include <string>

// The server-side decoration settings, in their own file.
//
// They live in $HOME/.config/yarfwm/window-decorations.json rather than in
// config.json because they are a different concern with a different consumer:
// config.json holds window-management behaviour (input, keybinds), this holds
// the decoration renderer's appearance. Nothing here affects window
// management, and nothing in config.json affects decoration painting.
//
// Every field has a hard-coded fallback, so a missing file, a malformed file,
// or a file carrying only some keys still yields a usable, decorated window.
// This struct is plain data: jsoncpp is confined to the .cpp so the renderer
// never depends on it.
struct WindowDecorationsConfig {
	bool enabled;
	int32_t titlebar_height;
	int32_t border_width;
	int32_t button_width;
	int32_t button_spacing;
	int32_t padding;
	bool titlebar_when_maximized;
	std::string font;
	uint32_t titlebar_active_color;
	uint32_t titlebar_inactive_color;
	uint32_t text_active_color;
	uint32_t text_inactive_color;
	uint32_t button_glyph_color;
	uint32_t border_active_color;
	uint32_t border_inactive_color;
};

// Parses "AARRGGBB" (no leading '#') into 0xAARRGGBB. Returns `fallback` when
// the text is not exactly eight hex digits -- a typo in the config must not
// produce a transparent or garbage titlebar. Pure, so it is unit tested
// directly.
uint32_t parse_decoration_color(const std::string &text, uint32_t fallback);

class WindowDecorations
{
      public:
	WindowDecorations();
	~WindowDecorations();

	// Loads window-decorations.json, writing the built-in default to
	// $HOME/.config/yarfwm/window-decorations.json first when no candidate
	// exists. This never fails in a way the caller must handle: on any
	// error the settings keep their defaults, because a window manager
	// that refuses to start over a decoration colour is worse than one
	// running with default colours.
	void load(const std::string &path);

	const WindowDecorationsConfig &settings() const { return values; }

      private:
	static std::string user_config_path();
	bool write_default_if_missing() const;
	bool parse_file(const std::string &path);

	WindowDecorationsConfig values;
};

#endif // WINDOW_DECORATIONS_CONFIG_HPP
