#include "QuickSettingsBar.hpp"

#include <algorithm>
#include <cmath>

#include <wx/sizer.h>

#include "GUI_App.hpp"
#include "I18N.hpp"
#include "PartPlate.hpp"
#include "Plater.hpp"
#include "Tab.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/SpinInput.hpp"

#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"

namespace Slic3r { namespace GUI {

// The tab bar is painted dark in both application themes, so the controls hosted on it use the
// same fixed palette as the tab buttons instead of the theme aware one.
static const wxColour QSB_TEXT               = wxColour(0xFE, 0xFE, 0xFE);
static const wxColour QSB_TEXT_DISABLED      = wxColour(0x8A, 0x8A, 0x8A);
static const wxColour QSB_CTRL_BG            = wxColour(0x3B, 0x44, 0x46);
static const wxColour QSB_CTRL_BG_DISABLED   = wxColour(0x33, 0x3A, 0x3C);
static const wxColour QSB_BORDER             = wxColour(0x57, 0x62, 0x64);
static const wxColour QSB_BORDER_HOVERED     = wxColour(0x00, 0x96, 0x88);

static DynamicPrintConfig* tab_config(Preset::Type type)
{
    Tab* tab = wxGetApp().get_tab(type);
    return tab == nullptr ? nullptr : tab->get_config();
}

// Publishes values that were just written into a tab's config: marks the preset dirty, refreshes
// the tab's own fields and runs the tab's value change logic, which is what invalidates the
// already sliced plates. Same sequence a field edited inside the tab goes through.
static void commit_tab_change(Preset::Type type, const std::vector<std::string>& opt_keys, const boost::any& value)
{
    Tab* tab = wxGetApp().get_tab(type);
    if (tab == nullptr)
        return;
    tab->update_dirty();
    tab->reload_config();
    for (const std::string& opt_key : opt_keys)
        tab->on_value_change(opt_key, value);
}

static void style_input(StaticBox* input)
{
    input->SetBackgroundColor(StateColor(std::make_pair(QSB_CTRL_BG_DISABLED, (int) StateColor::Disabled),
                                         std::make_pair(QSB_CTRL_BG, (int) StateColor::Normal)));
    input->SetBorderColor(StateColor(std::make_pair(QSB_BORDER, (int) StateColor::Disabled),
                                     std::make_pair(QSB_BORDER_HOVERED, (int) StateColor::Hovered),
                                     std::make_pair(QSB_BORDER, (int) StateColor::Normal)));
}

QuickSettingsBar::QuickSettingsBar(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
{
    SetBackgroundColour(parent->GetBackgroundColour());

    const wxSize combo_size = wxSize(FromDIP(136), FromDIP(26));
    const wxSize spin_size  = wxSize(FromDIP(86), FromDIP(26));

    const StateColor text_color(std::make_pair(QSB_TEXT_DISABLED, (int) StateColor::Disabled),
                                std::make_pair(QSB_TEXT, (int) StateColor::Normal));

    auto add_label = [this](const wxString& text) {
        auto* label = new Label(this, text);
        label->SetFont(Label::Body_13);
        label->SetBackgroundColour(GetBackgroundColour());
        label->SetForegroundColour(QSB_TEXT);
        return label;
    };

    m_wall_sequence_label = add_label(_L("Walls"));
    m_wall_sequence = new ComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, combo_size, 0, nullptr, wxCB_READONLY);
    if (const ConfigOptionDef* def = print_config_def.get("wall_sequence"); def != nullptr) {
        for (const std::string& enum_label : def->enum_labels)
            m_wall_sequence->Append(_L(enum_label));
        m_wall_sequence->SetToolTip(_L(def->label));
        m_wall_sequence_label->SetToolTip(_L(def->label));
    }
    style_input(m_wall_sequence);
    m_wall_sequence->SetLabelColor(text_color);
    m_wall_sequence->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) { on_wall_sequence(); });

    m_bed_temp_label = add_label(_L("Bed"));
    m_bed_temp = new SpinInput(this, wxEmptyString, _L(u8"℃" /* °C */), wxDefaultPosition, spin_size, wxTE_PROCESS_ENTER, 0, 300, 0);
    // One control for both keys: the first layer and the other layers temperature are set together.
    const wxString bed_tooltip = _L("Bed temperature") + " (" + _L("First layer") + " + " + _L("Other layers") + ")";
    m_bed_temp->SetToolTip(bed_tooltip);
    m_bed_temp_label->SetToolTip(bed_tooltip);
    m_bed_temp->Bind(wxEVT_SPINCTRL, [this](wxCommandEvent&) { on_bed_temperature(); });

    m_infill_density_label = add_label(_L("Infill"));
    m_infill_density = new SpinInput(this, wxEmptyString, "%", wxDefaultPosition, spin_size, wxTE_PROCESS_ENTER, 0, 100, 0, 5);
    if (const ConfigOptionDef* def = print_config_def.get("sparse_infill_density"); def != nullptr) {
        m_infill_density->SetToolTip(_L(def->label));
        m_infill_density_label->SetToolTip(_L(def->label));
    }
    m_infill_density->Bind(wxEVT_SPINCTRL, [this](wxCommandEvent&) { on_infill_density(); });

    for (SpinInput* spin : {m_bed_temp, m_infill_density}) {
        style_input(spin);
        spin->SetLabelColor(text_color);
        spin->SetTextColor(text_color);
        // The inner text control only picks up the colours on construction and on Enable(), so
        // repaint it by hand after restyling.
        spin->GetTextCtrl()->SetBackgroundColour(QSB_CTRL_BG);
        spin->GetTextCtrl()->SetForegroundColour(QSB_TEXT);
    }

    auto* sizer = new wxBoxSizer(wxHORIZONTAL);
    const std::pair<wxWindow*, wxWindow*> entries[] = {
        {m_wall_sequence_label, m_wall_sequence},
        {m_bed_temp_label, m_bed_temp},
        {m_infill_density_label, m_infill_density},
    };
    for (const auto& entry : entries) {
        sizer->Add(entry.first, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(16));
        sizer->Add(entry.second, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(6));
    }
    SetSizerAndFit(sizer);

    update();
}

BedType QuickSettingsBar::current_bed_type() const
{
    if (Plater* plater = wxGetApp().plater(); plater != nullptr) {
        if (PartPlate* plate = plater->get_partplate_list().get_curr_plate(); plate != nullptr)
            return plate->get_bed_type(true);
    }
    if (PresetBundle* bundle = wxGetApp().preset_bundle;
        bundle != nullptr && bundle->project_config.has("curr_bed_type"))
        return bundle->project_config.opt_enum<BedType>("curr_bed_type");
    return btDefault;
}

void QuickSettingsBar::update()
{
    if (m_updating)
        return;
    m_updating = true;

    const DynamicPrintConfig* print_cfg = tab_config(Preset::TYPE_PRINT);

    const bool has_wall_sequence = print_cfg != nullptr && print_cfg->has("wall_sequence");
    m_wall_sequence->Enable(has_wall_sequence);
    if (has_wall_sequence) {
        const int sel = (int) print_cfg->opt_enum<WallSequence>("wall_sequence");
        if (sel != m_wall_sequence->GetSelection())
            m_wall_sequence->SetSelection(sel);
    }

    const bool has_infill = print_cfg != nullptr && print_cfg->has("sparse_infill_density");
    m_infill_density->Enable(has_infill);
    if (has_infill && !m_infill_density->GetTextCtrl()->HasFocus()) {
        const int value = (int) std::lround(print_cfg->option<ConfigOptionPercent>("sparse_infill_density")->value);
        if (value != m_infill_density->GetValue())
            m_infill_density->SetValue(value);
    }

    // The bed temperature lives in the filament preset under a key that depends on the plate type
    // in use, so it can only be edited once that plate type is known.
    const BedType             bed_type         = current_bed_type();
    const DynamicPrintConfig* filament_cfg     = tab_config(Preset::TYPE_FILAMENT);
    const std::string         first_layer_key  = bed_type == btDefault ? std::string() : get_bed_temp_1st_layer_key(bed_type);
    const std::string         other_layers_key = bed_type == btDefault ? std::string() : get_bed_temp_key(bed_type);

    const ConfigOptionInts* bed_temp_opt = filament_cfg == nullptr || first_layer_key.empty() ?
                                               nullptr :
                                               filament_cfg->option<ConfigOptionInts>(first_layer_key);
    const bool has_bed_temp = bed_temp_opt != nullptr && !bed_temp_opt->values.empty();
    m_bed_temp->Enable(has_bed_temp);
    if (has_bed_temp) {
        // Stay within the range both keys accept, they don't always share the same limits.
        // Unconstrained options carry +-FLT_MAX, hence the guards before narrowing to int.
        int min = 0;
        int max = 1000;
        for (const std::string& key : {first_layer_key, other_layers_key}) {
            if (const ConfigOptionDef* def = print_config_def.get(key); def != nullptr) {
                if (def->min > (float) min)
                    min = (int) def->min;
                if (def->max < (float) max)
                    max = (int) def->max;
            }
        }
        m_bed_temp->SetRange(min, max);
        if (!m_bed_temp->GetTextCtrl()->HasFocus() && bed_temp_opt->values.front() != m_bed_temp->GetValue())
            m_bed_temp->SetValue(bed_temp_opt->values.front());
    }

    m_updating = false;
}

void QuickSettingsBar::on_wall_sequence()
{
    const int sel = m_wall_sequence->GetSelection();
    if (sel < 0 || sel >= (int) WallSequence::Count)
        return;

    DynamicPrintConfig* cfg = tab_config(Preset::TYPE_PRINT);
    if (cfg == nullptr || !cfg->has("wall_sequence") || (int) cfg->opt_enum<WallSequence>("wall_sequence") == sel)
        return;

    cfg->set_key_value("wall_sequence", new ConfigOptionEnum<WallSequence>((WallSequence) sel));
    commit_tab_change(Preset::TYPE_PRINT, {"wall_sequence"}, sel);
}

void QuickSettingsBar::on_infill_density()
{
    DynamicPrintConfig* cfg = tab_config(Preset::TYPE_PRINT);
    if (cfg == nullptr)
        return;

    auto*     opt   = cfg->option<ConfigOptionPercent>("sparse_infill_density");
    const int value = m_infill_density->GetValue();
    if (opt == nullptr || (int) std::lround(opt->value) == value)
        return;

    opt->value = value;
    commit_tab_change(Preset::TYPE_PRINT, {"sparse_infill_density"}, (double) value);
}

void QuickSettingsBar::on_bed_temperature()
{
    const BedType bed_type = current_bed_type();
    if (bed_type == btDefault)
        return;

    DynamicPrintConfig* cfg = tab_config(Preset::TYPE_FILAMENT);
    if (cfg == nullptr)
        return;

    const int                value = m_bed_temp->GetValue();
    std::vector<std::string> changed_keys;
    for (const std::string& key : {get_bed_temp_1st_layer_key(bed_type), get_bed_temp_key(bed_type)}) {
        auto* opt = key.empty() ? nullptr : cfg->option<ConfigOptionInts>(key);
        if (opt == nullptr || opt->values.empty() || opt->values.front() == value)
            continue;
        opt->values.front() = value;
        changed_keys.push_back(key);
    }
    if (changed_keys.empty())
        return;

    commit_tab_change(Preset::TYPE_FILAMENT, changed_keys, value);
}

void QuickSettingsBar::msw_rescale()
{
    m_wall_sequence->Rescale();
    m_bed_temp->Rescale();
    m_infill_density->Rescale();
    GetSizer()->Layout();
    Fit();
}

}} // namespace Slic3r::GUI
