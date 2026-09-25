// Unit tests for the key binding parser: key name -> keysym, modifier array
// -> river's modifier mask, and action name -> action kind.
//
// The public parsing surface exercised here exists because of refactor R1
// (the declarations moved from private: to public: in include/Keybind.hpp);
// nothing else about the engine needs a Wayland connection to test.
#include <gtest/gtest.h>

#include <xkbcommon/xkbcommon.h>

#include <string>
#include <vector>

#include "Keybind.hpp"
#include "river-window-management-v1-client-protocol.h"

namespace
{

Json::Value modifiers_of(std::initializer_list<const char *> names)
{
	Json::Value bind;
	bind["modifiers"] = Json::Value(Json::arrayValue);
	for (const char *name : names) {
		bind["modifiers"].append(name);
	}
	return bind;
}

} // namespace

TEST(Keysym, AsciiLettersFoldToLowercase)
{
	// River matches the base-layer keysym, so Super+Shift+R arrives as
	// 'r'; the parser registers the lowercase form.
	EXPECT_EQ(Keybind::resolve_keysym("A"),
		  static_cast<uint32_t>(XKB_KEY_a));
	EXPECT_EQ(Keybind::resolve_keysym("a"),
		  static_cast<uint32_t>(XKB_KEY_a));
}

TEST(Keysym, CanonicalCaseNamesResolve)
{
	// "Slash" is not the canonical spelling ("slash"); the parser only
	// retries case-insensitively after the first lookup fails.
	EXPECT_NE(Keybind::resolve_keysym("Slash"), 0U);
	EXPECT_EQ(Keybind::resolve_keysym("Slash"),
		  Keybind::resolve_keysym("slash"));
}

TEST(Keysym, UnknownNamesAreZero)
{
	EXPECT_EQ(Keybind::resolve_keysym("NotAKeyName"), 0U);
}

TEST(Modifiers, BitsMatchTheProtocolValues)
{
	// The values the XML defines; a regeneration that changed them would
	// silently break every binding.
	EXPECT_EQ(RIVER_SEAT_V1_MODIFIERS_SHIFT, 1U);
	EXPECT_EQ(RIVER_SEAT_V1_MODIFIERS_CTRL, 4U);
	EXPECT_EQ(RIVER_SEAT_V1_MODIFIERS_MOD1, 8U);
	EXPECT_EQ(RIVER_SEAT_V1_MODIFIERS_MOD4, 64U);
}

TEST(Modifiers, SuperAndShiftCombine)
{
	bool resolved = false;
	const Json::Value bind = modifiers_of({"Super", "Shift"});
	EXPECT_EQ(Keybind::resolve_modifiers(bind, &resolved),
		  RIVER_SEAT_V1_MODIFIERS_MOD4 | RIVER_SEAT_V1_MODIFIERS_SHIFT);
	EXPECT_TRUE(resolved);
}

TEST(Modifiers, AliasesResolve)
{
	bool resolved = false;
	const Json::Value bind = modifiers_of({"Control", "Logo"});
	EXPECT_EQ(Keybind::resolve_modifiers(bind, &resolved),
		  RIVER_SEAT_V1_MODIFIERS_CTRL | RIVER_SEAT_V1_MODIFIERS_MOD4);
	EXPECT_TRUE(resolved);
}

TEST(Modifiers, UnknownNameMarksUnresolved)
{
	bool resolved = true;
	const Json::Value bind = modifiers_of({"Hyper"});
	Keybind::resolve_modifiers(bind, &resolved);
	EXPECT_FALSE(resolved);
}

TEST(Modifiers, RequiresShiftFoldsIn)
{
	Json::Value bind = modifiers_of({"Super"});
	bind["requires_shift"] = true;
	bool resolved = false;
	EXPECT_EQ(Keybind::resolve_modifiers(bind, &resolved),
		  RIVER_SEAT_V1_MODIFIERS_MOD4 | RIVER_SEAT_V1_MODIFIERS_SHIFT);
	EXPECT_TRUE(resolved);
}

TEST(Actions, KnownNamesMapToTheirKinds)
{
	EXPECT_EQ(Keybind::parse_action("spawn", {}).kind,
		  Keybind::action_spawn);
	EXPECT_EQ(Keybind::parse_action("close_window", {}).kind,
		  Keybind::action_close_window);
	EXPECT_EQ(Keybind::parse_action("quit", {}).kind, Keybind::action_quit);
	EXPECT_EQ(Keybind::parse_action("exit_session", {}).kind,
		  Keybind::action_exit_session);
	EXPECT_EQ(Keybind::parse_action("focus_window_previous", {}).kind,
		  Keybind::action_focus_previous);
	EXPECT_EQ(Keybind::parse_action("toggle_maximize", {}).kind,
		  Keybind::action_toggle_maximize);
	EXPECT_EQ(Keybind::parse_action("fullscreen_window", {}).kind,
		  Keybind::action_fullscreen);
	EXPECT_EQ(Keybind::parse_action("toggle_always_on_top", {}).kind,
		  Keybind::action_toggle_always_on_top);
	EXPECT_EQ(Keybind::parse_action("minimize_window", {}).kind,
		  Keybind::action_minimize);
	EXPECT_EQ(Keybind::parse_action("restore_minimized_window", {}).kind,
		  Keybind::action_restore_minimized);
	EXPECT_EQ(Keybind::parse_action("center_window", {}).kind,
		  Keybind::action_center_window);
	EXPECT_EQ(Keybind::parse_action("center_all_windows", {}).kind,
		  Keybind::action_center_all_windows);
	EXPECT_EQ(Keybind::parse_action("fit_to_output", {}).kind,
		  Keybind::action_fit_to_output);
}

TEST(Actions, FocusWindowSetsDirection)
{
	EXPECT_EQ(Keybind::parse_action("focus_window_left", {}).direction,
		  focus_direction_left);
	EXPECT_EQ(Keybind::parse_action("focus_window_up", {}).direction,
		  focus_direction_up);
	EXPECT_EQ(Keybind::parse_action("focus_window_left", {}).kind,
		  Keybind::action_focus_direction);
}

TEST(Actions, MoveWindowSetsDirection)
{
	EXPECT_EQ(Keybind::parse_action("move_window_right", {}).direction,
		  focus_direction_right);
	EXPECT_EQ(Keybind::parse_action("move_window_down", {}).direction,
		  focus_direction_down);
	EXPECT_EQ(Keybind::parse_action("move_window_right", {}).kind,
		  Keybind::action_move_direction);
}

TEST(Actions, DesktopActionsCarryTheirStep)
{
	EXPECT_EQ(Keybind::parse_action("focus_desktop_next", {}).amount, 1);
	EXPECT_EQ(Keybind::parse_action("focus_desktop_previous", {}).amount,
		  -1);
	EXPECT_EQ(
	    Keybind::parse_action("move_window_to_desktop_next", {}).amount, 1);
	EXPECT_EQ(
	    Keybind::parse_action("move_window_to_desktop_previous", {}).amount,
	    -1);
}

TEST(Actions, ResizeActionsReadThePercentage)
{
	EXPECT_EQ(Keybind::parse_action("set_window_width", {"-10%"}).amount,
		  -10);
	EXPECT_EQ(Keybind::parse_action("set_window_height", {"+10%"}).amount,
		  10);
	EXPECT_EQ(Keybind::parse_action("set_window_width", {"bogus"}).amount,
		  0);
}

TEST(Actions, DroppedAndUnknownVerbsAreActionNone)
{
	// The two dropped verbs are plain unknown names now: reported once
	// and skipped like any typo.
	EXPECT_EQ(Keybind::parse_action("toggle_expose", {}).kind,
		  Keybind::action_none);
	EXPECT_EQ(Keybind::parse_action("show_hotkey_overlay", {}).kind,
		  Keybind::action_none);
	EXPECT_EQ(Keybind::parse_action("no_such_action", {}).kind,
		  Keybind::action_none);
}

TEST(Arguments, StringsAreCollectedInOrder)
{
	Json::Value bind;
	bind["args"] = Json::Value(Json::arrayValue);
	bind["args"].append("kitty");
	bind["args"].append("-e");
	bind["args"].append(7); // non-strings are skipped
	bind["args"].append("top");

	const std::vector<std::string> arguments =
	    Keybind::read_arguments(bind);
	ASSERT_EQ(arguments.size(), 3U);
	EXPECT_EQ(arguments[0], "kitty");
	EXPECT_EQ(arguments[1], "-e");
	EXPECT_EQ(arguments[2], "top");
}
