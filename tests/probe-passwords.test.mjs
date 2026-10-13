/* R2 final review I1: the repo is public, so a probe setter made by a test or tool must not have a password written in it -
   a run that crashed before its cleanup would leave a setter anyone could sign in as (sensitive recordings, every write).
   Each run makes its own random password. */
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync, readdirSync } from "node:fs";

const files = [...readdirSync("core/tests").filter((f) => f.endsWith(".mjs")).map((f) => "core/tests/" + f),
               ...readdirSync("tools").filter((f) => f.endsWith(".mjs")).map((f) => "tools/" + f),
               ...readdirSync("tests").filter((f) => f.endsWith(".mjs")).map((f) => "tests/" + f)];

test("no probe account password is written in the repo", () => {
  /* an assignment from a string with nothing random in it */
  const fixed = files.filter((f) => readFileSync(f, "utf8").split("\n").some((l) => /PASSWORD\w*\s*=\s*["'`]/.test(l) && !/randomUUID|randomBytes/.test(l)));
  assert.deepEqual(fixed, []);
});
