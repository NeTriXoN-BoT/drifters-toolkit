'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const source = fs.readFileSync(path.join(__dirname, 'ui_smoke.js'), 'utf8');
const harness = source.slice(0, source.indexOf('async function testProfileCreation()'));
let checks = 0;
for (const platform of ['windows', 'linux']) {
  const { ui, context, fixture, nativeRequests } = new Function('require', '__dirname', harness +
    '\nreturn {ui,context,fixture,nativeRequests};')(require, __dirname);
  const articles = context.window.FIELD_GUIDE;
  const chapters = articles.filter((article) => article.modding).sort((a, b) => a.chapter - b.chapter);
  assert.deepEqual(Array.from(chapters, (article) => article.id), [
    'modding-start', 'modding-tools', 'modding-export', 'modding-roundtrip',
    'modding-package', 'modding-test', 'modding-debug', 'modding-publish', 'modding-asi'
  ], 'The existing learning order stays intact, with one appended native-plugin chapter'); ++checks;
  assert.equal(new Set(articles.map((article) => article.id)).size, articles.length,
    'Help articles and cross-links need unique identities'); ++checks;
  ui.state.snapshot = ui.normalizeSnapshot({ ...fixture, platform, sandbox: false });
  ui.state.loading = false; ui.state.helpSection = 'modding'; ui.state.view = 'help';
  ui.state.article = 'modding-asi';
  const before = nativeRequests.length;
  const chapter = ui.renderHelp();
  assert(chapter.includes('CHAPTER 9') && chapter.includes('ASI loaders: when a mod needs one'));
  assert(chapter.includes('Mods can use PAKs, ASI plugins, or both.') &&
    chapter.includes('a PAK file does not mean its ASI component is optional'),
    'The guide must explain per-mod native requirements instead of treating a PAK extension as proof'); ++checks;
  assert(chapter.includes('The <strong>ASI loader</strong> loads native plugins') &&
    chapter.includes('plugin supplies the mod&#x27;s code') &&
    chapter.includes('A loader on its own does not add'),
    'Loader setup and the plugin implementing a feature are separate requirements'); ++checks;
  assert(chapter.includes('cooked Blueprint content') &&
    chapter.includes('not limited to changing existing numbers') &&
    chapter.includes('not proof that the same edit is available in a shipped Days Gone asset'),
    'Describe packaged content without promising arbitrary Blueprint edits in the shipped game'); ++checks;
  assert(chapter.includes('<strong>Bleed Out</strong>') &&
    chapter.includes('records wounds and advances bleeding during gameplay') &&
    chapter.includes('authored approximations, not stock anatomical organ colliders') &&
    chapter.includes('does not need an accompanying PAK'),
    'The native-only example must explain its live hit/timer work and approximate geometry'); ++checks;
  assert(chapter.includes('<strong>Headshot Only Freakers</strong>') &&
    chapter.includes('PAK handles full-pawn Freakers') &&
    chapter.includes('optional horde ASI extends the rule to distant lightweight horde members') &&
    chapter.includes('other mods need an ASI for their core feature'),
    'The mixed example must keep required dependencies distinct from optional horde coverage'); ++checks;
  assert(chapter.includes('mismatched or unreadable executable leaves their hooks disabled') &&
    chapter.includes('validate the target instructions') &&
    chapter.includes('does not prove that every hook, gameplay interaction or plugin combination is bug-free'),
    'Explain supported-build guards without an absolute safety guarantee'); ++checks;
  assert(!chapter.includes('A PAK mod usually does not need an ASI loader.') &&
    !chapter.includes('only an ASI can do that') && !chapter.includes('can\'t break anything'),
    'Do not reintroduce the misleading generalization or an exclusive/absolute runtime promise'); ++checks;
  assert(chapter.includes('Switching it off or changing a PAK profile leaves separately installed native plugins active'),
    'A combined mod still needs separate native-plugin lifecycle instructions'); ++checks;
  assert(chapter.includes('WINEDLLOVERRIDES="version=n,b" %command%') && chapter.includes('BendGame/Binaries/Win64'),
    'Both platforms can read the matching Windows folder and per-game Proton example'); checks += 2;
  for (const id of ['asi-loader', 'asi-loader-releases', 'daydg', 'proton-launch-options', 'wine-dll-overrides', 'epic-cooking']) {
    assert(chapter.includes('data-modding-resource="' + id + '"'), 'Reference opens through its named native allowlist'); ++checks;
  }
  assert(!chapter.includes('href="http') && !chapter.includes('onclick='), 'No raw browser URL or executable handler is injected into guide content'); ++checks;
  ui.state.article = 'asi-plugin-not-loading';
  const troubleshooting = ui.renderHelp();
  assert(troubleshooting.includes('data-article="modding-asi"'), 'The troubleshooting card opens the complete loader chapter'); ++checks;
  assert(troubleshooting.includes('A working PAK does not confirm that its required native component loaded') &&
    troubleshooting.includes('plugin&#x27;s own log') && troubleshooting.includes('unsupported game build'),
    'Troubleshooting must cover a working PAK whose native dependency did not load'); ++checks;
  assert.equal(nativeRequests.length, before, 'Reading either help page does not install tools, access the network or change game files'); ++checks;
}
console.log('ASI help and platform-render checks passed: ' + checks);
