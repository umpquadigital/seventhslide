// The left column's two panels (collapsing and resizing them) and, on a
// phone, the drawer that holds them.

import { $ } from './dom.js';

/* A panel's title bar folds it away, and the other takes the room. */
function toggleLeftPanel(which) {
  $('colLeft').classList.toggle(which === 'service' ? 'collapsed-top' : 'collapsed-bottom');
}

function wireSplitter() {
  const col = $('colLeft');
  const div = $('leftPanelDivider');
  let dragging = false;
  div.addEventListener('pointerdown', (e) => {
    if (col.classList.contains('collapsed-top') || col.classList.contains('collapsed-bottom')) return;
    dragging = true;
    col.classList.add('left-resizing');
    div.setPointerCapture(e.pointerId);
  });
  div.addEventListener('pointermove', (e) => {
    if (!dragging) return;
    const rect = col.getBoundingClientRect();
    const ratio = (e.clientY - rect.top) / rect.height;
    col.style.setProperty('--split-top', String(Math.min(0.85, Math.max(0.15, ratio))));
  });
  const end = () => { dragging = false; col.classList.remove('left-resizing'); };
  div.addEventListener('pointerup', end);
  div.addEventListener('pointercancel', end);
}

function setDrawer(open) {
  document.body.classList.toggle('drawer-left-open', open);
  $('btnDrawer').setAttribute('aria-expanded', String(open));
}
function toggleDrawer() {
  setDrawer(!document.body.classList.contains('drawer-left-open'));
}
function closeDrawers() {
  setDrawer(false);
}

export function wirePanels() {
  wireSplitter();
  $('svcHeader').onclick = () => toggleLeftPanel('service');
  $('libHeader').onclick = () => toggleLeftPanel('library');
  $('btnDrawer').onclick = toggleDrawer;
  $('drawerBackdrop').onclick = closeDrawers;
}
