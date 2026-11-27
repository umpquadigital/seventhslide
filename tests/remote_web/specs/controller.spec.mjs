// The song library and the controller: putting a song live, moving through
// it, blank and freeze, and the picture of a page live.

import { expect, test } from '../harness/test.mjs';

const GRACE = [
  'Amazing grace how sweet the sound',
  'That saved a wretch like me',
  'I once was lost but now am found',
  'Was blind but now I see',
  'Twas grace that taught my heart to fear',
  'And grace my fears relieved',
  'How precious did that grace appear',
  'The hour I first believed',
];

test('the song library lists, searches and clears', async ({ remote }) => {
  await expect(remote.libraryTab('Songs')).toHaveClass(/\bactive\b/);
  await expect(remote.libraryRows.locator('.row-title')).toHaveText([
    'Amazing Grace', 'Be Thou My Vision', 'Come Thou Fount', 'Holy Holy Holy',
    'It Is Well', 'Rock of Ages',
  ]);
  await expect(remote.libraryRow('Holy Holy Holy').locator('.row-sub'))
    .toHaveText('Reginald Heber, John B. Dykes');

  await remote.librarySearch.fill('grace');
  await expect(remote.libraryRows.locator('.row-title')).toHaveText(['Amazing Grace']);
  await remote.librarySearch.fill('no such song');
  await expect(remote.libraryList).toHaveText('Nothing matches your search.');

  await remote.librarySearchClear.click();
  await expect(remote.librarySearch).toHaveValue('');
  await expect(remote.libraryRows).toHaveCount(6);
});

test('a double-click puts a song live, once', async ({ remote, observer }) => {
  await expect(remote.liveMark).toBeVisible();
  await remote.playFromLibrary('Songs', 'Amazing Grace');

  await expect(remote.lines.locator('.line-content')).toHaveText(GRACE);
  await expect(remote.currentWords).toHaveText(GRACE[0]);
  await expect(remote.title).toHaveText('Amazing Grace');
  await observer.waitFor((s) => s.session.mode === 'song' && s.session.lineCursor === 0,
    'the song to be live on its first line');
  expect(remote.commands('playSong')).toEqual([
    { name: 'playSong', args: { songId: remote.app.fixture.firstSongId } },
  ]);
});

test('a double-click on the selected song plays it once', async ({ remote, observer }) => {
  const row = remote.libraryRow('Amazing Grace');
  await row.click();
  await expect(row).toHaveClass(/\bmarquee-selected\b/);
  await remote.page.waitForTimeout(600);
  // Its first click plays (the row is selected); the second does not again.
  await row.dblclick();
  await observer.waitFor((s) => s.session.currentTitle === 'Amazing Grace', 'Amazing Grace live');
  await remote.page.waitForTimeout(300);
  expect(remote.commands('playSong')).toHaveLength(1);
});

test('a click selects a song and a second click plays it', async ({ remote, observer }) => {
  const row = remote.libraryRow('Holy Holy Holy');
  await row.click();
  await expect(row).toHaveClass(/\bmarquee-selected\b/);
  expect(remote.commands('playSong')).toEqual([]);

  // Apart, so it is not a double-click.
  await remote.page.waitForTimeout(600);
  await row.click();
  await observer.waitFor((s) => s.session.currentTitle === 'Holy Holy Holy', 'Holy Holy Holy live');
  expect(remote.commands('playSong')).toHaveLength(1);
});

test('the lines of the live slide are marked, and the next slide\'s first', async ({ remote }) => {
  await remote.playFromLibrary('Songs', 'Amazing Grace');
  await expect(remote.currentWords).toHaveText(GRACE[0]);
  // The rest of the first slide shows; the next slide starts at line 4.
  await expect(remote.live.locator('.lyric-line.visible .line-content'))
    .toHaveText(GRACE.slice(1, 4));
  await expect(remote.live.locator('.lyric-line.next-line .line-content')).toHaveText(GRACE[4]);
});

test('Next and Previous move by slide, from the buttons and the keys', async ({ remote, observer }) => {
  await remote.playFromLibrary('Songs', 'Amazing Grace');
  await expect(remote.currentWords).toHaveText(GRACE[0]);

  await remote.next.click();
  await expect(remote.currentWords).toHaveText(GRACE[4]);
  await observer.waitFor((s) => s.session.lineCursor === 4, 'line 4 live');

  // The last slide: Next has nowhere to go and sends nothing.
  const sentBefore = remote.commands('nextSlide').length;
  await remote.next.click();
  await expect(remote.currentWords).toHaveText(GRACE[4]);
  expect(remote.commands('nextSlide')).toHaveLength(sentBefore);

  // Previous goes to the line before the slide (the server says where).
  await remote.prev.click();
  await expect(remote.currentWords).toHaveText(GRACE[3]);
  await observer.waitFor((s) => s.session.lineCursor === 3, 'line 3 live');

  await remote.page.keyboard.press('ArrowRight');
  await expect(remote.currentWords).toHaveText(GRACE[4]);
  await remote.page.keyboard.press('ArrowLeft');
  await expect(remote.currentWords).toHaveText(GRACE[3]);
  await observer.waitFor((s) => s.session.lineCursor === 3, 'line 3 live again');
});

test('clicking a line puts it live', async ({ remote, observer }) => {
  await remote.playFromLibrary('Songs', 'Amazing Grace');
  await remote.line(GRACE[6]).click();
  await expect(remote.currentWords).toHaveText(GRACE[6]);
  await observer.waitFor((s) => s.session.lineCursor === 6, 'line 6 live');
});

test('Blank and Freeze act on every output and show it', async ({ remote, observer }) => {
  await remote.playFromLibrary('Songs', 'Amazing Grace');
  await expect(remote.currentWords).toHaveText(GRACE[0]);

  await remote.blank.click();
  await expect(remote.blank).toHaveClass(/\bis-blank\b/);
  await observer.waitFor((s) => s.outputs.list.every((o) => o.isBlank), 'every output blank');
  await remote.blank.click();
  await expect(remote.blank).not.toHaveClass(/\bis-blank\b/);
  await observer.waitFor((s) => s.outputs.list.every((o) => !o.isBlank), 'no output blank');

  await remote.freeze.click();
  await expect(remote.freeze).toHaveClass(/\bis-freeze\b/);
  await observer.waitFor((s) => s.outputs.list.every((o) => o.isFrozen), 'every output frozen');

  // The keys do the same.
  await remote.page.keyboard.press('f');
  await expect(remote.freeze).not.toHaveClass(/\bis-freeze\b/);
  await remote.page.keyboard.press('b');
  await expect(remote.blank).toHaveClass(/\bis-blank\b/);
  await observer.waitFor((s) => s.outputs.list.every((o) => o.isBlank && !o.isFrozen),
    'blank, not frozen');

  // Shift+Esc lifts every blank and freeze.
  await remote.page.keyboard.press('f');
  await observer.waitFor((s) => s.outputs.list.every((o) => o.isFrozen), 'every output frozen');
  await remote.page.keyboard.press('Shift+Escape');
  await expect(remote.blank).not.toHaveClass(/\bis-blank\b/);
  await expect(remote.freeze).not.toHaveClass(/\bis-freeze\b/);
  await observer.waitFor((s) => s.outputs.list.every((o) => !o.isBlank && !o.isFrozen),
    'nothing blank or frozen');
});

test('a command while the app is away says so, and nothing moves', async ({ app, remote }) => {
  await remote.playFromLibrary('Songs', 'Amazing Grace');
  await expect(remote.currentWords).toHaveText(GRACE[0]);
  await app.stop();
  await expect(remote.offline).toBeVisible();

  await remote.blank.click();
  await expect(remote.toasts.last()).toHaveText('Not connected');
  // Next was not sent, so the line map does not show it as if it had been.
  // Read at once: a reconnect failing later would put the line back anyway.
  await remote.next.click();
  expect(await remote.currentLine.locator('.line-content').textContent()).toBe(GRACE[0]);
  await expect(remote.toasts).toHaveCount(2);
  // And the page throws nothing (the fixture checks): a refusal is an
  // answer, not an error nobody catches.
});

test.describe('on a phone', () => {
  test.use({ viewport: { width: 390, height: 844 } });

  test('a long line wraps rather than being cut off', async ({ app, remote, observer }) => {
    await observer.run('playBibleRef', {
      bibleId: app.fixture.bibleId, book: 'John', chapter: 3, verseStart: 17, verseEnd: 17,
    });
    const line = remote.lines.filter({ hasText: 'condemn the world' });
    await expect(line).toBeVisible();
    const words = line.locator('.line-content');
    expect(await words.evaluate((el) => el.scrollWidth <= el.clientWidth)).toBe(true);
    expect((await line.boundingBox()).height).toBeGreaterThan(40);
  });
});

// A 1x1 PNG: what the page is given for every preview here.
const PIXEL = Buffer.from(
  'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNkYAAAAAYAAjCB0C8AAAAASUVORK5CYII=',
  'base64');

test('the live picture ends on the last of two revisions close together', async ({ remotePage: remote }) => {
  const page = remote.page;
  await page.route(/\/api\/preview\//, (route) => route.fulfill({ contentType: 'image/png', body: PIXEL }));
  // Between the page and the server: the session the server sent last, and,
  // once the server is held back, patches of the test's own.
  let session = null;
  let rev = 0;
  let held = false;
  let toPage = null;
  await page.routeWebSocket(/\/ws/, (ws) => {
    const server = ws.connectToServer();
    ws.onMessage((message) => server.send(message));
    server.onMessage((message) => {
      if (held) return;
      const msg = JSON.parse(String(message));
      if (msg.type === 'snapshot') session = msg.state.session;
      if (msg.type === 'patch' && msg.sections.session) session = msg.sections.session;
      if (typeof msg.rev === 'number') rev = msg.rev;
      ws.send(message);
    });
    toPage = (msg) => ws.send(JSON.stringify(msg));
  });
  await remote.signIn();
  expect(session.primaryOutputId).toBeTruthy();

  // A web page live, whose picture moves twice at once: the second inside
  // the client's 120 ms between pictures.
  held = true;
  const picture = (previewRevision) => toPage({
    type: 'patch',
    rev: ++rev,
    sections: { session: { ...session, webMode: true, hasBuild: true, previewRevision } },
  });
  picture(1);
  picture(2);
  await expect(remote.mediaPreview).toHaveAttribute('src', /-web-2$/);
});
