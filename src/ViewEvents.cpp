#include "View.hpp"
#include "Seat.hpp"
#include "Display.hpp"
#include "river-window-management-v1-client-protocol.h"
#include <cstdio>

// Listener trampolines for the river window-management objects.
//
// Every slot is populated on purpose: libwayland aborts the process when an
// event arrives for a NULL listener slot, and windows send app_id/title early.
//
// The two sequence events (manage_start, render_start) carry the actual policy
// and live in View.cpp. Output events live in Output.cpp, layer shell seat
// events in Seat.cpp. Everything here either records state or is a no-op that
// keeps the protocol happy until the corresponding feature is implemented.

void View::window_manager_unavailable(void *data, struct river_window_manager_v1 *manager) {
    View *view = static_cast<View *>(data);
    (void)manager;
    std::fprintf(stderr, "Yarfwm: window management unavailable\n");
    view->shutdown_requested = true;
}

void View::window_manager_finished(void *data, struct river_window_manager_v1 *manager) {
    View *view = static_cast<View *>(data);
    (void)manager;
    std::fprintf(stderr, "Yarfwm: window manager finished by server\n");
    view->shutdown_requested = true;
}

void View::window_manager_session_locked(void *data, struct river_window_manager_v1 *manager) {
    (void)data;
    (void)manager;
}

void View::window_manager_session_unlocked(void *data, struct river_window_manager_v1 *manager) {
    (void)data;
    (void)manager;
}

void View::window_manager_window(void *data, struct river_window_manager_v1 *manager,
                                 struct river_window_v1 *window) {
    View *view = static_cast<View *>(data);
    (void)manager;
    view->add_window(window);
}

void View::window_manager_output(void *data, struct river_window_manager_v1 *manager,
                                 struct river_output_v1 *output) {
    View *view = static_cast<View *>(data);
    (void)manager;
    view->add_output(output);
}

void View::window_manager_seat(void *data, struct river_window_manager_v1 *manager,
                               struct river_seat_v1 *seat) {
    View *view = static_cast<View *>(data);
    (void)manager;
    if (view->seat && view->display) {
        view->seat->attach_river_seat(seat, view->display->layer_shell_state);
    }
}

void View::window_closed(void *data, struct river_window_v1 *window) {
    View *view = static_cast<View *>(data);
    view->remove_window(window);
}

void View::window_dimensions_hint(void *data, struct river_window_v1 *window,
                                 int32_t min_width, int32_t min_height,
                                 int32_t max_width, int32_t max_height) {
    (void)data;
    (void)window;
    (void)min_width;
    (void)min_height;
    (void)max_width;
    (void)max_height;
}

void View::window_dimensions(void *data, struct river_window_v1 *window,
                            int32_t width, int32_t height) {
    View *view = static_cast<View *>(data);
    Window *window_entry = view->find_window(window);
    if (!window_entry) {
        return;
    }

    if (width > 0 && height > 0 && !window_entry->rendered) {
        window_entry->rendered = true;
    }
}

void View::window_app_id(void *data, struct river_window_v1 *window, const char *app_id) {
    (void)data;
    (void)window;
    (void)app_id;
}

void View::window_title(void *data, struct river_window_v1 *window, const char *title) {
    (void)data;
    (void)window;
    (void)title;
}

void View::window_parent(void *data, struct river_window_v1 *window,
                         struct river_window_v1 *parent) {
    (void)data;
    (void)window;
    (void)parent;
}

void View::window_decoration_hint(void *data, struct river_window_v1 *window, uint32_t hint) {
    (void)data;
    (void)window;
    (void)hint;
}

void View::window_pointer_move_requested(void *data, struct river_window_v1 *window,
                                         struct river_seat_v1 *seat) {
    (void)data;
    (void)window;
    (void)seat;
}

void View::window_pointer_resize_requested(void *data, struct river_window_v1 *window,
                                           struct river_seat_v1 *seat, uint32_t edges) {
    (void)data;
    (void)window;
    (void)seat;
    (void)edges;
}

void View::window_show_window_menu_requested(void *data, struct river_window_v1 *window,
                                             int32_t x, int32_t y) {
    (void)data;
    (void)window;
    (void)x;
    (void)y;
}

void View::window_maximize_requested(void *data, struct river_window_v1 *window) {
    (void)data;
    (void)window;
}

void View::window_unmaximize_requested(void *data, struct river_window_v1 *window) {
    (void)data;
    (void)window;
}

void View::window_fullscreen_requested(void *data, struct river_window_v1 *window,
                                       struct river_output_v1 *output) {
    (void)data;
    (void)window;
    (void)output;
}

void View::window_exit_fullscreen_requested(void *data, struct river_window_v1 *window) {
    (void)data;
    (void)window;
}

void View::window_minimize_requested(void *data, struct river_window_v1 *window) {
    (void)data;
    (void)window;
}

void View::window_unreliable_pid(void *data, struct river_window_v1 *window, int32_t pid) {
    (void)data;
    (void)window;
    (void)pid;
}

void View::window_presentation_hint(void *data, struct river_window_v1 *window, uint32_t hint) {
    (void)data;
    (void)window;
    (void)hint;
}

void View::window_identifier(void *data, struct river_window_v1 *window, const char *identifier) {
    (void)data;
    (void)window;
    (void)identifier;
}

void View::window_capture_sessions(void *data, struct river_window_v1 *window, uint32_t count) {
    (void)data;
    (void)window;
    (void)count;
}

void View::window_touch_move_requested(void *data, struct river_window_v1 *window,
                                       struct river_seat_v1 *seat, int32_t touch_point) {
    (void)data;
    (void)window;
    (void)seat;
    (void)touch_point;
}

void View::window_touch_resize_requested(void *data, struct river_window_v1 *window,
                                         struct river_seat_v1 *seat, int32_t touch_point,
                                         uint32_t edges) {
    (void)data;
    (void)window;
    (void)seat;
    (void)touch_point;
    (void)edges;
}
