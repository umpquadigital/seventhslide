// The service: the one the console has open, and playing its rundown.

import { RemotePage } from '../harness/remote-page.mjs';
import { expect, test } from '../harness/test.mjs';

const SUNDAY = [
  '1. Amazing Grace', '2. Holy Holy Holy', 'Scripture', '3. John 3:16-17', '4. sunrise.png',
  '5. Backgrounds', '6. Welcome',
];

test('the panel shows the service the console has open, and offers no edits', async ({ app, remote, observer }) => {
  await remote.expectService('');
  // No service open: the well is empty, as the console's.
  await expect(remote.serviceList).toBeEmpty();

  await remote.openServiceFrom(observer, app.fixture.serviceId, 'Sunday Service');
  await expect.poll(() => remote.serviceTitles()).toEqual(SUNDAY);
  // A playback remote: the panel is its title bar and the rundown, with no
  // toolbar; nothing in it adds, removes, moves or opens.
  await expect(remote.servicePanel.locator('.dock-title')).toHaveText('Service');
  await expect(remote.servicePanel.locator('.dock-title + .panel-body > *')).toHaveCount(1);
  await expect(remote.servicePanel.getByRole('button')).toHaveCount(0);
  await expect(remote.serviceList.locator('[draggable="true"]')).toHaveCount(0);

  // The console opens another; the panel follows.
  await observer.run('openService', { serviceId: app.fixture.otherServiceId });
  await remote.expectService('Good Friday');
  await expect(remote.serviceList).toBeEmpty();
});

test('the library plays and adds nothing to the service', async ({ remote }) => {
  await remote.libraryRow('Rock of Ages').click();
  await expect(remote.libraryPanel.getByRole('button', { name: /service/i })).toHaveCount(0);
});

test('a first click, however soon after the page loads, selects and does not open', async ({ app, page }) => {
  // The page's clock held a moment after loading: a click there is still a
  // first click, not the second of a double.
  await page.clock.install({ time: new Date('2026-10-19T10:00:00') });
  await page.clock.pauseAt(new Date('2026-10-19T10:00:00.100'));
  const remote = new RemotePage(page, app);
  await remote.open();
  await remote.signIn();

  await remote.showLibrary('Images');
  await remote.libraryRow('Backgrounds').click();
  await expect(remote.libraryRow('Backgrounds')).toHaveClass(/\bmarquee-selected\b/);
  await expect(remote.libraryRow('city.png')).toBeVisible();
});

test.describe('the rundown', () => {
  test.beforeEach(async ({ app, remote, observer }) => {
    await remote.openServiceFrom(observer, app.fixture.serviceId, 'Sunday Service');
  });

  test('a double-click puts an item live, once', async ({ remote, observer }) => {
    await remote.serviceRow('Holy Holy Holy').dblclick();
    await observer.waitFor((s) => s.session.serviceItemIndex === 1, 'item 2 live');
    await expect(remote.serviceRow('Holy Holy Holy')).toHaveClass(/\bplaying\b/);
    await expect(remote.currentWords).toHaveText('Holy holy holy');
    expect(remote.commands('selectServiceItem')).toEqual([
      { name: 'selectServiceItem', args: { index: 1 } },
    ]);

    // Selected already: the first click plays, the second does not again.
    await remote.page.waitForTimeout(600);
    await remote.serviceRow('Amazing Grace').click();
    await remote.page.waitForTimeout(600);
    await remote.serviceRow('Amazing Grace').dblclick();
    await observer.waitFor((s) => s.session.serviceItemIndex === 0, 'item 1 live');
    await remote.page.waitForTimeout(300);
    expect(remote.commands('selectServiceItem')).toHaveLength(2);
  });

  test('one row is selected at a time', async ({ remote }) => {
    await remote.serviceRow('Holy Holy Holy').click();
    await remote.serviceRow('sunrise.png').click({ modifiers: ['ControlOrMeta'] });
    await expect(remote.serviceRow('sunrise.png')).toHaveClass(/\bmarquee-selected\b/);
    await expect(remote.serviceList.locator('.marquee-selected')).toHaveCount(1);
  });

  test('an image folder opens to its pictures, and each plays', async ({ remote, observer }) => {
    const folder = remote.serviceRow('Backgrounds');
    await expect(folder.locator('.row-sub')).toHaveText('2');
    await folder.locator('.folder-chevron').click();
    const pictures = remote.serviceList.locator('[data-image-index]');
    await expect(pictures.locator('.row-title')).toHaveText(['ocean.png', 'forest.png']);

    await pictures.nth(1).click();
    await observer.waitFor((s) => s.images.active && s.images.index === 1,
      'the folder live on its second picture');
    await expect(pictures.nth(1)).toHaveClass(/\bplaying\b/);
    await expect(remote.thumbs).toHaveCount(2);
    await expect(remote.thumbs.nth(1)).toHaveClass(/\bactive\b/);
  });
});
