// Auto-advance: the chip, its settings, and slides moving on their own.

import { expect, test } from '../harness/test.mjs';

/** Amazing Grace live (two slides: lines 0 and 4) with a one-second interval. */
async function songWithOneSecond(remote, observer) {
  await remote.playFromLibrary('Songs', 'Amazing Grace');
  await expect(remote.currentWords).toHaveText('Amazing grace how sweet the sound');
  await remote.openControllerMenu();
  await remote.advanceInterval.fill('1');
  await remote.advanceInterval.press('Tab');
  await observer.waitFor((s) => s.advance.byStack.main?.intervalSec === 1, 'a one-second interval');
}

test('it moves through the slides on its own and stops at the end', async ({ remote, observer }) => {
  await songWithOneSecond(remote, observer);
  await remote.page.locator('#u_lines').click({ position: { x: 5, y: 5 } });  // close the menu

  await remote.autoAdvance.click();
  await expect(remote.autoAdvance).toHaveClass(/\bis-on\b/);
  await observer.waitFor((s) => s.session.advancingStackIds.includes('main'), 'Main advancing');
  await observer.waitFor((s) => s.session.lineCursor === 4, 'the second slide, unasked', 4_000);
  await expect(remote.currentWords).toHaveText('Twas grace that taught my heart to fear');
  // Nothing after the last slide: it stops there.
  await observer.waitFor((s) => s.session.advancingStackIds.length === 0, 'advancing stopped', 4_000);
  expect(observer.session.lineCursor).toBe(4);
});

test('Loop goes round to the first slide again', async ({ remote, observer }) => {
  await songWithOneSecond(remote, observer);
  await remote.page.locator('label[for="autoAdvanceLoop"] ~ .toggle-switch').click();
  await expect(remote.advanceLoop).toBeChecked();
  await observer.waitFor((s) => s.advance.byStack.main?.loop === true, 'Loop on');
  await remote.page.locator('#u_lines').click({ position: { x: 5, y: 5 } });

  await remote.autoAdvance.click();
  await observer.waitFor((s) => s.session.lineCursor === 4, 'the second slide', 4_000);
  await observer.waitFor((s) => s.session.lineCursor === 0, 'round to the first', 4_000);
  expect(observer.session.advancingStackIds).toContain('main');
});

test('turning it off stops it', async ({ remote, observer }) => {
  await songWithOneSecond(remote, observer);
  await remote.page.locator('#u_lines').click({ position: { x: 5, y: 5 } });
  await remote.autoAdvance.click();
  await observer.waitFor((s) => s.session.advancingStackIds.includes('main'), 'Main advancing');
  await remote.autoAdvance.click();
  await expect(remote.autoAdvance).not.toHaveClass(/\bis-on\b/);
  await observer.waitFor((s) => s.session.advancingStackIds.length === 0, 'advancing stopped');

  await remote.page.waitForTimeout(1_500);
  expect(observer.session.lineCursor).toBe(0);
});

test('the interval is the stack\'s, and one that is no number of seconds is not sent',
  async ({ remote, observer }) => {
    await remote.playFromLibrary('Songs', 'Amazing Grace');
    await remote.openControllerMenu();
    // The server's default, as the console shows it.
    await expect(remote.advanceInterval).toHaveValue(String(observer.advance.main.intervalSec));
    await remote.advanceInterval.fill('1');
    await remote.advanceInterval.press('Tab');
    await observer.waitFor((s) => s.advance.byStack.main?.intervalSec === 1, 'a one-second interval');

    const before = remote.sent.length;
    await remote.advanceInterval.fill('');
    await remote.advanceInterval.press('Tab');
    await expect(remote.advanceInterval).toHaveValue('1');
    expect(remote.sent.slice(before).map((c) => c.name)).not.toContain('setAutoAdvance');
    expect(observer.advance.main.intervalSec).toBe(1);
  });
