'use strict';

// Held native replies exercise real progress events without updating files.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const all = fs.readFileSync(path.join(__dirname, 'ui_smoke.js'), 'utf8');
const boundary = all.indexOf('async function testProfileCreation()');
assert(boundary > 0);
const harness = all.slice(0, boundary).replace('state, normalizeSnapshot, render,', 'handleModalBackdrop, state, normalizeSnapshot, render,');

async function scenario(platform, refuse) {
  const { ui, node, context, reply, nativeRequests } = new Function('require', '__dirname', harness +
    '\nreturn {ui,node,context,nativeRequests,reply: (data) => nativeMessageListener({data})};')(require, __dirname);
  ui.state.preview = false;
  ui.state.busy = false;
  ui.state.snapshot.platform = platform;
  let held;
  const bridge = context.window.chrome.webview, original = bridge.postMessage;
  bridge.postMessage = (request) => {
    if (request.action === 'updateInstall') { held = request; nativeRequests.push(request); }
    else original.call(bridge, request);
  };
  ui.confirmUpdate({ version: '1.2.0', token: 'native-reviewed-token', bytes: 1024 });
  const markup = node('#modal-content').innerHTML;
  const unpack = markup.indexOf('data-step="unpack"'), preflight = markup.indexOf('data-step="preflight"'), swap = markup.indexOf('data-step="swap"');
  assert(unpack >= 0 && preflight > unpack && swap > preflight, 'The installation check precedes replacements');
  const work = node('#update-install-confirm').onclick();
  for (let attempt = 0; attempt < 20 && !held; ++attempt) await new Promise(setImmediate);
  assert(held && held.token === 'native-reviewed-token' && !held.path);
  const progress = (step, done, total = 1) => reply({ event: 'updateProgress', data: { step, done, total } });
  progress('verify', 1);
  progress('unpack', 1);
  progress('preflight', 0);
  assert.equal(node('#updater-progress').value, 70);
  assert.match(node('#updater-status').textContent, /free space/);
  assert(ui.state.busy && node('#update-install-confirm').disabled, 'A check cannot start another install');
  const dialog = node('#modal');
  dialog.getBoundingClientRect = () => ({ left: 100, top: 100, right: 400, bottom: 400 });
  ui.handleModalBackdrop({ target: dialog, clientX: 10, clientY: 10 });
  assert(dialog.open, 'The backdrop cannot hide active update verification or file replacement');
  if (refuse) {
    reply({ requestId: held.requestId, ok: false, error: 'Not enough space for recovery copies.' });
    assert.equal(await work, false);
    assert.equal(ui.state.busy, false);
    assert.match(node('#updater-status').textContent, /Not enough space/);
    assert(node('#update-install-confirm').hidden, 'A failed token cannot be replayed');
    assert.notEqual(node('#update-not-now').disabled, true, 'The person can close the failed update');
    progress('preflight', 1);
    assert.equal(node('#updater-progress').value, 70, 'Late progress cannot turn a refusal into a pass');
  } else {
    progress('preflight', 1);
    assert.equal(node('#updater-progress').value, 80);
    assert.match(node('#updater-status').textContent, /checks passed/);
    progress('swap', 1);
    reply({ requestId: held.requestId, ok: true, data: { restarting: true, version: '1.2.0' } });
    assert.equal(await work, true);
    assert.equal(ui.state.update.offer, null);
    assert.match(node('#updater-status').textContent, /Starting it now/);
  }
  assert.equal(nativeRequests.filter((request) => request.action === 'updateInstall').length, 1);
}

(async () => {
  for (const platform of ['windows', 'linux']) {
    await scenario(platform, false);
    await scenario(platform, true);
  }
  console.log('Update preflight progress, refusal and late-event checks passed on both platforms.');
})().catch((error) => { console.error(error); process.exitCode = 1; });
