/* core/tests/exports.mjs - the public engine exports what the page's sampler code calls (6a) */
import { readFileSync } from "node:fs";
const want = ["fs_analyse", "fs_engine_role_source", "fs_engine_role_analysis", "fs_engine_roles", "fs_engine_source_track", "fs_engine_create", "malloc", "free"];
const mod = new WebAssembly.Module(readFileSync("web/core.wasm"));
const got = new Set(WebAssembly.Module.exports(mod).map((e) => e.name));
const missing = want.filter((n) => !got.has(n));
console.log(missing.length ? "FAIL core.wasm lacks " + missing.join(", ") : "PASS core.wasm exports the sampler calls");
process.exit(missing.length ? 1 : 0);
