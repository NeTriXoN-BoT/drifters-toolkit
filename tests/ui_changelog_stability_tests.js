'use strict';

// Deterministic queued-click and held-reply regression checks. No native loader,
// user files, account or network is involved; both platform snapshots share UI.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const source = fs.readFileSync(path.join(__dirname, 'ui_smoke.js'), 'utf8');
const boundary = source.indexOf('async function testProfileCreation()');
assert(boundary > 0);
const harness = source.slice(0, boundary);
function create(platform) {
  const env = new Function('require', '__dirname', harness +
    '\nreturn {ui,node,context,nativeRequests,fixture,reply:(data)=>nativeMessageListener({data})};')(require, __dirname);
  const { ui, fixture, context } = env;
  const currentVersion = context.window.WHATS_NEW?.version;
  assert.match(currentVersion, /^\d+\.\d+\.\d+$/, 'Use the version supplied by the generated release notes');
  Object.assign(ui.state, { preview: false, loading: false, busy: false, view: 'settings',
    snapshot: ui.normalizeSnapshot({ ...fixture, appVersion: currentVersion, platform, edition: 'local', sandbox: false,
      accountContext: 'current-' + platform }), profileSession: { active: false }, nativeDropActivity: null,
    update: { status: null, offer: null, lastCheck: 0, checking: false } });
  let now = 1000000;
  context.Date = class extends Date { static now() { return now; } };
  env.advance = (value) => { now += value; };
  env.hold = (action) => {
    const bridge = context.window.chrome.webview, post = bridge.postMessage;
    let held;
    bridge.postMessage = (request) => {
      if (request.action === action) { held = request; env.nativeRequests.push(request); }
      else post.call(bridge, request);
    };
    return {
      count: () => env.nativeRequests.filter((request) => request.action === action).length,
      release: (data, error) => {
        assert(held, action + ' must be pending');
        env.reply({ requestId: held.requestId, ok: !error, ...(error ? { error } : { data }) });
      }
    };
  };
  return env;
}

async function scenario(platform) {
  {
    const { ui, node, context, nativeRequests } = create(platform);
    ui.showChangelog();
    const versions = ui.changelogReleases();
    const currentIndex = versions.findIndex((release) => release.version === ui.state.snapshot.appVersion);
    assert(currentIndex >= 0 && versions.length > 1);
    const otherIndex = (currentIndex + 1) % versions.length;
    const oldChoice = node('#changelog-pick-' + otherIndex).onclick;
    const oldKeyboard = node('#changelog-picker').onkeydown;
    const oldFocusOut = node('#changelog-picker').listeners.get('focusout');
    ui.showWhatsNew();
    const newerMarkup = node('#modal-content').innerHTML;
    // Unlike the common DOM stub, a real replacement modal has no old picker
    // nodes. This reproduced the old queued option handler's null dereference.
    const query = context.document.querySelector;
    context.document.querySelector = (selector) => selector.startsWith('#changelog-') ? null : query(selector);
    assert.doesNotThrow(oldChoice, 'A queued choice cannot dereference a removed changelog entry');
    assert.doesNotThrow(() => oldKeyboard({ key: 'ArrowDown', preventDefault() { throw Error('stale key'); } }));
    assert.doesNotThrow(() => oldFocusOut({ relatedTarget: null }));
    assert.equal(node('#modal-content').innerHTML, newerMarkup, 'A stale choice cannot replace the newer dialog');
    context.document.querySelector = query;

    ui.showChangelog();
    const entry = node('#changelog-entry');
    let writes = 0, markup = entry.innerHTML;
    Object.defineProperty(entry, 'innerHTML', { configurable: true, get: () => markup,
      set: (value) => { writes++; markup = value; } });
    for (let index = 0; index < 2000; ++index) node('#changelog-pick-' + currentIndex).onclick();
    assert.equal(writes, 0, 'Reselecting the displayed version does not rebuild its notes');
    node('#changelog-pick-' + otherIndex).onclick();
    assert.equal(writes, 0, 'Choosing another version does not replace parsed notes');
    assert.equal(entry.dataset.version, versions[otherIndex].version, 'Choosing another version updates immediately');
    assert.equal(node('#changelog-panel-' + currentIndex).hidden, true);
    assert.equal(node('#changelog-panel-' + otherIndex).hidden, false);
    assert.equal(node('#changelog-selected-version').textContent, versions[otherIndex].version);
    assert.equal(node('#changelog-installed-badge').hidden, true);
    assert.equal(node('#changelog-status').textContent, 'Showing changes for version ' + versions[otherIndex].version + '.');
    const summary = node('#changelog-version'), picker = node('#changelog-picker');
    const focus = summary.focus;
    let focusBeforeCollapse = false;
    summary.focus = (...args) => { focusBeforeCollapse = picker.open === true; focus.apply(summary, args); };
    picker.open = true;
    node('#changelog-pick-' + currentIndex).onclick();
    assert.equal(focusBeforeCollapse, true, 'Move focus before hiding the focused option');
    assert.equal(node('#changelog-installed-badge').hidden, false);
    summary.focus = focus;
    const chosenVersion = entry.dataset.version;
    const late = node('#changelog-pick-' + currentIndex).onclick;
    node('#modal').close();
    for (let index = 0; index < 2000; ++index) late();
    assert.equal(writes, 0, 'Clicks queued after closing do not rebuild notes or reopen the dialog');
    assert.equal(entry.dataset.version, chosenVersion, 'A closed selector cannot switch its retained panel');
    assert.equal(nativeRequests.length, 0, 'Changelog selection remains entirely offline');
  }
  {
    const env = create(platform), { ui, node } = env;
    const pending = env.hold('updateCheckDownloads');
    const first = ui.checkForUpdate(false);
    await Promise.all(Array.from({ length: 2000 }, () => ui.checkForUpdate(false)));
    assert.equal(pending.count(), 1, 'A burst of checks admits only one native ZIP scan');
    assert.match(ui.renderUpdatePanel(), /disabled aria-busy="true"[^>]*class="button small"[^>]*data-action="update-check"|disabled aria-busy="true"[^>]*data-action="update-check"/,
      'A rerendered update check remains visibly busy');
    pending.release({ found: false }); await first;
    await Promise.all(Array.from({ length: 2000 }, () => ui.checkForUpdate(false)));
    assert.equal(pending.count(), 1, 'Rapid checks after a reply cannot immediately rescan every archive');
    env.advance(2001);
    const retry = ui.checkForUpdate(false);
    assert.equal(pending.count(), 2, 'The short repeat guard permits a deliberate retry');
    pending.release({ found: false }); await retry;
    assert.equal(ui.state.update.checking, false);
    node('#modal').close();
  }
  for (const failed of [false, true]) {
    const env = create(platform), { ui } = env, pending = env.hold('updateCheckDownloads');
    const original = ui.state.update;
    const first = ui.checkForUpdate(false);
    ui.state.snapshot.accountContext = 'replacement-' + platform;
    const replacement = { status: null, offer: { version: '9.0.0', token: 'current-token' }, checking: true, lastCheck: 0 };
    ui.state.update = replacement;
    pending.release({ found: true, version: '2.0.0', token: 'old-token' }, failed ? 'Old scan error' : null);
    await first;
    assert.equal(ui.state.update, replacement);
    assert.equal(replacement.offer.token, 'current-token', 'An old scan cannot replace current installation state');
    assert.equal(replacement.checking, true, 'Old completion cannot unlock a newer operation');
    assert.equal(original.checking, false, 'The original operation releases its own guard');
  }
  {
    const env = create(platform), { ui } = env, pending = env.hold('toolkitUpdateCheck');
    ui.state.snapshot.edition = 'nexus';
    ui.state.snapshot.settings.autoCheckToolkitUpdates = true;
    ui.state.toolkitRelease = { checking: false, nextCheckAt: 0, status: null };
    const first = ui.checkToolkitRelease(false);
    await Promise.all(Array.from({ length: 1000 }, () => ui.checkToolkitRelease(false)));
    assert.equal(pending.count(), 1, 'Online checks retain their existing single-flight budget');
    ui.state.snapshot.settings.autoCheckToolkitUpdates = false;
    pending.release({ supported: true, available: true, status: 'available', version: '2.0.0' });
    await first;
    assert.equal(ui.state.toolkitRelease.status, null, 'Disabling notices while checking discards the late notice');
  }
}

(async () => {
  for (const platform of ['windows', 'linux']) await scenario(platform);
  console.log('Changelog queued-click, repeat-check and stale-result regressions passed for Windows/Linux states.');
})().catch((error) => { console.error(error); process.exitCode = 1; });
