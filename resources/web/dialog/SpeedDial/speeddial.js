// Speed Dial launcher page. Static-safe module: no DOM access at load time so a
// node vm can exercise the pure helpers (filterActions / actionLabel / nextSel / commandList).

// ---- state (populated by the C++ bridge via window.HandleStudio) ----
var ACTIONS = [];        // [{id,title,source,group,input,shortcut}], already frecency-sorted by C++
var FAVS = [];           // [id...]
var RECENTS = [];        // [{id,title,source,group,input,shortcut}] - last-N launched
var query = "";
var sel = { zone: "list", i: 0 };   // zone: 'list' | 'fav'
var lastResizeHeight = 0;
var matchIndex = {};

// ---- windowed list render ----------------------------------------------------
// The command list is rendered in windows (append-on-scroll) so a huge settings pool doesn't build
// the whole DOM per keystroke. Rows are exactly ROW_H tall (matches .row min-height 44px; see --row-h,
// which is documented to stay in sync). `renderEnd` is the exclusive count of rows currently in the DOM;
// a bottom spacer fills the rest of the list so the scrollbar reflects the full match count and
// "scroll past the last rendered row" reveals the next window.
var K_ROWS = 50;
var ROW_H  = 44;
var renderEnd = 0;
var builtKey  = "";   // phase|query|listLen - when it changes, rows are rebuilt from the first window
var spacerEl  = null; // the trailing height spacer, always the last child of listEl

// search-cache: the normalized (folded+lowercased) needle for the current query pass.
var searchNeedle = "";

// Palette phase: 'commands' (one unified search over actions/commands/settings, recents on empty
// query), 'percent' ("Go to layer" second phase: enter a 0-100 percentage), 'tab' ("Go to tab..."
// second phase: pick a notebook tab).
var phase = "commands";
var tabOptions = [];       // [{id,title}] - notebook pages, fetched on entering the tab phase

// why: fuzzy matcher (FoldChar/Norm/FuzzyRanges) lives in shared ../../js/fuzzy-search.js, loaded before
//      this script - it is shared with the Plugins dialog. Speed dial search is always case-insensitive.

// element handles, assigned in OnInit (kept null so load-time touches no DOM)
var qEl = null, listEl = null, favEl = null, clearEl = null, eyeEl = null, countEl = null, headEl = null;

// ---- inline setting editor state ---------------------------------------------
// The "setting" phase (opened by activating a setting action) replaces the list with an editor card
// for one option. phase transitions: commands -> setting -> (apply / open-in-sidebar) -> closed, or
// Esc back to commands. settingDesc is the C++ descriptor; settingRows are the per-index control
// descriptors (1 row for scalars, one per index for vectors); settingFieldEls hold the live controls.
var settingId = "";        // the setting action id being edited
var settingDesc = null;    // {id,opt_key,type,title,breadcrumb,category,unit,tooltip,editable,control,cardinality,value|values,index_labels,enum_options,min,max,is_int}
var settingRows = [];      // [{index,kind,value,label,enum_options,min,max,unit,is_int}]
var settingFieldEls = [];  // [Element...] parallel to settingRows
var settingPreviewIcon = null; // <img> beside the editor title, updated live on dropdown pick
var openDropDownEl = null; // the custom dropdown toggle button whose option list is expanded

// ---- pure helpers (no DOM; unit-tested) -------------------------------------
// Pre-normalized haystacks, cached on the action object. The fold is length-preserving (1:1 per
// char) so the ranges FuzzyRangesNorm returns slice the ORIGINAL title/source text correctly. The
// action objects arrive from C++ and are stable for the dialog's lifetime, so we compute these once.
function titleNorm(a) {
    if (a._tn === undefined)
        a._tn = NormText(a.title, false);
    return a._tn;
}
function otherNorm(a) {
    if (a._on === undefined)
        a._on = NormText((a.source || "") + " " + (a.group || ""), false);
    return a._on;
}

// Relevance score for a single field vs the current query needle, or -1 when there's no match.
// Higher is better: an earlier start and a more contiguous (fewer gaps) match beat a scattered late one.
function matchScoreNorm(haystackNorm) {
    if (!searchNeedle) return -1;
    var r = FuzzyRangesNorm(haystackNorm || "", searchNeedle);
    if (!r) return -1;
    var gaps = 0;
    for (var i = 1; i < r.length; i++)
        gaps += r[i][0] - r[i - 1][1];
    return 1000 - r[0][0] * 10 - gaps * 10;
}

// Per-action score: title matches rank above a source/group-only match of equal quality.
function actionSearchScore(a) {
    var title = matchScoreNorm(titleNorm(a));
    var other = matchScoreNorm(otherNorm(a));
    if (title < 0 && other < 0) return -1;
    return Math.max(title < 0 ? -1e9 : title + 10000, other < 0 ? -1e9 : other);
}

// The unified main-phase search: every action (command/plugin/setting) matching the query, ranked
// by relevance (not by action type). Sets matchIndex so rows highlight their match ranges. The query
// is normalized ONCE per pass - FuzzyRangesNorm then runs against each action's pre-normalized
// haystack, so per-keystroke cost is a cheap scan (no per-char normalize/regex).
function searchActions(actions, query) {
    var q = (query || "").trim();
    var list = actions || [];
    matchIndex = {};
    if (!q) { searchNeedle = ""; return list.slice(0); }
    searchNeedle = NormText(q, false);

    var scored = [];
    for (var i = 0; i < list.length; i++) {
        var a = list[i];
        var s = actionSearchScore(a);
        if (s < 0) continue;
        var titleMatch = FuzzyRangesNorm(titleNorm(a), searchNeedle);
        matchIndex[a.id] = { title: titleMatch, source: FuzzyRangesNorm(otherNorm(a), searchNeedle), useTitle: !!titleMatch };
        scored.push({ a: a, s: s });
    }
    scored.sort(function (x, y) {
        if (x.s !== y.s) return y.s - x.s;
        if (x.a.title !== y.a.title) return x.a.title < y.a.title ? -1 : 1;
        return x.a.id < y.a.id ? -1 : x.a.id > y.a.id ? 1 : 0;
    });
    return scored.map(function (e) { return e.a; });
}

// Pure: how many rows must be materialized to cover the given starting index plus `size` more.
// Clamped to the total; used to decide "render the next window" on scroll / arrow-nav.
function revealTarget(total, fromIndex, size) {
    return Math.min(total, Math.max(0, fromIndex) + size);
}

// buildKey: the command-list signature that decides whether rows must be rebuilt (new search / phase)
// or just have their selection refreshed in place (arrow-nav / click). Cheap to compute.
function buildKey() { return phase + "|" + (query || "").trim(); }

function visibleFavourites(favourites, actions) {
    // why: a fav whose id has no live action (plugin unloaded/disabled) renders a dead
    //      monogram tile whose click run()s to a silent no-op; drop it from the quick-bar.
    var seen = {};
    (actions || []).forEach(function (a) { seen[a.id] = true; });
    return (favourites || []).filter(function (id, i, arr) {
        return seen[id] && arr.indexOf(id) === i;
    });
}

// Numbered quick-launch slots (mirrors ActionRegistry::kFavLimit). Pure so the node-vm test
// can exercise the digit<->slot mapping without a DOM.
var K_FAV_LIMIT = 10;

// Badge label for a 0-based fav-bar index: 0..8 -> "1".."9", index 9 (the 10th) -> "0".
function favSlotForIndex(i) {
    if (i < 0 || i >= K_FAV_LIMIT) return null;
    return i < 9 ? String(i + 1) : "0";
}

// Digit key -> 0-based fav-bar index (1..9 -> 0..8, 0 -> 9); -1 for anything else.
function favIndexForDigit(d) {
    var c = String(d || "").charCodeAt(0);
    if (c >= 49 && c <= 57) return c - 49;
    if (c === 48) return 9;
    return -1;
}

function resultCountText(total, shown, query) {
    return (query || "").trim() ? "Showing " + shown + " of " + total + " actions" : total + " actions";
}

// Display label for a notebook tab. Notebook's ButtonsListCtrl labels every non-empty page as
// " <text>" (a leading space), so trim it; pages added with an empty title (Home, MainFrame adds
// TAB_ID_HOME with "") fall back to the title-cased id ("home" -> "Home").
function tabTitle(t) {
    var title = (t && t.title) ? String(t.title).trim() : "";
    return title || prettySource((t && t.id) || "");
}

// Filter the tab list by a fuzzy title/id match. Pure so the node-vm test can exercise it.
function filterTabs(tabs, query) {
    var q = (query || "").trim();
    if (!q)
        return (tabs || []).slice(0);
    return (tabs || []).filter(function (t) {
        return FuzzyRanges(tabTitle(t), q, false) || FuzzyRanges(t.id, q, false);
    });
}

function shouldRenderActionList(query) {
    return !!((query || "").trim());
}

// Label for a closed-enum entry, looked up from its enum_options by value; falls back to the value.
// Pure so the node-vm test can exercise the dropdown label mapping. `value` is the current int value.
function dropdownLabel(options, value) {
    var want = String(value == null ? "" : value);
    for (var i = 0; i < (options || []).length; i++)
        if (String(options[i].value) === want)
            return options[i].label != null && options[i].label !== "" ? String(options[i].label) : String(options[i].key != null ? options[i].key : options[i].value);
    return want;
}

// Pure: the data:URI pictogram for a dropdown's selected value, or "" when none of the options has
// one (most settings have no pattern icon). Mirrors dropdownLabel so tests can drive it without DOM.
function dropdownIcon(options, value) {
    var want = String(value == null ? "" : value);
    for (var i = 0; i < (options || []).length; i++)
        if (String(options[i].value) === want && options[i].icon)
            return options[i].icon;
    return "";
}

// Put an action's pattern pictogram into a tile (search row or favourites tile) when it has one,
// otherwise fall back to the monogram. Toggles the has-icon class so CSS neutralises the hue.
function fillTile(tile, a) {
    tile.classList.remove("has-icon");
    if (a && a.icon) {
        tile.textContent = "";
        var img = document.createElement("img");
        img.className = "tile-icon";
        img.src = a.icon;
        img.alt = "";
        tile.appendChild(img);
        tile.classList.add("has-icon");
    } else {
        tile.textContent = a ? tileCode(a, ACTIONS) : "";
    }
}

// Per-index control descriptors for the inline setting editor, derived from the C++ descriptor.
// Pure so the node-vm test can exercise the scalar/vector + control mapping without a DOM.
// Values are the current config value(s); vector options get one row per index, each labelled.
function settingControlRows(desc) {
    if (!desc || !desc.editable) return [];
    var rows = [];
    var values = desc.cardinality === "vector" ? (desc.values || []) : [desc.value];
    var labels = desc.cardinality === "vector" ? (desc.index_labels || []) : [];
    for (var i = 0; i < values.length; i++) {
        rows.push({
            index: i,
            kind: desc.control,
            value: values[i],
            label: labels[i] != null ? String(labels[i]) : (desc.cardinality === "vector" ? String(i + 1) : null),
            enum_options: desc.enum_options || [],
            min: typeof desc.min === "number" ? desc.min : null,
            max: typeof desc.max === "number" ? desc.max : null,
            unit: desc.unit || "",
            is_int: !!desc.is_int
        });
    }
    return rows;
}

// Pure: read the value a control would submit back for a setting. `el` is a DOM element (never
// passed in tests). Returns undefined for an unusable value (empty/invalid number, out of range),
// boolean for toggles, number for numeric, string otherwise.
function settingControlValue(row, el) {
    if (!row || !el) return undefined;
    switch (row.kind) {
    case "toggle": return !!el.checked;
    case "number": {
        var raw = String(el.value || "").trim();
        if (raw === "") return undefined;
        var n = row.is_int ? parseInt(raw, 10) : parseFloat(raw);
        if (!isFinite(n)) return undefined;
        if (row.min != null && n < row.min) return undefined;
        if (row.max != null && n > row.max) return undefined;
        return n;
    }
    case "dropdown": {
        // value is stored on the toggle button's dataset (set when an option is picked).
        var v = parseInt(el.dataset ? el.dataset.value : "", 10);
        return isFinite(v) ? v : undefined;
    }
    case "combo": {
        // Open enum: free text field (never a select), so read it as the seeded integer value.
        var v = parseInt(el.value, 10);
        return isFinite(v) ? v : undefined;
    }
    case "color":
    case "text":
    case "percent": {
        // percent submission is a string ("10%", "0.5"); C++ parses + clamps it. Empty is invalid.
        var raw = String(el.value || "").trim();
        return raw === "" ? undefined : raw;
    }
    default: return undefined;
    }
}

// Pure: assemble the value payload for a setting from its edited control rows. Returns the scalar
// for scalar settings, an array for vector settings, or undefined when any control is invalid.
function settingCollectedValue(desc, rows, values) {
    if (!desc || !desc.editable) return undefined;
    if (desc.cardinality === "vector") {
        var out = [];
        for (var i = 0; i < rows.length; i++) {
            var v = settingControlValue(rows[i], values[i]);
            if (v === undefined) return undefined;
            out.push(v);
        }
        return out;
    }
    return settingControlValue(rows[0], values[0]);
}

// The active list for the main phase. A typed query ranks every action (commands/plugins/settings)
// by relevance; an empty query shows the recent list (recents are a mixed bag - no discrimination).
function commandList(actions, recents, query) {
    if (shouldRenderActionList(query))
        return searchActions(actions || [], query);
    return (recents || []).slice(0);
}

// Resolve the selection cursor {zone,i} to the action id it points at: fav zone indexes the
// visible favourites, list zone the active commands list. `actions` must be the already-resolved
// list (recents for an empty query, the filtered list otherwise) - pure so runSelected() shares
// one lookup and the node-vm test can call it directly.
function selectedActionId(sel, actions, favIds, query) {
    if (sel.zone === "fav")
        return favIds[sel.i];
    if (!actions || !actions.length)
        return null;
    var a = actions[sel.i];
    return a && a.id;
}

function foldLabel(s) { return String(s || "").toLowerCase().replace(/[^a-z0-9]+/g, ""); }

// Title-case a source for display: "GCODE OPTIMIZER"/"iRoNiNg pRo" -> "Gcode Optimizer"/"Ironing Pro".
function prettySource(source) {
    return String(source || "").toLowerCase().replace(/\b\w/g, function (c) { return c.toUpperCase(); });
}

// Accessible label "Title from Pretty Source", disambiguated with the opaque action id when another
// action shares the same title+source (case/separator-insensitive) - so two rows never read out identically.
function actionLabel(action, actions) {
    var label = action.title + " from " + prettySource(action.source || action.group || "");
    if (actions && actions.length) {
        var mine = foldLabel(action.title) + "|" + foldLabel(action.source || action.group || "");
        var clash = actions.some(function (o) {
            return o.id !== action.id && foldLabel(o.title) + "|" + foldLabel(o.source || o.group || "") === mine;
        });
        if (clash)
            label += " (" + action.id + ")";
    }
    return label;
}

// Monogram code for a tile: title initial, escalated on collision by PREPENDING the source
// initial (pi+ti, e.g. "GC"), then a 1-based ordinal - so same-titled items stay distinct.
// why: ordinal is assigned by id, not by list order - list order is frecency-sorted and
// reshuffles as usage changes, which would otherwise flip who's "1" and who's "2" across runs.
function monogramFor(item, list, titleOf, sourceOf, idOf) {
    var items = list || [];
    var title = titleOf(item) || " ";
    var ti = title.charAt(0).toUpperCase();
    var sameTitle = items.filter(function (o) { return (titleOf(o) || " ").charAt(0).toUpperCase() === ti; });
    if (sameTitle.length <= 1)
        return ti;
    var source = sourceOf(item) || " ";
    var pi = source.charAt(0).toUpperCase();
    var sameSource = sameTitle.filter(function (o) { return (sourceOf(o) || " ").charAt(0).toUpperCase() === pi; });
    if (sameSource.length <= 1)
        return pi + ti;
    sameSource.sort(function (a, b) { return idOf(a) < idOf(b) ? -1 : idOf(a) > idOf(b) ? 1 : 0; });
    for (var i = 0; i < sameSource.length; i++)
        if (sameSource[i] === item || idOf(sameSource[i]) === idOf(item))
            return pi + ti + (i + 1);
    return pi + ti;
}

// Action tile code - see monogramFor for the escalation ladder. Settings are actions now, so
// they share this ladder (title initial, then source, then a stable ordinal).
function tileCode(action, actions) {
    return monogramFor(action, actions,
        function (o) { return o.title; },
        function (o) { return o.source; },
        function (o) { return o.id; });
}

function syncClearButton() {
    if (clearEl)
        clearEl.hidden = !query;
}

function stateFromPayload(payload) {
    return {
        actions: payload.actions || [],
        favourites: payload.favourites || [],
        recent: payload.recent || [],
        query: "",
        sel: { zone: "list", i: 0 },
        lastResizeHeight: 0,
        phase: "commands",
        tabOptions: []
    };
}

function resetScrollPositions(list, doc) {
    if (list)
        list.scrollTop = 0;
    if (doc && doc.scrollingElement)
        doc.scrollingElement.scrollTop = 0;
    if (doc && doc.documentElement)
        doc.documentElement.scrollTop = 0;
    if (doc && doc.body)
        doc.body.scrollTop = 0;
}

// nextSel: pure arrow-nav transition. Down fav->list0; Down list wraps at the bottom (last -> first).
// Up list wraps at the top (first -> last) only when there's no fav bar above; with a fav bar, Up at
// the list top goes to fav0 (unchanged). Left/Right clamp within fav. Returns a fresh {zone,i}.
function nextSel(sel, key, listLen, favLen) {
    var zone = sel.zone, i = sel.i;
    var last = Math.max(0, listLen - 1);
    if (key === "ArrowDown") {
        if (zone === "fav") return { zone: "list", i: 0 };
        return { zone: "list", i: i >= last ? 0 : i + 1 };
    }
    if (key === "ArrowUp") {
        if (zone === "list") {
            if (i <= 0) return favLen ? { zone: "fav", i: 0 } : { zone: "list", i: last };
            return { zone: "list", i: i - 1 };
        }
        return { zone: zone, i: i };
    }
    if (key === "ArrowLeft" && zone === "fav") return { zone: "fav", i: Math.max(0, i - 1) };
    if (key === "ArrowRight" && zone === "fav") return { zone: "fav", i: Math.min(favLen - 1, i + 1) };
    return { zone: zone, i: i };
}

// ---- bridge ------------------------------------------------------------------
function SendMessage(msg) {
    if (typeof SendWXMessage !== "function")
        return;
    if (typeof msg === "string") msg = { command: msg };
    if (msg.sequence_id === undefined) msg.sequence_id = Date.now();
    SendWXMessage(JSON.stringify(msg));
}

// C++ pushes payloads here.
window.HandleStudio = function (payload) {
    if (!payload) return;
    if (typeof payload === "string") { try { payload = JSON.parse(payload); } catch (e) { return; } }
    if (payload.command === "list_actions") {
        var next = stateFromPayload(payload);
        ACTIONS = next.actions;
        FAVS = next.favourites;
        RECENTS = next.recent;
        query = next.query;
        sel = next.sel;
        lastResizeHeight = next.lastResizeHeight;
        phase = next.phase;
        tabOptions = next.tabOptions;
        // Reset any half-open setting editor (the dialog was closed/reopened), restoring the search.
        settingId = ""; settingDesc = null; settingRows = []; settingFieldEls = [];
        settingPreviewIcon = null;
        openDropDownEl = null;
        // why: builtKey caches phase|query so renderCommandsList can skip a rebuild on arrow-nav. It
        // survives an apply-then-reopen (which never goes through exitPhase), so without a reset the
        // leftover editor card would be mistaken for the empty-query commands list and never rebuilt.
        builtKey = "";
        if (headEl) headEl.hidden = false;
        if (qEl) {
            qEl.value = "";
            qEl.placeholder = "Search " + ACTIONS.length + " actions";
            syncClearButton();
        }
        render({ resize: true, resetScroll: true });
        focusInput();
    } else if (payload.command === "setting_descriptor") {
        // Inline editor loaded: render the card. Guard against a stale response for a different id.
        if (payload.descriptor && payload.descriptor.id === settingId)
            settingDesc = payload.descriptor;
        render({ resize: true });
        // why: keyboard focus must land on the field after the card is built, not stay on the hidden
        // search input. fire on a timeout so the element is attached and its content selectable.
        focusSettingEditor();
    } else if (payload.command === "apply_failed") {
        flashHint("Couldn't apply that value");
    } else if (payload.command === "tab_results") {
        tabOptions = payload.tabs || [];
        if (sel.zone === "list")
            sel.i = Math.max(0, Math.min(sel.i, tabOptions.length - 1));
        render({ resize: true });
    } else if (payload.command === "favourite_full") {
        // Favourites are at the quick-launch cap - undo the optimistic pin and flash a hint.
        var fid = payload.id;
        if (fid && FAVS.indexOf(fid) !== -1) FAVS.splice(FAVS.indexOf(fid), 1);
        render({ resize: true });
        flashHint("Favourites are full (" + (payload.limit || K_FAV_LIMIT) + " max)");
    }
};

// ---- DOM helpers -------------------------------------------------------------
function $(id) { return document.getElementById(id); }

function byId(id) {
    for (var i = 0; i < ACTIONS.length; i++) if (ACTIONS[i].id === id) return ACTIONS[i];
    return null;
}

function findActionByInput(input) {
    for (var i = 0; i < ACTIONS.length; i++) if (ACTIONS[i].input === input) return ACTIONS[i];
    return null;
}

function currentVisibleFavs() { return visibleFavourites(FAVS, ACTIONS); }

// Active list for the current phase (drives list rendering + arrow nav).
function currentList() {
    if (phase === "tab") return filterTabs(tabOptions, query);
    if (phase === "commands") return commandList(ACTIONS, RECENTS, query);
    return []; // percent - the input itself is the only field
}

function hue(id) {
    var h = 0;
    for (var i = 0; i < id.length; i++)
        h = (h * 31 + id.charCodeAt(i)) >>> 0;
    return h % 360;
}

// Build a <div class=className> with the search-match ranges wrapped in <mark>. Used for both the
// title and the source eyebrow. Pure (only touches the document factory), so the node-vm test never
// calls it and load-time stays DOM-free.
function markedText(className, text, match) {
    var node = document.createElement("div");
    node.className = className; node.title = text;
    if (!match || !match.length) {
        node.textContent = text;
        return node;
    }
    var last = 0;
    for (var i = 0; i < match.length; i++) {
        var range = match[i];
        if (range[0] > last)
            node.appendChild(document.createTextNode(text.slice(last, range[0])));
        var m = document.createElement("mark");
        m.textContent = text.slice(range[0], range[1]);
        node.appendChild(m);
        last = range[1];
    }
    if (last < text.length)
        node.appendChild(document.createTextNode(text.slice(last)));
    return node;
}

function starSvg(on) {
    return '<svg width="15" height="15" viewBox="0 0 24 24" fill="' + (on ? "currentColor" : "none") +
        '" stroke="currentColor" stroke-width="1.7" stroke-linejoin="round">' +
        '<path d="M12 3.5l2.6 5.3 5.9.9-4.3 4.1 1 5.8L12 17.9 6.8 20.6l1-5.8L3.5 9.7l5.9-.9z"/></svg>';
}

// ---- render ------------------------------------------------------------------
function renderFav() {
    // Only the commands phase shows the pinned quick-bar.
    if (phase !== "commands") {
        if (favEl) { favEl.innerHTML = ""; favEl.hidden = true; }
        if (eyeEl) eyeEl.hidden = true;
        return;
    }
    favEl.innerHTML = "";
    var favs = currentVisibleFavs();
    favEl.hidden = favs.length === 0;
    if (!favs.length && sel.zone === "fav")
        sel = { zone: "list", i: 0 };
    else if (sel.zone === "fav")
        sel.i = Math.max(0, Math.min(sel.i, favs.length - 1));
    updateFavEyebrow(favs);
    favs.forEach(function (id, i) {
        var a = byId(id);
        var tile = document.createElement("button");
        tile.className = "fav-tile" + (sel.zone === "fav" && sel.i === i ? " sel" : "");
        tile.style.setProperty("--h", hue(id));
        fillTile(tile, a);
        tile.title = a.title;
        tile.setAttribute("aria-label", actionLabel(a, ACTIONS));
        tile.onclick = function () { sel = { zone: "fav", i: i }; activateEntry(a); };
        // Numbered quick-launch badge (Alt/Option+digit), drawn on the corner.
        var slot = favSlotForIndex(i);
        if (slot) {
            var badge = document.createElement("span");
            badge.className = "fav-slot";
            badge.textContent = slot;
            badge.title = slot === "0" ? "Favourite 10 (Alt+0)" : "Favourite " + slot + " (Alt+" + slot + ")";
            tile.appendChild(badge);
        }
        // Direct removal: a hover-revealed ✕ in the tile's corner. click() stops propagation so it
        // unpins without activating the action.
        var unpin = document.createElement("button");
        unpin.className = "fav-unpin";
        unpin.title = "Remove from favourites";
        unpin.setAttribute("aria-label", "Remove from favourites");
        unpin.innerHTML = '<svg width="9" height="9" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" aria-hidden="true"><line x1="4" y1="4" x2="12" y2="12"/><line x1="12" y1="4" x2="4" y2="12"/></svg>';
        unpin.onclick = function (ev) { ev.stopPropagation(); toggleFav(id); };
        tile.appendChild(unpin);
        tile.oncontextmenu = function (ev) {
            ev.preventDefault();
            // why: selecting shows the eyebrow, which grows the launcher - resize so the popup
            //      isn't clipped (mirrors arrow-nav). requestResize no-ops when height is unchanged.
            sel = { zone: "fav", i: i }; render({ resize: true });
            showFavMenu(ev.clientX, ev.clientY, id);
        };
        favEl.appendChild(tile);
    });
}

// ---- favourite context menu (right-click a tile) -----------------------------
var favMenuEl = null;

function hideFavMenu() { if (favMenuEl) favMenuEl.hidden = true; }

function addFavMenuItem(label, enabled, fn) {
    var item = document.createElement("button");
    item.className = "ctx-item";
    item.textContent = label;
    item.disabled = !enabled;
    item.onclick = function () { hideFavMenu(); fn(); };
    favMenuEl.appendChild(item);
}

// One reused menu node (Move left/right + Unpin), positioned at the cursor and clamped
// to the viewport. Native browser context menus can't add items, so we roll our own tiny one.
function showFavMenu(x, y, id) {
    if (!favMenuEl) {
        favMenuEl = document.createElement("div");
        favMenuEl.className = "ctx-menu";
        document.body.appendChild(favMenuEl);
    }
    favMenuEl.innerHTML = "";
    var favs = currentVisibleFavs();
    var vi = favs.indexOf(id);
    addFavMenuItem("Move left", vi > 0, function () { moveFav(id, -1); });
    addFavMenuItem("Move right", vi >= 0 && vi < favs.length - 1, function () { moveFav(id, 1); });
    addFavMenuItem("Unpin", true, function () { toggleFav(id); });
    favMenuEl.hidden = false;
    favMenuEl.style.left = Math.max(0, Math.min(x, window.innerWidth - favMenuEl.offsetWidth - 4)) + "px";
    favMenuEl.style.top = Math.max(0, Math.min(y, window.innerHeight - favMenuEl.offsetHeight - 4)) + "px";
}

// Swap a favourite with its visible neighbour (dir -1/+1) and persist the new order. Swapping
// by id inside FAVS (not the visible slice) keeps any hidden pins (no live action) in place.
function moveFav(id, dir) {
    var favs = currentVisibleFavs();
    var vi = favs.indexOf(id);
    var ni = vi + dir;
    if (vi === -1 || ni < 0 || ni >= favs.length) return;
    var a = FAVS.indexOf(id), b = FAVS.indexOf(favs[ni]);
    if (a === -1 || b === -1) return;
    FAVS[a] = favs[ni]; FAVS[b] = id;
    SendMessage({ command: "reorder_favourites", ids: FAVS.slice() });
    sel = { zone: "fav", i: ni };
    render({ resize: true });
}

// Name of the selected favourite, shown above the bar; hidden unless a fav is selected.
function updateFavEyebrow(favs) {
    if (!eyeEl) return;
    var a = sel.zone === "fav" && favs.length ? byId(favs[sel.i]) : null;
    eyeEl.textContent = a ? a.title : "";
    eyeEl.hidden = !a;
}

// One settings row; has no star/tile because settings aren't pinnable.
// A command/action row - used for search results, recents, and (because settings are actions now)
// the setting options too. All rows are pinnable, so every row carries a star.
function renderActionRow(a, i) {
    var on = FAVS.indexOf(a.id) !== -1;
    var row = document.createElement("div");
    row.className = "row" + (sel.zone === "list" && sel.i === i ? " sel" : "");
    row.setAttribute("aria-label", actionLabel(a, ACTIONS));

    var tile = document.createElement("div");
    tile.className = "tile";
    tile.style.setProperty("--h", hue(a.id));
    fillTile(tile, a);

    var left = document.createElement("div");
    left.className = "row-left";
    var mi = matchIndex[a.id];
    var sourceEl = markedText("row-eyebrow", a.group || a.source, mi ? mi.source : null);
    var line = document.createElement("div");
    line.className = "row-line";
    var name = markedText("row-name", a.title, mi ? mi.title : null);
    line.appendChild(name);
    if (a.shortcut) {
        var sc = document.createElement("div");
        sc.className = "row-sc";
        a.shortcut.split("+").forEach(function (k) {
            var key = document.createElement("kbd");
            key.textContent = k;
            sc.appendChild(key);
        });
        line.appendChild(sc);
    }
    left.appendChild(sourceEl);
    left.appendChild(line);
    row.appendChild(tile);
    row.appendChild(left);

    var star = document.createElement("button");
    star.className = "star" + (on ? " on" : "");
    star.innerHTML = starSvg(on);
    star.title = on ? "Unpin from favourites" : "Pin to favourites";
    star.onclick = function (ev) { ev.stopPropagation(); toggleFav(a.id); };
    // why: two quick fav/unfav clicks must not dblclick-run the row
    star.ondblclick = function (ev) { ev.stopPropagation(); };
    row.appendChild(star);

    row.onclick = function () { sel = { zone: "list", i: i }; render({ resize: true }); };
    row.ondblclick = function () { sel = { zone: "list", i: i }; activateEntry(a); };
    return row;
}

// Append rows [from, to) into listEl, always inserting before the bottom spacer so row order is preserved.
function appendActionRows(list, from, to) {
    var spacer = spacerEl || ensureSpacer();
    for (var i = from; i < to; i++) {
        var row = renderActionRow(list[i], i);
        row.setAttribute("data-idx", i);
        listEl.insertBefore(row, spacer);
    }
}

// Ensure the bottom spacer exists as the last child of listEl. It is (re)created on rebuild because
// listEl.innerHTML="" destroys the old node.
function ensureSpacer() {
    if (!spacerEl || spacerEl.parentNode !== listEl) {
        spacerEl = document.createElement("div");
        spacerEl.className = "dial-spacer-bottom";
        listEl.appendChild(spacerEl);
    }
    return spacerEl;
}

// Size the spacer to the un-rendered tail so the scrollbar reflects the full match count.
function setBottomSpacer(total) {
    ensureSpacer();
    spacerEl.style.height = Math.max(0, total - renderEnd) * ROW_H + "px";
}

// Reveal rows up to `upto` (an exclusive index), appending without rebuilding the whole list. Used by
// the scroll handler (viewport + overscan) and by arrow-nav that runs off the end of the current window.
function revealTo(list, upto) {
    var need = Math.min(list.length, upto);
    if (need <= renderEnd)
        return;
    appendActionRows(list, renderEnd, need);
    renderEnd = need;
    setBottomSpacer(list.length);
}

// Rebuild the list from the first window (new search / phase change), clearing stale rows.
function rebuildCommandsList(list) {
    listEl.innerHTML = "";
    listEl.className = "dial-list";
    ensureSpacer();
    renderEnd = 0;
    appendActionRows(list, 0, Math.min(list.length, K_ROWS));
    renderEnd = Math.min(list.length, K_ROWS);
    setBottomSpacer(list.length);
}

// Toggle the .sel class in place - arrow-nav/click don't rebuild the DOM, just re-highlight the row.
function updateSelection() {
    var rows = listEl ? listEl.querySelectorAll(".row") : [];
    for (var i = 0; i < rows.length; i++) {
        var idx = parseInt(rows[i].getAttribute("data-idx"), 10);
        rows[i].classList.toggle("sel", sel.zone === "list" && idx === sel.i);
    }
}

function renderCommandsList() {
    var list = currentList();
    var total = list.length;
    if (sel.zone === "list")
        sel.i = Math.max(0, Math.min(sel.i, total - 1));
    var showList = shouldRenderActionList(query);
    // Recents are not filtered, so clear any stale match marks from a previous typed query.
    if (!showList)
        matchIndex = {};

    if (!total) {
        listEl.innerHTML = "";
        spacerEl = null;
        listEl.className = "dial-list empty";
        if (countEl) countEl.hidden = true;
        var empty = document.createElement("div");
        empty.className = "dial-empty";
        empty.textContent = showList ? ("No actions match (Total: " + ACTIONS.length + ")") : "No actions yet";
        listEl.appendChild(empty);
        renderEnd = 0;
        builtKey = buildKey() + "|0";
        return;
    }

    var key = buildKey() + "|" + total;
    if (key !== builtKey) {
        builtKey = key;
        rebuildCommandsList(list);
    } else if (sel.i >= renderEnd) {
        // Arrow-nav walked past the rendered window - reveal enough to keep the selection visible.
        revealTo(list, revealTarget(total, sel.i, K_ROWS));
    }

    listEl.className = "dial-list";
    if (countEl) {
        countEl.hidden = false;
        countEl.textContent = showList ? resultCountText(ACTIONS.length, total, query) : total + " recent";
    }
    updateSelection();
}

// A tab row: no star/unpin (tabs aren't pinnable), tile monogram from the title. Uses tabTitle so
// pages added with an empty text (e.g. Home) still show a label and an icon letter.
function renderTabRow(t, i) {
    var label = tabTitle(t);
    var row = document.createElement("div");
    row.className = "row" + (sel.zone === "list" && sel.i === i ? " sel" : "");
    row.setAttribute("aria-label", label);

    var tile = document.createElement("div");
    tile.className = "tile";
    tile.style.setProperty("--h", hue(t.id));
    tile.textContent = label.charAt(0).toUpperCase();

    var left = document.createElement("div");
    left.className = "row-left";
    var line = document.createElement("div");
    line.className = "row-line";
    var name = document.createElement("div");
    name.className = "row-name";
    name.textContent = label;
    line.appendChild(name);
    left.appendChild(line);

    row.appendChild(tile);
    row.appendChild(left);
    row.onclick = function () { sel = { zone: "list", i: i }; render({ resize: true }); };
    row.ondblclick = function () { sel = { zone: "list", i: i }; jumpToTab(t); };
    return row;
}

function renderTabList() {
    var q = (query || "").trim();
    var list = currentList();
    listEl.innerHTML = "";

    if (!list.length) {
        listEl.className = "dial-list empty";
        if (countEl) countEl.hidden = true;
        var empty = document.createElement("div");
        empty.className = "dial-empty";
        empty.textContent = q ? "No tabs match" : "No tabs";
        listEl.appendChild(empty);
        return;
    }
    if (sel.zone === "list")
        sel.i = Math.max(0, Math.min(sel.i, list.length - 1));
    listEl.className = "dial-list";
    if (countEl) {
        countEl.hidden = false;
        countEl.textContent = q ? list.length + " matches" : list.length + " tabs";
    }
    list.forEach(function (t, i) { listEl.appendChild(renderTabRow(t, i)); });
}

function renderPercentList() {
    var q = (query || "").trim();
    listEl.innerHTML = "";
    listEl.className = "dial-list empty";
    if (countEl) countEl.hidden = true;
    var ph = document.createElement("div");
    ph.className = "dial-empty";
    ph.textContent = q ? ("Go to " + q + "% of the layer range") : "Enter a layer percentage (0-100)";
    listEl.appendChild(ph);
}

// ---- inline setting editor (DOM stage) ---------------------------------------

// Collapse every open custom dropdown except `keep` (null collapses all). The menu list elements
// are the .ed-dropdown-menu siblings of the toggle buttons we track via openDropDownEl.
function closeOtherDropDowns(keep) {
    if (openDropDownEl && openDropDownEl !== keep && openDropDownEl.parentNode) {
        var m = openDropDownEl.parentNode.querySelector(".ed-dropdown-menu");
        if (m) m.hidden = true;
        openDropDownEl.classList.remove("open");
    }
    if (!keep)
        openDropDownEl = null;
}

// Collapse the currently open dropdown, if any (kept for the editor's export/import-adjacent helpers).
function closeEditorDropDown() { closeOtherDropDowns(null); }

// Place an open dropdown menu as a fixed overlay just under its toggle, so the menu floats over the
// card (never resizing it) and is clamped to the popup's bottom edge with an internal scrollbar for
// long option lists. position:fixed escapes the card/launcher overflow clipping that an absolute
// menu would otherwise hit, keeping every option reachable within the window.
function positionDropDownMenu(btn, menu) {
    var lrect = (document.querySelector(".launcher") || { getBoundingClientRect: function () { return { top: 0, bottom: window.innerHeight }; } }).getBoundingClientRect();
    var rect = btn.getBoundingClientRect();
    // Available room above and below the toggle, within the popup. Opening the menu must not push it
    // past the window edge (that's the unreachable-overflow bug) - pick whichever side has more room
    // and clamp the box to it. Overflow-y:auto scrolls any long list inside the menu itself.
    var spaceBelow  = lrect.bottom - (rect.bottom + 8);
    var spaceAbove  = (rect.top - 8) - lrect.top;
    var openUp      = spaceBelow < spaceAbove;
    var maxH        = Math.max(0, Math.min(openUp ? spaceAbove : spaceBelow, 200));
    menu.style.position  = "fixed";
    menu.style.width     = rect.width + "px";
    menu.style.left      = rect.left + "px";
    menu.style.maxHeight = maxH + "px";
    if (openUp) {
        // bottom edge sits just above the toggle; the box grows upward to content height.
        menu.style.top    = "auto";
        menu.style.bottom = (lrect.bottom - rect.top + 4) + "px";
    } else {
        menu.style.top    = (rect.bottom + 4) + "px";
        menu.style.bottom = "auto";
    }
}

// Build the control element for one row (toggle/number/dropdown/combo/text/color) and seed it with
// the current value. Returns {el, node, extra} - node is what is appended, extra carries a datalist.
function settingInputFor(row) {
    var el;
    if (row.kind === "toggle") {
        el = document.createElement("input");
        el.type = "checkbox";
        el.checked = !!row.value;
        var sw = document.createElement("label");
        sw.className = "ed-switch";
        sw.appendChild(el);
        var slider = document.createElement("span");
        slider.className = "ed-slider";
        sw.appendChild(slider);
        return { el: el, node: sw };
    }
    if (row.kind === "number") {
        el = document.createElement("input");
        el.type = "number";
        el.step = row.is_int ? 1 : "any";
        if (row.min != null) el.min = row.min;
        if (row.max != null) el.max = row.max;
        if (row.value != null && row.value !== "") el.value = row.value;
        return { el: el, node: el };
    }
    if (row.kind === "dropdown") {
        // Native <select> popups are unreliable inside this wxWebView (a click synthesizes a
        // keydown that can reach the global Enter handler and apply+close). Build a custom
        // dropdown: a toggle button that expands an in-flow option list. Selection only updates
        // local state; nothing applies until Enter/Apply. The value lives on the toggle button's
        // dataset so settingControlValue can read it back without the DOM copy.
        var wrap = document.createElement("div");
        wrap.className = "ed-dropdown";
        var btn = document.createElement("button");
        btn.type = "button";
        btn.className = "ed-dropdown-toggle";
        btn.dataset.value = row.value != null ? String(row.value) : "";
        // The selected value's pattern pictogram (hidden when the value has none).
        var toggleIcon = document.createElement("img");
        toggleIcon.className = "ed-dropdown-icon";
        toggleIcon.setAttribute("aria-hidden", "true");
        toggleIcon.alt = "";
        var tIcon = dropdownIcon(row.enum_options || [], row.value);
        toggleIcon.src = tIcon || "";
        toggleIcon.hidden = !tIcon;
        btn.appendChild(toggleIcon);
        var label = document.createElement("span");
        label.className = "ed-dropdown-label";
        label.textContent = dropdownLabel(row.enum_options || [], row.value);
        btn.appendChild(label);
        var caret = document.createElement("span");
        caret.className = "ed-dropdown-caret";
        caret.textContent = "▾";
        btn.appendChild(caret);
        var listEl = document.createElement("div");
        listEl.className = "ed-dropdown-menu";
        listEl.hidden = true;
        (row.enum_options || []).forEach(function (o, oi) {
            var opt = document.createElement("button");
            opt.type = "button";
            opt.className = "ed-dropdown-option";
            if (o.icon) {
                var img = document.createElement("img");
                img.className = "ed-option-icon";
                img.src = o.icon;
                img.alt = "";
                img.setAttribute("aria-hidden", "true");
                opt.appendChild(img);
            }
            var optLabel = document.createElement("span");
            optLabel.className = "ed-option-label";
            optLabel.textContent = o.label;
            opt.appendChild(optLabel);
            if (String(o.value) === String(row.value))
                opt.classList.add("sel");
            opt.onclick = function (ev) {
                ev.stopPropagation();
                btn.dataset.value = String(o.value);
                label.textContent = o.label;
                toggleIcon.src = o.icon || "";
                toggleIcon.hidden = !o.icon;
                listEl.hidden = true;
                btn.classList.remove("open");
                openDropDownEl = null;
                onSettingValueChanged(settingDesc, o);
            };
            listEl.appendChild(opt);
        });
        btn.onclick = function (ev) {
            ev.stopPropagation();
            if (openDropDownEl === btn) {
                // clicking the open toggle closes it
                listEl.hidden = true;
                btn.classList.remove("open");
                openDropDownEl = null;
                return;
            }
            closeOtherDropDowns(null); // collapse any other open dropdown
            positionDropDownMenu(btn, listEl);
            listEl.hidden = false;
            btn.classList.add("open");
            openDropDownEl = btn;
        };
        wrap.appendChild(btn);
        wrap.appendChild(listEl);
        return { el: btn, node: wrap };
    }
    if (row.kind === "combo") {
        el = document.createElement("input");
        el.type = "text";
        var dl = document.createElement("datalist");
        el.setAttribute("list", dl.id = "ed-combo-" + row.index);
        (row.enum_options || []).forEach(function (o) {
            var op = document.createElement("option");
            op.value = o.value;
            op.textContent = o.label;
            dl.appendChild(op);
        });
        el.value = row.value != null ? String(row.value) : "";
        return { el: el, node: el, extra: dl };
    }
    if (row.kind === "color") {
        el = document.createElement("input");
        el.type = "color";
        el.value = row.value && /^#[0-9a-fA-F]{6}$/.test(row.value) ? row.value : "#000000";
        return { el: el, node: el };
    }
    if (row.kind === "percent") {
        // "mm or %" (coFloatOrPercent/coFloatsOrPercents): a free-text field showing the serialized
        // value (e.g. "10%" or "0.5"). The unit hints at the sidebar semantics (mm or %), so it's
        // not shown here - the value itself carries the % when applicable.
        el = document.createElement("input");
        el.type = "text";
        el.value = row.value != null ? String(row.value) : "";
        return { el: el, node: el };
    }
    // text
    el = document.createElement("input");
    el.type = "text";
    el.value = row.value != null ? String(row.value) : "";
    return { el: el, node: el };
}

// One labeled control row in the editor card.
function renderControlRow(row, i) {
    var wrap = document.createElement("div");
    wrap.className = "editor-row";
    if (row.label != null) {
        var lab = document.createElement("label");
        lab.className = "editor-label";
        lab.textContent = row.label;
        wrap.appendChild(lab);
    }
    var ctrl = settingInputFor(row);
    if (ctrl.extra)
        wrap.appendChild(ctrl.extra); // datalist for open-enum combos
    wrap.appendChild(ctrl.node);
    if (row.unit) {
        var unit = document.createElement("span");
        unit.className = "editor-unit";
        unit.textContent = row.unit;
        wrap.appendChild(unit);
    }
    settingFieldEls[i] = ctrl.el;
    return wrap;
}

// Render the editor card into listEl (phase === "setting"). Keeps the search head hidden so the
// card owns the layout.
function renderSettingStage() {
    listEl.innerHTML = "";
    listEl.className = "dial-list setting";
    if (countEl) countEl.hidden = true;
    if (!settingDesc || !settingDesc.opt_key) {
        var ph = document.createElement("div");
        ph.className = "dial-empty";
        ph.textContent = "Loading…";
        listEl.appendChild(ph);
        return;
    }
    var card = document.createElement("div");
    card.className = "dial-editor";
    if (settingDesc.breadcrumb) {
        var crumb = document.createElement("div");
        crumb.className = "row-eyebrow";
        crumb.textContent = settingDesc.breadcrumb;
        card.appendChild(crumb);
    }
    var titleRow = document.createElement("div");
    titleRow.className = "editor-title-row";
    var titleIcon = document.createElement("img");
    titleIcon.className = "editor-preview-icon";
    titleIcon.setAttribute("aria-hidden", "true");
    titleIcon.alt = "";
    var pIcon = dropdownIcon(settingDesc.enum_options || [], settingDesc.value);
    titleIcon.src = pIcon || "";
    titleIcon.hidden = !pIcon;
    titleRow.appendChild(titleIcon);
    settingPreviewIcon = titleIcon;
    var title = document.createElement("div");
    title.className = "editor-title";
    title.textContent = settingDesc.title || "";
    titleRow.appendChild(title);
    card.appendChild(titleRow);
    if (settingDesc.tooltip) {
        var tt = document.createElement("div");
        tt.className = "editor-tooltip";
        tt.textContent = settingDesc.tooltip;
        card.appendChild(tt);
    }

    var actions = document.createElement("div");
    actions.className = "editor-actions";
    if (settingDesc.editable) {
        settingRows = settingControlRows(settingDesc);
        settingFieldEls = [];
        if (settingRows.length) {
            settingRows.forEach(function (row, i) { card.appendChild(renderControlRow(row, i)); });
        } else {
            var empty = document.createElement("div");
            empty.className = "dial-empty";
            empty.textContent = "Nothing editable here";
            card.appendChild(empty);
        }
        var apply = document.createElement("button");
        apply.className = "ed-btn ed-btn-primary";
        apply.textContent = "Apply";
        apply.onclick = applySetting;
        actions.appendChild(apply);
    } else {
        var ro = document.createElement("div");
        ro.className = "editor-readonly";
        ro.textContent = "This setting can't be edited here";
        card.appendChild(ro);
        var open = document.createElement("button");
        open.className = "ed-btn";
        open.textContent = "Open in sidebar";
        open.onclick = openSettingInSidebar;
        actions.appendChild(open);
    }
    var cancel = document.createElement("button");
    cancel.className = "ed-btn";
    cancel.textContent = "Cancel";
    cancel.onclick = exitPhase;
    actions.appendChild(cancel);
    card.appendChild(actions);
    var hint = document.createElement("div");
    hint.className = "editor-hint";
    hint.textContent = "Enter to apply · Esc to cancel";
    card.appendChild(hint);
    listEl.appendChild(card);
}

function applySetting() {
    if (!settingDesc || !settingDesc.editable) return;
    var value = settingCollectedValue(settingDesc, settingRows, settingFieldEls);
    if (value === undefined) {
        flashHint("Enter a valid value");
        return;
    }
    SendMessage({ command: "set_setting", id: settingId, value: value });
}

function openSettingInSidebar() {
    if (!settingDesc) return;
    SendMessage({ command: "open_setting_in_sidebar", opt_key: settingDesc.opt_key, type: settingDesc.type, category: settingDesc.category || "" });
}

// When a dropdown option is picked, mirror its pattern pictogram onto the editor title's preview so
// the current selection is visible without opening the menu. Non-enum / icon-less rows no-op.
function onSettingValueChanged(desc, option) {
    if (!settingPreviewIcon) return;
    var icon = (option && option.icon) || "";
    settingPreviewIcon.src = icon;
    settingPreviewIcon.hidden = !icon;
}

function enterSettingPhase(a) {
    if (!a) return;
    phase = "setting";
    query = ""; qEl.value = ""; syncClearButton();
    sel = { zone: "list", i: 0 };
    settingId = a.id;
    settingDesc = null;
    settingRows = [];
    settingFieldEls = [];
    if (headEl) headEl.hidden = true;
    render({ resetScroll: true });
    SendMessage({ command: "setting_descriptor", id: a.id });
}

function renderList() {
    if (phase === "tab")
        renderTabList();
    else if (phase === "percent")
        renderPercentList();
    else if (phase === "setting")
        renderSettingStage();
    else
        renderCommandsList();
}

function render(opts) {
    renderFav();
    renderList();
    scrollSelectedIntoView();
    if (opts && opts.resetScroll)
        resetScrollPositions(listEl, document);
    if (opts && opts.resize)
        requestResize();
}

// Keep the selected item in view as arrows move it: the list scrolls vertically, the fav bar
// horizontally (arrow nav "pushes" the scrollable fav row to follow the selection).
function scrollSelectedIntoView() {
    var el = null;
    if (sel.zone === "list" && listEl)
        el = listEl.querySelector(".row.sel");
    else if (sel.zone === "fav" && favEl)
        el = favEl.querySelector(".fav-tile.sel");
    if (el && el.scrollIntoView)
        el.scrollIntoView({ block: "nearest", inline: "nearest" });
}

function requestResize() {
    if (!document.body)
        return;
    setTimeout(function () {
        var launcher = document.querySelector(".launcher");
        if (!launcher)
            return;
        var height = Math.ceil(launcher.getBoundingClientRect().height);
        if (!height || height === lastResizeHeight)
            return;
        lastResizeHeight = height;
        SendMessage({ command: "resize", height: height });
    }, 0);
}

// Transient inline hint pinned to the top of the launcher (e.g. "favourites are full").
function flashHint(text) {
    if (!document.body) return;
    var launcher = document.querySelector(".launcher");
    if (!launcher) return;
    var hint = document.createElement("div");
    hint.className = "dial-flash";
    hint.textContent = text;
    launcher.insertBefore(hint, launcher.firstChild);
    setTimeout(function () {
        if (hint && hint.parentNode) hint.parentNode.removeChild(hint);
    }, 2500);
}

// ---- actions -----------------------------------------------------------------
function toggleFav(id) {
    var k = FAVS.indexOf(id);
    var newState = k === -1;
    if (newState) FAVS.push(id); else FAVS.splice(k, 1);
    SendMessage({ command: "toggle_favourite", id: id, fav: newState });
    render({ resize: true });
}

// Fire a command/plugin action; C++ owns the run-confirm (native dialog) + suppression, then
// closes the popup + toasts.
function run(a) {
    if (!a) return;
    SendMessage({ command: "run_action", id: a.id, title: a.title, param: "" });
}

// Activate an entry in the main phase. Two-phase commands switch the palette to their input phase
// instead of running; everything else (including a setting jump, which is a plain action now) runs.
function activateEntry(a) {
    if (!a) return;
    if (a.input === "percent") { enterPercentPhase(); return; }
    if (a.input === "tab") { enterTabsPhase(); return; }
    if (a.input === "setting") { enterSettingPhase(a); return; }
    run(a);
}

function runSelected() {
    if (phase === "percent") {
        runJumpToLayer(query.trim());
        return;
    }
    if (phase === "tab") {
        var t = currentList()[sel.i];
        if (t) jumpToTab(t);
        return;
    }
    var list = currentList();
    var id = selectedActionId(sel, list, currentVisibleFavs(), query);
    if (id) activateEntry(byId(id));
}

function runJumpToLayer(pct) {
    if (pct === "") return;
    var n = parseFloat(pct);
    if (!isFinite(n) || n < 0 || n > 100) return;
    var a = findActionByInput("percent");
    if (!a) return;
    SendMessage({ command: "run_action", id: a.id, title: a.title, param: String(n) });
}

function jumpToTab(t) {
    SendMessage({ command: "go_to_tab", id: t.id, title: tabTitle(t) });
}

function enterPercentPhase() {
    phase = "percent"; query = ""; qEl.value = "";
    sel = { zone: "list", i: 0 };
    qEl.placeholder = "Go to layer % (0-100)";
    syncClearButton();
    render({ resize: true, resetScroll: true });
    qEl.focus();
}

function enterTabsPhase() {
    phase = "tab"; tabOptions = []; query = ""; qEl.value = "";
    sel = { zone: "list", i: 0 };
    qEl.placeholder = "Go to tab";
    syncClearButton();
    render({ resize: true, resetScroll: true });
    qEl.focus();
    SendMessage({ command: "search_tabs" });
}

function exitPhase() {
    phase = "commands"; tabOptions = []; query = ""; qEl.value = "";
    settingId = ""; settingDesc = null; settingRows = []; settingFieldEls = [];
    settingPreviewIcon = null;
    closeOtherDropDowns(null);
    if (headEl) headEl.hidden = false;
    sel = { zone: "list", i: 0 };
    // why: builtKey caches phase|query so renderCommandsList can skip a rebuild on arrow-nav/click.
    // Leftover from the setting phase it matches the (empty-query) commands key, which would skip
    // the rebuild and leave the editor card in the list. Reset it so the commands view is rebuilt.
    builtKey = "";
    qEl.placeholder = "Search " + ACTIONS.length + " actions";
    syncClearButton();
    render({ resize: true, resetScroll: true });
    qEl.focus();
}

function focusInput() { setTimeout(function () { if (qEl) qEl.focus(); }, 0); }

// Move keyboard focus onto the first editable field of the setting editor card. Deferred so the
// element is attached and its text is selectable by the time we focus it. For text-editable inputs
// also select the existing value so the user can type straight over it.
function focusSettingEditor() {
    setTimeout(function () {
        var el = settingFieldEls && settingFieldEls[0];
        if (!el) return;
        if (el.focus) el.focus();
        if ((el.tagName === "INPUT") && el.select)
            el.select();
    }, 0);
}

// ---- init --------------------------------------------------------------------
function OnInit() {
    qEl = $("q"); listEl = $("list"); favEl = $("favBar"); clearEl = $("clear"); eyeEl = $("favEyebrow"); countEl = $("count");
    headEl = document.querySelector(".dial-head");
    syncClearButton();

    $("clear").onclick = function () {
        query = ""; qEl.value = ""; sel = { zone: "list", i: 0 };
        render({ resize: true, resetScroll: true }); qEl.focus();
        syncClearButton();
    };
    qEl.addEventListener("input", function () {
        query = qEl.value; sel = { zone: "list", i: 0 }; syncClearButton();
        render({ resize: true, resetScroll: true });
    });
    // Windowed reveal: as the list scrolls, materialize the next window (append-only, no rebuild) so the
    // DOM stays bounded to what's near the viewport. Guarded to the commands phase (tabs/percent are tiny).
    listEl.addEventListener("scroll", function () {
        if (phase !== "commands")
            return;
        var list = currentList();
        if (renderEnd >= list.length)
            return;
        var firstVisible = Math.max(0, Math.floor(listEl.scrollTop / ROW_H));
        // Reveal the viewport + a full window of lookahead so fast scrolling doesn't hit a blank tail.
        revealTo(list, revealTarget(list.length, firstVisible, 2 * K_ROWS));
    });

    // why: dismiss the fav context menu on any click/scroll away from it (capture scroll to catch nested scrollers).
    document.addEventListener("click", hideFavMenu);
    // Dismiss an open editor dropdown on any outside click. Toggle/option clicks stopPropagation
    // so they don't immediately close the menu they just opened/picked from.
    document.addEventListener("click", function () {
        if (openDropDownEl) closeEditorDropDown();
    });
    document.addEventListener("scroll", hideFavMenu, true);

    document.addEventListener("keydown", function (e) {
        if (favMenuEl && !favMenuEl.hidden && e.key === "Escape") { e.preventDefault(); hideFavMenu(); return; }
        // While an editor dropdown menu is open it owns the keys: Escape closes the menu (a second
        // Esc exits the phase), Enter/arrows select options natively, and we must not apply/exit.
        if (phase === "setting" && openDropDownEl && openDropDownEl.parentNode) {
            if (e.key === "Escape") { e.preventDefault(); closeEditorDropDown(); }
            return;
        }
        // Quick-launch a numbered favourite: Alt/Option + digit (0 = the 10th). Only in the
        // commands phase, where the pinned bar is shown.
        if (phase === "commands" && e.altKey && !e.ctrlKey && !e.metaKey) {
            var slotIdx = favIndexForDigit(e.key);
            var favIds  = currentVisibleFavs();
            if (slotIdx >= 0 && slotIdx < favIds.length) {
                e.preventDefault();
                var fav = byId(favIds[slotIdx]);
                if (fav) activateEntry(fav);
                return;
            }
        }
        var list = currentList();
        // why: fav bar only exists in the commands phase; other phases are list-only, so an
        // ArrowUp at the top must not jump into a hidden fav zone.
        var favs = (phase === "commands") ? currentVisibleFavs() : [];
        // why: Up/Down always navigate; Left/Right only navigate the fav bar. In the list zone,
        //      let Left/Right fall through so they move the caret in the focused search field.
        var lr = e.key === "ArrowLeft" || e.key === "ArrowRight";
        if (e.key === "ArrowDown" || e.key === "ArrowUp" || (lr && sel.zone === "fav")) {
            // In the percent/setting phases the input controls own the caret - arrows edit text, not rows.
            if (phase === "percent" || phase === "setting") return;
            e.preventDefault();
            sel = nextSel(sel, e.key, list.length, favs.length);
            // why: entering/leaving the fav zone toggles the eyebrow line, changing launcher height;
            // resize so the popup grows/shrinks instead of clipping. requestResize no-ops when unchanged.
            render({ resize: true });
        } else if (e.key === "Enter") {
            e.preventDefault();
            if (phase === "percent") runJumpToLayer(query.trim());
            else if (phase === "setting") applySetting();
            else runSelected();
        } else if (e.key === "Escape") {
            e.preventDefault();
            if (phase !== "commands") { exitPhase(); }
            else if (query) { query = ""; qEl.value = ""; sel = { zone: "list", i: 0 }; syncClearButton(); render({ resize: true, resetScroll: true }); qEl.focus(); }
            else SendMessage({ command: "close_page" });
        }
    });

    SendMessage({ command: "request_actions" });
}
