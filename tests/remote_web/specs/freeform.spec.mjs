// The Freeform tab and a Freeform deck live: its slides as pictures.

import { expect, test } from '../harness/test.mjs';

async function expectLoaded(locator) {
  await expect.poll(() => locator.evaluate((img) => img.complete && img.naturalWidth),
    { message: 'the picture to load' }).toBeGreaterThan(0);
}

test.beforeEach(async ({ remote }) => {
  await remote.showLibrary('Freeform');
});

test('folders and items are listed, each item with its first slide', async ({ remote }) => {
  // Items keep the order they were put in.
  await expect(remote.libraryRows.locator('.row-title'))
    .toHaveText(['Announcements', 'Welcome', 'Offering', 'Three slides']);
  await expectLoaded(remote.libraryRow('Welcome').locator('img'));

  await remote.libraryRow('Announcements').dblclick();
  await expect(remote.libraryList).toContainText('No Freeform items in this folder');
  await remote.libraryRow('..').dblclick();
  await expect(remote.libraryRows).toHaveCount(4);
});

test('a deck plays as its slides, and a slide clicked goes live', async ({ remote, observer }) => {
  await remote.libraryRow('Three slides').dblclick();
  await expect(remote.thumbs).toHaveCount(3);
  await expect(remote.thumbs.locator('.img-thumb-num')).toHaveText(['1', '2', '3']);
  await expect(remote.thumbs.first()).toHaveClass(/\bactive\b/);
  for (let i = 0; i < 3; i += 1) await expectLoaded(remote.thumbs.nth(i).locator('img'));

  await remote.thumbs.nth(2).click();
  await expect(remote.thumbs.nth(2)).toHaveClass(/\bactive\b/);
  await observer.waitFor((s) => s.session.lineCursor === 2, 'the third slide live');

  await remote.prev.click();
  await expect(remote.thumbs.nth(1)).toHaveClass(/\bactive\b/);
  await observer.waitFor((s) => s.session.lineCursor === 1, 'the second slide live');
});

test('each deck\'s slides are its own pictures', async ({ remote }) => {
  // Slide 1 of one deck is another picture from slide 1 of the next.
  await remote.libraryRow('Three slides').dblclick();
  await expect(remote.thumbs).toHaveCount(3);
  const first = await remote.thumbs.first().locator('img').getAttribute('src');

  await remote.page.waitForTimeout(600);
  await remote.libraryRow('Welcome').dblclick();
  await expect(remote.thumbs).toHaveCount(1);
  await expect(remote.thumbs.first().locator('img')).not.toHaveAttribute('src', first);
  await expectLoaded(remote.thumbs.first().locator('img'));
});

// The server turns a picture away (503) when too many wait to be drawn; the
// page asks for it once more, a second later.
const busy = { status: 503, headers: { 'Retry-After': '1' }, contentType: 'application/json',
  body: '{"error":"busy"}' };

test('a slide the server was too busy to draw is asked for again', async ({ remote }) => {
  const asked = [];
  await remote.page.route(/\/api\/preview\/.*[?&]slide=1&/, (route) => {
    asked.push(route.request().url());
    return asked.length === 1 ? route.fulfill(busy) : route.fallback();
  });
  await remote.libraryRow('Three slides').dblclick();
  await expect(remote.thumbs).toHaveCount(3);
  await expectLoaded(remote.thumbs.nth(1).locator('img'));
  expect(asked).toHaveLength(2);
  expect(asked[1]).toBe(asked[0]);
});

test('an item\'s picture the server was too busy to draw is asked for again', async ({ remote }) => {
  // Shown again from the start, with Welcome's picture turned away once.
  const welcome = await remote.libraryRow('Welcome').locator('img').getAttribute('src');
  const asked = [];
  await remote.page.route((url) => url.pathname + url.search === welcome, (route) => {
    asked.push(route.request().url());
    return asked.length === 1 ? route.fulfill(busy) : route.fallback();
  });
  await remote.page.reload();
  await remote.waitForSnapshot();
  await remote.showLibrary('Freeform');
  await expectLoaded(remote.libraryRow('Welcome').locator('img'));
  expect(asked).toHaveLength(2);
});

test('an item\'s picture refused twice leaves its glyph', async ({ remote }) => {
  const welcome = await remote.libraryRow('Welcome').locator('img').getAttribute('src');
  await remote.page.route((url) => url.pathname + url.search === welcome,
    (route) => route.fulfill(busy));
  await remote.page.reload();
  await remote.waitForSnapshot();
  await remote.showLibrary('Freeform');
  await expect(remote.libraryRow('Welcome').locator('img')).toHaveCount(0);
  await expect(remote.libraryRow('Welcome').locator('.lib-thumb-box .ic')).toHaveCount(1);
});
