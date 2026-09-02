#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Slic3r/App/Lua/PluginDialog.hpp"
#include "Slic3r/App/Yoga/ImGuiFixture.hpp"
#include "Slic3r/App/Yoga/InputTextField.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Validator.hpp"

using Catch::Matchers::WithinAbs;

namespace {

class TestPluginDialog : public Slic3r::App::Lua::PluginDialog
{
public:
    using PluginDialog::PluginDialog;

    Slic3r::App::Yoga::Item* params_content() const
    {
        return content();
    }
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

    auto* buttons_row = dialog->params_content()->get_item(2);
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
