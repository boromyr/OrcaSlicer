#include <catch2/catch_test_macros.hpp>

#include "slic3r/GUI/ActionRegistry.hpp"
#include "slic3r/GUI/NativeCommands.hpp"

#include <memory>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

using Slic3r::GUI::AppAction;
using Slic3r::GUI::AppActionRunResult;
using Slic3r::GUI::ActionRegistry;

namespace {

// AppAction is abstract; this minimal concrete action lets the tests exercise its
// constructor-composed identity without involving a plugin runner.
class TestAppAction final : public AppAction
{
public:
    TestAppAction() : AppAction("test", "Action title", "src-key", "Action source") {}

    AppActionRunResult run(const std::string& param = {}) const override { return {}; }
};

} // namespace

TEST_CASE("AppAction composes a stable id from prefix:title:source_key", "[speeddial][actions]")
{
    CHECK(AppAction::compose_id("test", "Action title", "src-key") == "test:Action title:src-key");
    // source_key (not the display name) carries identity, so it is the third field.
    CHECK(AppAction::compose_id("script", "Do Thing", "pack.py") == "script:Do Thing:pack.py");
}

TEST_CASE("AppAction definitions are immutable after construction", "[speeddial][actions]")
{
    using StringAccessor = const std::string& (AppAction::*) () const;

    STATIC_CHECK(std::is_same_v<decltype(&AppAction::id), StringAccessor>);
    STATIC_CHECK(std::is_same_v<decltype(&AppAction::title), StringAccessor>);
    STATIC_CHECK(std::is_same_v<decltype(&AppAction::source_key), StringAccessor>);
    STATIC_CHECK(std::is_same_v<decltype(&AppAction::source_name), StringAccessor>);

    const TestAppAction action;
    CHECK(action.id() == "test:Action title:src-key");
    CHECK(action.title() == "Action title");
    CHECK(action.source_key() == "src-key");
    CHECK(action.source_name() == "Action source");
}

TEST_CASE("ActionRegistry takes exclusive ownership of published actions", "[speeddial][actions]")
{
    using ExpectedUpsert = void (ActionRegistry::*)(std::unique_ptr<AppAction>);

    STATIC_CHECK(std::is_same_v<decltype(&ActionRegistry::upsert), ExpectedUpsert>);
}

// A dynamic "Go to Plate N" action is keyed by plate index (not the display title), so renaming
// a plate never re-keys it - the same contract as a setting action.
TEST_CASE("Go-to-plate actions are keyed by index, not title", "[speeddial][actions]")
{
    CHECK(AppAction::compose_id("orca_plate_goto", "0", "orca") == "orca_plate_goto:0:orca");
    CHECK(AppAction::compose_id("orca_plate_goto", "2", "orca") == "orca_plate_goto:2:orca");
}

// A dynamic "Open recent project" action is keyed by file path (not the display name), so renaming
// a project or reordering the recents list never re-keys it - the same contract as a setting action.
TEST_CASE("Recent-project actions are keyed by path, not title", "[speeddial][actions]")
{
    CHECK(AppAction::compose_id("orca_recent_project", "/a/b/project.3mf", "orca") ==
          "orca_recent_project:/a/b/project.3mf:orca");
    CHECK(AppAction::compose_id("orca_recent_project", "C:/Data/cube.3mf", "orca") ==
          "orca_recent_project:C:/Data/cube.3mf:orca");
}

// A built-in command is keyed by its stable catalog key (not the localized display title), so a
// rename or a UI-language switch never re-keys the action and its persisted favourite/stats survive.
TEST_CASE("Command actions are keyed by catalog key, not display title", "[speeddial][actions]")
{
    CHECK(AppAction::compose_id("orca_command", "save_project", "orca") == "orca_command:save_project:orca");
    // The second field is the stable key, so distinct commands never collide.
    CHECK(AppAction::compose_id("orca_command", "save_project", "orca") !=
          AppAction::compose_id("orca_command", "load_project", "orca"));
}

// The quick-launch cap must stay 10 to match the numbered Alt/Option+1..9,0 keys. The web palette
// mirrors it as K_FAV_LIMIT (asserted in speeddial.test.js); the C++ side pins it here.
static_assert(Slic3r::GUI::ActionRegistry::kFavLimit == 10, "kFavLimit must stay 10");

TEST_CASE("Favourite lists are capped and deduped preserving order", "[speeddial][actions]")
{
    using Slic3r::GUI::cap_favourites;

    CHECK(cap_favourites({}, 10) == std::vector<std::string>{});
    CHECK(cap_favourites({"a", "b", "a"}, 10) == std::vector<std::string>{"a", "b"});
    CHECK(cap_favourites({"c", "a", "b", "c"}, 3) == std::vector<std::string>{"c", "a", "b"});
    CHECK(cap_favourites({"a", "b"}, 0) == std::vector<std::string>{});
}

TEST_CASE("Native command catalog has unique keys and present titles", "[speeddial][actions]")
{
    const std::vector<Slic3r::GUI::NativeCommand>& commands = Slic3r::GUI::NativeCommands::catalog();
    CHECK_FALSE(commands.empty());

    std::set<std::string> seen;
    for (const auto& c : commands) {
        CHECK_FALSE(c.key.empty());
        CHECK_FALSE(c.title.empty());
        // A duplicated key would silently shadow the earlier command in the palette.
        CHECK(seen.insert(c.key).second);
    }
}
