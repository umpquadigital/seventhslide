// Every glyph the client draws is in the app's table (src/app/glyphs.cpp),
// whose sprite the server puts in the page: a name missing there draws
// nothing, and no browser says so. lint.mjs runs it.
//
// The client names a glyph as `#ic-NAME` in index.html; in a module, as the
// first argument of `glyph('NAME'`, `thumbBox('NAME'` or `arrowButton('NAME'`,
// or as a value of an object it calls `..._GLYPH` / `..._GLYPHS`.
//
//     node glyphs.mjs

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const top = path.resolve(here, '..', '..');
const web = path.join(top, 'src', 'remote', 'web');

function tableNames() {
  const source = fs.readFileSync(path.join(top, 'src', 'app', 'glyphs.cpp'), 'utf8');
  return new Set([...source.matchAll(/^\s*\{"([\w/]+)",\s*(?:Stroke|Fill|Solid|Raw)\b/gm)]
    .map((m) => m[1]));
}

/** Each glyph name the client uses, with where. */
function clientNames() {
  const used = [];
  const html = fs.readFileSync(path.join(web, 'index.html'), 'utf8');
  for (const m of html.matchAll(/#ic-([\w]+)/g)) used.push({ name: m[1], where: 'index.html' });
  const dir = path.join(web, 'js');
  for (const file of fs.readdirSync(dir)) {
    const js = fs.readFileSync(path.join(dir, file), 'utf8');
    for (const m of js.matchAll(/\b(?:glyph|thumbBox|arrowButton)\(\s*'(\w+)'/g)) {
      used.push({ name: m[1], where: file });
    }
    for (const block of js.matchAll(/_GLYPHS?\s*=\s*\{([^}]*)\}/g)) {
      for (const m of block[1].matchAll(/:\s*'(\w+)'/g)) used.push({ name: m[1], where: file });
    }
  }
  return used;
}

export function checkGlyphs() {
  const table = tableNames();
  if (!table.size) return ['no glyphs found in src/app/glyphs.cpp'];
  return clientNames()
    .filter(({ name }) => !table.has(name))
    .map(({ name, where }) => `${where}: glyph "${name}" is not in src/app/glyphs.cpp`);
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const problems = checkGlyphs();
  for (const p of problems) console.error(p);
  process.exit(problems.length ? 1 : 0);
}
