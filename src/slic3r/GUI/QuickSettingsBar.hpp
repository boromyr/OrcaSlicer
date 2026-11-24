#ifndef slic3r_GUI_QuickSettingsBar_hpp_
#define slic3r_GUI_QuickSettingsBar_hpp_

#include <wx/panel.h>

#include "libslic3r/PrintConfig.hpp"

class ComboBox;
class SpinInput;
class Label;

namespace Slic3r { namespace GUI {

// ORCA: compact editors for the settings that get changed most often, hosted in the empty
// space of the tab bar next to the Prepare/Preview/Device/Project buttons. Every control is
// a shortcut to an option that also lives in the regular settings tabs; edits go through the
// owning Tab so the preset gets marked dirty and the plater is invalidated exactly as if the
// value had been typed in the tab itself.
class QuickSettingsBar : public wxPanel
{
public:
    explicit QuickSettingsBar(wxWindow* parent);

    // Reload the shown values from the current presets. Cheap, safe to call from any
    // "something changed" hook; controls the user is currently editing are left alone.
    void update();
    void msw_rescale();

private:
    void on_wall_sequence();
    void on_bed_temperature();
    void on_infill_density();

    // Bed type of the current plate, falling back to the project wide one. btDefault when
    // it can't be resolved (no plater yet), in which case the bed temperature is not editable.
    BedType current_bed_type() const;

    ComboBox*  m_wall_sequence{nullptr};
    SpinInput* m_bed_temp{nullptr};
    SpinInput* m_infill_density{nullptr};
    Label*     m_wall_sequence_label{nullptr};
    Label*     m_bed_temp_label{nullptr};
    Label*     m_infill_density_label{nullptr};

    // Guards update() against the preset change notifications our own edits trigger.
    bool m_updating{false};
};

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_QuickSettingsBar_hpp_
