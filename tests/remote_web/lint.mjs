// What CTest runs as remote_web_lint (tests/CMakeLists.txt): that every
// remote command is answered for (commands.mjs), every glyph the client
// draws exists (glyphs.mjs) and its markup, stylesheets and modules agree
// (markup.mjs), then ESLint over the
// bundled client and these tests, or a skip when it is not installed here.
// Above all ESLint finds a name used and never declared or imported, which
// the browser would report only when that line runs.
//
//     node lint.mjs [eslint arguments]

import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { checkCommands } from './commands.mjs';
import { checkGlyphs } from './glyphs.mjs';
import { checkMarkup } from './markup.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const top = path.resolve(here, '..', '..');
const SKIP = 77;  // CTest's SKIP_RETURN_CODE for this test

// Needs nothing installed, so it runs where ESLint would be skipped.
const problems = [...checkCommands(), ...checkGlyphs(), ...checkMarkup()];
if (problems.length) {
  for (const p of problems) console.error(p);
  process.exit(1);
}

const cli = path.join(here, 'node_modules', 'eslint', 'bin', 'eslint.js');
if (!fs.existsSync(cli)) {
  console.log('Skipped: ESLint is not installed. In tests/remote_web run\n    npm ci');
  process.exit(SKIP);
}

// From the top of the tree, so the config's patterns name the client.
const run = spawnSync(process.execPath, [
  cli, '--config', path.join(here, 'eslint.config.mjs'), '--max-warnings', '0',
  'src/remote/web', 'tests/remote_web', ...process.argv.slice(2),
], { cwd: top, stdio: 'inherit' });
process.exit(run.status ?? 1);
