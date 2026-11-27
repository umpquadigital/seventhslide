// The specs' ``test``: each test gets its own app and library (``app``), a
// protocol client beside the page (``observer``), and the page signed in
// (``remote``). A test fails if the app exits under it or the page throws.

import { test as base, expect } from '@playwright/test';

import { launchApp } from './app.mjs';
import { Observer } from './observer.mjs';
import { RemotePage } from './remote-page.mjs';

export { expect };
export { exactly } from './remote-page.mjs';

export const test = base.extend({
  /** What the test's library holds beyond the usual (``launchApp``'s
   *  options): ``test.use({ library: { songs: 250 } })``. */
  library: [{}, { option: true }],

  app: async ({ library }, use, testInfo) => {
    const app = await launchApp(testInfo.outputPath('app'), library);
    let exited;
    try {
      await use(app);
    } finally {
      exited = app.unexpectedExit;
      await app.stop();
      await testInfo.attach('app.log', { path: app.logPath, contentType: 'text/plain' });
      // Said beside a test that failed already, which keeps its own error.
      if (exited) testInfo.annotations.push({ type: 'app exited', description: exited });
    }
    if (exited) throw new Error(`The app exited during the test (${exited}); see app.log`);
  },

  observer: async ({ app }, use) => {
    const observer = await Observer.connect(app);
    await use(observer);
    observer.close();
  },

  /** The remote page, loaded but not signed in. */
  remotePage: async ({ page, app }, use, testInfo) => {
    const errors = [];
    page.on('pageerror', (err) => errors.push(err.stack || String(err)));
    // Not this suite's doing, but it explains a request that failed.
    page.on('requestfailed', (request) => {
      if (request.failure()?.errorText === 'net::ERR_NETWORK_CHANGED') {
        testInfo.annotations.push({
          type: 'network changed',
          description: `the machine's network changed and Chromium dropped ${request.url()}`,
        });
      }
    });
    const remote = new RemotePage(page, app);
    await remote.open();
    await use(remote);
    expect(errors, 'errors thrown in the page').toEqual([]);
  },

  /** The remote page, signed in and showing the server's state. */
  remote: async ({ remotePage }, use) => {
    await remotePage.signIn();
    await use(remotePage);
  },
});
