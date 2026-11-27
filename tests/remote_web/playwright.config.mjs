// Browser tests for the web remote. See README.md.

import { defineConfig } from '@playwright/test';

// The shots (shots/, see its header) run only when asked for by their
// directory, so a plain run is the specs alone.
const shots = !!process.env.SEVENTHSLIDE_REMOTE_SHOTS_DIR;

export default defineConfig({
  globalSetup: './harness/global-setup.mjs',
  // Every test has an app and a library of its own, so any two can run at once.
  fullyParallel: true,
  workers: process.env.CI ? 2 : undefined,
  // A test that passes only on a retry is a bug in the test or the remote.
  retries: 0,
  forbidOnly: !!process.env.CI,
  timeout: 30_000,
  expect: { timeout: 5_000 },
  reporter: process.env.CI
    ? [['list'], ['html', { open: 'never' }]]
    : [['list']],
  use: {
    browserName: 'chromium',
    headless: true,
    viewport: { width: 1280, height: 800 },
    trace: 'retain-on-failure',
    screenshot: 'only-on-failure',
  },
  projects: [
    { name: 'specs', testDir: './specs' },
    ...(shots ? [{ name: 'shots', testDir: './shots', testMatch: '*.shots.mjs' }] : []),
  ],
});
