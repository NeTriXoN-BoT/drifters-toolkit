'use strict';

// Optional focused visual acceptance using the real UI and synthetic replies.
// No native loader, game files, account, saves or network service is connected.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const http = require('node:http');
const crypto = require('node:crypto');
const { chromium } = require('playwright');
const root = path.resolve(__dirname, '..');
const output = fs.mkdtempSync(path.join(os.tmpdir(), 'drifter-107-save-layout-'));
const original = fs.readFileSync(path.join(root, 'ui/app.js'), 'utf8');
let app = original.replace(/\}\)\(\);\s*$/,
  'window.lifecycleProbe = {state,navigate,render,applyAppearance,reviewSaveDeletion,setSavesTab,openCombinationReview};})();');
const requestMarker = 'function request(action, args = {}, objects = null, waitForCompletion = false, activity = null) {';
assert(app.includes(requestMarker));
app = app.replace(requestMarker, requestMarker + `
  if (window.lifecycleReplies) {
    if (window.lifecycleFailDelete && action === 'currentSaveDelete') return Promise.reject(new Error('The selected save changed on disk. Select it again before retrying.'));
    return Promise.resolve(structuredClone(window.lifecycleReplies[action] || {}));
  }`);
const server = http.createServer((request, response) => {
  try {
    const relative = decodeURIComponent(new URL(request.url, 'http://localhost').pathname).slice(1), file = path.resolve(root, relative);
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
  const report = { scope: 'Real shared UI, isolated Edge, synthetic save and combination replies only.',
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
      await page.waitForFunction(() => window.lifecycleProbe && !window.lifecycleProbe.state.loading);
      await page.evaluate((mode) => {
        const ui = window.lifecycleProbe, id = 'a'.repeat(64), backupId = 'b'.repeat(64);
        document.querySelector('#modal').close();
        ui.state.setupPrompted = true; ui.state.noticeQueue = []; ui.state.preview = false;
        Object.assign(ui.state.snapshot, { platform: mode.platform, edition: 'local', accountContext: 'synthetic-layout',
          gameRunning: false, saveLocation: { resolved: true }, diagnostics: [] });
        Object.assign(ui.state.snapshot.settings, { autoScale: false, interfaceScale: 100, fontScale: mode.font,
          colorMode: mode.color, highContrast: !!mode.contrast, reducedMotion: true, seasonalThemes: false });
        const current = { context: 'synthetic-save-account', signature: 'stable', status: 'ready', saves: [
          { id, name: 'Deacon_At_The_Forest_Camp_' + 'LongSaveName_'.repeat(6) + '.sav', size: 2048,
            modifiedUtc: '2026-10-28T10:30:00Z', deleteEligible: true, exportEligible: true, replaceEligible: true }
        ] };
        const backups = { available: true, location: { resolved: true }, settings: { automatic: false, keep: 5 },
          backups: [{ id: backupId, label: 'Before trying the new mod combination', createdAt: Date.now(),
            files: 12, bytes: 4096, reason: 'manual', notes: 'A useful safety copy.', pinned: true }], recovery: { pending: false } };
        Object.assign(ui.state, { currentSaves: current, currentSaveSelection: { context: current.context, id },
          saveBackups: backups, saveSelection: { context: 'synthetic-layout', id: backupId },
          savesTab: 'current', profileSession: { active: false }, libraryMetaLoaded: true });
        const editor = (revision = 0, points = 2) => ({ id: 'synthetic-editor', revision, sourceId: id, saveName: current.saves[0].name, dirty: revision > 0,
          fields: [{ id: 'player-skill-points', label: 'Unspent skill points', group: 'Progression', type: 'IntProperty', editor: 'integer', min: 0, max: 2147483647, value: points }],
          protectedFields: [{ label: 'Player level', value: 12, reason: 'Linked progression state is protected. ' + 'ReadOnlyState_'.repeat(8) }] });
        const replacement = { token: 'synthetic-replacement', id, operation: 'import-current-save', targetName: current.saves[0].name,
          sourceName: 'A separately imported save file.sav', beforeBytes: 2048, afterBytes: 2048, safetyBackupRequired: true, warnings: [] };
        window.lifecycleReplies = { currentSavesList: current, saveBackupsList: backups,
          currentSaveExportBrowse: { exported: true }, currentSaveImportBrowse: replacement, currentSaveReplace: { replaced: true, safetyBackupId: backupId },
          saveEditorOpen: { supported: true, session: editor() }, saveEditorSet: { session: editor(1, 4) },
          saveEditorReview: { ...replacement, operation: 'edit-current-save', token: 'synthetic-editor-review', changes: [{ label: 'Unspent skill points', before: 2, after: 4 }] },
          saveEditorApply: { replaced: true, safetyBackupId: backupId }, saveEditorClose: { closed: true, session: null },
          currentSaveDeleteReview: { token: 'synthetic-current-token', id, name: current.saves[0].name,
            warnings: ['A safety backup is required before deleting this selected file.'] },
          backupDeleteReview: { token: 'synthetic-backup-token', id: backupId, warnings: ['This backup is pinned and linked to a profile.'] },
          saveImportBrowse: { imported: true, id: backupId, files: 12, bytes: 4096 } };
        ui.applyAppearance(); ui.navigate('saves');
      }, mode);
      const layout = async (footer) => page.evaluate((footer) => {
        const dialog = document.querySelector('#modal'), control = footer && document.querySelector(footer), rect = control?.getBoundingClientRect();
        return { bodyOverflow: document.body.scrollWidth > document.documentElement.clientWidth + 1,
          dialogOverflow: dialog.open && dialog.scrollWidth > dialog.clientWidth + 1,
          footerVisible: !control || rect.top >= 0 && rect.bottom <= innerHeight + 1 };
      }, footer);
      let checks = await layout(); assert.equal(checks.bodyOverflow, false, mode.name + ': current saves wrap');
      assert(await page.locator('[data-current-save-edit]').isDisabled(), 'Release capability defaults to disabled');
      assert((await page.locator('#save-editor-availability').textContent()).includes('disabled in this version'));
      await page.locator('#save-editor-availability').scrollIntoViewIfNeeded();
      await page.screenshot({ path: path.join(output, mode.name + '-current-saves.png') });
      // Only the synthetic future-editor fixture opts in; production native inventory remains false.
      await page.evaluate(() => {
        window.lifecycleProbe.state.currentSaves.saveEditorAvailable = true;
        window.lifecycleReplies.currentSavesList.saveEditorAvailable = true;
        window.lifecycleProbe.render();
      });
      await page.locator('[data-current-save-edit]').click();
      await page.locator('#save-editor-review').waitFor();
      checks = await layout('#save-editor-review'); assert(!checks.dialogOverflow && checks.footerVisible);
      await page.locator('.save-editor-protected summary').click();
      assert((await page.locator('#save-editor-content').textContent()).includes('Player level'));
      await page.locator('[data-save-editor-field]').fill('4');
      await page.waitForFunction(() => window.lifecycleProbe.state.saveEditor?.session.revision === 1 && !window.lifecycleProbe.state.saveEditor.pending);
      assert.equal(await page.locator('[data-save-editor-field]').inputValue(), '4');
      assert.equal(await page.locator('#save-editor-revert').isVisible(), false, 'No stale pending-value button after native validation');
      await page.screenshot({ path: path.join(output, mode.name + '-save-editor.png') });
      await page.locator('#save-editor-review').click();
      await page.locator('#save-editor-apply').waitFor();
      checks = await layout('#save-editor-apply'); assert(!checks.dialogOverflow && checks.footerVisible);
      assert((await page.locator('#modal-content').textContent()).includes('Other save slots'));
      await page.screenshot({ path: path.join(output, mode.name + '-save-edit-review.png') });
      await page.locator('#save-editor-back').click();
      await page.mouse.click(1, 1); await page.locator('#save-editor-discard').waitFor();
      assert(await page.evaluate(() => window.lifecycleProbe.state.saveEditor?.session.dirty), 'Backdrop close preserves the changed draft for review');
      await page.mouse.click(1, 1); await page.locator('#save-editor-review').waitFor();
      assert.equal(await page.locator('[data-save-editor-field]').inputValue(), '4', 'Dismissing discard keeps the edited value');
      await page.locator('#save-editor-close').click();
      await page.locator('#save-editor-discard').waitFor();
      checks = await layout('#save-editor-discard'); assert(!checks.dialogOverflow && checks.footerVisible);
      await page.locator('#save-editor-discard').click();
      await page.waitForFunction(() => !window.lifecycleProbe.state.saveEditor);
      await page.locator('[data-current-save-import]').click();
      await page.locator('#save-slot-confirm').waitFor();
      checks = await layout('#save-slot-confirm'); assert(!checks.dialogOverflow && checks.footerVisible);
      await page.screenshot({ path: path.join(output, mode.name + '-slot-import.png') });
      await page.locator('.modal-actions [data-modal-close]').click();
      await page.locator('[data-current-save-delete]').click();
      await page.locator('#save-delete-confirm').waitFor();
      checks = await layout('#save-delete-confirm');
      assert.equal(checks.dialogOverflow, false); assert(checks.footerVisible, mode.name + ': delete confirmation stays visible');
      assert(await page.locator('#save-delete-review').evaluate((element) => element.scrollWidth <= element.clientWidth + 1),
        mode.name + ': long save filenames wrap inside the confirmation');
      await page.evaluate(() => { window.lifecycleFailDelete = true; });
      await page.locator('#save-delete-confirm').click();
      await page.waitForFunction(() => document.querySelector('#save-delete-confirm')?.textContent === 'Review again');
      assert((await page.locator('#save-delete-status').textContent()).includes('changed on disk'));
      checks = await layout('#save-delete-confirm'); assert(checks.footerVisible);
      await page.screenshot({ path: path.join(output, mode.name + '-failed-delete.png') });
      await page.locator('.modal-actions [data-modal-close]').click();
      await page.evaluate(() => window.lifecycleProbe.setSavesTab('backups'));
      await page.locator('[data-backup-delete]').waitFor();
      checks = await layout(); assert.equal(checks.bodyOverflow, false, mode.name + ': backup tools wrap');
      await page.screenshot({ path: path.join(output, mode.name + '-backups.png') });
      await page.locator('[data-backup-delete]').click();
      await page.locator('#save-delete-confirm').waitFor();
      checks = await layout('#save-delete-confirm'); assert(checks.footerVisible);
      await page.locator('.modal-actions [data-modal-close]').click();
      await page.evaluate(() => {
        const ui = window.lifecycleProbe;
        ui.state.snapshot.mods = [{ id: 'library-one', name: 'Modern silencer model', imported: true, sha256: '1'.repeat(64) },
          { id: 'library-two', name: 'Unbreakable silencer', imported: true, sha256: '2'.repeat(64) }];
        window.lifecycleReplies.modMergeReview = { operation: 'mod-merge-review', status: 'needs-asset-merge', canAutoMerge: false,
          sources: ui.state.snapshot.mods.map((mod) => ({ id: mod.id, name: mod.name, available: true, kind: 'library', assetCount: 6 })),
          overlapCount: 1, truncated: false, warnings: ['Path checks cannot merge cooked assets.'], families: [{
            path: 'BendGame/Content/Weapons/' + 'LongCookedAssetName_'.repeat(12), kind: 'asset',
            sources: ui.state.snapshot.mods.map((mod) => ({ id: mod.id, name: mod.name,
              paths: ['BendGame/Content/Weapons/ModernSilencer.uasset', 'BendGame/Content/Weapons/ModernSilencer.uexp'] })) }] };
        ui.openCombinationReview();
      });
      await page.locator('[name="combination-package"]').nth(0).check();
      await page.locator('[name="combination-package"]').nth(1).check();
      checks = await layout('#combination-check'); assert(checks.footerVisible);
      await page.locator('#combination-check').click();
      await page.locator('#combination-choose').waitFor();
      await page.locator('.combination-family summary').click();
      checks = await layout('#combination-choose');
      assert.equal(checks.dialogOverflow, false); assert(checks.footerVisible);
      await page.screenshot({ path: path.join(output, mode.name + '-combination.png') });
      assert.equal(errors.length, 0, errors.join('\n'));
      report.cases.push({ ...mode, checks, scriptErrors: errors });
      await context.close();
    }
    fs.writeFileSync(path.join(output, 'report.json'), JSON.stringify(report, null, 2));
    console.log(JSON.stringify({ passed: true, layouts: report.cases.length, output }));
  } finally { await browser.close(); }
}
main().catch((error) => { console.error(error); process.exitCode = 1; }).finally(() => server.close());
