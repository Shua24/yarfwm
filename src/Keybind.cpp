#include "Keybind.hpp"
#include "Config.hpp"
#include "Display.hpp"
#include "river-window-management-v1-client-protocol.h"
#include "river-xkb-bindings-v1.hpp"

#include <csignal>
#include <cstdio>
#include <cstring>
#include <new>
#include <sys/wait.h>
#include <unistd.h>

// Key repeat timing: how long the key is held before it starts repeating,
// then how fast it repeats. 400ms matches the usual desktop auto-repeat
// delay; 40ms is about 25 repeats a second.
static const int repeat_delay_milliseconds = 400;
static const int repeat_interval_milliseconds = 40;

Keybind::Keybind()
    : server(nullptr), display(nullptr), seat(nullptr), view(nullptr),
      repeating_definition_index(-1), repeating_river_seat(nullptr)
{
}

Keybind::~Keybind() { terminate(); }

bool Keybind::initialize(Server *server, Display *display, Seat *seat,
			 View *view, Config &config)
{
	this->server = server;
	this->display = display;
	this->seat = seat;
	this->view = view;

	if (!display || !display->xkb_bindings) {
		// Not fatal: river without the bindings global leaves the
		// window manager usable, just without key bindings.
		std::fprintf(stderr, "Yarfwm: no river_xkb_bindings_v1 "
				     "available, key bindings disabled\n");
		return true;
	}

	if (!config.keybinds.isArray()) {
		std::fprintf(stderr, "Yarfwm: config has no keybinds array\n");
		return true;
	}

	std::vector<std::string> reported_missing;
	// Entries dropped because their action name is unknown. Counted
	// separately from reported_missing, which counts distinct action
	// names: several entries can share one unknown action, so the two
	// numbers differ. The startup line reports both so a reader can
	// reconcile registered + skipped against the config's entry count.
	int skipped_entry_count = 0;
	for (const Json::Value &bind : config.keybinds) {
		const std::string action_name =
		    bind.get("action", "").asString();
		const std::string key_name = bind.get("key", "").asString();

		// The args array is read before parsing: the resize actions
		// carry their percentage in it.
		const std::vector<std::string> arguments = read_arguments(bind);
		const Action action = parse_action(action_name, arguments);
		if (action.kind == action_none) {
			// An unknown action name: reported once and skipped,
			// so the key keeps reaching the focused window.
			bool already_reported = false;
			for (const std::string &reported : reported_missing) {
				already_reported =
				    already_reported || reported == action_name;
			}
			if (!already_reported) {
				reported_missing.push_back(action_name);
				std::fprintf(stderr,
					     "Yarfwm: keybind action not "
					     "implemented yet, skipping: %s\n",
					     action_name.c_str());
			}
			skipped_entry_count++;
			continue;
		}

		const uint32_t keysym = resolve_keysym(key_name);
		if (keysym == 0) {
			std::fprintf(stderr,
				     "Yarfwm: unknown key name '%s', skipping "
				     "its binding\n",
				     key_name.c_str());
			continue;
		}

		bool resolved = false;
		const uint32_t modifiers = resolve_modifiers(bind, &resolved);
		if (!resolved) {
			continue;
		}

		Definition definition{};
		definition.keysym = keysym;
		definition.modifiers = modifiers;
		definition.action = action;

		// Held-key repeat. An explicit "repeat" in the config wins;
		// otherwise the directional focus moves and the keyboard
		// pointer warps repeat. Spawning a program or quitting on
		// every repeat tick would be wrong.
		const Json::Value &repeat_value = bind["repeat"];
		definition.repeats =
		    repeat_value.isBool()
			? repeat_value.asBool()
			: (action.kind == action_focus_direction ||
			   action.kind == action_move_pointer);

		if (definition.action.kind == action_spawn) {
			definition.action.arguments = arguments;
			if (definition.action.arguments.empty()) {
				std::fprintf(stderr,
					     "Yarfwm: spawn binding '%s' has "
					     "no args, skipping\n",
					     key_name.c_str());
				continue;
			}
		}

		definitions.push_back(definition);
	}

	// The manage sequence handler lives in View, so it needs a way back
	// here to enable the bindings it creates.
	if (!definitions.empty()) {
		view->set_keybind(this);
	}

	// Key repeat is driven by our own timer; without it the bindings stay
	// single-shot.
	repeat_timer.initialize();

	if (skipped_entry_count > 0) {
		std::fprintf(stderr,
			     "Yarfwm: %zu key bindings parsed, %d entries "
			     "skipped (%zu distinct actions not implemented "
			     "yet)\n",
			     definitions.size(), skipped_entry_count,
			     reported_missing.size());
	} else {
		std::fprintf(stderr, "Yarfwm: %zu key bindings parsed\n",
			     definitions.size());
	}
	return true;
}

void Keybind::terminate()
{
	stop_repeating();
	repeat_timer.terminate();

	for (BindingEntry *entry : entries) {
		if (entry->binding) {
			river_xkb_binding_v1_destroy(entry->binding);
			entry->binding = nullptr;
		}
		delete entry;
	}
	entries.clear();
	definitions.clear();
	view = nullptr;
	seat = nullptr;
	display = nullptr;
	server = nullptr;
}

void Keybind::forget_removed_seats()
{
	if (!seat) {
		return;
	}

	// A repeat armed on a seat river removed would tick against a proxy
	// that is about to be destroyed.
	if (repeating_river_seat && seat->is_removed(repeating_river_seat)) {
		stop_repeating();
	}

	for (size_t i = 0; i < entries.size();) {
		BindingEntry *entry = entries[i];
		if (!entry->river_seat ||
		    !seat->is_removed(entry->river_seat)) {
			i++;
			continue;
		}

		// The seat proxy is destroyed by Seat::apply_manage() right
		// after this; the binding object has to go first.
		if (entry->binding) {
			river_xkb_binding_v1_destroy(entry->binding);
			entry->binding = nullptr;
		}
		delete entry;
		entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(i));
	}
}

bool Keybind::has_entries_for(struct river_seat_v1 *river_seat) const
{
	for (const BindingEntry *entry : entries) {
		if (entry->river_seat == river_seat) {
			return true;
		}
	}
	return false;
}

void Keybind::register_bindings_for_seat(struct river_seat_v1 *river_seat)
{
	static const struct river_xkb_binding_v1_listener binding_listener =
	    [] {
		    struct river_xkb_binding_v1_listener listener{};
		    listener.pressed = binding_pressed;
		    listener.released = binding_released;
		    listener.stop_repeat = binding_stop_repeat;
		    return listener;
	    }();

	int registered = 0;
	for (size_t i = 0; i < definitions.size(); i++) {
		struct river_xkb_binding_v1 *binding =
		    river_xkb_bindings_v1_get_xkb_binding(
			display->xkb_bindings, river_seat,
			definitions[i].keysym, definitions[i].modifiers);
		if (!binding) {
			std::fprintf(
			    stderr,
			    "Yarfwm: failed to create a key binding object\n");
			continue;
		}

		// Dynamic memory: one entry per binding object; the listener
		// data pointer must outlive the object, so each entry has its
		// own address.
		BindingEntry *entry = new (std::nothrow) BindingEntry{};
		if (!entry) {
			river_xkb_binding_v1_destroy(binding);
			continue;
		}

		entry->owner = this;
		entry->river_seat = river_seat;
		entry->binding = binding;
		entry->definition_index = static_cast<int>(i);
		entry->enabled = false;

		river_xkb_binding_v1_add_listener(binding, &binding_listener,
						  entry);
		entries.push_back(entry);
		registered++;
	}

	std::fprintf(stderr,
		     "Yarfwm: registered %d/%zu key bindings on a seat\n",
		     registered, definitions.size());
}

void Keybind::apply_manage()
{
	if (!display || !display->xkb_bindings || definitions.empty()) {
		return;
	}

	// river scopes a binding to a seat, so a seat that arrived after
	// startup gets its own set of binding objects.
	const int seat_total = seat->river_seat_count();
	for (int i = 0; i < seat_total; i++) {
		struct river_seat_v1 *river_seat = seat->river_seat_at(i);
		if (!river_seat || has_entries_for(river_seat)) {
			continue;
		}
		register_bindings_for_seat(river_seat);
	}

	// enable is a manage-sequence request.
	for (BindingEntry *entry : entries) {
		if (!entry->enabled) {
			river_xkb_binding_v1_enable(entry->binding);
			entry->enabled = true;
		}
	}
}

// Key repeat. River sends pressed once and leaves repeating to the window
// manager (att_wm does the same with Wm.zig's repeat_fd): start the timer on
// press and stop it on released/stop_repeat.
void Keybind::start_repeating(BindingEntry *entry)
{
	if (!entry || !definitions[static_cast<size_t>(entry->definition_index)]
			   .repeats) {
		// Not repeatable: make sure nothing else is left running.
		stop_repeating();
		return;
	}

	repeating_definition_index = entry->definition_index;
	repeating_river_seat = entry->river_seat;
	repeat_timer.arm(repeat_delay_milliseconds,
			 repeat_interval_milliseconds);
}

void Keybind::stop_repeating(BindingEntry *entry)
{
	// Only the binding that owns the repeat may stop it; a null entry
	// stops whatever is repeating (terminate()).
	if (entry && (entry->definition_index != repeating_definition_index ||
		      entry->river_seat != repeating_river_seat)) {
		return;
	}

	repeating_definition_index = -1;
	repeating_river_seat = nullptr;
	repeat_timer.disarm();
}

int Keybind::repeat_timer_file_descriptor() const
{
	return repeat_timer.file_descriptor();
}

void Keybind::handle_repeat_timer()
{
	repeat_timer.consume();

	if (repeating_definition_index < 0 || !repeating_river_seat) {
		stop_repeating();
		return;
	}

	perform(repeating_definition_index, repeating_river_seat);
}

void Keybind::binding_pressed(void *data, struct river_xkb_binding_v1 *binding)
{
	BindingEntry *entry = static_cast<BindingEntry *>(data);
	(void)binding;

	entry->owner->perform(entry->definition_index, entry->river_seat);
	entry->owner->start_repeating(entry);
}

void Keybind::binding_released(void *data, struct river_xkb_binding_v1 *binding)
{
	// The bound key went up: stop repeating it.
	BindingEntry *entry = static_cast<BindingEntry *>(data);
	(void)binding;

	entry->owner->stop_repeating(entry);
}

void Keybind::binding_stop_repeat(void *data,
				  struct river_xkb_binding_v1 *binding)
{
	// Another key was pressed while this binding was held: river asks us
	// to stop repeating it.
	BindingEntry *entry = static_cast<BindingEntry *>(data);
	(void)binding;

	entry->owner->stop_repeating(entry);
}
