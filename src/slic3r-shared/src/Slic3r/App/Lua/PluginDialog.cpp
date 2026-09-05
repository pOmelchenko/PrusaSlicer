#include "Slic3r/App/Lua/PluginDialog.hpp"

#include "Slic3r/App/Imgui/DoubleSlider.hpp"
#include "Slic3r/App/Yoga/InputTextField.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/ToggleButton.hpp"
#include "Slic3r/App/Yoga/Validator.hpp"
#include "Slic3r/App/Yoga/ScrollArea.hpp"
#include "Slic3r/App/Yoga/Rectangle.hpp"
#include "Slic3r/Biz/Algorithms/Color.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include <algorithm>
#include <utility>

namespace Slic3r::App::Lua {

namespace {

// Reuse the settings widgets' tooltip, including their native hover behavior.
template <typename Widget>
class ParamWidget : public Widget
{
public:
    template <typename... Args>
    ParamWidget(const PluginParamDef& param, Args&&... args) :
        Widget(std::forward<Args>(args)...)
    {
        if (!param.tooltip || param.tooltip->empty()) return;
        this->set_tooltip(*param.tooltip);
        this->m_tooltip->set_text_wrap(true);
        this->m_tooltip->content_item()->set_width(350);
    }
};

class StringControl : public Details::IParamControl
{
public:
    StringControl(const PluginParamDef& param_def, std::optional<PluginParamValue> init_value) :
        m_param_def(param_def), m_init_value(std::move(init_value))
    {}

    PluginParamValue value() const override
    {
        return m_textfield->text();
    }

    Yoga::Item& emplace_control(Yoga::Item& parent) override
    {
        std::string val;
        if (m_init_value.has_value()) {
            val = std::get<std::string>(m_init_value.value());
        } else {
            val = std::visit(
                []<typename T>(const T& val) -> std::string
                {
                    if constexpr (std::is_same_v<T, std::string>) {
                        return val;
                    }
                    return "";
                },
                m_param_def.default_value.value_or("")
            );
        }
        m_textfield = parent.emplace_back<ParamWidget<Yoga::InputTextField>>(m_param_def);
        m_textfield->set_text(val);
        return *m_textfield;
    }
private:
    const PluginParamDef& m_param_def;
    std::optional<PluginParamValue> m_init_value;
    Yoga::InputTextField* m_textfield{nullptr};
};

class BoolControl : public Details::IParamControl
{
public:
    BoolControl(const PluginParamDef& param_def, std::optional<PluginParamValue> init_value) :
        m_param_def(param_def),
        m_init_value(std::move(init_value))
    {}

    PluginParamValue value() const override
    {
        return m_toggle_button->checked();
    }

    Yoga::Item& emplace_control(Yoga::Item& parent) override
    {
        m_toggle_button = parent.emplace_back<ParamWidget<Yoga::ToggleButton>>(
            m_param_def);
        bool val{false};
        if (m_init_value.has_value()) {
            val = std::get<bool>(m_init_value.value());
        } else {
            val = std::visit(
                []<typename T>(const T& val) -> bool
                {
                    if constexpr (std::is_same_v<T, bool>) {
                        return val;
                    }
                    return false;
                },
                m_param_def.default_value.value_or(false)
            );
        }
        m_toggle_button->set_checked(val);
        return *m_toggle_button;
    }
private:
    const PluginParamDef& m_param_def;
    std::optional<PluginParamValue> m_init_value;
    Yoga::ToggleButton* m_toggle_button{nullptr};
};

template <typename ValidatorType, typename NumType>
class NumberControl : public Details::IParamControl
{
public:
    NumberControl(
        const PluginParamDef& param_def,
        std::optional<PluginParamValue> init_value
    ) :
        m_param_def(param_def),
        m_init_value(std::move(init_value))
    {}

    PluginParamValue value() const override
    {
        return static_cast<ValidatorType*>(m_textfield->validator())->value();
    }

    Yoga::Item& emplace_control(Yoga::Item& parent) override
    {
        std::string s_val;
        if (m_init_value.has_value()) {
            s_val = std::to_string(std::get<NumType>(m_init_value.value()));
        } else {
            s_val = std::visit(
                []<typename T>(const T& val) -> std::string
                {
                    if constexpr (std::is_same_v<T, std::string>) {
                        return val;
                    }
                    if constexpr (std::is_integral_v<T> || std::is_floating_point_v<T>) {
                        return std::to_string(val);
                    }
                    return "0";
                },
                m_param_def.default_value.value_or("0")
            );
        }
        m_textfield = parent.emplace_back<ParamWidget<Yoga::InputTextField>>(m_param_def);
        auto validator = std::make_unique<ValidatorType>();

        m_textfield->set_validator(std::move(validator));
        m_textfield->set_text(s_val);
        return *m_textfield;
    }
private:
    const PluginParamDef& m_param_def;
    std::optional<PluginParamValue> m_init_value;
    Yoga::InputTextField* m_textfield{nullptr};
};

}

PluginDialog::PluginDialog(ProcessFunction process_function)
    : m_process_function(std::move(process_function))
{
    append_tab("");

    this->set_closable(true);
    content_item()->set_modal(true);
    content()->set_gap(5);
}

void
PluginDialog::show_plugin(const PluginMeta& plugin_meta, const PluginParamValueMap& param_values)
{
    using namespace Yoga;

    m_meta = plugin_meta;

    m_param_controls.clear();
    m_param_rows.clear();
    m_result_shown = false;
    while (!content()->items().empty()) {
        content()->remove(content()->get_item(0));
    }

    remove_tab(0);
    append_tab(plugin_meta.title.value_or(plugin_meta.id));

    content()->set_orientation(Orientation::Vertical);
    const int dialog_width = std::clamp(plugin_meta.dialog_width.value_or(400), 400, 1000);
    content()->set_width(dialog_width);
    content()->set_min_width(dialog_width);
    m_input_page = content()->emplace_back<Item>();
    m_input_page->set_orientation(Orientation::Vertical);
    m_input_page->set_gap(8);
    emplace_context(*m_input_page);
    if (plugin_meta.description) {
        auto* description = m_input_page->emplace_back<Text>(*plugin_meta.description);
        description->set_wrap_mode(Text::WrapMode::Wrap);
    }
    m_fields = m_input_page->emplace_back<ScrollArea>();
    m_fields->set_orientation(Orientation::Vertical);
    m_fields->set_gap(5);
    m_fields->set_max_height(460);

    const float row_padding = 5.f;
    const float row_gap = 10.f;
    const Paddings button_padding(15.f, 5.f);

    for (const auto& param : plugin_meta.params) {
        auto init_it = param_values.find(param.name);
        auto init_value =
            init_it == param_values.end() ? std::nullopt : std::make_optional(init_it->second);
        if (param.type == "string") {
            emplace_string_param(param, init_value);
        } else if (param.type == "float") {
            emplace_float_param(param, init_value);
        } else if (param.type == "int") {
            emplace_int_param(param, init_value);
        } else if (param.type == "bool") {
            emplace_bool_param(param, init_value);
        } else {
            PANIC("Unsupported param type");
        }
    }
    update_visibility();
    Item* buttons_row = m_input_page->emplace_back<Item>();
    buttons_row->set_orientation(Orientation::Horizontal);
    buttons_row->set_flex_grow(1.f);
    buttons_row->set_padding(row_padding);
    buttons_row->set_gap(row_gap);
    buttons_row->set_justify_content(YGJustifyCenter);

    constexpr int button_height = 40;

    LayoutButton* ok_btn = buttons_row->emplace_back<LayoutButton>(Biz::_u8L("Run"));
    ok_btn->set_content_padding(button_padding);
    ok_btn->callbacks().action = [this]()
    {
        m_result_shown = false;
        if (m_process_function) {
            PluginParamValueMap param_values;
            collect_values(param_values);
            m_process_function(m_meta.value(), param_values);
        }
        if (!m_result_shown) close_action();
    };
    ok_btn->set_flex_grow(2);
    ok_btn->set_background_color(Platform::Color::AccentPrimary);
    ok_btn->set_label_font_type(Render::ImguiFontType::Bold);
    ok_btn->set_min_height(button_height);


    LayoutButton* cancel_btn = buttons_row->emplace_back<LayoutButton>(Biz::_u8L("Cancel"));
    cancel_btn->set_content_padding(button_padding);
    cancel_btn->callbacks().action = [this]()
    { close_action(); };
    cancel_btn->set_flex_grow(1);
    cancel_btn->set_min_height(button_height);

    // Keep both pages alive: switching from inside Run must not destroy its button.
    m_result_page = content()->emplace_back<Item>();
    m_result_page->set_orientation(Orientation::Vertical);
    m_result_page->set_gap(10);
    m_result_page->set_visible(false);
    auto* result_scroll = m_result_page->emplace_back<ScrollArea>();
    result_scroll->set_orientation(Orientation::Vertical);
    result_scroll->set_gap(10);
    result_scroll->set_max_height(460);
    emplace_context(*result_scroll);
    m_summary = result_scroll->emplace_back<Text>("");
    m_summary->set_wrap_mode(Text::WrapMode::Wrap);
    auto& details_row = emplace_row(*result_scroll, Biz::_u8L("Show details"));
    auto* details_toggle = details_row.emplace_back<ToggleButton>();
    style_control(*details_toggle);
    m_details = result_scroll->emplace_back<Text>("");
    m_details->set_wrap_mode(Text::WrapMode::Wrap);
    m_details->set_visible(false);
    details_toggle->callbacks().checked_changed = [this](bool checked)
    { m_details->set_visible(checked); };
    auto* result_buttons = m_result_page->emplace_back<Item>();
    result_buttons->set_orientation(Orientation::Horizontal);
    result_buttons->set_gap(row_gap);
    auto* back = result_buttons->emplace_back<LayoutButton>(Biz::_u8L("Back"));
    back->set_content_padding(button_padding);
    back->set_min_height(button_height);
    back->set_flex_grow(1);
    back->callbacks().action = [this, details_toggle]()
    {
        details_toggle->set_checked(false);
        m_details->set_visible(false);
        m_result_page->set_visible(false);
        m_input_page->set_visible(true);
    };
    auto* close = result_buttons->emplace_back<LayoutButton>(Biz::_u8L("Close"));
    close->set_content_padding(button_padding);
    close->set_min_height(button_height);
    close->set_flex_grow(1);
    close->callbacks().action = [this]() { close_action(); };

    open();
}

void PluginDialog::emplace_context(Yoga::Item& parent)
{
    using namespace Yoga;
    if (!m_meta->context || m_meta->context->text.empty()) return;
    const auto& context = *m_meta->context;
    Item* label_parent = &parent;
    Domain::ColorRGB color;
    if (context.color && context.color->size() == 7 && context.color->front() == '#' &&
        std::all_of(context.color->begin() + 1, context.color->end(), [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        }) &&
        Biz::Algorithms::Color::decode_color(*context.color, color)) {
        auto* row = parent.emplace_back<Item>();
        row->set_orientation(Orientation::Horizontal);
        row->set_gap(8);
        auto* marker = row->emplace_back<Rectangle>();
        marker->set_object_name("PluginContextColor");
        marker->set_width(16);
        marker->set_height(16);
        marker->set_min_width(16);
        marker->set_min_height(16);
        marker->set_flex_shrink(0);
        marker->set_self_align(YGAlignCenter);
        marker->set_rounding(8);
        marker->set_border_width(1);
        marker->set_border_color(ImColor(160, 160, 160));
        marker->set_fill(ImColor(color.r(), color.g(), color.b()));
        label_parent = row;
    }
    auto* text = label_parent->emplace_back<Text>(context.text);
    if (label_parent != &parent) text->set_flex_grow(1.f);
    text->set_wrap_mode(Text::WrapMode::Wrap);
}

void PluginDialog::emplace_string_param(
    const PluginParamDef& param,
    std::optional<PluginParamValue> default_value
)
{
    auto& row = emplace_row(*m_fields, param.label);
    auto [it, _] =
        m_param_controls.emplace(param.name, std::make_unique<StringControl>(param, default_value));
    style_control(it->second->emplace_control(row));
    m_param_rows[param.name] = &row;
}

void PluginDialog::emplace_float_param(
    const PluginParamDef& param,
    std::optional<PluginParamValue> default_value
)
{
    auto& row     = emplace_row(*m_fields, param.label);
    using Control = NumberControl<Yoga::DoubleValidator, double>;
    auto [it, _]  = m_param_controls.emplace(
        param.name,
        std::make_unique<Control>(param, std::move(default_value))
    );
    style_control(it->second->emplace_control(row));
    m_param_rows[param.name] = &row;
}

void PluginDialog::emplace_int_param(
    const PluginParamDef& param,
    std::optional<PluginParamValue> default_value
)
{
    auto& row     = emplace_row(*m_fields, param.label);
    using Control = NumberControl<Yoga::IntValidator, int>;
    auto [it, _]  = m_param_controls.emplace(
        param.name,
        std::make_unique<Control>(param, std::move(default_value))
    );
    style_control(it->second->emplace_control(row));
    m_param_rows[param.name] = &row;
}

void PluginDialog::emplace_bool_param(
    const PluginParamDef& param,
    std::optional<PluginParamValue> default_value
)
{
    auto& row = emplace_row(*m_fields, param.label);
    auto [it, _] = m_param_controls.emplace(
        param.name,
        std::make_unique<BoolControl>(param, std::move(default_value))
    );
    auto& control = static_cast<Yoga::ToggleButton&>(it->second->emplace_control(row));
    style_control(control);
    control.callbacks().checked_changed = [this](bool) { update_visibility(); };
    m_param_rows[param.name] = &row;
}

Yoga::Item& PluginDialog::emplace_row(Yoga::Item& parent, const std::string& label)
{
    auto* row = parent.emplace_back<Yoga::Item>();
    row->set_orientation(Yoga::Orientation::Horizontal);
    row->set_flex_grow(1.f);
    row->set_flex_shrink(0.f);
    row->set_justify_content(YGJustifySpaceBetween);
    row->set_align_items(YGAlignCenter);
    row->set_gap(12);

    // The same label column aligns toggles with text/numeric inputs and results.
    const int width = std::clamp(m_meta->dialog_width.value_or(400), 400, 1000);
    const int input_width = std::clamp(m_meta->input_width.value_or(width / 2),
                                      100, std::min(600, width - 120));
    auto* text = row->emplace_back<Yoga::Text>(label);
    text->set_width(width - input_width - 32);
    text->set_flex_shrink(0);
    text->set_wrap_mode(Yoga::Text::WrapMode::Wrap);

    return *row;
}

void PluginDialog::style_control(Yoga::Item& ctrl)
{
    // Fill the remaining column, including the space available beside a scrollbar.
    ctrl.set_flex_grow(1);
    ctrl.set_flex_shrink(1);
    ctrl.set_min_height(28);
}

void PluginDialog::collect_values(PluginParamValueMap& param_values) const
{
    for (const auto& [k, v] : m_param_controls) {
        param_values[k] = v->value();
    }
}

void PluginDialog::update_visibility()
{
    for (const auto& param : m_meta->params) {
        if (!param.visible_if) continue;
        auto row = m_param_rows.find(param.name);
        auto dependency = m_param_controls.find(*param.visible_if);
        if (row == m_param_rows.end()) continue;
        // An invalid dependency must not silently hide a required field.
        bool visible = true;
        if (dependency != m_param_controls.end()) {
            const auto value = dependency->second->value();
            if (const auto* checked = std::get_if<bool>(&value)) visible = *checked;
        }
        row->second->set_visible(visible);
    }
}

void PluginDialog::show_result(const std::string& summary, const std::string& details)
{
    m_summary->set_text(summary);
    m_details->set_text(details);
    m_result_shown = true;
    m_input_page->set_visible(false);
    m_result_page->set_visible(true);
}
}
