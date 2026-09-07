// Regression tests for the DOM-free Speed Dial helpers.
// Run: node resources/web/dialog/SpeedDial/speeddial.test.js
const assert = require("assert");
const fs = require("fs");
const vm = require("vm");

const ctx = {};
ctx.window = ctx;
vm.createContext(ctx);
vm.runInContext(fs.readFileSync(__dirname + "/../../js/fuzzy-search.js", "utf8"), ctx);
vm.runInContext(fs.readFileSync(__dirname + "/speeddial.js", "utf8"), ctx);

assert.equal(typeof ctx.parseId, "undefined", "opaque action ids must never be parsed");

const duplicateActions = [
  { id: "0123456789abcdef", title: "Repair", source: "Mesh Tools" },
  { id: "fedcba9876543210", title: "Repair", source: "Mesh Tools" }
];
assert.equal(
  ctx.actionLabel(duplicateActions[0], duplicateActions),
  "Repair from Mesh Tools (0123456789abcdef)",
  "duplicate labels should use the opaque id without interpreting its contents"
);

assert.equal(ctx.shouldRenderActionList(""), false, "an empty search keeps recent/empty list");
assert.equal(ctx.shouldRenderActionList("  "), false, "whitespace-only search keeps recent/empty list");
assert.equal(ctx.shouldRenderActionList("r"), true, "typing starts rendering matching actions");

// commandList: an empty query shows recents; a typed query filters all actions.
assert.deepEqual(ctx.commandList(duplicateActions, [], ""), [],
  "empty query + no recents shows nothing");
assert.deepEqual(ctx.commandList(duplicateActions, [duplicateActions[0]], ""),
  [duplicateActions[0]],
  "empty query shows the recent list");
assert.deepEqual(ctx.commandList(duplicateActions, [], "rep"), duplicateActions,
  "a typed query filters actions (both identical titles match) instead of showing recents");

// filterTabs (tab phase): an empty query keeps the whole list; a typed query filters by title/id.
const tabOptions = [
  { id: "home", title: "Home" },
  { id: "prepare", title: "Prepare" },
  { id: "monitor", title: "Device" },
  { id: "project", title: "Project" }
];
assert.deepEqual(ctx.filterTabs(tabOptions, ""), tabOptions,
  "empty query keeps the whole tab list");
assert.equal(ctx.filterTabs(tabOptions, "prep").length, 1,
  "a typed query filters tabs by title");
assert.equal(ctx.filterTabs(tabOptions, "Device").length, 1,
  "a typed query matches a tab title");
assert.deepEqual(ctx.filterTabs(tabOptions, "zzz"), [],
  "a typed query with no match returns an empty list");

// tabTitle: pages added with an empty title (e.g. MainFrame's Home tab) fall back to the id.
assert.equal(ctx.tabTitle({ id: "home", title: "" }), "Home",
  "an empty title falls back to the title-cased id");
assert.equal(ctx.tabTitle({ id: "home" }), "Home",
  "a missing title falls back to the title-cased id");
assert.equal(ctx.tabTitle({ id: "prepare", title: "Prepare" }), "Prepare",
  "a populated title is kept as-is");
assert.equal(ctx.tabTitle({ id: "prepare", title: " Prepare" }), "Prepare",
  "a leading space from the Notebook button label is trimmed so the icon letter shows");
assert.equal(ctx.filterTabs([{ id: "home", title: "" }], "home").length, 1,
  "an untitled tab still matches a typed query via the id/title fallback");
assert.equal(ctx.filterTabs([{ id: "prepare", title: " Prepare" }], "prepare").length, 1,
  "a leading-space tab title still matches a typed query");

// settingCode: a setting tile uses the leaf option-name initial (label's last " : "-segment),
// escalated by category, then a stable ordinal - so settings aren't all a generic "S".
const settings = [
  { opt_key: "layer_height", label: "Quality : Layer Height", category: "Quality", type: 0 },
  { opt_key: "smooth", label: "Quality : Smooth", category: "Quality", type: 0 }
];
assert.equal(ctx.leafLabel({ label: "Quality : Layer Height", opt_key: "x" }), "Layer Height",
  "leafLabel takes the last segment of the label");
assert.equal(ctx.settingCode(settings[0], settings), "L",
  "a unique leaf initial yields a single letter");
// renderSettingRow iterates settingsResults, so the coded item is always a member of the list.
const infillQ = { opt_key: "a", label: "Quality : Infill", category: "Quality", type: 0 };
const infillS = { opt_key: "b", label: "Supports : Infill", category: "Supports", type: 0 };
const sameLeaf = [infillQ, infillS];
assert.equal(ctx.settingCode(infillQ, sameLeaf), "QI",
  "a colliding leaf initial escalates to category + initial");
assert.equal(ctx.settingCode(infillS, sameLeaf), "SI",
  "a different category disambiguates the same leaf initial");
const dupQ = [
  { opt_key: "a", label: "Quality : Infill", category: "Quality", type: 0 },
  { opt_key: "b", label: "Quality : Infill", category: "Quality", type: 0 }
];
assert.equal(ctx.settingCode(dupQ[0], dupQ), "QI1",
  "both title and category collide -> stable ordinal by opt_key");
assert.equal(ctx.settingCode(dupQ[1], dupQ), "QI2",
  "the ordinal advances by opt_key order");

// selectedActionId: resolves the active list (recents for an empty query, filtered list otherwise).
assert.equal(
  ctx.selectedActionId({ zone: "list", i: 0 }, ctx.commandList(duplicateActions, [], ""), [], ""),
  null,
  "Enter with an empty query and no recents must not resolve to an action the list never showed"
);
assert.equal(
  ctx.selectedActionId({ zone: "list", i: 0 }, ctx.commandList(duplicateActions, [], "rep"), [], "rep"),
  "0123456789abcdef",
  "a typed query resolves the list selection"
);
assert.equal(
  ctx.selectedActionId({ zone: "list", i: 0 }, ctx.commandList(duplicateActions, [duplicateActions[0]], ""), [], ""),
  "0123456789abcdef",
  "Enter with an empty query resolves the recent entry"
);
assert.equal(
  ctx.selectedActionId({ zone: "fav", i: 0 }, duplicateActions, ["fedcba9876543210"], ""),
  "fedcba9876543210",
  "favourites stay runnable with an empty query - the fav bar is always visible"
);

console.log("ok");
