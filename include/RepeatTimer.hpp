#ifndef REPEAT_TIMER_HPP
#define REPEAT_TIMER_HPP

// A timer used to repeat a held key binding.
//
// River does not repeat a binding itself: it sends pressed once, then either
// stop_repeat (another key was pressed first) or released (the bound key went
// up). Like att_wm (Wm.zig repeat_fd), the window manager runs its own timer
// and performs the action again while the key is held.
//
// The timer is a timerfd, so the main loop can poll it next to the Wayland
// display descriptor. Nothing sleeps and no signal handler is needed.
class RepeatTimer
{
      public:
	RepeatTimer();
	~RepeatTimer();

	// Returns false when the kernel refuses the timer; the binding engine
	// then keeps working without key repeat.
	bool initialize();
	void terminate();

	// Descriptor to poll for readability, or -1 when unavailable.
	int file_descriptor() const;

	// Start (or restart) the timer: first expiry after the delay, then one
	// every interval until disarm().
	void arm(int delay_milliseconds, int interval_milliseconds);
	void disarm();

	// Clear the readable state after the timer fired.
	void consume();

      private:
	int timer_descriptor;
};

#endif // REPEAT_TIMER_HPP
