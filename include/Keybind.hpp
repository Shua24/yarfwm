#ifndef KEYBIND_HPP
#define KEYBIND_HPP

#include "RepeatTimer.hpp"
#include "Seat.hpp"
#include "Server.hpp"
#include "View.hpp"

#include <json/json.h>
#include <string>
#include <vector>

class Config;
class Display;

struct river_seat_v1;
struct river_xkb_binding_v1;
struct river_window_v1;

// The keyboard binding engine: turns the keybinds array from config.json into
// river_xkb_binding_v1 objects and performs the configured action when one
// fires.
//
// River matches a binding against the base-layer (level 0) keysym and requires
// the modifier mask to match exactly (river 0.4.8, XkbBinding.zig match()), so
// every key name is resolved to its level-0 keysym and ASCII A-Z is lowercased
// before registering. A binding is not live until enable is sent during a
// manage sequence, so that happens in apply_manage().
//
// Unknown action names are reported once at startup and skipped: a skipped
// binding is never registered, so its key keeps reaching the focused window.
//
// Dynamic memory: the parsed definitions and the per-seat binding entries are
// heap-allocated while the configuration is read and freed in terminate().
// Each binding entry is individually allocated because the Wayland listener
// data pointer must stay valid for the lifetime of the binding object.
class Keybind
{
      public:
	Keybind();
	~Keybind();

	bool initialize(Server *server, Display *display, Seat *seat,
			View *view, Config &config);
	void terminate();

	// Create the binding objects for any seat that does not have them yet
	// and enable every binding that is not enabled yet. Manage sequence
	// only; called from View::window_manager_manage_start().
	void apply_manage();

	// Key repeat. River sends pressed once and leaves repeating to the
	// window manager, so the main loop polls this descriptor and calls
	// handle_repeat_timer() while a repeatable binding is held. Returns -1
	// when the timer is unavailable.
	int repeat_timer_file_descriptor() const;
	void handle_repeat_timer();

	// A seat river removed must lose its binding objects and any repeat
	// armed on it, before Seat::apply_manage() destroys the seat proxy
	// those objects and the repeat point at. Called from
	// View::window_manager_manage_start(), before Seat::apply_manage().
	void forget_removed_seats();

	// The parsing surface is public so the unit tests can exercise it
	// without a Wayland connection.
	enum ActionKind {
		action_none = 0,
		action_spawn,
		action_close_window,
		action_exit_session,
		action_focus_direction,
		action_focus_previous,
		action_move_direction,
		action_toggle_maximize,
		action_fullscreen,
		action_toggle_always_on_top,
		action_minimize,
		action_restore_minimized,
		action_center_window,
		action_center_all_windows,
		action_fit_to_output,
		action_resize_width,
		action_resize_height,
		action_focus_desktop,
		action_move_window_to_desktop,
		action_move_pointer,
		action_toggle_decorations,
	};

	struct Action {
		ActionKind kind;
		FocusDirection direction; // action_focus_direction only
		std::vector<std::string> arguments; // action_spawn only
		// action_resize_width/height: the percentage step, signed.
		// action_focus_desktop / action_move_window_to_desktop: +1 or
		// -1.
		int amount;
	};

	static Action parse_action(const std::string &action_name,
				   const std::vector<std::string> &arguments);
	static std::vector<std::string> read_arguments(const Json::Value &bind);
	static uint32_t resolve_keysym(const std::string &key_name);
	static uint32_t resolve_modifiers(const Json::Value &bind,
					  bool *resolved);

      private:
	// One usable entry from the config's keybinds array.
	struct Definition {
		uint32_t keysym;
		uint32_t modifiers;
		Action action;
		// Whether holding the key repeats the action.
		bool repeats;
	};

	// One river_xkb_binding_v1 object: river scopes a binding to a seat, so
	// there is one entry per (seat, definition) pair.
	struct BindingEntry {
		Keybind *owner;
		struct river_seat_v1 *river_seat;
		struct river_xkb_binding_v1 *binding;
		int definition_index;
		bool enabled;
	};

	static void binding_pressed(void *data,
				    struct river_xkb_binding_v1 *binding);
	static void binding_released(void *data,
				     struct river_xkb_binding_v1 *binding);
	static void binding_stop_repeat(void *data,
					struct river_xkb_binding_v1 *binding);
	// A readable name for an action kind, used when reporting that a
	// binding was refused while the session is locked.
	static const char *action_name(ActionKind kind);

	bool has_entries_for(struct river_seat_v1 *river_seat) const;
	void register_bindings_for_seat(struct river_seat_v1 *river_seat);
	void perform(int definition_index, struct river_seat_v1 *river_seat);

	// Start repeating the binding that was just pressed, if it is
	// repeatable; stop_repeating() takes a binding entry to stop only the
	// binding that owns the repeat (null stops whatever is repeating).
	void start_repeating(BindingEntry *entry);
	void stop_repeating(BindingEntry *entry = nullptr);

	Server *server;
	Display *display;
	Seat *seat;
	View *view;

	// Key repeat state: the definition being repeated and the seat it
	// belongs to, or -1/null when nothing is repeating.
	RepeatTimer repeat_timer;
	int repeating_definition_index;
	struct river_seat_v1 *repeating_river_seat;

	std::vector<Definition> definitions;
	std::vector<BindingEntry *> entries;
};

#endif // KEYBIND_HPP
