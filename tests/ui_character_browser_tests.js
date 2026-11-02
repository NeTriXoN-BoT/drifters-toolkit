'use strict';

// Focused layout acceptance for the supplied static model image. A temporary,
// isolated browser loads only the local UI; no native account or game backend.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const http = require('node:http');
const crypto = require('node:crypto');
const { chromium } = require('playwright');
const root = path.resolve(__dirname, '..');
const output = fs.mkdtempSync(path.join(os.tmpdir(), 'drifter-107-character-'));
const original = fs.readFileSync(path.join(root, 'ui/app.js'), 'utf8');
const app = original.replace(/\}\)\(\);\s*$/, 'window.characterProbe = {state,navigate,showModDetails,applyAppearance,openFinder};})();');
assert.notEqual(app, original);
const server = http.createServer((request, response) => {
  try {
    const relative = decodeURIComponent(new URL(request.url, 'http://localhost').pathname).slice(1);
    const file = path.resolve(root, relative);
    if (!(file.startsWith(path.join(root, 'ui') + path.sep) || file === path.join(root, 'catalog/catalog.json'))) {
      response.writeHead(404).end(); return;
    }
    const content = relative === 'ui/app.js' ? app : fs.readFileSync(file);
    const type = { '.html': 'text/html', '.js': 'application/javascript', '.css': 'text/css', '.png': 'image/png', '.svg': 'image/svg+xml', '.json': 'application/json' }[path.extname(file)] || 'application/octet-stream';
    response.writeHead(200, { 'Content-Type': type, 'Cache-Control': 'no-store' }).end(content);
  } catch { response.writeHead(404).end(); }
});

async function main() {
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  const browser = await chromium.launch({ headless: true, channel: 'msedge', chromiumSandbox: true });
  const report = { scope: 'Shared UI in an isolated Edge test browser, no native backend.',
    appSha256: crypto.createHash('sha256').update(original).digest('hex'), output, cases: [] };
  try {
    for (const mode of [
      { name: 'windows-dark', platform: 'windows', width: 1440, height: 1000, font: 100, color: 'dark' },
      { name: 'linux-light-large-text', platform: 'linux', width: 900, height: 760, font: 150, color: 'light' },
      { name: 'linux-minimum-contrast', platform: 'linux', width: 640, height: 560, font: 150, color: 'dark', contrast: true }
    ]) {
      const context = await browser.newContext({ viewport: { width: mode.width, height: mode.height }, reducedMotion: 'reduce' });
      const page = await context.newPage(), errors = [];
      page.on('pageerror', (error) => errors.push(String(error)));
      await page.goto(`http://127.0.0.1:${server.address().port}/ui/index.html`);
      await page.waitForFunction(() => window.characterProbe && !window.characterProbe.state.loading);
      await page.evaluate((mode) => {
        const ui = window.characterProbe;
        document.querySelector('#modal').close();
        ui.state.setupPrompted = true; ui.state.noticeQueue = [];
        ui.state.snapshot.platform = mode.platform;
        Object.assign(ui.state.snapshot.settings, { autoScale: false, interfaceScale: 100, fontScale: mode.font,
          colorMode: mode.color, highContrast: !!mode.contrast, reducedMotion: true, seasonalThemes: false });
        const art = ui.state.snapshot.mods.find((mod) => mod.id === 'art-the-clown-player-model');
        Object.assign(art, { imported: true, enabled: false, owned: false });
        ui.state.libraryMetaLoaded = true; ui.applyAppearance(); ui.navigate('mods');
      }, mode);
      await page.locator('[data-action="character-appearance"]').click();
      await page.locator('.character-preview-card img').waitFor();
      await page.waitForFunction(() => document.querySelector('.character-preview-card img')?.naturalWidth > 0);
      const appearancePage = await page.locator('#main-content').evaluate((element) => {
        const card = element.querySelector('.character-preview-card'), image = card.querySelector('img'), box = card.getBoundingClientRect(), rect = image.getBoundingClientRect();
        return { overflow: element.scrollWidth > element.clientWidth + 1,
          imageFits: rect.left >= box.left - 1 && rect.right <= box.right + 1,
          suppliedPreview: card.textContent.includes('View supplied preview'),
          title: element.querySelector('h1').textContent,
          noDisabledTab: !element.querySelector('[data-filter="disabled"]') };
      });
      assert.equal(appearancePage.overflow, false); assert(appearancePage.imageFits && appearancePage.suppliedPreview && appearancePage.noDisabledTab);
      assert(appearancePage.title.includes('Character appearance'));
      await page.screenshot({ path: path.join(output, mode.name + '-appearance-page.png') });
      await page.locator('#mod-search').fill('No such character package 012345');
      assert((await page.locator('#mod-list').textContent()).includes('No character packages in this view'));
      await page.locator('[data-clear-filters]').click();
      assert.equal(await page.locator('[data-filter="all"]').getAttribute('aria-pressed'), 'true');
      assert.equal(await page.locator('#mod-search').inputValue(), '');
      await page.locator('.character-preview-card').first().click();
      assert((await page.locator('#modal-content').textContent()).includes('static reference image'));
      await page.locator('#modal .modal-actions [data-modal-close]').click();
      await page.evaluate(() => window.characterProbe.showModDetails('art-the-clown-player-model'));
      await page.locator('#detail-character-image').waitFor();
      await page.waitForFunction(() => document.querySelector('#detail-character-image')?.naturalWidth > 0);
      const panel = await page.locator('#mod-details-panel').evaluate((element) => {
        const image = element.querySelector('#detail-character-image'), rect = image.getBoundingClientRect(), box = element.getBoundingClientRect();
        return { overflow: element.scrollWidth > element.clientWidth + 1, imageFits: rect.left >= box.left - 1 && rect.right <= box.right + 1,
          ratio: rect.width / rect.height, expectedRatio: image.naturalWidth / image.naturalHeight,
          caption: element.querySelector('.character-model-preview figcaption').textContent };
      });
      assert.equal(panel.overflow, false); assert(panel.imageFits);
      assert(Math.abs(panel.ratio - panel.expectedRatio) < .02, 'The author model image keeps its aspect ratio');
      assert(panel.caption.includes('static reference image'));
      await page.screenshot({ path: path.join(output, mode.name + '-details.png') });
      await page.locator('#detail-character-preview').click();
      assert.equal(await page.locator('#modal').evaluate((element) => element.open), true);
      const modal = await page.locator('#modal').evaluate((element) => {
        const image = element.querySelector('.character-model-preview img'), box = element.getBoundingClientRect(), rect = image.getBoundingClientRect();
        const close = element.querySelector('.modal-actions [data-modal-close]').getBoundingClientRect();
        return { overflow: element.scrollWidth > element.clientWidth + 1, imageFits: rect.left >= box.left - 1 && rect.right <= box.right + 1,
          closeReachable: close.bottom <= innerHeight + 1 && close.top >= 0 };
      });
      assert.equal(modal.overflow, false); assert(modal.imageFits); assert(modal.closeReachable);
      await page.screenshot({ path: path.join(output, mode.name + '-preview.png') });
      await page.locator('#modal .modal-actions [data-modal-close]').click();
      await page.locator('#main-nav [data-view="workshop"]').click();
      await page.locator('.workshop-starters').waitFor();
      assert.equal(await page.locator('#main-nav [data-view="workshop"]').getAttribute('aria-current'), 'page');
      assert.equal(await page.locator('#main-nav [data-view="help"]').getAttribute('aria-current'), null);
      const workshopPage = await page.locator('#main-content').evaluate((element) => ({
        overflow: element.scrollWidth > element.clientWidth + 1,
        starters: [...element.querySelectorAll('.workshop-starters h2')].map((heading) => heading.textContent),
        sourceLimit: element.textContent.includes('Choose your mod PAKs')
      }));
      assert.equal(workshopPage.overflow, false); assert.deepEqual(workshopPage.starters, ['Edit a mod', 'Combine mods']); assert(workshopPage.sourceLimit);
      await page.screenshot({ path: path.join(output, mode.name + '-workshop-page.png') });
      await page.locator('#main-nav [data-view="mods"]').click();
      await page.locator('[data-action="library-tools"]').click();
      const libraryTools = await page.locator('#modal').evaluate((element) => {
        const close = element.querySelector('.modal-actions [data-modal-close]').getBoundingClientRect();
        return { overflow: element.scrollWidth > element.clientWidth + 1,
          closeReachable: close.bottom <= innerHeight + 1 && close.top >= 0,
          workshop: !!element.querySelector('#library-tool-workshop'), character: !!element.querySelector('#library-tool-characters') };
      });
      assert(!libraryTools.overflow && libraryTools.closeReachable && libraryTools.workshop && libraryTools.character);
      await page.locator('#library-tool-workshop').click();
      assert.equal(await page.locator('#modal').evaluate((element) => element.open), false);
      assert.equal(await page.locator('#main-nav [data-view="workshop"]').getAttribute('aria-current'), 'page');
      await page.locator('#main-nav [data-view="help"]').scrollIntoViewIfNeeded();
      const sidebar = await page.locator('#main-nav [data-view="help"]').evaluate((element) => { const rect = element.getBoundingClientRect(); return { reachable: rect.top >= 0 && rect.bottom <= innerHeight + 1 }; });
      assert(sidebar.reachable, 'The added Workshop entry keeps lower navigation reachable');
      await page.evaluate(() => {
        const ui = window.characterProbe;
        ui.state.preview = false; ui.state.snapshot.gameRunning = false;
        ui.state.snapshot.finder = { active: true, stage: 'testing', round: 2, canContinue: false, canKeepCurrent: true,
          unavailable: [{ id: 'missing', name: 'A missing library package with a long name ' + 'Package_'.repeat(16), reason: 'The current library copy cannot be verified.' }] };
        ui.openFinder();
      });
      const finderReview = await page.locator('#modal').evaluate((element) => {
        const restore = element.querySelector('#finder-stop-restore').getBoundingClientRect();
        return { overflow: element.scrollWidth > element.clientWidth + 1,
          footerVisible: restore.top >= 0 && restore.bottom <= innerHeight + 1 };
      });
      assert(!finderReview.overflow && finderReview.footerVisible);
      await page.locator('#finder-stop-keep').click();
      assert((await page.locator('#modal-content').textContent()).includes('Stop without restoring?'));
      const finderConfirm = await page.locator('#confirm-action').evaluate((element) => {
        const rect = element.getBoundingClientRect(); return { footerVisible: rect.top >= 0 && rect.bottom <= innerHeight + 1 };
      });
      assert(finderConfirm.footerVisible);
      assert.equal(errors.length, 0, errors.join('\n'));
      report.cases.push({ ...mode, appearancePage, panel, modal, workshopPage, libraryTools, sidebar, finderReview, finderConfirm, scriptErrors: errors });
      await context.close();
    }
    fs.writeFileSync(path.join(output, 'report.json'), JSON.stringify(report, null, 2));
    console.log(JSON.stringify({ passed: true, layouts: report.cases.length, output }));
  } finally { await browser.close(); }
}
main().catch((error) => { console.error(error); process.exitCode = 1; }).finally(() => server.close());
