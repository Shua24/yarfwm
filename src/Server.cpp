#include "Server.hpp"
#include <cstring>
#include <cstdio>
#include <cstdlib>

Server::Server() : display(nullptr), registry(nullptr) {
    std::memset(socket_name, 0, sizeof(socket_name));
}

Server::~Server() {
    terminate();
}

bool Server::initialize() {
    display = wl_display_connect(nullptr);
    if (!display) {
        const char *wayland_display = std::getenv("WAYLAND_DISPLAY");
        std::fprintf(stderr, "Yarfwm: failed to connect to Wayland display: %s\n",
                      wayland_display ? wayland_display : "(null)");
        return false;
    }

    registry = wl_display_get_registry(display);
    if (!registry) {
        std::fprintf(stderr, "Yarfwm: failed to get Wayland registry\n");
        wl_display_disconnect(display);
        display = nullptr;
        return false;
    }

    return true;
}

void Server::terminate() {
    if (registry) {
        wl_registry_destroy(registry);
        registry = nullptr;
    }
    if (display) {
        wl_display_disconnect(display);
        display = nullptr;
    }
}
