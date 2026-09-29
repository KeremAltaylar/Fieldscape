/* A feature merged from the server (published from another device) and deleted here must count
   as pending, and a sent removal must not count again. Nothing is published.
     node tools/serve.mjs 8765    (repo root, in another shell)
     node core/tests/web_remote_delete.mjs */
import { spawn } from "node:child_process";
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { targets, session } from "../../tools/cdp.mjs";

const URL = process.env.FS_URL || "http://localhost:8765/";
const ch = spawn("C:/Program Files/Google/Chrome/Application/chrome.exe",
  ["--headless=new", "--remote-debugging-port=9263", "--user-data-dir=" + mkdtempSync(join(tmpdir(), "fs-rd-")), "about:blank"]);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let failed = 0;
const check = (name, ok, got) => { console.log((ok ? "PASS " : "FAIL ") + name + (ok ? "" : "  (got " + JSON.stringify(got) + ")")); if (!ok) failed++; };
try {
  let t; for (let i = 0; i < 50 && !t; i++) { try { t = (await targets(9263))[0]; } catch { await sleep(200); } }
  const s = session(t); await s.ready;
  await s.send("Runtime.enable"); await s.send("Page.enable");
  const ev = async (e) => (await s.send("Runtime.evaluate", { expression: e, returnByValue: true, awaitPromise: true })).result.value;
  const trashed = (sent) => `localStorage.setItem("fieldarc.trash", JSON.stringify([{ deleted_at: new Date().toISOString(), ${sent ? "sent: true, " : ""}feature: { type: "Feature", geometry: { type: "Point", coordinates: [29, 41] }, properties: { id: "rd-test", kind: "point", _remote: true } } }]))`;
  await s.send("Page.navigate", { url: URL });
  for (let i = 0; i < 100 && !(await ev("!!(window.__fa && __fa.publish && window.FA_PENDING && window.fsListen)")); i++) await sleep(300);
  await ev("fsListen._gate(true), 0");
  const before = await ev("__fa.publish.count()");
  await ev(trashed(false) + ", 0");
  check("a deleted server feature is pending", (await ev("__fa.publish.diff().removed")).includes("rd-test"), await ev("__fa.publish.diff()"));
  await ev("__fa.publish.refresh(), 0");
  check("the panel counts it", await ev("__fa.publish.count()") === before + 1, await ev("document.querySelector('#pending-count').textContent"));
  await ev(trashed(true) + ", 0");
  check("a sent removal is not pending again", !(await ev("__fa.publish.diff().removed")).includes("rd-test"), await ev("__fa.publish.diff()"));
} finally { ch.kill(); }
process.exit(failed ? 1 : 0);
