// Pictures, video and audio: the Images and Videos tabs, and the transport.

import { expect, test } from '../harness/test.mjs';

/** Wait until the image ``locator`` names has decoded a picture. */
async function expectLoaded(locator) {
  await expect.poll(() => locator.evaluate((img) => img.complete && img.naturalWidth),
    { message: 'the picture to load' }).toBeGreaterThan(0);
}

test.describe('pictures', () => {
  test.beforeEach(async ({ remote }) => {
    await remote.showLibrary('Images');
  });

  test('folders come first, then pictures, each with its thumbnail', async ({ remote }) => {
    await expect(remote.libraryRows.locator('.row-title'))
      .toHaveText(['Backgrounds', 'Seasons', 'city.png', 'snow.png', 'sunrise.png']);
    await expectLoaded(remote.libraryRow('city.png').locator('img'));
  });

  test('a folder opens on a double-click, and ".." comes back', async ({ remote }) => {
    await remote.libraryRow('Backgrounds').dblclick();
    // A folder keeps its own order.
    await expect(remote.libraryRows.locator('.row-title')).toHaveText(['..', 'ocean.png', 'forest.png']);
    await remote.libraryRow('..').dblclick();
    await expect(remote.libraryRows).toHaveCount(5);

    await remote.libraryRow('Seasons').dblclick();
    await expect(remote.libraryList).toContainText('No media in this folder');
  });

  test('a picture plays on its own, shown whole', async ({ remote, observer }) => {
    await remote.libraryRow('city.png').dblclick();
    await observer.waitFor((s) => s.session.mode === 'image' && s.images.images[0] === 'city.png',
      'city.png live');
    await expect(remote.mediaPreview).toHaveAttribute('src', '/api/thumb/images/city.png?w=960');
    await expectLoaded(remote.mediaPreview);
  });

  test('a folder in the service plays as a gallery', async ({ app, remote, observer }) => {
    await remote.openServiceFrom(observer, app.fixture.serviceId, 'Sunday Service');
    await observer.run('addImageFolderToService', { folderId: app.fixture.imageFolderId });
    await expect.poll(() => remote.serviceTitles().then((t) => t.at(-1))).toBe('7. Backgrounds');
    await expect(remote.serviceRows.last().locator('.row-sub')).toHaveText('2');

    await remote.serviceRows.last().dblclick();
    await expect(remote.thumbs).toHaveCount(2);
    await expect(remote.thumbs.first()).toHaveClass(/\bactive\b/);
    await expectLoaded(remote.thumbs.first().locator('img'));
    await remote.thumbs.nth(1).click();
    await expect(remote.thumbs.nth(1)).toHaveClass(/\bactive\b/);
    await observer.waitFor((s) => s.images.index === 1, 'the second picture live');
  });
});

test.describe('video and audio', () => {
  test('a video plays with the transport: pause, seek and restart', async ({ remote, observer }) => {
    await remote.playFromLibrary('Videos', 'clip.mp4');
    await expect(remote.videoControls).toBeVisible();
    await expect(remote.slideControls).toBeHidden();
    await expect(remote.videoBar).toBeVisible();
    await expect(remote.videoDuration).toHaveText('0:20');
    await expect(remote.videoPlay).toHaveText('Pause');
    await observer.waitFor((s) => s.session.videoPlaying, 'the video playing');

    // No picture of it here: the app's mark, and the transport.
    await expect(remote.liveMark).toBeVisible();
    await expect(remote.mediaPreview).toHaveCount(0);

    await remote.videoPlay.click();
    await expect(remote.videoPlay).toHaveText('Play');
    await observer.waitFor((s) => !s.session.videoPlaying, 'the video paused');

    // Let go of the bar halfway: the slider is in thousandths.
    await remote.videoSeek.evaluate((bar) => {
      bar.value = '500';
      bar.dispatchEvent(new Event('change'));
    });
    await observer.waitFor((s) => Math.abs(s.session.videoPositionSec - 10) < 0.5, 'at 0:10');
    await expect(remote.videoPosition).toHaveText('0:10');

    await remote.videoRestart.click();
    await observer.waitFor((s) => s.session.videoPlaying && s.session.videoPositionSec < 3,
      'playing from the start');
    await expect(remote.videoPlay).toHaveText('Pause');
  });

  test('a press on the seek bar that does not move it lets go of the bar', async ({ remote, observer }) => {
    await remote.playFromLibrary('Videos', 'clip.mp4');
    await observer.waitFor((s) => s.session.videoPlaying, 'the video playing');

    // Pressed and let go where it is: no change fires, and the bar follows
    // the clock again rather than staying where it was pressed.
    await remote.videoSeek.evaluate((bar) => {
      bar.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));
      bar.dispatchEvent(new PointerEvent('pointerup', { bubbles: true }));
    });
    const pressedAt = Number(await remote.videoSeek.inputValue());
    // Thousandths of 20 s: a second on.
    await expect.poll(async () => Number(await remote.videoSeek.inputValue()))
      .toBeGreaterThan(pressedAt + 50);
  });

  // The clips here are short, so the clock is read off the page's own module:
  // past an hour it shows the hours, as the console's does.
  test('the transport clock shows hours past an hour, as the console does', async ({ remote }) => {
    const read = await remote.page.evaluate(async () => {
      const { clock } = await import('/js/dom.js');
      return [0, 65, 3599, 3600, 4503, 36000 + 62].map(clock);
    });
    expect(read).toEqual(['0:00', '1:05', '59:59', '1:00:00', '1:15:03', '10:01:02']);
  });

  test('Blank works from the video controls too', async ({ remote, observer }) => {
    await remote.playFromLibrary('Videos', 'clip.mp4');
    await expect(remote.videoControls).toBeVisible();
    await remote.videoBlank.click();
    await expect(remote.videoBlank).toHaveClass(/\bis-blank\b/);
    await observer.waitFor((s) => s.outputs.list.every((o) => o.isBlank), 'every output blank');
  });

  test('audio plays with the transport and no picture', async ({ app, remote, observer }) => {
    await remote.openServiceFrom(observer, app.fixture.mediaServiceId, 'Media');
    await remote.serviceRow('Tone.wav').dblclick();
    await expect(remote.videoControls).toBeVisible();
    await expect(remote.videoDuration).toHaveText('0:04');
    await observer.waitFor((s) => s.session.mediaTransport && !s.session.videoMode
      && s.session.videoPlaying, 'the audio playing');
    await expect(remote.mediaPreview).toHaveCount(0);
    await expect(remote.lines).toHaveCount(0);

    await remote.videoPlay.click();
    await observer.waitFor((s) => !s.session.videoPlaying, 'the audio paused');
  });
});
