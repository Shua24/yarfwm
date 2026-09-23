#ifndef YARFWM_HPP
#define YARFWM_HPP

#include "Server.hpp"
#include "Display.hpp"
#include "Seat.hpp"
#include "View.hpp"
#include "Config.hpp"
#include "Keybind.hpp"

class Yarfwm {
public:
    Yarfwm() = default;
    ~Yarfwm() = default;

    bool initialize(int argc, char *argv[]);
    int run();
    void terminate();

private:
    Server server;
    Display display;
    Seat seat;
    View view;
    Config config;
    Keybind keybind;

    bool running = false;
};

#endif // YARFWM_HPP
