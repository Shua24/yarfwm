#include "Keybind.hpp"

Keybind::Keybind() = default;

Keybind::~Keybind() = default;

bool Keybind::initialize(Server *server, Seat *seat, View *view, Config &config) {
    return true;
}

void Keybind::terminate() {
}
