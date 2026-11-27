// A stack's Preview: what is sent to it is cued beside what is live, worked
// there, and taken live by Transition, T, or a Cut or a Fade from
// Transition's menu.
//
// The library's third stack, "Stage", has a Preview; nothing goes to it
// unless a test sends it there (the observer arms it, as the console's Send
// to would).

import { expect, test } from '../harness/test.mjs';

const GRACE_FIRST = 'Amazing grace how sweet the sound';
const GRACE_SECOND = 'That saved a wretch like me';
const GRACE_FIFTH = 'Twas grace that taught my heart to fear';
const HOLY_FIRST = 'Holy holy holy';

/** Arm Stage, so what the page plays next goes to it. */
async function sendToStage(app, observer) {
  await observer.run('armSendTo', { stackId: app.fixture.previewStack.id });
}

/** Look at Stage from the observer, which has a tab of its own. */
async function watchStage(app, observer) {
  await observer.run('setFocusedStackId', { stackId: app.fixture.previewStack.id });
}

test('a stack with no Preview shows one well, and none of the take', async ({ remote }) => {
  await expect(remote.previewColumn).toBeHidden();
  await expect(remote.transition).toBeHidden();
});

test('a song sent to a stack with a Preview is cued, worked there, and taken where it was', async ({ app, remote, observer }) => {
  await sendToStage(app, observer);
  await watchStage(app, observer);
  await remote.playFromLibrary('Songs', 'Amazing Grace');

  // Cued: in the Preview, its stack's tab unmarked; nothing live on Stage.
  await expect(remote.previewColumn).toBeVisible();
  await expect(remote.previewCurrentWords).toHaveText(GRACE_FIRST);
  await expect(remote.previewClear).toBeEnabled();
  await expect(remote.liveMark).toBeVisible();
  await expect(remote.stackTab('Stage')).toBeVisible();
  await expect(remote.stackTab('Stage').locator('.cue-dot')).toHaveCount(0);
  await observer.waitFor((s) => s.preview.occupied && s.session.totalLines === 0,
    'the song cued on Stage, nothing live');

  // Worked in Preview: a tap moves what is cued, not what is live.
  await remote.previewLine(GRACE_SECOND).click();
  await observer.waitFor((s) => s.preview.lineCursor === 1, 'the cued song on its second line');
  expect(remote.commands('jumpToLine').at(-1).args).toEqual({ lineIndex: 1, side: 'preview' });

  // Taken: live where it was cued, and the Preview empty again.
  await remote.transition.click();
  await expect(remote.currentWords).toHaveText(GRACE_SECOND);
  await expect(remote.previewClear).toBeDisabled();
  await expect(remote.transition).toBeDisabled();
  await observer.waitFor((s) => s.session.lineCursor === 1 && !s.preview.occupied,
    'the song live on its second line');
  expect(remote.commands('transition')).toEqual([
    { name: 'transition', args: { stackId: app.fixture.previewStack.id } },
  ]);
});

test('T takes what is cued, and what was live goes back to the Preview', async ({ app, remote, observer }) => {
  await sendToStage(app, observer);
  await remote.playFromLibrary('Songs', 'Amazing Grace');
  await expect(remote.previewCurrentWords).toHaveText(GRACE_FIRST);
  await remote.page.keyboard.press('t');
  await expect(remote.currentWords).toHaveText(GRACE_FIRST);

  await remote.page.waitForTimeout(600);
  await remote.playFromLibrary('Songs', 'Holy Holy Holy');
  await expect(remote.previewCurrentWords).toHaveText(HOLY_FIRST);
  await remote.page.keyboard.press('t');
  await expect(remote.currentWords).toHaveText(HOLY_FIRST);
  await expect(remote.previewCurrentWords).toHaveText(GRACE_FIRST);
});

test('Transition\'s menu takes by a Cut or a Fade of this computer\'s length; Clear drops what is cued', async ({ app, remote, observer }) => {
  const stage = app.fixture.previewStack.id;
  await sendToStage(app, observer);
  await remote.playFromLibrary('Songs', 'Amazing Grace');
  await remote.takeMenuButton.click();
  await expect(remote.takeMenu).toBeVisible();
  await expect(remote.takeFadeShown).toHaveText('0.5 s');
  await expect(remote.takeFadeLength).toHaveValue('0.5');

  // Its length a step at a time, or typed; kept on the computer, and the
  // menu still open for the take.
  await remote.takeFadeLonger.click();
  await remote.takeFadeLonger.click();
  await expect(remote.takeFadeShown).toHaveText('0.7 s');
  await observer.waitFor((s) => s.stacks.takeFadeMs === 700, 'the Fade kept at 0.7 s');
  await remote.takeFadeShorter.click();
  await observer.waitFor((s) => s.stacks.takeFadeMs === 600, 'the Fade kept at 0.6 s');
  await remote.takeFadeLength.fill('1.5');
  await remote.takeFadeLength.press('Enter');
  await observer.waitFor((s) => s.stacks.takeFadeMs === 1500, 'the Fade kept at 1.5 s');
  await expect(remote.takeMenu).toBeVisible();

  await remote.takeFade.click();
  await expect(remote.takeMenu).toBeHidden();
  await expect(remote.currentWords).toHaveText(GRACE_FIRST);
  expect(remote.commands('transition').at(-1).args).toEqual({
    stackId: stage, type: 'fade', durationMs: 1500,
  });

  // By a Cut: what went back to the Preview comes live again.
  await remote.page.waitForTimeout(1600);
  await remote.playFromLibrary('Songs', 'Holy Holy Holy');
  await expect(remote.previewCurrentWords).toHaveText(HOLY_FIRST);
  await remote.takeMenuButton.click();
  await remote.takeCut.click();
  await expect(remote.currentWords).toHaveText(HOLY_FIRST);
  expect(remote.commands('transition').at(-1).args).toEqual({
    stackId: stage, type: 'cut', durationMs: 0,
  });

  // Cleared, what went back is gone, and what is live stays.
  await expect(remote.previewCurrentWords).toHaveText(GRACE_FIRST);
  await remote.previewClear.click();
  await expect(remote.previewClear).toBeDisabled();
  await expect(remote.currentWords).toHaveText(HOLY_FIRST);
});

test('a service row sent to a stack with a Preview is not marked until it is live', async ({ app, remote, observer }) => {
  await remote.openServiceFrom(observer, app.fixture.serviceId, 'Sunday Service');
  // The rundown's songs play on Main by default: pin one to Stage.
  const graceRow = (s) => s.service.items.find((i) => i.title === 'Amazing Grace');
  await observer.waitFor(graceRow, 'the rundown, as the observer has it');
  await observer.run('setServiceItemStack', {
    itemId: graceRow(observer.state).itemId, stackId: app.fixture.previewStack.id,
  });
  await remote.serviceRow('Amazing Grace').dblclick();
  await expect(remote.previewCurrentWords).toHaveText(GRACE_FIRST);
  // Cued: the Preview shows it; the rundown leaves it as it was.
  await observer.waitFor((s) => graceRow(s) && graceRow(s).cued, 'the row cued');
  await expect(remote.serviceRow('Amazing Grace')).not.toHaveClass(/\bplaying\b/);
  await expect(remote.serviceRow('Amazing Grace').locator('.row-tag-cued')).toHaveCount(0);

  await remote.transition.click();
  await expect(remote.serviceRow('Amazing Grace')).toHaveClass(/\bplaying\b/);
});

test('the arrows drive what is live; T waits for something cued, and not while typing', async ({ app, remote, observer }) => {
  await sendToStage(app, observer);
  await watchStage(app, observer);
  await remote.playFromLibrary('Songs', 'Amazing Grace');
  await expect(remote.previewCurrentWords).toHaveText(GRACE_FIRST);
  await remote.page.keyboard.press('t');
  await expect(remote.currentWords).toHaveText(GRACE_FIRST);

  // Nothing cued: nothing to take, by button or key.
  await expect(remote.transition).toBeDisabled();
  await expect(remote.takeMenuButton).toBeDisabled();
  await remote.page.keyboard.press('t');
  // Next is the next slide: Grace's are four lines each.
  await remote.page.keyboard.press('ArrowRight');
  await expect(remote.currentWords).toHaveText(GRACE_FIFTH);
  await observer.waitFor((s) => s.session.lineCursor === 4 && !s.preview.occupied,
    'what is live moved, nothing cued');

  // A T typed into a search box is a letter.
  await remote.page.waitForTimeout(600);
  await remote.playFromLibrary('Songs', 'Holy Holy Holy');
  await expect(remote.previewCurrentWords).toHaveText(HOLY_FIRST);
  await remote.librarySearch.click();
  await remote.page.keyboard.type('t');
  await expect(remote.librarySearch).toHaveValue('t');
  await expect(remote.previewCurrentWords).toHaveText(HOLY_FIRST);
  expect(remote.commands('transition')).toHaveLength(1);
});

test('a blank goes with its side at the take', async ({ app, remote, observer }) => {
  const stage = (s) => (s.stacks.occupied || []).find((e) => e.id === app.fixture.previewStack.id);
  await sendToStage(app, observer);
  await remote.playFromLibrary('Songs', 'Amazing Grace');
  await remote.transition.click();
  await expect(remote.currentWords).toHaveText(GRACE_FIRST);
  // Nothing cued has nothing to blank.
  await expect(remote.previewBlank).toBeDisabled();

  // What is live blanked: the take still shows what is cued, and what was
  // live comes back blanked.
  await remote.page.keyboard.press('Shift+B');
  await observer.waitFor((s) => stage(s) && stage(s).blank, 'Stage blanked');
  await remote.page.waitForTimeout(600);
  await remote.playFromLibrary('Songs', 'Holy Holy Holy');
  await expect(remote.previewCurrentWords).toHaveText(HOLY_FIRST);
  await expect(remote.previewBlank).toBeEnabled();
  await expect(remote.previewBlank).not.toHaveClass(/\bis-blank\b/);
  await expect(remote.takeHeld).toBeHidden();
  await remote.page.keyboard.press('t');
  await expect(remote.currentWords).toHaveText(HOLY_FIRST);
  await expect(remote.previewCurrentWords).toHaveText(GRACE_FIRST);
  await expect(remote.previewBlank).toHaveClass(/\bis-blank\b/);
  await observer.waitFor((s) => stage(s) && !stage(s).blank && stage(s).previewBlank,
    'what is live unblanked, what went back blanked');

  // The Preview's own Blank lifts it.
  await remote.previewBlank.click();
  await expect(remote.previewBlank).not.toHaveClass(/\bis-blank\b/);
  expect(remote.commands('toggleStackBlank').at(-1).args).toEqual({
    stackId: app.fixture.previewStack.id, side: 'preview',
  });
  await observer.waitFor((s) => stage(s) && !stage(s).previewBlank, 'nothing blanked');
});

test('a take onto a frozen stack says it will not show', async ({ app, remote, observer }) => {
  await sendToStage(app, observer);
  await remote.playFromLibrary('Songs', 'Amazing Grace');
  await remote.transition.click();
  await expect(remote.currentWords).toHaveText(GRACE_FIRST);
  await expect(remote.takeHeld).toBeHidden();
  await remote.page.keyboard.press('Shift+F');
  await expect(remote.takeHeld).toHaveText('Frozen: the take will not show');
  await remote.page.keyboard.press('Shift+F');
  await expect(remote.takeHeld).toBeHidden();
});
