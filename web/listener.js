/* The listener's frame on the website: the apps' views (ios/App.swift, WalkPanel.swift, Chrome.swift,
   Cards.swift; the mock in design/app) over the same map and engine. On a phone the panel lies
   across the foot of the map; on a computer it floats over the map's left edge (Kerem, 2026-09-27).
   It reads and acts only through window.fsListen (index.html's listener bridge). Signed in,
   body.listener is off and all of this is hidden: the setter keeps the page's own chrome. */
(function () {
  var L = window.fsListen;
  if (!L) { return; }

  /* ---- small builders ---- */
  function h(tag, attrs, kids) {
    var e = document.createElement(tag);
    for (var k in attrs || {}) {
      if (k === "text") { e.textContent = attrs[k]; }
      else if (k === "on") { for (var ev in attrs.on) { e.addEventListener(ev, attrs.on[ev]); } }
      else if (attrs[k] !== null && attrs[k] !== undefined && attrs[k] !== false) { e.setAttribute(k, attrs[k] === true ? "" : attrs[k]); }
    }
    (kids || []).forEach(function (c) { if (c) { e.appendChild(typeof c === "string" ? document.createTextNode(c) : c); } });
    return e;
  }
  function ink(s) { return h("b", { text: s }); }
  function note(kids) { return h("p", { class: "ls-note" }, kids); }
  function btn(label, attrs, onClick) {
    attrs = attrs || {}; attrs.type = "button"; attrs.class = "ls-btn " + (attrs.class || ""); attrs.text = label;
    attrs.on = { click: onClick };
    return h("button", attrs);
  }
  function icon(d) {
    var s = document.createElementNS("http://www.w3.org/2000/svg", "svg");
    s.setAttribute("viewBox", "0 0 24 24"); s.setAttribute("aria-hidden", "true");
    var p = document.createElementNS("http://www.w3.org/2000/svg", "path");
    p.setAttribute("d", d); s.appendChild(p);
    return s;
  }
  var ICON = { layers: "M12 3 2 8.5 12 14l10-5.5L12 3Zm-10 9.5L12 18l10-5.5M2 16.5 12 22l10-5.5",
               person: "M12 12a4 4 0 1 0 0-8 4 4 0 0 0 0 8Zm-7.5 8.5c.8-3.6 3.9-5.5 7.5-5.5s6.7 1.9 7.5 5.5",
               close: "M6 6l12 12M18 6 6 18", photo: "M4 7h3l2-2h6l2 2h3v12H4zM12 17a4 4 0 1 0 0-8 4 4 0 0 0 0 8Z",
               play: "M8 5v14l11-7z", pause: "M7 5h4v14H7zM13 5h4v14h-4z",
               list: "M8 6h12M8 12h12M8 18h12M4 6h.01M4 12h.01M4 18h.01" };
  function round(name, label, onClick, attrs) {
    attrs = attrs || {};
    attrs.type = "button"; attrs.class = "ls-round " + (attrs.class || ""); attrs["aria-label"] = label; attrs.on = { click: onClick };
    return h("button", attrs, [icon(ICON[name])]);
  }
  function chip(text) { return h("span", { class: "ls-chip", text: text }); }
  function metres(d) { return d < 1000 ? Math.round(d) + " m" : (d / 1000).toFixed(1) + " km"; }
  function clock(s) { s = Math.max(0, Math.floor(s || 0)); return Math.floor(s / 60) + ":" + ("0" + s % 60).slice(-2); }
  function sheetTitle(title) {
    return h("div", { class: "ls-sheethead" }, [h("h2", { class: "ls-title", text: title }), round("close", "Close", closeView, { class: "ls-close" })]);
  }

  /* ---- the frame ---- */
  var placeName = h("span", { id: "ls-place-name" });
  var top = h("div", { id: "ls-top" }, [
    h("button", { id: "ls-place", type: "button", "aria-label": "Places", on: { click: function () { toggleView("places"); } } },
      [placeName, h("span", { class: "ls-caret", text: "▾" })]),
    h("span", { class: "ls-gap" }),
    round("list", "Archive", function () { toggleView("archive"); }, { id: "ls-archive", hidden: true }),
    round("layers", "Layers", function () { toggleView("layers"); }, { id: "ls-layers" }),
    round("person", "Account", function () { toggleView("account"); }, { id: "ls-account" })]);
  var cellsSlot = h("div", { id: "ls-cells", hidden: true });
  var grip = h("button", { id: "ls-grip", type: "button", "aria-label": "Hide the panel", on: { click: function () { folded = !folded; render(); } } }, [h("span")]);
  var walk = h("div", { id: "ls-walk" }), bar = h("div", { id: "ls-bar", hidden: true }), holder = h("div", { id: "ls-holder" });
  /* the setter's: the drawing modes under the grip, the publish bar at the foot (both moved in) */
  var tools = h("div", { id: "ls-tools", hidden: true }), pub = h("div", { id: "ls-pub", hidden: true });
  var panel = h("section", { id: "ls-panel", "aria-label": "Walk" }, [grip, tools, walk, holder, bar, pub]);
  var viewer = h("div", { id: "ls-photos", hidden: true, role: "dialog", "aria-label": "Photos" });
  var root = h("div", { id: "ls" }, [top, cellsSlot, panel, viewer]);
  document.getElementById("map").appendChild(root);
  /* the setter card's photo viewer is a dialog that lived in the retired sidebar, where a hidden
     ancestor kept it from ever showing: it moves to the body, once */
  var pv = document.getElementById("photo-view");
  if (pv) { document.body.appendChild(pv); }
  cellsSlot.appendChild(L.cells());

  /* ---- the setter: the page's own blocks, moved in with every handler they have ---- */
  var homes = [];          /* [{ el, parent, next, hidden, view }] */
  /* show: unhide it while it is here (the card, the archive list); otherwise the page keeps
     deciding (the icon picker shows in Point mode only, Undo delete only when there is one) */
  function move(el, into, forView, show) {
    if (!el) { return; }
    if (!homes.some(function (x) { return x.el === el; })) {
      homes.push({ el: el, parent: el.parentNode, next: el.nextSibling, hidden: el.hidden, view: !!forView, show: !!show });
    }
    if (show) { el.hidden = false; }
    into.appendChild(el);
  }
  function sendHome(onlyViews) {
    homes = homes.filter(function (x) {
      if (onlyViews && !x.view) { return true; }
      x.parent.insertBefore(x.el, x.next && x.next.parentNode === x.parent ? x.next : null);
      if (x.show) { x.el.hidden = x.hidden; }
      return false;
    });
  }
  function $$(sel) { return document.querySelector(sel); }
  var setter = false, lastMode = "select", cardTab = "main";
  function showPart() {
    var c = $$("#card"), tabs = $$("#ls-cardtabs");
    if (!c || !tabs) { return; }
    if (!tabs.querySelector("[data-ct='" + cardTab + "']")) { cardTab = "main"; }   /* a route has no Photos */
    [].forEach.call(tabs.children, function (b) { b.setAttribute("aria-selected", String(b.dataset.ct === cardTab)); });
    [].forEach.call(c.children, function (el) { el.classList.toggle("ls-ct-off", !!el.dataset.ct && el.dataset.ct !== cardTab); });
    if (window.fsNoteFit) { fsNoteFit(); }
  }
  function setterChanged(on) {
    setter = on;
    if (on) { move($$("#mode-section .modes"), tools); move($$("#mode-icons"), tools); move($$("#publishbar"), pub); move($$("#saved"), pub); }
    else { view = null; holder.textContent = ""; sendHome(false); }
    tools.hidden = pub.hidden = !on;
    document.getElementById("ls-archive").hidden = !on;
  }

  /* what the panel holds: null = the walk, or "places" / "layers" / "account" / "archive" / {point} / {route} / {setter} */
  var view = null, folded = false, lastRoute = null;
  function toggleView(v) { if (view === v) { closeView(); } else { openView(v); } }
  function openView(v) { sendHome(true); view = v; folded = false; buildView(); render(); }
  /* a card's close: it pops off and the panel folds down with it (the apps' closeCard) */
  function closeView() {
    var card = view && typeof view === "object";
    view = null;
    sendHome(true);
    holder.textContent = "";
    if (card) { L.stopPlaying(); L.select(null); folded = true; }
    render();
  }
  L.onSelect(function (id) {
    if (!id) { if (view && typeof view === "object") { sendHome(true); view = null; holder.textContent = ""; render(); } return; }
    if (setter) { if (!(view && view.setter === id)) { openView({ setter: id }); } return; }
    if (L.point(id)) { openView({ point: id }); } else if (L.route(id)) { openView({ route: id }); }
  });

  /* ---- the walk ---- */
  function soundButton(st) {
    return btn(st.sound ? "Stop" : st.starting ? "…" : "Sound", { id: "ls-sound", "aria-label": st.sound ? "Stop the sound" : "Play the sound" },
               function () { L.sound(); });
  }
  function gpsChip(st) {
    if (!st.gps.on) { return chip("BY HAND"); }
    if (st.gps.acc === null) { return null; }
    return chip("±" + Math.round(st.gps.acc) + " m" + (st.gps.held ? " · HOLDING" : ""));
  }
  function chordText(st) {
    var c = st.live && st.live.chord;
    return c && c.step >= 0 && st.core && st.core.route ? "chord " + (c.step + 1) + " of " + c.count + ", " + c.label : null;
  }
  function row(r) {
    var fill = h("i", { style: "width:" + Math.round(Math.max(0, Math.min(1, r.level)) * 100) + "%" });
    if (!(r.sounding && r.loaded)) { fill.className = "ls-quiet"; }
    return h("button", { type: "button", class: "ls-row", "data-id": r.id, on: { click: function () { L.select(r.id); } } }, [
      h("span", { class: "ls-rowline" }, [h("span", { class: "ls-lamp" }), h("span", { class: "ls-name", text: r.name }),
        h("span", { class: "ls-dist", text: r.sounding && !r.loaded ? "Preparing" : metres(r.dist) }), h("span", { class: "ls-chev", text: "›" })]),
      h("span", { class: "ls-level" }, [fill])]);
  }
  function renderWalk(st) {
    var kids = [h("div", { class: "ls-head" }, [h("h2", { class: "ls-title", text: st.place || "Fieldscape" }), soundButton(st), gpsChip(st)])];
    if (setter && L.mode() === "point") { kids.push(note([ink("Tap the map to place a point."), " Its card opens, to name it and give it a sound."])); }
    if (setter && L.mode() === "route") {
      var nv = L.draftCount();
      kids.push(h("div", { class: "ls-routeline ls-drawing" }, [
        note([ink("Drawing a route · "), nv ? nv + " point" + (nv > 1 ? "s" : "") : "tap the map for its first point"]),
        btn("Undo", { id: "ls-draw-undo", disabled: !nv }, function () { L.undoVertex(); }),
        btn("Done", { id: "ls-draw-done", class: "ls-primary" }, function () { L.finishDraft(); })]));
    }
    var route = st.core && st.core.route, rh = (st.core && st.core.rhythms) || [];
    var solo = L.listening(), sp = solo && L.point(solo);
    /* soloed, the route and the other points rest: the panel names who is playing instead */
    if ((route || rh.length) && !sp) {
      var line = [], chord = chordText(st);
      if (route) { line.push("Route ", ink(route)); if (chord) { line.push(" · " + chord); } }
      if (rh.length) { line.push(route ? " · rhythm " : "Rhythm ", ink(rh.join(", "))); }
      var cellsBtn = route ? btn("Cells", { id: "ls-cells-btn", class: "ls-toggle", "aria-pressed": String(L.cellsOn()), "aria-label": "Morph cells" },
                                 function () { L.cellsSet(!L.cellsOn()); render(); }) : null;
      kids.push(h("div", { class: "ls-routeline" }, [note(line), cellsBtn]));
    }
    if (sp) {
      kids.push(h("div", { class: "ls-routeline" }, [note(["Listening to ", ink(sp.name), " alone."]),
        btn("Everything", { "aria-label": "Listen to everything again" }, function () { L.listen(null); })]));
    }
    if (!st.rows.length) {
      kids.push(note([st.anyPoints ? "Finding where you are…" : "No recordings are published yet."]));
    } else {
      kids.push(h("div", { class: "ls-rows" }, st.rows.map(row)));
      if (!solo && !st.rows.some(function (r) { return r.sounding; }) && st.nearest) {
        kids.push(note([ink("Nothing in range here. "), "Walk toward " + st.nearest.name + ", " + st.nearest.dir + "."]));
      }
    }
    if (!st.gps.on) {
      kids.push(note(["Listening from where you put yourself. Drag your dot, or press and hold anywhere, to walk."]));
      kids.push(btn("Use my location", { id: "ls-locate", class: "ls-wide" }, function () { L.useLocation(); }));
    } else if (st.gps.held) {
      kids.push(note(["GPS is too rough here, so the sound holds where you last were until the fix is better than ",
                      h("span", { class: "ls-mono", text: "40 m" }), "."]));
    }
    return kids;
  }
  function renderBar(st) {
    var chord = chordText(st), solo = L.listening(), sp = solo && L.point(solo);
    return [h("button", { type: "button", class: "ls-bartext", "aria-label": "Show the panel", on: { click: function () { folded = false; render(); } } }, [
              h("span", { class: "ls-title", text: sp ? "Listening to " + sp.name : st.place || "Fieldscape" }),
              chord ? h("span", { class: "ls-note", text: chord }) : null]), soundButton(st)];
  }

  /* ---- Places: search, parks with what they hold, routes ---- */
  function buildPlaces() {
    var list = h("div", { class: "ls-scroll" });
    var search = h("input", { type: "search", class: "ls-search", placeholder: "Search parks and routes", "aria-label": "Search parks and routes" });
    function fill() {
      var q = search.value.trim().toLocaleLowerCase("tr");
      var has = function (n) { return !q || n.toLocaleLowerCase("tr").indexOf(q) >= 0; };
      var routes = L.routes().filter(function (r) { return has(r.name); });
      /* Only the parks a setter set something in (Kerem, 2026-09-29); a setter's All parks switch
         brings the rest of the city back, with the page's own park picker under it. */
      var all = setter && allParks();
      var parks = L.parks().filter(function (p) { return has(p.name) && (p.held || all); });
      list.textContent = "";
      if (setter) {
        list.appendChild(h("button", { type: "button", class: "ls-item ls-switch", "data-ls-allparks": "", "aria-pressed": String(all),
                                       on: { click: function () { allParks(!all); sendHome(true); buildView(); } } },
                           [h("span", { class: "ls-name", text: "All parks" }), h("span", { class: "ls-knob" })]));
      }
      list.appendChild(h("p", { class: "ls-eyebrow", text: "Routes" }));
      list.appendChild(h("div", { class: "ls-routes" }, routes.length ? routes.map(function (r) {
        return h("button", { type: "button", class: "ls-item", on: { click: function () { L.select(r.id); L.frame(r.id); } } },
                 [h("span", { class: "ls-name", text: r.name }), h("span", { class: "ls-dist", text: metres(r.metres) }), h("span", { class: "ls-chev", text: "›" })]);
      }) : [note([q ? "No route by that name." : "No routes are published yet."])]));
      list.appendChild(h("p", { class: "ls-eyebrow", text: all ? "Parks" : "Parks with recordings" }));
      list.appendChild(h("div", { class: "ls-parks" }, parks.slice(0, 40).map(function (p) {
        var what = p.held ? [p.held.points ? p.held.points + " point" + (p.held.points > 1 ? "s" : "") : "", p.held.routes ? p.held.routes + " route" + (p.held.routes > 1 ? "s" : "") : ""].filter(Boolean).join(" · ") : "";
        return h("button", { type: "button", class: "ls-item", on: { click: function () { closeView(); L.frame(p.id); } } },
                 [h("span", { class: "ls-name", text: p.name }), h("span", { class: "ls-dist", text: what }), h("span", { class: "ls-chev", text: "›" })]);
      })));
    }
    search.addEventListener("input", fill);
    fill();
    return [sheetTitle("Places"), search, list];
  }

  function allParks(on) {
    try { if (on === undefined) { return localStorage.getItem("fs.allParks") === "1"; } localStorage.setItem("fs.allParks", on ? "1" : "0"); }
    catch (e) { /* private window: the switch just stays off */ }
    return false;
  }

  /* ---- Archive (setters): the page's own list, filter and sort ---- */
  function buildArchive() {
    var box = h("div", { class: "ls-scroll ls-archive" });
    setTimeout(function () { move($$("#pane-list"), box, true, true); L.renderArchive(); }, 0);
    return [sheetTitle("Archive"), box];
  }

  /* ---- Layers ---- */
  function buildLayers() {
    var s = L.layers();
    var bases = [["osm", "Map"], ["topo", "Topo"], ["sat", "Satellite"], ["virtual", "Virtual"]];
    function toggle(label, on, set, attrs) {
      attrs.type = "button"; attrs.class = "ls-item ls-switch"; attrs["aria-pressed"] = String(on);
      attrs.on = { click: function () { set(!on); buildView(); } };
      return h("button", attrs, [h("span", { class: "ls-name", text: label }), h("span", { class: "ls-knob" })]);
    }
    return [sheetTitle("Layers"),
      h("div", { class: "ls-seg", role: "group", "aria-label": "Base map" }, bases.map(function (b) {
        return h("button", { type: "button", "data-ls-base": b[0], "aria-pressed": String(s.base === b[0]),
                             on: { click: function () { L.setBase(b[0]); buildView(); } } }, [b[1]]);
      })),
      h("div", { class: "ls-list" }, [
        toggle("Park boundary", s.boundary, L.setBoundary, { "data-ls-layer": "boundary" }),
        toggle("Zones", s.zones, function (v) { L.setLayer("zones", v); }, { "data-ls-layer": "zones" }),
        toggle("Sections", s.sections, function (v) { L.setLayer("sections", v); }, { "data-ls-layer": "sections" })]),
      btn("Fit all", { class: "ls-wide" }, function () { L.fitAll(); })];
  }

  /* ---- Account: the build line, and the setter's quiet door ---- */
  function buildAccount() {
    var email = h("input", { type: "email", class: "ls-search", placeholder: "you@example.com", autocomplete: "email", "aria-label": "Setter email" });
    var said = note([""]);          /* filled once a link is sent: the page's own line is setter wording */
    /* Offline: the park's recordings and map tiles on this device, so a walk plays with no signal
       (the page's own Download map: it sizes first, then a second tap fetches) */
    var park = L.offlinePark();
    var off = btn(park ? "Download " + park : "Download for offline", { class: "ls-wide ls-offline", disabled: !park },
                  function () { L.offline(); tick(); });
    var offNote = note([park ? "Its recordings and map tiles, kept on this device, so the walk plays with no signal." :
                               "Walk into a park, or choose one in Places, to keep it for offline."]);
    function tick() {
      if (!off.isConnected) { return; }
      var st = L.offlineState();
      if (st.label && st.label !== "Download map") { off.textContent = st.label; }
      if (st.note) { offNote.textContent = st.note; }
      off.disabled = st.busy;
      setTimeout(tick, 400);
    }
    if (setter) {
      return [sheetTitle("Account"), h("p", { class: "ls-eyebrow", text: "Offline" }), offNote, off,
              h("p", { class: "ls-fine" }, [h("a", { href: "privacy.html", text: "Privacy" }), " · Fieldscape © Kerem Altaylar · recordings © their authors · map imagery © Esri, © OpenStreetMap contributors, © OpenTopoMap"])];
    }
    return [sheetTitle("Account"),
      note(["Fieldscape is for listening: walk, and the recordings around you play. Setters place points, record and shape the sound."]),
      h("p", { class: "ls-eyebrow", text: "Setter sign-in" }),
      h("div", { class: "ls-routeline" }, [email, btn("Send link", {}, function () { if (email.value) { L.signIn(email.value); said.textContent = "A sign-in link is on its way to " + email.value + "."; } })]),
      said,
      h("p", { class: "ls-eyebrow", text: "Offline" }), offNote, off,
      h("p", { class: "ls-fine" }, [h("a", { href: "privacy.html", text: "Privacy" }), " · Fieldscape © Kerem Altaylar · recordings © their authors · map imagery © Esri, © OpenStreetMap contributors, © OpenTopoMap"])];
  }

  /* ---- the cards ---- */
  function cardHead(kind, distance, extra) {
    return h("div", { class: "ls-cardhead" }, [round("close", "Close", closeView, { class: "ls-close" }), chip(kind), h("span", { class: "ls-gap" }),
                                               distance ? chip(distance) : null, extra || null]);
  }
  function player(key, path, label) {
    var pl = L.playing(), mine = pl && pl.key === key, on = mine && !pl.paused;
    return h("div", { class: "ls-player" }, [
      round(on ? "pause" : "play", on ? "Pause the recording" : label, function () { L.play(key, path); render(); }, { class: "ls-play" }),
      h("span", { class: "ls-name", text: label }),
      h("span", { class: "ls-dist ls-pos", "data-key": key, text: mine && pl.loading ? "Loading…" : "" })]);
  }
  function renderPoint(id) {
    var q = L.point(id);
    if (!q) { return [note(["This point is no longer published."])]; }
    var photos = q.images ? round("photo", "Photos, " + q.images, function () { openPhotos(id); }, { class: "ls-photo" })
                          : h("button", { type: "button", class: "ls-round ls-photo", disabled: true, "aria-label": "No photos yet" }, [icon(ICON.photo)]);
    var kids = [cardHead(q.mode.toUpperCase(), q.dist !== null ? metres(q.dist) + " away" : null, photos),
                h("h2", { class: "ls-cardtitle", text: q.name })];
    if (q.note) { kids.push(note([q.note])); }
    if (q.mode === "stretch" || q.mode === "grains") {
      var c = h("canvas", { class: "ls-wave", "aria-hidden": "true", "data-peaks": q.id });
      kids.push(c);
      kids.push(h("p", { class: "ls-meta", text: [q.recorded ? "Recorded " + q.recorded : "", q.duration ? clock(q.duration) : ""].filter(Boolean).join(" · ") }));
    }
    if (q.mode !== "silent" && L.canListen()) {
      var soloed = L.listening() === id;
      kids.push(btn(soloed ? "Stop listening" : "Listen", { class: "ls-wide ls-listen" + (soloed ? " ls-on" : "") },
                    function () { L.listen(soloed ? null : id); render(); }));
      kids.push(h("p", { class: "ls-fine", text: soloed ? "Only this point plays, at its full level, wherever you are." :
                                                 "Hear this point alone, at its full level, from wherever you are." }));
    }
    if (q.mode === "rhythm") {
      kids.push(h("p", { class: "ls-eyebrow", text: "The four original recordings" }));
      q.hits.forEach(function (x) { kids.push(player(id + "#" + x.slot, x.path, x.name)); });
    } else if (q.path) {
      kids.push(player(id, q.path, "Play the recording as it was made"));
    }
    return kids;
  }
  function renderRoute(id) {
    var r = L.route(id);
    if (!r) { return [note(["This route is no longer published."])]; }
    var kids = [cardHead("ROUTE", metres(r.metres)), h("h2", { class: "ls-cardtitle", text: r.name })];
    if (r.note) { kids.push(note([r.note])); }
    kids.push(h("p", { class: "ls-eyebrow", text: "The progression — " + r.prog.length + " chords along the route" }));
    kids.push(h("div", { class: "ls-prog" }, r.prog.map(function (c, i) {
      return h("span", { class: "ls-chord" + (i === r.at ? " ls-at" : ""), style: "--c:" + L.colour(c.pc), title: c.label, text: c.label });
    })));
    if (r.at >= 0) { kids.push(note(["You are at chord ", ink(String(r.at + 1)), ", ", ink(r.prog[r.at].label), "."])); }
    kids.push(h("div", { class: "ls-facts" }, [
      h("span", {}, [h("span", { class: "ls-eyebrow", text: "Key" }), ink(r.key)]),
      h("span", {}, [h("span", { class: "ls-eyebrow", text: "Tempo" }), ink(r.tempo + " bpm")]),
      h("span", {}, [h("span", { class: "ls-eyebrow", text: "Sections" }), ink(String(r.sections))])]));
    kids.push(btn("Show whole route", { class: "ls-wide" }, function () { L.frame(id); }));
    return kids;
  }

  /* the photos, full screen; tap anywhere to close */
  function openPhotos(id) {
    viewer.textContent = ""; viewer.hidden = false;
    viewer.appendChild(round("close", "Close the photos", function () { viewer.hidden = true; viewer.textContent = ""; }, { class: "ls-close" }));
    var strip = h("div", { class: "ls-strip" }, [note(["Loading…"])]);
    viewer.appendChild(strip);
    L.photos(id).then(function (urls) {
      strip.textContent = "";
      if (!urls.length) { strip.appendChild(note(["The photos could not be loaded."])); return; }
      urls.forEach(function (u, i) { strip.appendChild(h("img", { src: u, alt: "Photo " + (i + 1) + " of " + urls.length })); });
    });
  }

  /* ---- rendering ---- */
  /* Live data arrives ~30 times a second; rebuilding on each would replace a button between the
     finger going down and coming up, and the tap would be lost. Swapped only when what it shows
     changes (the handlers read nothing stale: they act through L alone). */
  function replace(el, kids) {
    var next = document.createElement("div");
    kids.forEach(function (k) { if (k) { next.appendChild(k); } });
    /* against what was last rendered, not the live DOM: a drawn canvas gains attributes */
    if (next.innerHTML === el._sig) { return false; }
    el._sig = next.innerHTML;
    while (el.firstChild) { el.removeChild(el.firstChild); }
    while (next.firstChild) { el.appendChild(next.firstChild); }
    return true;
  }
  /* sheets are built when opened (they hold inputs that must keep their focus); cards live-update */
  function buildView() {
    holder.textContent = "";
    if (!view) { return; }
    var kids = view === "places" ? buildPlaces() : view === "layers" ? buildLayers() : view === "account" ? buildAccount() :
               view === "archive" ? buildArchive() : [];
    var box = h("div", { id: typeof view === "object" ? "ls-card" : "ls-sheet", class: typeof view === "object" ? "ls-card" : "ls-sheet" }, kids);
    holder.appendChild(box);
    /* the setter's card is the page's own, every field and handler as it was */
    if (view.setter) {
      /* close and the part switch share one row (Kerem, 2026-09-28: a shorter window still scrolled) */
      var head = h("div", { class: "ls-cardhead" }, [round("close", "Close", closeView, { class: "ls-close" })]);
      box.appendChild(head);
      var card = h("div", { class: "ls-setcard" });
      box.appendChild(card);
      move($$("#card"), card, true, true);
      /* No scroll bar (Kerem, 2026-09-27): the card is taller than the panel, so a switch shows one part
         at a time, and the action row (Sound / Patch, Zoom to, Delete) stays under every part */
      var isRoute = !L.point(view.setter);
      var parts = [["main", isRoute ? "Route" : "Point"]].concat(isRoute ? [] : [["photos", "Photos"]], [["sound", "Sound"], ["where", "Where"]]);
      var tabs = h("div", { id: "ls-cardtabs", role: "tablist", "aria-label": "Card" }, parts.map(function (t) {
        return h("button", { type: "button", role: "tab", "data-ct": t[0], text: t[1], on: { click: function () { cardTab = t[0]; showPart(); } } });
      }));
      head.appendChild(tabs);
      /* Photos on their own part (Kerem, 2026-09-27): with a note and photos the Point part scrolled again */
      var PART = { main: [".cardhead", "#f-note", "#g-type", "#f-tags"], photos: ["#g-photos"], sound: [".rec", ".mixrow"],
                   where: ["#f-meta", ".chips:not(.mixrow)", "#f-walk-note"] };
      [].forEach.call($$("#card").children, function (el) {
        el.dataset.ct = "";
        for (var k in PART) { if (PART[k].some(function (sel) { return el.matches(sel) || !!el.querySelector(sel); })) { el.dataset.ct = k; } }
      });
      showPart();
    }
    if (setter && view === "account") {
      var more = h("div", { class: "ls-setacct" });
      box.insertBefore(more, box.children[1]);
      move($$("#setter"), more, true); move($$("#storage > .actions"), more, true); move($$("#undo"), more, true); move($$("#audit"), more, true);
    }
    if (setter && view === "places") {
      var world = h("div", { class: "ls-setplaces" + (allParks() ? "" : " ls-fewparks") });
      box.insertBefore(world, box.children[1]);
      move($$(".worldrow"), world, true); move($$("#place-list-label"), world, true); move($$("#place-list"), world, true);
    }
  }
  function render() {
    var st = L.state();
    if (!!st.signedIn !== setter) { setterChanged(!!st.signedIn); }
    /* choosing Point or Route opens the panel: its instructions, Undo and Done live there */
    var m = setter ? L.mode() : "select";
    if (m !== lastMode) { lastMode = m; if (m !== "select" && view && typeof view !== "object") { view = null; sendHome(true); holder.textContent = ""; } }
    if (m !== "select") { folded = false; }
    grip.hidden = m !== "select";
    placeName.textContent = st.place || "Open world";
    var card = view && typeof view === "object";
    walk.hidden = folded || !!view; holder.hidden = folded || !view; bar.hidden = !folded;
    panel.classList.toggle("ls-tall", !!view && !folded);
    grip.setAttribute("aria-label", folded ? "Show the panel" : "Hide the panel");
    if (folded) { replace(bar, renderBar(st)); }
    else if (!view) { replace(walk, renderWalk(st)); }
    else if (card && !view.setter) {
      var box = holder.firstChild;
      if (box && replace(box, view.point ? renderPoint(view.point) : renderRoute(view.route))) {
        [].forEach.call(box.querySelectorAll("canvas[data-peaks]"), function (c) { L.wave(c, (L.point(c.getAttribute("data-peaks")) || {}).peaks); });
      }
      /* the player's clock moves in place, so its buttons are not rebuilt while it plays */
      var pl = L.playing();
      if (box) { [].forEach.call(box.querySelectorAll(".ls-pos"), function (e) {
        var k = e.getAttribute("data-key");
        e.textContent = pl && pl.key === k ? (pl.loading ? "Loading…" : clock(pl.pos) + " / " + clock(pl.dur)) : "";
      }); }
    }
    /* each route opens its cells as its setter saved them (morph.cells), as the apps and the web do */
    /* read only while the engine runs: a Sound off and on is not a new route, and must not undo the
       listener's own Cells choice */
    var route = st.live ? (st.live.n >= 0 ? st.live.route : null) : lastRoute;
    if (st.live && route !== lastRoute) { lastRoute = route; if (route !== null) { L.cellsSet(!!st.live.shown); } }
    if (!st.live) { route = null; }
    var show = route !== null && L.cellsOn() && !view;
    if (cellsSlot.hidden === show) { cellsSlot.hidden = !show; if (show) { requestAnimationFrame(L.cellsResize); } }
    place();
    L.follow();
  }
  /* on a phone the cells ride just above the panel */
  function place() {
    cellsSlot.style.bottom = matchMedia("(min-width: 861px)").matches ? "" : (panel.offsetHeight + 12) + "px";
  }
  if (window.ResizeObserver) { new ResizeObserver(place).observe(panel); }
  addEventListener("resize", function () { place(); L.cellsResize(); });

  /* press and hold anywhere on the map puts the walker there (the apps' gesture); a drag or a
     second finger is the map's own */
  (function () {
    var canvas = document.querySelector("#map .maplibregl-canvas-container") || document.getElementById("map");
    var HOLD = 550, t = null, x0 = 0, y0 = 0, t0 = 0;
    function cancel() { clearTimeout(t); t = null; }
    function hold() {
      cancel();
      var r = document.getElementById("map").getBoundingClientRect();
      L.moveToScreen(x0 - r.left, y0 - r.top);
      if (navigator.vibrate) { navigator.vibrate(12); }
    }
    canvas.addEventListener("pointerdown", function (e) {
      if ((setter && L.mode() !== "select") || !e.isPrimary || e.target.closest(".maplibregl-marker")) { return; }
      x0 = e.clientX; y0 = e.clientY; t0 = performance.now(); cancel();
      t = setTimeout(hold, HOLD);
    });
    canvas.addEventListener("pointermove", function (e) { if (t && Math.hypot(e.clientX - x0, e.clientY - y0) > 8) { cancel(); } });
    /* a busy page can run the timer late: a release after HOLD still counts as the hold it was */
    canvas.addEventListener("pointerup", function () { if (t && performance.now() - t0 >= HOLD) { hold(); } else { cancel(); } });
    ["pointercancel", "pointerleave"].forEach(function (n) { canvas.addEventListener(n, cancel); });
    canvas.addEventListener("contextmenu", function (e) { e.preventDefault(); });
  })();

  L.onChange(render);
  render();
})();
