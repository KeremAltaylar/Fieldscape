/* Knobs over the setter's sliders (rulebook C-12: a continuous parameter is a knob, and a knob has
   affordances). Each input[type=range] in the sound panels gets a knob drawn beside it that moves
   the input and fires its own `input` event (and `change` on release), so the page's handlers -
   the patch, the readout, the sound - do everything, and nothing about the patch moves here.
     drag up / down        the whole range in ~200 px
     Shift, or a long-press on touch before moving   fine: ~1000 px
     double-click / double-tap   the default (the input's data-def)
     arrows a step, Page Up/Down ten, Home / End the ends
   The panels re-render often (an instrument change rebuilds them), so a MutationObserver
   enhances whatever new sliders appear. */
(function () {
  var ROOTS = ["#pp-body", "#rp-body"], SWEEP = 270, R = 15;
  var NS = "http://www.w3.org/2000/svg";

  function arc(a0, a1) {   /* degrees, 0 = up, clockwise */
    var p = function (a) { var r = (a - 90) * Math.PI / 180; return [18 + R * Math.cos(r), 18 + R * Math.sin(r)]; };
    var s = p(a0), e = p(a1), big = a1 - a0 > 180 ? 1 : 0;
    return "M" + s[0].toFixed(2) + " " + s[1].toFixed(2) + " A" + R + " " + R + " 0 " + big + " 1 " + e[0].toFixed(2) + " " + e[1].toFixed(2);
  }
  function svg(tag, attrs) { var e = document.createElementNS(NS, tag); for (var k in attrs) { e.setAttribute(k, attrs[k]); } return e; }

  function enhance(input) {
    if (input.dataset.knob) { return; }
    input.dataset.knob = "1";
    /* in a patch row the row names it; elsewhere (a morph's period and depth) the slider's title
       does, shown as a caption under the knob */
    var row = input.closest(".pprow");
    var name = row && row.querySelector("span") ? row.querySelector("span").textContent.trim()
             : (input.getAttribute("aria-label") || input.title || "Value");
    var readout = row && row.querySelector("i");
    var k = document.createElement("div");
    k.className = "knob";
    k.tabIndex = 0;
    k.setAttribute("role", "slider");
    k.setAttribute("aria-label", name);
    if (input.dataset.k) { k.dataset.k = input.dataset.k; }
    if (input.dataset.def !== undefined) { k.dataset.def = input.dataset.def; }
    var g = svg("svg", { viewBox: "0 0 36 36", "aria-hidden": "true" });
    var track = svg("path", { class: "knob-track", d: arc(-SWEEP / 2, SWEEP / 2) });
    var fill = svg("path", { class: "knob-fill" });
    var tick = svg("line", { class: "knob-tick", x1: 18, y1: 18, x2: 18, y2: 6 });
    g.appendChild(track); g.appendChild(fill); g.appendChild(tick);
    k.appendChild(g);
    if (row) { input.insertAdjacentElement("afterend", k); row.classList.add("has-knob"); }
    else {
      var cell = document.createElement("span");
      cell.className = "knob-cell";
      input.insertAdjacentElement("afterend", cell);
      cell.appendChild(k);
      var cap = document.createElement("span");
      cap.className = "knob-cap"; cap.textContent = name; cap.setAttribute("aria-hidden", "true");
      cell.appendChild(cap);
    }

    var min = function () { return +input.min || 0; }, max = function () { return input.max === "" ? 100 : +input.max; };
    var step = function () { return +input.step || 1; };
    function draw() {
      var v = +input.value, f = max() > min() ? (v - min()) / (max() - min()) : 0;
      var a = -SWEEP / 2 + SWEEP * Math.max(0, Math.min(1, f));
      fill.setAttribute("d", f > 0.002 ? arc(-SWEEP / 2, a) : "");
      tick.setAttribute("transform", "rotate(" + a.toFixed(1) + " 18 18)");
      k.setAttribute("aria-valuemin", min()); k.setAttribute("aria-valuemax", max()); k.setAttribute("aria-valuenow", v);
      if (readout) { k.setAttribute("aria-valuetext", readout.textContent); }
    }
    function set(v, done) {
      var s = step(), q = Math.round((Math.max(min(), Math.min(max(), v)) - min()) / s) * s + min();
      q = +q.toFixed(6);
      if (+input.value !== q) { input.value = q; input.dispatchEvent(new Event("input", { bubbles: true })); }
      if (done) { input.dispatchEvent(new Event("change", { bubbles: true })); }
      draw();
    }
    input.addEventListener("input", draw);
    draw();

    /* dragging: vertical travel, finer with Shift or after a touch held still */
    var drag = null, lastTap = 0;
    k.addEventListener("pointerdown", function (e) {
      e.preventDefault(); k.focus();
      drag = { id: e.pointerId, y: e.clientY, x: e.clientX, v: +input.value, fine: false, moved: false, t0: performance.now(), touch: e.pointerType === "touch" };
      k.setPointerCapture(e.pointerId);
      k.classList.add("on");
    });
    k.addEventListener("pointermove", function (e) {
      if (!drag || e.pointerId !== drag.id) { return; }
      var dy = drag.y - e.clientY, dx = e.clientX - drag.x;
      if (!drag.moved && Math.hypot(dx, dy) < 3) { return; }
      if (!drag.moved) { drag.moved = true; if (drag.touch && performance.now() - drag.t0 > 400) { drag.fine = true; } }
      var px = e.shiftKey || drag.fine ? 1000 : 200;
      set(drag.v + (dy + dx * 0.25) / px * (max() - min()));
    });
    function end(e) {
      if (!drag || (e && e.pointerId !== drag.id)) { return; }
      var moved = drag.moved;
      drag = null; k.classList.remove("on");
      if (moved) { input.dispatchEvent(new Event("change", { bubbles: true })); return; }
      /* a double tap resets (touch has no dblclick) */
      var now = performance.now();
      if (now - lastTap < 350) { reset(); lastTap = 0; } else { lastTap = now; }
    }
    k.addEventListener("pointerup", end);
    k.addEventListener("pointercancel", end);
    function reset() { if (k.dataset.def !== undefined && k.dataset.def !== "") { set(+k.dataset.def, true); } }
    k.addEventListener("dblclick", function (e) { e.preventDefault(); reset(); });
    k.addEventListener("keydown", function (e) {
      var s = step(), v = +input.value, keys = { ArrowUp: v + s, ArrowRight: v + s, ArrowDown: v - s, ArrowLeft: v - s,
        PageUp: v + 10 * s, PageDown: v - 10 * s, Home: min(), End: max() };
      if (!(e.key in keys)) { return; }
      e.preventDefault(); set(keys[e.key], true);
    });
  }

  function sweep(root) { [].forEach.call(root.querySelectorAll('input[type="range"]'), enhance); }
  ROOTS.forEach(function (sel) {
    var root = document.querySelector(sel);
    if (!root) { return; }
    sweep(root);
    new MutationObserver(function () { sweep(root); }).observe(root, { childList: true, subtree: true });
  });
})();
