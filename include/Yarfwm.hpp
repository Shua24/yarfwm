#ifndef YARFWM_HPP
#define YARFWM_HPP

#include "Config.hpp"
#include "Display.hpp"
#include "Keybind.hpp"
#include "Seat.hpp"
#include "Server.hpp"
#include "View.hpp"

class Yarfwm
{
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
