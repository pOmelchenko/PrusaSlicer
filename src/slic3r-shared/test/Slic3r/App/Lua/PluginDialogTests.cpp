#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <boost/filesystem.hpp>
#include <fstream>

#include "Slic3r/App/Lua/PluginDialog.hpp"
#include "Slic3r/App/Yoga/ImGuiFixture.hpp"
#include "Slic3r/App/Yoga/InputTextField.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Validator.hpp"
#include "Slic3r/App/Yoga/ToggleButton.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/Rectangle.hpp"
#include "Slic3r/Biz/Lua/LuaException.hpp"

using Catch::Matchers::WithinAbs;

namespace {

class TestPluginDialog : public Slic3r::App::Lua::PluginDialog
{
public:
    using PluginDialog::PluginDialog;

    Slic3r::App::Yoga::Item* params_content() const
    {
        return content()->get_item(0)->get_item(0);
    }

    Slic3r::App::Yoga::Item* input_page() const { return content()->get_item(0); }
    Slic3r::App::Yoga::Item* result_page() const { return content()->get_item(1); }
};

} // namespace

TEST_CASE("Lua parameter tooltips accept plain text and ignore unsupported metadata", "[Lua][PluginDialog]")
{
    using namespace Slic3r;
    for (const auto& help : {"nil", "42", "false", "{}", "''", "'Line one\\nLine two: 50% <text>'"}) {
        Biz::Lua::LuaEngine lua;
        REQUIRE(lua.run_script(std::string{
            "info={id='help',type='project.plugin',params={{name='reading',type='string',default='',tooltip="}
            + help + "}}}; function execute() end").valid());
        const auto plugin = App::Lua::Plugin::parse(lua, "", "help.lua");
        REQUIRE(plugin.has_value());
        const auto& tooltip = plugin->meta().params.at(0).tooltip;
        if (help[0] == '\'') {
            REQUIRE(tooltip.has_value());
            REQUIRE(*tooltip == (std::string{help} == "''" ? "" : "Line one\nLine two: 50% <text>"));
        } else {
            REQUIRE_FALSE(tooltip.has_value());
        }
    }
}

TEST_CASE_METHOD(ImGuiFixture, "Lua tooltips use native widgets without changing parameter values", "[Lua][PluginDialog]")
{
    using namespace Slic3r::App;
    int runs = 0;
    Lua::PluginParamValueMap values;
    auto* dialog = root.emplace_back<TestPluginDialog>(
        [&](const Lua::PluginMeta&, const Lua::PluginParamValueMap& params) { ++runs; values = params; });
    Lua::PluginMeta meta{.id="tooltips", .type=Lua::PluginType::ProjectPlugin,
        .params={
            {.name="reading", .label="Reading", .type="string", .default_value=std::string{"39.98"}},
            {.name="scale", .label="Scale", .type="float", .default_value=0.5},
            {.name="count", .label="Count", .type="int", .default_value=3},
            {.name="apply", .label="Apply", .type="bool", .default_value=false}
        }};
    for (const std::optional<std::string>& help : std::vector<std::optional<std::string>>{
        "First line\n\nLong help with a literal 50% and <text>; use 39.98;40.00;39.99 as readings.",
        std::string{}, std::nullopt}) {
        for (auto& param : meta.params) param.tooltip = help;
        dialog->show_plugin(meta, {});
        for (size_t i = 0; i < meta.params.size(); ++i) {
            auto* row = dialog->params_content()->get_item(i);
            auto* widget = row->get_item(i == 3 ? 0 : 1);
            Yoga::Tooltip* tooltip = nullptr;
            for (auto* child : widget->objects()) {
                if (auto* candidate = dynamic_cast<Yoga::Tooltip*>(child)) tooltip = candidate;
            }
            REQUIRE(tooltip != nullptr);
            REQUIRE(tooltip->text() == help.value_or(""));
            REQUIRE_FALSE(tooltip->opened());
            if (help && !help->empty()) {
                auto* label = dynamic_cast<Yoga::Text*>(tooltip->content_item()->get_item(0));
                REQUIRE(label != nullptr);
                REQUIRE(label->wrap_mode() == Yoga::Text::WrapMode::Wrap);
                tooltip->content_item()->style_node();
                tooltip->content_item()->resize(default_size_info);
                YGNodeCalculateLayout(tooltip->content_item()->node(), 800, 600, YGDirectionLTR);
                REQUIRE_THAT(tooltip->content_item()->width(), WithinAbs(350, 1));
            }
            if (auto* input = dynamic_cast<Yoga::InputTextField*>(widget)) {
                input->callbacks().hovered_changed(true);
                REQUIRE(tooltip->opened() == (help && !help->empty()));
                input->callbacks().hovered_changed(false);
                REQUIRE_FALSE(tooltip->opened());
            }
        }
        REQUIRE(runs == 0);
    }
    auto* run = dynamic_cast<Yoga::LayoutButton*>(dialog->input_page()->get_item(1)->get_item(0));
    REQUIRE(run != nullptr);
    run->callbacks().action();
    REQUIRE(runs == 1);
    REQUIRE(values.size() == 4);
    REQUIRE(std::get<std::string>(values.at("reading")) == "39.98");
    REQUIRE(std::get<double>(values.at("scale")) == 0.5);
    REQUIRE(std::get<int>(values.at("count")) == 3);
    REQUIRE_FALSE(std::get<bool>(values.at("apply")));
}

TEST_CASE("Lua dialog context is recomputed without executing the plugin", "[Lua][PluginDialog]")
{
    using namespace Slic3r;
    struct TempScript
    {
        boost::filesystem::path path = boost::filesystem::temp_directory_path() /
            boost::filesystem::unique_path("plugin-context-%%%%-%%%%.lua");
        ~TempScript() { boost::filesystem::remove(path); }
        void write(const std::string& source) { std::ofstream(path.string()) << source; }
    } script;
    const std::string source = R"lua(
        info = {id="test.context", type="project.plugin"}
        function execute() error("must not execute when opening the form") end
        function describe() return api.name end
    )lua";
    script.write(source);
    Biz::Lua::LuaEngine scan;
    REQUIRE(scan.run_file(script.path.string()).valid());
    auto parsed = App::Lua::Plugin::parse(scan, "", script.path.string());
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->meta().has_description_callback);

    Biz::Lua::LuaEngine lua;
    lua.state()["api"] = lua.state().create_table_with("name", "PLA");
    lua.set_path_resolver([](const std::string&) { return "previous resolver"; });
    REQUIRE(parsed->describe(lua)->text == "PLA");
    REQUIRE_FALSE(parsed->describe(lua)->color.has_value());
    lua.state()["api"]["name"] = "PETG";
    REQUIRE(parsed->describe(lua)->text == "PETG");
    script.write(source + "\nfunction describe() return api.name, api.color end");
    lua.state()["api"]["color"] = "#1234AB";
    REQUIRE(parsed->describe(lua)->color == "#1234AB");
    lua.state()["api"]["color"] = 123;
    REQUIRE_FALSE(parsed->describe(lua)->color.has_value());
    REQUIRE(lua.resolve_file("test") == "previous resolver");

    SECTION("callback errors restore the resolver") {
        script.write(source + "\nfunction describe() error('context failure') end");
    }
    SECTION("non-text context is rejected") {
        script.write(source + "\nfunction describe() return {} end");
    }
    SECTION("top-level errors are not ignored") {
        script.write(source + "\nerror('load failure')");
    }
    REQUIRE_THROWS_AS(parsed->describe(lua), Biz::Lua::LuaException);
    REQUIRE(lua.resolve_file("test") == "previous resolver");
}

TEST_CASE("Legacy Lua plugins do not run code when opening their forms", "[Lua][PluginDialog]")
{
    using namespace Slic3r;
    Biz::Lua::LuaEngine scan;
    REQUIRE(scan.run_script("info={id='legacy', type='project.plugin'}; function execute() end").valid());
    auto parsed = App::Lua::Plugin::parse(scan, "", "nonexistent-legacy-plugin.lua");
    REQUIRE(parsed.has_value());
    REQUIRE_FALSE(parsed->meta().has_description_callback);
    Biz::Lua::LuaEngine lua;
    REQUIRE_FALSE(parsed->describe(lua).has_value());
}

TEST_CASE_METHOD(ImGuiFixture, "Lua dialog context is read-only and refreshed on reopening", "[Lua][PluginDialog]")
{
    using namespace Slic3r::App;
    Lua::PluginParamValueMap values;
    auto* dialog = root.emplace_back<TestPluginDialog>(
        [&](const Lua::PluginMeta&, const Lua::PluginParamValueMap& params) { values = params; });
    Lua::PluginMeta meta{
        .id="test.context", .type=Lua::PluginType::ProjectPlugin,
        .params={{.name="reading", .label="Reading", .type="string", .default_value=std::string{}}},
        .context=Lua::PluginContext{"Filament: PLA, slot 1", std::nullopt}
    };
    dialog->show_plugin(meta, {{"reading", std::string{"39.98"}}});
    REQUIRE(dynamic_cast<Yoga::Text*>(dialog->input_page()->get_item(0))->text() == meta.context->text);
    auto* run = dynamic_cast<Yoga::LayoutButton*>(dialog->input_page()->get_item(2)->get_item(0));
    run->callbacks().action();
    REQUIRE(values.size() == 1);
    REQUIRE(std::get<std::string>(values.at("reading")) == "39.98");
    dialog->show_result("Preview", "Details");
    REQUIRE(dynamic_cast<Yoga::Text*>(dialog->result_page()->get_item(0)->get_item(0))->text() == meta.context->text);
    meta.context = Lua::PluginContext{"Filament: PETG, slot 1", std::nullopt};
    dialog->show_plugin(meta, values);
    REQUIRE(dynamic_cast<Yoga::Text*>(dialog->input_page()->get_item(0))->text() == meta.context->text);
    REQUIRE_FALSE(dialog->result_page()->is_self_visible());
    auto* fields = dialog->input_page()->get_item(1);
    REQUIRE(dynamic_cast<Yoga::InputTextField*>(fields->get_item(0)->get_item(1))->text() == "39.98");
}

TEST_CASE_METHOD(ImGuiFixture, "Lua context color is a bordered read-only marker on both pages", "[Lua][PluginDialog]")
{
    using namespace Slic3r::App;
    int runs = 0;
    auto* dialog = root.emplace_back<TestPluginDialog>(
        [&](const Lua::PluginMeta&, const Lua::PluginParamValueMap&) { ++runs; });
    Lua::PluginMeta meta{.id="color", .type=Lua::PluginType::ProjectPlugin,
        .context=Lua::PluginContext{"Filament: PLA, slot 1", "#1234AB"}};
    dialog->show_plugin(meta, {});
    const auto check_marker = [&](Yoga::Item* row, const ImColor& expected) {
        auto* marker = dynamic_cast<Yoga::Rectangle*>(row->get_item(0));
        REQUIRE(marker != nullptr);
        REQUIRE(dynamic_cast<Yoga::LayoutButton*>(marker) == nullptr);
        REQUIRE(marker->border_width() > 0);
        REQUIRE_THAT(marker->fill().Value.x, WithinAbs(expected.Value.x, 1e-6));
        REQUIRE_THAT(marker->fill().Value.y, WithinAbs(expected.Value.y, 1e-6));
        REQUIRE_THAT(marker->fill().Value.z, WithinAbs(expected.Value.z, 1e-6));
        auto* text = dynamic_cast<Yoga::Text*>(row->get_item(1));
        REQUIRE(text != nullptr);
        REQUIRE(text->text() == meta.context->text);
        row->set_width(400);
        row->style_node();
        row->resize(default_size_info);
        YGNodeCalculateLayout(row->node(), 400, 60, YGDirectionLTR);
        REQUIRE(text->width() > 300);
        REQUIRE(marker->width() >= 16);
    };
    check_marker(dialog->input_page()->get_item(0), ImColor(0x12, 0x34, 0xAB));
    dialog->show_result("Preview", "Details");
    check_marker(dialog->result_page()->get_item(0)->get_item(0), ImColor(0x12, 0x34, 0xAB));
    for (const auto& [hex, expected] : std::vector<std::pair<std::string, ImColor>>{
        {"#000000", ImColor(0,0,0)}, {"#FFFFFF", ImColor(255,255,255)}}) {
        meta.context->color = hex;
        dialog->show_plugin(meta, {});
        check_marker(dialog->input_page()->get_item(0), expected);
    }
    for (const auto& invalid : {"#GGGGGG", "red", "", "#123", "#12345678"}) {
        meta.context->color = invalid;
        dialog->show_plugin(meta, {});
        auto* text = dynamic_cast<Yoga::Text*>(dialog->input_page()->get_item(0));
        REQUIRE(text != nullptr);
        REQUIRE(text->text() == meta.context->text);
    }
    REQUIRE(runs == 0);
}

TEST_CASE_METHOD(
    ImGuiFixture,
    "Lua plugin numeric parameters preserve their declared types",
    "[Lua][PluginDialog]"
)
{
    using namespace Slic3r::App;

    Lua::PluginParamValueMap collected_values;
    auto* dialog = root.emplace_back<TestPluginDialog>(
        [&collected_values](const Lua::PluginMeta&, const Lua::PluginParamValueMap& values)
        { collected_values = values; }
    );

    Lua::PluginMeta meta{
        .id = "test.numeric-params",
        .type = Lua::PluginType::ProjectPlugin,
        .title = "Numeric parameters",
        .params = {
            {
                .name = "measurement",
                .label = "Measurement",
                .type = "float",
                .default_value = 40.0
            },
            {
                .name = "count",
                .label = "Count",
                .type = "int",
                .default_value = 40
            }
        }
    };

    dialog->show_plugin(meta, {});

    auto* float_row = dialog->params_content()->get_item(0);
    auto* float_input = dynamic_cast<Yoga::InputTextField*>(float_row->get_item(1));
    REQUIRE(float_input != nullptr);
    REQUIRE(dynamic_cast<Yoga::DoubleValidator*>(float_input->validator()) != nullptr);
    float_input->set_text("39.92");
    REQUIRE_THAT(std::stod(float_input->text()), WithinAbs(39.92, 1e-9));

    auto* int_row = dialog->params_content()->get_item(1);
    auto* int_input = dynamic_cast<Yoga::InputTextField*>(int_row->get_item(1));
    REQUIRE(int_input != nullptr);
    REQUIRE(dynamic_cast<Yoga::IntValidator*>(int_input->validator()) != nullptr);
    int_input->set_text("39.92");
    REQUIRE(int_input->text() == "40");

    auto* buttons_row = dialog->input_page()->get_item(1);
    auto* run_button = dynamic_cast<Yoga::LayoutButton*>(buttons_row->get_item(0));
    REQUIRE(run_button != nullptr);
    REQUIRE(run_button->callbacks().action);
    run_button->callbacks().action();

    REQUIRE_THAT(
        std::get<double>(collected_values.at("measurement")),
        WithinAbs(39.92, 1e-9)
    );
    REQUIRE(std::get<int>(collected_values.at("count")) == 40);
}

TEST_CASE_METHOD(ImGuiFixture, "Lua dialog preserves hidden inputs and shows results without rerunning", "[Lua][PluginDialog]")
{
    using namespace Slic3r::App;
    int runs = 0;
    Lua::PluginParamValueMap values;
    TestPluginDialog* dialog = nullptr;
    dialog = root.emplace_back<TestPluginDialog>(
        [&](const Lua::PluginMeta&, const Lua::PluginParamValueMap& params)
        {
            ++runs;
            values = params;
            dialog->show_result("XY shrinkage: unavailable -> 0.5000%", "Experimental estimate");
        });
    Lua::PluginMeta meta{
        .id = "test.dialog-result", .type = Lua::PluginType::ProjectPlugin,
        .params = {
            {.name = "z", .label = "Z", .type = "bool", .default_value = false},
            {.name = "z40", .label = "Z40 [mm]", .type = "string", .default_value = std::string{}, .visible_if = "z"}
        },
        .dialog_width = 760, .input_width = 360
    };
    dialog->show_plugin(meta, {{"z40", std::string{"39.98;40.00;39.99;40.01;40.00"}}});
    auto* toggle = dynamic_cast<Yoga::ToggleButton*>(dialog->params_content()->get_item(0)->get_item(0));
    auto* row = dialog->params_content()->get_item(1);
    auto* input = dynamic_cast<Yoga::InputTextField*>(row->get_item(1));
    REQUIRE(toggle != nullptr);
    REQUIRE(input != nullptr);
    REQUIRE_FALSE(row->is_self_visible());
    toggle->set_checked(true);
    REQUIRE(row->is_self_visible());
    toggle->set_checked(false);
    REQUIRE_FALSE(row->is_self_visible());
    auto* run = dynamic_cast<Yoga::LayoutButton*>(dialog->input_page()->get_item(1)->get_item(0));
    run->callbacks().action();
    REQUIRE(runs == 1);
    REQUIRE(std::get<std::string>(values.at("z40")) == input->text());
    REQUIRE(dialog->result_page()->is_self_visible());
    REQUIRE_FALSE(dialog->input_page()->is_self_visible());
    auto* result_scroll = dialog->result_page()->get_item(0);
    REQUIRE(dynamic_cast<Yoga::Text*>(result_scroll->get_item(0))->text().find("0.5000%") != std::string::npos);
    auto* details = result_scroll->get_item(2);
    REQUIRE_FALSE(details->is_self_visible());
    dynamic_cast<Yoga::ToggleButton*>(result_scroll->get_item(1))->set_checked(true);
    REQUIRE(details->is_self_visible());
    dynamic_cast<Yoga::LayoutButton*>(dialog->result_page()->get_item(1)->get_item(0))->callbacks().action();
    REQUIRE(runs == 1);
    REQUIRE(dialog->input_page()->is_self_visible());
    REQUIRE_FALSE(dialog->result_page()->is_self_visible());
    REQUIRE_FALSE(details->is_self_visible());
    REQUIRE(input->text() == "39.98;40.00;39.99;40.01;40.00");
}
