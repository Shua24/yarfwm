// Unit tests for the decoration settings file.
//
// WindowDecorations::load reads $HOME: when no candidate file parses it writes
// the built-in default to $HOME/.config/yarfwm/window-decorations.json (see
// src/WindowDecorationsConfig.cpp). Every test therefore redirects HOME to a
// scratch directory, so a test can never overwrite the developer's real file.
//
// The scratch-HOME pattern is not optional here: a naive test would write the
// embedded default into the developer's actual config directory.
#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

#include "WindowDecorationsConfig.hpp"

namespace
{

class WindowDecorationsTest : public ::testing::Test
{
      protected:
	void SetUp() override
	{
		const char *home = std::getenv("HOME");
		had_home = home != nullptr;
		saved_home = home ? home : "";

		const std::filesystem::path base =
		    std::filesystem::temp_directory_path() /
		    ("yarfwm-decorations-test-" + std::to_string(::getpid()) +
		     "-" + std::to_string(counter));
		counter++;
		std::filesystem::create_directories(base);
		scratch = base.string();

		::setenv("HOME", scratch.c_str(), 1);
	}

	void TearDown() override
	{
		if (had_home) {
			::setenv("HOME", saved_home.c_str(), 1);
		} else {
			::unsetenv("HOME");
		}
		std::error_code ignored;
		std::filesystem::remove_all(scratch, ignored);
	}

	// Write text into the scratch directory, return the full path.
	static std::string write_file(const std::string &scratch,
				      const std::string &name,
				      const std::string &text)
	{
		const std::string path = scratch + "/" + name;
		std::ofstream output(path);
		output << text;
		return path;
	}

	static int counter;
	std::string scratch;
	std::string saved_home;
	bool had_home = false;
};

int WindowDecorationsTest::counter = 0;

} // namespace

// The colour parser is pure, so it is tested directly rather than through a
// file. Every rejection must return the fallback: a typo must never produce a
// transparent titlebar.
TEST(DecorationColor, ParsesEightHexDigits)
{
	EXPECT_EQ(parse_decoration_color("ff5c8fb0", 0u), 0xff5c8fb0u);
	EXPECT_EQ(parse_decoration_color("FF5C8FB0", 0u), 0xff5c8fb0u);
	EXPECT_EQ(parse_decoration_color("00000000", 0xffu), 0x00000000u);
	EXPECT_EQ(parse_decoration_color("ffffffff", 0u), 0xffffffffu);
}

TEST(DecorationColor, RejectsAnythingElseAndReturnsTheFallback)
{
	EXPECT_EQ(parse_decoration_color("", 0xffabcdefu), 0xffabcdefu);
	EXPECT_EQ(parse_decoration_color("fff", 0xffabcdefu), 0xffabcdefu);
	EXPECT_EQ(parse_decoration_color("ff5c8fb", 0xffabcdefu), 0xffabcdefu);
	EXPECT_EQ(parse_decoration_color("ff5c8fb00", 0xffabcdefu),
		  0xffabcdefu);
	EXPECT_EQ(parse_decoration_color("#ff5c8fb", 0xffabcdefu), 0xffabcdefu);
	EXPECT_EQ(parse_decoration_color("gg5c8fb0", 0xffabcdefu), 0xffabcdefu);
}

TEST(WindowDecorationsConfig, FallbacksAreTheDocumentedDefaults)
{
	// A WindowDecorations that never loaded a file must still be usable.
	WindowDecorations decorations;
	const WindowDecorationsConfig &values = decorations.settings();
	EXPECT_TRUE(values.enabled);
	EXPECT_EQ(values.titlebar_height, 26);
	EXPECT_EQ(values.border_width, 1);
	EXPECT_EQ(values.button_width, 26);
	EXPECT_EQ(values.button_spacing, 0);
	EXPECT_EQ(values.padding, 0);
	EXPECT_TRUE(values.titlebar_when_maximized);
	EXPECT_EQ(values.font, "JetBrains Mono 10");
	EXPECT_EQ(values.border_active_color, 0xff5c8fb0u);
	EXPECT_EQ(values.border_inactive_color, 0xff404040u);
}

TEST_F(WindowDecorationsTest, AGeneratedFileMatchesTheFallbacks)
{
	// The embedded default and the hard-coded fallbacks must agree, or a
	// fresh user gets different decorations from an existing one. This is
	// the drift check: edit data/window-decorations.json.in without
	// editing default_values() and this test fails.
	WindowDecorations decorations;
	decorations.load("does-not-exist.json");

	const WindowDecorationsConfig &values = decorations.settings();
	EXPECT_TRUE(values.enabled);
	EXPECT_EQ(values.titlebar_height, 26);
	EXPECT_EQ(values.border_width, 1);
	EXPECT_EQ(values.button_width, 26);
	EXPECT_EQ(values.button_spacing, 0);
	EXPECT_EQ(values.padding, 0);
	EXPECT_TRUE(values.titlebar_when_maximized);
	EXPECT_EQ(values.font, "JetBrains Mono 10");
	EXPECT_EQ(values.titlebar_active_color, 0xff203040u);
	EXPECT_EQ(values.titlebar_inactive_color, 0xff404040u);
	EXPECT_EQ(values.text_active_color, 0xffffffffu);
	EXPECT_EQ(values.text_inactive_color, 0xffccccccu);
	EXPECT_EQ(values.button_glyph_color, 0xffffffffu);
	EXPECT_EQ(values.border_active_color, 0xff5c8fb0u);
	EXPECT_EQ(values.border_inactive_color, 0xff404040u);

	// And the file really was written, at the documented path.
	EXPECT_TRUE(std::filesystem::exists(
	    scratch + "/.config/yarfwm/window-decorations.json"));
}

TEST_F(WindowDecorationsTest, AnExistingFileIsNeverOverwritten)
{
	const std::string path = write_file(scratch, "window-decorations.json",
					    "{\"titlebar_height\": 40}");

	WindowDecorations decorations;
	decorations.load(path);
	EXPECT_EQ(decorations.settings().titlebar_height, 40);

	// A second load must not rewrite it.
	WindowDecorations second;
	second.load(path);
	std::ifstream file(path);
	std::string contents((std::istreambuf_iterator<char>(file)),
			     std::istreambuf_iterator<char>());
	EXPECT_NE(contents.find("40"), std::string::npos);
	EXPECT_EQ(second.settings().titlebar_height, 40);
}

TEST_F(WindowDecorationsTest, APartialFileKeepsTheFallbacksForAbsentKeys)
{
	// Only titlebar_height is present; everything else must stay default.
	// A partial file is NOT an error.
	const std::string path =
	    write_file(scratch, "partial.json", "{\"titlebar_height\": 40}");

	WindowDecorations decorations;
	decorations.load(path);

	const WindowDecorationsConfig &values = decorations.settings();
	EXPECT_EQ(values.titlebar_height, 40);
	EXPECT_EQ(values.border_width, 1);	     // fallback survived
	EXPECT_EQ(values.font, "JetBrains Mono 10"); // fallback survived
	EXPECT_TRUE(values.enabled);		     // fallback survived
}

TEST_F(WindowDecorationsTest, ABadColourFallsBackRatherThanGoingTransparent)
{
	const std::string path =
	    write_file(scratch, "bad-colour.json",
		       "{\"border_active_color\": \"not-a-colour\"}");

	WindowDecorations decorations;
	decorations.load(path);

	// The fallback, not 0 (transparent) and not garbage.
	EXPECT_EQ(decorations.settings().border_active_color, 0xff5c8fb0u);
}

TEST_F(WindowDecorationsTest, AMalformedFileLeavesTheDefaultsIntact)
{
	const std::string path =
	    write_file(scratch, "malformed.json", "{ this is not json ");

	WindowDecorations decorations;
	decorations.load(path);

	// load() must not fail the caller: the defaults are still in place.
	EXPECT_EQ(decorations.settings().titlebar_height, 26);
	EXPECT_EQ(decorations.settings().border_width, 1);
	EXPECT_TRUE(decorations.settings().enabled);
}

TEST_F(WindowDecorationsTest, DisablingDecorationsIsHonoured)
{
	const std::string path =
	    write_file(scratch, "disabled.json", "{\"enabled\": false}");

	WindowDecorations decorations;
	decorations.load(path);
	EXPECT_FALSE(decorations.settings().enabled);
}

TEST_F(WindowDecorationsTest, NoHomeDirectoryStillYieldsUsableSettings)
{
	::unsetenv("HOME");

	WindowDecorations decorations;
	decorations.load("does-not-exist.json");

	EXPECT_EQ(decorations.settings().titlebar_height, 26);
	EXPECT_TRUE(decorations.settings().enabled);
}
