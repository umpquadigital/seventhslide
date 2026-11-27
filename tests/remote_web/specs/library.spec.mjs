// The library's lists: read a page at a time as they scroll, and a search
// that is typed on answered by its own rows only.

import { expect, test } from '../harness/test.mjs';

test.describe('a long list', () => {
  // 250 more songs: the six the sample has, then "Hymn 001" to "Hymn 250".
  test.use({ library: { songs: 250 } });

  test('reads on as it scrolls, to its last row', async ({ remote }) => {
    await expect(remote.libraryRows.first()).toBeVisible();
    // One page now, not all of it.
    await expect(remote.libraryRows).toHaveCount(100);
    await expect(remote.libraryPanel.getByRole('button', { name: /load more/i })).toHaveCount(0);

    await expect(async () => {
      await remote.libraryList.evaluate((el) => { el.scrollTop = el.scrollHeight; });
      await expect(remote.libraryRows).toHaveCount(256, { timeout: 500 });
    }).toPass();
    await expect(remote.libraryRows.last().locator('.row-title')).toHaveText('Rock of Ages');
  });

  test('starts at the top again for a new search', async ({ remote }) => {
    await remote.libraryList.evaluate((el) => { el.scrollTop = el.scrollHeight; });
    await expect(remote.libraryRows).not.toHaveCount(100);
    await remote.librarySearch.fill('hymn');
    await expect(remote.libraryRows.first().locator('.row-title')).toHaveText('Hymn 001');
    await expect(remote.libraryRows).toHaveCount(100);
    expect(await remote.libraryList.evaluate((el) => el.scrollTop)).toBe(0);
  });
});

test('an answer for an older search does not replace a newer one', async ({ remote }) => {
  // The answer for "a" comes late, after the one for "amazing".
  await remote.page.route(/\/api\/library\/songs\?q=a&/, async (route) => {
    await new Promise((resolve) => setTimeout(resolve, 800));
    await route.continue();
  });
  const slow = remote.page.waitForResponse(/\/api\/library\/songs\?q=a&/);
  await remote.librarySearch.fill('a');
  await remote.page.waitForRequest(/\/api\/library\/songs\?q=a&/);
  await remote.librarySearch.fill('amazing');
  await expect(remote.libraryRows.locator('.row-title')).toHaveText(['Amazing Grace']);
  await slow;
  await remote.page.waitForTimeout(100);
  await expect(remote.libraryRows.locator('.row-title')).toHaveText(['Amazing Grace']);
});

test('from the keyboard, the arrows choose a row and Enter plays it', async ({ remote, observer }) => {
  await expect(remote.libraryRows.first()).toBeVisible();
  // Into the library's list by Tab, from its search field.
  await remote.librarySearch.focus();
  await remote.page.keyboard.press('Tab');
  await expect(remote.libraryList).toBeFocused();
  await remote.page.keyboard.press('ArrowDown');
  await remote.page.keyboard.press('ArrowDown');
  await expect(remote.libraryRows.nth(1)).toHaveClass(/\bmarquee-selected\b/);
  await expect(remote.libraryRows.nth(1)).toHaveAttribute('aria-selected', 'true');
  expect(remote.commands('playSong')).toEqual([]);
  await remote.page.keyboard.press('Enter');
  await observer.waitFor((s) => s.session.mode === 'song', 'the song live');
  expect(remote.commands('playSong')).toHaveLength(1);
});

test('a search finds what the console finds: more than one word, and lyrics', async ({ remote }) => {
  await remote.librarySearch.fill('amazing grace');
  await expect(remote.libraryRows.locator('.row-title')).toHaveText(['Amazing Grace']);

  // Found by a lyric line, the row shows the line.
  await remote.librarySearch.fill('saved a wretch');
  await expect(remote.libraryRows.locator('.row-title')).toHaveText(['Amazing Grace']);
  await expect(remote.libraryRows.locator('.row-sub.is-lyric')).toHaveText('That saved a wretch like me');
});
