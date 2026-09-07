#include "ActionRegistry.hpp"

#include "GCodeViewer.hpp"
#include "GLCanvas3D.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "IMSlider.hpp"
#include "MainFrame.hpp"
#include "Notebook.hpp"
#include "Plater.hpp"
#include "Search.hpp"
#include "Tab.hpp"
#include "slic3r/plugin/PluginManager.hpp"

#include <libslic3r/AppConfig.hpp>
#include <libslic3r/Config.hpp>
#include <libslic3r/PresetBundle.hpp>
#include <slic3r/plugin/PythonPluginInterface.hpp>

#include <wx/thread.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/nowide/convert.hpp>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <unordered_map>

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
    // "go_to_setting"/"go_to_tab" are two-phase: the palette collects the option after
    // activating it, so dispatch here is a no-op (the actual jump goes through the web command).
    if (command_key == "go_to_setting" || command_key == "go_to_tab")
        return {AppActionRunResult::Level::Success};
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

// The built-in palette commands, registered once at init().
std::vector<std::unique_ptr<AppAction>> native_commands()
{
    std::vector<std::unique_ptr<AppAction>> out;
    // why: _u8L (std::string) for titles/groups - make_command takes std::string; _L would
    // return a wxString and silently fail to convert here.
    out.push_back(make_command("slice_and_preview", _u8L("Slice and Preview"), _u8L("Commands")));
    // Two-phase commands: activating them collects input in the palette, then runs.
    out.push_back(make_command("go_to_layer", _u8L("Go to layer (percent)"), _u8L("Commands"), "percent"));
    // The "…" is avoided in the msgid: use ASCII "..." to keep the .pot extraction simple.
    out.push_back(make_command("go_to_setting", _u8L("Go to setting..."), _u8L("Commands"), "settings"));
    out.push_back(make_command("go_to_tab", _u8L("Go to tab..."), _u8L("Commands"), "tab"));
    out.push_back(make_command("load_project", _u8L("Load Project"), _u8L("Commands")));
    out.push_back(make_command("save_project", _u8L("Save Project"), _u8L("Commands")));
    out.push_back(make_command("save_project_as", _u8L("Save Project As"), _u8L("Commands")));
    out.push_back(make_command("open_preferences", _u8L("Preferences"), _u8L("Commands")));
    out.push_back(make_command("mode_simple", _u8L("Mode: Simple"), _u8L("Mode")));
    out.push_back(make_command("mode_advanced", _u8L("Mode: Advanced"), _u8L("Mode")));
    out.push_back(make_command("mode_expert", _u8L("Mode: Expert"), _u8L("Mode")));
    return out;
}

// Replicates Sidebar's get_search_inputs(): the configs of every tab supporting the current
// printer technology, in the current UI mode.
std::vector<Search::InputInfo> settings_inputs()
{
    std::vector<Search::InputInfo> ret;
    GUI_App& app = wxGetApp();
    if (!app.preset_bundle)
        return ret;
    auto print_tech = app.preset_bundle->printers.get_selected_preset().printer_technology();
    for (Tab* tab : app.tabs_list)
        if (tab && tab->supports_printer_technology(print_tech))
            ret.emplace_back(Search::InputInfo{tab->get_config(), tab->type(), app.get_mode()});
    return ret;
}

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
    auto favs   = read_string_array("favourite_actions");
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

void ActionRegistry::set_favourite(const std::string& id, bool on)
{
    assert(wxThread::IsMain());
    auto favs = read_string_array("favourite_actions");
    auto it   = std::find(favs.begin(), favs.end(), id);
    if (on && it == favs.end())
        favs.push_back(id);
    if (!on && it != favs.end())
        favs.erase(it);
    write_section("favourite_actions", nlohmann::json(favs));
    if (AppAction* live = find(id))
        live->favourite = on;
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
    write_section("favourite_actions", nlohmann::json(next));
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

nlohmann::json ActionRegistry::snapshot() const
{
    assert(wxThread::IsMain());
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
                               {"shortcut", ""}});
    };

    nlohmann::json actions = nlohmann::json::array();
    for (const AppAction* a : sorted)
        actions.push_back(action_to_json(a));

    // why: favourites is the ORDERED pin list - it must come from favourite_actions
    // as stored, not be re-derived from the frecency-sorted actions (that would
    // reorder the favourites bar). The page (js) filters out ids with no live action itself.
    nlohmann::json favourites(read_string_array("favourite_actions"));

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

nlohmann::json ActionRegistry::settings_search(const std::string& query)
{
    assert(wxThread::IsMain());
    std::string q = boost::trim_copy(query);
    // Empty query: show the recently-jumped-to settings instead of a blank list.
    if (q.empty())
        return settings_recent();

    // Use the sidebar's live searcher. It is the instance Tab registration (add_key) populates
    // with each option's group/category, and it carries the current printer technology. A fresh
    // OptionsSearcher has an empty groups_and_categories, so init()/append_options() drops every
    // option and the search returns nothing.
    Search::OptionsSearcher& searcher = wxGetApp().sidebar().get_searcher();
    searcher.init(settings_inputs());
    searcher.search(q, true);
    auto& found = searcher.found_options();

    constexpr size_t kLimit = 20;
    const size_t n          = std::min<size_t>(kLimit, found.size());
    nlohmann::json out      = nlohmann::json::array();
    for (size_t i = 0; i < n; ++i) {
        const auto& opt = searcher.get_option(i);
        // Clean plain label "category : group : label" - OptionsSearcher's own label string
        // carries ImGui icon control chars + <b>/</b> markup (SUPPORTS_MARKUP), which render
        // as garbage in the webview. Build it from the Option's localized strings instead.
        std::wstring plain;
        const std::wstring* prev = nullptr;
        for (const std::wstring* const s : {&opt.category_local, &opt.group_local, &opt.label_local})
            if (s != nullptr && !s->empty() && (prev == nullptr || *prev != *s)) {
                if (!plain.empty())
                    plain += L" : ";
                plain += *s;
                prev = s;
            }
        out.push_back({{"opt_key", opt.opt_key()},
                       {"type", int(opt.type)},
                       {"label", boost::nowide::narrow(plain)},
                       {"category", boost::nowide::narrow(opt.category)},
                       {"group", boost::nowide::narrow(opt.group)}});
    }
    return out;
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

// ---- settings recents (persisted, most-recent-first, capped at 8) -----------

nlohmann::json ActionRegistry::settings_recent() const
{
    assert(wxThread::IsMain());
    return read_section("recent_settings", nlohmann::json::array());
}

void ActionRegistry::record_setting_recent(
    const std::string& opt_key, int type, const std::string& label, const std::string& category, const std::string& group)
{
    assert(wxThread::IsMain());
    if (opt_key.empty())
        return;

    constexpr size_t kLimit = 8;
    auto arr                = read_section("recent_settings", nlohmann::json::array());
    if (!arr.is_array())
        arr = nlohmann::json::array();
    auto same = [&](const nlohmann::json& e) {
        return e.is_object() && e.value("opt_key", std::string()) == opt_key && e.value("type", int(-1)) == type;
    };

    nlohmann::json next = nlohmann::json::array();
    next.push_back({{"opt_key", opt_key}, {"type", type}, {"label", label}, {"category", category}, {"group", group}});
    for (const auto& e : arr)
        if (!same(e))
            next.push_back(e);
    if (next.size() > kLimit)
        next.erase(next.begin() + long(kLimit), next.end());
    write_section("recent_settings", next);
}

}} // namespace Slic3r::GUI
