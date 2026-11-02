'use strict';

// Optional visual acceptance: production UI/CSS and the actual bundled catalogue,
// synthetic native replies, isolated browser profile and no external network.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const http = require('node:http');
const crypto = require('node:crypto');
const { chromium } = require('playwright');
const root = path.resolve(__dirname, '..');
const output = path.join(root, 'build-artifacts/2026-10-30-next-update/manual-catalog-browser');
fs.mkdirSync(output, { recursive: true });
const original = fs.readFileSync(path.join(root, 'ui/app.js'), 'utf8');
const css = fs.readFileSync(path.join(root, 'ui/style.css'));
const catalogue = fs.readFileSync(path.join(root, 'catalog/catalog.json'));
const hash = (body) => crypto.createHash('sha256').update(body).digest('hex');
let app = original.replace(/\}\)\(\);\s*$/, 'window.catalogProbe={state,navigate,render,applyAppearance,showModDetails,closeModDetails};})();');
assert.notEqual(app, original, 'Inject only private test exports into the served copy');
const marker = 'function request(action, args = {}, objects = null, waitForCompletion = false, activity = null) {';
assert(app.includes(marker));
app = app.replace(marker, marker + `
  if (window.catalogFixture) {
    window.catalogFixture.calls.push({action,...args});
    if(action==='snapshot')return Promise.resolve(structuredClone(window.catalogProbe.state.snapshot));
    if(action==='libraryMetaList')return Promise.resolve({entries:{},history:[]});
    if(action==='openNexusFile')return Promise.resolve({opened:true});
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
    const mime = { '.html': 'text/html', '.js': 'application/javascript', '.json': 'application/json', '.css': 'text/css',
      '.png': 'image/png', '.svg': 'image/svg+xml', '.webp': 'image/webp' }[path.extname(file)] || 'application/octet-stream';
    response.writeHead(200, { 'Content-Type': mime, 'Cache-Control': 'no-store' }).end(body);
  } catch { response.writeHead(404).end(); }
});

async function layout(page, label) {
  const result = await page.evaluate(() => {
    const panel = document.querySelector('#mod-details-panel');
    return {
      bodyOverflow: document.body.scrollWidth > document.documentElement.clientWidth + 1,
      contentOverflow: document.querySelector('#main-content').scrollWidth > document.querySelector('#main-content').clientWidth + 1,
      panelOverflow: !panel.hidden && panel.scrollWidth > panel.clientWidth + 1,
      colorMode: document.documentElement.dataset.colorMode,
      highContrast: document.documentElement.classList.contains('high-contrast'),
      textSize: getComputedStyle(document.documentElement).fontSize,
      recoveryVisible: !document.querySelector('#interface-recovery').hidden
    };
  });
  assert(!result.bodyOverflow && !result.contentOverflow && !result.panelOverflow, label + ': no horizontal overflow');
  assert(!result.recoveryVisible, label + ': no interface fault');
  return result;
}

async function usable(control, label) {
  await control.scrollIntoViewIfNeeded();
  assert(await control.isVisible() && await control.isEnabled(), label + ': visible and enabled');
  const result = await control.evaluate((element) => {
    const box = element.getBoundingClientRect();
    const x = box.left + box.width / 2, y = box.top + box.height / 2;
    const hit = document.elementFromPoint(x, y);
    return { left: box.left, right: box.right, top: box.top, bottom: box.bottom,
      width: box.width, height: box.height, viewport: innerWidth,
      hit: hit === element || element.contains(hit) };
  });
  assert(result.width >= 32 && result.height >= 28 && result.left >= -1 && result.right <= result.viewport + 1 && result.hit,
    label + ': unclipped clickable target ' + JSON.stringify(result));
  return result;
}

async function main() {
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  const origin = `http://127.0.0.1:${server.address().port}`;
  const browser = await chromium.launch({ headless: true, chromiumSandbox: true,
    ...(process.env.TOOLKIT_TEST_BROWSER ? { executablePath: process.env.TOOLKIT_TEST_BROWSER } : { channel: 'msedge' }) });
  const report = { scope: 'Real Edge renderer with actual80-entry catalogue/shared UI/CSS and synthetic Windows/Linux native replies. No real native app, game, user data, credentials or external network.',
    appSha256: hash(original), cssSha256: hash(css), catalogueSha256: hash(catalogue), cases: [] };
  try {
    for (const mode of [
      { name: 'windows-dark', platform: 'windows', width: 1440, height: 960, font: 100, color: 'dark' },
      { name: 'linux-light-large', platform: 'linux', width: 900, height: 760, font: 150, color: 'light' },
      { name: 'linux-small-contrast', platform: 'linux', width: 640, height: 560, font: 150, color: 'dark', contrast: true }
    ]) {
      const context = await browser.newContext({ viewport: { width: mode.width, height: mode.height }, reducedMotion: 'reduce' });
      const external = [];
      await context.route('**/*', (route) => {
        if (route.request().url().startsWith(origin + '/')) return route.continue();
        external.push(route.request().url()); return route.abort();
      });
      const page = await context.newPage(), errors = [];
      page.on('pageerror', (error) => errors.push(String(error)));
      page.on('crash', () => errors.push('Browser renderer crashed'));
      await page.goto(origin + '/ui/index.html');
      await page.waitForFunction(() => window.catalogProbe && !window.catalogProbe.state.loading);
      await page.evaluate((mode) => {
        const ui = window.catalogProbe;
        document.querySelector('#modal').close();
        window.catalogFixture = { calls: [] };
        Object.assign(ui.state.snapshot, { platform: mode.platform, edition: 'local',
          accountContext: 'synthetic-manual-catalog-' + mode.name, gameRunning: false,
          recoveryRequired: false, saveRecoveryRequired: false, libraryBackupRecovery: { pending: false },
          mods: ui.state.snapshot.mods.map((mod) => ({ ...mod, enabled: false, imported: false,
            cached: false, heldLocally: false, owned: false, status: 'not-imported',
            conflictsWithInstalled: [], canRemoveLibrary: false })) });
        Object.assign(ui.state.snapshot.settings, { gamePath: mode.platform === 'linux' ? '/synthetic/DaysGone' : 'C:/Synthetic/DaysGone',
          autoScale: false, interfaceScale: 100, fontScale: mode.font, colorMode: mode.color,
          useSystemTheme: false, highContrast: !!mode.contrast, reducedMotion: true, seasonalThemes: false });
        Object.assign(ui.state, { loading: false, preview: false, busy: false, nativeWindowHidden: false,
          profileSession: { active: false }, setupPrompted: true, noticeQueue: [], libraryMetaLoaded: true,
          filter: 'available', category: 'all', search: '', detailId: null, modLimit: 100 });
        ui.applyAppearance(); ui.navigate('mods');
      }, mode);
      assert.equal(await page.evaluate(() => window.catalogProbe.state.snapshot.mods.length), 80);
      assert.match(await page.locator('[data-filter="available"]').textContent(), /80/);
      await page.locator('#mod-search').fill('Bleed Out');
      const bleed = page.locator('[data-mod-row="bleed-out"]');
      await bleed.waitFor();
      assert.match(await bleed.textContent(), /Manual setup/);
      assert.equal(await bleed.locator('[data-toggle], [data-download], [data-remove-mod]').count(), 0);
      const setup = bleed.getByRole('button', { name: 'Manual setup for Bleed Out', exact: true });
      const cardTarget = await usable(setup, mode.name + ': View setup');
      const cardLayout = await layout(page, mode.name + ': available card');
      assert.equal(cardLayout.colorMode, mode.color);
      assert.equal(cardLayout.highContrast, !!mode.contrast);
      await page.screenshot({ path: path.join(output, mode.name + '-bleed-card.png') });
      await setup.click();
      const panel = page.locator('#mod-details-panel');
      await panel.waitFor({ state: 'visible' });
      assert.match(await panel.textContent(), /PAK conflict scans, profiles and Find the bad mod do not inspect/);
      assert.match(await panel.textContent(), /Installed ASI and DLL plugins stay active across PAK profiles/);
      assert.equal(await panel.locator('#detail-import, #detail-toggle, #detail-assets, #detail-action-reason').count(), 0);
      assert(!((await panel.textContent()).includes('Manual ASI setup is required.')),
        'The dedicated setup banner is not repeated by generic PAK action messages');
      const closeTarget = await usable(page.locator('#mod-panel-close'), mode.name + ': close details');
      const detailLayout = await layout(page, mode.name + ': manual details');
      await page.screenshot({ path: path.join(output, mode.name + '-bleed-details.png') });
      const nexus = page.locator('#detail-nexus');
      const nexusTarget = await usable(nexus, mode.name + ': Setup on Nexus');
      assert.equal(await nexus.innerText(), 'Setup on Nexus');
      await nexus.click();
      await page.waitForFunction(() => window.catalogFixture.calls.some((call) => call.action === 'openNexusFile'));
      const opened = await page.evaluate(() => window.catalogFixture.calls.find((call) => call.action === 'openNexusFile'));
      assert.equal(opened.modId, 994); assert.equal(opened.fileId, 3302);
      await usable(page.locator('#detail-asi-guide'), mode.name + ': ASI guide');
      await page.screenshot({ path: path.join(output, mode.name + '-bleed-setup.png') });
      await page.locator('#detail-asi-guide').click();
      await page.getByRole('heading', { name: 'ASI loaders: when a mod needs one', exact: true }).waitFor();
      assert.equal(await page.evaluate(() => window.catalogProbe.state.article), 'modding-asi');
      await page.evaluate(() => { const ui = window.catalogProbe; ui.state.search = 'Headshot Only Freakers'; ui.navigate('mods'); });
      const headshot = page.locator('[data-mod-row="headshot-only-freakers"]');
      await headshot.waitFor();
      await headshot.getByRole('button', { name: 'Details for Headshot Only Freakers', exact: true }).click();
      assert.match(await panel.textContent(), /optional Horde support|optional ASI|Horde support/);
      await usable(page.locator('#mod-panel-close'), mode.name + ': Headshot close');
      const headshotLayout = await layout(page, mode.name + ': Headshot details');
      await page.screenshot({ path: path.join(output, mode.name + '-headshot-details.png') });
      const warning = panel.getByText('An installed horde ASI stays active across PAK profiles. Disabling this PAK in the toolkit does not switch off the separate plugin.', { exact: true });
      await warning.scrollIntoViewIfNeeded();
      assert(await warning.isVisible());
      await layout(page, mode.name + ': Headshot plugin notes');
      await page.screenshot({ path: path.join(output, mode.name + '-headshot-plugin-notes.png') });
      const importTarget = await usable(page.locator('#detail-import'), mode.name + ': Headshot PAK import');
      assert.equal(await page.locator('#detail-import').innerText(), 'Import mod');
      await usable(page.locator('#mod-panel-return'), mode.name + ': Back to library');
      await page.locator('#mod-panel-return').click();
      assert(await panel.isHidden());
      const calls = await page.evaluate(() => window.catalogFixture.calls);
      assert(!calls.some((call) => /^(?:enable|disable|import|nexus)/.test(call.action)), 'No writes or Nexus account API were requested');
      assert.deepEqual(external, [], 'No public network requests');
      assert.deepEqual(errors, [], 'No renderer errors');
      report.cases.push({ ...mode, cardLayout, detailLayout, headshotLayout, cardTarget, closeTarget,
        nexusTarget, importTarget, nativeActions: calls.map((call) => call.action), externalRequests: external, errors });
      await context.close();
    }
    report.passed = true;
    fs.writeFileSync(path.join(output, 'verification.json'), JSON.stringify(report, null, 2) + '\n');
    console.log(JSON.stringify({ passed: true, cases: report.cases.length, output }));
  } finally { await browser.close(); server.close(); }
}
main().catch((error) => { console.error(error); server.close(); process.exitCode = 1; });
