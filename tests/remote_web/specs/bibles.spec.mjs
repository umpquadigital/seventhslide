// The Bibles tab: looking up a reference and playing verses.

import { expect, test } from '../harness/test.mjs';

const JOHN_3 = [
  'For God so loved the world, that he gave his only Son.',
  'For God did not send his Son into the world to condemn the world.',
  'Whoever believes in him is not condemned.',
];

test.beforeEach(async ({ remote }) => {
  await remote.showLibrary('Bibles');
  await expect(remote.bibleSelect).toHaveText('Sample Bible');
});

test('a reference lists its verses, and a double-click plays one, once', async ({ remote, observer }) => {
  await remote.bibleReference.fill('John 3');
  await expect(remote.verses.locator('.verse-num')).toHaveText(['16', '17', '18']);
  await expect(remote.verses.locator('.row-title')).toHaveText(JOHN_3);

  await remote.verses.nth(1).dblclick();
  await observer.waitFor((s) => s.session.mode === 'bible', 'the verse live');
  await expect(remote.lines.first()).toContainText('For God did not send');
  expect(remote.commands('playBibleRef')).toEqual([{
    name: 'playBibleRef',
    args: { bibleId: remote.app.fixture.bibleId, book: 'John', chapter: 3, verseStart: 17, verseEnd: 17 },
  }]);

  // The verse is selected now: its first click plays, the second not again.
  await remote.page.waitForTimeout(600);
  await remote.verses.nth(1).dblclick();
  await remote.page.waitForTimeout(400);
  expect(remote.commands('playBibleRef')).toHaveLength(2);
});

test('Enter plays every verse the reference found', async ({ remote, observer }) => {
  await remote.bibleReference.fill('Ps 23');
  await expect(remote.verses).toHaveCount(2);
  await remote.bibleReference.press('Enter');
  await observer.waitFor((s) => s.session.mode === 'bible', 'the passage live');
  expect(remote.commands('playBibleRef').at(-1).args)
    .toMatchObject({ book: 'Psalms', chapter: 23, verseStart: 1, verseEnd: 2 });
});

test('a reference that finds nothing says why, unless books are suggested', async ({ remote }) => {
  await remote.bibleReference.fill('Hezekiah 1');
  await expect(remote.libraryEmpty).toHaveText('Could not find "Hezekiah 1" in this bible.');
  await remote.bibleReference.fill('John 3:99');
  await expect(remote.libraryEmpty).toHaveText('John 3 has no verse 99.');

  // The suggestions are the answer while there are any.
  await remote.bibleReference.fill('Jo');
  await expect(remote.bibleSuggestions).toHaveText(['John']);
  await expect(remote.libraryEmpty).toHaveCount(0);

  await remote.bibleReference.fill('John 3');
  await expect(remote.verses).toHaveCount(3);
  await expect(remote.libraryEmpty).toHaveCount(0);
  await remote.bibleReference.fill('');
  await expect(remote.verses).toHaveCount(0);
  await expect(remote.libraryEmpty).toHaveCount(0);
});

test('a partial book name is suggested, and Tab takes it', async ({ remote }) => {
  await remote.bibleReference.fill('Jo');
  await expect(remote.bibleSuggestions).toHaveText(['John']);
  await remote.bibleReference.press('Tab');
  await expect(remote.bibleReference).toHaveValue('John ');
  await expect(remote.bibleReference).toBeFocused();
  await remote.bibleReference.pressSequentially('3');
  await expect(remote.verses).toHaveCount(3);
  await expect(remote.bibleSuggestions).toHaveCount(0);
});

test('one verse is selected at a time', async ({ remote }) => {
  await remote.bibleReference.fill('John 3');
  await expect(remote.verses).toHaveCount(3);
  await remote.verses.nth(0).click();
  await remote.verses.nth(2).click({ modifiers: ['Shift'] });
  await expect(remote.libraryList.locator('.verse-item.marquee-selected')).toHaveCount(1);
  await expect(remote.verses.nth(2)).toHaveClass(/\bmarquee-selected\b/);
});

test('the Bible menu lists the Bibles, the current one marked', async ({ remote }) => {
  await remote.bibleSelect.click();
  await expect(remote.bibleMenu).toBeVisible();
  await expect(remote.bibleMenu.locator('.menu-item')).toHaveText(['Sample Bible']);
  await expect(remote.bibleMenu.locator('.menu-item.is-chosen')).toHaveText('Sample Bible');
  // Escape closes it, as a tap outside does.
  await remote.page.keyboard.press('Escape');
  await expect(remote.bibleMenu).toBeHidden();
  await expect(remote.bibleSelect).toBeFocused();
  await remote.bibleSelect.click();
  await expect(remote.bibleMenu).toBeVisible();
  await remote.page.locator('body').click({ position: { x: 5, y: 5 } });
  await expect(remote.bibleMenu).toBeHidden();
});

test('one menu is open at a time', async ({ remote }) => {
  await remote.bibleSelect.click();
  await expect(remote.bibleMenu).toBeVisible();
  await remote.openControllerMenu();
  await expect(remote.bibleMenu).toBeHidden();
  await remote.page.keyboard.press('Escape');
  await expect(remote.controllerMenu).not.toHaveClass(/\bopen\b/);
});

test('the reference field is a whole field, under the Bible menu', async ({ remote }) => {
  const field = await remote.page.locator('#bibleSearchField').boundingBox();
  const menu = await remote.bibleSelect.boundingBox();
  expect(field.height).toBeGreaterThanOrEqual(menu.height);
  expect(field.y).toBeGreaterThanOrEqual(menu.y + menu.height);
});
