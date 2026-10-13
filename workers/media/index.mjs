/* The deployed Worker: handle() with the real fetch and the edge cache (workers/media/handle.mjs has the rules) */
import { handle } from "./handle.mjs";
export default { fetch: (req, env) => handle(req, env, { fetch: (u, i) => fetch(u, i), cache: caches.default }) };
