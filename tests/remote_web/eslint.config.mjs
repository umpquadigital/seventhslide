// What `node lint.mjs` checks: the bundled client and these tests. Paths are
// from the top of the source tree, where lint.mjs runs ESLint.

import js from '@eslint/js';
import { defineConfig, globalIgnores } from 'eslint/config';
import globals from 'globals';

export default defineConfig([
  globalIgnores(['tests/remote_web/node_modules/', 'tests/remote_web/test-results/',
    'tests/remote_web/playwright-report/']),
  {
    files: ['src/remote/web/**/*.js'],
    extends: [js.configs.recommended],
    languageOptions: { sourceType: 'module', globals: globals.browser },
  },
  {
    // Node, with the browser's names for code a test runs in the page.
    files: ['tests/remote_web/**/*.mjs'],
    extends: [js.configs.recommended],
    languageOptions: { sourceType: 'module', globals: { ...globals.node, ...globals.browser } },
  },
]);
