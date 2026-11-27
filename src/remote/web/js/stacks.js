// The stack tabs above the controller, and "+N more" when they do not fit.

import { $, el, glyph, picture } from './dom.js';
import { state } from './state.js';
import { send } from './connection.js';
import { closeMenu, menuOpen, wireMenu } from './menus.js';

export function renderStackTabs() {
  const nav = $('stackTabs');
  const occupied = (state.stacks && state.stacks.occupied) || [];
  // A tab for each stack with something live or cued, from the first, as the
  // console's strip.
  if (!occupied.length) {
    nav.hidden = true;
    nav.replaceChildren();
    layoutStackTabs();
    return;
  }
  nav.hidden = false;
  const advancing = new Set((state.session && state.session.advancingStackIds) || []);
  const focused = state.stacks.focusedStackId;
  const wanted = new Set(occupied.map((s) => s.id));
  const existing = new Map(
    [...nav.querySelectorAll('.stack-tab')].map((el) => [el.dataset.stackId, el]),
  );
  const next = [];
  for (const stack of occupied) {
    let btn = existing.get(stack.id);
    if (!btn) {
      btn = document.createElement('button');
      btn.type = 'button';
      btn.dataset.stackId = stack.id;
      bindStackTab(btn);
    }
    const active = stack.id === focused;
    btn.className = 'stack-tab' + (active ? ' is-active' : '')
      + (stack.blank ? ' is-blank' : '') + (stack.frozen ? ' is-frozen' : '');

    let icon = btn.querySelector('img');
    // Two tints ship with every stack, so the glyph can carry focus the way
    // the desktop strip does.
    const iconUrl = active ? stack.iconUrlActive : stack.iconUrlIdle;
    if (iconUrl) {
      if (!icon) {
        icon = document.createElement('img');
        icon.alt = '';
        btn.prepend(icon);
      }
      if (icon.getAttribute('src') !== iconUrl) icon.src = iconUrl;
    } else if (icon) {
      icon.remove();
    }

    // The inner span carries the text; the outer one reserves the semibold
    // width through data-label, so focus never changes a tab's width.
    let name = btn.querySelector('.tab-name');
    if (!name) {
      name = document.createElement('span');
      name.className = 'tab-name';
      name.appendChild(document.createElement('span'));
      btn.appendChild(name);
    }
    const label = stack.name || stack.id;
    if (name.dataset.label !== label) {
      name.dataset.label = label;
      name.firstChild.textContent = label;
    }

    showDot(btn, 'adv-dot', advancing.has(stack.id));
    next.push(btn);
  }
  for (const [id, el] of existing) {
    if (!wanted.has(id)) el.remove();
  }
  next.forEach((el, i) => {
    if (nav.children[i] !== el) nav.insertBefore(el, nav.children[i] || null);
  });
  // Fitted now, not on the next frame: reading offsetWidth flushes layout, so
  // the fit already sees the tabs this render just produced. A rAF would
  // strand a backgrounded phone with a stale fit until it next got a frame.
  layoutStackTabs();
}

/// A dot of ``className`` (adv-dot) on ``btn`` while ``on``.
function showDot(btn, className, on) {
  const dot = btn.querySelector(`.${className}`);
  if (on && !dot) btn.appendChild(el('span', className));
  else if (!on && dot) dot.remove();
}

/// Gap between tabs and before "+N more". Mirrors .stack-strip / .stack-tabs.
const STACK_TAB_GAP = 3;
/// Stack ids behind "+N more", in stack order, from the last fit.
let stackOverflowIds = [];

/// Decide which stacks get a tab and which go behind "+N more". The fit reads
/// the strip's width and stack order only, never focus, so focusing a tab
/// restyles it without moving anything. Tab widths already reserve their
/// semibold names, so no focus state can change the result.
function layoutStackTabs() {
  const strip = $('stackStrip');
  const nav = $('stackTabs');
  const more = $('stackMore');
  const tabs = [...nav.querySelectorAll('.stack-tab')];
  if (nav.hidden || !tabs.length) {
    stackOverflowIds = [];
    more.hidden = true;
    closeStackMoreMenu();
    return;
  }
  tabs.forEach((t) => { t.hidden = false; });
  more.hidden = false;
  const moreWidth = more.offsetWidth + STACK_TAB_GAP;
  const widths = tabs.map((t) => t.offsetWidth);
  const avail = strip.clientWidth;
  const total = widths.reduce((a, w) => a + w, 0) + STACK_TAB_GAP * (widths.length - 1);
  let count = widths.length;
  if (total > avail) {
    count = 0;
    let used = 0;
    for (const w of widths) {
      const next = used + w + (count ? STACK_TAB_GAP : 0);
      if (next > avail - moreWidth) break;
      used = next;
      count += 1;
    }
  }
  tabs.forEach((t, i) => { t.hidden = i >= count; });
  stackOverflowIds = tabs.slice(count).map((t) => t.dataset.stackId);
  more.hidden = stackOverflowIds.length === 0;
  if (!stackOverflowIds.length) {
    closeStackMoreMenu();
    return;
  }

  const occupied = (state.stacks && state.stacks.occupied) || [];
  const focusedId = state.stacks.focusedStackId;
  const focused = stackOverflowIds.includes(focusedId)
    ? occupied.find((s) => s.id === focusedId) : null;
  const btn = $('stackMoreBtn');
  btn.classList.toggle('is-lit', !!focused);
  $('stackMoreLabel').textContent = `+${stackOverflowIds.length} more`;

  // What the stacks behind it say on their tabs: advancing.
  const advancing = new Set((state.session && state.session.advancingStackIds) || []);
  showDot(btn, 'adv-dot', stackOverflowIds.some((id) => advancing.has(id)));
  if (menuOpen($('stackMoreMenu'))) fillStackMoreMenu();
}

function fillStackMoreMenu() {
  const menu = $('stackMoreMenu');
  menu.textContent = '';
  const occupied = (state.stacks && state.stacks.occupied) || [];
  const focusedId = state.stacks && state.stacks.focusedStackId;
  const advancing = new Set((state.session && state.session.advancingStackIds) || []);
  for (const id of stackOverflowIds) {
    const stack = occupied.find((s) => s.id === id);
    if (!stack) continue;
    const chosen = id === focusedId;
    const item = el('button', 'menu-item' + (chosen ? ' is-chosen' : ''));
    item.type = 'button';
    item.setAttribute('role', 'menuitemradio');
    item.setAttribute('aria-checked', String(chosen));
    const iconUrl = chosen ? stack.iconUrlActive : stack.iconUrlIdle;
    item.append(iconUrl ? picture(iconUrl) : glyph('layers'),
      el('span', 'menu-label', stack.name || stack.id));
    if (advancing.has(id)) item.appendChild(el('span', 'menu-tag', 'Auto'));
    if (stack.cued) item.appendChild(el('span', 'menu-tag', 'Cued'));
    if (stack.blank) item.appendChild(el('span', 'menu-tag is-blank', 'Blank'));
    if (stack.frozen) item.appendChild(el('span', 'menu-tag is-frozen', 'Frozen'));
    item.onclick = () => send('setFocusedStackId', { stackId: id });
    menu.appendChild(item);
  }
}

function closeStackMoreMenu() {
  if (menuOpen($('stackMoreMenu'))) closeMenu();
}

/// A 26px tab is too small to hold two targets, so it does one thing and the
/// whole tab is that target. Shift or middle clears, everything else focuses;
/// the strip looks identical either way, so an incidental modifier press
/// changes nothing on screen while the room is watching. Touch has no
/// modifier, so there the menu's Clear Stack is the only route - which is the
/// safer default for a phone held mid-service anyway.
function bindStackTab(btn) {
  const id = btn.dataset.stackId;
  btn.onclick = (e) => {
    if (e.shiftKey) send('clearStack', { stackId: id });
    else send('setFocusedStackId', { stackId: id });
  };
  btn.onauxclick = (e) => {
    if (e.button !== 1) return;
    e.preventDefault();
    send('clearStack', { stackId: id });
  };
}

export function wireStacks() {
  // The strip's width moves for more reasons than a window resize - the drawer,
  // an orientation flip, the tools growing a chip. Watching the box itself
  // catches all of them, and the fit only toggles what's inside it, so no loop.
  new ResizeObserver(layoutStackTabs).observe($('stackStrip'));
  wireMenu($('stackMoreBtn'), $('stackMoreMenu'), { onOpen: fillStackMoreMenu });
}
