// The service rundown: the service the console has open, and choosing and
// playing its items. The remote plays a service; choosing and editing one
// stay on the console.

import { $, el, glyph, picture } from './dom.js';
import { state } from './state.js';
import { send } from './connection.js';
import { selectOrActivate } from './gestures.js';

// The row chosen (an item id, as a string) and the image folders opened.
const rundown = { selected: '', expanded: new Set() };

// Each item type's glyph, as the console's rundown draws it.
const TYPE_GLYPH = {
  song: 'song',
  bible: 'bible',
  video: 'video',
  audio: 'audio',
  image: 'image',
  image_folder: 'folder',
  freeform: 'freeform',
  web: 'globe',
  live: 'cast',
  divider: 'divider',
};

/* The rundown is named for the service, which the page does not show in
   words: a screen reader says it. */
function nameRundown() {
  const s = state.session || {};
  const name = s.openServiceId > 0 && s.openServiceName;
  $('serviceItems').setAttribute('aria-label', name || 'Service');
}

export function patchServiceChrome() {
  const s = state.session || {};
  nameRundown();
  const liveIndex = s.serviceItemIndex;
  const imgIndex = (state.images && state.images.active) ? state.images.index : -1;
  for (const row of $('serviceItems').querySelectorAll('[data-idx]')) {
    const idx = Number(row.dataset.idx);
    if (row.dataset.imageIndex !== undefined) {
      row.classList.toggle('playing',
        idx === liveIndex && Number(row.dataset.imageIndex) === imgIndex);
    } else {
      row.classList.toggle('playing', idx === liveIndex);
    }
  }
}

export function renderService() {
  nameRundown();
  const list = $('serviceItems');
  const items = (state.service && state.service.items) || [];
  const liveIndex = state.session.serviceItemIndex;
  list.textContent = '';

  // No service open, or an empty one: the well stays empty, as the
  // console's does.
  if (!items.length) return;

  let selectedStill = false;
  items.forEach((item, index) => {
    const divider = item.itemType === 'divider';
    const folder = item.itemType === 'image_folder';
    const isLive = item.selected || index === liveIndex;
    const selected = rundown.selected === String(item.itemId);
    if (selected) selectedStill = true;

    if (divider) {
      const row = el('div', 'service-divider');
      row.dataset.itemId = item.itemId;
      row.dataset.idx = String(index);
      row.appendChild(el('span', 'divider-label', item.title || 'Section'));
      list.appendChild(row);
      return;
    }

    const row = el('div', 'list-item' + (isLive ? ' playing' : '')
      + (selected ? ' marquee-selected' : ''));
    row.dataset.itemId = item.itemId;
    row.dataset.idx = String(index);
    row.setAttribute('role', 'option');
    row.setAttribute('aria-selected', String(selected));

    const glyphName = TYPE_GLYPH[item.itemType] || '';
    const open = folder && rundown.expanded.has(item.itemId);
    const pictures = folder && Array.isArray(item.folderImages) ? item.folderImages : [];
    const linked = (Array.isArray(item.alsoPlayIds) && item.alsoPlayIds.length)
      || (Array.isArray(item.alsoPlayWith) && item.alsoPlayWith.length);
    if (folder) {
      const chevron = el('span', 'folder-chevron', open ? '▾' : '▸');
      chevron.dataset.toggleFolder = item.itemId;
      row.appendChild(chevron);
    }
    if (item.displayNumber) row.appendChild(el('span', 'row-num', `${item.displayNumber}.`));
    if (glyphName) row.appendChild(glyph(glyphName, 'row-ic'));
    row.appendChild(el('span', 'row-title', item.title || item.itemType));
    if (pictures.length) row.appendChild(el('span', 'row-sub', String(pictures.length)));
    if (linked) row.appendChild(glyph('chain', 'svc-mark'));
    if (item.stackId && state.stacks && state.stacks.showStackTabs) {
      const badge = stackBadge(item);
      if (badge) row.appendChild(badge);
    }
    bindServiceRow(row, item, index);
    list.appendChild(row);

    if (open) {
      pictures.forEach((fn, ii) => {
        const child = el('div', 'list-item svc-picture');
        child.dataset.idx = String(index);
        child.dataset.imageIndex = String(ii);
        if (isLive && state.images && state.images.index === ii) child.classList.add('playing');
        const thumb = picture(`/api/thumb/images/${encodeURIComponent(fn)}?w=160`);
        thumb.loading = 'lazy';
        child.append(el('span', 'row-sub', `${ii + 1}/${pictures.length}`), thumb,
          el('span', 'row-title', fn));
        child.onclick = () => send('selectServiceItemAtImage', { index, imageIndex: ii });
        list.appendChild(child);
      });
    }
  });

  if (!selectedStill) rundown.selected = '';
}

function stackBadge(item) {
  const defs = (state.stacks && state.stacks.defs) || [];
  const def = defs.find((d) => d.id === item.stackId);
  const url = def && def.iconUrl;
  if (!url) return null;
  return picture(url, 'svc-mark-img');
}

function bindServiceRow(row, item, index) {
  row.onclick = (e) => {
    if (e.target.closest('[data-toggle-folder]')) {
      const id = Number(e.target.closest('[data-toggle-folder]').dataset.toggleFolder);
      if (rundown.expanded.has(id)) rundown.expanded.delete(id);
      else rundown.expanded.add(id);
      renderService();
      return;
    }
    const key = String(item.itemId);
    selectOrActivate(e, `svc:${key}`,
      () => rundown.selected === key,
      () => {
        rundown.selected = key;
        renderService();
      },
      () => send('selectServiceItem', { index }));
  };
}
