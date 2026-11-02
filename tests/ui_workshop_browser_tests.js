'use strict';

// Optional layout acceptance in an isolated browser. Native replies are fixtures.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const http = require('node:http');
const crypto = require('node:crypto');
const { chromium } = require('playwright');
const root = path.resolve(__dirname, '..'), output = fs.mkdtempSync(path.join(os.tmpdir(), 'drifter-workshop-layout-'));
const original = fs.readFileSync(path.join(root, 'ui/app.js'), 'utf8');
let app = original.replace(/\}\)\(\);\s*$/,
  'window.workshopProbe = {state,navigate,render,applyAppearance,startWorkshopAsset,reviewWorkshopBuild,openWorkshopFileInput,extractWorkshopFile};})();');
const requestMarker = 'function request(action, args = {}, objects = null, waitForCompletion = false, activity = null) {';
assert(app.includes(requestMarker));
app = app.replace(requestMarker, requestMarker + `
  if (window.workshopFixture) return window.workshopFixture(action, args);`);
const server = http.createServer((request, response) => {
  try {
    const relative = decodeURIComponent(new URL(request.url, 'http://localhost').pathname).slice(1), file = path.resolve(root, relative);
    if (!(file.startsWith(path.join(root, 'ui') + path.sep) || file === path.join(root, 'catalog/catalog.json'))) {
      response.writeHead(404).end(); return;
    }
    const body = relative === 'ui/app.js' ? app : fs.readFileSync(file), type = { '.html': 'text/html', '.js': 'application/javascript',
      '.css': 'text/css', '.png': 'image/png', '.svg': 'image/svg+xml', '.json': 'application/json' }[path.extname(file)] || 'application/octet-stream';
    response.writeHead(200, { 'Content-Type': type, 'Cache-Control': 'no-store' }).end(body);
  } catch { response.writeHead(404).end(); }
});

async function main() {
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  const browser = await chromium.launch({ headless: true, channel: 'msedge', chromiumSandbox: true });
  const report = { scope: 'Shared workshop UI in isolated Edge, synthetic native replies; no game or PAK writes.',
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
      await page.waitForFunction(() => window.workshopProbe && !window.workshopProbe.state.loading);
      await page.evaluate((mode) => {
        const ui = window.workshopProbe;
        document.querySelector('#modal').close();
        Object.assign(ui.state, { preview: false, setupPrompted: true, noticeQueue: [], workshopLoaded: true,
          workshopWorking: false, workshopLoading: false, profileSession: { active: false }, libraryMetaLoaded: true });
        Object.assign(ui.state.snapshot, { platform: mode.platform, edition: 'local', accountContext: 'workshop-layout', gameRunning: false });
        Object.assign(ui.state.snapshot.settings, { autoScale: false, interfaceScale: 100, fontScale: mode.font, colorMode: mode.color,
          highContrast: !!mode.contrast, reducedMotion: true, seasonalThemes: false });
        const summary = { id: 'draft-native', revision: 0, dirty: false, sourceCount: 2, assetCount: 31, canBuild: true, canUndo: false,
          unresolvedCount: 0, analysis: { sharedFamilies: 31, identicalFamilies: 31, conflictingFamilies: 0, uncheckedFamilies: 31 }, sources: [{ id: 'left', name: 'First source' }, { id: 'right', name: 'Other source' }], warnings: [],
          assets: Array.from({ length: 31 }, (_, index) => ({ id: 'asset-' + index,
            path: 'BendGame/Content/Weapons/' + (index === 0 ? 'VeryLongAssetName_'.repeat(12) : 'Package_' + index) + '.uasset', bytes: 1024,
            sourceIds: ['left', 'right'], sourceNames: ['First_source_' + 'LongName_'.repeat(8) + '.pak', 'Other_source.pak'], selectedSource: 'left', conflict: false })) };
        const detail = { ...summary.assets[0], editable: true, fields: [
          { id: 'durability', name: 'Silencer durability ' + 'LongPropertyName_'.repeat(8), type: 'IntProperty', value: 10, originalValue: 10, changed: false },
          { id: 'float', name: 'Damage multiplier', type: 'FloatProperty', value: 1.25, originalValue: 1.25, changed: false },
          { id: 'boolean', name: 'Enabled', type: 'BoolProperty', value: true, originalValue: true, changed: false },
          { id: 'wide-integer', name: 'Exact 64-bit value', type: 'UInt64Property', editor: 'text', maxLength: 20,
            hint: 'An exact decimal value; ' + 'LongRangeHint_'.repeat(12), value: '18446744073709551615', originalValue: '18446744073709551615', changed: false },
          { id: 'enum-field', name: 'Existing enum choice', type: 'ByteProperty', editor: 'enum', value: 'First', originalValue: 'First', changed: false,
            choices: [{ value: 'First', label: 'LongEnumLabel_'.repeat(16) }, { value: 'Second', label: 'Second existing value' }] }
        ] };
        ui.state.workshop = summary; ui.state.workshopAsset = null; ui.state.workshopSelection = '';
        window.workshopFixture = async (action, args) => {
          (window.workshopCalls ||= []).push({ action, args: structuredClone(args) });
          if (action === 'workshopAddFileBrowse') {
            const current = structuredClone(ui.state.workshop), added = { id: 'new-file', path: args.virtualPath, originalPath: args.virtualPath,
              bytes: 20 * 1024 * 1024, selectedSource: 'added', sourceIds: [], sourceNames: [], added: true, editable: false, fields: [],
              reason: 'Opaque bulk data can be exported or replaced as a complete file.' };
            current.revision++; current.assetCount++; current.assets.push(added); current.dirty = true;
            return { session: current, asset: added };
          }
          if (action === 'workshopRenameFile' || action === 'workshopReplaceFileBrowse' || action === 'workshopRemoveFile' || action === 'workshopRestoreFile') {
            const current = structuredClone(ui.state.workshop), selected = { ...ui.state.workshopAsset };
            if (action === 'workshopRenameFile') selected.path = args.virtualPath;
            else if (action === 'workshopReplaceFileBrowse') selected.replaced = true;
            else selected.removed = action === 'workshopRemoveFile';
            current.revision++; current.assets = current.assets.map((asset) => asset.id === selected.id ? selected : asset);
            current.assetCount = current.assets.filter((asset) => !asset.removed).length;
            return { session: current, asset: selected };
          }
          if (action === 'workshopExtractReview') return { token: 'native-raw-token', session: ui.state.workshop.id,
            revision: ui.state.workshop.revision, asset: args.asset, displayName: ui.state.workshopAsset.path.split('/').at(-1),
            bytes: ui.state.workshopAsset.bytes, sha256: 'a'.repeat(64) };
          if (action === 'workshopExtractFileBrowse') return { exported: true };
          if (action === 'workshopResolveMerge') {
            const current = structuredClone(ui.state.workshop); current.revision++; current.unresolvedCount = 0; current.canBuild = true;
            window.manyWayChosen = args.choices;
            return { session: current, asset: { ...ui.state.workshopAsset, merge: { supported: true, conflicts: [] } } };
          }
          if (action === 'workshopAsset') return { session: summary.id, revision: summary.revision, asset: structuredClone(detail) };
          if (action === 'workshopEdit') {
            if (window.failWorkshopEdit) throw new Error('The property layout could not be verified. The draft was preserved.');
            summary.revision += 1; summary.dirty = true; summary.canUndo = true; summary.assets[0].edited = true;
            const field = detail.fields.find((field) => field.id === args.property);
            field.value = args.value; field.changed = field.value !== field.originalValue;
            return { session: structuredClone(summary), asset: structuredClone(detail) };
          }
          if (action === 'workshopResetValue') {
            const field = detail.fields.find((field) => field.id === args.property);
            field.value = field.originalValue; field.changed = false; summary.revision += 1;
            return { session: structuredClone(summary), asset: structuredClone(detail) };
          }
          if (action === 'workshopChanges') return { session: summary.id, revision: summary.revision,
            changes: detail.fields.filter((field) => field.changed).map((field) => ({ asset: detail.id, path: detail.path, property: field.id,
              name: field.name, type: field.type, before: field.originalValue, after: field.value })),
            families: [{ path: detail.path, keptSource: 'First source', discardedSources: ['Other source'] }],
            changeCount: detail.fields.filter((field) => field.changed).length, truncated: false };
          if (action === 'workshopBuildReview') return { session: summary.id, revision: summary.revision, token: 'native-build-token', assetCount: 31,
            bytes: 123456, changedProperties: 1, sourceNames: summary.sources.map((source) => source.name),
            warnings: Array.from({ length: 12 }, (_, index) => 'Review warning ' + index + ': unsupported gameplay interactions still need an actual game test.') };
          return {};
        };
        ui.applyAppearance(); ui.navigate('workshop');
      }, mode);
      const layout = () => page.evaluate(() => ({ bodyOverflow: document.body.scrollWidth > document.documentElement.clientWidth + 1,
        mainOverflow: document.querySelector('main').scrollWidth > document.querySelector('main').clientWidth + 1 }));
      let checks = await layout(); assert(!checks.bodyOverflow && !checks.mainOverflow, mode.name + ': asset browser wraps');
      await page.locator('[data-workshop-asset]').first().click();
      await page.locator('[data-workshop-field="durability"]').waitFor();
      checks = await layout(); assert(!checks.bodyOverflow && !checks.mainOverflow, mode.name + ': property editor wraps');
      assert.equal(await page.locator('[data-action="workshop-revert-inputs"]').isVisible(), false, 'No unapplied values means no Revert button');
      await page.screenshot({ path: path.join(output, mode.name + '-workshop.png') });
      await page.locator('#workshop-property-search').fill('Damage');
      assert.equal(await page.locator('[data-workshop-field]').count(), 1);
      await page.locator('#workshop-property-search').fill('');
      assert.equal(await page.locator('[data-workshop-field]').count(), 5);
      assert.equal(await page.locator('[data-workshop-field="wide-integer"]').inputValue(), '18446744073709551615');
      assert.equal(await page.locator('[data-workshop-field="enum-field"]').evaluate((element) => element.tagName), 'SELECT');
      await page.locator('[data-workshop-field="durability"]').fill('25');
      await page.waitForFunction(() => window.workshopProbe.state.workshop.revision === 1);
      assert.equal(await page.locator('[data-workshop-field="durability"]').inputValue(), '25');
      await page.locator('[data-action="workshop-changes"]').click();
      await page.locator('.workshop-changes-review').waitFor();
      assert((await page.locator('.workshop-changes-review').textContent()).includes('Opened source'));
      await page.locator('.modal-actions [data-modal-close]').click();
      await page.locator('[data-workshop-reset="durability"]').click();
      await page.waitForFunction(() => document.querySelector('[data-workshop-field="durability"]')?.value === '10');
      assert(await page.locator('[data-workshop-reset="durability"]').isDisabled());
      await page.locator('[data-workshop-field="durability"]').fill('25');
      await page.waitForFunction(() => window.workshopProbe.state.workshop.revision === 3);
      await page.locator('[data-workshop-field="durability"]').fill('not-a-number');
      assert(await page.locator('[data-action="workshop-build"]').isDisabled());
      await page.locator('[data-action="workshop-revert-inputs"]').click();
      assert.equal(await page.locator('[data-workshop-field="durability"]').inputValue(), '25');
      await page.evaluate(() => { window.failWorkshopEdit = true; });
      await page.locator('[data-workshop-field="durability"]').fill('40');
      await page.waitForFunction(() => document.querySelector('#workshop-error')?.textContent.includes('preserved'));
      await page.locator('[data-action="workshop-revert-inputs"]').click();
      await page.evaluate(() => { window.failWorkshopEdit = false; });
      await page.locator('[data-action="workshop-build"]').click();
      await page.locator('#workshop-export').waitFor();
      const review = await page.locator('#workshop-export').evaluate((control) => {
        const rect = control.getBoundingClientRect(), dialog = document.querySelector('#modal');
        return { footerVisible: rect.top >= 0 && rect.bottom <= innerHeight + 1, dialogOverflow: dialog.scrollWidth > dialog.clientWidth + 1 };
      });
      assert(review.footerVisible && !review.dialogOverflow, mode.name + ': build review keeps footer visible');
      await page.screenshot({ path: path.join(output, mode.name + '-build-review.png') });
      await page.locator('.modal-actions [data-modal-close]').click();
      await page.locator('[data-action="workshop-new"]').click();
      if (await page.locator('#workshop-project-discard').count()) await page.locator('#workshop-project-discard').click();
      await page.locator('#workshop-new-path').waitFor();
      const newAsset = await page.locator('#workshop-new-choose').evaluate((control) => {
        const rect = control.getBoundingClientRect(), dialog = document.querySelector('#modal');
        return { footerVisible: rect.top >= 0 && rect.bottom <= innerHeight + 1, dialogOverflow: dialog.scrollWidth > dialog.clientWidth + 1 };
      });
      assert(newAsset.footerVisible && !newAsset.dialogOverflow, mode.name + ': exported asset dialog keeps footer visible');
      await page.locator('.modal-actions [data-modal-close]').click();
      await page.locator('[data-action="workshop-file-add"]').click();
      await page.locator('#workshop-file-path').fill('BendGame/Content/Textures/WorkshopFixture.ubulk');
      const fileDialog = await page.locator('#workshop-file-confirm').evaluate((control) => {
        const rect = control.getBoundingClientRect(), dialog = document.querySelector('#modal');
        return { footerVisible: rect.top >= 0 && rect.bottom <= innerHeight + 1, dialogOverflow: dialog.scrollWidth > dialog.clientWidth + 1 };
      });
      assert(fileDialog.footerVisible && !fileDialog.dialogOverflow, mode.name + ': raw file dialog keeps footer visible');
      await page.locator('#workshop-file-confirm').click();
      await page.waitForFunction(() => window.workshopProbe.state.workshopSelection === 'new-file');
      assert((await page.locator('.workshop-file-tools').textContent()).includes('Export file…'));
      checks = await layout(); assert(!checks.bodyOverflow && !checks.mainOverflow, mode.name + ': raw file tools wrap');
      await page.screenshot({ path: path.join(output, mode.name + '-file-tools.png') });
      await page.locator('[data-action="workshop-file-replace"]').click();
      assert.equal(await page.locator('#workshop-file-path').count(), 0, 'Replacing bytes preserves the package path');
      await page.locator('#workshop-file-confirm').click();
      await page.waitForFunction(() => window.workshopProbe.state.workshopAsset.replaced === true);
      await page.locator('[data-action="workshop-file-extract"]').click();
      await page.locator('#workshop-file-export-confirm').click();
      await page.waitForFunction(() => !document.querySelector('#modal').open);
      await page.locator('[data-action="workshop-file-rename"]').click();
      await page.locator('#workshop-file-path').fill('BendGame/Content/Textures/RenamedWorkshopFixture.ubulk');
      await page.locator('#workshop-file-confirm').click();
      await page.waitForFunction(() => window.workshopProbe.state.workshopAsset.path.includes('RenamedWorkshopFixture'));
      await page.locator('[data-action="workshop-file-remove"]').click();
      await page.locator('#confirm-action').click();
      await page.waitForFunction(() => window.workshopProbe.state.workshopAsset.removed === true);
      assert((await page.locator('.workshop-file-tools').textContent()).includes('Restore file'));
      await page.locator('[data-action="workshop-file-restore"]').click();
      await page.locator('#confirm-action').click();
      await page.waitForFunction(() => window.workshopProbe.state.workshopAsset.removed === false);
      await page.evaluate(() => {
        const ui = window.workshopProbe, current = structuredClone(ui.state.workshop), selected = { ...ui.state.workshopAsset };
        current.sources = Array.from({ length: 24 }, (_, n) => ({ id: 'source-' + (n + 1), name: `Mod ${n + 1}.pak` }));
        current.sourceCount = 24; current.canBuild = false; current.unresolvedCount = 1;
        selected.path = 'BendGame/Content/Example.uasset'; selected.sourceIds = current.sources.map((source) => source.id);
        selected.sourceNames = current.sources.map((source) => source.name); selected.selectedSource = 'source-1'; selected.replaced = false;
        selected.merge = { supported: true, conflicts: [{ property: 'count', name: 'Count', base: 7,
          options: current.sources.map((source, n) => ({ source: source.id, value: n + 1 })) }] };
        ui.state.workshop = current; ui.state.workshopAsset = selected; ui.state.workshopMerge = { ...selected.merge, asset: selected.id };
        ui.render();
      });
      await page.locator('select[data-workshop-merge="count"]').selectOption('source-24');
      await page.locator('#workshop-merge-apply').click();
      await page.waitForFunction(() => window.manyWayChosen?.count === 'source-24');
      checks = await layout(); assert(!checks.bodyOverflow && !checks.mainOverflow, mode.name + ': many-source choices wrap');
      assert.equal(errors.length, 0, errors.join('\n'));
      report.cases.push({ ...mode, checks, review, newAsset, fileDialog, fileAndManyWayWorkflows: true, scriptErrors: errors });
      await context.close();
    }
    fs.writeFileSync(path.join(output, 'report.json'), JSON.stringify(report, null, 2));
    console.log(JSON.stringify({ passed: true, layouts: report.cases.length, output }));
  } finally { await browser.close(); }
}
main().catch((error) => { console.error(error); process.exitCode = 1; }).finally(() => server.close());
