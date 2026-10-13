/* Phase 1b: every recording of the newest local backup (tools/backup.mjs) put into R2 under its Supabase path - no download
   from Supabase. Run tools/backup.mjs first so the backup is current:  node tools/migrate-r2.mjs [--dry]
   Putting the same key again replaces it, so a second run is harmless. */
import { execFileSync } from "node:child_process";
import { existsSync, readdirSync, statSync } from "node:fs";
import { join, relative, sep } from "node:path";

const ROOT = join(import.meta.dirname, ".."), BACKUPS = join(ROOT, "..", "Fieldscape-backups"), DRY = process.argv.includes("--dry");
const newest = readdirSync(BACKUPS).filter((d) => existsSync(join(BACKUPS, d, "server", "storage", "recordings"))).sort().at(-1);
const base = join(BACKUPS, newest, "server", "storage", "recordings");
const TYPES = { wav: "audio/wav", json: "application/json", webm: "audio/webm", mp3: "audio/mpeg", m4a: "audio/mp4", ogg: "audio/ogg", flac: "audio/flac", txt: "text/plain" };
const walk = (d) => readdirSync(d).flatMap((n) => { const p = join(d, n); return statSync(p).isDirectory() ? walk(p) : [p]; });
const files = walk(base);
console.log(`${newest}: ${files.length} files, ${(files.reduce((s, f) => s + statSync(f).size, 0) / 1e6).toFixed(1)} MB${DRY ? " (dry run)" : ""}`);
let done = 0, failed = 0;
for (const f of files) {
  const key = relative(base, f).split(sep).join("/"), type = TYPES[key.split(".").pop().toLowerCase()] || "application/octet-stream";
  if (DRY) { console.log("[dry] " + key + " " + type); continue; }
  try {
    execFileSync("npx", ["--yes", "wrangler", "r2", "object", "put", `fieldscape-media/${key}`, "--file", `"${f}"`, "--content-type", type, "--remote"],
      { cwd: join(ROOT, "workers", "media"), stdio: "ignore", shell: true });
    done++; if (done % 10 === 0) console.log(done + " / " + files.length);
  } catch (e) { failed++; console.log("FAILED " + key + ": " + e.message.split("\n")[0]); }
}
console.log(`${done} put, ${failed} failed`);
process.exit(failed ? 1 : 0);
