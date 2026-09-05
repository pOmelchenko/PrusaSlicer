#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Slic3r/App/Lua/PluginDialog.hpp"
#include "Slic3r/App/Yoga/ImGuiFixture.hpp"
#include "Slic3r/App/Yoga/InputTextField.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Validator.hpp"
#include "Slic3r/App/Yoga/ToggleButton.hpp"
#include "Slic3r/App/Yoga/Text.hpp"

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
