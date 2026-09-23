#ifndef SERVER_HPP
#define SERVER_HPP

#include <wayland-client.h>

// Represents the Wayland client connection to the running River compositor.
class Server {
public:
    Server();
    ~Server();

    bool initialize();
    void terminate();

    struct wl_display *display;
    struct wl_registry *registry;
    char socket_name[64];
};

#endif // SERVER_HPP
