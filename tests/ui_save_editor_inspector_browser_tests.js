'use strict';

// Optional visual acceptance: shared production UI with synthetic save metadata.
// No native loader, real saves, decoder, credentials or external service is used.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const http = require('node:http');
const crypto = require('node:crypto');
const { chromium } = require('playwright');
const root = path.resolve(__dirname, '..');
const output = path.join(root, 'build-artifacts/2026-10-30-next-update/save-editor-inspector-browser');
fs.mkdirSync(output, { recursive: true });
const original = fs.readFileSync(path.join(root, 'ui/app.js'), 'utf8');
let app = original.replace(/\}\)\(\);\s*$/,
  'window.editorProbe={state,navigate,render,applyAppearance,openSaveEditor,updateSavesGameStatus,currentSaveChangeReason,currentSaveGuard};})();');
const marker = 'function request(action, args = {}, objects = null, waitForCompletion = false, activity = null) {';
assert(app.includes(marker));
app = app.replace(marker, marker + `
  if (window.editorFixture) {
    const f=window.editorFixture;f.calls.push({action,...args});
    if(action==='snapshot')return Promise.resolve(structuredClone(window.editorProbe.state.snapshot));
    if(action==='currentSavesList')return Promise.resolve(structuredClone(f.current));
    if(action==='saveBackupsList')return Promise.resolve({available:true,backups:[],recovery:{pending:false}});
    if(action==='saveEditorOpen'){
      const session=structuredClone(f.session);
      if(args.inspectOnly===true){session.id+='-inspection';session.inspectionOnly=true;session.readOnly=true;session.dirty=false;}
      return Promise.resolve({supported:true,inspectionAvailable:true,session});
    }
    if(action==='saveEditorSet'){
      const field=f.session.fields.find(v=>v.id===args.field);if(!field)return Promise.reject(new Error('Read-only field'));
      field.value=args.value;++f.session.revision;f.session.dirty=true;
      return Promise.resolve({session:structuredClone(f.session)});
    }
    if(action==='saveEditorReview')return Promise.resolve({id:f.session.sourceId,token:'synthetic-review',operation:'edit-current-save',
      safetyBackupRequired:true,targetName:f.session.saveName,beforeBytes:2048,afterBytes:2048,
      advancedChangeCount:f.session.fields.filter(v=>v.advanced&&v.value!==v.originalValue).length,
      advancedWarning:'The advanced values are schema-checked. Their in-game effect has not been verified.',
      changes:f.session.fields.filter(v=>v.value!==v.originalValue).map(v=>({label:v.label,before:v.originalValue,after:v.value,advanced:!!v.advanced}))});
    if(action==='saveEditorClose')return Promise.resolve({closed:true,session:null});
    return Promise.resolve({available:false,found:false});
  }`);
const server = http.createServer((request, response) => {
  try {
    const name = decodeURIComponent(new URL(request.url, 'http://localhost').pathname).slice(1), file = path.resolve(root, name);
    if (!(file.startsWith(path.join(root, 'ui') + path.sep) || file === path.join(root, 'catalog/catalog.json'))) return response.writeHead(404).end();
    const body = name === 'ui/app.js' ? app : fs.readFileSync(file);
    const mime = { '.html': 'text/html', '.js': 'application/javascript', '.css': 'text/css', '.json': 'application/json',
      '.png': 'image/png', '.svg': 'image/svg+xml', '.webp': 'image/webp' }[path.extname(file)] || 'application/octet-stream';
    response.writeHead(200, { 'Content-Type': mime, 'Cache-Control': 'no-store' }).end(body);
  } catch { response.writeHead(404).end(); }
});
async function layout(page, label) {
  const result = await page.evaluate(() => {
    const modal = document.querySelector('#modal'), content = document.querySelector('#save-editor-content'), footer = document.querySelector('#save-editor-close'), r = footer.getBoundingClientRect();
    return { bodyOverflow: document.body.scrollWidth > document.documentElement.clientWidth + 1,
      modalOverflow: modal.scrollWidth > modal.clientWidth + 1, contentOverflow: content.scrollWidth > content.clientWidth + 1,
      footerVisible: r.top >= 0 && r.bottom <= innerHeight + 1,
      renderedFields: document.querySelectorAll('.save-editor-field').length,
      colorMode: document.documentElement.dataset.colorMode, font: getComputedStyle(document.documentElement).fontSize };
  });
  assert(!result.bodyOverflow && !result.modalOverflow && !result.contentOverflow && result.footerVisible, label + ': layout ' + JSON.stringify(result));
  assert(result.renderedFields <= 24); return result;
}
async function main() {
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  const origin = `http://127.0.0.1:${server.address().port}`;
  const browser = await chromium.launch({ headless: true, chromiumSandbox: true,
    ...(process.env.TOOLKIT_TEST_BROWSER ? { executablePath: process.env.TOOLKIT_TEST_BROWSER } : { channel: 'msedge' }) });
  const report = { appSha256: crypto.createHash('sha256').update(original).digest('hex'),
    cssSha256: crypto.createHash('sha256').update(fs.readFileSync(path.join(root, 'ui/style.css'))).digest('hex'),
    scope: 'Real isolated Edge/CSS. Synthetic Windows/Linux replies only. No real saves or external network.', cases: [] };
  try {
    for (const mode of [
      { name: 'windows-dark', platform: 'windows', width: 1440, height: 960, font: 100, color: 'dark' },
      { name: 'linux-light-large', platform: 'linux', width: 900, height: 760, font: 150, color: 'light' },
      { name: 'linux-small-contrast', platform: 'linux', width: 640, height: 560, font: 150, color: 'dark', contrast: true }
    ]) {
      const context = await browser.newContext({ viewport: { width: mode.width, height: mode.height }, reducedMotion: 'reduce' }), external = [];
      await context.route('**/*', (route) => route.request().url().startsWith(origin + '/') ? route.continue() : (external.push(route.request().url()), route.abort()));
      const page = await context.newPage(), errors = []; page.on('pageerror', (error) => errors.push(String(error)));
      await page.goto(origin + '/ui/index.html'); await page.waitForFunction(() => window.editorProbe && !window.editorProbe.state.loading);
      await page.evaluate((mode) => {
        const ui = window.editorProbe, id = 'a'.repeat(64); document.querySelector('#modal').close();
        Object.assign(ui.state.snapshot, { platform: mode.platform, edition: 'local', accountContext: 'synthetic-editor', gameRunning: false,
          saveRecoveryRequired: false, libraryBackupRecovery: { pending: false } });
        Object.assign(ui.state.snapshot.settings, { gamePath: '/Synthetic/DaysGone', autoScale: false, interfaceScale: 100, fontScale: mode.font,
          colorMode: mode.color, highContrast: !!mode.contrast, reducedMotion: true, seasonalThemes: false });
        const current = { saveEditorAvailable: true, context: 'synthetic-current', signature: 'synthetic-stable', previewsSupported: false, status: 'ready', saves: [{ id,
          name: 'Deacon_At_The_Camp_' + 'LongName_'.repeat(7) + '.sav', size: 2048, replaceEligible: true, exportEligible: true, deleteEligible: true }] };
        window.editorFixture = { current, calls: [], session: { id: 'synthetic-session', revision: 1, sourceId: id, saveName: current.saves[0].name, dirty: false,
          fields: [{ id: 'player-skill-points', label: 'Unspent skill points', group: 'Player progression', editor: 'integer', type: 'IntProperty',
            min: 0, max: 2147483647, value: 2, originalValue: 2 },
          { id: 'synthetic-use-effect-level-0', label: 'Health / UseEffectLevels[0]', group: 'Player attributes', editor: 'number', type: 'FloatProperty',
            min: -3.4e38, max: 3.4e38, value: 1.5, originalValue: 1.5, advanced: true, gameplayVerified: false },
          { id: 'synthetic-can-be-damaged', label: 'bCanBeDamaged', group: 'Controller flags', editor: 'boolean', type: 'BoolProperty',
            value: true, originalValue: true, advanced: true, gameplayVerified: false }],
          protectedFields: Array.from({ length: 77 }, (_, n) => ({ id: 'recorded-' + n, label: n === 46 ? 'Health infusion entries' : 'Recorded data ' + n,
            group: n < 40 ? 'Player attributes' : 'Saved data', type: n === 46 ? 'StrProperty' : 'ArrayProperty',
            value: n === 46 ? '<img src=x onerror="window.inspectorInjected=true"><a href="https://example.invalid/">local text</a>' : '12 entries',
            reason: 'Linked native state is preserved. This value has not been approved for editing.' })) } };
        Object.assign(ui.state, { preview: false, loading: false, busy: false, setupPrompted: true, noticeQueue: [], nativeDropActivity: null,
          currentSaves: current, currentSaveSelection: { context: current.context, id }, profileSession: { active: false }, saveEditor: null });
        ui.applyAppearance(); ui.navigate('saves');
      }, mode);
      await page.waitForFunction(() => !window.editorProbe.state.busy && !window.editorProbe.state.loading);
      await page.evaluate(() => { window.editorProbe.state.snapshot.gameRunning = true; window.editorProbe.updateSavesGameStatus(); });
      assert(await page.locator('[data-current-save-edit]').isDisabled());
      assert(await page.locator('[data-current-save-inspect]').isEnabled());
      await page.locator('[data-current-save-inspect]').click();
      assert((await page.locator('.modal-head').textContent()).includes('Inspect save'));
      assert(await page.locator('#save-editor-review').isHidden());
      assert.equal(await page.locator('[data-save-editor-field]').count(), 0);
      assert.equal(await page.evaluate(() => window.editorProbe.openSaveEditor('a'.repeat(64), false)), false);
      await page.locator('#save-editor-close').click();
      await page.evaluate(() => { window.editorProbe.state.snapshot.gameRunning = false; window.editorProbe.updateSavesGameStatus(); });
      const opening = await page.evaluate(async () => {
        const ui = window.editorProbe, id = 'a'.repeat(64), opened = await ui.openSaveEditor(id);
        return { opened, reason: ui.currentSaveChangeReason(ui.currentSaveGuard(id)), calls: window.editorFixture.calls,
          modal: document.querySelector('#modal-content').textContent.slice(0, 1200) };
      });
      assert(opening.opened, JSON.stringify(opening));
      await page.locator('#save-editor-review').waitFor();
      const editable = await layout(page, mode.name + ' editable');
      assert.equal(await page.locator('[data-save-editor-field]').count(), 1);
      await page.screenshot({ path: path.join(output, mode.name + '-editable.png') });
      await page.locator('[data-save-editor-field]').fill('4');
      await page.waitForFunction(() => window.editorProbe.state.saveEditor?.session.dirty && !window.editorProbe.state.saveEditor.pending);
      assert(!await page.locator('#save-editor-advanced').isChecked(), 'Advanced defaults off');
      await page.locator('#save-editor-advanced').check();
      assert.equal(await page.locator('[data-save-editor-field]').count(), 3);
      await page.locator('[data-save-editor-field="synthetic-can-be-damaged"]').uncheck();
      await page.waitForFunction(() => !window.editorProbe.state.saveEditor.pending && window.editorProbe.state.saveEditor.session.fields.find(v => v.id === 'synthetic-can-be-damaged').value === false);
      assert(await page.evaluate(() => window.editorFixture.calls.some(v => v.action === 'saveEditorSet' && v.advanced === true && v.value === false)));
      await page.locator('#save-editor-advanced').uncheck();
      assert(await page.locator('#save-editor-review').isDisabled(), 'Advanced dirty draft cannot be reviewed with consent off');
      await page.locator('#save-editor-advanced').check();
      await layout(page, mode.name + ' advanced'); await page.screenshot({ path: path.join(output, mode.name + '-advanced.png') });
      await page.locator('#save-editor-filter-read-only').click();
      const readonly = await layout(page, mode.name + ' readonly');
      assert.equal(await page.locator('[data-save-editor-field]').count(), 0);
      const calls = await page.evaluate(() => window.editorFixture.calls.length);
      await page.locator('#save-editor-search').fill('infusion entries');
      assert.equal(await page.locator('.save-editor-field').count(), 1);
      assert((await page.locator('#save-editor-inspector').textContent()).includes('Health infusion entries'));
      assert.equal(await page.locator('#save-editor-inspector img, #save-editor-inspector a').count(), 0);
      assert((await page.locator('.save-editor-recorded-value').textContent()).startsWith('<img src=x onerror='));
      assert.equal(await page.evaluate(() => window.inspectorInjected), undefined);
      assert.equal(await page.evaluate(() => window.editorFixture.calls.length), calls, 'Search stays local');
      await page.locator('.save-editor-readonly').first().scrollIntoViewIfNeeded();
      await page.screenshot({ path: path.join(output, mode.name + '-search.png') });
      await page.locator('#save-editor-search').press('Escape'); assert.equal(await page.locator('#save-editor-search').inputValue(), '');
      assert(await page.locator('#modal').isVisible(), 'First Escape clears search without closing draft');
      await page.locator('#save-editor-page-next').click(); assert((await page.locator('.save-editor-pagination').textContent()).includes('25–48'));
      await layout(page, mode.name + ' paged'); await page.screenshot({ path: path.join(output, mode.name + '-paged.png') });
      await page.locator('#save-editor-review').click();
      assert((await page.locator('#save-editor-content').textContent()).includes('Advanced changes included'));
      assert(await page.evaluate(() => window.editorFixture.calls.filter(v => v.action === 'saveEditorReview').at(-1).advanced === true));
      await page.locator('#save-editor-back').click();
      assert.equal(await page.locator('#save-editor-filter-read-only').getAttribute('aria-pressed'), 'true');
      assert((await page.locator('.save-editor-pagination').textContent()).includes('25–48'), 'Returning from review preserves view');
      await page.locator('#save-editor-close').click(); await page.locator('#save-editor-discard').click();
      assert.equal(await page.evaluate(() => window.editorProbe.state.saveEditor), null);
      const mutationCalls = await page.evaluate(() => window.editorFixture.calls.filter(v => ['saveEditorSet', 'saveEditorReview', 'saveEditorApply'].includes(v.action)).length);
      await page.evaluate(() => {
        const f = window.editorFixture, ui = window.editorProbe;
        f.session = { ...f.session, fields: [], readOnly: true, dirty: false, revision: 1 };
        f.current.saves[0].replaceEligible = false; ui.state.currentSaves = f.current; ui.render();
      });
      assert.equal(await page.locator('[data-current-save-edit]').count(), 0);
      assert((await page.locator('[data-current-save-inspect]').textContent()).includes('Inspect save'));
      await page.locator('[data-current-save-inspect]').click();
      assert((await page.locator('.modal-head').textContent()).includes('Inspect save'));
      assert(await page.locator('#save-editor-review').isHidden());
      assert.equal(await page.locator('[data-save-editor-field]').count(), 0);
      assert.equal(await page.locator('#save-editor-close').textContent(), 'Close inspection');
      await page.locator('#save-editor-search').fill('recorded data 1');
      await page.locator('.save-editor-readonly').first().scrollIntoViewIfNeeded();
      await page.screenshot({ path: path.join(output, mode.name + '-read-only-inspection.png') });
      assert.equal(await page.evaluate(() => window.editorFixture.calls.filter(v => ['saveEditorSet', 'saveEditorReview', 'saveEditorApply'].includes(v.action)).length), mutationCalls);
      await page.locator('#save-editor-close').click();
      assert(!errors.length, errors.join('\n')); assert(!external.length, external.join('\n'));
      assert(!await page.evaluate(() => window.editorFixture.calls.some((call) => call.action === 'saveEditorApply')));
      report.cases.push({ ...mode, editable, readonly, rendererErrors: errors, externalRequests: external.length, result: 'PASS' });
      await context.close();
    }
    fs.writeFileSync(path.join(output, 'verification.json'), JSON.stringify(report, null, 2) + '\n');
    console.log('Save editor inspector browser acceptance passed (3 layouts).');
  } finally { await browser.close(); await new Promise((resolve) => server.close(resolve)); }
}
main().catch((error) => { console.error(error); server.close(); process.exitCode = 1; });
