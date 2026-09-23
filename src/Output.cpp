#include "Output.hpp"
#include "View.hpp"
#include "LayerShell.hpp"
#include "river-layer-shell-v1-client-protocol.h"
#include "river-window-management-v1-client-protocol.h"
#include <cstdio>

Output::Output()
    : river_output(nullptr),
      x(0),
      y(0),
      width(0),
      height(0),
      has_dimensions(false),
      has_non_exclusive_area(false),
      non_exclusive_area_x(0),
      non_exclusive_area_y(0),
      non_exclusive_area_width(0),
      non_exclusive_area_height(0),
      layer_shell_output(nullptr),
      view(nullptr) {
}

Output::~Output() {
    terminate();
}

bool Output::initialize(LayerShell &layer_shell, struct river_output_v1 *output, View *owner) {
    river_output = output;
    view = owner;

    static const struct river_output_v1_listener output_listener = {
        river_output_removed,
        river_output_wl_output,
        river_output_position,
        river_output_dimensions,
        river_output_capture_sessions,
    };

    if (river_output_v1_add_listener(output, &output_listener, this) != 0) {
        std::fprintf(stderr, "Yarfwm: failed to add output listener\n");
        return false;
    }

    // Layer shell is optional: river only offers the global to a window
    // manager client, and an older river may not offer it at all.
    if (layer_shell.available()) {
        layer_shell_output = layer_shell.create_output_state(output);
        if (layer_shell_output) {
            static const struct river_layer_shell_output_v1_listener
                layer_shell_output_listener = {
                    layer_shell_output_non_exclusive_area,
                };

            if (river_layer_shell_output_v1_add_listener(layer_shell_output,
                                                         &layer_shell_output_listener,
                                                         this) != 0) {
                std::fprintf(stderr, "Yarfwm: failed to add layer shell output listener\n");
                river_layer_shell_output_v1_destroy(layer_shell_output);
                layer_shell_output = nullptr;
            }
        }
    }

    return true;
}

void Output::terminate() {
    // Children first: the layer shell state is made inert by the
    // river_output_v1.removed event and is destroyed before the output itself.
    if (layer_shell_output) {
        river_layer_shell_output_v1_destroy(layer_shell_output);
        layer_shell_output = nullptr;
    }
    if (river_output) {
        river_output_v1_destroy(river_output);
        river_output = nullptr;
    }
}

bool Output::set_default() {
    if (!layer_shell_output) {
        return false;
    }
    river_layer_shell_output_v1_set_default(layer_shell_output);
    std::fprintf(stderr, "Yarfwm: set default layer output %dx%d\n", width, height);
    return true;
}

void Output::river_output_removed(void *data, struct river_output_v1 *output) {
    Output *output_entry = static_cast<Output *>(data);
    (void)output;
    // The View owns this object: it destroys the protocol objects and frees
    // the entry. Do not touch any member after this call.
    if (output_entry->view) {
        output_entry->view->remove_output(output_entry);
    }
}

void Output::river_output_wl_output(void *data, struct river_output_v1 *output, uint32_t name) {
    (void)data;
    (void)output;
    (void)name;
}

void Output::river_output_position(void *data, struct river_output_v1 *output,
                                   int32_t x, int32_t y) {
    Output *output_entry = static_cast<Output *>(data);
    (void)output;
    output_entry->x = x;
    output_entry->y = y;
}

void Output::river_output_dimensions(void *data, struct river_output_v1 *output,
                                     int32_t width, int32_t height) {
    Output *output_entry = static_cast<Output *>(data);
    (void)output;
    output_entry->width = width;
    output_entry->height = height;
    output_entry->has_dimensions = true;
}

void Output::river_output_capture_sessions(void *data, struct river_output_v1 *output,
                                           uint32_t count) {
    (void)data;
    (void)output;
    (void)count;
}

void Output::layer_shell_output_non_exclusive_area(
    void *data, struct river_layer_shell_output_v1 *layer_shell_output,
    int32_t x, int32_t y, int32_t width, int32_t height) {
    Output *output_entry = static_cast<Output *>(data);
    (void)layer_shell_output;

    output_entry->has_non_exclusive_area = true;
    output_entry->non_exclusive_area_x = x;
    output_entry->non_exclusive_area_y = y;
    output_entry->non_exclusive_area_width = width;
    output_entry->non_exclusive_area_height = height;

    std::fprintf(stderr, "Yarfwm: non-exclusive area %d,%d %dx%d\n", x, y, width, height);
}
