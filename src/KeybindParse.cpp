#include "Config.hpp"
#include "Keybind.hpp"
#include "river-window-management-v1-client-protocol.h"

#include <cstdio>
#include <xkbcommon/xkbcommon.h>

// The half of the binding engine that turns the config's text into the action
// and the keysym/modifier pair river matches on. It lives apart from
// Keybind.cpp so both files stay inside the project's line budget.

// Parse the percentage argument of a set_window_width/height bind, which the
// config writes as a signed percentage string such as "-10%" or "+10%".
static int parse_percent(const std::vector<std::string> &arguments)
{
	if (arguments.empty()) {
		return 0;
	}
	const std::string &text = arguments[0];
	try {
		return std::stoi(text);
	} catch (const std::exception &) {
		std::fprintf(stderr,
			     "Yarfwm: could not read the percentage '%s', "
			     "using 0\n",
			     text.c_str());
		return 0;
	}
}

Keybind::Action Keybind::parse_action(const std::string &action_name,
				      const std::vector<std::string> &arguments)
{
	Action action{};
	action.kind = action_none;
	action.direction = focus_direction_left;
	action.amount = 0;

	if (action_name == "spawn") {
		action.kind = action_spawn;
	} else if (action_name == "close_window") {
		action.kind = action_close_window;
	} else if (action_name == "exit_session") {
		action.kind = action_exit_session;
	} else if (action_name == "focus_window_left") {
		action.kind = action_focus_direction;
		action.direction = focus_direction_left;
	} else if (action_name == "focus_window_right") {
		action.kind = action_focus_direction;
		action.direction = focus_direction_right;
	} else if (action_name == "focus_window_up") {
		action.kind = action_focus_direction;
		action.direction = focus_direction_up;
	} else if (action_name == "focus_window_down") {
		action.kind = action_focus_direction;
		action.direction = focus_direction_down;
	} else if (action_name == "focus_window_previous") {
		action.kind = action_focus_previous;
	} else if (action_name == "move_window_left") {
		action.kind = action_move_direction;
		action.direction = focus_direction_left;
	} else if (action_name == "move_window_right") {
		action.kind = action_move_direction;
		action.direction = focus_direction_right;
	} else if (action_name == "move_window_up") {
		action.kind = action_move_direction;
		action.direction = focus_direction_up;
	} else if (action_name == "move_window_down") {
		action.kind = action_move_direction;
		action.direction = focus_direction_down;
	} else if (action_name == "toggle_maximize") {
		action.kind = action_toggle_maximize;
	} else if (action_name == "fullscreen_window") {
		action.kind = action_fullscreen;
	} else if (action_name == "toggle_always_on_top") {
		action.kind = action_toggle_always_on_top;
	} else if (action_name == "minimize_window") {
		action.kind = action_minimize;
	} else if (action_name == "restore_minimized_window") {
		action.kind = action_restore_minimized;
	} else if (action_name == "center_window") {
		action.kind = action_center_window;
	} else if (action_name == "center_all_windows") {
		action.kind = action_center_all_windows;
	} else if (action_name == "fit_to_output") {
		action.kind = action_fit_to_output;
	} else if (action_name == "set_window_width") {
		action.kind = action_resize_width;
		action.amount = parse_percent(arguments);
	} else if (action_name == "set_window_height") {
		action.kind = action_resize_height;
		action.amount = parse_percent(arguments);
	} else if (action_name == "focus_desktop_next") {
		action.kind = action_focus_desktop;
		action.amount = 1;
	} else if (action_name == "focus_desktop_previous") {
		action.kind = action_focus_desktop;
		action.amount = -1;
	} else if (action_name == "move_window_to_desktop_next") {
		action.kind = action_move_window_to_desktop;
		action.amount = 1;
	} else if (action_name == "move_window_to_desktop_previous") {
		action.kind = action_move_window_to_desktop;
		action.amount = -1;
	} else if (action_name == "move_pointer_left") {
		action.kind = action_move_pointer;
		action.direction = focus_direction_left;
	} else if (action_name == "move_pointer_right") {
		action.kind = action_move_pointer;
		action.direction = focus_direction_right;
	} else if (action_name == "move_pointer_up") {
		action.kind = action_move_pointer;
		action.direction = focus_direction_up;
	} else if (action_name == "move_pointer_down") {
		action.kind = action_move_pointer;
		action.direction = focus_direction_down;
	} else if (action_name == "toggle_decorations") {
		action.kind = action_toggle_decorations;
	}

	return action;
}

std::vector<std::string> Keybind::read_arguments(const Json::Value &bind)
{
	std::vector<std::string> arguments;
	const Json::Value &args = bind["args"];
	if (args.isArray()) {
		// Indexed rather than range-for: jsoncpp's iterator dereference
		// returns by value, which trips -Waggregate-return under the
		// project's warning set.
		for (Json::ArrayIndex index = 0; index < args.size(); index++) {
			const Json::Value &item = args[index];
			if (item.isString()) {
				arguments.push_back(item.asString());
			}
		}
	}
	return arguments;
}

uint32_t Keybind::resolve_keysym(const std::string &key_name)
{
	xkb_keysym_t keysym =
	    xkb_keysym_from_name(key_name.c_str(), XKB_KEYSYM_NO_FLAGS);
	if (keysym == XKB_KEY_NoSymbol) {
		// The config spells key names the way xkbcommon's canonical
		// names do, but not always with the canonical case ("Slash"
		// vs "slash").
		keysym = xkb_keysym_from_name(key_name.c_str(),
					      XKB_KEYSYM_CASE_INSENSITIVE);
	}
	if (keysym == XKB_KEY_NoSymbol) {
		return 0;
	}

	// River matches the base-layer keysym, "as if modifiers didn't change
	// keysyms": pressing Super+Shift+R delivers keysym 'r'. Register the
	// lowercase form so Shift+letter bindings fire.
	if (keysym >= XKB_KEY_A && keysym <= XKB_KEY_Z) {
		keysym =
		    static_cast<xkb_keysym_t>(keysym + (XKB_KEY_a - XKB_KEY_A));
	}

	return static_cast<uint32_t>(keysym);
}

uint32_t Keybind::resolve_modifiers(const Json::Value &bind, bool *resolved)
{
	uint32_t modifiers = RIVER_SEAT_V1_MODIFIERS_NONE;
	*resolved = true;

	const Json::Value &names = bind["modifiers"];
	if (names.isArray()) {
		for (const Json::Value &item : names) {
			const std::string name = item.asString();
			if (name == "Shift") {
				modifiers |= RIVER_SEAT_V1_MODIFIERS_SHIFT;
			} else if (name == "Ctrl" || name == "Control") {
				modifiers |= RIVER_SEAT_V1_MODIFIERS_CTRL;
			} else if (name == "Alt") {
				modifiers |= RIVER_SEAT_V1_MODIFIERS_MOD1;
			} else if (name == "Super" || name == "Logo") {
				modifiers |= RIVER_SEAT_V1_MODIFIERS_MOD4;
			} else {
				std::fprintf(stderr,
					     "Yarfwm: unknown modifier '%s', "
					     "skipping its binding\n",
					     name.c_str());
				*resolved = false;
				return modifiers;
			}
		}
	}

	// The inherited config carries a couple of binds whose Shift sits in a
	// separate flag rather than in the modifiers array; river compares the
	// modifier mask exactly, so fold it in.
	if (bind.get("requires_shift", false).asBool()) {
		modifiers |= RIVER_SEAT_V1_MODIFIERS_SHIFT;
	}

	return modifiers;
}
