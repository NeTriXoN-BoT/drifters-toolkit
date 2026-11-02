'use strict';

// Optional focused layout/keyboard acceptance. Uses an isolated synthetic browser
// profile and serves the real UI locally; it never connects to a native loader.
// NODE_PATH must expose Playwright. TOOLKIT_TEST_BROWSER may name an installed browser.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const http = require('node:http');
const crypto = require('node:crypto');
const { chromium } = require('playwright');
const root = path.resolve(__dirname, '..');
const output = process.argv[2] ? path.resolve(process.argv[2]) : fs.mkdtempSync(path.join(os.tmpdir(), 'drifter-activity-106-'));
fs.mkdirSync(output, { recursive: true });
const original = fs.readFileSync(path.join(root, 'ui/app.js'), 'utf8');
const app = original.replace(/\}\)\(\);\s*$/, 'window.uiActivityProbe = { state, recentActivity, navigate, recordRecentActivity, showRecentActivity, applyAppearance, render };})();');
assert.notEqual(app, original, 'The UI test export must be injected');
const css = fs.readFileSync(path.join(root, 'ui/style.css'));
const hash = (bytes) => crypto.createHash('sha256').update(bytes).digest('hex');
const server = http.createServer((request, response) => {
  try {
    const relative = decodeURIComponent(new URL(request.url, 'http://localhost').pathname).slice(1);
    const absolute = path.resolve(root, relative);
    if (!(absolute.startsWith(path.join(root, 'ui') + path.sep) || absolute === path.join(root, 'catalog/catalog.json'))) {
      response.writeHead(404).end(); return;
    }
    const data = relative === 'ui/app.js' ? app : relative === 'ui/style.css' ? css : fs.readFileSync(absolute);
    const type = { '.html': 'text/html', '.js': 'application/javascript', '.css': 'text/css', '.json': 'application/json', '.png': 'image/png', '.svg': 'image/svg+xml' }[path.extname(absolute)] || 'application/octet-stream';
    response.writeHead(200, { 'Content-Type': type, 'Cache-Control': 'no-store' }).end(data);
  } catch { response.writeHead(404).end(); }
});

async function main() {
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  const browser = await chromium.launch({ headless: true,
    ...(process.env.TOOLKIT_TEST_BROWSER ? { executablePath: process.env.TOOLKIT_TEST_BROWSER } : {}) });
  const report = { scope: 'Focused production shared UI in isolated Chromium browser; synthetic Windows/Linux snapshot values, not native OS backends.', appSha256: hash(original), cssSha256: hash(css), cases: [] };
  try {
    for (const mode of [
      { name: 'windows-desktop', width: 1440, height: 1000, platform: 'windows', font: 100 },
      { name: 'linux-narrow-large-text', width: 760, height: 760, platform: 'linux', font: 150, contrast: true, reduced: true },
      { name: 'windows-4k-large-text', width: 3840, height: 2160, platform: 'windows', font: 150 },
      { name: 'linux-minimum-large-text', width: 640, height: 560, platform: 'linux', font: 150, reduced: true }
    ]) {
      const context = await browser.newContext({ viewport: { width: mode.width, height: mode.height },
        reducedMotion: mode.reduced ? 'reduce' : 'no-preference' });
      const page = await context.newPage();
      const errors = [];
      page.on('pageerror', (error) => errors.push(String(error)));
      await page.goto(`http://127.0.0.1:${server.address().port}/ui/index.html`);
      await page.waitForFunction(() => window.uiActivityProbe && !window.uiActivityProbe.state.loading);
      await page.evaluate((mode) => {
        const ui = window.uiActivityProbe;
        document.querySelector('#modal').close();
        ui.state.setupPrompted = true; ui.state.noticeQueue = [];
        ui.state.snapshot.platform = mode.platform;
        Object.assign(ui.state.snapshot.settings, { autoScale: false, interfaceScale: 100, fontScale: mode.font,
          highContrast: !!mode.contrast, reducedMotion: !!mode.reduced });
        ui.applyAppearance(); ui.navigate('settings');
      }, mode);
      await page.keyboard.press('Control+f');
      await assert.doesNotReject(() => page.locator('#settings-search').waitFor({ state: 'visible' }));
      assert(await page.locator('#settings-search').evaluate((el) => el === document.activeElement));
      await page.locator('#settings-search').fill('contrast');
      await page.keyboard.press('Escape');
      assert.equal(await page.locator('#settings-search').inputValue(), '');
      assert.equal(await page.evaluate(() => window.uiActivityProbe.state.view), 'settings');
      await page.locator('#settings-search').fill('typing');
      await page.keyboard.type('/?');
      assert.equal(await page.locator('#settings-search').inputValue(), 'typing/?');
      assert.equal(await page.locator('#modal').evaluate((el) => el.open), false);
      await page.locator('#main-content').focus();
      await page.keyboard.type('?');
      assert.equal(await page.locator('#modal-title').textContent(), 'Keyboard shortcuts');
      await page.keyboard.press('Escape');
      await page.evaluate(() => {
        const ui = window.uiActivityProbe;
        ui.recentActivity.length = 0;
        for (let i = 0; i < 50; i++) ui.recordRecentActivity(`Result ${i}: ${'A_long_mod_package_name_'.repeat(12)} <img src=x>`,
          { type: i % 3 === 0 ? 'error' : i % 3 === 1 ? 'warning' : 'success', label: `Operation ${i}` });
      });
      await page.locator('#recent-activity-button').click();
      assert.equal(await page.locator('.recent-activity-entry').count(), 50);
      assert.equal(await page.locator('#recent-activity-content img').count(), 0);
      const layout = await page.evaluate(() => {
        const dialog = document.querySelector('#modal'), top = document.querySelector('.topbar');
        return { bodyOverflow: document.body.scrollWidth > document.documentElement.clientWidth + 1,
          dialogOverflow: dialog.scrollWidth > dialog.clientWidth + 1,
          topbarOverflow: top.scrollWidth > top.clientWidth + 1,
          footerVisible: document.querySelector('#recent-activity-clear').getBoundingClientRect().bottom <= innerHeight,
          reduced: document.documentElement.classList.contains('reduce-motion') };
      });
      assert.equal(layout.bodyOverflow, false, `${mode.name}: no body horizontal overflow`);
      assert.equal(layout.dialogOverflow, false, `${mode.name}: long result names wrap`);
      assert.equal(layout.topbarOverflow, false, `${mode.name}: header actions wrap`);
      assert.equal(layout.footerVisible, true, `${mode.name}: clear/help controls stay visible with long history`);
      await page.screenshot({ path: path.join(output, mode.name + '-activity.png') });
      await page.locator('#recent-keyboard-help').click();
      const helpOverflow = await page.locator('#modal').evaluate((el) => el.scrollWidth > el.clientWidth + 1);
      assert.equal(helpOverflow, false, `${mode.name}: keyboard guide wraps`);
      await page.screenshot({ path: path.join(output, mode.name + '-keyboard.png') });
      await page.locator('#keyboard-activity-back').click();
      assert.equal(await page.locator('.recent-activity-entry').count(), 50);
      await page.locator('#recent-activity-clear').click();
      assert.equal(await page.locator('.recent-activity-entry').count(), 0);
      assert(await page.locator('#recent-activity-clear').isDisabled());
      assert.equal(errors.length, 0, errors.join('\n'));
      report.cases.push({ name: mode.name, ...layout, helpOverflow, scriptErrors: errors });
      await context.close();
    }
    fs.writeFileSync(path.join(output, 'report.json'), JSON.stringify(report, null, 2) + '\n');
    console.log(`Focused browser activity/keyboard acceptance passed ${report.cases.length} layouts. Evidence: ${output}`);
  } finally { await browser.close(); }
}
main().catch((error) => { console.error(error); process.exitCode = 1; }).finally(() => server.close());
