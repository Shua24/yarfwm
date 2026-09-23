#include "Seat.hpp"
#include "LayerShell.hpp"
#include "river-layer-shell-v1-client-protocol.h"
#include <cstdio>
#include <cstring>

Seat::Seat() : seat_count(0) {
    std::memset(seat_entries, 0, sizeof(seat_entries));
}

Seat::~Seat() {
    terminate();
}

bool Seat::initialize(Server *server, Display *display, Config &config) {
    // Dynamic memory: none yet; per-seat state is created lazily in
    // attach_river_seat() when river hands the window manager a seat.
    (void)server;
    (void)display;
    (void)config;
    return true;
}

void Seat::attach_river_seat(struct river_seat_v1 *river_seat, LayerShell &layer_shell) {
    if (!river_seat) {
        return;
    }

    // get_seat may only be requested once per river_seat_v1: a repeat would be
    // a protocol error, so a repeated attach is ignored.
    for (int i = 0; i < seat_count; i++) {
        if (seat_entries[i].river_seat == river_seat) {
            return;
        }
    }

    if (seat_count >= max_seats) {
        std::fprintf(stderr, "Yarfwm: more seats than the first %d, ignoring the rest\n",
                     max_seats);
        return;
    }

    SeatEntry *entry = &seat_entries[seat_count];
    std::memset(entry, 0, sizeof(*entry));
    entry->river_seat = river_seat;
    entry->layer_surface_focus = layer_focus_none;

    if (layer_shell.available()) {
        entry->layer_shell_seat = layer_shell.create_seat_state(river_seat);
        if (entry->layer_shell_seat) {
            static const struct river_layer_shell_seat_v1_listener layer_shell_seat_listener = {
                layer_shell_seat_focus_exclusive,
                layer_shell_seat_focus_non_exclusive,
                layer_shell_seat_focus_none,
            };

            if (river_layer_shell_seat_v1_add_listener(entry->layer_shell_seat,
                                                       &layer_shell_seat_listener, entry) != 0) {
                std::fprintf(stderr, "Yarfwm: failed to add layer shell seat listener\n");
                river_layer_shell_seat_v1_destroy(entry->layer_shell_seat);
                entry->layer_shell_seat = nullptr;
            }
        }
    }

    seat_count++;
}

void Seat::terminate() {
    for (int i = 0; i < seat_count; i++) {
        if (seat_entries[i].layer_shell_seat) {
            river_layer_shell_seat_v1_destroy(seat_entries[i].layer_shell_seat);
            seat_entries[i].layer_shell_seat = nullptr;
        }
        // The river_seat_v1 object itself is destroyed after its removed
        // event; seat removal handling arrives with the keyboard batch.
        seat_entries[i].river_seat = nullptr;
    }
    seat_count = 0;
}

void Seat::layer_shell_seat_focus_exclusive(void *data,
                                            struct river_layer_shell_seat_v1 *layer_shell_seat) {
    SeatEntry *entry = static_cast<SeatEntry *>(data);
    (void)layer_shell_seat;
    entry->layer_surface_focus = layer_focus_exclusive;
    std::fprintf(stderr, "Yarfwm: layer surface took exclusive keyboard focus\n");
}

void Seat::layer_shell_seat_focus_non_exclusive(void *data,
                                                struct river_layer_shell_seat_v1 *layer_shell_seat) {
    SeatEntry *entry = static_cast<SeatEntry *>(data);
    (void)layer_shell_seat;
    entry->layer_surface_focus = layer_focus_non_exclusive;
    std::fprintf(stderr, "Yarfwm: layer surface wants non-exclusive keyboard focus\n");
}

void Seat::layer_shell_seat_focus_none(void *data,
                                       struct river_layer_shell_seat_v1 *layer_shell_seat) {
    SeatEntry *entry = static_cast<SeatEntry *>(data);
    (void)layer_shell_seat;
    entry->layer_surface_focus = layer_focus_none;
    // Returning focus to the last window is the seat focus work; the state is
    // tracked from here on so that work has it available.
    std::fprintf(stderr, "Yarfwm: no layer surface holds keyboard focus\n");
}
