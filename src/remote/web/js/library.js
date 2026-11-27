// The library: its tabs, the list of each kind with its folders and
// search, and choosing and playing a row. The Bibles tab's list is
// bible.js. Nothing in the library is changed from here, and nothing is
// added to the service: the remote plays.

import { $, el, emptyNote, glyph, retryPicture, toast } from './dom.js';
import { state } from './state.js';
import { api, send } from './connection.js';
import { selectOrActivate } from './gestures.js';
import {
  closeBibleMenu, closeBibleSuggest, ensureBibles, liveBibleSearch, renderBibleVerses,
  syncBibleToolbar,
} from './bible.js';

/* Each tab, as the console's LibraryTab: what it is called, what its
   search and its empty list say, whether it has folders, and how one of its
   rows is drawn and played (``row`` answers ``{ el, key, play }``). Bibles
   is bible.js's. The first STRIP_TABS are on the strip; the rest are behind
   its arrow. */
const TABS = [
  { kind: 'songs', label: 'Songs', empty: 'No songs', row: songRow },
  {
    kind: 'freeform', label: 'Freeform', empty: 'No Freeform items',
    emptyFolder: 'No Freeform items in this folder', folders: true, row: freeformRow,
  },
  { kind: 'bibles', label: 'Bibles' },
  {
    kind: 'videos', label: 'Videos', empty: 'No videos', emptyFolder: 'No media in this folder',
    folders: true, row: (r) => mediaRow(r, 'videos', 'playVideo'),
  },
  {
    kind: 'images', label: 'Images', empty: 'No images', emptyFolder: 'No media in this folder',
    folders: true, row: (r) => mediaRow(r, 'images', 'playImage'),
  },
  {
    kind: 'audio', label: 'Audio', empty: 'No audio', emptyFolder: 'No media in this folder',
    folders: true, row: audioRow,
  },
  {
    kind: 'web', label: 'Browser', empty: 'No web pages',
    emptyFolder: 'No web pages in this folder', folders: true, row: webRow,
  },
  { kind: 'live', label: 'Live', empty: 'No live sources found', row: liveRow, live: true },
];

// How many tabs the strip shows; the rest are behind its arrow. One fewer
// than the console's default six (LibraryPanel.stripTabCount), with Audio
// behind the arrow too, so the rail fits a phone.
const STRIP_TABS = 5;

// The tab shown, each tab's search, the rows read so far of how many, the
// folder shown (-1 the top) and the way back up, and the row chosen (its
// key: see the row functions).
const lib = {
  kind: 'songs',
  queries: {},
  rows: [],
  total: 0,
  folder: -1,
  crumbs: [],
  sel: '',
};

// Rows are read a page at a time as the list scrolls to its end.
const PAGE_ROWS = 100;
// How far ahead of the end the next page is asked for.
const READ_AHEAD = '0px 0px 600px 0px';
// Each load of a list (a tab, a folder, a search) counts one up; an answer to
// an older one is dropped, so a slow answer cannot replace a newer list.
const reading = { gen: 0, busy: false, failed: false, watcher: null };
// The Live tab's list is read again this often while it shows: sources come
// and go, and the server looks for them only while someone reads it.
const LIVE_REFRESH_MS = 2000;
let liveTimer = null;
// The strip shows the tabs behind the arrow.
let stripOnMore = false;

function tab() {
  return TABS.find((t) => t.kind === lib.kind) || TABS[0];
}

/* The tab the device showed last (sessionStorage is per browser tab), or
   Songs. */
function savedKind() {
  try {
    const kind = sessionStorage.getItem('ss_lib_tab');
    return TABS.some((t) => t.kind === kind) ? kind : 'songs';
  } catch { return 'songs'; }
}
function saveKind(kind) {
  try { sessionStorage.setItem('ss_lib_tab', kind); } catch { /* storage off */ }
}

function showTab(kind) {
  if (kind === lib.kind) return;
  lib.kind = kind;
  lib.folder = -1;
  lib.crumbs = [];
  saveKind(kind);
  closeBibleMenu();
  closeBibleSuggest();
  renderLibTabs();
  loadLibrary(true);
}

// --- The tab strip (PanelTabStrip) -------------------------------------------

function tabButton(t) {
  const btn = el('button', 'tab-btn' + (t.kind === lib.kind ? ' active' : ''), t.label);
  btn.type = 'button';
  btn.setAttribute('aria-pressed', String(t.kind === lib.kind));
  btn.onclick = () => showTab(t.kind);
  return btn;
}

function arrowButton(name, label, onMore) {
  const btn = el('button', 'tab-arrow');
  btn.type = 'button';
  btn.setAttribute('aria-label', label);
  btn.replaceChildren(glyph(name));
  btn.onclick = () => {
    stripOnMore = onMore;
    syncStripPage();
  };
  return btn;
}

/* Show the strip's page, and keep the one out of view from the keyboard. */
function syncStripPage() {
  const nav = $('libTabs');
  const pages = nav.querySelectorAll('.tab-page');
  const onMore = pages.length > 1 && stripOnMore;
  nav.classList.toggle('on-more', onMore);
  pages.forEach((page, i) => { page.inert = (i === 1) !== onMore; });
}

/* The tab strip: the first tabs, then an arrow to the rest and back. The
   page with the tab shown is the one in view. */
export function renderLibTabs() {
  const nav = $('libTabs');
  const first = TABS.slice(0, STRIP_TABS);
  const more = TABS.slice(STRIP_TABS);
  if (more.some((t) => t.kind === lib.kind)) stripOnMore = true;
  else if (first.some((t) => t.kind === lib.kind)) stripOnMore = false;

  const pages = el('div', 'tab-pages');
  const page = el('div', 'tab-page');
  page.append(...first.map(tabButton));
  pages.appendChild(page);
  if (more.length) {
    page.appendChild(arrowButton('chevronRight', 'More tabs', true));
    const back = el('div', 'tab-page is-more');
    back.append(arrowButton('chevronLeft', 'Back to the first tabs', false), ...more.map(tabButton));
    pages.appendChild(back);
  }
  // On its page before it is shown, so drawing it again does not slide it.
  nav.classList.toggle('on-more', more.length > 0 && stripOnMore);
  nav.replaceChildren(pages);
  syncStripPage();
}

// --- The toolbar ---------------------------------------------------------------

export function syncLibToolbar() {
  const bible = lib.kind === 'bibles';
  $('libSearchField').hidden = bible;
  $('bibleSelectWrap').hidden = !bible;
  $('bibleSearchField').hidden = !bible;
  if (bible) {
    syncBibleToolbar();
  } else {
    const q = lib.queries[lib.kind] || '';
    const input = $('libSearch');
    if (input.value !== q) input.value = q;
    input.placeholder = `Search ${tab().label}…`;
    $('libSearchClear').hidden = !q;
  }
}

// --- Reading -------------------------------------------------------------------

function listParams(offset, limit) {
  const params = new URLSearchParams({
    q: lib.queries[lib.kind] || '',
    offset: String(offset),
    limit: String(limit),
  });
  if (lib.folder >= 0) params.set('folderId', String(lib.folder));
  return params;
}

/* Read the list again from its start (``reset``), or its next page. */
async function loadLibrary(reset) {
  const gen = reset ? ++reading.gen : reading.gen;
  if (reset) {
    lib.rows = [];
    lib.total = 0;
    lib.sel = '';
    reading.busy = false;
    reading.failed = false;
  }
  syncLibToolbar();
  syncLiveTimer();

  if (lib.kind === 'bibles') {
    try {
      await ensureBibles(reset);
      if (gen !== reading.gen) return;
      syncLibToolbar();
      if (reset) await liveBibleSearch();
      else renderLibrary();
    } catch (err) {
      if (gen === reading.gen) failedToRead(err);
    }
    return;
  }

  if (!reset && (reading.busy || lib.rows.length >= lib.total)) return;
  reading.busy = true;
  reading.failed = false;
  if (!reset) syncMore();
  const params = listParams(lib.rows.length, PAGE_ROWS);

  try {
    const body = await api(`/api/library/${lib.kind}?${params.toString()}`);
    if (gen !== reading.gen) return;
    const rows = body.rows || [];
    lib.total = body.total || 0;
    reading.busy = false;
    if (reset) {
      lib.rows = rows;
      renderLibrary({ top: true });
    } else {
      lib.rows = lib.rows.concat(rows);
      appendRows(rows);
    }
  } catch (err) {
    if (gen !== reading.gen) return;
    reading.busy = false;
    if (reset) {
      failedToRead(err);
    } else if (!(err && err.message === 'unauthorized')) {
      reading.failed = true;
      syncMore();
    }
  }
}

function failedToRead(err) {
  if (err && err.message === 'unauthorized') return;
  toast('Could not load library', 'error');
}

/* The Live tab: its list read again while it shows and the page is seen, in
   place (where it was scrolled, with the row chosen still chosen). */
function syncLiveTimer() {
  const want = tab().live && document.visibilityState === 'visible';
  if (want && !liveTimer) {
    liveTimer = setInterval(refreshLive, LIVE_REFRESH_MS);
  } else if (!want && liveTimer) {
    clearInterval(liveTimer);
    liveTimer = null;
  }
}

async function refreshLive() {
  // Not while a page is coming, or while the device is asked for the PIN.
  if (!tab().live || reading.busy || $('app').hidden) return;
  const gen = reading.gen;
  try {
    const body = await api(`/api/library/${lib.kind}?${listParams(0, 200).toString()}`);
    if (gen !== reading.gen || !tab().live) return;
    const rows = body.rows || [];
    if (JSON.stringify(rows) === JSON.stringify(lib.rows)) return;
    lib.rows = rows;
    lib.total = body.total || 0;
    if (!rows.some((row) => `live:${row.key}` === lib.sel)) lib.sel = '';
    renderLibrary();
  } catch { /* the next one tries again */ }
}

// --- Drawing -------------------------------------------------------------------

function emptyText() {
  if ((lib.queries[lib.kind] || '').trim()) return 'Nothing matches your search.';
  const t = tab();
  return lib.folder >= 0 && t.emptyFolder ? t.emptyFolder : t.empty || 'Nothing here.';
}

/* Draw the list. ``top`` puts it at its start (a new tab, folder or
   search); otherwise it stays where it was scrolled. */
export function renderLibrary({ top = false } = {}) {
  const body = $('libBody');
  const prev = $('libList');
  const scroll = prev && !top ? prev.scrollTop : 0;
  if (reading.watcher) reading.watcher.disconnect();
  reading.watcher = null;
  syncLibToolbar();

  const list = el('div', 'scroll-list');
  list.id = 'libList';
  list.setAttribute('role', 'listbox');
  list.tabIndex = 0;
  list.setAttribute('aria-label', tab().label);
  // Drawn again, the list keeps the keyboard if it had it.
  const focused = prev && prev.contains(document.activeElement);

  if (lib.kind === 'bibles') {
    renderBibleVerses(list);
    body.replaceChildren(list);
    list.scrollTop = scroll;
    if (focused) list.focus();
    return;
  }

  if (lib.folder >= 0 && !(lib.queries[lib.kind] || '').trim()) list.appendChild(parentRow());
  if (!lib.rows.length) list.appendChild(emptyNote(emptyText()));
  else list.append(...rowsWithHeads(lib.rows, null));

  // The end of what has been read: in view (or nearly), the next page.
  const end = el('div', 'lib-more');
  end.id = 'libMore';
  list.appendChild(end);
  body.replaceChildren(list);
  list.scrollTop = scroll;
  if (focused) list.focus();
  reading.watcher = new IntersectionObserver((entries) => {
    if (entries.some((e) => e.isIntersecting) && !reading.failed) loadLibrary(false);
  }, { root: list, rootMargin: READ_AHEAD });
  syncMore();
}

/* ``rows`` drawn, a heading before each kind where the tab groups them (the
   Live tab: NDI, capture devices, streams). ``last`` is the row before. */
function rowsWithHeads(rows, last) {
  const out = [];
  let group = last && last.kindLabel;
  for (const row of rows) {
    if (tab().live && row.kindLabel && row.kindLabel !== group) {
      group = row.kindLabel;
      out.push(el('div', 'lib-group-head', group));
    }
    out.push(entryRow(row));
  }
  return out;
}

/* A page read: its rows go on the end of the list. */
function appendRows(rows) {
  const end = $('libMore');
  if (!end) {
    renderLibrary();
    return;
  }
  const last = lib.rows[lib.rows.length - rows.length - 1] || null;
  end.before(...rowsWithHeads(rows, last));
  syncMore();
}

/* The list's end: watched while there is more to read, saying so while a
   page is coming, and offering to try again when one did not come. */
function syncMore() {
  const end = $('libMore');
  if (!end || !reading.watcher) return;
  const more = lib.rows.length < lib.total;
  end.hidden = !more;
  end.textContent = '';
  if (reading.failed) {
    end.append('Couldn’t load more. ');
    const retry = el('button', 'lib-more-retry', 'Retry');
    retry.type = 'button';
    retry.onclick = () => {
      reading.failed = false;
      loadLibrary(false);
    };
    end.appendChild(retry);
  } else if (reading.busy) {
    end.textContent = 'Loading…';
  }
  // Watched again, so the watcher says at once whether the end is still in
  // view: a page that did not fill the list asks for the next.
  reading.watcher.unobserve(end);
  if (more && !reading.failed) reading.watcher.observe(end);
}

// --- Rows ------------------------------------------------------------------------

/* A row: chosen with a tap, played (or opened) with a tap on it chosen or a
   double tap, as every list here. */
function makeRow({ cls, thumb, title, sub, key, activate }) {
  const row = el('div', `list-item ${cls}` + (key === lib.sel ? ' marquee-selected' : ''));
  row.dataset.key = key;
  row.setAttribute('role', 'option');
  row.setAttribute('aria-selected', String(key === lib.sel));
  if (thumb) row.appendChild(thumb);
  const name = el('span', 'row-title', title);
  if (sub !== undefined) {
    const text = el('span', 'lib-two-line');
    text.appendChild(name);
    if (sub) text.appendChild(el('span', 'row-sub', sub));
    row.appendChild(text);
  } else {
    row.appendChild(name);
  }
  row.onclick = (e) => selectOrActivate(e, `lib:${key}`,
    () => lib.sel === key,
    () => {
      lib.sel = key;
      paintSelection();
    },
    activate);
  return row;
}

/* The box at a row's start: ``src``'s picture, or glyph ``name`` while there is
   none (or it does not load). */
function thumbBox(name, src, cls) {
  const box = el('span', 'lib-thumb-box');
  box.appendChild(glyph(name, cls));
  if (src) {
    // Over the glyph, unseen until it has loaded; gone if it cannot, asked
    // for twice.
    const img = el('img', 'is-loading');
    img.alt = '';
    img.loading = 'lazy';
    img.onload = () => {
      img.classList.remove('is-loading');
      box.querySelector('.ic')?.remove();
    };
    retryPicture(img, () => img.remove());
    img.src = src;
    box.appendChild(img);
  }
  return box;
}

function entryRow(row) {
  if (row.folderId !== undefined && row.id === undefined) return folderRow(row);
  return tab().row(row).el;
}

function folderRow(row) {
  return makeRow({
    cls: 'lib-media',
    thumb: thumbBox('folder', '', 'ic-folder-fill'),
    title: row.name,
    key: `folder:${row.folderId}`,
    activate: () => enterFolder(row.folderId, row.name),
  });
}

function parentRow() {
  return makeRow({
    cls: 'lib-media',
    thumb: thumbBox('parent', '', 'ic-parent'),
    title: '..',
    key: 'parent',
    activate: goUp,
  });
}

function playable(key, play, draw) {
  return { key, play, el: draw({ key, activate: play }) };
}

function songRow(row) {
  const key = `song:${row.id}`;
  return playable(key, () => send('playSong', { songId: row.id }), (r) => {
    const song = makeRow({ ...r, cls: 'lib-song', title: row.title });
    // Found by a lyric line, the song says which, as the console's list does.
    if (row.matchedLine) song.appendChild(el('span', 'row-sub is-lyric', row.matchedLine));
    else if (row.authors) song.appendChild(el('span', 'row-sub', row.authors));
    return song;
  });
}

function freeformRow(row) {
  return playable(`freeform:${row.id}`, () => send('playFreeform', { freeformId: row.id }),
    (r) => makeRow({
      ...r,
      cls: 'lib-media',
      thumb: thumbBox('freeform', `/api/freeform-thumb/${row.id}?w=160`),
      title: row.name,
    }));
}

// Each media kind's glyph while its row has no picture.
const MEDIA_GLYPHS = { videos: 'video', images: 'image' };

function mediaRow(row, kind, command) {
  return playable(`${kind}:${row.filename}`, () => send(command, { filename: row.filename }),
    (r) => makeRow({
      ...r,
      cls: 'lib-media',
      thumb: thumbBox(MEDIA_GLYPHS[kind], `/api/thumb/${kind}/${encodeURIComponent(row.filename)}?w=160`),
      title: row.title || row.filename,
    }));
}

function audioRow(row) {
  return playable(`audio:${row.filename}`, () => send('playAudio', { filename: row.filename }),
    (r) => makeRow({
      ...r,
      cls: 'lib-media',
      thumb: thumbBox('audio', ''),
      title: row.title || row.filename,
    }));
}

function webRow(row) {
  return playable(`web:${row.id}`, () => send('playWebPage', { pageId: row.id }),
    (r) => makeRow({
      ...r,
      cls: 'lib-media',
      thumb: thumbBox('globe', `/api/web-thumb/${row.id}?w=160&r=${encodeURIComponent(row.thumb || '')}`),
      title: row.name,
    }));
}

// Each kind of live source's glyph while it has no still.
const LIVE_GLYPHS = { ndi: 'ndi', device: 'video', decklink: 'bmd', stream: 'cast' };

/* A live source: its still, its signal by its name, and its detail. A
   stream is called by its address, the rest by name. */
function liveRow(row) {
  const name = row.kind === 'stream' ? row.url : row.name;
  return playable(`live:${row.key}`, () => send('playLiveSource', { kind: row.kind, name }),
    (r) => {
      const source = makeRow({
        ...r,
        cls: 'lib-media lib-live',
        thumb: thumbBox(LIVE_GLYPHS[row.kind] || 'cast',
          `/api/live-thumb/${encodeURIComponent(row.key)}?w=160&r=${state.boot}-${row.thumbRev || 0}`),
        title: row.name,
        sub: row.detail || '',
      });
      if (row.signal) {
        const dot = el('span', 'signal-dot' + (row.signal === 'lost' ? ' is-lost' : ''));
        dot.title = row.signal === 'lost' ? 'No picture' : 'Picture coming';
        source.querySelector('.row-title').prepend(dot);
      }
      return source;
    });
}

function paintSelection() {
  const list = $('libList');
  if (!list) return;
  for (const row of list.querySelectorAll('.list-item[data-key]')) {
    row.classList.toggle('marquee-selected', row.dataset.key === lib.sel);
    row.setAttribute('aria-selected', String(row.dataset.key === lib.sel));
  }
}

function enterFolder(folderId, name) {
  lib.crumbs.push({ id: lib.folder, name });
  lib.folder = folderId;
  loadLibrary(true);
}

function goUp() {
  const prev = lib.crumbs.pop();
  lib.folder = prev ? prev.id : -1;
  loadLibrary(true);
}

export function wireLibrary() {
  lib.kind = savedKind();
  let searchTimer = null;
  $('libSearch').addEventListener('input', (e) => {
    lib.queries[lib.kind] = e.target.value;
    $('libSearchClear').hidden = !e.target.value;
    clearTimeout(searchTimer);
    searchTimer = setTimeout(() => loadLibrary(true), 220);
  });
  $('libSearchClear').onclick = () => {
    lib.queries[lib.kind] = '';
    $('libSearch').value = '';
    $('libSearchClear').hidden = true;
    $('libSearch').focus();
    loadLibrary(true);
  };
  document.addEventListener('visibilitychange', syncLiveTimer);
}

/* Read the library when there is nothing of it yet (signing in again keeps
   what was read). */
export function loadLibraryIfEmpty() {
  if (!lib.rows.length) loadLibrary(true);
}
