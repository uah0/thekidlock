// Exercise the actual Web UI conversions and API calls without a lock.
const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
const html = fs.readFileSync(path.join(root, 'main/web/index.html'), 'utf8');
const webUiDialog = fs.readFileSync(path.join(root, 'android_lock_beacon/app/src/main/java/com/blelock/app/LockWebUiDialog.kt'), 'utf8');
const names = ['pulseSliderToMs', 'pulseMsToSlider', 'updateDeparturePulseValue',
  'sendDeparturePreview', 'saveDeparturePulse', 'updateBeaconReopenProtectionValue',
  'saveConfig', 'loadConfig', 'zoneSliderToRssi', 'rssiToZoneSlider', 'updateZoneValue'];
const functions = names.map(name => {
  const line = html.split(/\r?\n/).find(line => line.startsWith(`function ${name}(`) || line.startsWith(`async function ${name}(`));
  assert.ok(line, name);
  return line;
}).join('\n');
const element = () => ({value: 0, textContent: '', disabled: false, setAttribute(name, value) {this[name] = value;}});
const knob = () => ({knobValue: 0, textContent: '', flashes: 0,
  flashSavedValue() {this.flashes++;}, setAttribute(name, value) {this[name] = value;}});
const calls = [];
let config = {phone_rssi_threshold: -68, timer_a_ms: 3000, beacon_reopen_protection_ms: 3000,
  departure_buzzer_enabled: true, departure_buzzer_pulse_ms: 20};
let failSave = false;
let context;
context = vm.createContext({Math, Number, URLSearchParams, encodeURIComponent,
  departurePulse: element(), departurePulseValue: element(), departurePulseKnob: knob(),
  pr: element(), prValue: element(), prKnob: knob(), ta: element(), taKnob: knob(), beaconReopenProtection: element(),
  beaconReopenProtectionKnob: knob(), beaconReopenProtectionValue: element(), updateTimerAValue() {},
  document: {getElementById(id) {return context[id];}},
  wui: (key, values) => key === 'milliseconds' ? `${values.value} мс` :
    key === 'sound_off' ? 'Звук выключен' : key === 'seconds' ? `${values.value} с` :
      key === 'decimal_separator' ? '.' : key,
  showNotice() {}, uiError: e => String(e), setTimeout() {},
  async j(url) {calls.push(url);if(url === '/api/config') return config;if(failSave && url.includes('departure-buzzer-pulse')) throw Error('save failed');return {};}
});
vm.runInContext('let savedDeparturePulse=20;let departurePreviewPending=null,departurePreviewRunning=false;let configSaveRunning=false;const configSavePendingKnobs=new Set();\n' + functions, context);
async function test() {
  assert.equal(context.pulseSliderToMs(0), 0);
  assert.equal(context.pulseSliderToMs(1000), 120);
  assert.equal(context.pulseSliderToMs(500), 10); // Finer control at the quiet end.
  const seen = new Set();
  let previous = -1;
  for(let position=0;position<=1000;position++) {
    const ms = context.pulseSliderToMs(position);
    assert.ok(ms >= previous && ms >= 0 && ms <= 120);
    seen.add(ms);previous = ms;
  }
  assert.equal(seen.size, 121);
  for(let ms=0;ms<=120;ms++) assert.equal(context.pulseSliderToMs(context.pulseMsToSlider(ms)), ms);
  context.departurePulse.value = 0;
  context.updateDeparturePulseValue(true);
  await context.sendDeparturePreview();
  assert.equal(context.departurePulseValue.textContent, 'Звук выключен');
  assert.ok(calls.includes('/api/config/departure-buzzer-preview?ms=0'));
  await context.saveDeparturePulse();
  assert.ok(calls.includes('/api/config/departure-buzzer-pulse?ms=0'));
  assert.equal(context.departurePulseKnob.flashes, 1);
  context.departurePulse.value = 1000;
  await context.saveDeparturePulse();
  assert.ok(calls.includes('/api/config/departure-buzzer-pulse?ms=120'));
  context.departurePulse.value = 500;failSave = true;
  await context.saveDeparturePulse();
  assert.equal(context.departurePulse.value, 1000); // Restore confirmed 120 ms after failure.
  failSave = false;config.departure_buzzer_enabled = false;
  await context.loadConfig();
  assert.equal(context.prValue.textContent, '-68 dBm');
  assert.equal(context.prKnob.knobValue, 68);
  assert.equal(context.pr['aria-valuetext'], '-68 dBm');
  context.pr.value = 90;
  context.updateZoneValue();
  assert.equal(context.prValue.textContent, '-90 dBm');
  await context.loadConfig();
  assert.equal(context.prValue.textContent, '-68 dBm');
  assert.equal(context.departurePulse.value, 0);
  assert.equal(context.departurePulseKnob.knobValue, 0);
  config.departure_buzzer_enabled = true;config.departure_buzzer_pulse_ms = 160;
  await context.loadConfig();
  assert.equal(context.departurePulse.value, 1000);
  config.beacon_reopen_protection_ms = 5700;
  await context.loadConfig();
  assert.equal(context.beaconReopenProtection.value, 5700);
  assert.equal(context.beaconReopenProtectionKnob.knobValue, 5700);
  assert.equal(context.beaconReopenProtectionValue.textContent, '5.7 с');
  await context.saveConfig('beaconReopenProtectionKnob');
  assert.ok(calls.some(url => url.includes('beacon_reopen_protection_ms=5700')));
  assert.equal(context.beaconReopenProtectionKnob.flashes, 1);
  assert.match(html, /id="pr"[^>]*onchange="saveConfig\('prKnob'\)"/);
  assert.match(html, /id="ta"[^>]*onchange="saveConfig\('taKnob'\)"/);
  assert.match(html, /id="beaconReopenProtection"[^>]*onchange="saveConfig\('beaconReopenProtectionKnob'\)"/);
  assert.match(html, /id="departurePulse"[^>]*onchange="saveDeparturePulse\(\)"/);
  assert.doesNotMatch(html, /<button[^>]*onclick="saveConfig\(\)"/);
  assert.match(html, /id="beaconReopenProtection" class="zone-slider" type="range" min="3000" max="8000" step="100"/);
  assert.match(html, /id="pr" class="zone-slider" type="range" min="30" max="127" step="1"[^>]*oninput="updateZoneValue\(\)"/);
  assert.match(html, /id="prValue"/);
  for(const id of ['prKnob','departurePulseKnob','taKnob','beaconReopenProtectionKnob']) assert.ok(html.includes(`id="${id}"`));
  assert.match(html, /\.wrap\{[^}]*touch-action:pan-y/);
  assert.match(html, /\.knob-control\{display:block;width:225px;height:225px;margin:8px auto 2px/);
  assert.match(html, /\.knob-control\{[^}]*touch-action:pan-y/);
  assert.match(html, /\.knob-body\.save-flash\{animation:knob-save-flash 700ms ease-out\}/);
  assert.match(html, /@keyframes knob-save-flash[\s\S]*?var\(--good\)/);
  assert.match(html, /body\.style\.setProperty\('--knob-rest-fill','url\(#'\+gid\+'\)'\)/);
  assert.match(html, /el\.flashSavedValue=\(\)=>\{body\.classList\.remove\('save-flash'\)[\s\S]*?body\.classList\.add\('save-flash'\)/);
  assert.doesNotMatch(html, /knob-value-bg|knob-num\.save-flash/);
  assert.doesNotMatch(html, /id="(?:saved|departurePulseSaved)"/);
  assert.doesNotMatch(html, /scroll-rail|scroll-track|scroll-thumb|updateScrollIndicator/);
  assert.match(html, /function hapticTick\(\)\{if\(window\.BleLockHaptics&&typeof window\.BleLockHaptics\.tick==='function'\)window\.BleLockHaptics\.tick\(\);else if\(navigator\.vibrate\)navigator\.vibrate\(5\)\}/);
  assert.match(html, /if\(notch!==lastVibrationNotch\)\{lastVibrationNotch=notch;hapticTick\(\)\}/);
  assert.match(webUiDialog, /webView\.addJavascriptInterface\(HapticsBridge\(\), "BleLockHaptics"\)/);
  assert.match(webUiDialog, /@JavascriptInterface\s+fun tick\(\)\s*\{\s*if \(!trustedPage\) return[\s\S]*?performHapticFeedback\(HapticFeedbackConstants\.CLOCK_TICK\)/);
  assert.match(html, /p\.axis=Math\.abs\(dx\)>Math\.abs\(dy\)\?'horizontal':'vertical';if\(p\.axis==='vertical'\)return/);
  assert.match(html, /if\(p\.axis==='horizontal'\)set\(p\.startValue\+dx\*span\/el\.getBoundingClientRect\(\)\.width\)/);
  assert.doesNotMatch(html, /\.knob-control[^}]*wheel/);
  assert.ok(!html.includes('id="departureBuzzer"'));
  for(const locale of ['en','ru','uk']) {
    const dictionary = JSON.parse(fs.readFileSync(path.join(root, `android_lock_beacon/app/src/main/assets/webui/i18n/${locale}.json`), 'utf8'));
    assert.ok(dictionary.sound_off && dictionary.beacon_reopen_protection);
  }
  console.log('PASS: circular-control value sync, opening-zone dBm, sound and protection ranges, and locales');
}
test().catch(error => {console.error(error);process.exitCode = 1;});
