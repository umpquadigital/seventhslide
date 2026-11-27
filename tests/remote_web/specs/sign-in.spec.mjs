// Signing in with the PIN, staying signed in, and being asked again.

import { expect, test } from '../harness/test.mjs';

test('a wrong PIN is refused and the right one lets the device in', async ({ remotePage: remote }) => {
  await expect(remote.gate).toBeVisible();
  await expect(remote.gateError).toBeHidden();

  await remote.pin.fill('0000');
  await remote.connect.click();
  await expect(remote.gateError).toHaveText(/incorrect pin/i);
  await expect(remote.appRoot).toBeHidden();

  await remote.signIn();
  await expect(remote.gate).toBeHidden();
  // The app opens no service when it starts.
  await remote.expectService('');
  await expect(remote.serviceList).toBeEmpty();
});

test('a device stays signed in across a reload', async ({ remote }) => {
  await remote.page.reload();
  await remote.waitForSnapshot();
  await expect(remote.gate).toBeHidden();
});

test('repeated wrong PINs lock the device out for a while', async ({ remotePage: remote }) => {
  // Five tries are free. The sixth wrong one locks the device out for a
  // second (then two, four ...), so a seventh straight after is refused
  // unseen, and the page says how long to wait.
  const statuses = [];
  for (let attempt = 1; attempt <= 9 && !statuses.includes(429); attempt += 1) {
    await remote.pin.fill('0000');
    const answer = remote.page.waitForResponse((res) => res.url().endsWith('/api/auth'));
    await remote.connect.click();
    const res = await answer;
    statuses.push(res.status());
    if (res.status() === 429) {
      const { retryAfter } = await res.json();
      expect(retryAfter).toBeGreaterThan(0);
      await expect(remote.gateError).toHaveText(`Too many attempts. Try again in ${retryAfter}s.`);
    } else {
      await expect(remote.gateError).toHaveText(/incorrect pin/i);
    }
  }
  expect(statuses).toEqual([401, 401, 401, 401, 401, 401, 429]);
  await expect(remote.appRoot).toBeHidden();
});

test('signing in again after a refused request leaves one connection', async ({ remote }) => {
  await expect.poll(() => remote.openSockets()).toBe(1);
  // The token is still good, but one read is refused as though it were not.
  await remote.page.route(/\/api\/library\//, (route) => route.fulfill({
    status: 401, contentType: 'application/json', body: '{"error":"unauthorized"}',
  }), { times: 1 });
  await remote.librarySearch.fill('grace');
  await expect(remote.gate).toBeVisible();
  await expect(remote.gateError).toHaveText('Session expired. Enter the PIN again.');
  // Asking for the PIN closes the connection.
  await expect.poll(() => remote.openSockets()).toBe(0);

  await remote.signIn();
  await expect.poll(() => remote.openSockets()).toBe(1);
  // One more socket for signing in again, and none behind the gate.
  expect(remote.sockets).toHaveLength(2);
});

/* Between the page and the server, so a test can drop the connection as a
   network would. Lists every socket the page opens: ``open`` until either end
   closes it, ``drop()`` closes it on the page's side. */
async function droppableSockets(page) {
  const sockets = [];
  await page.routeWebSocket(/\/ws/, (ws) => {
    const entry = { open: true, drop: () => ws.close() };
    sockets.push(entry);
    const server = ws.connectToServer();
    ws.onMessage((message) => server.send(message));
    server.onMessage((message) => ws.send(message));
    ws.onClose((code, reason) => {
      entry.open = false;
      server.close({ code, reason });
    });
    server.onClose((code, reason) => {
      entry.open = false;
      ws.close({ code, reason });
    });
  });
  return sockets;
}

const openCount = (sockets) => sockets.filter((s) => s.open).length;

// One read refused as though the token had ended (it has not), which asks
// for the PIN.
async function refuseOneRead(remote) {
  await remote.page.route(/\/api\/library\//, (route) => route.fulfill({
    status: 401, contentType: 'application/json', body: '{"error":"unauthorized"}',
  }), { times: 1 });
  await remote.librarySearch.fill('grace');
  await expect(remote.gate).toBeVisible();
}

test('signing in while a reconnect waits leaves one connection', async ({ remotePage: remote }) => {
  const sockets = await droppableSockets(remote.page);
  await remote.signIn();
  await expect.poll(() => openCount(sockets)).toBe(1);

  // The connection drops, so the page will reconnect in a second; before
  // then it is asked for the PIN, and signed in again.
  sockets[0].drop();
  await expect(remote.offline).toBeVisible();
  await refuseOneRead(remote);
  await remote.signIn();
  await expect.poll(() => openCount(sockets)).toBe(1);

  // Past when the reconnect was to run: it was cancelled.
  await remote.page.waitForTimeout(2000);
  expect(openCount(sockets)).toBe(1);
  expect(sockets).toHaveLength(2);
});

test('a reconnect answered after signing in again keeps the device signed in', async ({ remotePage: remote }) => {
  const page = remote.page;
  const sockets = await droppableSockets(page);
  await remote.signIn();

  // The reconnect asks whether this device is still signed in, and the
  // answer is held while the device is asked for the PIN and signs in.
  let answer;
  const held = new Promise((resolve) => { answer = resolve; });
  let hellos = 0;
  await page.route(/\/api\/hello$/, async (route) => {
    hellos += 1;
    if (hellos > 1) return route.fallback();
    await held;
    return route.fulfill({
      contentType: 'application/json', body: '{"needsPin":true,"authed":false}',
    });
  });
  const asked = page.waitForRequest(/\/api\/hello$/);
  sockets[0].drop();
  await asked;
  await refuseOneRead(remote);
  await remote.signIn();

  // The old answer arrives: about a token from before, so it is let go.
  const answered = page.waitForResponse(/\/api\/hello$/);
  answer();
  await answered;
  // Give the page a moment to act on it.
  await page.evaluate(() => new Promise((resolve) => setTimeout(resolve, 200)));
  await expect(remote.gate).toBeHidden();
  await expect(remote.appRoot).toBeVisible();
  expect(openCount(sockets)).toBe(1);
});

test('a device asks for the PIN again when the app restarts', async ({ app, observer, remote }) => {
  await observer.run('openService', { serviceId: app.fixture.serviceId });
  await remote.expectService('Sunday Service');

  // Tokens do not outlive the app, so the page's is refused: it asks for the
  // PIN rather than retrying behind "Reconnecting".
  await app.restart();
  await expect(remote.gate).toBeVisible({ timeout: 10_000 });
  await expect(remote.gateError).toHaveText('Session expired. Enter the PIN again.');
  await expect(remote.offline).toBeHidden();

  await remote.signIn();
  // A new run of the app, with nothing open.
  await remote.expectService('');
});
