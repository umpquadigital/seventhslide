// Pop-up menus, as the app's AppMenu: each hangs off the button that opens
// it, one is open at a time, and Escape, a tap outside it or choosing one of
// its items closes it. A menu's items are role="menuitem"; anything else in
// it (a switch, a field) leaves it open.

// Each menu's button and what it does as it opens; the menu open now.
const menus = new Map();
let shown = null;

/* ``button`` opens and closes ``menu``; ``onOpen`` fills it first. */
export function wireMenu(button, menu, { onOpen } = {}) {
  menus.set(menu, { button, onOpen });
  button.setAttribute('aria-expanded', 'false');
  button.addEventListener('click', (e) => {
    e.stopPropagation();
    if (shown === menu) closeMenu();
    else openMenu(menu);
  });
  // A choice closes it, after the item's own handler has run.
  menu.addEventListener('click', (e) => {
    if (e.target.closest('[role="menuitem"], [role="menuitemradio"]')) closeMenu();
  });
}

export function openMenu(menu) {
  closeMenu();
  const entry = menus.get(menu);
  if (!entry) return;
  if (entry.onOpen) entry.onOpen();
  menu.classList.add('open');
  entry.button.classList.add('open');
  entry.button.setAttribute('aria-expanded', 'true');
  shown = menu;
}

export function closeMenu() {
  if (!shown) return;
  const { button } = menus.get(shown);
  shown.classList.remove('open');
  button.classList.remove('open');
  button.setAttribute('aria-expanded', 'false');
  shown = null;
}

export function menuOpen(menu) {
  return shown === menu;
}

/* Escape closes the open menu and gives its button the focus back. Answers
   whether it did, so the key does nothing else. */
export function menuKeydown(e) {
  if (e.key !== 'Escape' || !shown) return false;
  const { button } = menus.get(shown);
  closeMenu();
  button.focus();
  e.preventDefault();
  return true;
}

export function wireMenus() {
  document.addEventListener('click', (e) => {
    if (!shown) return;
    const { button } = menus.get(shown);
    if (!shown.contains(e.target) && !button.contains(e.target)) closeMenu();
  });
}
