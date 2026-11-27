// Pictures of the remote in its main states, at a desktop's, a tablet's and a
// phone's size, for reviewing a change to how it looks beside the app's own
// (tests/ui_shots.cpp). Not compared by pixel; not part of the default run:
//
//     SEVENTHSLIDE_REMOTE_SHOTS_DIR=out npx playwright test --project=shots
//
// What a shot sets up goes through the protocol (the observer) where it can,
// and through RemotePage where only the page can do it, so a change to the
// markup is fixed in one place.

import path from 'node:path';

import { expect, test } from '../harness/test.mjs';

const SIZES = {
  desktop: { width: 1280, height: 800 },
  tablet: { width: 900, height: 1180 },
  phone: { width: 390, height: 844 },
};

const dir = process.env.SEVENTHSLIDE_REMOTE_SHOTS_DIR || 'test-results/shots';

for (const [size, viewport] of Object.entries(SIZES)) {
  test.describe(size, () => {
    test.use({ viewport });

    test(`the remote at ${size} size`, async ({ app, remotePage: remote, observer }) => {
      test.setTimeout(90_000);
      const page = remote.page;
      const shot = async (name) => {
        // Thumbnails and the preview load after the rows: let them land.
        await page.waitForLoadState('networkidle');
        await page.waitForTimeout(250);
        await page.screenshot({ path: path.join(dir, `${size}-${name}.png`) });
      };

      await shot('00-gate');
      await remote.signIn();
      await shot('01-no-service');

      await remote.openServiceFrom(observer, app.fixture.serviceId, 'Sunday Service');
      await observer.run('selectServiceItem', { index: 0 });
      await expect(remote.currentWords).toHaveText('Amazing grace how sweet the sound');
      await observer.run('nextSlide');
      await shot('02-song-live');

      await remote.openControllerMenu();
      await shot('03-controller-menu');
      await remote.closeControllerMenu();

      // Narrow screens keep the service and the library in a drawer.
      const drawer = await remote.drawerButton.isVisible();
      if (drawer) await remote.openDrawer();
      for (const tab of remote.libraryTabLabels) {
        await remote.showLibrary(tab);
        await shot(`04-library-${tab.toLowerCase()}`);
      }
      if (drawer) await remote.closeDrawer();

      // A second stack live: the strip shows.
      await observer.run('selectServiceItem', { index: 3 });
      await expect(remote.stackTabs).toHaveCount(2);
      await shot('05-two-stacks');

      await observer.run('clearAllStacks');
      await observer.run('playFreeform', { freeformId: app.fixture.threeSlidesId });
      await expect(remote.thumbs).toHaveCount(3);
      await shot('06-freeform-gallery');

      await observer.run('playVideo', { filename: 'clip.mp4' });
      await expect(remote.videoControls).toBeVisible();
      // Paused once it has a picture to show.
      await observer.waitFor((st) => st.session.videoPositionSec > 0.5, 'the clip running');
      await observer.run('videoPause');
      await shot('07-video');

      await observer.run('toggleGlobalBlank');
      await shot('08-blank');
      await observer.run('clearBlankFreeze');

      // What the other tabs play: a web page, a live source, audio.
      const playFrom = async (tab, title) => {
        if (drawer) await remote.openDrawer();
        await remote.playFromLibrary(tab, title);
        if (drawer) await remote.closeDrawer();
      };
      await playFrom('Browser', 'Countdown');
      await expect(remote.webControls).toBeVisible();
      await shot('09-web-page');
      await playFrom('Live', 'Overflow room');
      await observer.waitFor((st) => st.session.liveSourceMode, 'the source live');
      await shot('10-live-source');
      await playFrom('Audio', 'Tone.wav');
      await expect(remote.liveMark).toBeVisible();
      await shot('11-audio');

      // A stack with a Preview: a song live on it, another cued beside it.
      await observer.run('clearAllStacks');
      await observer.run('armSendTo', { stackId: app.fixture.previewStack.id });
      await playFrom('Songs', 'Amazing Grace');
      await expect(remote.previewCurrentWords).toHaveText('Amazing grace how sweet the sound');
      await remote.transition.click();
      await expect(remote.currentWords).toHaveText('Amazing grace how sweet the sound');
      await page.waitForTimeout(600);
      await playFrom('Songs', 'Holy Holy Holy');
      await expect(remote.previewCurrentWords).toHaveText('Holy holy holy');
      await shot('12-preview');
    });
  });
}
