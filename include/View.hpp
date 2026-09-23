#ifndef VIEW_HPP
#define VIEW_HPP

#include "Server.hpp"
#include "river-window-management-v1-client-protocol.h"

class Display;
class Seat;
class Config;
class Output;

// Represents the window management policy and render state.
class View {
public:
    View();
    ~View();

    bool initialize(Server *server, Seat *seat, Display *display, Config &config);
    void terminate();

    // Set when the server tells us to stop (unavailable/finished). The main
    // loop polls this; nothing is destroyed from inside an event callback.
    bool should_shutdown() const;

    // Internal API used by Output's event handlers (an output is removed when
    // river sends river_output_v1.removed, not when the View decides).
    void remove_output(Output *output_entry);

private:
    struct Window {
        struct river_window_v1 *window;
        struct river_node_v1 *node;
        bool managed;
        bool shown;
        bool rendered;
        bool placed;
    };

    // Manager events.
    static void window_manager_manage_start(void *data, struct river_window_manager_v1 *manager);
    static void window_manager_render_start(void *data, struct river_window_manager_v1 *manager);
    static void window_manager_window(void *data, struct river_window_manager_v1 *manager,
                                      struct river_window_v1 *window);
    static void window_manager_output(void *data, struct river_window_manager_v1 *manager,
                                      struct river_output_v1 *output);
    static void window_manager_seat(void *data, struct river_window_manager_v1 *manager,
                                    struct river_seat_v1 *seat);
    static void window_manager_unavailable(void *data, struct river_window_manager_v1 *manager);
    static void window_manager_finished(void *data, struct river_window_manager_v1 *manager);
    static void window_manager_session_locked(void *data, struct river_window_manager_v1 *manager);
    static void window_manager_session_unlocked(void *data, struct river_window_manager_v1 *manager);

    // Window events. Every slot must be non-NULL: libwayland aborts the
    // process when an event arrives for a NULL listener slot.
    static void window_closed(void *data, struct river_window_v1 *window);
    static void window_dimensions_hint(void *data, struct river_window_v1 *window,
                                       int32_t min_width, int32_t min_height,
                                       int32_t max_width, int32_t max_height);
    static void window_dimensions(void *data, struct river_window_v1 *window,
                                 int32_t width, int32_t height);
    static void window_app_id(void *data, struct river_window_v1 *window, const char *app_id);
    static void window_title(void *data, struct river_window_v1 *window, const char *title);
    static void window_parent(void *data, struct river_window_v1 *window,
                              struct river_window_v1 *parent);
    static void window_decoration_hint(void *data, struct river_window_v1 *window, uint32_t hint);
    static void window_pointer_move_requested(void *data, struct river_window_v1 *window,
                                              struct river_seat_v1 *seat);
    static void window_pointer_resize_requested(void *data, struct river_window_v1 *window,
                                                struct river_seat_v1 *seat, uint32_t edges);
    static void window_show_window_menu_requested(void *data, struct river_window_v1 *window,
                                                  int32_t x, int32_t y);
    static void window_maximize_requested(void *data, struct river_window_v1 *window);
    static void window_unmaximize_requested(void *data, struct river_window_v1 *window);
    static void window_fullscreen_requested(void *data, struct river_window_v1 *window,
                                            struct river_output_v1 *output);
    static void window_exit_fullscreen_requested(void *data, struct river_window_v1 *window);
    static void window_minimize_requested(void *data, struct river_window_v1 *window);
    static void window_unreliable_pid(void *data, struct river_window_v1 *window, int32_t pid);
    static void window_presentation_hint(void *data, struct river_window_v1 *window, uint32_t hint);
    static void window_identifier(void *data, struct river_window_v1 *window,
                                  const char *identifier);
    static void window_capture_sessions(void *data, struct river_window_v1 *window, uint32_t count);
    static void window_touch_move_requested(void *data, struct river_window_v1 *window,
                                            struct river_seat_v1 *seat, int32_t touch_point);
    static void window_touch_resize_requested(void *data, struct river_window_v1 *window,
                                              struct river_seat_v1 *seat, int32_t touch_point,
                                              uint32_t edges);

    Window *find_window(struct river_window_v1 *window) const;
    void add_window(struct river_window_v1 *window);
    void remove_window(struct river_window_v1 *window);
    void add_output(struct river_output_v1 *output);
    void propose_default_dimensions(Window *window) const;
    void place_windows();
    void placement_area(int32_t *x, int32_t *y, int32_t *width, int32_t *height) const;

    Seat *seat;
    Display *display;
    struct river_window_manager_v1 *manager;
    Window *windows;
    int window_count;
    int window_capacity;
    Output **outputs;
    int output_count;
    int output_capacity;
    Output *default_layer_output;
    int pending_manage_count;
    int pending_render_count;
    bool shutdown_requested;
};

#endif // VIEW_HPP
