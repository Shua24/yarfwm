#ifndef SEAT_HPP
#define SEAT_HPP

#include "Server.hpp"

struct river_seat_v1;
struct river_layer_shell_seat_v1;

class Display;
class Config;
class LayerShell;

// Represents river seats.
//
// Keyboard focus, pointer interaction and key bindings arrive with the
// keyboard batch; for now a seat carries its layer shell state so the window
// manager learns when a layer surface (a bar, a launcher) holds keyboard
// focus, in which case window focus requests must not fight it.
//
// Dynamic memory: per-seat state lives in a fixed-size array (a machine with
// more than max_seats seats is not a real configuration), so no allocation
// happens here; the layer shell seat object per seat is a protocol object
// created on demand and destroyed in terminate().
class Seat {
public:
    Seat();
    ~Seat();

    bool initialize(Server *server, Display *display, Config &config);
    void terminate();

    // Called when river hands the window manager a new river_seat_v1.
    void attach_river_seat(struct river_seat_v1 *river_seat, LayerShell &layer_shell);

private:
    static const int max_seats = 4;

    enum LayerSurfaceFocus {
        layer_focus_none = 0,
        layer_focus_exclusive = 1,
        layer_focus_non_exclusive = 2,
    };

    struct SeatEntry {
        struct river_seat_v1 *river_seat;
        struct river_layer_shell_seat_v1 *layer_shell_seat;
        LayerSurfaceFocus layer_surface_focus;
    };

    static void layer_shell_seat_focus_exclusive(void *data,
                                                 struct river_layer_shell_seat_v1 *layer_shell_seat);
    static void layer_shell_seat_focus_non_exclusive(void *data,
                                                     struct river_layer_shell_seat_v1 *layer_shell_seat);
    static void layer_shell_seat_focus_none(void *data,
                                            struct river_layer_shell_seat_v1 *layer_shell_seat);

    SeatEntry seat_entries[max_seats];
    int seat_count;
};

#endif // SEAT_HPP
