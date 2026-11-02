'use strict';

// Optional layout acceptance in an isolated browser. Native replies are fixtures.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const http = require('node:http');
const crypto = require('node:crypto');
const { chromium } = require('playwright');
const root = path.resolve(__dirname, '..'), output = fs.mkdtempSync(path.join(os.tmpdir(), 'drifter-workshop-projects-layout-'));
const original = fs.readFileSync(path.join(root, 'ui/app.js'), 'utf8');
let app = original.replace(/\}\)\(\);\s*$/,
  'window.workshopProbe = {state,navigate,render,applyAppearance,saveWorkshopProject,showWorkshopProjects,closeWorkshop};})();');
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
  const report = { scope: 'Saved-project shared UI only; synthetic native replies, no real project or PAK writes.',
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
          workshopWorking: false, workshopLoading: false, workshopProjectsLoading: false, profileSession: { active: false }, libraryMetaLoaded: true });
        Object.assign(ui.state.snapshot, { platform: mode.platform, edition: 'local', accountContext: 'project-layout', gameRunning: false });
        Object.assign(ui.state.snapshot.settings, { autoScale: false, interfaceScale: 100, fontScale: mode.font, colorMode: mode.color,
          highContrast: !!mode.contrast, reducedMotion: true, seasonalThemes: false });
        const summary = { id: 'draft-native', revision: 4, dirty: true, projectDirty: true, project: null,
          sourceCount: 2, assetCount: 1, canBuild: false, canUndo: true, unresolvedCount: 1,
          sources: [{ id: 'left', name: 'First_source.pak' }, { id: 'right', name: 'Other_source.pak' }], warnings: [],
          assets: [{ id: 'asset-one', path: 'BendGame/Content/Weapons/Example.uasset', bytes: 1024, edited: true }] };
        const projects = Array.from({ length: 32 }, (_, index) => ({ id: index.toString(16).padStart(32, '0'),
          name: (index === 0 ? 'A long saved project name with details ' : 'Saved project ' + index + ' ') + 'Collection_'.repeat(8),
          generation: 1, modifiedAt: 1793185200 + index, sourceCount: 2, assetCount: 1, changedProperties: index, unresolvedCount: 1 }));
        ui.state.workshop = summary; ui.state.workshopAsset = null; ui.state.workshopSelection = '';
        window.workshopFixture = async (action, args) => {
          if (action === 'workshopProjectsList') return { projects: structuredClone(projects), warnings: [] };
          if (action === 'workshopProjectSave') {
            const project = { ...projects[0], id: args.projectId || 'f'.repeat(32), name: args.name };
            summary.project = project; summary.projectDirty = false;
            return { project, session: structuredClone(summary) };
          }
          if (action === 'workshopProjectOpen') {
            const project = projects.find((item) => item.id === args.id);
            summary.id = 'reopened-draft'; summary.project = project; summary.projectDirty = false;
            return { project, session: structuredClone(summary) };
          }
          if (action === 'workshopProjectDeleteReview') return { token: 'native-delete-token', project: structuredClone(projects.find((item) => item.id === args.id)) };
          if (action === 'workshopStatus') return { available: true, session: structuredClone(summary) };
          return {};
        };
        ui.applyAppearance(); ui.navigate('workshop');
      }, mode);
      const pageLayout = () => page.evaluate(() => ({ bodyOverflow: document.body.scrollWidth > document.documentElement.clientWidth + 1,
        mainOverflow: document.querySelector('main').scrollWidth > document.querySelector('main').clientWidth + 1 }));
      const footer = (selector) => page.locator(selector).evaluate((control) => {
        const r = control.getBoundingClientRect(), dialog = document.querySelector('#modal');
        return { visible: r.top >= 0 && r.bottom <= innerHeight + 1, overflow: dialog.scrollWidth > dialog.clientWidth + 1 };
      });
      const pageCheck = await pageLayout(); assert(!pageCheck.bodyOverflow && !pageCheck.mainOverflow, mode.name + ': project toolbar wraps');
      await page.locator('[data-action="workshop-project-save"]').click();
      await page.locator('#workshop-project-name').fill('My saved project');
      const save = await footer('#workshop-project-save-confirm'); assert(save.visible && !save.overflow);
      await page.screenshot({ path: path.join(output, mode.name + '-save.png') });
      await page.locator('#workshop-project-save-confirm').click();
      await page.waitForFunction(() => !document.querySelector('#modal').open && window.workshopProbe.state.workshop.projectDirty === false);
      assert((await page.locator('.workshop-project-status').textContent()).includes('My saved project'));
      await page.locator('[data-action="workshop-projects"]').click();
      await page.locator('.workshop-project-row').first().waitFor();
      assert.equal(await page.locator('.workshop-project-row').count(), 32);
      const list = await footer('.modal-actions [data-modal-close]'); assert(list.visible && !list.overflow);
      await page.screenshot({ path: path.join(output, mode.name + '-list.png') });
      await page.locator('[data-workshop-project-delete]').first().click();
      await page.locator('#workshop-project-delete-confirm').waitFor();
      const deletion = await footer('#workshop-project-delete-confirm'); assert(deletion.visible && !deletion.overflow);
      await page.locator('.modal-actions [data-modal-close]').click();
      await page.evaluate(() => { window.workshopProbe.state.workshop.projectDirty = true; window.workshopProbe.closeWorkshop(); });
      await page.locator('#workshop-project-keep').waitFor();
      const replace = await footer('#workshop-project-keep'); assert(replace.visible && !replace.overflow);
      await page.locator('#workshop-project-keep').click();
      await page.locator('#workshop-project-save-copy').waitFor();
      const copy = await footer('#workshop-project-save-confirm'); assert(copy.visible && !copy.overflow);
      assert.equal(errors.length, 0, errors.join('\n'));
      report.cases.push({ ...mode, pageCheck, save, list, deletion, replace, copy, scriptErrors: errors });
      await context.close();
    }
    fs.writeFileSync(path.join(output, 'report.json'), JSON.stringify(report, null, 2));
    console.log(JSON.stringify({ passed: true, layouts: report.cases.length, output }));
  } finally { await browser.close(); }
}
main().catch((error) => { console.error(error); process.exitCode = 1; }).finally(() => server.close());
