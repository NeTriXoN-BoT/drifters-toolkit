'use strict';

// Optional real-renderer acceptance. All native replies and save pictures are
// synthetic; the server permits UI assets only and external requests are denied.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const http = require('node:http');
const crypto = require('node:crypto');
const { chromium } = require('playwright');
const root = path.resolve(__dirname, '..');
const output = path.join(root, 'build-artifacts/2026-10-30-next-update/save-thumbnails-browser');
fs.mkdirSync(output, { recursive: true });
const original = fs.readFileSync(path.join(root, 'ui/app.js'), 'utf8');
let app = original.replace(/\}\)\(\);\s*$/, `window.thumbnailProbe={state,navigate,render,applyAppearance,
  setSavesTab,syncWindowVisibility,syncCurrentSavesPoll,updateSavesGameStatus};})();`);
const marker = 'function request(action, args = {}, objects = null, waitForCompletion = false, activity = null) {';
assert(app.includes(marker));
app = app.replace(marker, marker + `
  if (window.thumbnailFixture) {
    const fixture=window.thumbnailFixture;
    if (action==='currentSavePreview' || action==='saveBackupPicture') {
      fixture.calls.push({action,id:args.id}); ++fixture.active;
      fixture.maximum=Math.max(fixture.maximum,fixture.active);
      return new Promise(resolve=>setTimeout(()=>{
        --fixture.active;
        const failure=action==='currentSavePreview' ? fixture.outcomes?.[args.id] : null;
        resolve({id:args.id,context:fixture.current.context,available:!failure,
          actualGamePreview:!failure,...(failure||{dataUrl:fixture.png})});
      },45));
    }
    if(action==='currentSavesList')return Promise.resolve(structuredClone(fixture.current));
    if(action==='saveBackupsList')return Promise.resolve(structuredClone(fixture.backups));
    if(action==='snapshot')return Promise.resolve(structuredClone(window.thumbnailProbe.state.snapshot));
    if(action==='nexusStatus')return Promise.resolve({connected:false,premium:false,edition:'local'});
    return Promise.resolve({found:false,available:false});
  }`);
const server = http.createServer((request, response) => {
  try {
    const name = decodeURIComponent(new URL(request.url, 'http://localhost').pathname).slice(1);
    const file = path.resolve(root, name);
    if (!(file.startsWith(path.join(root, 'ui') + path.sep) || file === path.join(root, 'catalog/catalog.json'))) {
      response.writeHead(404).end(); return;
    }
    const body = name === 'ui/app.js' ? app : fs.readFileSync(file);
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
  const report = { scope: 'Real Edge renderer, synthetic current/backup saves and actual 1x1 PNG replies; no native loader, game, account or public network.',
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
      await page.waitForFunction(() => window.thumbnailProbe && !window.thumbnailProbe.state.loading);
      await page.evaluate((mode) => {
        const ui = window.thumbnailProbe;
        document.querySelector('#modal').close();
        ui.state.noticeQueue = []; ui.state.setupPrompted = true; ui.state.preview = false;
        Object.assign(ui.state.snapshot, { platform: mode.platform, edition: 'local', accountContext: 'synthetic-' + mode.name,
          gameRunning: null, saveLocation: { resolved: true }, libraryBackupRecovery: { pending: false }, saveRecoveryRequired: false });
        Object.assign(ui.state.snapshot.settings, { autoScale: false, interfaceScale: 100, fontScale: mode.font,
          colorMode: mode.color, highContrast: !!mode.contrast, reducedMotion: true, seasonalThemes: false, gamePath: '' });
        const saves = Array.from({ length: 48 }, (_, i) => ({ id: (i + 1).toString(16).padStart(64, '0'),
          name: 'Deacon at Lost Lake ' + String(i + 1).padStart(2, '0') + '.sav', size: 5120,
            modifiedUtc: '2026-10-30T12:00:00Z', previewEligible: true, replaceEligible: true, deleteEligible: true, exportEligible: true }));
        saves[44].name = 'profile.sav'; saves[45].name = 'checkpoint.sav';
        const current = { context: 'synthetic-native-save-root', signature: 'same', status: 'ready', previewsSupported: true,
          saveEditorAvailable: true, saves };
        const backups = { available: true, location: { resolved: true }, settings: { automatic: false, keep: 48 },
          backups: saves.map((save, i) => ({ id: save.id, label: 'Before the ride ' + (i + 1),
            createdAt: 1793354400 - i * 3600, pictureAvailable: true, files: 12, bytes: 5120, reason: 'manual' })),
          recovery: { pending: false } };
        window.thumbnailFixture = { current, backups, calls: [], active: 0, maximum: 0, outcomes: {
          [saves[44].id]: { reasonCode: 'profile-data', retryable: false, reason: 'Profile data does not contain an in-game screenshot.' },
          [saves[45].id]: { reasonCode: 'no-picture', retryable: false, reason: 'No picture stored in this save.' } },
          png: 'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aGxkAAAAASUVORK5CYII=' };
        Object.assign(ui.state, { currentSaves: current, currentSaveSelection: null, currentSavePreviews: new Map(),
          saveBackups: backups, savePictures: new Map(), saveSelection: null, savesTab: 'current', busy: false,
          loading: false, nativeWindowHidden: false, profileSession: { active: false }, libraryMetaLoaded: true });
        ui.applyAppearance(); ui.navigate('saves');
      }, mode);

      const imageForCurrent = (index) => page.locator('[data-current-save-select]').nth(index).locator('.sg-card-picture img');
      await page.locator('[data-current-save-select]').first().scrollIntoViewIfNeeded();
      await imageForCurrent(0).waitFor();
      assert(await imageForCurrent(0).evaluate((image) => image.complete && image.naturalWidth === 1), 'The real PNG decodes in the browser');
      assert(await page.locator('[data-current-save-edit]').isDisabled(), 'An unconfigured game cannot silently enable save mutations');
      assert.match(await page.locator('#current-save-action-reason').textContent(), /Choose your Days Gone game folder/,
        'The missing setup reason appears directly beside the selected save actions');
      await page.evaluate(() => {
        const ui = window.thumbnailProbe;
        ui.state.snapshot.settings.gamePath = '/synthetic/DaysGone'; ui.state.snapshot.gameRunning = false; ui.updateSavesGameStatus();
      });
      assert(await page.locator('[data-current-save-edit]').isEnabled(), 'Confirmed setup and closed game enable the development editor fixture');
      assert(await page.locator('#current-save-action-reason').isHidden(), 'The adjacent warning clears when its cause is resolved');
      await page.waitForFunction(() => window.thumbnailFixture.active === 0);
      const initial = await page.evaluate(() => ({ calls: window.thumbnailFixture.calls.slice(),
        selected: window.thumbnailProbe.state.currentSaveSelection.id,
        lastId: window.thumbnailFixture.current.saves.at(-1).id }));
      assert(!initial.calls.some((request) => request.id === initial.lastId), 'A far-offscreen save is not requested');
      assert(initial.calls.length < 16, 'Opening Saves does not decode the whole collection');
      await page.locator('[data-current-save-select]').nth(12).scrollIntoViewIfNeeded();
      await imageForCurrent(12).waitFor();
      assert.equal(await page.evaluate(() => window.thumbnailProbe.state.currentSaveSelection.id), initial.selected,
        'Scrolling to load another picture never selects that save');
      await page.screenshot({ path: path.join(output, mode.name + '-current.png') });

      await page.waitForFunction(() => window.thumbnailFixture.active === 0);
      await page.evaluate(() => { window.thumbnailProbe.state.nativeWindowHidden = true; window.thumbnailProbe.syncWindowVisibility(); });
      const beforeHidden = await page.evaluate(() => window.thumbnailFixture.calls.length);
      await page.locator('[data-current-save-select]').nth(35).scrollIntoViewIfNeeded();
      await page.waitForTimeout(150);
      assert.equal(await page.evaluate(() => window.thumbnailFixture.calls.length), beforeHidden, 'Hidden native windows do not start new picture requests');
      await page.evaluate(() => { window.thumbnailProbe.state.nativeWindowHidden = false; window.thumbnailProbe.syncWindowVisibility(); });
      await imageForCurrent(35).waitFor();
      await page.locator('[data-current-save-select]').nth(44).scrollIntoViewIfNeeded();
      await page.locator('[data-current-save-select]').nth(44).getByText('Profile data · no in-game picture').waitFor();
      await page.locator('[data-current-save-select]').nth(45).scrollIntoViewIfNeeded();
      await page.locator('[data-current-save-select]').nth(45).getByText('No in-game picture stored').waitFor();
      assert.equal(await imageForCurrent(44).count(), 0, 'Profile data never borrows a save-slot picture');
      assert.equal(await imageForCurrent(45).count(), 0, 'A legitimate save without PNG uses a truthful no-picture label');
      await page.screenshot({ path: path.join(output, mode.name + '-no-picture-labels.png') });

      await page.waitForFunction(() => window.thumbnailFixture.active === 0);
      await page.locator('[data-saves-tab="backups"]').click();
      // Playwright scrolls the tab control into view before clicking; that may
      // legitimately reveal current cards while Current is still the active tab.
      const currentBeforeTab = await page.evaluate(() => window.thumbnailFixture.calls.filter((request) => request.action === 'currentSavePreview').length);
      await page.locator('[data-save-select]').first().scrollIntoViewIfNeeded();
      await page.locator('[data-save-select]').first().locator('.sg-card-picture img').waitFor();
      const backupSelected = await page.evaluate(() => window.thumbnailProbe.state.saveSelection.id);
      await page.locator('[data-save-select]').nth(14).scrollIntoViewIfNeeded();
      await page.locator('[data-save-select]').nth(14).locator('.sg-card-picture img').waitFor();
      await page.waitForFunction(() => window.thumbnailFixture.active === 0);
      const result = await page.evaluate(() => ({ selected: window.thumbnailProbe.state.saveSelection.id,
        currentCount: window.thumbnailFixture.calls.filter((request) => request.action === 'currentSavePreview').length,
        maximumNativePictures: window.thumbnailFixture.maximum, calls: window.thumbnailFixture.calls,
        bodyOverflow: document.body.scrollWidth > document.documentElement.clientWidth + 1,
        retainedBytes: [...window.thumbnailProbe.state.currentSavePreviews.values()].reduce((sum, picture) => sum + (picture.url?.length || 0), 0) +
          [...window.thumbnailProbe.state.savePictures.values()].reduce((sum, url) => sum + url.length, 0) }));
      assert.equal(result.selected, backupSelected, 'Backup pictures load without changing selection');
      assert.equal(result.currentCount, currentBeforeTab, 'Current preview requests pause in the Backups tab');
      assert.equal(result.maximumNativePictures, 1, 'The two real galleries share one native image request at a time');
      assert.equal(result.bodyOverflow, false, 'Large-font and narrow layouts do not overflow horizontally');
      assert(result.retainedBytes <= 16 * 1024 * 1024);
      assert.deepEqual(errors, [], mode.name + ': no browser errors');
      await page.screenshot({ path: path.join(output, mode.name + '-backups.png') });
      report.cases.push({ ...mode, ...result, calls: result.calls.length, errors });
      await context.close();
    }
    report.passed = true;
    fs.writeFileSync(path.join(output, 'verification.json'), JSON.stringify(report, null, 2) + '\n');
    console.log(JSON.stringify({ passed: true, cases: report.cases.length, output }));
  } finally { await browser.close(); server.close(); }
}
main().catch((error) => { console.error(error); server.close(); process.exitCode = 1; });
