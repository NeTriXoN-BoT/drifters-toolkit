'use strict';

// Real shared UI in an isolated headless browser, with a synthetic native reply.
// No user browser profile, game files, credentials or external network is used.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const http = require('node:http');
const crypto = require('node:crypto');
const { chromium } = require('playwright');
const root = path.resolve(__dirname, '..');
const output = process.argv[2] ? path.resolve(process.argv[2]) : fs.mkdtempSync(path.join(os.tmpdir(), 'drifter-changelog-'));
fs.mkdirSync(output, { recursive: true });
const original = fs.readFileSync(path.join(root, 'ui/app.js'), 'utf8');
const script = original.replace(/\}\)\(\);\s*$/, 'window.changelogProbe={state,pending,changelogReleases,showChangelog,showWhatsNew,checkForUpdate,render,applyAppearance};})();');
assert.notEqual(script, original);
const server = http.createServer((request, response) => {
  try {
    const relative = decodeURIComponent(new URL(request.url, 'http://localhost').pathname).slice(1);
    const file = path.resolve(root, relative);
    if (!(file.startsWith(path.join(root, 'ui') + path.sep) || file === path.join(root, 'catalog/catalog.json'))) {
      response.writeHead(404).end(); return;
    }
    const body = relative === 'ui/app.js' ? script : fs.readFileSync(file);
    const mime = { '.html': 'text/html', '.js': 'text/javascript', '.json': 'application/json', '.css': 'text/css',
      '.png': 'image/png', '.svg': 'image/svg+xml', '.webp': 'image/webp' }[path.extname(file)] || 'application/octet-stream';
    response.writeHead(200, { 'Content-Type': mime, 'Cache-Control': 'no-store' }).end(body);
  } catch { response.writeHead(404).end(); }
});

async function main() {
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  const origin = `http://127.0.0.1:${server.address().port}`;
  const browser = await chromium.launch({ headless: true, chromiumSandbox: true,
    ...(process.env.TOOLKIT_TEST_BROWSER ? { executablePath: process.env.TOOLKIT_TEST_BROWSER } : { channel: 'msedge' }) });
  const report = { scope: 'Isolated real Edge shared interface and synthetic native replies; not a native loader crash reproduction.',
    appSha256: crypto.createHash('sha256').update(original).digest('hex'), cases: [] };
  try {
    for (const mode of [
      { name: 'windows-dark', platform: 'windows', width: 1440, height: 960, font: 100, color: 'dark' },
      { name: 'linux-light-large', platform: 'linux', width: 900, height: 760, font: 150, color: 'light' },
      { name: 'linux-small-contrast', platform: 'linux', width: 640, height: 560, font: 150, color: 'dark', contrast: true }
    ]) {
      const context = await browser.newContext({ viewport: { width: mode.width, height: mode.height }, reducedMotion: 'reduce' });
      await context.route('**/*', (route) => route.request().url().startsWith(origin + '/') ? route.continue() : route.abort());
      const page = await context.newPage(), errors = [];
      page.on('pageerror', (error) => errors.push(String(error)));
      page.on('crash', () => errors.push('Browser renderer crashed'));
      await page.goto(origin + '/ui/index.html');
      await page.waitForFunction(() => window.changelogProbe && !window.changelogProbe.state.loading);
      const result = await page.evaluate(async (mode) => {
        const ui = window.changelogProbe;
        document.querySelector('#modal').close();
        ui.state.noticeQueue = []; ui.state.setupPrompted = true;
        ui.state.snapshot.platform = mode.platform;
        ui.state.snapshot.appVersion = window.WHATS_NEW.version;
        Object.assign(ui.state.snapshot.settings, { autoScale: false, interfaceScale: 100, fontScale: mode.font,
          colorMode: mode.color, highContrast: !!mode.contrast, reducedMotion: true, seasonalThemes: false });
        ui.applyAppearance(); ui.showChangelog();
        const entry = document.querySelector('#changelog-entry');
        const firstPicker = document.querySelector('#changelog-picker');
        firstPicker.open = true;
        await new Promise((resolve) => setTimeout(resolve, 30));
        const menuBox = firstPicker.querySelector('.changelog-options').getBoundingClientRect();
        const bodyBox = firstPicker.closest('.modal-body').getBoundingClientRect();
        const pickerFits = menuBox.top >= bodyBox.top - 1 && menuBox.bottom <= Math.min(bodyBox.bottom, innerHeight) + 1;
        const pickerBounds = { top: menuBox.top, bottom: menuBox.bottom, bodyTop: bodyBox.top, bodyBottom: bodyBox.bottom };
        firstPicker.open = false;
        const observer = new MutationObserver(() => {});
        observer.observe(entry, { childList: true, subtree: true });
        const selected = document.querySelector('.changelog-option[aria-current="true"]');
        for (let i = 0; i < 2000; ++i) selected.onclick();
        const repeatedMutations = observer.takeRecords().length;
        observer.disconnect();
        const choices = [...document.querySelectorAll('.changelog-option')];
        const picker = document.querySelector('#changelog-picker');
        const panels = [...entry.children];
        const historyObserver = new MutationObserver(() => {});
        historyObserver.observe(entry, { childList: true, subtree: true });
        for (let i = 0; i < 400; ++i) {
          picker.open = true;
          choices[i % choices.length].focus();
          choices[i % choices.length].click();
        }
        const versionMutations = historyObserver.takeRecords().length;
        historyObserver.disconnect();
        const selectedVersion = ui.changelogReleases()[(400 - 1) % choices.length].version;
        const stablePanels = panels.length === choices.length &&
          panels.every((panel, index) => panel === entry.children[index]);
        const visiblePanels = panels.filter((panel) => !panel.hidden);
        const selectedPanel = visiblePanels.length === 1 &&
          visiblePanels[0].querySelector('h3')?.textContent === 'Version ' + selectedVersion;
        const unnumbered = entry.querySelectorAll('li,ul,ol').length === 0;
        const staleChoice = choices.find((choice) => choice.getAttribute('aria-current') !== 'true').onclick;
        ui.showWhatsNew();
        const replacement = document.querySelector('#modal-content').innerHTML;
        for (let i = 0; i < 500; ++i) staleChoice();
        const replacementPreserved = document.querySelector('#modal-content').innerHTML === replacement;
        for (let i = 0; i < 80; ++i) {
          ui.showWhatsNew(); document.querySelector('#whats-new-changelog').click();
          document.querySelector('#changelog-back').click();
        }
        ui.showChangelog();
        const closedChoice = document.querySelector('.changelog-option[aria-current="false"]').onclick;
        document.querySelector('#modal').close();
        for (let i = 0; i < 500; ++i) closedChoice();
        const closed = !document.querySelector('#modal').open;

        // Late browser callbacks are resolved through the test-only injected
        // pending map, with the same UI request lifecycle as a native reply.
        let scans = 0;
        window.chrome ||= {};
        window.chrome.webview = { postMessage(request) {
          if (request.action === 'updateCheckDownloads') scans++;
          setTimeout(() => {
            const pending = ui.pending.get(request.requestId);
            if (!pending) return;
            clearTimeout(pending.timer); ui.pending.delete(request.requestId);
            pending.resolve(request.action === 'snapshot' ? ui.state.snapshot : { found: false });
          }, 25);
        } };
        ui.state.preview = false; ui.state.loading = true; ui.state.busy = false;
        ui.state.snapshot.sandbox = false;
        ui.state.update = { lastCheck: 0, checking: false, offer: null };
        await Promise.all(Array.from({ length: 1000 }, () => ui.checkForUpdate(false)));
        await Promise.all(Array.from({ length: 1000 }, () => ui.checkForUpdate(false)));
        ui.state.preview = true; ui.state.loading = false;
        ui.showChangelog();
        const modal = document.querySelector('#modal').getBoundingClientRect();
        const footer = document.querySelector('#modal .modal-actions').getBoundingClientRect();
        return { version: ui.state.snapshot.appVersion, pickerFits, pickerBounds, repeatedMutations, versionMutations, stablePanels, selectedPanel, unnumbered, replacementPreserved, closed, scans, pending: ui.pending.size,
          dialogFits: modal.left >= -1 && modal.right <= innerWidth + 1 && modal.top >= -1 && modal.bottom <= innerHeight + 1,
          footerVisible: footer.top >= 0 && footer.bottom <= innerHeight + 1,
          sameChoiceClicks: 2000, alternateChoiceClicks: 400, staleChoiceClicks: 1000, backLoops: 80, checkRequests: 2000 };
      }, mode);
      assert.equal(result.repeatedMutations, 0, 'Same-version spam must not rebuild release notes');
      assert.equal(result.versionMutations, 0, 'Different versions retain all parsed note nodes');
      assert(result.stablePanels && result.selectedPanel && result.unnumbered, 'Only the selected unnumbered release panel is visible');
      assert(result.pickerFits && result.footerVisible, 'Open version options and Close remain inside the viewport at large text sizes');
      assert(result.replacementPreserved && result.closed && result.dialogFits);
      assert.equal(result.scans, 1, 'Repeated checks must admit one synthetic native scan');
      assert.equal(result.pending, 0, 'Completed checks release pending callbacks');
      assert.deepEqual(errors, [], 'Changelog stress must not throw or crash the real renderer');
      await page.screenshot({ path: path.join(output, mode.name + '.png') });
      report.cases.push({ ...mode, ...result, errors });
      await context.close();
    }
    report.passed = true;
    fs.writeFileSync(path.join(output, 'report.json'), JSON.stringify(report, null, 2));
    console.log(JSON.stringify({ passed: true, cases: report.cases.length, output }));
  } finally { await browser.close(); await new Promise((resolve) => server.close(resolve)); }
}
main().catch((error) => { console.error(error); process.exitCode = 1; server.close(); });
