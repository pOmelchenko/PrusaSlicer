#pragma once

#include <variant>
#include <string>
#include <string_view>
#include <map>

#include <boost/filesystem/path.hpp>
#include <tl/expected.hpp>

#include "Slic3r/Biz/Lua/LuaEngine.hpp"

namespace Slic3r::App::Lua {

enum class PluginType
{
    ProjectPlugin
};

tl::expected<PluginType, std::string> parse_plugin_type(std::string_view s);
std::string to_string(PluginType type);

using PluginParamValue = std::variant<bool, int, double, std::string>;
using PluginParamValueMap = std::map<std::string, PluginParamValue>;

struct PluginParamDef
{
    std::string name;
    std::string label;
    std::string type;
    std::optional<PluginParamValue> default_value;
    // Display-only dependency on a boolean parameter; hidden values are retained.
    std::optional<std::string> visible_if;
    // Optional plain-text help; never a saved parameter value.
    std::optional<std::string> tooltip;
};

using PluginParamDefs = std::vector<PluginParamDef>;

struct PluginContext
{
    std::string text;
    std::optional<std::string> color;
};

struct PluginMeta
{
    std::string id;
    PluginType type;
    std::optional<std::string> title;
    std::vector<std::string> menu;
    PluginParamDefs params;
    std::optional<std::string> description;
    std::optional<int> dialog_width;
    std::optional<int> input_width;
    bool has_description_callback{false};
    // Fresh display-only text for this opening; never a saved input parameter.
    std::optional<PluginContext> context;
};


class Plugin
{
public:
    const PluginMeta& meta() const { return m_meta; }
    PluginMeta& meta() { return m_meta; }
    const std::string& path() const { return m_path; }

    void execute(Biz::Lua::LuaEngine& lua, const PluginParamValueMap& params) const;
    std::optional<PluginContext> describe(Biz::Lua::LuaEngine& lua) const;

    using ParseResult = tl::expected<Plugin, std::string>;
    static ParseResult
    parse(Biz::Lua::LuaEngine& lua, const std::string& id_prefix, const std::string& path);

private:
    Plugin(std::string  path, PluginMeta  meta);

private:
    std::string m_path;
    PluginMeta m_meta;
};

bool is_path_in_sandbox(
    const boost::filesystem::path& sandbox_path,
    const boost::filesystem::path& tested_path
);

}
