// Telling a click from a double-click, the same way on every list.

// The row whose first click of the current gesture already played it.
let activatedByClick = null;

/* A playable row's click: it selects, a click on the selected row plays, and a
   double-click plays. Each gesture plays once: when the first click of a
   double-click already played (the row was selected), the second does not
   play again. The browser's click count (``e.detail``) names the gesture; it
   survives the row being drawn again between the clicks, which a rundown
   click does. ``key`` names the row. */
export function selectOrActivate(e, key, isSelected, select, activate) {
  if (e.detail >= 2) {
    if (e.detail === 2 && activatedByClick !== key) activate();
    activatedByClick = key;
    return;
  }
  if (isSelected()) {
    activate();
    activatedByClick = key;
  } else {
    select();
    activatedByClick = null;
  }
}

// The rows of a list that can be chosen: the rundown's items, the library's
// rows, a chapter's verses.
const CHOOSABLE = '.list-item[data-item-id], .list-item[data-key], .list-item[data-verse]';

/* A list given the focus from the keyboard (role="listbox") takes the
   arrow keys, which move the row chosen, and Enter, which plays (or opens)
   it, as a click on it would. A list a tap or click focused leaves them to
   the Controller. Answers whether it took the key. */
export function listKeydown(e) {
  const list = e.target instanceof Element ? e.target.closest('[role="listbox"]') : null;
  if (!list || !list.matches(':focus-visible')) return false;
  if (!['ArrowDown', 'ArrowUp', 'Enter'].includes(e.key)) return false;
  const rows = [...list.querySelectorAll(CHOOSABLE)];
  if (!rows.length) return false;
  const at = rows.findIndex((row) => row.classList.contains('marquee-selected'));
  e.preventDefault();
  if (e.key === 'Enter') {
    if (at >= 0) rows[at].click();
    return true;
  }
  const step = e.key === 'ArrowDown' ? 1 : -1;
  const to = at < 0 ? 0 : Math.max(0, Math.min(rows.length - 1, at + step));
  // A click on a row not chosen only chooses it.
  if (to !== at) rows[to].click();
  const chosen = list.querySelectorAll(CHOOSABLE)[to];
  if (chosen) chosen.scrollIntoView({ block: 'nearest' });
  return true;
}
