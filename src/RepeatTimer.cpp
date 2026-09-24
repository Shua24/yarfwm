#include "RepeatTimer.hpp"

#include <cstdint>
#include <cstdio>
#include <sys/timerfd.h>
#include <unistd.h>

RepeatTimer::RepeatTimer() : timer_descriptor(-1) {}

RepeatTimer::~RepeatTimer() { terminate(); }

bool RepeatTimer::initialize()
{
	if (timer_descriptor >= 0) {
		return true;
	}

	// Non-blocking so consume() can never stall the main loop, and
	// close-on-exec so a spawned program does not inherit it.
	timer_descriptor =
	    timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
	if (timer_descriptor < 0) {
		std::fprintf(stderr,
			     "Yarfwm: failed to create the key repeat timer\n");
		return false;
	}
	return true;
}

void RepeatTimer::terminate()
{
	if (timer_descriptor >= 0) {
		close(timer_descriptor);
		timer_descriptor = -1;
	}
}

int RepeatTimer::file_descriptor() const { return timer_descriptor; }

static void set_timer(int descriptor, int interval_milliseconds,
		      int value_milliseconds)
{
	struct itimerspec specification = {};

	specification.it_interval.tv_sec = interval_milliseconds / 1000;
	specification.it_interval.tv_nsec =
	    static_cast<long>(interval_milliseconds % 1000) * 1000000L;
	specification.it_value.tv_sec = value_milliseconds / 1000;
	specification.it_value.tv_nsec =
	    static_cast<long>(value_milliseconds % 1000) * 1000000L;

	timerfd_settime(descriptor, 0, &specification, nullptr);
}

void RepeatTimer::arm(int delay_milliseconds, int interval_milliseconds)
{
	if (timer_descriptor < 0) {
		return;
	}
	set_timer(timer_descriptor, interval_milliseconds, delay_milliseconds);
}

void RepeatTimer::disarm()
{
	if (timer_descriptor < 0) {
		return;
	}
	// A zero value with a zero interval stops the timer.
	set_timer(timer_descriptor, 0, 0);
}

void RepeatTimer::consume()
{
	if (timer_descriptor < 0) {
		return;
	}

	// The kernel counts expirations here; reading clears readability. This
	// only runs when poll() reported the descriptor.
	std::uint64_t expirations = 0;
	const ssize_t bytes_read =
	    read(timer_descriptor, &expirations, sizeof(expirations));
	(void)bytes_read;
}
