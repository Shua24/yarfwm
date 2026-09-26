#include "ChildProcesses.hpp"
#include "Config.hpp"
#include "Display.hpp"
#include "Keybind.hpp"
#include "river-window-management-v1-client-protocol.h"

#include <cstdio>
#include <sys/wait.h>
#include <unistd.h>

// What a binding does when it fires: spawn a program, or ask the View to change
// state.
//
// This lives apart from Keybind.cpp (which holds the config parsing, the
// binding objects and the repeat timer) so both files stay inside the project's
// line budget.

// The action log lines name the direction a focus bind moved in.
static const char *direction_name(enum FocusDirection direction)
{
	switch (direction) {
	case focus_direction_left:
		return "left";
	case focus_direction_right:
		return "right";
	case focus_direction_up:
		return "up";
	case focus_direction_down:
		return "down";
	default:
		return "?";
	}
}

// A readable name for an action kind, used when reporting that a binding was
// refused while the session is locked. A member so it can see the private
// ActionKind.
const char *Keybind::action_name(ActionKind kind)
{
	switch (kind) {
	case action_spawn:
		return "spawn";
	case action_close_window:
		return "close_window";
	case action_exit_session:
		return "exit_session";
	case action_focus_direction:
		return "focus_window";
	case action_focus_previous:
		return "focus_window_previous";
	case action_move_direction:
		return "move_window";
	case action_toggle_maximize:
		return "toggle_maximize";
	case action_fullscreen:
		return "fullscreen_window";
	case action_toggle_always_on_top:
		return "toggle_always_on_top";
	case action_minimize:
		return "minimize_window";
	case action_restore_minimized:
		return "restore_minimized_window";
	case action_center_window:
		return "center_window";
	case action_center_all_windows:
		return "center_all_windows";
	case action_fit_to_output:
		return "fit_to_output";
	case action_resize_width:
		return "set_window_width";
	case action_resize_height:
		return "set_window_height";
	case action_focus_desktop:
		return "focus_desktop";
	case action_move_window_to_desktop:
		return "move_window_to_desktop";
	case action_move_pointer:
		return "move_pointer";
	case action_toggle_decorations:
		return "toggle_decorations";
	case action_none:
	default:
		return "none";
	}
}

// Fork and exec a configured command. The config's args array is the argument
// vector, so args[0] is the program and the rest are its arguments.
//
// Dynamic memory: the argument pointer array is allocated on the child's heap
// after the fork; it is either replaced by the new image or freed again by
// _exit, so nothing leaks into the window manager. The intermediate child is
// reaped immediately; the grandchild is reparented to init and is never waited
// on.
static void spawn_command(const std::vector<std::string> &arguments)
{
	if (arguments.empty()) {
		return;
	}

	pid_t process_id = fork();
	if (process_id < 0) {
		std::fprintf(stderr, "Yarfwm: failed to fork for spawn\n");
		return;
	}

	if (process_id == 0) {
		setsid();
		// Double fork so the user program is reparented to init: the
		// window manager has no business waiting on the programs it
		// starts.
		pid_t inner = fork();
		if (inner < 0) {
			_exit(1);
		}
		if (inner > 0) {
			_exit(0);
		}

		// The blocked signals are inherited across fork() and preserved
		// across execve(), so the child would start with its own child
		// exits silently blocked. Restore the mask before the exec, the
		// way labwc does in reset_signals_and_limits (spawn.c).
		ChildProcesses::prepare_child_for_execution();

		std::vector<char *> argument_pointers;
		argument_pointers.reserve(arguments.size() + 1);
		// Indexed rather than range-for: the argument type's iterator
		// dereference returns by value under this standard library,
		// which trips -Waggregate-return.
		for (std::size_t index = 0; index < arguments.size(); index++) {
			argument_pointers.push_back(
			    const_cast<char *>(arguments[index].c_str()));
		}
		argument_pointers.push_back(nullptr);

		execvp(argument_pointers[0], argument_pointers.data());
		std::fprintf(stderr, "Yarfwm: failed to exec %s\n",
			     argument_pointers[0]);
		_exit(127);
	}

	int status = 0;
	waitpid(process_id, &status, 0);
}

void Keybind::perform(int definition_index, struct river_seat_v1 *river_seat)
{
	if (definition_index < 0 ||
	    definition_index >= static_cast<int>(definitions.size())) {
		return;
	}

	const Action &action = definitions[definition_index].action;

	// While a lock screen holds the keyboard, only ending the session
	// is safe: every other action would fight the lock screen for
	// focus. This mirrors the reference window manager, and river
	// documents the lock event as existing exactly for this.
	if (view->session_is_locked() && action.kind != action_exit_session) {
		std::fprintf(stderr,
			     "Yarfwm: %s ignored, the session is locked\n",
			     action_name(action.kind));
		return;
	}

	switch (action.kind) {
	case action_spawn:
		// Nothing river knows about changed, so there is nothing to
		// apply.
		if (!action.arguments.empty()) {
			std::fprintf(stderr, "Yarfwm: spawn %s\n",
				     action.arguments[0].c_str());
		}
		spawn_command(action.arguments);
		return;
	case action_close_window: {
		struct river_window_v1 *window =
		    seat->focused_window(river_seat);
		if (window) {
			std::fprintf(stderr, "Yarfwm: close_window\n");
			view->request_close(window);
		} else {
			std::fprintf(
			    stderr,
			    "Yarfwm: close_window: no focused window\n");
		}
		break;
	}
	case action_exit_session:
		// End the session: river exits the compositor and disconnects
		// every client, this one included.
		view->request_exit_session();
		return;
	case action_focus_direction: {
		struct river_window_v1 *target = view->window_in_direction(
		    seat->focused_window(river_seat), action.direction);
		if (target) {
			std::fprintf(stderr, "Yarfwm: focus_window_%s\n",
				     direction_name(action.direction));
			seat->focus(river_seat, target);
		} else {
			std::fprintf(stderr,
				     "Yarfwm: focus_window_%s: no neighbour\n",
				     direction_name(action.direction));
		}
		break;
	}
	case action_focus_previous: {
		struct river_window_v1 *target =
		    seat->previous_focused_window(river_seat);
		// The previous window may have been left behind on
		// another desktop (or minimized) since it was focused; a
		// hidden window must never take the keyboard.
		if (target && !view->window_is_visible(target)) {
			target = nullptr;
		}
		if (target) {
			std::fprintf(stderr, "Yarfwm: focus_window_previous\n");
			seat->focus(river_seat, target);
		} else {
			std::fprintf(stderr, "Yarfwm: focus_window_previous: "
					     "no previous window\n");
		}
		break;
	}
	case action_move_direction: {
		struct river_window_v1 *target =
		    seat->focused_window(river_seat);
		if (target) {
			view->move_window(target, action.direction);
		} else {
			std::fprintf(stderr,
				     "Yarfwm: move_window_%s: no focused "
				     "window\n",
				     direction_name(action.direction));
		}
		return;
	}
	case action_move_pointer: {
		// Keyboard-driven pointer movement, one step per press. The
		// step matches the cascade step, so the two feel alike.
		const int32_t pointer_step = 32;
		int32_t delta_x = 0;
		int32_t delta_y = 0;
		switch (action.direction) {
		case focus_direction_left:
			delta_x = -pointer_step;
			break;
		case focus_direction_right:
			delta_x = pointer_step;
			break;
		case focus_direction_up:
			delta_y = -pointer_step;
			break;
		case focus_direction_down:
		default:
			delta_y = pointer_step;
			break;
		}
		seat->move_pointer(river_seat, delta_x, delta_y);
		return;
	}
	case action_toggle_maximize: {
		struct river_window_v1 *target =
		    seat->focused_window(river_seat);
		if (target) {
			view->toggle_maximize(target);
		} else {
			std::fprintf(stderr,
				     "Yarfwm: toggle_maximize: no focused "
				     "window\n");
		}
		return;
	}
	case action_fullscreen: {
		struct river_window_v1 *target =
		    seat->focused_window(river_seat);
		if (!target) {
			std::fprintf(stderr,
				     "Yarfwm: fullscreen_window: no focused "
				     "window\n");
			return;
		}
		// The binding toggles: pressing it again leaves fullscreen.
		// The window's own request event always says which way it
		// wants, so only this path has to decide.
		view->toggle_fullscreen(target);
		return;
	}
	case action_toggle_always_on_top: {
		struct river_window_v1 *target =
		    seat->focused_window(river_seat);
		if (target) {
			view->toggle_always_on_top(target);
		} else {
			std::fprintf(stderr,
				     "Yarfwm: toggle_always_on_top: no focused "
				     "window\n");
		}
		return;
	}
	case action_minimize: {
		struct river_window_v1 *target =
		    seat->focused_window(river_seat);
		if (target) {
			view->minimize_window(target);
		} else {
			std::fprintf(stderr,
				     "Yarfwm: minimize_window: no focused "
				     "window\n");
		}
		return;
	}
	case action_restore_minimized:
		view->restore_minimized_window();
		return;
	case action_center_window: {
		struct river_window_v1 *target =
		    seat->focused_window(river_seat);
		if (target) {
			view->center_window(target);
		} else {
			std::fprintf(stderr,
				     "Yarfwm: center_window: no focused "
				     "window\n");
		}
		return;
	}
	case action_center_all_windows:
		view->center_all_windows();
		return;
	case action_fit_to_output: {
		struct river_window_v1 *target =
		    seat->focused_window(river_seat);
		if (target) {
			view->fit_to_output(target);
		} else {
			std::fprintf(stderr,
				     "Yarfwm: fit_to_output: no focused "
				     "window\n");
		}
		return;
	}
	case action_resize_width:
	case action_resize_height: {
		struct river_window_v1 *target =
		    seat->focused_window(river_seat);
		if (target) {
			view->resize_window(target,
					    action.kind == action_resize_width,
					    action.amount);
		} else {
			std::fprintf(stderr,
				     "Yarfwm: set_window_%s: no focused "
				     "window\n",
				     action.kind == action_resize_width
					 ? "width"
					 : "height");
		}
		return;
	}
	case action_focus_desktop:
		view->focus_desktop(river_seat, action.amount);
		return;
	case action_move_window_to_desktop: {
		struct river_window_v1 *target =
		    seat->focused_window(river_seat);
		if (target) {
			view->move_window_to_desktop(river_seat, target,
						     action.amount);
		} else {
			std::fprintf(stderr,
				     "Yarfwm: move_window_to_desktop: no "
				     "focused window\n");
		}
		return;
	}
	case action_toggle_decorations:
		view->set_decorations_enabled(!view->decorations_enabled());
		std::fprintf(stderr, "Yarfwm: toggle_decorations -> %s\n",
			     view->decorations_enabled() ? "on" : "off");
		return;
	case action_none:
	default:
		return;
	}

	// Focus and close are recorded as intent; ask for the manage sequence
	// that applies it. River follows the pressed event with one of its own,
	// which makes this redundant, but it keeps the intent from depending on
	// that.
	view->request_manage();
}
