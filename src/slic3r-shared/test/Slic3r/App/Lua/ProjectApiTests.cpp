#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <boost/filesystem.hpp>

#include "Slic3r/App/Lua/ProjectApi.hpp"
#include "Slic3r/App/Platform/StdMainThreadDispatcher.hpp"
#include "Slic3r/Biz/Emboss/IFontManager.hpp"
#include "Slic3r/Biz/ISlicingInputChangedListener.hpp"
#include "Slic3r/Biz/ProjectSettingsInteractor.hpp"
#include "Slic3r/Biz/OverridableConfigBoxObservableList.hpp"
#include "Slic3r/Biz/Preset/IO/BundlePaths.hpp"
#include "Slic3r/Biz/Preset/IPresetChangedListener.hpp"
#include "Slic3r/Biz/Slicing/TestUtils.hpp"
#include "Slic3r/TestUtils/AppInstanceMessageHandlerScope.hpp"
#include "Slic3r/TestUtils/JobManagerScope.hpp"
#include "Slic3r/TestUtils/TestData.hpp"

using Catch::Matchers::WithinAbs;

namespace {

class TestFontManager : public Slic3r::Biz::Emboss::IFontManager
{
public:
    TestFontManager()
    {
        m_fonts.emplace_back();
        m_fonts.back().name = "Test font";
    }

    const Slic3r::Domain::FontList& get_fonts() override
    {
        return m_fonts;
    }

    std::unique_ptr<const Slic3r::Domain::FontFile> open(
        const Slic3r::Domain::FontDescriptor&
    ) override
    {
        auto data = std::make_unique<std::vector<unsigned char>>();
        data->push_back(0);
        return std::make_unique<const Slic3r::Domain::FontFile>(
            std::move(data),
            std::vector<Slic3r::Domain::FontFile::Info>{}
        );
    }

    Descriptor get_current_descriptor(
        const Slic3r::Domain::FontDescriptor& descriptor
    ) override
    {
        return descriptor;
    }

    Slic3r::Domain::FontDescriptor::Type get_current_type() const override
    {
        return Slic3r::Domain::FontDescriptor::Type::undefined;
    }

    Slic3r::Domain::FontList create_favorit() override
    {
        return m_fonts;
    }

private:
    Slic3r::Domain::FontList m_fonts;
};

class ConfigChangeRecorder :
    public Slic3r::Biz::Preset::IPresetChangedListener,
    public Slic3r::Biz::ISlicingInputChangedListener
{
public:
    void on_preset_value_changed(
        Slic3r::Domain::SelectionId,
        Slic3r::Domain::SelectionId,
        const Slic3r::Domain::ConfigItem& item
    ) override
    {
        changed_items.push_back(item.name());
    }

    void on_slicing_input_changed(const Slic3r::Domain::BedRef&) override
    {
        ++slicing_input_change_count;
    }

    void on_slicing_input_removed(const Slic3r::Domain::BedRef&) override {}

    std::vector<std::string> changed_items;
    size_t slicing_input_change_count{0};
};

struct LuaProjectApiFixture
{
    Slic3r::Domain::Workbench workbench;
    Slic3r::App::Platform::StdMainThreadDispatcher main_thread_dispatcher;
    Tests::AppInstanceMessageHandlerScope app_instance_message_handler_scope{
        main_thread_dispatcher
    };
    Tests::JobManagerScope job_manager_scope{main_thread_dispatcher};
    Slic3r::Test::MockThumbnailImageGenerator thumbnail_image_generator;
    Slic3r::Biz::ProjectInteractor project_interactor{
        workbench,
        main_thread_dispatcher,
        thumbnail_image_generator
    };
    TestFontManager font_manager;
    const Slic3r::Biz::Preset::IO::BundlePaths bundle_paths{
        Slic3r::Biz::Preset::IO::BundlePaths::make_test_runtime(Tests::get_datadir())
    };
    ConfigChangeRecorder config_change_recorder;

    LuaProjectApiFixture()
    {
        auto& preset_interactor = project_interactor.preset_interactor();
        preset_interactor.set_use_hw_config_short_name(false);
        preset_interactor.load_preset_bundle(bundle_paths);
        project_interactor.new_project();
        preset_interactor.add_listener<Slic3r::Biz::Preset::IPresetChangedListener>(
            &config_change_recorder
        );
        preset_interactor.add_listener<Slic3r::Biz::ISlicingInputChangedListener>(
            &config_change_recorder
        );
    }

    ~LuaProjectApiFixture()
    {
        auto& preset_interactor = project_interactor.preset_interactor();
        preset_interactor.remove_listener<Slic3r::Biz::Preset::IPresetChangedListener>(
            &config_change_recorder
        );
        preset_interactor.remove_listener<Slic3r::Biz::ISlicingInputChangedListener>(
            &config_change_recorder
        );
        main_thread_dispatcher.close();
        boost::filesystem::remove_all(bundle_paths.user_bundle_path);
    }
};

} // namespace

TEST_CASE_METHOD(LuaProjectApiFixture, "Lua material identity follows the selected preset and returns a copy", "[Lua][ProjectApi]")
{
    using namespace Slic3r;
    App::Lua::ProjectApi project_api(project_interactor, font_manager);
    Biz::Lua::LuaEngine lua;
    lua.open_registry([&project_api](auto& engine) { project_api.register_api(engine); });
    const auto initial = project_interactor.selected_config_container().selected_preset().metadata();
    REQUIRE(lua.run_script(R"lua(
        bed = api.project:current_bed()
        original = bed:material_preset_info(0)
        original_id, original_name = original.id, original.name
        original.name = "changed in Lua only"
        invalid_slot_ok = pcall(function() bed:material_preset_info(9999) end)
    )lua").valid());
    REQUIRE(lua.state()["original_id"].get<std::string>() == initial.materials.at(0).id);
    REQUIRE(lua.state()["original_name"].get<std::string>() == initial.materials.at(0).name);
    REQUIRE_FALSE(lua.state()["invalid_slot_ok"].get<bool>());
    REQUIRE(project_interactor.selected_config_container().selected_preset().materials.at(0).name == initial.materials.at(0).name);
    REQUIRE(config_change_recorder.changed_items.empty());
    REQUIRE(config_change_recorder.slicing_input_change_count == 0);

    // The minimal fixture has one preset. Supply a second material identity
    // and change the selected slot to check lookup freshness and slot indexing.
    const std::string next_id = "test.other-material";
    const std::string next_name = "Custom PETG";
    auto& selected = project_interactor.selected_config_container().mutable_selected_preset();
    auto other = selected.materials.at(0);
    other.id = next_id;
    other.name = next_name;
    selected.materials.push_back(other);
    REQUIRE(lua.run_script("second = bed:material_preset_info(1)").valid());
    REQUIRE(lua.state()["second"]["name"].get<std::string>() == next_name);
    selected.materials.at(0) = other;
    REQUIRE(lua.run_script("current = bed:material_preset_info(0)").valid());
    REQUIRE(lua.state()["current"]["id"].get<std::string>() == next_id);
    REQUIRE(lua.state()["current"]["name"].get<std::string>() == next_name);
    REQUIRE(lua.state()["original"]["id"].get<std::string>() == initial.materials.at(0).id);
}

TEST_CASE_METHOD(LuaProjectApiFixture, "Lua material color matches project slot overrides without changing the preset", "[Lua][ProjectApi]")
{
    using namespace Slic3r;
    App::Lua::ProjectApi project_api(project_interactor, font_manager);
    Biz::Lua::LuaEngine lua;
    lua.open_registry([&project_api](auto& engine) { project_api.register_api(engine); });
    REQUIRE(lua.run_script(R"lua(
        bed = api.project:current_bed()
        original_preset_color = bed:material_presets(0):value("filament_colour")
        original = bed:material_preset_info(0)
    )lua").valid());
    auto& settings = project_interactor.project_settings_interactor();
    const auto container_id = project_interactor.selected_config_container_id();
    for (const auto& color : {"#1234AB", "#000000", "#FFFFFF"}) {
        settings.set_color_from_user(container_id, 0, color);
        REQUIRE(lua.run_script(R"lua(
            current = bed:material_preset_info(0)
            preset_color = bed:material_presets(0):value("filament_colour")
        )lua").valid());
        REQUIRE(lua.state()["current"]["color"].get<std::string>() == color);
        REQUIRE(lua.state()["preset_color"].get<std::string>() == lua.state()["original_preset_color"].get<std::string>());
        REQUIRE(lua.state()["current"]["id"].get<std::string>() == lua.state()["original"]["id"].get<std::string>());
    }
    REQUIRE(lua.run_script(R"lua(
        current.color = "#000000"
        assert(bed:material_preset_info(0).color == "#FFFFFF")
    )lua").valid());
    REQUIRE(config_change_recorder.changed_items.empty());
}

TEST_CASE_METHOD(
    LuaProjectApiFixture,
    "Lua preset setters apply values and notify project listeners",
    "[Lua][ProjectApi]"
)
{
    Slic3r::App::Lua::ProjectApi project_api(project_interactor, font_manager);
    Slic3r::Biz::Lua::LuaEngine lua;
    lua.open_registry([&project_api](auto& engine) { project_api.register_api(engine); });

    const auto result = lua.run_script(R"lua(
        local bed = api.project:current_bed()
        local material = bed:material_presets(0)
        local print = bed:print_presets()
        material:value("filament_shrinkage_compensation_xy")
        print:value("xy_size_compensation")
        material:set("filament_shrinkage_compensation_xy", "1.250000%")
        print:set("xy_size_compensation", -0.08)
    )lua");
    if (!result.valid()) {
        const sol::error error = result;
        INFO(error.what());
    }
    REQUIRE(result.valid());

    const auto& selected_preset =
        project_interactor.selected_config_container().selected_preset();

    const auto shrinkage = selected_preset.materials.at(0).config_box().find(
        "filament_shrinkage_compensation_xy"
    );
    REQUIRE(shrinkage.item != nullptr);
    const auto shrinkage_value = shrinkage.item->get<Slic3r::Domain::Percentage>();
    REQUIRE_THAT(shrinkage_value.value, WithinAbs(1.25, 1e-9));

    const auto xy_compensation = selected_preset.print.config_box().find(
        "xy_size_compensation"
    );
    REQUIRE(xy_compensation.item != nullptr);
    REQUIRE_THAT(xy_compensation.item->get<double>(), WithinAbs(-0.08, 1e-9));

    REQUIRE(config_change_recorder.changed_items == std::vector<std::string>{
        "filament_shrinkage_compensation_xy",
        "xy_size_compensation"
    });
    REQUIRE(config_change_recorder.slicing_input_change_count == 2);
}

TEST_CASE_METHOD(
    LuaProjectApiFixture,
    "Lua preset setters enable material overrides with unchanged stored values",
    "[Lua][ProjectApi]"
)
{
    const auto& initial_material_config =
        project_interactor.selected_config_container()
            .selected_preset()
            .materials.at(0)
            .config_box();
    REQUIRE_FALSE(initial_material_config.overrides.get("retract_length").has_value());

    const auto* stored_override = initial_material_config.overrides.find("retract_length");
    REQUIRE(stored_override != nullptr);
    REQUIRE_THAT(stored_override->get<double>(), WithinAbs(2.0, 1e-9));

    const auto observable_material_config =
        project_interactor.preset_interactor()
            .material_cbi_list()
            .at(0)
            .config_box_overridable_list()
            .lock();
    REQUIRE(observable_material_config != nullptr);
    const auto [initial_observable_value, initial_observable_override_enabled] =
        observable_material_config->find("retract_length");
    REQUIRE(initial_observable_value != nullptr);
    REQUIRE(initial_observable_override_enabled.has_value());
    REQUIRE_FALSE(*initial_observable_override_enabled);

    Slic3r::App::Lua::ProjectApi project_api(project_interactor, font_manager);
    Slic3r::Biz::Lua::LuaEngine lua;
    lua.open_registry([&project_api](auto& engine) { project_api.register_api(engine); });

    const auto result = lua.run_script(R"lua(
        local material = api.project:current_bed():material_presets(0)
        material:set("retract_length", 2.0)
    )lua");
    if (!result.valid()) {
        const sol::error error = result;
        INFO(error.what());
    }
    REQUIRE(result.valid());

    const auto& material_config =
        project_interactor.selected_config_container()
            .selected_preset()
            .materials.at(0)
            .config_box();
    const auto retract_length = material_config.overrides.get("retract_length");
    REQUIRE(retract_length.has_value());
    REQUIRE_THAT(retract_length->get<double>(), WithinAbs(2.0, 1e-9));

    const auto [observable_value, observable_override_enabled] =
        observable_material_config->find("retract_length");
    REQUIRE(observable_value != nullptr);
    REQUIRE(observable_override_enabled.has_value());
    REQUIRE(*observable_override_enabled);

    REQUIRE(config_change_recorder.changed_items == std::vector<std::string>{"retract_length"});
    REQUIRE(config_change_recorder.slicing_input_change_count == 1);
}
