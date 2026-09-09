#include "ActionRegistry.hpp"

#include "calib_dlg.hpp"
#include "GCodeViewer.hpp"
#include "GLCanvas3D.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "IMSlider.hpp"
#include "MainFrame.hpp"
#include "Notebook.hpp"
#include "Plater.hpp"
#include "PlateSettingsDialog.hpp"
#include "Search.hpp"
#include "Tab.hpp"
#include "slic3r/plugin/PluginManager.hpp"

#include <libslic3r/AppConfig.hpp>
#include <libslic3r/Config.hpp>
#include <libslic3r/PresetBundle.hpp>
#include <libslic3r/Utils.hpp>
#include <slic3r/plugin/PythonPluginInterface.hpp>

#include <wx/thread.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/any.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/filesystem.hpp>
#include <boost/nowide/convert.hpp>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Slic3r { namespace GUI {

namespace {

constexpr const char* kConfigSection = "speed_dial";

nlohmann::json parse_config_json(const std::string& value, nlohmann::json fallback)
{
    nlohmann::json parsed = nlohmann::json::parse(value, nullptr, false);
    return parsed.is_discarded() ? std::move(fallback) : parsed;
}

nlohmann::json read_section(const char* key, nlohmann::json fallback)
{ return parse_config_json(wxGetApp().app_config->get(kConfigSection, key), std::move(fallback)); }

void write_section(const char* key, const nlohmann::json& j) { wxGetApp().app_config->set(kConfigSection, key, j.dump()); }

std::vector<std::string> read_string_array(const char* key)
{
    auto j = read_section(key, nlohmann::json::array());
    std::vector<std::string> v;
    for (auto& e : j)
        if (e.is_string())
            v.push_back(e.get<std::string>());
    return v;
}

// frecency = frequency + recency; score halves every 30 idle days.
double frecency_score(int count, long long last, long long now)
{
    if (count <= 0)
        return 0.0;
    constexpr double HALF_LIFE_DAYS = 30.0;
    double age                      = std::max(0.0, double(now - last) / 86400.0);
    return count * std::pow(2.0, -age / HALF_LIFE_DAYS);
}

// ---- script-plugin action source (the one and only source) ------------------

std::string find_loaded_source_name(PluginManager& manager, const std::string& plugin_key)
{
    PluginDescriptor descriptor;
    if (manager.try_get_plugin_descriptor(plugin_key, descriptor) && !descriptor.name.empty())
        return descriptor.name;
    return plugin_key;
}

// A runnable script capability exposed as a speed-dial action. source_key = plugin_key
// (identity), so a plugin display-name change does not re-key the action.
struct PluginScriptAction : AppAction
{
    static constexpr const char* kIdPrefix = "plugin_script_action";

    std::string plugin_key;
    std::string capability;

    // The id an action for (plugin_key, capability) would have - lets refresh_capability
    // remove a gone capability without materialising the action.
    static std::string id_for(const std::string& plugin_key, const std::string& capability)
    { return AppAction::compose_id(kIdPrefix, capability.empty() ? plugin_key : capability, plugin_key); }

    PluginScriptAction(std::string plugin_key_in, std::string capability_in, std::string source_name)
        : AppAction(kIdPrefix,
                    capability_in.empty() ? plugin_key_in : capability_in, // title
                    plugin_key_in,                                         // source_key
                    std::move(source_name))
        , plugin_key(std::move(plugin_key_in))
        , capability(std::move(capability_in))
    {}

    AppActionRunResult run(const std::string& /*param*/) const override
    {
        std::string error;
        const ExecutionResult result = PluginManager::instance().run_script_capability(plugin_key, capability, error);
        if (!error.empty())
            return {AppActionRunResult::Level::Error, from_u8(error)};

        const bool skipped      = result.status == PluginResult::Skipped;
        const wxString fallback = skipped ? _L("Script plugin skipped.") : _L("Script plugin finished.");
        return {skipped ? AppActionRunResult::Level::Info : AppActionRunResult::Level::Success,
                result.message.empty() ? fallback : from_u8(result.message)};
    }
};

// Builds an action for a capability, or nullptr if it is not a currently-loaded,
// enabled script capability.
std::unique_ptr<AppAction> make_action(const std::string& plugin_key, const std::string& capability, const std::string& source_name)
{
    PluginManager& manager = PluginManager::instance();
    if (!manager.is_plugin_loaded(plugin_key))
        return nullptr;
    // only_enabled defaults true, so a disabled capability resolves to nullptr here.
    if (!manager.get_plugin_capability({PluginCapabilityType::Script, capability, plugin_key}))
        return nullptr;
    return std::make_unique<PluginScriptAction>(plugin_key, capability, source_name);
}

// ---- built-in command actions (the speed dial "commands" section) ------

constexpr const char* kCommandPrefix  = "orca_command";
constexpr const char* kOrcaSourceKey  = "orca";
constexpr const char* kOrcaSourceName = "OrcaSlicer";
constexpr const char* kSettingPrefix  = "orca_setting";
constexpr const char* kPlateGotoPrefix = "orca_plate_goto";

// Display context for a setting action's eyebrow, e.g. the "Process" in "Process : Quality : Layers".
// Keyed by the option's preset type so the palette reads like the settings sidebar tabs.
std::string setting_type_context(Preset::Type type)
{
    switch (type) {
    case Preset::TYPE_FILAMENT:
    case Preset::TYPE_SLA_MATERIAL: return _u8L("Filament");
    case Preset::TYPE_PRINTER:      return _u8L("Printer");
    case Preset::TYPE_PRINT:
    case Preset::TYPE_SLA_PRINT:
    default:                        return _u8L("Process");
    }
}

// A config setting exposed as a first-class action: selecting it jumps the sidebar to the option.
// The id is keyed by opt_key+type (NOT the display label), so renaming/localizing never re-keys
// the action; title/group/source are purely for display + search. run() performs the jump, and
// the generic registry run() bumps stats so a jump shows up in "recents" like any other action.
struct SettingAction : AppAction
{
    std::string  opt_key;
    Preset::Type type;
    std::wstring category; // localized category, forwarded to jump_to_option

    static std::string id_for(const std::string& opt_key, Preset::Type type)
    { return std::string(kSettingPrefix) + ":" + opt_key + ":" + std::to_string(int(type)); }

    SettingAction(std::string opt_key_in, Preset::Type type_in, std::string title, std::string group,
                  std::wstring category_in, std::string source_name)
        : AppAction(AppActionId{id_for(opt_key_in, type_in)}, std::move(title), kOrcaSourceKey, std::move(source_name))
        , opt_key(std::move(opt_key_in))
        , type(type_in)
        , category(std::move(category_in))
    {
        // A setting is a single-phase command: activating it jumps the sidebar to the option
        // (like the sidebar's own settings search), then the dial closes. run() performs the jump.
        this->kind  = AppActionKind::Command;
        this->group = std::move(group);
    }

    AppActionRunResult run(const std::string& /*param*/) const override
    {
        wxGetApp().sidebar().jump_to_option(opt_key, type, category);
        return {AppActionRunResult::Level::Success};
    }

    // The current value's pattern pictogram (e.g. the selected infill pattern), for the search-result
    // tile. Defined below after the icon helper it delegates to.
    std::string icon() const override;
};

// Jump the preview to a layer selected by a 0-100 percent of the layer range. Best-effort:
// switches to the preview tab and requests a slice (select_view_3D("Preview", false)); if the
// slicer result is already present the slider is repositioned immediately, otherwise the user
// can re-run after slicing.
void go_to_layer(Plater* plater, const std::string& param)
{
    if (!plater)
        return;
    double pct = 50.0;
    try {
        pct = std::stod(param);
    } catch (const std::exception&) {}
    pct = std::clamp(pct, 0.0, 100.0);

    GLCanvas3D* canvas = plater->get_current_canvas3D();
    if (!canvas)
        return;
    GCodeViewer& viewer = canvas->get_gcode_viewer();
    IMSlider* layers    = viewer.get_layers_slider();
    IMSlider* moves     = viewer.get_moves_slider();
    if (!layers || layers->GetMaxValue() <= 0)
        return; // no slice result yet - the slice request above will populate it

    const double max = double(layers->GetMaxValue());
    const int target = int(std::lround(pct / 100.0 * max));
    layers->SetHigherValue(target);
    // In "one layer" mode the lower handle follows the higher one (mirrors arrow-key nav).
    if (layers->is_one_layer())
        layers->SetLowerValue(target);
    layers->set_as_dirty();
    if (moves) {
        moves->SetHigherValue(moves->GetMaxValue());
        moves->set_as_dirty();
    }
}

// Select a named camera view ("top"/"front"/...); Plater::select_view dispatches to the current
// panel. Shared by the view_* speed-dial commands.
AppActionRunResult view_command(Plater* plater, const std::string& dir)
{
    if (plater)
        plater->select_view(dir);
    return {AppActionRunResult::Level::Success};
}

// Dispatch a built-in command. The CommandAction stays a thin value; the actual GUI work
// lives here so it can touch the live app state.
AppActionRunResult run_native_command(const std::string& command_key, const std::string& param)
{
    GUI_App& app = wxGetApp();
    if (app.is_closing())
        return {};

    Plater* plater = app.plater();

    if (command_key == "save_project") {
        if (plater)
            plater->save_project(false);
        return {AppActionRunResult::Level::Success};
    }
    if (command_key == "save_project_as") {
        if (plater)
            plater->save_project(true);
        return {AppActionRunResult::Level::Success};
    }
    if (command_key == "load_project") {
        if (plater)
            plater->load_project();
        return {AppActionRunResult::Level::Success};
    }
    if (command_key == "open_preferences") {
        app.open_preferences();
        return {AppActionRunResult::Level::Success};
    }
    if (command_key == "mode_simple" || command_key == "mode_advanced" || command_key == "mode_expert") {
        const int mode = command_key == "mode_simple" ? comSimple : command_key == "mode_advanced" ? comAdvanced : comExpert;
        app.save_mode(mode);
        return {AppActionRunResult::Level::Success};
    }
    if (command_key == "slice_and_preview") {
        if (plater) {
            // Actually re-slice (respects the toolbar's current plate/all selection), then show the result.
            plater->reslice();
            plater->select_view_3D("Preview", false);
            if (app.mainframe)
                app.mainframe->select_tab(TAB_ID_PREVIEW);
        }
        return {AppActionRunResult::Level::Success};
    }
    if (command_key == "go_to_layer") {
        if (plater) {
            plater->select_view_3D("Preview", false);
            if (app.mainframe)
                app.mainframe->select_tab(TAB_ID_PREVIEW);
            go_to_layer(plater, param);
        }
        return {AppActionRunResult::Level::Success};
    }
    // "go_to_tab" is two-phase: the palette collects the tab after activating it, so native
    // dispatch here is a no-op (the jump goes through the go_to_tab web command).
    if (command_key == "go_to_tab")
        return {AppActionRunResult::Level::Success};

    // ---- Slice -> Export pipeline. Each Plater method self-guards (empty model / error /
    // background-invalid) and then opens its own save dialog / show_error, mirroring the File menu.
    if (command_key == "export_gcode") {
        if (plater)
            plater->export_gcode(false);
        return {AppActionRunResult::Level::Success};
    }
    if (command_key == "export_stl") {
        if (plater)
            plater->export_stl();
        return {AppActionRunResult::Level::Success};
    }
    if (command_key == "export_3mf") {
        if (plater)
            plater->export_core_3mf();
        return {AppActionRunResult::Level::Success};
    }
    if (command_key == "export_sliced_file") {
        if (plater)
            plater->export_gcode_3mf();
        return {AppActionRunResult::Level::Success};
    }
    if (command_key == "export_all_sliced_file") {
        if (plater)
            plater->export_gcode_3mf(true);
        return {AppActionRunResult::Level::Success};
    }

    // ---- Calibration wizards. Each mirrors the menu handler (MainFrame.cpp): recreate the dialog
    // fresh per launch. The palette hides itself and defers dispatch off the webview callback, so a
    // ShowModal() here is safe (same path as open_preferences). The 3D panel is ensured below.
    auto calib = [&](auto&& open) -> AppActionRunResult {
        if (!plater)
            return {AppActionRunResult::Level::Info, _L("Open the 3D view first.")};
        // Auto-switch to the Prepare (3D) view instead of prompting: set the 3D panel
        // synchronously (the wizard's new_project also re-establishes it) and select the
        // Prepare notebook page so the tab label matches. The palette is hidden and this
        // dispatch is deferred off the webview callback, so a modal on a switched tab is safe.
        if (!plater->is_view3D_shown()) {
            plater->select_view_3D("3D");
            if (MainFrame* mf = wxGetApp().mainframe; mf)
                mf->select_tab(TAB_ID_PREPARE);
        }
        open(plater);
        return {AppActionRunResult::Level::Success};
    };
    if (command_key == "calib_temperature")
        return calib([](Plater* p) {
            Temp_Calibration_Dlg* dlg = new Temp_Calibration_Dlg((wxWindow*) wxGetApp().mainframe, wxID_ANY, p);
            dlg->ShowModal();
            dlg->Destroy();
        });
    if (command_key == "calib_max_volumetric")
        return calib([](Plater* p) {
            MaxVolumetricSpeed_Test_Dlg* dlg = new MaxVolumetricSpeed_Test_Dlg((wxWindow*) wxGetApp().mainframe, wxID_ANY, p);
            dlg->ShowModal();
            dlg->Destroy();
        });
    if (command_key == "calib_pressure_advance")
        return calib([](Plater* p) {
            PA_Calibration_Dlg* dlg = new PA_Calibration_Dlg((wxWindow*) wxGetApp().mainframe, wxID_ANY, p);
            dlg->ShowModal();
            dlg->Destroy();
        });
    if (command_key == "calib_flow_ratio")
        return calib([](Plater* p) {
            FlowRateCalibrationDialog* dlg = new FlowRateCalibrationDialog((wxWindow*) wxGetApp().mainframe, wxID_ANY, p);
            dlg->ShowModal();
            dlg->Destroy();
        });
    if (command_key == "calib_retraction")
        return calib([](Plater* p) {
            Retraction_Test_Dlg* dlg = new Retraction_Test_Dlg((wxWindow*) wxGetApp().mainframe, wxID_ANY, p);
            dlg->ShowModal();
            dlg->Destroy();
        });
    if (command_key == "calib_cornering")
        return calib([](Plater* p) {
            Cornering_Test_Dlg* dlg = new Cornering_Test_Dlg((wxWindow*) wxGetApp().mainframe, wxID_ANY, p);
            dlg->ShowModal();
            dlg->Destroy();
        });
    if (command_key == "calib_input_shaping_freq")
        return calib([](Plater* p) {
            Input_Shaping_Freq_Test_Dlg* dlg = new Input_Shaping_Freq_Test_Dlg((wxWindow*) wxGetApp().mainframe, wxID_ANY, p);
            dlg->ShowModal();
            dlg->Destroy();
        });
    if (command_key == "calib_input_shaping_damp")
        return calib([](Plater* p) {
            Input_Shaping_Damp_Test_Dlg* dlg = new Input_Shaping_Damp_Test_Dlg((wxWindow*) wxGetApp().mainframe, wxID_ANY, p);
            dlg->ShowModal();
            dlg->Destroy();
        });
    if (command_key == "calib_vfa")
        return calib([](Plater* p) {
            VFA_Test_Dlg* dlg = new VFA_Test_Dlg((wxWindow*) wxGetApp().mainframe, wxID_ANY, p);
            dlg->ShowModal();
            dlg->Destroy();
        });

    // ---- View controls. select_view dispatches to the current panel; named views + perspective
    // toggle + fit-to-bed mirror the View menu items (MainFrame.cpp). reset_window_layout is direct.
    if (command_key == "view_top")
        return view_command(plater, "top");
    if (command_key == "view_bottom")
        return view_command(plater, "bottom");
    if (command_key == "view_front")
        return view_command(plater, "front");
    if (command_key == "view_rear")
        return view_command(plater, "rear");
    if (command_key == "view_left")
        return view_command(plater, "left");
    if (command_key == "view_right")
        return view_command(plater, "right");
    if (command_key == "view_iso")
        return view_command(plater, "iso");
    if (command_key == "view_default") {
        if (plater) {
            plater->select_view("plate");
            if (GLCanvas3D* canvas = plater->get_current_canvas3D())
                canvas->zoom_to_bed();
        }
        return {AppActionRunResult::Level::Success};
    }
    if (command_key == "view_fit_bed") {
        if (plater)
            if (GLCanvas3D* canvas = plater->get_current_canvas3D())
                canvas->zoom_to_bed();
        return {AppActionRunResult::Level::Success};
    }
    if (command_key == "view_toggle_perspective") {
        if (plater)
            plater->get_camera().select_next_type();
        return {AppActionRunResult::Level::Success};
    }
    if (command_key == "reset_window_layout") {
        if (plater)
            plater->reset_window_layout();
        return {AppActionRunResult::Level::Success};
    }

    // ---- Object / interaction operations (single-phase). Each mirrors a toolbar/menu action and is
    // guarded by an existing can_* / selection check so nothing crashes on empty selection or a busy
    // background worker, and returns a friendly Info instead. Structural ops self-update()/schedule a
    // re-slice; transform ops (mirror/center/drop) post their own schedule-background event. We only
    // need the underlying object (not a specific object index), so a non-capturing lambda is used as
    // the guard/op pair below. Rotate/scale by angle/factor, duplicate (modal count dialog) and
    // cut/segment/merge (unimplemented on Plater) are deliberately left out of this MVP.
    auto obj = [&](bool (*ok)(Plater*), void (*op)(Plater*)) -> AppActionRunResult {
        if (!plater)
            return {AppActionRunResult::Level::Info, _L("Open the 3D view first.")};
        // Object ops read the Prepare (3D) canvas selection, so ensure that view before guarding so
        // a launch from the Preview/other tab doesn't report a spuriously empty selection.
        if (!plater->is_view3D_shown()) {
            plater->select_view_3D("3D");
            if (MainFrame* mf = wxGetApp().mainframe; mf)
                mf->select_tab(TAB_ID_PREPARE);
        }
        if (!ok(plater))
            return {AppActionRunResult::Level::Info, _L("Select an object first.")};
        op(plater);
        return {AppActionRunResult::Level::Success};
    };
    if (command_key == "obj_delete")
        return obj([](Plater* p) { return !p->is_selection_empty(); }, [](Plater* p) { p->remove_selected(); });
    if (command_key == "obj_delete_all")
        return obj([](Plater* p) { return p->can_delete_all(); }, [](Plater* p) { p->delete_all_objects_from_model(); });
    if (command_key == "obj_mirror_x")
        return obj([](Plater* p) { return p->can_mirror(); }, [](Plater* p) { p->mirror(Axis::X); });
    if (command_key == "obj_mirror_y")
        return obj([](Plater* p) { return p->can_mirror(); }, [](Plater* p) { p->mirror(Axis::Y); });
    if (command_key == "obj_mirror_z")
        return obj([](Plater* p) { return p->can_mirror(); }, [](Plater* p) { p->mirror(Axis::Z); });
    if (command_key == "obj_split_objects")
        return obj([](Plater* p) { return p->can_split_to_objects(); }, [](Plater* p) { p->split_object(true); });
    if (command_key == "obj_split_parts")
        return obj([](Plater* p) { return p->can_split_to_volumes(); }, [](Plater* p) { p->split_volume(); });
    if (command_key == "obj_center")
        return obj([](Plater* p) { return !p->is_selection_empty(); }, [](Plater* p) { p->center_selection(); });
    if (command_key == "obj_drop")
        return obj([](Plater* p) { return !p->is_selection_empty(); }, [](Plater* p) { p->drop_selection(); });
    if (command_key == "obj_fit_volume")
        return obj([](Plater* p) { return p->can_scale_to_print_volume(); }, [](Plater* p) { p->scale_selection_to_fit_print_volume(); });
    if (command_key == "obj_instances_up")
        return obj([](Plater* p) { return p->can_increase_instances(); }, [](Plater* p) { p->increase_instances(); });
    if (command_key == "obj_instances_down")
        return obj([](Plater* p) { return p->can_decrease_instances(); }, [](Plater* p) { p->decrease_instances(); });
    if (command_key == "obj_arrange")
        return obj([](Plater* p) { return p->can_arrange(); }, [](Plater* p) { p->arrange(); });
    // Auto-orient has no dedicated can_*; can_arrange covers "objects exist + UI worker idle".
    if (command_key == "obj_orient")
        return obj([](Plater* p) { return p->can_arrange(); }, [](Plater* p) { p->orient(); });

    // ---- Plate management. Plates are a filament (FFF) feature: SLA builds a single plate with
    // no plate UI, and gcode-only mode has no editable project - so gate every plate op on FFF +
    // the normal editor (mirroring where the plate toolbar/menu live). These act on the CURRENT
    // plate (delete/duplicate take -1) except plate_goto, which jumps to the index in `param`.
    auto plate_plater = [&]() -> Plater* {
        return (plater && plater->printer_technology() == ptFFF && !plater->only_gcode_mode()) ? plater : nullptr;
    };
    const AppActionRunResult plate_unavailable{AppActionRunResult::Level::Info, _L("Plates are a filament (FFF) feature.")};

    if (command_key == "plate_add") {
        if (Plater* p = plate_plater(); p) {
            if (!p->can_add_plate())
                return {AppActionRunResult::Level::Info, _L("Cannot add another plate (maximum reached).")};
            p->add_plate();
            return {AppActionRunResult::Level::Success};
        }
        return plate_unavailable;
    }
    if (command_key == "plate_duplicate") {
        if (Plater* p = plate_plater(); p) {
            if (!p->can_add_plate())
                return {AppActionRunResult::Level::Info, _L("Cannot duplicate a plate (maximum reached).")};
            p->duplicate_plate();
            return {AppActionRunResult::Level::Success};
        }
        return plate_unavailable;
    }
    if (command_key == "plate_delete") {
        if (Plater* p = plate_plater(); p) {
            if (!p->can_delete_plate())
                return {AppActionRunResult::Level::Info, _L("Cannot delete the only plate.")};
            p->delete_plate();
            return {AppActionRunResult::Level::Success};
        }
        return plate_unavailable;
    }
    if (command_key == "plate_rename") {
        if (Plater* p = plate_plater(); p) {
            PartPlate* curr = p->get_partplate_list().get_curr_plate();
            PlateNameEditDialog dlg((wxWindow*) wxGetApp().mainframe, wxID_ANY, _L("Edit Plate Name"));
            dlg.set_plate_name(from_u8(curr->get_plate_name()));
            if (dlg.ShowModal() == wxID_YES)
                curr->set_plate_name(dlg.get_plate_name().ToUTF8().data());
            return {AppActionRunResult::Level::Success};
        }
        return plate_unavailable;
    }
    if (command_key == "plate_toggle_lock") {
        if (Plater* p = plate_plater(); p) {
            PartPlateList& plates  = p->get_partplate_list();
            const int      index   = plates.get_curr_plate_index();
            p->take_snapshot("lock partplate");
            plates.lock_plate(index, !plates.is_locked(index));
            return {AppActionRunResult::Level::Success};
        }
        return plate_unavailable;
    }
    if (command_key == "plate_goto") {
        if (Plater* p = plate_plater(); p) {
            PartPlateList& plates = p->get_partplate_list();
            const int      count  = plates.get_plate_count();
            if (count <= 0)
                return {AppActionRunResult::Level::Info, _L("No plates available.")};
            int index = 0;
            try {
                index = std::stoi(param);
            } catch (const std::exception&) {}
            index = std::clamp(index, 0, count - 1);
            p->select_plate(index, false);
            return {AppActionRunResult::Level::Success};
        }
        return plate_unavailable;
    }
    return {AppActionRunResult::Level::Info, _L("Unknown command.")};
}

// A built-in command action. source_key is the constant "orca" so a renamed title never
// re-keys the action (matches the plugin source-key contract).
struct CommandAction : AppAction
{
    std::string command_key;

    CommandAction(std::string command_key, std::string title, std::string group, std::string input = "")
        : AppAction(kCommandPrefix, std::move(title), kOrcaSourceKey, kOrcaSourceName), command_key(std::move(command_key))
    {
        this->kind  = AppActionKind::Command;
        this->group = std::move(group);
        this->input = std::move(input);
    }

    AppActionRunResult run(const std::string& param) const override { return run_native_command(command_key, param); }
};

std::unique_ptr<AppAction> make_command(std::string key, std::string title, std::string group, std::string input = "")
{ return std::make_unique<CommandAction>(std::move(key), std::move(title), std::move(group), std::move(input)); }

// A dynamic "Go to Plate N" action, one per live plate, rebuilt on every snapshot() (so a
// rename/move immediately shows up). id is keyed by plate index, NOT the display title, so
// renaming a plate never re-keys it - the same contract as SettingAction. A pinned "Go to
// Plate N" whose plate is deleted simply stops resolving (visibleFavourites drops dead pins).
struct PlateAction : AppAction
{
    int plate_index;

    static std::string id_for(int index)
    { return AppAction::compose_id(kPlateGotoPrefix, std::to_string(index), kOrcaSourceKey); }

    PlateAction(int index, std::string title, std::string source_name)
        : AppAction(AppActionId{id_for(index)}, std::move(title), kOrcaSourceKey, std::move(source_name))
        , plate_index(index)
    {
        this->kind  = AppActionKind::Command;
        this->group = _u8L("Plate");
    }

    AppActionRunResult run(const std::string& /*param*/) const override
    { return run_native_command("plate_goto", std::to_string(plate_index)); }
};

// The built-in palette commands, registered once at init().
std::vector<std::unique_ptr<AppAction>> native_commands()
{
    std::vector<std::unique_ptr<AppAction>> out;
    // why: _u8L (std::string) for titles/groups - make_command takes std::string; _L would
    // return a wxString and silently fail to convert here.
    out.push_back(make_command("slice_and_preview", _u8L("Slice and Preview"), _u8L("Slice & Export")));
    // Two-phase commands: activating them collects input in the palette, then runs. Settings are
    // not a command here - they're materialised as first-class SettingActions (see materialize_).
    out.push_back(make_command("go_to_layer", _u8L("Go to layer (percent)"), _u8L("Commands"), "percent"));
    out.push_back(make_command("go_to_tab", _u8L("Go to tab..."), _u8L("Commands"), "tab"));
    out.push_back(make_command("load_project", _u8L("Load Project"), _u8L("Commands")));
    out.push_back(make_command("save_project", _u8L("Save Project"), _u8L("Commands")));
    out.push_back(make_command("save_project_as", _u8L("Save Project As"), _u8L("Commands")));
    out.push_back(make_command("open_preferences", _u8L("Preferences"), _u8L("Commands")));
    out.push_back(make_command("mode_simple", _u8L("Mode: Simple"), _u8L("Mode")));
    out.push_back(make_command("mode_advanced", _u8L("Mode: Advanced"), _u8L("Mode")));
    out.push_back(make_command("mode_expert", _u8L("Mode: Expert"), _u8L("Mode")));

    // Slice -> Export pipeline. Each runs a public Plater method; the methods self-guard (empty
    // model / error / background-invalid) and open their own save dialog / show_error.
    out.push_back(make_command("export_gcode", _u8L("Export G-code"), _u8L("Slice & Export")));
    out.push_back(make_command("export_stl", _u8L("Export STL"), _u8L("Slice & Export")));
    out.push_back(make_command("export_3mf", _u8L("Export 3MF"), _u8L("Slice & Export")));
    out.push_back(make_command("export_sliced_file", _u8L("Export Sliced File"), _u8L("Slice & Export")));
    out.push_back(make_command("export_all_sliced_file", _u8L("Export All Sliced Files"), _u8L("Slice & Export")));

    // Calibration wizards (one command per dialog mirroring the Calibration menu, MainFrame.cpp).
    out.push_back(make_command("calib_temperature", _u8L("Temperature Calibration"), _u8L("Calibration")));
    out.push_back(make_command("calib_max_volumetric", _u8L("Max Volumetric Speed Calibration"), _u8L("Calibration")));
    out.push_back(make_command("calib_pressure_advance", _u8L("Pressure Advance Calibration"), _u8L("Calibration")));
    out.push_back(make_command("calib_flow_ratio", _u8L("Flow Ratio Calibration"), _u8L("Calibration")));
    out.push_back(make_command("calib_retraction", _u8L("Retraction Calibration"), _u8L("Calibration")));
    out.push_back(make_command("calib_cornering", _u8L("Cornering Calibration"), _u8L("Calibration")));
    out.push_back(make_command("calib_input_shaping_freq", _u8L("Input Shaping Frequency Calibration"), _u8L("Calibration")));
    out.push_back(make_command("calib_input_shaping_damp", _u8L("Input Shaping Damping Calibration"), _u8L("Calibration")));
    out.push_back(make_command("calib_vfa", _u8L("VFA Calibration"), _u8L("Calibration")));

    // View controls (mirror the View menu; most duplicate the Ctrl+0..6 shortcuts).
    out.push_back(make_command("view_top", _u8L("View: Top"), _u8L("View")));
    out.push_back(make_command("view_bottom", _u8L("View: Bottom"), _u8L("View")));
    out.push_back(make_command("view_front", _u8L("View: Front"), _u8L("View")));
    out.push_back(make_command("view_rear", _u8L("View: Rear"), _u8L("View")));
    out.push_back(make_command("view_left", _u8L("View: Left"), _u8L("View")));
    out.push_back(make_command("view_right", _u8L("View: Right"), _u8L("View")));
    out.push_back(make_command("view_iso", _u8L("View: Isometric"), _u8L("View")));
    out.push_back(make_command("view_default", _u8L("View: Default"), _u8L("View")));
    out.push_back(make_command("view_fit_bed", _u8L("Fit Bed to View"), _u8L("View")));
    out.push_back(make_command("view_toggle_perspective", _u8L("Toggle Perspective"), _u8L("View")));
    out.push_back(make_command("reset_window_layout", _u8L("Reset Window Layout"), _u8L("View")));

    // Object operations (single-phase). Each maps to a public Plater method guarded by a can_* /
    // selection check in run_native_command; structural ops self-update()/schedule re-slice.
    out.push_back(make_command("obj_delete", _u8L("Delete Selected"), _u8L("Object")));
    out.push_back(make_command("obj_delete_all", _u8L("Delete All Objects"), _u8L("Object")));
    out.push_back(make_command("obj_mirror_x", _u8L("Mirror X"), _u8L("Object")));
    out.push_back(make_command("obj_mirror_y", _u8L("Mirror Y"), _u8L("Object")));
    out.push_back(make_command("obj_mirror_z", _u8L("Mirror Z"), _u8L("Object")));
    out.push_back(make_command("obj_split_objects", _u8L("Split to Objects"), _u8L("Object")));
    out.push_back(make_command("obj_split_parts", _u8L("Split to Parts"), _u8L("Object")));
    out.push_back(make_command("obj_center", _u8L("Center Selected on Plate"), _u8L("Object")));
    out.push_back(make_command("obj_drop", _u8L("Drop to Bed"), _u8L("Object")));
    out.push_back(make_command("obj_fit_volume", _u8L("Scale to Fit Print Volume"), _u8L("Object")));
    out.push_back(make_command("obj_instances_up", _u8L("Increase Instances"), _u8L("Object")));
    out.push_back(make_command("obj_instances_down", _u8L("Decrease Instances"), _u8L("Object")));
    out.push_back(make_command("obj_arrange", _u8L("Auto-Arrange"), _u8L("Object")));
    out.push_back(make_command("obj_orient", _u8L("Auto-Orient"), _u8L("Object")));

    // Plate management. These act on the CURRENT plate (like Plater::delete_plate(-1)); the
    // per-plate "Go to Plate N" actions are dynamic and materialised in materialize_plate_actions().
    out.push_back(make_command("plate_add", _u8L("Add Plate"), _u8L("Plate")));
    out.push_back(make_command("plate_duplicate", _u8L("Duplicate Plate"), _u8L("Plate")));
    out.push_back(make_command("plate_delete", _u8L("Delete Plate"), _u8L("Plate")));
    out.push_back(make_command("plate_rename", _u8L("Rename Plate"), _u8L("Plate")));
    out.push_back(make_command("plate_toggle_lock", _u8L("Toggle Plate Lock"), _u8L("Plate")));
    return out;
}

// ---- setting action helpers --------------------------------------------------

// Self-contained base64 encoder (for the tiny pictogram SVGs), avoiding a dependency on the exact
// wxBase64Encode overload/return type across wx versions.
std::string base64_encode(const std::string& data)
{
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    auto enc = [&](unsigned n, int pad) {
        // pad = number of extraneous bytes in the final group (0, 1 or 2):
        //   0 leftover -> 4 chars from all 24 bits
        //   2 leftover (pad=1) -> 3 chars then '='
        //   1 leftover (pad=2) -> 2 chars then "=="
        // The '=' padding always comes LAST; a misplaced '=' decodes as garbage in the webview.
        std::string out;
        out.push_back(tbl[(n >> 18) & 63]);
        out.push_back(tbl[(n >> 12) & 63]);
        out.push_back(pad >= 2 ? '=' : tbl[(n >> 6) & 63]);
        out.push_back(pad >= 1 ? '=' : tbl[n & 63]);
        return out;
    };
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    size_t i = 0;
    for (; i + 3 <= data.size(); i += 3)
        out += enc(((unsigned char) data[i]) << 16 | ((unsigned char) data[i + 1]) << 8 | ((unsigned char) data[i + 2]), 0);
    if (i + 1 == data.size())
        out += enc(((unsigned char) data[i]) << 16, 2);
    else if (i + 2 == data.size())
        out += enc(((unsigned char) data[i]) << 16 | ((unsigned char) data[i + 1]) << 8, 1);
    return out;
}

// data:URI for the pattern pictogram icons/param_<key>.svg, or "" when there is no such icon.
// This mirrors the sidebar Choice field (Field.cpp add_item_bitmaps), which loads param_<value>.svg
// per enum value - most settings have no icon, only pattern-style enums (infill/support patterns).
// Base64 data URIs are used so the embedded webview renders them identically on every backend
// (no file:// subresource / CORS restrictions).
std::string setting_icon_for_key(const std::string& key)
{
    if (key.empty())
        return {};

    const std::string path = (boost::filesystem::path(resources_dir()) / "images" / ("param_" + key + ".svg")).string();
    // Non-throwing stat: a throwing filesystem_error here would propagate out of snapshot() and
    // abort the app (the palette opener). exists(fs ::error_code) never throws.
    boost::system::error_code ec;
    if (!boost::filesystem::exists(path, ec))
        return {};

    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (data.empty())
        return {};

    return "data:image/svg+xml;base64," + base64_encode(data);
}

// The pattern pictogram for a setting's CURRENT value (its enum int), empty when it isn't a
// pattern-style enum or the value has no icon. Used for the search-result tile.
std::string setting_action_icon(const SettingAction& a)
{
    Tab* tab = wxGetApp().get_tab(a.type);
    if (!tab || !tab->get_config())
        return {};
    DynamicPrintConfig* config = tab->get_config();
    const ConfigOptionDef* def = config->def()->get(a.opt_key);
    if (!def || def->type != coEnum || (int(def->type) & int(coVectorType)) != 0)
        return {};
    // Read the value WITHOUT config->opt_int(): the non-const overload routes through a type-checked
    // option<ConfigOptionInt>() that returns null for enum values (type() is coEnum, not coInt) and
    // would deref null. Pull the ConfigOption* and dynamic_cast instead (succeeds: enums derive from
    // ConfigOptionInt), falling back to the def default when the option is absent.
    const ConfigOption* opt = (config->has(a.opt_key) ? config->option(a.opt_key) : def->default_value.get());
    const ConfigOptionInt* int_opt = dynamic_cast<const ConfigOptionInt*>(opt);
    if (!int_opt)
        return {};
    const int value = int_opt->getInt();
    if (def->enum_keys_map)
        for (const auto& kv : *def->enum_keys_map)
            if (kv.second == value)
                return setting_icon_for_key(kv.first);
    return {};
}

std::string SettingAction::icon() const { return setting_action_icon(*this); }

} // namespace

ActionRegistry::~ActionRegistry() = default;

void ActionRegistry::init()
{
    assert(wxThread::IsMain());
    assert(!m_started);
    m_started = true;

    PluginManager& manager = PluginManager::instance();

    auto on_source = [this](const std::string& plugin_key, ActionChange change) {
        if (!wxTheApp || wxGetApp().is_closing())
            return;
        wxGetApp().CallAfter([this, plugin_key, change] {
            if (!wxGetApp().is_closing())
                this->refresh_source(plugin_key, change);
        });
    };
    auto on_capability = [this](const PluginCapabilityId& capability, ActionChange change) {
        if (capability.type != PluginCapabilityType::Script || !wxTheApp || wxGetApp().is_closing())
            return;
        const std::string plugin_key = capability.plugin_key;
        const std::string name       = capability.name;
        wxGetApp().CallAfter([this, plugin_key, name, change] {
            if (!wxGetApp().is_closing())
                this->refresh_capability(plugin_key, name, change);
        });
    };

    // Subscribe before enumerating so a concurrent load cannot land between the initial
    // snapshot and callback registration. Duplicate notifications are safe: upsert is by
    // id and the m_actions scan in refresh_source is idempotent.
    manager.subscribe_on_load_callback([on_source](const std::string& key) { on_source(key, ActionChange::Added); });
    manager.subscribe_on_unload_callback([on_source](const std::string& key) { on_source(key, ActionChange::Removed); });
    manager.subscribe_on_capability_load_callback(
        [on_capability](const PluginCapabilityId& capability) { on_capability(capability, ActionChange::Added); });
    manager.subscribe_on_capability_unload_callback(
        [on_capability](const PluginCapabilityId& capability) { on_capability(capability, ActionChange::Removed); });

    // enumerate current script capabilities
    std::unordered_map<std::string, std::string> source_names;
    for (const PluginDescriptor& desc : manager.get_plugin_descriptors())
        if (!desc.name.empty())
            source_names.emplace(desc.plugin_key, desc.name);

    for (const auto& capability : manager.get_plugin_capabilities("", PluginCapabilityType::Script)) {
        if (!capability)
            continue;
        const std::string& key         = capability->audit_plugin_key();
        auto it                        = source_names.find(key);
        const std::string& source_name = it == source_names.end() ? key : it->second;
        if (auto action = make_action(key, capability->name(), source_name))
            upsert(std::move(action));
    }

    // Built-in palette commands (Save/Load, Preferences, Mode switch, Slice/Preview, Go to layer).
    // Register after plugins so the plugin ids win on any (unlikely) id collision - ids are distinct
    // by prefix, so this is order-independent.
    for (auto& action : native_commands())
        upsert(std::move(action));
}

void ActionRegistry::refresh_source(const std::string& plugin_key, ActionChange change)
{
    assert(wxThread::IsMain());

    // Remove this source's current actions. Collect first - erasing from m_actions while
    // iterating invalidates the iterator. why: m_actions (not the loader) is the source of
    // truth, so this is correct even after the plugin has already unloaded.
    std::vector<std::string> stale;
    for (const auto& [id, action] : m_actions)
        if (action->source_key() == plugin_key)
            stale.push_back(id);
    for (const std::string& id : stale)
        remove(id);

    if (change == ActionChange::Removed)
        return;

    PluginManager& manager        = PluginManager::instance();
    const std::string source_name = find_loaded_source_name(manager, plugin_key);
    for (const auto& capability : manager.get_plugin_capabilities(plugin_key, PluginCapabilityType::Script)) {
        if (!capability)
            continue;
        if (auto action = make_action(plugin_key, capability->name(), source_name))
            upsert(std::move(action));
    }
}

void ActionRegistry::refresh_capability(const std::string& plugin_key, const std::string& capability, ActionChange change)
{
    assert(wxThread::IsMain());

    const std::string id = PluginScriptAction::id_for(plugin_key, capability);
    if (change == ActionChange::Removed) {
        remove(id);
        return;
    }

    PluginManager& manager = PluginManager::instance();
    if (auto action = make_action(plugin_key, capability, find_loaded_source_name(manager, plugin_key)))
        upsert(std::move(action));
    else
        remove(id);
}

void ActionRegistry::upsert(std::unique_ptr<AppAction> action)
{
    assert(wxThread::IsMain());
    if (!action)
        return;

    seed_state(*action);
    std::string id                    = action->id();
    std::shared_ptr<AppAction> stored = std::move(action);
    m_actions.insert_or_assign(std::move(id), std::move(stored));
}

void ActionRegistry::remove(const std::string& id)
{
    assert(wxThread::IsMain());
    m_actions.erase(id);
}

void ActionRegistry::seed_state(AppAction& a) const
{
    // Favourites carry the quick-launch order, so the persisted list is the source of truth
    // (not re-derived from the frecency sort). Cap it so stale configs can't exceed kFavLimit.
    auto favs   = favourite_ids();
    a.favourite = std::find(favs.begin(), favs.end(), a.id()) != favs.end();

    nlohmann::json stats = read_section("stats", nlohmann::json::object());
    auto it              = stats.find(a.id());
    if (it != stats.end() && it->is_object()) {
        a.count = it->value("count", 0);
        a.last  = it->value("last", 0LL);
    } else {
        a.count = 0;
        a.last  = 0;
    }
}

// ---- read surface -----------------------------------------------------------

const AppAction* ActionRegistry::by_id(const std::string& id) const
{
    assert(wxThread::IsMain());
    auto it = m_actions.find(id);
    return it == m_actions.end() ? nullptr : it->second.get();
}

AppAction* ActionRegistry::find(const std::string& id) { return const_cast<AppAction*>(by_id(id)); }

// ---- dispatch + write-through ----------------------------------------------

AppActionRunResult ActionRegistry::run(const std::string& id, const std::string& param)
{
    assert(wxThread::IsMain());
    auto it = m_actions.find(id);
    if (it == m_actions.end())
        return {}; // default Info, empty message
    // why: hold a shared_ptr keep-alive, never a bare map entry. A runner may pump a
    // nested event loop; a queued source refresh can erase the entry while the
    // keep-alive preserves the action until run returns.
    std::shared_ptr<AppAction> keep = it->second;
    AppActionRunResult o            = keep->run(param);
    if (o.level == AppActionRunResult::Level::Busy)
        return o;

    // Bump stats (write-through). Re-read to avoid clobbering a concurrent field.
    nlohmann::json stats = read_section("stats", nlohmann::json::object());
    if (!stats.is_object()) // corrupt (valid-JSON, non-object) value degrades to empty
        stats = nlohmann::json::object();
    nlohmann::json& e = stats[id];
    if (!e.is_object())
        e = nlohmann::json::object();
    e["count"] = e.value("count", 0) + 1;
    e["last"]  = (long long) std::time(nullptr);
    write_section("stats", stats);
    if (AppAction* live = find(id)) {
        live->count = e["count"];
        live->last  = e["last"];
    }
    return o;
}

bool ActionRegistry::set_favourite(const std::string& id, bool on)
{
    assert(wxThread::IsMain());
    // Start from the capped, deduped list so a persisted config can never be written back larger.
    auto favs = favourite_ids();
    auto it   = std::find(favs.begin(), favs.end(), id);
    if (on && it == favs.end()) {
        if (favs.size() >= kFavLimit)
            return false; // bar is full - the caller surfaces a hint
        favs.push_back(id);
    }
    if (!on && it != favs.end())
        favs.erase(it);
    write_section("favourite_actions", nlohmann::json(favs));
    if (AppAction* live = find(id))
        live->favourite = on;
    return true;
}

std::vector<std::string> ActionRegistry::favourite_ids() const
{
    assert(wxThread::IsMain());
    // Enforce the cap + dedupe on read so the persisted order can never grow past kFavLimit,
    // even from an older config. The pinned order is intentionally preserved (slice, not sort).
    std::vector<std::string> favs = read_string_array("favourite_actions");
    std::vector<std::string> out;
    out.reserve(std::min(favs.size(), kFavLimit));
    for (const auto& id : favs) {
        if (out.size() >= kFavLimit)
            break;
        if (std::find(out.begin(), out.end(), id) == out.end())
            out.push_back(id);
    }
    return out;
}

void ActionRegistry::reorder_favourites(const std::vector<std::string>& ids)
{
    assert(wxThread::IsMain());
    auto cur = read_string_array("favourite_actions");
    std::vector<std::string> next;
    // keep the requested order, but only ids that are actually favourites (guard a bad payload)
    for (const auto& id : ids)
        if (std::find(cur.begin(), cur.end(), id) != cur.end() && std::find(next.begin(), next.end(), id) == next.end())
            next.push_back(id);
    // why: don't drop favourites the page omitted (e.g. pins with no live action hidden from the bar)
    for (const auto& id : cur)
        if (std::find(next.begin(), next.end(), id) == next.end())
            next.push_back(id);
    // never write the bar back larger than the quick-launch slots
    if (next.size() > kFavLimit)
        next.resize(kFavLimit);
    write_section("favourite_actions", nlohmann::json(next));
}

void ActionRegistry::materialize_setting_actions()
{
    assert(wxThread::IsMain());

    // Reuse the Sidebar's live searcher: it's the only OptionsSearcher whose groups_and_categories
    // map is populated (Tab::add_key feeds it at build time), and it already mirrors the current
    // configs/mode/printer-technology - i.e. exactly what the sidebar's own search would show. A
    // fresh OptionsSearcher has an empty groups_and_categories, so append_options() would drop every
    // option and nothing would materialise. Turn each visible option into a SettingAction.
    const std::vector<Search::Option>& options = wxGetApp().sidebar().get_searcher().all_options();

    // Load the persisted per-action state ONCE (not per-option) so a re-materialised setting keeps
    // its recency/favourite; mirroring seed_state but amortised over the whole option set.
    nlohmann::json stats = read_section("stats", nlohmann::json::object());
    if (!stats.is_object())
        stats = nlohmann::json::object();
    const std::vector<std::string> favs = favourite_ids();

    std::unordered_set<std::string> seen;
    for (const Search::Option& opt : options) {
        const std::string id = SettingAction::id_for(opt.opt_key(), opt.type);
        seen.insert(id);

        const std::wstring label_w = opt.label_local.empty() ? opt.label : opt.label_local;

        // Eyebrow/source = the full settings path "Process : Quality : Layers" (localized). The JS
        // renders group || source and searches source + " " + group, so putting the whole path in
        // source both displays it and makes it matchable by any segment (e.g. a "quality" query).
        std::wstring path = boost::nowide::widen(setting_type_context(opt.type));
        if (!opt.category_local.empty())
            path += L" : " + opt.category_local;
        if (!opt.group_local.empty())
            path += L" : " + opt.group_local;

        // title = the option leaf name (last label segment); group stays empty so the source path
        // (above) is the single display/search breadcrumb rather than being duplicated.
        auto action = std::make_unique<SettingAction>(opt.opt_key(), opt.type, boost::nowide::narrow(label_w),
                                                      std::string(), opt.category_local, boost::nowide::narrow(path));
        action->favourite                    = std::find(favs.begin(), favs.end(), id) != favs.end();
        if (auto it = stats.find(id); it != stats.end() && it->is_object()) {
            action->count = it->value("count", 0);
            action->last  = it->value("last", 0LL);
        }
        m_actions.insert_or_assign(action->id(), std::shared_ptr<AppAction>(std::move(action)));
    }

    // Drop SettingActions whose option no longer exists in the current configs (e.g. the printer
    // technology / UI mode changed). Non-setting actions are untouched.
    for (auto it = m_actions.begin(); it != m_actions.end();) {
        if (it->first.rfind(kSettingPrefix, 0) == 0 && !seen.count(it->first))
            it = m_actions.erase(it);
        else
            ++it;
    }
}

void ActionRegistry::materialize_plate_actions()
{
    assert(wxThread::IsMain());

    // Plates are a filament (FFF) feature: SLA has a single plate and no plate UI, and gcode-only
    // mode has no editable project - so no "Go to Plate N" actions are offered there.
    Plater* plater = wxTheApp ? wxGetApp().plater() : nullptr;
    if (!plater || plater->printer_technology() != ptFFF || plater->only_gcode_mode()) {
        // Drop any stale plate actions (e.g. the printer technology switched to SLA).
        for (auto it = m_actions.begin(); it != m_actions.end();) {
            if (it->first.rfind(kPlateGotoPrefix, 0) == 0)
                it = m_actions.erase(it);
            else
                ++it;
        }
        return;
    }

    // Persisted per-action state, read ONCE (mirrors materialize_setting_actions) so a relisted
    // "Go to Plate N" keeps its recency/favourite when the plate is renamed - the id is index-keyed.
    nlohmann::json stats = read_section("stats", nlohmann::json::object());
    if (!stats.is_object())
        stats = nlohmann::json::object();
    const std::vector<std::string> favs = favourite_ids();

    const std::vector<PartPlate*>& list = plater->get_partplate_list().get_plate_list();
    std::unordered_set<std::string> seen;
    for (size_t i = 0; i < list.size(); ++i) {
        PartPlate* plate = list[i];
        if (!plate)
            continue;
        const std::string id = PlateAction::id_for(int(i));
        seen.insert(id);

        // "Go to Plate N" + " (name)" when the plate is named, matching the object-list label.
        std::string title(_u8L("Go to Plate"));
        title += " " + std::to_string(i + 1);
        const std::string name = plate->get_plate_name();
        if (!name.empty())
            title += " (" + name + ")";

        auto action     = std::make_unique<PlateAction>(int(i), title, kOrcaSourceName);
        action->favourite = std::find(favs.begin(), favs.end(), id) != favs.end();
        if (auto it = stats.find(id); it != stats.end() && it->is_object()) {
            action->count = it->value("count", 0);
            action->last  = it->value("last", 0LL);
        }
        m_actions.insert_or_assign(action->id(), std::shared_ptr<AppAction>(std::move(action)));
    }

    // Drop plate actions whose index no longer exists (a plate was deleted / moved to the front).
    for (auto it = m_actions.begin(); it != m_actions.end();) {
        if (it->first.rfind(kPlateGotoPrefix, 0) == 0 && !seen.count(it->first))
            it = m_actions.erase(it);
        else
            ++it;
    }
}

bool ActionRegistry::should_ask(const std::string& id) const
{
    assert(wxThread::IsMain());
    auto arr = read_string_array("ask_suppressed");
    return std::find(arr.begin(), arr.end(), id) == arr.end();
}

void ActionRegistry::suppress_ask(const std::string& id)
{
    assert(wxThread::IsMain());
    auto arr = read_string_array("ask_suppressed");
    if (std::find(arr.begin(), arr.end(), id) == arr.end())
        arr.push_back(id);
    write_section("ask_suppressed", nlohmann::json(arr));
}

// ---- snapshot ---------------------------------------------------------------

nlohmann::json ActionRegistry::snapshot()
{
    assert(wxThread::IsMain());
    // Settings and plates are first-class actions; make sure the current visible option set and the
    // live plate list are materialised before we serialise the pool (tabs_list is built by the time
    // the palette opens).
    materialize_setting_actions();
    materialize_plate_actions();

    std::vector<const AppAction*> sorted;
    sorted.reserve(m_actions.size());
    for (const auto& entry : m_actions)
        sorted.push_back(entry.second.get());

    const long long now = (long long) std::time(nullptr);
    std::sort(sorted.begin(), sorted.end(), [&](const AppAction* a, const AppAction* b) {
        double sa = frecency_score(a->count, a->last, now);
        double sb = frecency_score(b->count, b->last, now);
        if (sa != sb)
            return sa > sb;
        if (a->title() != b->title())
            return a->title() < b->title();
        if (a->source_name() != b->source_name())
            return a->source_name() < b->source_name();
        return a->id() < b->id();
    });

    auto action_to_json = [](const AppAction* a) {
        return nlohmann::json({{"id", a->id()},
                               {"title", a->title()},
                               {"source", a->source_name()},
                               {"group", a->group},
                               {"input", a->input},
                               {"shortcut", ""},
                               {"icon", a->icon()}});
    };

    nlohmann::json actions = nlohmann::json::array();
    for (const AppAction* a : sorted)
        actions.push_back(action_to_json(a));

    // why: favourites is the ORDERED pin list - it must come from favourite_actions
    // as stored, not be re-derived from the frecency-sorted actions (that would
    // reorder the favourites bar). The page (js) filters out ids with no live action itself.
    // Cap on read so the bar cannot exceed the quick-launch slots (kFavLimit).
    nlohmann::json favourites(favourite_ids());

    // Recent = the last-N launched actions by recency (only actions with a run history).
    constexpr size_t kRecentLimit = 5;
    std::vector<const AppAction*> recent;
    for (const auto& entry : m_actions)
        if (entry.second->last > 0)
            recent.push_back(entry.second.get());
    std::sort(recent.begin(), recent.end(), [](const AppAction* a, const AppAction* b) {
        if (a->last != b->last)
            return a->last > b->last;
        return a->id() < b->id();
    });
    if (recent.size() > kRecentLimit)
        recent.resize(kRecentLimit);
    nlohmann::json recent_json = nlohmann::json::array();
    for (const AppAction* a : recent)
        recent_json.push_back(action_to_json(a));

    return {{"actions", std::move(actions)}, {"favourites", std::move(favourites)}, {"recent", std::move(recent_json)}};
}

// ---- tab options (enumerate the MainFrame notebook's current pages) ----------

nlohmann::json ActionRegistry::tab_options() const
{
    assert(wxThread::IsMain());
    nlohmann::json out = nlohmann::json::array();
    if (!wxTheApp || wxGetApp().is_closing())
        return out;
    MainFrame* mf = wxGetApp().mainframe;
    if (!mf || !mf->m_tabpanel)
        return out;
    Notebook* notebook = mf->m_tabpanel;
    for (size_t i = 0; i < notebook->GetPageCount(); ++i) {
        const wxString id = notebook->GetPageName(i);
        if (id.empty())
            continue;
        out.push_back({{"id", id.ToStdString()}, {"title", notebook->GetPageText(i).ToStdString()}});
    }
    return out;
}

}} // namespace Slic3r::GUI
