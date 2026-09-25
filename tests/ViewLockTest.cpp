// Unit tests for the session lock guard.
//
// A lock/unlock cycle must leave session_is_locked() false: the key binding
// layer refuses every action except quit and exit_session while the guard is
// set, so a flag that is set but never cleared silently kills every binding
// for the rest of the run. That regression shipped once (the unlocked event
// never cleared the flag); this test is the fast guard against it, and the
// live probe in the session notes is the end-to-end proof.
#include <gtest/gtest.h>

#include "View.hpp"

TEST(SessionLockGuard, StartsUnlocked)
{
	View view;
	EXPECT_FALSE(view.session_is_locked());
}

TEST(SessionLockGuard, LockSetsTheGuard)
{
	View view;
	view.window_manager_session_locked(&view, nullptr);
	EXPECT_TRUE(view.session_is_locked());
}

TEST(SessionLockGuard, UnlockClearsTheGuard)
{
	// The exact regression: locked -> unlocked must end unlocked.
	View view;
	view.window_manager_session_locked(&view, nullptr);
	view.window_manager_session_unlocked(&view, nullptr);
	EXPECT_FALSE(view.session_is_locked());
}

TEST(SessionLockGuard, RepeatedCyclesStayConsistent)
{
	View view;
	for (int cycle = 0; cycle < 3; cycle++) {
		view.window_manager_session_locked(&view, nullptr);
		EXPECT_TRUE(view.session_is_locked());
		view.window_manager_session_unlocked(&view, nullptr);
		EXPECT_FALSE(view.session_is_locked());
	}
}

TEST(SessionLockGuard, UnlockWithoutLockIsSafe)
{
	View view;
	view.window_manager_session_unlocked(&view, nullptr);
	EXPECT_FALSE(view.session_is_locked());
}
