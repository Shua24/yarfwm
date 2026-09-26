// Unit tests for config parsing.
//
// Config::load reads $HOME: when no candidate file parses it writes the
// built-in default to $HOME/.config/yarfwm/config.json (see src/Config.cpp).
// Every test therefore redirects HOME to a scratch directory, so a test can
// never overwrite the developer's real config.
#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

#include "Config.hpp"

namespace
{

class ConfigTest : public ::testing::Test
{
      protected:
	void SetUp() override
	{
		const char *home = std::getenv("HOME");
		had_home = home != nullptr;
		saved_home = home ? home : "";

		const std::filesystem::path base =
		    std::filesystem::temp_directory_path() /
		    ("yarfwm-config-test-" + std::to_string(::getpid()) + "-" +
		     std::to_string(counter));
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

	// Where load() writes the embedded default when nothing parses.
	std::string default_config_path() const
	{
		return scratch + "/.config/yarfwm/config.json";
	}

	static int counter;
	std::string saved_home;
	bool had_home = false;
	std::string scratch;
};

int ConfigTest::counter = 0;

TEST_F(ConfigTest, ReadsTheLiveKeys)
{
	const std::string path =
	    write_file(scratch, "config.json",
		       "{\"input\": {\"focus_follows_mouse\": false},"
		       " \"keybinds\": [{\"action\": \"exit_session\"}]}");

	Config config;
	ASSERT_TRUE(config.load(path));
	EXPECT_FALSE(config.input.get("focus_follows_mouse", true).asBool());
	ASSERT_TRUE(config.keybinds.isArray());
	EXPECT_EQ(config.keybinds.size(), 1U);
}

TEST_F(ConfigTest, MissingFocusFollowsMouseDefaultsToFalse)
{
	// The default the Seat reads when the key is absent: click-to-focus,
	// the labwc default.
	const std::string path =
	    write_file(scratch, "config.json", "{\"keybinds\": []}");
	Config config;
	ASSERT_TRUE(config.load(path));
	EXPECT_FALSE(config.input.get("focus_follows_mouse", false).asBool());
}

TEST_F(ConfigTest, AbsentKeybindsIsNotAnArray)
{
	// Keybind::initialize reports this case instead of binding nothing.
	const std::string path =
	    write_file(scratch, "config.json", "{\"input\": {}}");
	Config config;
	ASSERT_TRUE(config.load(path));
	EXPECT_FALSE(config.keybinds.isArray());
}

TEST_F(ConfigTest, IgnoresKeysThatLeftTheSchema)
{
	// A hand-edited file from before the schema slim-down carries every
	// removed key; the load must ignore them, not fail.
	const std::string path =
	    write_file(scratch, "config.json",
		       "{\"layout\": {\"gaps\": 4}, \"outputs\": {},"
		       " \"workspaces\": [\"ID\", \"JP\"],"
		       " \"spawn_at_startup\": [\"waybar\"],"
		       " \"window_rules\": [], \"prefer_no_csd\": true,"
		       " \"screenshot_path\": \"/tmp/shot.png\","
		       " \"input\": {\"focus_follows_mouse\": true},"
		       " \"keybinds\": [{\"action\": \"exit_session\"}]}");

	Config config;
	ASSERT_TRUE(config.load(path));
	ASSERT_TRUE(config.keybinds.isArray());
	EXPECT_EQ(config.keybinds.size(), 1U);
	// The file supplies true; the fallback literal is the new absent-key
	// default, so the assertion is about the file value either way.
	EXPECT_TRUE(config.input.get("focus_follows_mouse", false).asBool());
}

TEST_F(ConfigTest, ReloadingStaysStable)
{
	const std::string path =
	    write_file(scratch, "config.json",
		       "{\"keybinds\": [{\"action\": \"exit_session\"},"
		       " {\"action\": \"spawn\"}]}");
	Config config;
	ASSERT_TRUE(config.load(path));
	ASSERT_TRUE(config.load(path));
	ASSERT_TRUE(config.keybinds.isArray());
	EXPECT_EQ(config.keybinds.size(), 2U);
}

TEST_F(ConfigTest, MissingFileWritesTheEmbeddedDefault)
{
	// load() never fails on a missing path: it falls through to the
	// default-write path and loads what it wrote.
	Config config;
	ASSERT_TRUE(config.load(scratch + "/does-not-exist.json"));
	ASSERT_TRUE(std::filesystem::exists(default_config_path()));

	Config written;
	ASSERT_TRUE(written.load(default_config_path()));
	ASSERT_TRUE(written.keybinds.isArray());
	// 47 since toggle_decorations (Super+Shift+D) joined the default set.
	EXPECT_EQ(written.keybinds.size(), 47U);
	// The embedded default is click-to-focus now, the labwc default.
	EXPECT_FALSE(written.input.get("focus_follows_mouse", false).asBool());
}

TEST_F(ConfigTest, MalformedFileFallsBackToTheDefault)
{
	const std::string path =
	    write_file(scratch, "broken.json", "{ this is not json");
	Config config;
	ASSERT_TRUE(config.load(path));
	ASSERT_TRUE(config.keybinds.isArray());
	// 47: the embedded default, including toggle_decorations.
	EXPECT_EQ(config.keybinds.size(), 47U);
}

} // namespace
