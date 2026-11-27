// What the page knows of the server: its sections as last sent (session,
// stacks, service, lines, outputs, images, advance, and the tab's Preview:
// preview, previewLines, previewImages), the line this device has asked for
// and not yet seen, and the reads of them more than one view makes. What only
// one view keeps (a selection, a search) is in its module.

export const state = {
  token: localStorage.getItem('ss_token') || '',
  // This run of the server (the hello frame): thumbnail URLs carry it.
  boot: '',
  rev: 0,
  session: {},
  stacks: { defs: [], occupied: [], focusedStackId: '', showStackTabs: false },
  service: { items: [] },
  lines: { rows: [] },
  outputs: { list: [] },
  images: { active: false, images: [], index: -1 },
  advance: { byStack: {} },
  preview: { available: false, occupied: false },
  previewLines: { rows: [] },
  previewImages: { active: false, images: [], index: -1 },
  // The line asked for on each side and not yet confirmed: its well shows
  // it at once.
  pendingCursor: null,
  previewPendingCursor: null,
  // Keep each well's current line in view (the Controller menu's switch).
  autoScroll: true,
};

export function focusedStackId() {
  return (state.stacks && state.stacks.focusedStackId) || '';
}

/* This device's stack as ``stacks.occupied`` lists it (its blank, freeze,
   and whether something is live or cued on it); null while it holds
   nothing. */
export function focusedEntry() {
  const occ = (state.stacks && state.stacks.occupied) || [];
  const fid = focusedStackId();
  return occ.find((s) => s.id === fid) || null;
}

export function stackTabsShown() {
  return !!(state.stacks && state.stacks.showStackTabs);
}

// Blank and Freeze act on every output not exempt from them.
export function globalBlank() {
  return !!(state.session && state.session.globalBlank);
}

export function globalFreeze() {
  return !!(state.session && state.session.globalFreeze);
}

/* A stack's auto-advance settings, every field (the server sends each
   stack's), or null for no stack or before the server has said. */
export function advanceFor(stackId) {
  const by = (state.advance && state.advance.byStack) || {};
  return by[stackId] || null;
}

export function isAdvancing(stackId) {
  const ids = (state.session && state.session.advancingStackIds) || [];
  return !!stackId && ids.indexOf(stackId) >= 0;
}

export function cursor() {
  if (state.pendingCursor !== null) return state.pendingCursor;
  return state.session ? state.session.lineCursor : -1;
}

export function syncPendingCursor() {
  if (state.pendingCursor !== null
      && (state.session && state.session.lineCursor) === state.pendingCursor) {
    state.pendingCursor = null;
  }
  if (state.previewPendingCursor !== null
      && (state.preview && state.preview.lineCursor) === state.previewPendingCursor) {
    state.previewPendingCursor = null;
  }
}

/* The line the Preview's well shows as current: one asked for, or the
   server's. */
export function previewCursor() {
  if (state.previewPendingCursor !== null) return state.previewPendingCursor;
  return state.preview ? state.preview.lineCursor : -1;
}

/* Lines asked for that will not come: each well shows the server's again.
   True when there were any. */
export function dropPendingCursors() {
  const had = state.pendingCursor !== null || state.previewPendingCursor !== null;
  state.pendingCursor = null;
  state.previewPendingCursor = null;
  return had;
}
