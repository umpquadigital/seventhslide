// The Bibles tab: which Bible, the reference typed and the book names
// suggested for it, and the chapter's verses to choose and play.

import { $, el, emptyNote } from './dom.js';
import { api, send } from './connection.js';
import { selectOrActivate } from './gestures.js';
import { renderLibrary, syncLibToolbar } from './library.js';
import { closeMenu, menuOpen, wireMenu } from './menus.js';

// The Bible and chapter shown, the reference typed, the Bibles there are,
// the chapter's verses and the one chosen (0 none), the book names
// suggested, whether the reference resolved, and if it did not, the
// server's reason.
const bible = {
  current: { id: -1, name: '', book: '', chapter: 0 },
  search: '',
  list: [],
  verses: [],
  selected: 0,
  suggestions: [],
  suggestIndex: 0,
  resolved: false,
  reason: '',
};

export async function ensureBibles(force) {
  if (force || !bible.list.length) {
    const body = await api('/api/library/bibles?q=&offset=0&limit=50');
    bible.list = (body.rows || []).filter((b) => b.id && !b.book && !b.verse);
  }
  if (bible.current.id < 0 && bible.list[0]) {
    bible.current = {
      id: bible.list[0].id, name: bible.list[0].name, book: '', chapter: 0,
    };
  }
}

export function renderBibleVerses(list) {
  if (!bible.list.length) {
    list.appendChild(emptyNote('No Bibles imported'));
    return;
  }
  if (!bible.verses.length && bible.reason) {
    list.appendChild(emptyNote(bible.reason));
    return;
  }
  for (const v of bible.verses) {
    const row = el('div', 'list-item verse-item'
      + (bible.selected === v.verse_num ? ' marquee-selected' : ''));
    row.dataset.verse = String(v.verse_num);
    row.setAttribute('role', 'option');
    row.setAttribute('aria-selected', String(bible.selected === v.verse_num));
    row.append(el('span', 'verse-num', String(v.verse_num)), el('span', 'row-title', v.text || ''));
    const bibleId = bible.current.id;
    const book = bible.current.book;
    const chapter = bible.current.chapter;
    const play = () => {
      if (!book || !chapter) return;
      send('playBibleRef', {
        bibleId, book, chapter,
        verseStart: v.verse_num, verseEnd: v.verse_num,
      });
    };
    row.onclick = (e) => selectOrActivate(e, `verse:${bibleId}:${book}:${chapter}:${v.verse_num}`,
      () => bible.selected === v.verse_num,
      () => {
        bible.selected = v.verse_num;
        paintBibleSelection();
      },
      play);
    list.appendChild(row);
  }
}

function paintBibleSelection() {
  for (const row of document.querySelectorAll('#libList .verse-item')) {
    const chosen = Number(row.dataset.verse) === bible.selected;
    row.classList.toggle('marquee-selected', chosen);
    row.setAttribute('aria-selected', String(chosen));
  }
}

function bibleBookToken(raw) {
  let s = String(raw || '').trim();
  s = s.replace(/[:.\-\s]+$/g, '');
  const m = s.match(/^(.*?)(?:\s+\d[\d\s:.-]*)$/);
  return (m ? m[1] : s).trim();
}

let bibleSearchGen = 0;

export async function liveBibleSearch() {
  const gen = ++bibleSearchGen;
  closeBibleSuggest();
  bible.resolved = false;
  bible.reason = '';
  bible.verses = [];
  bible.selected = 0;
  if (bible.current.id < 0) {
    if (gen === bibleSearchGen) renderLibrary();
    return;
  }
  const reference = bible.search;
  if (!reference.trim()) {
    bible.suggestions = [];
    if (gen === bibleSearchGen) renderLibrary();
    return;
  }
  try {
    const ack = await send('resolveBibleRef', {
      reference, bibleId: bible.current.id,
    });
    if (gen !== bibleSearchGen) return;
    const data = (ack && ack.data) || {};
    if (data.success) {
      bible.resolved = true;
      bible.current.book = data.book;
      bible.current.chapter = data.chapter;
      bible.verses = data.verses || [];
      bible.selected = 0;
      bible.suggestions = [];
      closeBibleSuggest();
      renderLibrary();
      return;
    }
    await refreshBibleSuggestions();
    if (gen !== bibleSearchGen) return;
    // Books to choose from answer a reference still being typed; without
    // them, say why it found nothing (or why it was not looked up).
    if (!bible.suggestions.length) bible.reason = data.message || ack.error || '';
    renderLibrary();
  } catch {
    if (gen === bibleSearchGen) renderLibrary();
  }
}

async function refreshBibleSuggestions() {
  const token = bibleBookToken(bible.search);
  if (!token || bible.current.id < 0) {
    bible.suggestions = [];
    closeBibleSuggest();
    return;
  }
  try {
    const ack = await send('suggestBibleBooks', { prefix: token, bibleId: bible.current.id });
    const books = (ack && ack.data && ack.data.books) || [];
    if (books.length === 1 && String(books[0]).toLowerCase() === token.toLowerCase()) {
      bible.suggestions = [];
      closeBibleSuggest();
      return;
    }
    bible.suggestions = books;
    bible.suggestIndex = 0;
    renderBibleSuggest();
  } catch {
    bible.suggestions = [];
    closeBibleSuggest();
  }
}

function renderBibleSuggest() {
  const box = $('bibleSuggest');
  box.textContent = '';
  if (!bible.suggestions.length) {
    closeBibleSuggest();
    return;
  }
  bible.suggestions.forEach((name, i) => {
    const btn = document.createElement('button');
    btn.type = 'button';
    btn.className = 'menu-item' + (i === bible.suggestIndex ? ' is-current' : '');
    btn.setAttribute('role', 'option');
    btn.setAttribute('aria-selected', String(i === bible.suggestIndex));
    btn.textContent = name;
    btn.onmouseenter = () => {
      bible.suggestIndex = i;
      for (const option of box.querySelectorAll('.menu-item')) option.classList.remove('is-current');
      btn.classList.add('is-current');
    };
    btn.onclick = (e) => {
      e.preventDefault();
      acceptBibleSuggestion(name);
    };
    box.appendChild(btn);
  });
  box.hidden = false;
  box.classList.add('open');
}

function acceptBibleSuggestion(name) {
  if (!name) return;
  const raw = bible.search;
  const token = bibleBookToken(raw);
  let rest = '';
  if (token) {
    const idx = raw.toLowerCase().indexOf(token.toLowerCase());
    if (idx >= 0) rest = raw.slice(idx + token.length).replace(/^[:.\-\s]+/, '').trim();
  }
  bible.search = rest ? `${name} ${rest}` : `${name} `;
  const input = $('bibleQuickRef');
  input.value = bible.search;
  input.focus();
  $('bibleSearchClear').hidden = !bible.search;
  bible.suggestions = [];
  closeBibleSuggest();
  liveBibleSearch();
}

export function closeBibleSuggest() {
  const box = $('bibleSuggest');
  if (!box) return;
  box.hidden = true;
  box.classList.remove('open');
  box.textContent = '';
}

/* The Bible menu's rows: the Bibles switched on, the one shown chosen. */
function fillBibleMenu() {
  closeBibleSuggest();
  const menu = $('bibleMenu');
  menu.textContent = '';
  for (const b of bible.list) {
    const btn = document.createElement('button');
    btn.type = 'button';
    btn.className = 'menu-item' + (b.id === bible.current.id ? ' is-chosen' : '');
    btn.setAttribute('role', 'menuitemradio');
    btn.setAttribute('aria-checked', String(b.id === bible.current.id));
    btn.textContent = b.name;
    btn.onclick = () => pickBible(b.id, b.name);
    menu.appendChild(btn);
  }
}

export function closeBibleMenu() {
  if (menuOpen($('bibleMenu'))) closeMenu();
}

function pickBible(id, name) {
  bible.current = { id, name, book: '', chapter: 0 };
  bible.verses = [];
  bible.selected = 0;
  bible.resolved = false;
  syncLibToolbar();
  liveBibleSearch();
}

async function quickBibleSearch() {
  if (bible.suggestions.length) {
    const i = Math.max(0, Math.min(bible.suggestIndex, bible.suggestions.length - 1));
    acceptBibleSuggestion(bible.suggestions[i]);
    return;
  }
  if (!bible.resolved) await liveBibleSearch();
  if (!bible.resolved || !bible.verses.length) return;
  const start = bible.verses[0].verse_num;
  const end = bible.verses[bible.verses.length - 1].verse_num;
  send('playBibleRef', {
    bibleId: bible.current.id, book: bible.current.book, chapter: bible.current.chapter,
    verseStart: start, verseEnd: end,
  });
}

export function wireBible() {
  let bibleSearchTimer = null;
  $('bibleQuickRef').addEventListener('input', (e) => {
    bible.search = e.target.value;
    $('bibleSearchClear').hidden = !bible.search;
    clearTimeout(bibleSearchTimer);
    bibleSearchTimer = setTimeout(() => liveBibleSearch(), 140);
  });
  $('bibleQuickRef').addEventListener('keydown', (e) => {
    if (e.key === 'ArrowDown' && bible.suggestions.length) {
      e.preventDefault();
      bible.suggestIndex = Math.min(
        bible.suggestions.length - 1, bible.suggestIndex + 1);
      renderBibleSuggest();
    } else if (e.key === 'ArrowUp' && bible.suggestions.length) {
      e.preventDefault();
      bible.suggestIndex = Math.max(0, bible.suggestIndex - 1);
      renderBibleSuggest();
    } else if (e.key === 'Tab' && bible.suggestions.length) {
      e.preventDefault();
      const i = Math.max(0, Math.min(bible.suggestIndex, bible.suggestions.length - 1));
      acceptBibleSuggestion(bible.suggestions[i]);
    } else if (e.key === 'Enter') {
      e.preventDefault();
      quickBibleSearch();
    } else if (e.key === 'Escape' && bible.suggestions.length) {
      e.preventDefault();
      bible.suggestions = [];
      closeBibleSuggest();
    }
  });
  $('bibleSearchClear').onclick = () => {
    bible.search = '';
    $('bibleQuickRef').value = '';
    $('bibleSearchClear').hidden = true;
    $('bibleQuickRef').focus();
    liveBibleSearch();
  };
  wireMenu($('bibleSelectBtn'), $('bibleMenu'), { onOpen: fillBibleMenu });
  document.addEventListener('click', (e) => {
    if (!$('bibleSearchField').contains(e.target)) closeBibleSuggest();
  });
}

/* The Bibles tab's part of the library toolbar: the Bible chosen and the
   reference typed. */
export function syncBibleToolbar() {
  $('bibleSelectLabel').textContent = bible.current.name || 'Select bible…';
  const ref = $('bibleQuickRef');
  if (ref.value !== bible.search) ref.value = bible.search;
  $('bibleSearchClear').hidden = !bible.search;
}
