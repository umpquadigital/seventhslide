// A stack's Preview, beside what is live (above it on a phone): what is cued
// on this device's stack (its well), its video or audio's Play / Pause and
// clock, its Blank (it goes live blanked) and Clear, and between the two the
// take: Transition (and T), and its caret's menu, which takes by a Cut or a
// Fade instead and sets how long this computer's Fade takes. A cued web page
// is worked from the console only.

import { $, clock, setPlayButton } from './dom.js';
import { focusedEntry, focusedStackId, previewCursor, state } from './state.js';
import { send } from './connection.js';
import { makeWell } from './well.js';
import { closeMenu, menuOpen, wireMenu } from './menus.js';

/* What is cued: the Preview's well. */
const preview = makeWell({
  box: $('p_lines'),
  side: 'preview',
  sections: () => ({
    session: state.preview, lines: state.previewLines, images: state.previewImages,
  }),
  cursor: previewCursor,
  // Shown at once; the server's answer confirms it, or a refusal drops it.
  ask: (line) => { state.previewPendingCursor = line; },
});

// The Fade's length, as the console's Transition menu keeps it: from a tenth
// of a second to ten, a tenth at a time.
const FADE_MIN_MS = 100;
const FADE_MAX_MS = 10000;
const FADE_STEP_MS = 100;

// The length last asked for, until the server's answer says it: a second
// step before then goes on from it.
let fadeAsked = null;

/* How long this computer's Fade takes, in ms. */
function fadeMs() {
  const kept = Number(state.stacks && state.stacks.takeFadeMs) || 500;
  if (fadeAsked !== null && fadeAsked !== kept) return fadeAsked;
  fadeAsked = null;
  return kept;
}

/* ``ms`` in seconds, as the field and the menu show it: "0.5". */
function seconds(ms) {
  return String(Math.round(ms / 10) / 100);
}

/* Set the Fade's length, brought into its range. */
function setFade(ms) {
  const next = Math.min(FADE_MAX_MS, Math.max(FADE_MIN_MS, Math.round(ms)));
  if (next !== fadeMs()) {
    fadeAsked = next;
    send('setTakeFade', { durationMs: next });
  }
  renderFade(next);
}

/* The menu's Fade and its length, while the field is not being typed in. */
function renderFade(ms) {
  $('takeFadeShown').textContent = `${seconds(ms)} s`;
  const field = $('takeFadeSec');
  if (document.activeElement !== field) field.value = seconds(ms);
}

/* Something is cued on this device's stack, to take. */
function canTake() {
  const p = state.preview || {};
  return !!p.available && !!p.occupied;
}

/* The stack whose Preview this is: the section says, as it may come before
   the stacks section that moves this device's tab. */
function previewStackId() {
  return (state.preview && state.preview.stackId) || focusedStackId();
}

/* Take what is cued live: by the theme's transition, or by ``type`` ("cut"
   or "fade", this computer's length). */
function take(type) {
  if (!canTake()) return;
  const stackId = previewStackId();
  if (!type) send('transition', { stackId });
  else if (type === 'cut') send('transition', { stackId, type, durationMs: 0 });
  else send('transition', { stackId, type, durationMs: fadeMs() });
}

/* Show the Preview and the take while this device's stack has a Preview. */
export function renderPreview() {
  const p = state.preview || {};
  const on = !!p.available;
  $('wells').classList.toggle('has-preview', on);
  $('previewCol').hidden = !on;
  $('takeCol').hidden = !on;
  if (!on) return;

  const occupied = !!p.occupied;
  $('btnPreviewClear').disabled = !occupied;
  // Turning it off always works.
  $('btnPreviewBlank').disabled = !occupied && !p.blank;
  $('btnPreviewBlank').classList.toggle('is-blank', !!p.blank);
  // Nothing cued, nothing to take: the menu is for a take.
  $('btnTransition').disabled = !canTake();
  $('btnTakeMenu').disabled = !canTake();
  if (!canTake() && menuOpen($('takeMenu'))) closeMenu();
  renderFade(fadeMs());
  // The take will not show: the stack's outputs are frozen. (A blank goes
  // with its side.)
  const stack = focusedEntry();
  const held = stack && stack.live && stack.frozen ? 'Frozen: the take will not show' : '';
  const note = $('takeHeld');
  note.hidden = !held;
  if (note.textContent !== held) note.textContent = held;
  $('previewMedia').hidden = !p.mediaTransport;
  if (p.mediaTransport) {
    setPlayButton($('btnPreviewPlay'), !!p.videoPlaying);
    $('previewClock').textContent = `${clock(p.videoPositionSec)} / ${clock(p.videoDurationSec)}`;
  }
  preview.render();
}

/* A snapshot redraws everything: place the view again, without animating. */
export function resetPreviewFollow() {
  preview.resetFollow();
}

/* The menu's Follow switch turned on: back to the current line. */
export function followPreview() {
  preview.followCurrent(true);
}

/* T takes what is cued, as on the console, once a press: held down, it
   would take back and forth. True when it was the key. */
export function previewKeydown(e) {
  if (e.key.toLowerCase() !== 't' || e.shiftKey || e.ctrlKey || e.metaKey || e.altKey) {
    return false;
  }
  e.preventDefault();
  if (!e.repeat) take();
  return true;
}

export function wirePreview() {
  $('btnTransition').onclick = () => take();
  wireMenu($('btnTakeMenu'), $('takeMenu'), { onOpen: () => renderFade(fadeMs()) });
  $('takeCut').onclick = () => take('cut');
  $('takeFade').onclick = () => take('fade');
  $('takeFadeLess').onclick = () => setFade(fadeMs() - FADE_STEP_MS);
  $('takeFadeMore').onclick = () => setFade(fadeMs() + FADE_STEP_MS);
  $('takeFadeSec').onchange = (e) => {
    const sec = Number(e.target.value);
    if (Number.isFinite(sec) && sec > 0) setFade(sec * 1000);
    else renderFade(fadeMs());
  };
  $('btnPreviewClear').onclick = () => send('clearPreview', { stackId: previewStackId() });
  $('btnPreviewBlank').onclick = () =>
    send('toggleStackBlank', { stackId: previewStackId(), side: 'preview' });
  $('btnPreviewPlay').onclick = () =>
    preview.command((state.preview || {}).videoPlaying ? 'videoPause' : 'videoPlay');
}
