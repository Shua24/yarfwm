#include "Yarfwm.hpp"
#include <cstdio>

bool Yarfwm::initialize(int argc, char *argv[]) {
    // Dynamic memory: config is heap-allocated during parsing from config.json,
    // then copied into fixed Config members; raw config object freed at end of load.
    (void)argc;
    (void)argv;

    if (!config.load("config.json")) {
        return false;
    }

    // B13: connect to the compositor before anything that needs the registry.
    if (!server.initialize()) {
        return false;
    }

    // Registry globals (river_window_manager_v1, river_layer_shell_v1) arrive
    // during the roundtrip, so it must complete before anything inspects them.
    if (!display.initialize(&server, config)) {
        server.terminate();
        return false;
    }

    if (!display.roundtrip(&server)) {
        std::fprintf(stderr, "Yarfwm: failed to complete registry roundtrip\n");
        display.terminate();
        server.terminate();
        return false;
    }

    if (!display.window_manager) {
        std::fprintf(stderr, "Yarfwm: compositor does not offer river_window_manager_v1\n");
        display.terminate();
        server.terminate();
        return false;
    }

    if (!seat.initialize(&server, &display, config)) {
        display.terminate();
        server.terminate();
        return false;
    }

    if (!view.initialize(&server, &seat, &display, config)) {
        seat.terminate();
        display.terminate();
        server.terminate();
        return false;
    }

    if (!keybind.initialize(&server, &seat, &view, config)) {
        view.terminate();
        seat.terminate();
        display.terminate();
        server.terminate();
        return false;
    }

    running = true;
    return true;
}

int Yarfwm::run() {
    if (!server.display || !server.registry) {
        return 1;
    }

    while (running) {
        // C8: the server asks for shutdown through a flag; the loop exits and
        // teardown happens outside any event callback.
        if (view.should_shutdown()) {
            break;
        }

        if (wl_display_flush(server.display) < 0) {
            break;
        }

        if (wl_display_dispatch(server.display) == -1) {
            break;
        }
    }

    terminate();
    return 0;
}

void Yarfwm::terminate() {
    running = false;
    keybind.terminate();
    view.terminate();
    seat.terminate();
    display.terminate();
    server.terminate();
}
