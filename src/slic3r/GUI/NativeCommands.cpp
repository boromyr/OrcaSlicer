#include "NativeCommands.hpp"

#include "calib_dlg.hpp"
#include "Camera.hpp"
#include "GCodeViewer.hpp"
#include "GLCanvas3D.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "IMSlider.hpp"
#include "MainFrame.hpp"
#include "Plater.hpp"
#include "PlateSettingsDialog.hpp"
#include "DeviceCore/DevManager.h"

#include <libslic3r/Utils.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <string>
#include <tuple>
#include <utility>

namespace Slic3r { namespace GUI {

namespace {

// Plate ops are an FFF feature: SLA has a single plate and no plate UI, gcode-only mode has no
// editable project - so gate every plate op on FFF + the normal editor.
bool is_fff_plater(Plater* plater) { return plater && plater->printer_technology() == ptFFF && !plater->only_gcode_mode(); }

AppActionRunResult plate_unavailable() { return {AppActionRunResult::Level::Info, _L("Plates are a filament (FFF) feature.")}; }

// Switch to the Prepare (3D) panel so object/calibration ops have a live canvas + selection, and
// the notebook page label matches. A no-op when the 3D panel is already shown.
void ensure_3d_view(Plater* plater)
{
    if (plater && !plater->is_view3D_shown()) {
        plater->select_view_3D("3D");
        if (MainFrame* mf = wxGetApp().mainframe; mf)
            mf->select_tab(TAB_ID_PREPARE);
    }
}

// Object op guard + run: object ops read the Prepare canvas selection, so ensure that view first so
// a launch from another tab doesn't report a spuriously empty selection.
AppActionRunResult object_op(Plater* plater, bool (*ok)(Plater*), void (*op)(Plater*))
{
    if (!plater)
        return {AppActionRunResult::Level::Info, _L("Open the 3D view first.")};
    ensure_3d_view(plater);
    if (!ok(plater))
        return {AppActionRunResult::Level::Info, _L("Select an object first.")};
    op(plater);
    return {AppActionRunResult::Level::Success};
}

// Jump the preview to a layer selected by a 0-100 percent of the layer range. Best-effort: switches
// to the preview tab and requests a slice; if the slicer result is already present the slider is
// repositioned immediately, otherwise the user can re-run after slicing.
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
        return;

    const double max = double(layers->GetMaxValue());
    const int target = int(std::lround(pct / 100.0 * max));
    layers->SetHigherValue(target);
    if (layers->is_one_layer())
        layers->SetLowerValue(target);
    layers->set_as_dirty();
    if (moves) {
        moves->SetHigherValue(moves->GetMaxValue());
        moves->set_as_dirty();
    }
}

// Select a named camera view. Plater::select_view dispatches to the current panel.
AppActionRunResult view_command(Plater* plater, const std::string& dir)
{
    if (plater)
        plater->select_view(dir);
    return {AppActionRunResult::Level::Success};
}

// Calibration wizards. Reuses MainFrame::run_calibration so the speed dial shows the same cached
// member dialogs as the Calibration menu (the menu handlers call run_calibration too).
AppActionRunResult calib_command(CalibKind kind)
{
    MainFrame* mf = wxGetApp().mainframe;
    if (!mf)
        return {AppActionRunResult::Level::Info, _L("Open the 3D view first.")};
    ensure_3d_view(wxGetApp().plater());
    mf->run_calibration(kind);
    return {AppActionRunResult::Level::Success};
}

std::vector<NativeCommand> build_command_catalog()
{
    std::vector<NativeCommand> out;
    auto add = [&](std::string key, std::string title, std::string group,
                   std::function<AppActionRunResult(const std::string&)> runner, std::string input = {}) {
        out.push_back({std::move(key), std::move(title), std::move(group), std::move(input), std::move(runner)});
    };

    // ---- Slice & Export ----
    add("slice_and_preview", _u8L("Slice and Preview"), _u8L("Slice & Export"), [](const std::string&) {
        Plater* plater = wxGetApp().plater();
        if (plater) {
            plater->reslice();
            plater->select_view_3D("Preview", false);
            if (MainFrame* mf = wxGetApp().mainframe; mf)
                mf->select_tab(TAB_ID_PREVIEW);
        }
        return AppActionRunResult{AppActionRunResult::Level::Success};
    });

    add(
        "go_to_layer", _u8L("Go to layer (percent)"), _u8L("Commands"),
        [](const std::string& param) {
            Plater* plater = wxGetApp().plater();
            if (plater) {
                plater->select_view_3D("Preview", false);
                if (MainFrame* mf = wxGetApp().mainframe; mf)
                    mf->select_tab(TAB_ID_PREVIEW);
                go_to_layer(plater, param);
            }
            return AppActionRunResult{AppActionRunResult::Level::Success};
        },
        "percent");

    // "go_to_tab" is two-phase: the palette collects the tab after activating it, so dispatch here
    // is a no-op (the jump goes through the go_to_tab web command).
    add(
        "go_to_tab", _u8L("Go to tab..."), _u8L("Commands"),
        [](const std::string&) { return AppActionRunResult{AppActionRunResult::Level::Success}; }, "tab");

    add("load_project", _u8L("Load Project"), _u8L("Commands"), [](const std::string&) {
        if (Plater* plater = wxGetApp().plater())
            plater->load_project();
        return AppActionRunResult{AppActionRunResult::Level::Success};
    });
    add("save_project", _u8L("Save Project"), _u8L("Commands"), [](const std::string&) {
        if (Plater* plater = wxGetApp().plater())
            plater->save_project(false);
        return AppActionRunResult{AppActionRunResult::Level::Success};
    });
    add("save_project_as", _u8L("Save Project As"), _u8L("Commands"), [](const std::string&) {
        if (Plater* plater = wxGetApp().plater())
            plater->save_project(true);
        return AppActionRunResult{AppActionRunResult::Level::Success};
    });
    add("open_preferences", _u8L("Preferences"), _u8L("Commands"), [](const std::string&) {
        wxGetApp().open_preferences();
        return AppActionRunResult{AppActionRunResult::Level::Success};
    });

    // ---- Mode ----
    add("mode_simple", _u8L("Mode: Simple"), _u8L("Mode"), [](const std::string&) {
        wxGetApp().save_mode(comSimple);
        return AppActionRunResult{AppActionRunResult::Level::Success};
    });
    add("mode_advanced", _u8L("Mode: Advanced"), _u8L("Mode"), [](const std::string&) {
        wxGetApp().save_mode(comAdvanced);
        return AppActionRunResult{AppActionRunResult::Level::Success};
    });
    add("mode_expert", _u8L("Mode: Expert"), _u8L("Mode"), [](const std::string&) {
        wxGetApp().save_mode(comExpert);
        return AppActionRunResult{AppActionRunResult::Level::Success};
    });

    // ---- Export pipeline ----
    add("export_gcode", _u8L("Export G-code"), _u8L("Slice & Export"), [](const std::string&) {
        if (Plater* plater = wxGetApp().plater())
            plater->export_gcode(false);
        return AppActionRunResult{AppActionRunResult::Level::Success};
    });
    add("export_stl", _u8L("Export STL"), _u8L("Slice & Export"), [](const std::string&) {
        if (Plater* plater = wxGetApp().plater())
            plater->export_stl();
        return AppActionRunResult{AppActionRunResult::Level::Success};
    });
    add("export_3mf", _u8L("Export 3MF"), _u8L("Slice & Export"), [](const std::string&) {
        if (Plater* plater = wxGetApp().plater())
            plater->export_core_3mf();
        return AppActionRunResult{AppActionRunResult::Level::Success};
    });
    add("export_sliced_file", _u8L("Export Sliced File"), _u8L("Slice & Export"), [](const std::string&) {
        if (Plater* plater = wxGetApp().plater())
            plater->export_gcode_3mf(false);
        return AppActionRunResult{AppActionRunResult::Level::Success};
    });
    add("export_all_sliced_file", _u8L("Export All Sliced Files"), _u8L("Slice & Export"),
        [](const std::string&) {
            if (Plater* plater = wxGetApp().plater())
                plater->export_gcode_3mf(true);
            return AppActionRunResult{AppActionRunResult::Level::Success};
        });

    // ---- Calibration ----
    add("calib_temperature", _u8L("Temperature Calibration"), _u8L("Calibration"),
        [](const std::string&) { return calib_command(CalibKind::Temperature); });
    add("calib_max_volumetric", _u8L("Max Volumetric Speed Calibration"), _u8L("Calibration"),
        [](const std::string&) { return calib_command(CalibKind::MaxVolumetric); });
    add("calib_pressure_advance", _u8L("Pressure Advance Calibration"), _u8L("Calibration"),
        [](const std::string&) { return calib_command(CalibKind::PressureAdvance); });
    add("calib_flow_ratio", _u8L("Flow Ratio Calibration"), _u8L("Calibration"),
        [](const std::string&) { return calib_command(CalibKind::FlowRatio); });
    add("calib_retraction", _u8L("Retraction Calibration"), _u8L("Calibration"),
        [](const std::string&) { return calib_command(CalibKind::Retraction); });
    add("calib_cornering", _u8L("Cornering Calibration"), _u8L("Calibration"),
        [](const std::string&) { return calib_command(CalibKind::Cornering); });
    add("calib_input_shaping_freq", _u8L("Input Shaping Frequency Calibration"), _u8L("Calibration"),
        [](const std::string&) { return calib_command(CalibKind::InputShapingFreq); });
    add("calib_input_shaping_damp", _u8L("Input Shaping Damping Calibration"), _u8L("Calibration"),
        [](const std::string&) { return calib_command(CalibKind::InputShapingDamp); });
    add("calib_vfa", _u8L("VFA Calibration"), _u8L("Calibration"),
        [](const std::string&) { return calib_command(CalibKind::VFA); });

    // ---- View ----
    for (auto [key, dir, title] :
         std::initializer_list<std::tuple<const char*, const char*, const char*>>{{"view_top", "top", "View: Top"},
                                                                                  {"view_bottom", "bottom", "View: Bottom"},
                                                                                  {"view_front", "front", "View: Front"},
                                                                                  {"view_rear", "rear", "View: Rear"},
                                                                                  {"view_left", "left", "View: Left"},
                                                                                  {"view_right", "right", "View: Right"},
                                                                                  {"view_iso", "iso", "View: Isometric"}}) {
        std::string k = key, d = dir;
        add(k, Slic3r::GUI::I18N::translate_utf8(title), _u8L("View"),
            [d](const std::string&) { return view_command(wxGetApp().plater(), d); });
    }
    add("view_default", _u8L("View: Default"), _u8L("View"), [](const std::string&) {
        Plater* plater = wxGetApp().plater();
        if (plater) {
            plater->select_view("plate");
            if (GLCanvas3D* canvas = plater->get_current_canvas3D())
                canvas->zoom_to_bed();
        }
        return AppActionRunResult{AppActionRunResult::Level::Success};
    });
    add("view_fit_bed", _u8L("Fit Bed to View"), _u8L("View"), [](const std::string&) {
        if (Plater* plater = wxGetApp().plater())
            if (GLCanvas3D* canvas = plater->get_current_canvas3D())
                canvas->zoom_to_bed();
        return AppActionRunResult{AppActionRunResult::Level::Success};
    });
    add("view_toggle_perspective", _u8L("Toggle Perspective"), _u8L("View"), [](const std::string&) {
        if (Plater* plater = wxGetApp().plater())
            plater->get_camera().select_next_type();
        return AppActionRunResult{AppActionRunResult::Level::Success};
    });
    add("reset_window_layout", _u8L("Reset Window Layout"), _u8L("View"), [](const std::string&) {
        if (Plater* plater = wxGetApp().plater())
            plater->reset_window_layout();
        return AppActionRunResult{AppActionRunResult::Level::Success};
    });

    // ---- Object ----
    add("obj_delete", _u8L("Delete Selected"), _u8L("Object"), [](const std::string&) {
        return object_op(wxGetApp().plater(), [](Plater* p) { return !p->is_selection_empty(); }, [](Plater* p) { p->remove_selected(); });
    });
    add("obj_delete_all", _u8L("Delete All Objects"), _u8L("Object"), [](const std::string&) {
        return object_op(
            wxGetApp().plater(), [](Plater* p) { return p->can_delete_all(); }, [](Plater* p) { p->delete_all_objects_from_model(); });
    });
    add("obj_mirror_x", _u8L("Mirror X"), _u8L("Object"), [](const std::string&) {
        return object_op(wxGetApp().plater(), [](Plater* p) { return p->can_mirror(); }, [](Plater* p) { p->mirror(Axis::X); });
    });
    add("obj_mirror_y", _u8L("Mirror Y"), _u8L("Object"), [](const std::string&) {
        return object_op(wxGetApp().plater(), [](Plater* p) { return p->can_mirror(); }, [](Plater* p) { p->mirror(Axis::Y); });
    });
    add("obj_mirror_z", _u8L("Mirror Z"), _u8L("Object"), [](const std::string&) {
        return object_op(wxGetApp().plater(), [](Plater* p) { return p->can_mirror(); }, [](Plater* p) { p->mirror(Axis::Z); });
    });
    add("obj_split_objects", _u8L("Split to Objects"), _u8L("Object"), [](const std::string&) {
        return object_op(wxGetApp().plater(), [](Plater* p) { return p->can_split_to_objects(); }, [](Plater* p) { p->split_object(true); });
    });
    add("obj_split_parts", _u8L("Split to Parts"), _u8L("Object"), [](const std::string&) {
        return object_op(wxGetApp().plater(), [](Plater* p) { return p->can_split_to_volumes(); }, [](Plater* p) { p->split_volume(); });
    });
    add("obj_center", _u8L("Center Selected on Plate"), _u8L("Object"), [](const std::string&) {
        return object_op(wxGetApp().plater(), [](Plater* p) { return !p->is_selection_empty(); }, [](Plater* p) { p->center_selection(); });
    });
    add("obj_drop", _u8L("Drop to Bed"), _u8L("Object"), [](const std::string&) {
        return object_op(wxGetApp().plater(), [](Plater* p) { return !p->is_selection_empty(); }, [](Plater* p) { p->drop_selection(); });
    });
    add("obj_fit_volume", _u8L("Scale to Fit Print Volume"), _u8L("Object"), [](const std::string&) {
        return object_op(
            wxGetApp().plater(), [](Plater* p) { return p->can_scale_to_print_volume(); },
            [](Plater* p) { p->scale_selection_to_fit_print_volume(); });
    });
    add("obj_instances_up", _u8L("Increase Instances"), _u8L("Object"), [](const std::string&) {
        return object_op(
            wxGetApp().plater(), [](Plater* p) { return p->can_increase_instances(); }, [](Plater* p) { p->increase_instances(); });
    });
    add("obj_instances_down", _u8L("Decrease Instances"), _u8L("Object"), [](const std::string&) {
        return object_op(
            wxGetApp().plater(), [](Plater* p) { return p->can_decrease_instances(); }, [](Plater* p) { p->decrease_instances(); });
    });
    add("obj_arrange", _u8L("Auto-Arrange"), _u8L("Object"), [](const std::string&) {
        return object_op(wxGetApp().plater(), [](Plater* p) { return p->can_arrange(); }, [](Plater* p) { p->arrange(); });
    });
    add("obj_orient", _u8L("Auto-Orient"), _u8L("Object"), [](const std::string&) {
        return object_op(wxGetApp().plater(), [](Plater* p) { return p->can_arrange(); }, [](Plater* p) { p->orient(); });
    });

    // ---- Plate ----
    add("plate_add", _u8L("Add Plate"), _u8L("Plate"), [](const std::string&) {
        Plater* plater = wxGetApp().plater();
        if (!is_fff_plater(plater))
            return plate_unavailable();
        if (!plater->can_add_plate())
            return AppActionRunResult{AppActionRunResult::Level::Info, _L("Cannot add another plate (maximum reached).")};
        plater->add_plate();
        return AppActionRunResult{AppActionRunResult::Level::Success};
    });
    add("plate_duplicate", _u8L("Duplicate Plate"), _u8L("Plate"), [](const std::string&) {
        Plater* plater = wxGetApp().plater();
        if (!is_fff_plater(plater))
            return plate_unavailable();
        if (!plater->can_add_plate())
            return AppActionRunResult{AppActionRunResult::Level::Info, _L("Cannot duplicate a plate (maximum reached).")};
        plater->duplicate_plate();
        return AppActionRunResult{AppActionRunResult::Level::Success};
    });
    add("plate_delete", _u8L("Delete Plate"), _u8L("Plate"), [](const std::string&) {
        Plater* plater = wxGetApp().plater();
        if (!is_fff_plater(plater))
            return plate_unavailable();
        if (!plater->can_delete_plate())
            return AppActionRunResult{AppActionRunResult::Level::Info, _L("Cannot delete the only plate.")};
        plater->delete_plate();
        return AppActionRunResult{AppActionRunResult::Level::Success};
    });
    add("plate_rename", _u8L("Rename Plate"), _u8L("Plate"), [](const std::string&) {
        Plater* plater = wxGetApp().plater();
        if (!is_fff_plater(plater))
            return plate_unavailable();
        PartPlate* curr = plater->get_partplate_list().get_curr_plate();
        PlateNameEditDialog dlg((wxWindow*) wxGetApp().mainframe, wxID_ANY, _L("Edit Plate Name"));
        dlg.set_plate_name(from_u8(curr->get_plate_name()));
        if (dlg.ShowModal() == wxID_YES)
            curr->set_plate_name(dlg.get_plate_name().ToUTF8().data());
        return AppActionRunResult{AppActionRunResult::Level::Success};
    });
    add("plate_toggle_lock", _u8L("Toggle Plate Lock"), _u8L("Plate"), [](const std::string&) {
        Plater* plater = wxGetApp().plater();
        if (!is_fff_plater(plater))
            return plate_unavailable();
        PartPlateList& plates = plater->get_partplate_list();
        const int index       = plates.get_curr_plate_index();
        plater->take_snapshot("lock partplate");
        plates.lock_plate(index, !plates.is_locked(index));
        return AppActionRunResult{AppActionRunResult::Level::Success};
    });
    add("plate_goto", _u8L("Go to Plate"), _u8L("Plate"), [](const std::string& param) {
        Plater* plater = wxGetApp().plater();
        if (!is_fff_plater(plater))
            return plate_unavailable();
        PartPlateList& plates = plater->get_partplate_list();
        const int count       = plates.get_plate_count();
        if (count <= 0)
            return AppActionRunResult{AppActionRunResult::Level::Info, _L("No plates available.")};
        int index = 0;
        try {
            index = std::stoi(param);
        } catch (const std::exception&) {}
        index = std::clamp(index, 0, count - 1);
        plater->select_plate(index, false);
        return AppActionRunResult{AppActionRunResult::Level::Success};
    });

    // ---- Printer / device connection ----
    add("connect_printer", _u8L("Connect Printer"), _u8L("Printer"), [](const std::string&) {
        if (Plater* plater = wxGetApp().plater())
            plater->connect_to_printer();
        return AppActionRunResult{AppActionRunResult::Level::Success};
    });
    add("disconnect_printer", _u8L("Disconnect Printer"), _u8L("Printer"), [](const std::string&) {
        DeviceManager* dev = wxGetApp().getDeviceManager();
        if (!dev)
            return AppActionRunResult{AppActionRunResult::Level::Info, _L("Printer connection is unavailable.")};
        if (MachineObject* machine = dev->get_selected_machine()) {
            machine->disconnect();
            dev->set_selected_machine("");
            return AppActionRunResult{AppActionRunResult::Level::Success};
        }
        return AppActionRunResult{AppActionRunResult::Level::Info, _L("Printer is not connected.")};
    });
    add("sync_ams", _u8L("Synchronize Filament List from AMS"), _u8L("Printer"), [](const std::string&) {
        Plater* plater     = wxGetApp().plater();
        DeviceManager* dev = wxGetApp().getDeviceManager();
        if (dev && dev->get_selected_machine() && plater) {
            plater->sidebar().sync_ams_list();
            return AppActionRunResult{AppActionRunResult::Level::Success};
        }
        return AppActionRunResult{AppActionRunResult::Level::Info, _L("Connect a printer to synchronize the AMS filament list.")};
    });

    // ---- Presets / cloud ----
    add("preset_bundle", _u8L("Open Preset Bundle"), _u8L("Presets"), [](const std::string&) {
        wxGetApp().open_presetbundledialog();
        return AppActionRunResult{AppActionRunResult::Level::Success};
    });
    add("sync_presets", _u8L("Sync Presets"), _u8L("Presets"), [](const std::string&) {
        if (!wxGetApp().is_user_login())
            return AppActionRunResult{AppActionRunResult::Level::Info, _L("Sign in to sync presets.")};
        wxGetApp().restart_sync_user_preset();
        return AppActionRunResult{AppActionRunResult::Level::Success};
    });

    // ---- Import ----
    add("import_file", _u8L("Import 3MF/STL/STEP/SVG/OBJ/AMF"), _u8L("Import"), [](const std::string&) {
        if (Plater* plater = wxGetApp().plater()) {
#ifdef __APPLE__
            plater->add_model();
#else
                plater->add_file();
#endif
        }
        return AppActionRunResult{AppActionRunResult::Level::Success};
    });
    add("import_zip_archive", _u8L("Import ZIP Archive"), _u8L("Import"), [](const std::string&) {
        if (Plater* plater = wxGetApp().plater())
            plater->import_zip_archive();
        return AppActionRunResult{AppActionRunResult::Level::Success};
    });
    add("import_configs", _u8L("Import Configs"), _u8L("Import"), [](const std::string&) {
        if (MainFrame* mf = wxGetApp().mainframe)
            mf->load_config_file();
        return AppActionRunResult{AppActionRunResult::Level::Success};
    });

    // ---- Export extras ----
    add("export_stl_multi", _u8L("Export All Objects as STLs"), _u8L("Export"), [](const std::string&) {
        if (Plater* plater = wxGetApp().plater())
            plater->export_stl(false, false, true);
        return AppActionRunResult{AppActionRunResult::Level::Success};
    });
    add("export_drc_single", _u8L("Export All Objects as DRC (one file)"), _u8L("Export"), [](const std::string&) {
        if (Plater* plater = wxGetApp().plater())
            plater->export_stl(false, false, false, FT_DRC);
        return AppActionRunResult{AppActionRunResult::Level::Success};
    });
    add("export_drc_multi", _u8L("Export All Objects as DRCs"), _u8L("Export"), [](const std::string&) {
        if (Plater* plater = wxGetApp().plater())
            plater->export_stl(false, false, true, FT_DRC);
        return AppActionRunResult{AppActionRunResult::Level::Success};
    });
    add("export_toolpaths_obj", _u8L("Export Toolpaths as OBJ"), _u8L("Export"), [](const std::string&) {
        if (Plater* plater = wxGetApp().plater())
            plater->export_toolpaths_to_obj();
        return AppActionRunResult{AppActionRunResult::Level::Success};
    });
    add("export_config", _u8L("Export Preset Bundle"), _u8L("Export"), [](const std::string&) {
        if (MainFrame* mf = wxGetApp().mainframe)
            mf->export_config();
        return AppActionRunResult{AppActionRunResult::Level::Success};
    });

    return out;
}

} // namespace

const std::vector<NativeCommand>& NativeCommands::catalog()
{
    static const std::vector<NativeCommand> commands = build_command_catalog();
    return commands;
}

AppActionRunResult NativeCommands::run(const std::string& key, const std::string& param)
{
    GUI_App& app = wxGetApp();
    if (app.is_closing())
        return {};
    for (const NativeCommand& c : catalog())
        if (c.key == key)
            return c.runner(param);
    return {AppActionRunResult::Level::Info, _L("Unknown command.")};
}

}} // namespace Slic3r::GUI
