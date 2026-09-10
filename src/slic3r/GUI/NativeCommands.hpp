#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ActionRegistry.hpp" // for AppActionRunResult

namespace Slic3r { namespace GUI {

// A built-in speed-dial command: identity + how to run it. The registry keeps commands as thin
// values (CommandAction) and routes run() here, so this catalog is the single source of truth for
// the behaviour (runner => an owner method) and the presentation (title/group/input).
struct NativeCommand
{
    std::string key;
    std::string title;
    std::string group;
    std::string input; // "percent"/"tab" or "" for immediate run
    std::function<AppActionRunResult(const std::string& param)> runner;
};

namespace NativeCommands {
// The full built-in command catalog, built once on first use. UI thread only.
const std::vector<NativeCommand>& catalog();

// Dispatches `key` to its runner (unknown keys return a quiet Info). UI thread only.
AppActionRunResult run(const std::string& key, const std::string& param = {});
} // namespace NativeCommands

}} // namespace Slic3r::GUI
