#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <string>
#include <vector>
#include <json/json.h>

// Represents parsed runtime configuration loaded from config.json.
class Config {
public:
    Config();
    ~Config();

    bool load(const std::string &path);

    Json::Value layout;
    Json::Value input;
    Json::Value outputs;
    std::vector<std::string> workspaces;
    std::vector<std::string> spawn_at_startup;
    Json::Value window_rules;
    Json::Value keybinds;
    bool prefer_no_csd;
    std::string screenshot_path;

private:
    std::string user_config_path() const;
    bool write_default_if_missing() const;
    bool parse_file(const std::string &path);
};

#endif // CONFIG_HPP
