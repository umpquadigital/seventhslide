// The centre column: what is live on this device's stack (its well: lines, a
// gallery, a picture), following it as it moves, the media clock and
// transport under it, and the controls above it (next and previous, Blank and
// Freeze of every output, auto-advance, the settings menu). A stack with a
// Preview shows its Preview beside it (preview.js).

import { $, clock, setPlayButton } from './dom.js';
import {
  advanceFor, cursor, focusedEntry, focusedStackId, globalBlank, globalFreeze, isAdvancing,
  stackTabsShown, state,
} from './state.js';
import { send } from './connection.js';
import { renderStackTabs } from './stacks.js';
import { wireMenu } from './menus.js';
import { makeWell } from './well.js';
import { followPreview, previewKeydown, renderPreview, resetPreviewFollow } from './preview.js';

let seekHeld = false;

/* What is live: the Program's well. */
const program = makeWell({
  box: $('u_lines'),
  side: 'program',
  sections: () => ({ session: state.session, lines: state.lines, images: state.images }),
  cursor,
  // Shown at once; the server's answer confirms it, or a refusal drops it.
  ask: (line) => { state.pendingCursor = line; },
  advancing: () => isAdvancing(focusedStackId()),
});

function toggleBlank() {
  send('toggleGlobalBlank');
}

function toggleFreeze() {
  send('toggleGlobalFreeze');
}

/* Shift+B / Shift+F: this device's stack, on every output, while something
   is on it. Whether that is live is the server's to say (it refuses, and
   says why, when nothing is): the page hears of a take a moment after its
   words, and a key pressed just after one is meant for what was taken. */
function toggleStack(command) {
  const stack = focusedEntry();
  if (stack) send(command, { stackId: stack.id });
}

/* A snapshot redraws everything: place the views again, without animating. */
export function resetFollow() {
  program.resetFollow();
  resetPreviewFollow();
}

export function renderLive() {
  const s = state.session || {};
  $('mobileNavTitle').textContent = s.currentTitle || 'Controller';

  const blank = globalBlank();
  const freeze = globalFreeze();
  for (const id of BLANK_BUTTONS) $(id).classList.toggle('is-blank', blank);
  for (const id of FREEZE_BUTTONS) $(id).classList.toggle('is-freeze', freeze);

  renderStackTabs();
  renderVideoBar();

  // Video and audio share the transport; only video has a picture. A web
  // page has its own: the page before, and Reload.
  const transport = !!s.mediaTransport;
  const web = !transport && !!s.webMode;
  $('slideControls').hidden = transport || web;
  $('videoControls').hidden = !transport;
  $('webControls').hidden = !web;
  $('btnWebBack').disabled = !s.webCanGoBack;
  setPlayButton($('btnVideoPlay'), !!s.videoPlaying);

  program.render();
  renderPreview();
}

// Each transport row's Blank and Freeze.
const BLANK_BUTTONS = ['btnBlank', 'btnBlankVideo', 'btnBlankWeb'];
const FREEZE_BUTTONS = ['btnFreeze', 'btnFreezeVideo', 'btnFreezeWeb'];

function renderVideoBar() {
  const s = state.session || {};
  $('videoBar').hidden = !s.mediaTransport;
  if (!s.mediaTransport) return;
  $('videoPos').textContent = clock(s.videoPositionSec);
  $('videoDur').textContent = clock(s.videoDurationSec);
  if (!seekHeld) {
    const dur = Number(s.videoDurationSec) || 0;
    $('videoSeek').value = dur > 0
      ? String(Math.round((Number(s.videoPositionSec) / dur) * 1000)) : '0';
  }
}

export function renderAdvance() {
  const id = focusedStackId();
  const adv = advanceFor(id);
  const chip = $('btnAutoAdvance');
  const interval = $('autoAdvanceInterval');
  const loop = $('autoAdvanceLoop');
  const through = $('autoAdvanceThrough');
  const stop = $('autoAdvanceAtDividers');
  if (chip) {
    chip.classList.toggle('is-on', !!(adv && adv.enabled));
    chip.disabled = !adv;
  }
  if (!adv) return;
  if (interval && document.activeElement !== interval) {
    interval.value = String(adv.intervalSec);
  }
  if (loop) loop.checked = !!adv.loop;
  if (through) through.checked = !!adv.through;
  if (stop) stop.checked = !!adv.stopAtDividers;
}

function patchAdvance(fields) {
  const id = focusedStackId();
  const cur = advanceFor(id);
  if (!cur) return;
  send('setAutoAdvance', Object.assign({ stackId: id }, {
    enabled: cur.enabled,
    intervalSec: cur.intervalSec,
    loop: cur.loop,
    through: cur.through,
    stopAtDividers: cur.stopAtDividers,
  }, fields));
}

/* Next or previous (``dir``) through what is live. On lines, the line it
   goes to shows at once: the server sends where each line goes. */
function step(dir) {
  const s = state.session || {};
  // A build step on the slide: the line stays, so there is nothing to guess.
  const stepping = dir > 0 ? (s.stepCursor || 0) + 1 < (s.totalSteps || 1)
    : (s.stepCursor || 0) > 0;
  if (program.mode() === 'lines' && !stepping) {
    const to = program.target(dir);
    if (to < 0) return;
    state.pendingCursor = to;
    program.navigated();
    send(dir > 0 ? 'nextSlide' : 'prevSlide');
    program.render();
    return;
  }
  program.navigated();
  send(dir > 0 ? 'nextSlide' : 'prevSlide');
}

/* The controller's keys: next and previous (what is live), Blank and Freeze
   as on the console (B / F every output, Shift with them this device's
   stack, Shift+Esc lifts them all), and T, the take. */
export function controllerKeydown(e) {
  if (previewKeydown(e)) return;
  const key = e.key.toLowerCase();
  if (e.key === 'ArrowRight' || e.key === 'ArrowDown' || e.key === 'PageDown') {
    e.preventDefault();
    // A web page has no next.
    if ($('webControls').hidden) $('btnNext').click();
  } else if (e.key === 'ArrowLeft' || e.key === 'ArrowUp' || e.key === 'PageUp') {
    e.preventDefault();
    // A web page's Previous is the page before it.
    if (!$('webControls').hidden) $('btnWebBack').click();
    else $('btnPrev').click();
  } else if (key === 'b') {
    if (e.shiftKey) toggleStack('toggleStackBlank');
    else toggleBlank();
  } else if (key === 'f') {
    if (e.shiftKey) toggleStack('toggleStackFreeze');
    else toggleFreeze();
  } else if (e.key === 'Escape' && e.shiftKey) {
    send('clearBlankFreeze');
  }
}

export function wireController() {
  $('btnNext').onclick = () => step(+1);
  $('btnPrev').onclick = () => step(-1);
  $('btnBlank').onclick = toggleBlank;
  $('btnFreeze').onclick = toggleFreeze;
  $('btnBlankVideo').onclick = toggleBlank;
  $('btnFreezeVideo').onclick = toggleFreeze;
  $('btnBlankWeb').onclick = toggleBlank;
  $('btnFreezeWeb').onclick = toggleFreeze;
  $('btnWebBack').onclick = () => send('webBack');
  $('btnWebReload').onclick = () => send('webReload');
  $('btnVideoPlay').onclick = () =>
    send(state.session.videoPlaying ? 'videoPause' : 'videoPlay');
  $('btnVideoRestart').onclick = () => send('videoRestart');
  $('videoSeek').addEventListener('pointerdown', () => { seekHeld = true; });
  // A press let go of without moving the bar fires no change, which would
  // leave it held and still. Let go of it here, once the change a move does
  // fire (in the same task as the release) has had its turn.
  const releaseSeek = () => {
    if (!seekHeld) return;
    setTimeout(() => {
      if (!seekHeld) return;
      seekHeld = false;
      renderVideoBar();
    }, 0);
  };
  document.addEventListener('pointerup', releaseSeek);
  document.addEventListener('pointercancel', releaseSeek);
  $('videoSeek').addEventListener('change', () => {
    seekHeld = false;
    const dur = Number(state.session.videoDurationSec) || 0;
    send('videoSeek', { positionSec: (Number($('videoSeek').value) / 1000) * dur });
  });

  $('btnAutoAdvance').onclick = () => {
    const cur = advanceFor(focusedStackId());
    if (cur) patchAdvance({ enabled: !cur.enabled });
  };
  // What is not a number of seconds is not sent, and the field shows the
  // stack's again.
  $('autoAdvanceInterval').onchange = (e) => {
    const sec = Math.floor(Number(e.target.value));
    if (sec >= 1) patchAdvance({ intervalSec: sec });
    else e.target.value = String(advanceFor(focusedStackId())?.intervalSec ?? '');
  };
  $('autoAdvanceLoop').onchange = (e) => patchAdvance({ loop: e.target.checked });
  $('autoAdvanceThrough').onchange = (e) => patchAdvance({ through: e.target.checked });
  $('autoAdvanceAtDividers').onchange = (e) =>
    patchAdvance({ stopAtDividers: e.target.checked });
  $('autoScroll').onchange = (e) => {
    state.autoScroll = e.target.checked;
    if (state.autoScroll) {
      program.followCurrent(true);
      followPreview();
    }
  };

  // The Stacks section only means anything while more than one stack is up,
  // which is the same moment the strip appears.
  // Clear Stack clears what is live on this device's stack: nothing to do
  // while nothing is.
  wireMenu($('ctrlSettingsBtn'), $('ctrlDropdown'), {
    onOpen: () => {
      $('ctrlMenuStacks').hidden = !stackTabsShown();
      const stack = focusedEntry();
      $('menuClearStack').disabled = !(stack && stack.live);
    },
  });

  // "Clear Stack" acts on the focused stack, what is live on it. The menu
  // already hangs off its own row of controls, so a label that rewrote itself
  // on every switch would only add movement to a header built to hold still.
  $('menuClearStack').onclick = () => {
    const stack = focusedEntry();
    if (stack && stack.live) send('clearStack', { stackId: stack.id });
  };
  $('menuClearAllStacks').onclick = () => send('clearAllStacks');
}
