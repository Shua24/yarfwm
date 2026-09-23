#ifndef KEYBIND_HPP
#define KEYBIND_HPP

#include "Server.hpp"
#include "Seat.hpp"
#include "View.hpp"

class Config;

class Keybind {
public:
    Keybind();
    ~Keybind();

    bool initialize(Server *server, Seat *seat, View *view, Config &config);
    void terminate();
};

#endif // KEYBIND_HPP
