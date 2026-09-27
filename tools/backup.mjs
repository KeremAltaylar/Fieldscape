/* A full backup, the way the 2026-09-19/21 ones were made by hand, as one command:
     node --env-file=.env.local tools/backup.mjs <label>
   -> ../Fieldscape-backups/<date>-<label>/
        Fieldscape-all-refs.bundle     every branch and tag (restore: git clone the bundle)
        server/db/<schema>.<table>.json  every row of every public and private table
        server/storage/recordings/...  every file in the private bucket
        server/summary.json, README.txt
   Storage counts against the Free plan's download budget, so a file already in the latest
   earlier backup with the same size is copied from there, not downloaded again.
   .env.local is left out on purpose: the folder syncs to OneDrive. */
import { execFileSync } from "node:child_process";
import { copyFileSync, existsSync, mkdirSync, readdirSync, statSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import pg from "pg";
import { createClient } from "@supabase/supabase-js";

const label = process.argv[2];
if (!label) throw new Error("usage: tools/backup.mjs <label>");
const ROOT = join(import.meta.dirname, ".."), BACKUPS = join(ROOT, "..", "Fieldscape-backups");
const day = new Date().toISOString().slice(0, 10), out = join(BACKUPS, `${day}-${label}`);
if (existsSync(out)) throw new Error(`${out} exists`);
const earlier = readdirSync(BACKUPS).filter((d) => existsSync(join(BACKUPS, d, "server", "storage"))).sort().at(-1);

mkdirSync(join(out, "server", "db"), { recursive: true });
const git = (...a) => execFileSync("git", a, { cwd: ROOT, encoding: "utf8" }).trim();
git("bundle", "create", join(out, "Fieldscape-all-refs.bundle"), "--all");
const commit = git("rev-parse", "--short", "HEAD"), branch = git("rev-parse", "--abbrev-ref", "HEAD");

const db = new pg.Client({ connectionString: process.env.DATABASE_URL, ssl: { rejectUnauthorized: false } });
await db.connect();
const tables = {};
try {
  const { rows } = await db.query(`select table_schema s, table_name t from information_schema.tables
                                   where table_schema in ('public', 'private') and table_type = 'BASE TABLE' order by 1, 2`);
  for (const { s, t } of rows) {
    const r = await db.query(`select * from "${s}"."${t}"`);
    writeFileSync(join(out, "server", "db", `${s}.${t}.json`), JSON.stringify(r.rows, null, 2));
    tables[`${s}.${t}`] = r.rows.length;
  }
} finally { await db.end(); }

const supa = createClient(process.env.SUPABASE_URL, process.env.SUPABASE_SERVICE_KEY, { auth: { persistSession: false } });
async function list(prefix) {
  const files = [];
  for (let offset = 0; ; offset += 1000) {
    const { data, error } = await supa.storage.from("recordings").list(prefix, { limit: 1000, offset });
    if (error) throw error;
    for (const e of data) {
      const p = prefix ? `${prefix}/${e.name}` : e.name;
      if (e.id === null) files.push(...await list(p)); else files.push({ path: p, size: e.metadata?.size ?? -1 });
    }
    if (data.length < 1000) return files;
  }
}
let bytes = 0, copied = 0, fetched = 0, fetchedBytes = 0;
const files = await list("");
for (const f of files) {
  const to = join(out, "server", "storage", "recordings", f.path);
  mkdirSync(dirname(to), { recursive: true });
  const from = earlier && join(BACKUPS, earlier, "server", "storage", "recordings", f.path);
  if (from && existsSync(from) && statSync(from).size === f.size) { copyFileSync(from, to); copied++; }
  else {
    const { data, error } = await supa.storage.from("recordings").download(f.path);
    if (error) throw error;
    const buf = Buffer.from(await data.arrayBuffer());
    writeFileSync(to, buf); fetched++; fetchedBytes += buf.length;
  }
  bytes += statSync(to).size;
}

const summary = { taken: new Date().toISOString(), commit, branch, tables, storage: { recordings: { files: files.length, bytes } },
                  copiedFrom: earlier ?? null, copied, downloaded: { files: fetched, bytes: fetchedBytes } };
writeFileSync(join(out, "server", "summary.json"), JSON.stringify(summary, null, 2));
writeFileSync(join(out, "README.txt"), `Fieldscape backup — ${day}, ${label} (branch ${branch}, commit ${commit})

Fieldscape-all-refs.bundle   whole git repo: every branch and tag.
                             Restore: git clone Fieldscape-all-refs.bundle Fieldscape
server/db/*.json             every row of every table (public and private schemas).
                             The schema is supabase/migrations in the repo; these are the rows.
server/storage/recordings/   every file in the private 'recordings' bucket (audio + photos).
server/summary.json          row and file counts, taken ${summary.taken}.

Not included: .env.local (the keys). Keep that separately. private.config holds the fuzz
salt, so treat this folder as private.
`);
console.log(JSON.stringify(summary, null, 2));
