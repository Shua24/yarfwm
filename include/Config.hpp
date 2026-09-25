#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <json/json.h>
#include <string>
#include <vector>

// Represents parsed runtime configuration loaded from config.json.
//
// Only the settings with a consumer are carried: input.focus_follows_mouse
// (read by Seat) and the keybinds array (read by Keybind). A key nothing
// reads is not a setting; it is removed rather than stored.
class Config
{
      public:
	Config();
	~Config();

	bool load(const std::string &path);

	Json::Value input;
	Json::Value keybinds;

      private:
	std::string user_config_path() const;
	bool write_default_if_missing() const;
	bool parse_file(const std::string &path);
};

#endif // CONFIG_HPP
