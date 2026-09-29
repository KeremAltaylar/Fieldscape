/* Places lists only the parks something is published in; a setter's All parks switch brings every
   park (and the page's own picker) back. Nothing is published.
     node tools/serve.mjs 8765    (repo root, in another shell)
     node core/tests/web_parks.mjs */
import { spawn } from "node:child_process";
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { targets, session } from "../../tools/cdp.mjs";

const URL = process.env.FS_URL || "http://localhost:8765/";
const ch = spawn("C:/Program Files/Google/Chrome/Application/chrome.exe",
  ["--headless=new", "--remote-debugging-port=9264", "--user-data-dir=" + mkdtempSync(join(tmpdir(), "fs-parks-")), "about:blank"]);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let failed = 0;
const check = (name, ok, got) => { console.log((ok ? "PASS " : "FAIL ") + name + (ok ? "" : "  (got " + JSON.stringify(got) + ")")); if (!ok) failed++; };
try {
  let t; for (let i = 0; i < 50 && !t; i++) { try { t = (await targets(9264))[0]; } catch { await sleep(200); } }
  const s = session(t); await s.ready;
  await s.send("Runtime.enable"); await s.send("Page.enable");
  const ev = async (e) => (await s.send("Runtime.evaluate", { expression: e, returnByValue: true, awaitPromise: true })).result.value;
  await s.send("Emulation.setDeviceMetricsOverride", { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false });
  await s.send("Page.navigate", { url: URL });
  for (let i = 0; i < 100 && !(await ev("!!(window.__fa && window.fsListen && document.body.classList.contains('world'))")); i++) await sleep(300);
  await sleep(3000);
  const openPlaces = async () => { await ev("document.querySelector('.ls-parks') || document.querySelector('#ls-place').click(), 0"); await sleep(600); };
  const rows = () => ev("document.querySelectorAll('.ls-parks .ls-item').length");
  const search = async (q) => { await ev(`(function(){ var i = document.querySelector('.ls-search'); i.value = ${JSON.stringify(q)}; i.dispatchEvent(new Event('input')); })(), 0`); await sleep(300); };
  await openPlaces();
  check("listener: Places is open", await ev("!!document.querySelector('.ls-parks')"), null);
  const listenerRows = await rows();
  check("listener: the parks with published work are listed", listenerRows > 0, await ev("document.querySelector('#ls-sheet').textContent"));
  await search("a"); const listenerSearch = await rows(); await search("");
  check("listener: a search lists no park beyond those with recordings", listenerSearch <= listenerRows, [listenerRows, listenerSearch]);
  check("listener: no All parks switch", !(await ev("!!document.querySelector('[data-ls-allparks]')")), null);

  await ev("document.querySelector('#ls-place').click(), 0"); await sleep(400);   /* close Places; it rebuilds as the setter's on the next open */
  await ev("fsListen._gate(true), 0"); await sleep(800);
  await openPlaces();
  check("setter: All parks switch, off", await ev("(document.querySelector('[data-ls-allparks]')||{}).getAttribute && document.querySelector('[data-ls-allparks]').getAttribute('aria-pressed')") === "false", null);
  check("setter off: only parks with recordings", await rows() === listenerRows, [await rows(), listenerRows]);
  check("setter off: the page's park picker hidden", await ev("!document.querySelector('#place-list').offsetParent"), null);
  await ev("document.querySelector('[data-ls-allparks]').click(), 0"); await sleep(600);
  check("setter on: switch pressed", await ev("document.querySelector('[data-ls-allparks]').getAttribute('aria-pressed')") === "true", null);
  check("setter on: every park listed", await rows() > listenerRows, [await rows(), listenerRows]);
  check("setter on: the page's park picker shown", await ev("!!document.querySelector('#place-list').offsetParent"), null);
} finally { ch.kill(); }
process.exit(failed ? 1 : 0);
