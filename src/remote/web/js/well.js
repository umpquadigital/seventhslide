// One well of the Controller: what one side of this device's stack shows
// (its lines, a gallery, a picture, the app's mark), kept on its current line
// as it moves. The Program's well and the Preview's are two of these, each
// over its side's sections (session / lines / images, or preview /
// previewLines / previewImages, which the server sends alike).

import { el, emptyNote, glyph, retryPicture } from './dom.js';
import { state } from './state.js';
import { send } from './connection.js';

const FOLLOW_ANCHOR = 0.37;
// The least time between two pictures of a moving page or source.
const PICTURE_MIN_MS = 120;

/* A well over ``box``:
   - ``side``: 'program' or 'preview', which its commands name;
   - ``sections()``: its side's { session, lines, images };
   - ``cursor()``: the line it shows as current (the Program's may be one
     asked for and not yet confirmed);
   - ``ask(line)``: a line asked for, before the command goes (the
     Program's shows it at once);
   - ``advancing()``: whether its stack is advancing on its own, which it
     then follows. */
export function makeWell({ box, side, sections, cursor, ask = () => {}, advancing = () => false }) {
  const cued = side === 'preview';
  // Commands name the Preview; the Program is what they work by default.
  const sideArgs = cued ? { side: 'preview' } : {};
  // Keeping the current line in view: nav marks a move this device asked
  // for; lastCursor and lastItem are what was last followed, and deck the
  // build it was in; placed says the view has been placed since the last
  // snapshot.
  const follow = { nav: false, lastCursor: -1, lastItem: -1, deck: '', placed: false };
  // Its moving picture changes at most once every PICTURE_MIN_MS (at). One
  // asked for sooner waits (img, src) and is shown when the window ends; a
  // newer one takes its place.
  const pictureSwap = { at: -Infinity, timer: 0, img: null, src: '' };

  function session() { return sections().session || {}; }
  function lines() { return sections().lines || {}; }
  function images() { return sections().images || {}; }

  function command(name, args) {
    send(name, Object.assign({}, args, sideArgs));
  }

  function pick(line) {
    ask(line);
    render();
    command('jumpToLine', { lineIndex: line });
  }

  function setMode(mode) {
    box.classList.toggle('is-gallery', mode === 'images' || mode === 'freeform');
    box.classList.toggle('is-media', ['image-single', 'web', 'live', 'mark'].includes(mode));
    if (box.dataset.mode === mode) return false;
    box.dataset.mode = mode;
    box.replaceChildren();
    return true;
  }

  function setEmpty(message) {
    setMode('empty');
    if (box.dataset.emptyMsg === message && box.querySelector('.empty')) return;
    box.dataset.emptyMsg = message;
    box.replaceChildren(emptyNote(message));
  }

  /* The side's picture while ``kind`` (web, live) moves on it. The revision
     counts each kind's pictures apart, so the kind is in the name too. A
     Preview's is its current slide on its own deck. */
  function pictureUrl(kind) {
    const s = session();
    const out = (state.session || {}).primaryOutputId;
    if (!out || !s.hasBuild) return '';
    const rev = s.previewRevision || 0;
    if (!cued) return `/api/preview/${encodeURIComponent(out)}?w=640&r=${state.boot}-${kind}-${rev}`;
    const stack = lines().stackId || '';
    return `/api/preview/${encodeURIComponent(out)}?w=640&stack=${encodeURIComponent(stack)}`
      + `&side=preview&r=${state.boot}-cued-${kind}-${rev}`;
  }

  function ensureMediaStage(mode, alt) {
    const rebuilt = setMode(mode);
    let stage = box.querySelector('.media-preview');
    if (rebuilt || !stage) {
      stage = el('div', 'media-preview');
      const fit = el('div', 'media-fit');
      const img = el('img', 'media-preview-img');
      img.alt = alt || '';
      img.decoding = 'async';
      fit.append(img, el('div', 'media-rim'));
      stage.appendChild(fit);
      box.replaceChildren(stage);
    }
    return stage.querySelector('img');
  }

  function swapImage(img, src, moving) {
    if (!src) return;
    if (moving) {
      pictureSwap.img = null;
      pictureSwap.src = '';
    }
    if (img.dataset.shownSrc === src || img.dataset.loadingSrc === src) return;
    if (moving) {
      const now = performance.now();
      const wait = pictureSwap.at + PICTURE_MIN_MS - now;
      if (wait > 0) {
        pictureSwap.img = img;
        pictureSwap.src = src;
        if (!pictureSwap.timer) pictureSwap.timer = setTimeout(swapWaiting, wait);
        return;
      }
      pictureSwap.at = now;
    }
    const probe = new Image();
    img.dataset.loadingSrc = src;
    probe.onload = () => {
      if (img.dataset.loadingSrc !== src) return;
      img.src = src;
      img.dataset.shownSrc = src;
      delete img.dataset.loadingSrc;
    };
    probe.onerror = () => {
      if (img.dataset.loadingSrc === src) delete img.dataset.loadingSrc;
    };
    probe.src = src;
  }

  // The window is over: show the picture that waited for it, if its frame
  // is still on the page.
  function swapWaiting() {
    const { img, src } = pictureSwap;
    pictureSwap.timer = 0;
    pictureSwap.img = null;
    pictureSwap.src = '';
    if (img && img.isConnected) swapImage(img, src, true);
  }

  /* A web page or a live source: its picture, and over it why it does not
     show (loading, a page that failed, a source gone quiet); a page's
     address under it. A source whose picture stopped is rimmed in orange, as
     on the console. */
  function renderSource(kind) {
    const s = session();
    // No alt text: the note over the frame says what a missing picture means.
    const img = ensureMediaStage(kind, '');
    swapImage(img, pictureUrl(kind), true);
    const stage = img.closest('.media-preview');
    const fit = img.parentElement;
    fit.querySelector('.media-rim').classList.toggle('is-lost', kind === 'live' && !!s.liveSourceLost);

    const text = kind === 'web'
      ? (s.webProblem || (s.webLoading ? 'Loading…' : ''))
      : (s.liveSourceProblem || '');
    let note = fit.querySelector('.media-note');
    if (text && !note) {
      note = el('div', 'media-note');
      fit.appendChild(note);
    }
    if (note) {
      note.hidden = !text;
      if (note.textContent !== text) note.textContent = text;
    }

    let address = stage.querySelector('.media-address');
    if (kind === 'web' && !address) {
      address = el('div', 'media-address');
      stage.appendChild(address);
    }
    if (address && address.textContent !== (s.webUrl || '')) address.textContent = s.webUrl || '';
  }

  /* Nothing to show in words or pictures (nothing there, a video, audio):
     the app's mark, faint, as the console's Controller shows it. */
  function renderMark() {
    setMode('mark');
    if (!box.querySelector('.well-mark')) {
      const mark = el('div', 'well-mark');
      mark.appendChild(glyph('logo'));
      box.replaceChildren(mark);
    }
  }

  function slideMaps() {
    const l = lines();
    return Array.isArray(l.slideMaps) ? l.slideMaps : [];
  }

  function slidesAtLine(line) {
    return slideMaps().map((m) => (m && m[line] !== undefined ? m[line] : 0));
  }

  function currentSlides() {
    if (cursor() !== session().lineCursor && slideMaps().length) return slidesAtLine(cursor());
    return session().outputSlides || [];
  }

  /* The line next (dir > 0) or prev moves to from ``line``, or -1. The
     server sends the target of every line, so a cursor it has not reached
     yet (a quick second tap) still gets the answer it will give. */
  function changeTarget(line, dir) {
    const l = lines();
    const table = dir > 0 ? l.nextChange : l.prevChange;
    return Array.isArray(table) && line >= 0 && line < table.length ? table[line] : -1;
  }

  function lineIsVisible(row, active) {
    if (row.lineIndex === active) return false;
    const maps = slideMaps();
    const slides = currentSlides();
    if (maps.length && slides.length) {
      const n = Math.min(maps.length, slides.length);
      for (let k = 0; k < n; k += 1) {
        const mapped = maps[k] && maps[k][row.lineIndex] !== undefined
          ? maps[k][row.lineIndex] : 0;
        if (mapped === slides[k]) return true;
      }
      return false;
    }
    return !!row.isVisible;
  }

  function lineClass(row, active) {
    const nextIdx = changeTarget(active, +1);
    let cls = 'lyric-line';
    if (row.lineIndex === active) cls += ' current';
    else if (nextIdx >= 0 && row.lineIndex === nextIdx) cls += ' next-line';
    if (lineIsVisible(row, active)) cls += ' visible';
    return cls;
  }

  function fillLineEl(div, row, active) {
    div.dataset.lineIndex = String(row.lineIndex);
    div.className = lineClass(row, active);
    const label = div.querySelector('.line-label');
    const content = div.querySelector('.line-content');
    const labelText = row.label || '';
    if (label.textContent !== labelText) label.textContent = labelText;
    const body = row.text || '(empty)';
    if (content.textContent !== body) content.textContent = body;
    content.classList.toggle('is-empty', !row.text);
    div.onclick = () => pick(row.lineIndex);
  }

  function makeLineEl(row, active) {
    const div = el('div');
    div.append(el('div', 'line-label'), el('div', 'line-content'));
    fillLineEl(div, row, active);
    return div;
  }

  function renderLineMap() {
    const rows = lines().rows || [];
    const active = cursor();
    if (!rows.length) {
      renderMark();
      return;
    }
    const rebuilt = setMode('lines');
    const kids = [...box.children];
    const canPatch = !rebuilt && kids.length === rows.length
      && kids.every((k) => k.classList.contains('lyric-line'));
    if (!canPatch) {
      const frag = document.createDocumentFragment();
      for (const row of rows) frag.appendChild(makeLineEl(row, active));
      box.replaceChildren(frag);
      return;
    }
    rows.forEach((row, i) => fillLineEl(kids[i], row, active));
  }

  function patchThumb(btn, cell) {
    btn.className = 'img-thumb-ctrl' + (cell.active ? ' active' : '');
    const img = btn.querySelector('img');
    if (img && img.getAttribute('src') !== cell.src) {
      img.src = cell.src;
      img.alt = cell.label;
    }
    const num = btn.querySelector('.img-thumb-num');
    if (num && num.textContent !== cell.label) num.textContent = cell.label;
    btn.onclick = cell.onClick;
  }

  function makeThumb(cell) {
    const btn = el('button');
    btn.type = 'button';
    btn.dataset.key = cell.key;
    const img = el('img');
    img.alt = cell.label;
    img.decoding = 'async';
    img.loading = 'lazy';
    retryPicture(img);
    btn.append(img, el('span', 'img-thumb-num'));
    patchThumb(btn, cell);
    return btn;
  }

  function reconcileThumbs(grid, cells) {
    const existing = [...grid.children];
    const byKey = new Map(existing.map((t) => [t.dataset.key, t]));
    const used = new Set();
    const next = [];
    for (const cell of cells) {
      let thumb = byKey.get(cell.key);
      if (!thumb) thumb = makeThumb(cell);
      else patchThumb(thumb, cell);
      used.add(cell.key);
      next.push(thumb);
    }
    for (const thumb of existing) {
      if (!used.has(thumb.dataset.key)) thumb.remove();
    }
    next.forEach((thumb, i) => {
      if (grid.children[i] !== thumb) grid.insertBefore(thumb, grid.children[i] || null);
    });
  }

  // The thumbnail grid, made the first time a gallery shows here.
  function ensureThumbGrid() {
    let grid = box.querySelector('.img-gallery-grid');
    if (!grid) {
      grid = el('div', 'img-gallery-grid');
      box.replaceChildren(grid);
    }
    return grid;
  }

  function renderImageGallery() {
    const pl = images();
    const files = pl.images || [];
    if (!files.length) {
      setEmpty('No images');
      return;
    }
    if (files.length === 1) {
      const src = `/api/thumb/images/${encodeURIComponent(files[0])}?w=960`;
      const img = ensureMediaStage('image-single', files[0]);
      if (img.getAttribute('src') !== src) {
        img.src = src;
        img.dataset.shownSrc = src;
      }
      return;
    }
    setMode('images');
    reconcileThumbs(ensureThumbGrid(), files.map((filename, i) => ({
      key: filename,
      src: `/api/thumb/images/${encodeURIComponent(filename)}?w=320`,
      label: String(i + 1),
      active: i === pl.index,
      onClick: () => command('imageGoto', { index: i }),
    })));
  }

  function renderFreeformGallery() {
    const rows = lines().rows || [];
    const active = cursor();
    const s = session();
    const out = (state.session || {}).primaryOutputId;
    if (!rows.length || !out) {
      renderLineMap();
      return;
    }
    setMode('freeform');
    // The deck, its side and its build name the slides, so a thumbnail
    // changes exactly when they do (the same slide index of another item is
    // another picture).
    const stack = lines().stackId || '';
    const rev = `${state.boot}-${lines().build || 0}`;
    const onSide = cued ? '&side=preview' : '';
    reconcileThumbs(ensureThumbGrid(), rows.map((row, i) => ({
      key: `${out}:${stack}:${side}:${rev}:${row.lineIndex}`,
      src: `/api/preview/${encodeURIComponent(out)}?w=320&slide=${row.lineIndex}`
        + `&stack=${encodeURIComponent(stack)}${onSide}&r=${rev}`,
      // The current slide says where it is in its builds: "3 · step 2 of 3".
      label: row.lineIndex === active && (s.totalSteps || 1) > 1
        ? `${i + 1} · step ${(s.stepCursor || 0) + 1} of ${s.totalSteps}`
        : String(i + 1),
      active: row.lineIndex === active,
      onClick: () => pick(row.lineIndex),
    })));
  }

  /* Scroll so ``target`` sits at the follow anchor (a third of the way
     down), measured where it is: a long line wraps, so rows differ in
     height. */
  function scrollToFollow(target, smooth) {
    const top = target.getBoundingClientRect().top - box.getBoundingClientRect().top
      + box.scrollTop;
    const ideal = top - (box.clientHeight - target.offsetHeight) * FOLLOW_ANCHOR;
    const maxY = Math.max(0, box.scrollHeight - box.clientHeight);
    const y = Math.max(0, Math.min(ideal, maxY));
    if (Math.abs(box.scrollTop - y) < 1) return;
    if (smooth) box.scrollTo({ top: y, behavior: 'smooth' });
    else box.scrollTop = y;
  }

  function followCurrent(animated) {
    if (!state.autoScroll) return;
    const s = session();
    follow.lastCursor = s.lineCursor;
    follow.lastItem = s.serviceItemIndex;
    const mode = box.dataset.mode;
    const reduce = window.matchMedia
      && window.matchMedia('(prefers-reduced-motion: reduce)').matches;
    let target = null;
    if (mode === 'lines') {
      const idx = cursor();
      if (idx >= 0) target = box.querySelector(`.lyric-line[data-line-index="${idx}"]`);
    } else if (mode === 'images' || mode === 'freeform') {
      target = box.querySelector('.img-thumb-ctrl.active');
    }
    if (target) scrollToFollow(target, animated && !reduce);
  }

  function followAfterRender() {
    const s = session();
    const cur = s.lineCursor;
    const item = s.serviceItemIndex;
    // Other rows in the box (a take, something sent, a new build of what is
    // there): placed on the current line at once, as on a snapshot.
    const deck = `${lines().stackId || ''}:${lines().build || 0}`;
    const newDeck = deck !== follow.deck;
    follow.deck = deck;
    if (!follow.placed || newDeck) {
      follow.placed = true;
      followCurrent(false);
      return;
    }
    const moved = cur !== follow.lastCursor || item !== follow.lastItem;
    if (follow.nav) {
      follow.nav = false;
      followCurrent(true);
      return;
    }
    if (moved && advancing()) {
      followCurrent(true);
      return;
    }
    follow.lastCursor = cur;
    follow.lastItem = item;
  }

  /* Draw what the side shows. Video and audio show no picture here: the
     transport is what drives them. */
  function render() {
    const s = session();
    if (images().active) {
      renderImageGallery();
    } else if (s.webMode) {
      renderSource('web');
    } else if (s.liveSourceMode) {
      renderSource('live');
    } else if (s.videoMode || s.mediaTransport) {
      renderMark();
    } else if (s.freeformGallery) {
      renderFreeformGallery();
    } else {
      renderLineMap();
    }
    followAfterRender();
  }

  return {
    render,
    /* What it shows now: 'lines', 'images', 'freeform', 'web', ... */
    mode: () => box.dataset.mode,
    /* Where next (dir > 0) or previous goes from its current line, or -1. */
    target: (dir) => changeTarget(cursor(), dir),
    /* A move this device asks for: followed when it lands. */
    navigated() { follow.nav = true; },
    /* A snapshot redraws everything: place the view again, without
       animating. */
    resetFollow() { follow.placed = false; },
    followCurrent,
    command,
  };
}
