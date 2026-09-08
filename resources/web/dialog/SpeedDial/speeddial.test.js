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

// The main phase is ONE pool: commands/plugins/settings are all actions, ranked by relevance
// (no group headers, no actions-vs-settings discrimination).
const pool = [
  { id: "c1", title: "Layer Height", source: "Quality", group: "Quality : Layers", input: "" },
  { id: "s1", title: "Go to layer (percent)", source: "OrcaSlicer", group: "Commands", input: "percent" },
  { id: "c2", title: "Top Surface Layers", source: "Quality", group: "Quality : Layers", input: "" }
];
assert.deepEqual(ctx.searchActions(pool, ""), pool, "an empty query returns the pool unchanged");
assert.equal(ctx.searchActions(pool, "zzz").length, 0, "a query with no match returns nothing");
// "layer" matches multiple; the exact-titled action ranks above the loosely-matching command.
assert.equal(ctx.searchActions(pool, "layer")[0].id, "c1",
  "a title-exact match ranks above a partial match");
assert.equal(ctx.searchActions(pool, "layer").length >= 2, true,
  "both a setting and a command match the same query in the same list");
assert.equal(ctx.searchActions(pool, "surface")[0].id, "c2",
  "a later-but-precise match still ranks by relevance, not by pool type");

// commandList (the main-phase list) delegates to the ranked search for a typed query and returns
// the mixed recents (no discrimination) for an empty query.
const mixed = [
  { id: "cmd", title: "Slice", source: "OrcaSlicer", group: "Commands", input: "" },
  { id: "set", title: "Sparse Infill Density", source: "Quality", group: "Quality", input: "" }
];
assert.equal(ctx.commandList(mixed, [], "sli")[0].id, "cmd",
  "a typed query keeps the relevance-ranked action list (best match first)");
assert.deepEqual(ctx.commandList(mixed, mixed.slice(0, 1), "").map(function (a) { return a.id; }), ["cmd"],
  "an empty query shows the mixed recents list verbatim");

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

// Fav quick-launch slots: digit 1..9 -> index 0..8, digit 0 -> index 9 (the 10th), else -1.
assert.equal(ctx.favIndexForDigit("1"), 0, "digit 1 maps to the 1st favourite slot");
assert.equal(ctx.favIndexForDigit("9"), 8, "digit 9 maps to the 9th favourite slot");
assert.equal(ctx.favIndexForDigit("0"), 9, "digit 0 maps to the 10th (last) favourite slot");
assert.equal(ctx.favIndexForDigit("x"), -1, "non-digit keys are not a slot");
assert.equal(ctx.favIndexForDigit(""), -1, "an empty key is not a slot");

// Badge label per 0-based index: 0..8 -> "1".."9", index 9 -> "0", out of range -> null.
assert.equal(ctx.favSlotForIndex(0), "1", "index 0 shows badge 1");
assert.equal(ctx.favSlotForIndex(8), "9", "index 8 shows badge 9");
assert.equal(ctx.favSlotForIndex(9), "0", "index 9 shows badge 0 (the 10th slot)");
assert.equal(ctx.favSlotForIndex(10), null, "index 10 is beyond the cap");
assert.equal(ctx.favSlotForIndex(-1), null, "negative index is not a slot");
assert.equal(ctx.K_FAV_LIMIT, 10, "the slot count matches the quick-launch cap");

// nextSel: arrow-nav wrapping. Down wraps at the list bottom to the first row; Up wraps at the
// list top to the last row ONLY when there's no fav bar above (else it goes to the fav bar).
assert.deepEqual(ctx.nextSel({ zone: "list", i: 2 }, "ArrowDown", 3, 0), { zone: "list", i: 0 },
  "ArrowDown at the last row wraps to the first row");
assert.deepEqual(ctx.nextSel({ zone: "list", i: 1 }, "ArrowDown", 3, 0), { zone: "list", i: 2 },
  "ArrowDown in the middle advances by one");
assert.deepEqual(ctx.nextSel({ zone: "list", i: 0 }, "ArrowUp", 3, 0), { zone: "list", i: 2 },
  "ArrowUp at the first row with no fav bar wraps to the last row");
assert.deepEqual(ctx.nextSel({ zone: "list", i: 0 }, "ArrowUp", 3, 2), { zone: "fav", i: 0 },
  "ArrowUp at the first row with a fav bar goes to the fav bar (unchanged)");
assert.deepEqual(ctx.nextSel({ zone: "list", i: 2 }, "ArrowUp", 3, 0), { zone: "list", i: 1 },
  "ArrowUp in the middle moves up by one");
assert.deepEqual(ctx.nextSel({ zone: "fav", i: 1 }, "ArrowDown", 3, 2), { zone: "list", i: 0 },
  "ArrowDown from the fav bar lands on the first list row");
assert.deepEqual(ctx.nextSel({ zone: "list", i: 0 }, "ArrowDown", 1, 0), { zone: "list", i: 0 },
  "a single-row list never wraps off the end");
assert.deepEqual(ctx.nextSel({ zone: "list", i: 0 }, "ArrowUp", 1, 0), { zone: "list", i: 0 },
  "ArrowUp on the only row stays put");

// Windowed list reveal: how many rows must be materialized to cover `fromIndex` plus `size` more,
// clamped to the total. Drives the "render the next window on scroll / arrow-nav" append.
assert.equal(ctx.revealTarget(100, 0, 100), 100, "covers the whole list when the window reaches the end");
assert.equal(ctx.revealTarget(100, 60, 100), 100, "clamps to the total at the tail");
assert.equal(ctx.revealTarget(30, 5, 100), 30, "a short list is fully covered");
assert.equal(ctx.revealTarget(100, 5, 50), 55, "reveals exactly fromIndex + size");
assert.equal(ctx.revealTarget(100, -5, 50), 50, "negative start is clamped to the first row");
assert.equal(ctx.revealTarget(200, 50, 100), 150, "a scroll viewpoint reveals a window past the current rows");
assert.equal(ctx.revealTarget(10, 0, 50), 10, "a list shorter than one window stays fully materialized");

// ---- inline setting editor helpers (settingControlRows / Value / CollectedValue) ----
const scalarBoolDesc = {
  editable: true, control: "toggle", cardinality: "scalar", value: true,
  min: undefined, max: undefined, is_int: false, unit: ""
};
const scalarRows = ctx.settingControlRows(scalarBoolDesc);
assert.equal(scalarRows.length, 1, "a scalar setting yields exactly one control row");
assert.equal(scalarRows[0].kind, "toggle", "the control kind is carried through");
assert.equal(scalarRows[0].index, 0, "the single row is indexed 0");
assert.deepEqual(ctx.settingControlRows({ editable: false }), [], "a non-editable setting yields no control rows");

const vectorDesc = {
  editable: true, control: "number", cardinality: "vector", values: [0.2, 0.4],
  index_labels: ["1", "2"], min: 0, max: 1, is_int: false, unit: "mm"
};
const vectorRows = ctx.settingControlRows(vectorDesc);
assert.equal(vectorRows.length, 2, "a vector setting yields one row per value");
assert.deepEqual(vectorRows.map(function (r) { return r.value; }), [0.2, 0.4], "each row carries its current value");
assert.deepEqual(vectorRows.map(function (r) { return r.label; }), ["1", "2"], "vector rows use the per-index labels");
assert.equal(vectorRows[0].unit, "mm", "the unit is carried to each row");

// settingControlValue reads a control element (checked/value) as the shipped value.
assert.equal(ctx.settingControlValue({ kind: "toggle" }, { checked: true }), true, "a toggle submits its checked state");
assert.equal(ctx.settingControlValue({ kind: "toggle" }, { checked: false }), false, "an off toggle submits false");
assert.equal(ctx.settingControlValue({ kind: "number", is_int: false, min: 0, max: 1 }, { value: "0.5" }), 0.5, "a float number parses");
assert.equal(ctx.settingControlValue({ kind: "number", is_int: true, min: 0, max: 10 }, { value: "5" }), 5, "an int number parses");
assert.equal(ctx.settingControlValue({ kind: "number", is_int: false, min: 0, max: 1 }, { value: "2" }), undefined, "an out-of-range number is rejected");
assert.equal(ctx.settingControlValue({ kind: "number", is_int: false, min: 0, max: 1 }, { value: "" }), undefined, "an empty number is rejected");
assert.equal(ctx.settingControlValue({ kind: "dropdown" }, { dataset: { value: "3" } }), 3, "an enum dropdown submits its stored int value");
assert.equal(ctx.settingControlValue({ kind: "dropdown" }, { dataset: { value: "nope" } }), undefined, "a non-numeric dropdown value is rejected");
assert.equal(ctx.settingControlValue({ kind: "dropdown" }, {}), undefined, "a dropdown with no value is rejected");
// dropdownLabel maps the current int value to its human label, falling back to the value.
const seamOptions = [
  { value: 0, key: "nearest", label: "Nearest" },
  { value: 1, key: "aligned", label: "Aligned" },
  { value: 2, key: "random", label: "Random" }
];
assert.equal(ctx.dropdownLabel(seamOptions, 1), "Aligned", "the dropdown label for a known value is its label");
assert.equal(ctx.dropdownLabel(seamOptions, 9), "9", "an unknown value falls back to the raw value");
assert.equal(ctx.dropdownLabel([], 3), "3", "empty options fall back to the raw value");
assert.equal(ctx.dropdownLabel(
  [{ value: 1, key: "aligned", label: "" }], 1), "aligned", "a blank label falls back to the key");
// dropdownIcon maps the current int value to its pattern pictogram, empty when there is none.
const patternOptions = [
  { value: 0, key: "rectilinear", label: "Rectilinear" },
  { value: 3, key: "gyroid", label: "Gyroid", icon: "data:image/svg+xml;base64,AAA" },
  { value: 5, key: "grid", label: "Grid", icon: "data:image/svg+xml;base64,BBB" }
];
assert.equal(ctx.dropdownIcon(patternOptions, 3), "data:image/svg+xml;base64,AAA", "a known value returns its icon");
assert.equal(ctx.dropdownIcon(patternOptions, 0), "", "a value with no icon returns empty");
assert.equal(ctx.dropdownIcon(patternOptions, 9), "", "an unknown value returns empty");
assert.equal(ctx.dropdownIcon([], 3), "", "empty options return empty");
assert.equal(ctx.settingControlValue({ kind: "text" }, { value: "hello" }), "hello", "text submits as a string");
// percent (coFloatOrPercent / coFloatsOrPercents): submits the raw typed string; empty is invalid.
assert.equal(ctx.settingControlValue({ kind: "percent" }, { value: "10%" }), "10%", "a percent value submits as its string");
assert.equal(ctx.settingControlValue({ kind: "percent" }, { value: "0.5" }), "0.5", "an mm value submits as a plain string");
assert.equal(ctx.settingControlValue({ kind: "percent" }, { value: "  " }), undefined, "a blank percent value is rejected");
assert.equal(ctx.settingControlValue({ kind: "percent" }, {}), undefined, "a percent field with no value is rejected");
// A percent scalar-to-scalar payload carries the raw string through unchanged.
assert.equal(ctx.settingCollectedValue(
  { editable: true, control: "percent", cardinality: "scalar", value: "10%" },
  [{ kind: "percent", value: "10%" }], [{ value: "10%" }]), "10%", "a percent scalar assembles its string");

// settingCollectedValue assembles the payload (scalar vs vector) for the set_setting message.
assert.equal(ctx.settingCollectedValue({ editable: false }, [], []), undefined, "a non-editable setting yields no payload");
assert.equal(ctx.settingCollectedValue(scalarBoolDesc, scalarRows, [{ checked: true }]), true, "a scalar assembles a single value");
assert.deepEqual(ctx.settingCollectedValue(vectorDesc, vectorRows, [{ value: "0.3" }, { value: "0.7" }]), [0.3, 0.7], "a vector assembles an array");
assert.equal(ctx.settingCollectedValue(vectorDesc, vectorRows, [{ value: "0.3" }, { value: "2" }]), undefined, "an invalid vector element aborts the whole payload");

console.log("ok");
