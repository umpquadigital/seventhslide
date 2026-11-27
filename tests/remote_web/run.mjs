// What CTest runs (tests/CMakeLists.txt): the browser tests, or a skip when
// they are not installed here. Arguments go on to Playwright.
//
//     node run.mjs [playwright test arguments]

import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const SKIP = 77;  // CTest's SKIP_RETURN_CODE for this test

if (!fs.existsSync(path.join(here, 'node_modules', '@playwright', 'test'))) {
  console.log('Skipped: the browser tests are not installed. In tests/remote_web run\n'
    + '    npm ci && npx playwright install chromium');
  process.exit(SKIP);
}

const cli = path.join(here, 'node_modules', '@playwright', 'test', 'cli.js');
const run = spawnSync(process.execPath, [cli, 'test', ...process.argv.slice(2)], {
  cwd: here,
  stdio: 'inherit',
});
process.exit(run.status ?? 1);
