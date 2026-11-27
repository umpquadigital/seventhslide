// Stacks: two things live at once, each device on a tab of its own.
//
// The library's "Sunday Service" plays its reading (John 3:16-17) on the
// second stack, "Lower thirds"; everything else plays on "Main".

import { Observer } from '../harness/observer.mjs';
import { RemotePage } from '../harness/remote-page.mjs';
import { expect, test } from '../harness/test.mjs';

const GRACE_FIRST = 'Amazing grace how sweet the sound';

/** The song on Main and the reading on Lower thirds, from the page. */
async function playBoth(remote) {
  await remote.serviceRow('Amazing Grace').dblclick();
  await expect(remote.currentWords).toHaveText(GRACE_FIRST);
  await remote.page.waitForTimeout(600);
  await remote.serviceRow('John 3:16-17').dblclick();
  await expect(remote.stackTabs).toHaveCount(2);
}

test.beforeEach(async ({ app, remote, observer }) => {
  await remote.openServiceFrom(observer, app.fixture.serviceId, 'Sunday Service');
});

test('a tab shows for each stack live, from the first, and the device follows what it sends', async ({ remote }) => {
  await expect(remote.stackTabs).toHaveCount(0);
  await remote.serviceRow('Amazing Grace').dblclick();
  await expect(remote.stackTabs).toHaveText(['Main']);
  await remote.page.waitForTimeout(600);
  await remote.serviceRow('John 3:16-17').dblclick();
  await expect(remote.stackTabs).toHaveText(['Main', 'Lower thirds']);
  // Sending the reading to Lower thirds took this device's tab there.
  await expect(remote.stackTab('Lower thirds')).toHaveClass(/\bis-active\b/);
  await expect(remote.lines.first()).toContainText('For God so loved the world');

  await remote.stackTab('Main').click();
  await expect(remote.stackTab('Main')).toHaveClass(/\bis-active\b/);
  await expect(remote.currentWords).toHaveText(GRACE_FIRST);
});

test('each device has its own tab', async ({ app, browser, remote }) => {
  // A device that only watches, on the console's tab, Main.
  const bystander = await Observer.connect(app);
  try {
    await playBoth(remote);
    const other = await browser.newContext();
    try {
      const second = new RemotePage(await other.newPage(), app);
      await second.open();
      await second.signIn();
      await expect(second.stackTabs).toHaveCount(2);
      await second.stackTab('Lower thirds').click();
      await expect(second.stackTab('Lower thirds')).toHaveClass(/\bis-active\b/);
      await second.stackTab('Main').click();
      await expect(second.stackTab('Main')).toHaveClass(/\bis-active\b/);

      // What the others did moved neither the page nor the bystander.
      await remote.page.waitForTimeout(300);
      await expect(remote.stackTab('Lower thirds')).toHaveClass(/\bis-active\b/);
      expect(bystander.stacks.focusedStackId).toBe('main');
    } finally {
      await other.close();
    }
  } finally {
    bystander.close();
  }
});

test('a device keeps its tab across a reload', async ({ remote }) => {
  await playBoth(remote);
  await expect(remote.stackTab('Lower thirds')).toHaveClass(/\bis-active\b/);
  await remote.page.reload();
  await remote.waitForSnapshot();
  await expect(remote.stackTab('Lower thirds')).toHaveClass(/\bis-active\b/);
});

test('Blank acts on every output whichever tab shows; Shift+B on the device\'s stack', async ({ app, remote, observer }) => {
  await playBoth(remote);
  await remote.blank.click();
  await expect(remote.blank).toHaveClass(/\bis-blank\b/);
  await observer.waitFor((s) => s.outputs.list.every((o) => o.isBlank), 'every output blank');
  await expect(remote.stackTab('Lower thirds')).not.toHaveClass(/\bis-blank\b/);
  // On Main the button still shows the outputs' state.
  await remote.stackTab('Main').click();
  await expect(remote.blank).toHaveClass(/\bis-blank\b/);
  await remote.blank.click();
  await observer.waitFor((s) => s.outputs.list.every((o) => !o.isBlank), 'no output blank');

  // Shift+B and Shift+F: this device's stack (Lower thirds), on every output.
  // The keys act on the tab the server says is this device's: wait for it.
  await remote.stackTab('Lower thirds').click();
  await expect(remote.stackTab('Lower thirds')).toHaveClass(/\bis-active\b/);
  await remote.page.keyboard.press('Shift+B');
  await remote.page.keyboard.press('Shift+F');
  await expect(remote.stackTab('Lower thirds')).toHaveClass(/\bis-blank\b/);
  await expect(remote.stackTab('Lower thirds')).toHaveClass(/\bis-frozen\b/);
  await expect(remote.stackTab('Main')).not.toHaveClass(/\bis-blank\b/);
  await expect(remote.blank).not.toHaveClass(/\bis-blank\b/);
  const second = app.fixture.secondStack.id;
  await observer.waitFor((s) => s.stacks.occupied.find((st) => st.id === second)?.blank
    && !s.stacks.occupied.find((st) => st.id === 'main').blank, 'only Lower thirds blank');

  // Shift+Esc lifts them all.
  await remote.page.keyboard.press('Shift+Escape');
  await observer.waitFor((s) => s.stacks.occupied.every((st) => !st.blank && !st.frozen),
    'no stack blank or frozen');
});

test('Clear Stack clears the device\'s stack; Clear all clears every one', async ({ remote, observer }) => {
  await playBoth(remote);
  await remote.openControllerMenu();
  await remote.clearStack.click();
  // Only Main is left, so it is the only tab.
  await expect(remote.stackTabs).toHaveText(['Main']);
  await observer.waitFor((s) => s.stacks.occupied.length === 1
    && s.stacks.occupied[0].id === 'main', 'only Main live');

  await remote.openControllerMenu();
  await expect(remote.clearAllStacks).toBeHidden();  // only offered while the tabs show
  await remote.page.keyboard.press('Escape');
  await remote.page.locator('#u_lines').click({ position: { x: 5, y: 5 } });

  await remote.page.waitForTimeout(600);
  await remote.serviceRow('John 3:16-17').dblclick();
  await expect(remote.stackTabs).toHaveCount(2);
  await remote.openControllerMenu();
  await remote.clearAllStacks.click();
  await observer.waitFor((s) => s.stacks.occupied.length === 0, 'nothing live');
  await expect(remote.stackTabs).toHaveCount(0);
  await expect(remote.liveMark).toBeVisible();
});
