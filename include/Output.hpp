#ifndef OUTPUT_HPP
#define OUTPUT_HPP

#include <cstdint>

struct river_output_v1;
struct river_layer_shell_output_v1;

class LayerShell;
class View;

// Represents one managed output and its layer shell state.
//
// river hands the window manager one river_output_v1 per output; its events
// report position and dimensions. The paired river_layer_shell_output_v1
// object (created through river_layer_shell_v1.get_output) reports the
// non-exclusive area: the output rectangle left after subtracting the
// exclusive zones of layer surfaces such as bars and docks. That area is the
// placement hint for windows. set_default() marks this output as the one new
// layer surfaces land on when they do not name an output themselves, and may
// only be sent inside a manage sequence.
//
// Dynamic memory: one Output object is heap-allocated per output by the View
// that owns it; terminate() destroys the protocol objects it holds and the
// View then deletes the object itself.
class Output {
public:
    Output();
    ~Output();

    bool initialize(LayerShell &layer_shell, struct river_output_v1 *output, View *view);
    void terminate();

    // Returns true when the set_default request was actually sent (layer shell
    // state present).
    bool set_default();

    struct river_output_v1 *river_output;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    bool has_dimensions;

    bool has_non_exclusive_area;
    int32_t non_exclusive_area_x;
    int32_t non_exclusive_area_y;
    int32_t non_exclusive_area_width;
    int32_t non_exclusive_area_height;

private:
    static void river_output_removed(void *data, struct river_output_v1 *output);
    static void river_output_wl_output(void *data, struct river_output_v1 *output, uint32_t name);
    static void river_output_position(void *data, struct river_output_v1 *output,
                                      int32_t x, int32_t y);
    static void river_output_dimensions(void *data, struct river_output_v1 *output,
                                        int32_t width, int32_t height);
    static void river_output_capture_sessions(void *data, struct river_output_v1 *output,
                                              uint32_t count);
    static void layer_shell_output_non_exclusive_area(
        void *data, struct river_layer_shell_output_v1 *layer_shell_output,
        int32_t x, int32_t y, int32_t width, int32_t height);

    struct river_layer_shell_output_v1 *layer_shell_output;
    View *view;
};

#endif // OUTPUT_HPP
