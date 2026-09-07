#pragma once

#include <nlohmann/json.hpp>

#include <wx/string.h>
#include <wx/thread.h>

#include <cassert>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Slic3r { namespace GUI {

// How a source's action set changed. Drives the registry's refresh handlers.
enum class ActionChange { Added, Removed };

// What kind of runnable thing an action is. Drives the palette section + dispatch.
enum class AppActionKind { Plugin, Command };

// Result of running an AppAction, in the action layer's own vocabulary. Concrete
// actions translate their runner-specific result into this generic shape.
struct AppActionRunResult
{
    enum class Level { Success, Info, Error, Busy };

    Level    level = Level::Info;
    wxString message;   // empty = "nothing worth showing"
};

// A speed-dial action: identity + user-state seeded from config + how to run itself.
// Abstract base - the only virtual is run(); concrete subclasses know how to run
// and what their source is.
// note: named AppAction, not Action - Slic3r::GUI::Action is already taken by
// UnsavedChangesDialog's exit-action enum, and this header reaches most GUI TUs.
struct AppAction
{
    const std::string& id() const { return m_id; }
    const std::string& title() const { return m_title; }
    const std::string& source_key() const { return m_source_key; }   // stable source identity
    const std::string& source_name() const { return m_source_name; } // source display name

    // Builds the stable id "<prefix>:<title>:<source_key>". why: one place owns the
    // format - both the base ctor and the one raw-key lookup (removing a capability
    // without an action object) go through this; ids are never parsed back apart.
    static std::string compose_id(std::string_view prefix, std::string_view title, std::string_view source_key)
    {
        std::string out;
        out.reserve(prefix.size() + title.size() + source_key.size() + 2);
        out.append(prefix).append(1, ':').append(title).append(1, ':').append(source_key);
        return out;
    }

    // seeded from AppConfig for the snapshot / sort:
    bool        favourite = false;
    int         count = 0;
    long long   last = 0;     // epoch seconds

    // Speed Dial presentation: Plugin keeps group empty (the UI falls back to the
    // source name); Command sets a section label (e.g. "Commands", "Mode").
    AppActionKind kind = AppActionKind::Plugin;
    std::string   group;
    // Second-phase input descriptor for the palette: "settings" (jump to a config option)
    // or "percent" (jump to layer by a 0-100 value). Empty = run immediately on activation.
    std::string input;

    virtual ~AppAction() = default;
    // Re-resolves + runs (UI thread). `param` carries an optional per-run argument for
    // commands (e.g. a layer percentage); plugins ignore it.
    virtual AppActionRunResult run(const std::string& param = {}) const = 0;

protected:
    // The definition is constructor-set and immutable. Refreshes replace an action
    // instead of mutating identity after the registry has indexed it by id.
    // why: source_key (not the display name) carries identity, so renaming the source's
    // display name leaves the id - and its persisted stats/favourite - intact.
    AppAction(std::string_view prefix, std::string title, std::string source_key, std::string source_name)
        : m_id(compose_id(prefix, title, source_key)),
          m_title(std::move(title)),
          m_source_key(std::move(source_key)),
          m_source_name(std::move(source_name)) {}

private:
    std::string m_id;          // <prefix>:<title>:<source_key> - stable identity + AppConfig key
    std::string m_title;       // display name
    std::string m_source_key;  // stable identity of the action's source (e.g. plugin_key)
    std::string m_source_name; // display name of the action's source
};

// Self-contained sink and single owner of runnable actions for the app session.
//
// Workflow:
// 1. init() (once, UI thread) subscribes to the plugin loader and enumerates the
//    current script capabilities into actions.
// 2. Loader load/unload callbacks route through refresh_source()/refresh_capability(),
//    which upsert()/remove() actions. The registry keeps the only action list and
//    restores persisted user state as actions arrive.
// 3. Consumers use by_id(), snapshot(), and run() without knowing the source.
//
// note: there is exactly one source (script plugins), so it lives inline here rather
// than behind a polymorphic source interface.
class ActionRegistry
{
public:
    ~ActionRegistry();

    // Subscribes to the plugin loader and enumerates its current actions. Call once
    // on the UI thread after the plugin system is up; wires the initial list and live
    // updates together.
    void init();

    // Takes ownership, seeds persisted state, then inserts the action or replaces
    // the action with the same id. A null action is ignored.
    void upsert(std::unique_ptr<AppAction> action);

    // Removes the action with this id. Missing ids are a harmless no-op.
    void remove(const std::string& id);

    // Always-clean read surface. UI thread only.
    const AppAction*                               by_id(const std::string& id) const;

    // Dispatch + write-through (registry is the only thing that touches AppConfig).
    AppActionRunResult run(const std::string& id, const std::string& param = {}); // runs + bumps stats
    void             set_favourite(const std::string& id, bool on);
    void             reorder_favourites(const std::vector<std::string>& ids);   // persist a new bar order

    // Run-confirm gate, keyed by action id (per-action "don't ask again").
    bool should_ask(const std::string& id) const;
    void suppress_ask(const std::string& id);

    // Flat, frecency-sorted snapshot for the webview:
    // {actions:[...], favourites:[...], recent:[...]} (recent = last-N launched by recency).
    nlohmann::json snapshot() const;

    // "Go to setting..." Speed Dial helper: query the current print/filament/printer
    // config options via the sidebar's live OptionsSearcher (the instance Tab registration
    // populates with group/category, and which carries the current printer technology) and
    // return the top matches as JSON. The searcher is re-seeded from the current configs +
    // user mode on every call so the result always reflects what the sidebar's own search
    // would show. An empty/whitespace query returns the recent settings list (below), and the
    // page shows a "type to search" hint when there are no recents.
    nlohmann::json settings_search(const std::string& query);

    // Recently-jumped-to settings, persisted (most-recent-first, capped at 8). Returns the
    // stored JSON array [{opt_key,type,label,category,group},...]; record_setting_recent()
    // prepends an entry (deduped by opt_key+type) and re-persists.
    nlohmann::json settings_recent() const;
    void record_setting_recent(const std::string& opt_key, int type, const std::string& label,
                               const std::string& category, const std::string& group);

    // "Go to tab..." Speed Dial helper: enumerate the MainFrame notebook's current pages
    // as [{id,title},...]. Live by construction - built-in tabs (Home/Prepare/Preview/Device/
    // Project/Calibration) and plugin tabs (plugin.<key>.<name>) are all Notebook pages, so a
    // page appears/disappears with the notebook. Plugin tabs hidden in the overflow menu (many
    // plugins) aren't separate pages and are not listed. Call on the UI thread; null-safe.
    nlohmann::json tab_options() const;

private:
    void         seed_state(AppAction& a) const;               // favourite/stats from config
    AppAction*      find(const std::string& id);

    // Loader callbacks (marshalled to the UI thread) land here. refresh_source rebuilds
    // one plugin's whole action set; refresh_capability touches a single capability.
    void refresh_source(const std::string& plugin_key, ActionChange change);
    void refresh_capability(const std::string& plugin_key, const std::string& capability, ActionChange change);

    bool m_started = false;  // init() runs exactly once; guards double-subscription
    std::unordered_map<std::string, std::shared_ptr<AppAction>> m_actions;  // UI-thread confined; no lock
};

}} // namespace Slic3r::GUI
