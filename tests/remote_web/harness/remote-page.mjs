// The web remote as a test drives it: where things are on the page and what a
// person does with them. Specs go through this, so a change to the client's
// markup is fixed here once.

import { expect } from '@playwright/test';

/** ``text`` as a whole-string pattern. */
export function exactly(text) {
  return new RegExp(`^${text.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}$`);
}

export class RemotePage {
  constructor(page, app) {
    this.page = page;
    this.app = app;
    /** Every command the page sent, in order: ``{ name, args }``. */
    this.sent = [];
    /** Every socket the page opened, open or closed. */
    this.sockets = [];
    page.on('websocket', (ws) => {
      this.sockets.push(ws);
      ws.on('framesent', ({ payload }) => {
        let msg;
        try { msg = JSON.parse(String(payload)); } catch { return; }
        if (msg.type === 'cmd') this.sent.push({ name: msg.name, args: msg.args });
      });
    });

    // Sign-in
    this.gate = page.locator('#gate');
    this.pin = page.locator('#gatePin');
    this.gateError = page.locator('#gateError');
    this.connect = this.gate.getByRole('button', { name: 'Connect' });
    this.appRoot = page.locator('#app');
    this.offline = page.locator('#offline');

    // Service
    this.servicePanel = page.locator('#colLeft .left-panel-top');
    this.serviceList = page.locator('#serviceItems');
    this.serviceRows = this.serviceList.locator('[data-item-id]');

    // Library
    this.libraryPanel = page.locator('#colLeft .left-panel-bottom');
    this.libraryTabs = page.locator('#libTabs');
    // The strip's arrows; the page out of view is inert, and a click there
    // would wait for ever, so these are clicked only on the page in view.
    this.moreTabs = this.libraryTabs.locator('.tab-page:not([inert])')
      .getByRole('button', { name: 'More tabs' });
    this.firstTabs = this.libraryTabs.locator('.tab-page:not([inert])')
      .getByRole('button', { name: 'Back to the first tabs' });
    this.librarySearch = page.locator('#libSearch');
    this.librarySearchClear = page.locator('#libSearchClear');
    this.libraryList = page.locator('#libList');
    this.libraryRows = this.libraryList.locator('.list-item');
    this.bibleSelect = page.locator('#bibleSelectBtn');
    this.bibleMenu = page.locator('#bibleMenu');
    this.bibleReference = page.locator('#bibleQuickRef');
    this.bibleSuggestions = page.locator('#bibleSuggest .menu-item');
    this.verses = this.libraryList.locator('.verse-item');
    this.libraryEmpty = this.libraryList.locator('.empty');

    // Controller
    this.live = page.locator('#u_lines');
    this.lines = this.live.locator('.lyric-line');
    this.currentLine = this.live.locator('.lyric-line.current');
    // The live line's words, without the verse code shown beside them.
    this.currentWords = this.currentLine.locator('.line-content');
    this.thumbs = this.live.locator('.img-thumb-ctrl');
    this.mediaPreview = this.live.locator('.media-preview-img');
    this.next = page.locator('#btnNext');
    this.prev = page.locator('#btnPrev');
    this.blank = page.locator('#btnBlank');
    this.freeze = page.locator('#btnFreeze');
    this.slideControls = page.locator('#slideControls');
    this.videoControls = page.locator('#videoControls');
    this.videoBar = page.locator('#videoBar');
    this.videoPlay = page.locator('#btnVideoPlay');
    this.videoRestart = page.locator('#btnVideoRestart');
    this.videoBlank = page.locator('#btnBlankVideo');
    this.videoSeek = page.locator('#videoSeek');
    this.videoPosition = page.locator('#videoPos');
    this.videoDuration = page.locator('#videoDur');
    this.webControls = page.locator('#webControls');
    this.webBack = page.locator('#btnWebBack');
    this.webReload = page.locator('#btnWebReload');
    this.webAddress = this.live.locator('.media-address');
    // The app's mark, faint, where nothing is shown: nothing live, a video,
    // audio.
    this.liveMark = this.live.locator('.well-mark');
    this.title = page.locator('#mobileNavTitle');

    // A stack's Preview, beside what is live, and the take between them.
    this.previewColumn = page.locator('#previewCol');
    this.preview = page.locator('#p_lines');
    this.previewLines = this.preview.locator('.lyric-line');
    this.previewCurrentWords = this.preview.locator('.lyric-line.current .line-content');
    this.previewClear = page.locator('#btnPreviewClear');
    this.previewBlank = page.locator('#btnPreviewBlank');
    this.transition = page.locator('#btnTransition');
    this.takeMenuButton = page.locator('#btnTakeMenu');
    this.takeMenu = page.locator('#takeMenu');
    this.takeCut = page.locator('#takeCut');
    this.takeFade = page.locator('#takeFade');
    this.takeFadeShown = page.locator('#takeFadeShown');
    this.takeFadeLength = page.locator('#takeFadeSec');
    this.takeFadeLonger = page.locator('#takeFadeMore');
    this.takeFadeShorter = page.locator('#takeFadeLess');
    this.takeHeld = page.locator('#takeHeld');
    this.drawerButton = page.locator('#btnDrawer');

    // Stacks and the controller menu
    this.stackTabs = page.locator('#stackTabs .stack-tab');
    this.autoAdvance = page.locator('#btnAutoAdvance');
    this.controllerMenuButton = page.locator('#ctrlSettingsBtn');
    this.controllerMenu = page.locator('#ctrlDropdown');
    this.advanceInterval = page.locator('#autoAdvanceInterval');
    this.advanceLoop = page.locator('#autoAdvanceLoop');
    this.clearStack = page.locator('#menuClearStack');
    this.clearAllStacks = page.locator('#menuClearAllStacks');

    this.toasts = page.locator('#toasts .app-toast');
  }

  /** Load the remote. Chromium drops whatever is loading when the machine's
   *  network changes (a VPN, Docker starting a container: net::
   *  ERR_NETWORK_CHANGED), and a page whose script was dropped never starts;
   *  that one failure, and only while loading, gets a second load. */
  async open() {
    for (let attempt = 1; ; attempt += 1) {
      const dropped = [];
      const onFailed = (request) => {
        if (request.failure()?.errorText === 'net::ERR_NETWORK_CHANGED') dropped.push(request.url());
      };
      this.page.on('requestfailed', onFailed);
      try {
        await this.page.goto(this.app.url);
      } finally {
        this.page.off('requestfailed', onFailed);
      }
      if (!dropped.length || attempt === 2) return;
    }
  }

  /** Enter the PIN and wait for the first snapshot to be drawn. */
  async signIn(pin = this.app.fixture.pin) {
    await expect(this.gate).toBeVisible();
    await this.pin.fill(pin);
    await this.connect.click();
    await this.waitForSnapshot();
  }

  /** The app is showing and the server's state has been drawn once. */
  async waitForSnapshot() {
    await expect(this.appRoot).toBeVisible();
    await expect(this.offline).toBeHidden();
    // The Controller has drawn what is live, or the mark for nothing.
    await expect(this.live).toHaveAttribute('data-mode', /\w/);
  }

  /** The commands the page sent named ``name``. */
  commands(name) {
    return this.sent.filter((c) => c.name === name);
  }

  /** How many of the page's sockets are open. One closes, for the browser,
   *  once the server has closed its end too. */
  openSockets() {
    return this.sockets.filter((ws) => !ws.isClosed()).length;
  }

  // --- Phone --------------------------------------------------------------------

  /** Open the drawer that holds the service and the library (a phone). */
  async openDrawer() {
    await this.drawerButton.click();
    await expect(this.page.locator('body')).toHaveClass(/\bdrawer-left-open\b/);
  }

  async closeDrawer() {
    await this.drawerButton.click();
    await expect(this.page.locator('body')).not.toHaveClass(/\bdrawer-left-open\b/);
  }

  // --- Library ---------------------------------------------------------------

  /** Every library tab, in the strip's order: the first five, then those
   *  behind its arrow. */
  get libraryTabLabels() {
    return ['Songs', 'Freeform', 'Bibles', 'Videos', 'Images', 'Audio', 'Browser', 'Live'];
  }

  /** How many tabs the strip shows before its arrow (library.js STRIP_TABS). */
  get libraryStripTabs() {
    return 5;
  }

  libraryTab(label) {
    return this.libraryTabs.getByRole('button', { name: label, exact: true });
  }

  /** Show tab ``label``, turning the strip to its page first. */
  async showLibrary(label) {
    const behind = this.libraryTabLabels.indexOf(label) >= this.libraryStripTabs;
    const onMore = await this.libraryTabs.evaluate((el) => el.classList.contains('on-more'));
    if (behind !== onMore) await (behind ? this.moreTabs : this.firstTabs).click();
    await this.libraryTab(label).click();
    await expect(this.libraryTab(label)).toHaveClass(/\bactive\b/);
  }

  /** The library row titled exactly ``title``. */
  libraryRow(title) {
    return this.libraryRows.filter({
      has: this.page.locator('.row-title', { hasText: exactly(title) }),
    });
  }

  /** Play ``title`` from the library tab ``label``. */
  async playFromLibrary(label, title) {
    await this.showLibrary(label);
    await this.libraryRow(title).dblclick();
  }

  // --- Service ---------------------------------------------------------------

  /** The rundown row titled ``title`` (its number aside), or the divider
   *  labelled ``title``. */
  serviceRow(title) {
    return this.serviceRows.filter({
      has: this.page.locator('.row-title, .divider-label', { hasText: exactly(title) }),
    });
  }

  /** The rundown's titles and divider labels, top to bottom, as shown: a
   *  numbered item as "2. Holy Holy Holy". */
  async serviceTitles() {
    return this.serviceRows.evaluateAll((rows) => rows.map((row) => {
      const label = row.querySelector('.divider-label');
      if (label) return label.textContent.trim();
      const num = row.querySelector('.row-num');
      const title = row.querySelector('.row-title').textContent.trim();
      return num ? `${num.textContent.trim()} ${title}` : title;
    }));
  }

  /** The rundown is of service ``name`` ('' for none open): the page shows
   *  no name, so the list is labelled with it. */
  async expectService(name) {
    await expect(this.serviceList).toHaveAttribute('aria-label', name || 'Service');
  }

  /** Open service ``id`` from ``observer``, as the console would (the
   *  remote opens none), and wait until the page shows it. */
  async openServiceFrom(observer, id, name) {
    await observer.run('openService', { serviceId: id });
    await this.expectService(name);
  }

  // --- Controller ------------------------------------------------------------

  /** The live line whose text is ``text``. */
  line(text) {
    return this.lines.filter({
      has: this.page.locator('.line-content', { hasText: exactly(text) }),
    });
  }

  async openControllerMenu() {
    await this.controllerMenuButton.click();
    await expect(this.controllerMenu).toHaveClass(/\bopen\b/);
  }

  async closeControllerMenu() {
    if (await this.controllerMenu.evaluate((el) => el.classList.contains('open'))) {
      await this.controllerMenuButton.click();
    }
    await expect(this.controllerMenu).not.toHaveClass(/\bopen\b/);
  }

  stackTab(name) {
    return this.stackTabs.filter({ hasText: exactly(name) });
  }

  /** The Preview's line whose text is ``text``. */
  previewLine(text) {
    return this.previewLines.filter({
      has: this.page.locator('.line-content', { hasText: exactly(text) }),
    });
  }
}
