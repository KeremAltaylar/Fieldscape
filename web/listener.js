/* The listener's frame on the website: the apps' views (ios/App.swift, ios/WalkPanel.swift, the
   mock in design/app) over the same map and engine. On a phone the panel lies across the foot of
   the map; on a computer it floats over the map's left edge (Kerem, 2026-09-27). It reads and acts
   only through window.fsListen (index.html's listener bridge) and draws nothing of its own but DOM.
   Signed in, body.listener is off and all of this is hidden: the setter keeps the page's own chrome. */
(function () {
  var L = window.fsListen;
  if (!L) { return; }

  function h(tag, attrs, kids) {
    var e = document.createElement(tag);
    for (var k in attrs || {}) {
      if (k === "text") { e.textContent = attrs[k]; } else if (attrs[k] !== null && attrs[k] !== undefined) { e.setAttribute(k, attrs[k]); }
    }
    (kids || []).forEach(function (c) { if (c) { e.appendChild(typeof c === "string" ? document.createTextNode(c) : c); } });
    return e;
  }
  function ink(s) { return h("b", { text: s }); }
  function note(kids) { return h("p", { class: "ls-note" }, kids); }
  function icon(d) {
    var s = document.createElementNS("http://www.w3.org/2000/svg", "svg");
    s.setAttribute("viewBox", "0 0 24 24"); s.setAttribute("aria-hidden", "true");
    var p = document.createElementNS("http://www.w3.org/2000/svg", "path");
    p.setAttribute("d", d); s.appendChild(p);
    return s;
  }
  function metres(d) { return d < 1000 ? Math.round(d) + " m" : (d / 1000).toFixed(1) + " km"; }

  /* the frame */
  var placeName = h("span", { id: "ls-place-name" });
  var top = h("div", { id: "ls-top" }, [
    h("button", { id: "ls-place", type: "button", "aria-label": "Places" }, [placeName, h("span", { class: "ls-caret", text: "▾" })]),
    h("span", { class: "ls-gap" }),
    h("button", { id: "ls-layers", type: "button", class: "ls-round", "aria-label": "Layers" },
      [icon("M12 3 2 8.5 12 14l10-5.5L12 3Zm-10 9.5L12 18l10-5.5M2 16.5 12 22l10-5.5")]),
    h("button", { id: "ls-account", type: "button", class: "ls-round", "aria-label": "Account" },
      [icon("M12 12a4 4 0 1 0 0-8 4 4 0 0 0 0 8Zm-7.5 8.5c.8-3.6 3.9-5.5 7.5-5.5s6.7 1.9 7.5 5.5")])]);
  var cellsSlot = h("div", { id: "ls-cells", hidden: "" });
  var grip = h("button", { id: "ls-grip", type: "button", "aria-label": "Hide the panel" }, [h("span")]);
  var walk = h("div", { id: "ls-walk" }), bar = h("div", { id: "ls-bar", hidden: "" });
  var panel = h("section", { id: "ls-panel", "aria-label": "Walk" }, [grip, walk, bar]);
  var root = h("div", { id: "ls" }, [top, cellsSlot, panel]);
  document.getElementById("map").appendChild(root);
  cellsSlot.appendChild(L.cells());

  var folded = false, lastRoute = null;
  grip.addEventListener("click", function () { folded = !folded; render(); });

  function soundButton(st) {
    var b = h("button", { id: "ls-sound", type: "button", class: "ls-btn", "aria-label": st.sound ? "Stop the sound" : "Play the sound",
                          text: st.sound ? "Stop" : st.starting ? "…" : "Sound" });
    b.addEventListener("click", function () { L.sound(); });
    return b;
  }
  function chip(st) {
    if (!st.gps.on) { return h("span", { class: "ls-chip", text: "BY HAND" }); }
    if (st.gps.acc === null) { return null; }
    return h("span", { class: "ls-chip", text: "±" + Math.round(st.gps.acc) + " m" + (st.gps.held ? " · HOLDING" : "") });
  }
  function chordText(st) {
    var c = st.live && st.live.chord;
    return c && c.step >= 0 && st.core && st.core.route ? "chord " + (c.step + 1) + " of " + c.count + ", " + c.label : null;
  }
  function row(r) {
    var fill = h("i", { style: "width:" + Math.round(Math.max(0, Math.min(1, r.level)) * 100) + "%" });
    if (!(r.sounding && r.loaded)) { fill.className = "ls-quiet"; }
    return h("div", { class: "ls-row" }, [
      h("div", { class: "ls-rowline" }, [h("span", { class: "ls-lamp" }), h("span", { class: "ls-name", text: r.name }),
        h("span", { class: "ls-dist", text: r.sounding && !r.loaded ? "Preparing" : metres(r.dist) })]),
      h("div", { class: "ls-level" }, [fill])]);
  }

  function renderWalk(st) {
    var kids = [h("div", { class: "ls-head" }, [h("h2", { class: "ls-title", text: st.place || "Fieldscape" }), soundButton(st), chip(st)])];
    var route = st.core && st.core.route, rh = (st.core && st.core.rhythms) || [];
    /* the route's own sound and the rhythm points in reach (the piece) */
    if (route || rh.length) {
      var line = [];
      var chord = chordText(st);
      if (route) { line.push("Route ", ink(route)); if (chord) { line.push(" · " + chord); } }
      if (rh.length) { line.push(route ? " · rhythm " : "Rhythm ", ink(rh.join(", "))); }
      var cellsBtn = null;
      if (route) {
        cellsBtn = h("button", { id: "ls-cells-btn", type: "button", class: "ls-btn ls-toggle", "aria-pressed": String(L.cellsOn()),
                                 "aria-label": "Morph cells", text: "Cells" });
        cellsBtn.addEventListener("click", function () { L.cellsSet(!L.cellsOn()); render(); });
      }
      kids.push(h("div", { class: "ls-routeline" }, [note(line), cellsBtn]));
    }
    if (!st.rows.length) {
      kids.push(note([st.anyPoints ? "Finding where you are…" : "No recordings are published yet."]));
    } else {
      kids.push(h("div", { class: "ls-rows" }, st.rows.map(row)));
      var hearing = st.rows.some(function (r) { return r.sounding; });
      if (!hearing && st.nearest) { kids.push(note([ink("Nothing in range here. "), "Walk toward " + st.nearest.name + ", " + st.nearest.dir + "."])); }
    }
    if (!st.gps.on) {
      kids.push(note(["Listening from where you put yourself. Drag your dot, or press and hold anywhere, to walk."]));
      var use = h("button", { id: "ls-locate", type: "button", class: "ls-btn ls-wide", text: "Use my location" });
      use.addEventListener("click", function () { L.useLocation(); });
      kids.push(use);
    } else if (st.gps.held) {
      kids.push(note(["GPS is too rough here, so the sound holds where you last were until the fix is better than ",
                      h("span", { class: "ls-mono", text: "40 m" }), "."]));
    }
    return kids;
  }
  function renderBar(st) {
    var chord = chordText(st);
    return [h("div", { class: "ls-bartext" }, [h("h2", { class: "ls-title", text: st.place || "Fieldscape" }),
                                               chord ? h("p", { class: "ls-note", text: chord }) : null]), soundButton(st)];
  }
  /* Live data arrives ~30 times a second; rebuilding on each would replace a button between the
     finger going down and coming up, and the tap would be lost. Swapped only when what it shows
     changes (the handlers read nothing stale: they act through L alone). */
  function replace(el, kids) {
    var next = document.createElement("div");
    kids.forEach(function (k) { if (k) { next.appendChild(k); } });
    if (next.innerHTML === el.innerHTML) { return; }
    while (el.firstChild) { el.removeChild(el.firstChild); }
    while (next.firstChild) { el.appendChild(next.firstChild); }
  }

  function render() {
    var st = L.state();
    placeName.textContent = st.place || "Open world";
    walk.hidden = folded; bar.hidden = !folded;
    grip.setAttribute("aria-label", folded ? "Show the panel" : "Hide the panel");
    replace(folded ? bar : walk, folded ? renderBar(st) : renderWalk(st));
    /* each route opens its cells as its setter saved them (morph.cells), as the apps and the web do */
    var route = st.live && st.live.n >= 0 ? st.live.route : null;
    if (route !== lastRoute) { lastRoute = route; if (route !== null) { L.cellsSet(!!st.live.shown); } }
    var show = route !== null && L.cellsOn();
    if (cellsSlot.hidden === show) { cellsSlot.hidden = !show; if (show) { requestAnimationFrame(L.cellsResize); } }
    place();
  }
  /* on a phone the cells ride just above the panel */
  function place() {
    cellsSlot.style.bottom = matchMedia("(min-width: 861px)").matches ? "" : (panel.offsetHeight + 12) + "px";
  }
  if (window.ResizeObserver) { new ResizeObserver(place).observe(panel); }
  addEventListener("resize", function () { place(); L.cellsResize(); });

  L.onChange(render);
  render();
})();
