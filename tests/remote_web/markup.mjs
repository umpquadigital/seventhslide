// The client's markup, stylesheets and modules agree: every class a
// stylesheet styles is used by index.html or a module (dead rules go), every
// token tokens.css defines is used, and every element a module finds by id
// (`$('id')`) exists. lint.mjs runs it.
//
//     node markup.mjs

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const web = path.resolve(here, '..', '..', 'src', 'remote', 'web');

function readDir(dir, ext) {
  return fs.readdirSync(path.join(web, dir))
    .filter((f) => f.endsWith(ext))
    .map((f) => ({ file: `${dir}/${f}`, text: fs.readFileSync(path.join(web, dir, f), 'utf8') }));
}

export function checkMarkup() {
  const html = fs.readFileSync(path.join(web, 'index.html'), 'utf8');
  const modules = readDir('js', '.js');
  const sheets = readDir('css', '.css');
  const code = [html, ...modules.map((m) => m.text)].join('\n');
  // Every word the markup and modules say, as a class could be named.
  const words = new Set(code.match(/[\w-]+/g));
  const problems = [];

  for (const { file, text } of sheets) {
    const rules = text.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\{[^{}]*\}/g, '{}');
    const classes = new Set([...rules.matchAll(/\.([a-zA-Z][\w-]*)/g)].map((m) => m[1]));
    for (const name of classes) {
      if (!words.has(name)) problems.push(`${file}: .${name} is styled but nothing uses it`);
    }
  }

  const css = sheets.map((sheet) => sheet.text).join('\n');
  for (const { file, text } of sheets) {
    for (const m of text.matchAll(/^\s*(--[\w-]+)\s*:/gm)) {
      const used = new RegExp(`var\\(\\s*${m[1]}\\s*[,)]`).test(css + code);
      if (!used) problems.push(`${file}: ${m[1]} is defined but nothing uses it`);
    }
  }

  const ids = new Set([
    ...[...html.matchAll(/\bid="([\w-]+)"/g)].map((m) => m[1]),
    ...modules.flatMap((m) => [...m.text.matchAll(/\.id\s*=\s*'([\w-]+)'/g)].map((x) => x[1])),
  ]);
  for (const { file, text } of modules) {
    for (const m of text.matchAll(/\$\(\s*'([\w-]+)'\s*\)/g)) {
      if (!ids.has(m[1])) problems.push(`${file}: $('${m[1]}') names no element`);
    }
  }
  return problems;
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const problems = checkMarkup();
  for (const p of problems) console.error(p);
  process.exit(problems.length ? 1 : 0);
}
