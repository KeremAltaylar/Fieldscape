/* A local server for testing the site as GitHub Pages serves it: static files, and any other path
   (a deep link like /R8845862/<route>, or a sign-in link returning to one) answered with index.html
   the way 404.html sends it there. python -m http.server 404s those, which broke local sign-in.
     node tools/serve.mjs [port]        (repo root; default 8765) */
import { createServer } from "node:http";
import { readFile, stat } from "node:fs/promises";
import { extname, join, normalize } from "node:path";

const ROOT = join(import.meta.dirname, ".."), PORT = +(process.argv[2] || 8765);
const TYPES = { ".html": "text/html; charset=utf-8", ".js": "text/javascript", ".mjs": "text/javascript", ".css": "text/css",
  ".json": "application/json", ".geojson": "application/geo+json", ".wasm": "application/wasm", ".png": "image/png",
  ".svg": "image/svg+xml", ".webmanifest": "application/manifest+json", ".wav": "audio/wav" };
createServer(async (req, res) => {
  const path = decodeURIComponent(new URL(req.url, "http://x").pathname);
  let file = normalize(join(ROOT, path));
  if (!file.startsWith(ROOT)) { res.writeHead(403).end(); return; }
  try { if ((await stat(file)).isDirectory()) { file = join(file, "index.html"); } }
  catch { file = extname(path) ? file : join(ROOT, "index.html"); }   /* a page address, not a file: the app */
  try {
    const body = await readFile(file);
    res.writeHead(200, { "Content-Type": TYPES[extname(file)] || "application/octet-stream", "Cache-Control": "no-cache" }).end(body);
  } catch { res.writeHead(404, { "Content-Type": "text/plain" }).end("Not found: " + path); }
}).listen(PORT, () => console.log("serving " + ROOT + " on http://localhost:" + PORT));
