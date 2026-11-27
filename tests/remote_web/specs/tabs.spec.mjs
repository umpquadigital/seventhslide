// The library tabs the console has beyond the first five: Audio, and behind
// the strip's arrow Browser and Live; each plays, and the Controller shows
// what it is playing.

import { exactly, expect, test } from '../harness/test.mjs';

test('the strip shows five tabs, and its arrow the rest', async ({ remote }) => {
  const shown = remote.libraryTabs.locator('.tab-page:not([inert]) .tab-btn');
  await expect(shown).toHaveText(['Songs', 'Freeform', 'Bibles', 'Videos', 'Images']);
  await remote.moreTabs.click();
  await expect(shown).toHaveText(['Audio', 'Browser', 'Live']);
  await remote.firstTabs.click();
  await expect(shown).toHaveText(['Songs', 'Freeform', 'Bibles', 'Videos', 'Images']);
});

test('the tab shown is kept across a reload', async ({ remote }) => {
  await remote.showLibrary('Live');
  await remote.page.reload();
  await remote.waitForSnapshot();
  await expect(remote.libraryTab('Live')).toHaveClass(/\bactive\b/);
  await expect(remote.librarySearch).toHaveAttribute('placeholder', 'Search Live…');
});

test('Audio lists its folders and files, and a double-click plays one', async ({ remote, observer }) => {
  await remote.showLibrary('Audio');
  await expect(remote.libraryRows.locator('.row-title'))
    .toHaveText(['Preludes', 'Gathering pad.wav', 'Postlude.flac', 'Tone.wav']);

  await remote.libraryRow('Tone.wav').dblclick();
  await observer.waitFor((s) => s.session.mode === 'audio', 'the audio live');
  expect(remote.commands('playAudio')).toEqual([
    { name: 'playAudio', args: { filename: 'Tone.wav' } },
  ]);
  // No picture and no words: the app's mark, and the transport.
  await expect(remote.liveMark).toBeVisible();
  await expect(remote.videoControls).toBeVisible();

  await remote.page.waitForTimeout(600);
  await remote.libraryRow('Preludes').dblclick();
  await expect(remote.libraryRows.locator('.row-title')).toHaveText(['..', 'Prelude in G.mp3']);
});

test('Browser lists the pages, and one plays with Previous and Reload', async ({ remote, observer }) => {
  await remote.showLibrary('Browser');
  await expect(remote.libraryRows.locator('.row-title'))
    .toHaveText(['Announcements', 'Countdown', 'Welcome slides']);

  await remote.libraryRow('Countdown').dblclick();
  await observer.waitFor((s) => s.session.webMode, 'the page live');
  expect(remote.commands('playWebPage')).toHaveLength(1);
  await expect(remote.webControls).toBeVisible();
  await expect(remote.slideControls).toBeHidden();
  await expect(remote.webAddress).toHaveText('https://example.com/countdown');
  // No page before this one yet.
  await expect(remote.webBack).toBeDisabled();
  await remote.webReload.click();
  await expect.poll(() => remote.commands('webReload').length).toBe(1);
});

test('Live lists the sources, and one plays', async ({ remote, observer }) => {
  await remote.showLibrary('Live');
  // The stream is kept by the library; the list comes as it is read again.
  const stream = remote.libraryRow('Overflow room');
  await expect(stream).toBeVisible({ timeout: 8_000 });
  // Grouped by kind, as on the console.
  await expect(remote.libraryList.locator('.lib-group-head')).toHaveText([exactly('Stream')]);

  await stream.dblclick();
  await observer.waitFor((s) => s.session.liveSourceMode, 'the source live');
  expect(remote.commands('playLiveSource')).toEqual([
    { name: 'playLiveSource', args: { kind: 'stream', name: 'srt://127.0.0.1:9' } },
  ]);
  await expect(remote.live.locator('.media-preview')).toBeVisible();
});
