#ifndef YARFWM_HPP
#define YARFWM_HPP

#include "ChildProcesses.hpp"
#include "Config.hpp"
#include "Display.hpp"
#include "Keybind.hpp"
#include "Seat.hpp"
#include "Server.hpp"
#include "View.hpp"
#include "WindowDecorationsConfig.hpp"

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

	// The decoration appearance, from its own file: a bad colour here must
	// never be able to stop the window manager or break a key binding.
	WindowDecorations window_decorations;

	// The programs river's init script started before it exec'd this
	// process, plus anything started since. Owns the SIGCHLD reaping and
	// the shutdown of those children.
	ChildProcesses child_processes;

	bool running = false;
};

#endif // YARFWM_HPP
