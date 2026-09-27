/* The route patch on a phone: one tab at a time (rulebook M-2), Chords · Voices · Effects · Morphs
   · Rhythm. On a computer the whole panel stays one screen (C-8) and these tabs are hidden. Each
   section is tagged from its heading after every render of the panel (renderPatchPanel rebuilds
   #pp-body often); the tab bar itself is built once. */
(function () {
  var TABS = [["chords", "Chords", /^(Transport|Progression)/], ["voices", "Voices", /^(Bed|Voice|Second voice|Third voice)/],
              ["effects", "Effects", /^Effects/], ["morphs", "Morphs", /^Morphs/], ["rhythm", "Rhythm", /^(Rhythm|Zones)/]];
  var panel = document.getElementById("patchpanel"), body = document.getElementById("pp-body");
  if (!panel || !body) { return; }
  var active = "chords", sub = "v1";
  /* Voices holds three voices, taller than a phone: a toggle inside the tab, not a scrollbar (C-8) */
  var SUBS = [["v1", "Bed & voice", /^(Bed|Voice)/], ["v2", "Second", /^Second voice/], ["v3", "Third", /^Third voice/]];
  var subbar = document.createElement("div");
  subbar.id = "pp-sub"; subbar.setAttribute("role", "group"); subbar.setAttribute("aria-label", "Which voice");
  SUBS.forEach(function (t) {
    var b = document.createElement("button");
    b.type = "button"; b.dataset.sub = t[0]; b.textContent = t[1];
    b.addEventListener("click", function () { sub = t[0]; apply(); body.scrollTop = 0; });
    subbar.appendChild(b);
  });
  var bar = document.createElement("div");
  bar.id = "pp-tabs"; bar.setAttribute("role", "tablist"); bar.setAttribute("aria-label", "Patch");
  TABS.forEach(function (t) {
    var b = document.createElement("button");
    b.type = "button"; b.dataset.tab = t[0]; b.textContent = t[1]; b.setAttribute("role", "tab");
    b.addEventListener("click", function () { active = t[0]; apply(); body.scrollTop = 0; });
    bar.appendChild(b);
  });
  panel.insertBefore(bar, body);
  panel.insertBefore(subbar, body);

  function tabOf(title) {
    for (var i = 0; i < TABS.length; i++) { if (TABS[i][2].test(title)) { return TABS[i][0]; } }
    return null;
  }
  /* every element under a heading takes that heading's tab, in the columns and after them */
  function tag() {
    var cur = "chords", cs = "";
    [].forEach.call(body.querySelectorAll(".ppcol > *"), function (el) {
      if (el.tagName === "H2") {
        cur = tabOf(el.textContent) || cur;
        cs = "";
        for (var i = 0; i < SUBS.length; i++) { if (cur === "voices" && SUBS[i][2].test(el.textContent)) { cs = SUBS[i][0]; } }
      }
      el.dataset.tab = cur;
      if (cs) { el.dataset.sub = cs; } else { delete el.dataset.sub; }
    });
    [].forEach.call(body.children, function (el) {
      if (el.classList.contains("ppcols")) { return; }
      var h = el.classList.contains("pphead") ? el.querySelector("h2") : null;
      if (h) { cur = tabOf(h.textContent) || cur; }
      el.dataset.tab = cur;
    });
  }
  function apply() {
    [].forEach.call(bar.children, function (b) { b.setAttribute("aria-selected", String(b.dataset.tab === active)); });
    [].forEach.call(subbar.children, function (b) { b.setAttribute("aria-pressed", String(b.dataset.sub === sub)); });
    subbar.hidden = active !== "voices";
    body.dataset.active = active;
    [].forEach.call(body.querySelectorAll("[data-tab]"), function (el) {
      el.classList.toggle("pp-off", el.dataset.tab !== active || (active === "voices" && !!el.dataset.sub && el.dataset.sub !== sub));
    });
  }
  new MutationObserver(function () { tag(); apply(); }).observe(body, { childList: true });
  tag(); apply();
})();
