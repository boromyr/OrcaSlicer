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

// Palette phase: 'commands' (search actions/commands, show recents), 'settings' ("Go to
// setting..." second phase: search config options), 'percent' ("Go to layer" second phase:
// enter a 0-100 percentage), 'tab' ("Go to tab..." second phase: pick a notebook tab).
var phase = "commands";
var settingsResults = [];  // [{opt_key,type,label,category,group}]
var tabOptions = [];       // [{id,title}] - notebook pages, fetched on entering the tab phase

// why: fuzzy matcher (FoldChar/Norm/FuzzyRanges) lives in shared ../../js/fuzzy-search.js, loaded before
//      this script - it is shared with the Plugins dialog. Speed dial search is always case-insensitive.

// element handles, assigned in OnInit (kept null so load-time touches no DOM)
var qEl = null, listEl = null, favEl = null, clearEl = null, eyeEl = null, countEl = null;

// ---- pure helpers (no DOM; unit-tested) -------------------------------------
function filterActions(actions, query) {
    var q = (query || "").trim();
    var list = actions || [];
    matchIndex = {};
    if (!q)
        return list.slice(0);

    var out = [];
    for (var i = 0; i < list.length; i++) {
        var a = list[i];
        var titleMatch = FuzzyRanges(a.title, q, false);
        var sourceMatch = FuzzyRanges(a.source || "", q, false);
        if (!titleMatch && !sourceMatch)
            continue;
        matchIndex[a.id] = { title: titleMatch, source: sourceMatch, useTitle: !!titleMatch };
        out.push(a);
    }
    return out;
}

function visibleFavourites(favourites, actions) {
    // why: a fav whose id has no live action (plugin unloaded/disabled) renders a dead
    //      monogram tile whose click run()s to a silent no-op; drop it from the quick-bar.
    var seen = {};
    (actions || []).forEach(function (a) { seen[a.id] = true; });
    return (favourites || []).filter(function (id, i, arr) {
        return seen[id] && arr.indexOf(id) === i;
    });
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

// The active list for the commands phase. A typed query filters every action (plugins +
// commands); an empty query shows the recent list instead (recents live below the search bar).
function commandList(actions, recents, query) {
    if (shouldRenderActionList(query))
        return filterActions(actions || [], query);
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

// Action tile code - see monogramFor for the escalation ladder. Accessed via accessors so the
// same helper serves settings rows (leaf label + category) without duplicating the logic.
function tileCode(action, actions) {
    return monogramFor(action, actions,
        function (o) { return o.title; },
        function (o) { return o.source; },
        function (o) { return o.id; });
}

// Last " : "-separated segment of a "category : group : label" setting label = the option name
// (e.g. "Quality : Layer Height" -> "Layer Height"); empty labels fall back to the opt_key.
function leafLabel(s) {
    var label = String(s && (s.label || s.opt_key || "") || "");
    var parts = label.split(" : ");
    var leaf = parts[parts.length - 1];
    return (leaf || "").trim() || label.trim();
}

// Setting tile code - the option name initial, then the section (category) on collision, then a
// stable ordinal, so settings no longer all collapse to a generic "S".
function settingCode(s, list) {
    return monogramFor(s, list,
        function (o) { return leafLabel(o); },
        function (o) { return o.category; },
        function (o) { return o.opt_key; });
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
        settingsResults: [],
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

// nextSel: pure arrow-nav transition. Down fav->list0; Down list->clamp; Up list@0->fav0;
// Up list->i-1; Left/Right clamp within fav. Returns a fresh {zone,i}.
function nextSel(sel, key, listLen, favLen) {
    var zone = sel.zone, i = sel.i;
    if (key === "ArrowDown") {
        if (zone === "fav") return { zone: "list", i: 0 };
        return { zone: "list", i: Math.min(i + 1, Math.max(0, listLen - 1)) };
    }
    if (key === "ArrowUp") {
        if (zone === "list") {
            if (i <= 0) return favLen ? { zone: "fav", i: 0 } : { zone: "list", i: 0 };
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
        settingsResults = next.settingsResults;
        tabOptions = next.tabOptions;
        if (qEl) {
            qEl.value = "";
            qEl.placeholder = "Search " + ACTIONS.length + " actions";
            syncClearButton();
        }
        render({ resize: true, resetScroll: true });
        focusInput();
    } else if (payload.command === "settings_results") {
        settingsResults = payload.results || [];
        if (sel.zone === "list")
            sel.i = Math.max(0, Math.min(sel.i, settingsResults.length - 1));
        render({ resize: true });
    } else if (payload.command === "tab_results") {
        tabOptions = payload.tabs || [];
        if (sel.zone === "list")
            sel.i = Math.max(0, Math.min(sel.i, tabOptions.length - 1));
        render({ resize: true });
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
    if (phase === "settings") return settingsResults;
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
        tile.textContent = tileCode(a, ACTIONS);
        tile.title = a.title;
        tile.setAttribute("aria-label", actionLabel(a, ACTIONS));
        tile.onclick = function () { sel = { zone: "fav", i: i }; activateEntry(a); };
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
function renderSettingRow(s, i) {
    var row = document.createElement("div");
    row.className = "row" + (sel.zone === "list" && sel.i === i ? " sel" : "");
    row.setAttribute("aria-label", s.label);

    var tile = document.createElement("div");
    tile.className = "tile";
    tile.style.setProperty("--h", hue(s.opt_key));
    tile.textContent = settingCode(s, settingsResults);

    var left = document.createElement("div");
    left.className = "row-left";
    // why: the label already packs "Category : Group : Label", so no separate eyebrow.
    var line = document.createElement("div");
    line.className = "row-line";
    var name = document.createElement("div");
    name.className = "row-name";
    name.textContent = s.label;
    line.appendChild(name);
    left.appendChild(line);

    row.appendChild(tile);
    row.appendChild(left);
    row.onclick = function () { sel = { zone: "list", i: i }; render({ resize: true }); };
    row.ondblclick = function () { sel = { zone: "list", i: i }; jumpToSetting(s); };
    return row;
}

// A command/action row (used for both recents and filtered results).
function renderActionRow(a, i) {
    var on = FAVS.indexOf(a.id) !== -1;
    var row = document.createElement("div");
    row.className = "row" + (sel.zone === "list" && sel.i === i ? " sel" : "");
    row.setAttribute("aria-label", actionLabel(a, ACTIONS));

    var tile = document.createElement("div");
    tile.className = "tile";
    tile.style.setProperty("--h", hue(a.id));
    tile.textContent = tileCode(a, ACTIONS);

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

function renderCommandsList() {
    var q = (query || "").trim();
    var list = currentList();
    if (sel.zone === "list")
        sel.i = Math.max(0, Math.min(sel.i, list.length - 1));
    // Recents are not filtered, so clear any stale match marks from a previous typed query.
    if (!shouldRenderActionList(query))
        matchIndex = {};
    listEl.innerHTML = "";

    if (!shouldRenderActionList(query) && list.length) {
        var head = document.createElement("div");
        head.className = "dial-group";
        head.textContent = "Recent";
        listEl.appendChild(head);
    }

    if (!list.length) {
        listEl.className = "dial-list empty";
        if (countEl) countEl.hidden = true;
        var empty = document.createElement("div");
        empty.className = "dial-empty";
        empty.textContent = shouldRenderActionList(query) ? ("No actions match (Total: " + ACTIONS.length + ")") : "No actions yet";
        listEl.appendChild(empty);
        return;
    }

    listEl.className = "dial-list";
    if (countEl) {
        countEl.hidden = false;
        countEl.textContent = shouldRenderActionList(query) ? resultCountText(ACTIONS.length, list.length, query) : list.length + " recent";
    }
    list.forEach(function (a, i) { listEl.appendChild(renderActionRow(a, i)); });
}

function renderSettingsList() {
    var q = (query || "").trim();
    listEl.innerHTML = "";
    // Empty query + recents -> show the recent settings under a "Recent" group header.
    var showingRecents = !q && settingsResults.length > 0;
    if (!q && !showingRecents) {
        listEl.className = "dial-list empty";
        if (countEl) countEl.hidden = true;
        var hint = document.createElement("div");
        hint.className = "dial-empty";
        hint.textContent = "Type to search print, filament and printer settings";
        listEl.appendChild(hint);
        return;
    }
    if (q && !settingsResults.length) {
        listEl.className = "dial-list empty";
        if (countEl) countEl.hidden = true;
        var empty = document.createElement("div");
        empty.className = "dial-empty";
        empty.textContent = "No settings match";
        listEl.appendChild(empty);
        return;
    }
    if (sel.zone === "list")
        sel.i = Math.max(0, Math.min(sel.i, settingsResults.length - 1));
    listEl.className = "dial-list";
    if (countEl) {
        countEl.hidden = false;
        countEl.textContent = showingRecents ? settingsResults.length + " recent" : settingsResults.length + " matches";
    }
    if (showingRecents) {
        var head = document.createElement("div");
        head.className = "dial-group";
        head.textContent = "Recent";
        listEl.appendChild(head);
    }
    settingsResults.forEach(function (s, i) { listEl.appendChild(renderSettingRow(s, i)); });
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

function renderList() {
    if (phase === "settings")
        renderSettingsList();
    else if (phase === "tab")
        renderTabList();
    else if (phase === "percent")
        renderPercentList();
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

// Activate an entry in the commands phase. Two-phase commands switch the palette to their input
// phase instead of running; everything else runs immediately.
function activateEntry(a) {
    if (!a) return;
    if (a.input === "settings") { enterSettingsPhase(); return; }
    if (a.input === "percent") { enterPercentPhase(); return; }
    if (a.input === "tab") { enterTabsPhase(); return; }
    run(a);
}

function runSelected() {
    if (phase === "percent") {
        runJumpToLayer(query.trim());
        return;
    }
    if (phase === "settings") {
        var s = settingsResults[sel.i];
        if (s) jumpToSetting(s);
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

function jumpToSetting(s) {
    SendMessage({ command: "go_to_setting", opt_key: s.opt_key, type: s.type, category: s.category || "", label: s.label || "", group: s.group || "" });
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

function enterSettingsPhase() {
    phase = "settings"; settingsResults = []; query = ""; qEl.value = "";
    sel = { zone: "list", i: 0 };
    qEl.placeholder = "Search settings...";
    syncClearButton();
    render({ resize: true, resetScroll: true });
    qEl.focus();
    SendMessage({ command: "search_settings", q: "" });
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
    phase = "commands"; settingsResults = []; tabOptions = []; query = ""; qEl.value = "";
    sel = { zone: "list", i: 0 };
    qEl.placeholder = "Search " + ACTIONS.length + " actions";
    syncClearButton();
    render({ resize: true, resetScroll: true });
    qEl.focus();
}

function focusInput() { setTimeout(function () { if (qEl) qEl.focus(); }, 0); }

// ---- init --------------------------------------------------------------------
function OnInit() {
    qEl = $("q"); listEl = $("list"); favEl = $("favBar"); clearEl = $("clear"); eyeEl = $("favEyebrow"); countEl = $("count");
    syncClearButton();

    $("clear").onclick = function () {
        query = ""; qEl.value = ""; sel = { zone: "list", i: 0 };
        if (phase === "settings") SendMessage({ command: "search_settings", q: "" });
        render({ resize: true, resetScroll: true }); qEl.focus();
        syncClearButton();
    };
    qEl.addEventListener("input", function () {
        query = qEl.value; sel = { zone: "list", i: 0 }; syncClearButton();
        if (phase === "settings") SendMessage({ command: "search_settings", q: query });
        render({ resize: true, resetScroll: true });
    });

    // why: dismiss the fav context menu on any click/scroll away from it (capture scroll to catch nested scrollers).
    document.addEventListener("click", hideFavMenu);
    document.addEventListener("scroll", hideFavMenu, true);

    document.addEventListener("keydown", function (e) {
        if (favMenuEl && !favMenuEl.hidden && e.key === "Escape") { e.preventDefault(); hideFavMenu(); return; }
        var list = currentList();
        // why: fav bar only exists in the commands phase; other phases are list-only, so an
        // ArrowUp at the top must not jump into a hidden fav zone.
        var favs = (phase === "commands") ? currentVisibleFavs() : [];
        // why: Up/Down always navigate; Left/Right only navigate the fav bar. In the list zone,
        //      let Left/Right fall through so they move the caret in the focused search field.
        var lr = e.key === "ArrowLeft" || e.key === "ArrowRight";
        if (e.key === "ArrowDown" || e.key === "ArrowUp" || (lr && sel.zone === "fav")) {
            // In the percent phase the input is the whole UI - arrows move the caret, not rows.
            if (phase === "percent") return;
            e.preventDefault();
            sel = nextSel(sel, e.key, list.length, favs.length);
            // why: entering/leaving the fav zone toggles the eyebrow line, changing launcher height;
            // resize so the popup grows/shrinks instead of clipping. requestResize no-ops when unchanged.
            render({ resize: true });
        } else if (e.key === "Enter") {
            e.preventDefault();
            if (phase === "percent") runJumpToLayer(query.trim());
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
