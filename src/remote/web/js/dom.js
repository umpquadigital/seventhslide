// What every view uses: elements by id, toasts, empty-list notes, elements,
// glyphs and pictures, and a media transport's Play / Pause and clock.

export const $ = (id) => document.getElementById(id);

export function toast(text, level) {
  const note = el('div', `app-toast ${level || ''}`.trim(), text);
  $('toasts').appendChild(note);
  setTimeout(() => note.remove(), 2600);
}

/* The note a list shows in place of rows it does not have. */
export function emptyNote(text) {
  return el('div', 'empty', text);
}

/* An element: ``tag`` with ``className``, and ``text`` when given. Rows are
   built from these, never from markup with data in it. */
export function el(tag, className, text) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text !== undefined) node.textContent = text;
  return node;
}

/* Glyph ``name`` from the app's table (the sprite the server puts in the
   page). */
export function glyph(name, cls) {
  const ns = 'http://www.w3.org/2000/svg';
  const svg = document.createElementNS(ns, 'svg');
  svg.setAttribute('class', `ic ${cls || ''}`.trim());
  svg.setAttribute('aria-hidden', 'true');
  const use = document.createElementNS(ns, 'use');
  use.setAttribute('href', `#ic-${name}`);
  svg.appendChild(use);
  return svg;
}

/* A transport's Play / Pause: its glyph and label say what a press does. */
export function setPlayButton(btn, playing) {
  const want = playing ? 'pause' : 'play';
  if (btn.dataset.shows === want) return;
  btn.dataset.shows = want;
  btn.replaceChildren(glyph(want), el('span', '', playing ? 'Pause' : 'Play'));
}

/* ``sec`` as a transport's clock reads it, as the console's does: "1:05",
   and past an hour "1:15:03". */
export function clock(sec) {
  const t = Math.max(0, Math.floor(Number(sec) || 0));
  const h = Math.floor(t / 3600);
  const m = Math.floor((t % 3600) / 60);
  const ss = String(t % 60).padStart(2, '0');
  return h > 0 ? `${h}:${String(m).padStart(2, '0')}:${ss}` : `${m}:${ss}`;
}

/* An <img> of ``src``, decorative. */
export function picture(src, className) {
  const img = el('img', className);
  img.alt = '';
  img.src = src;
  return img;
}

// How long a picture that failed waits before it is asked for once more: the
// Retry-After of a server too busy to draw it.
const RETRY_PICTURE_MS = 1000;

/* Have ``img`` ask once more, a second later, for a picture that failed to
   load. A server too busy to draw it answers 503, and an <img> cannot tell
   that from a picture that does not exist, so each picture it is given gets
   one more try; ``failed()`` runs when that fails too. */
export function retryPicture(img, failed) {
  img.onerror = () => {
    const src = img.getAttribute('src');
    if (img.dataset.retried === src) {
      if (failed) failed();
      return;
    }
    img.dataset.retried = src;
    setTimeout(() => {
      if (img.getAttribute('src') === src) img.src = src;
    }, RETRY_PICTURE_MS);
  };
}
