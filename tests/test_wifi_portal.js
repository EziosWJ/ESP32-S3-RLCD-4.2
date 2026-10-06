// Executes the real inline page script against a small DOM/fetch fixture.
// Node: node tests/test_wifi_portal.js
async function testWifiPortal(html) {
  const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
  const check = (value, message) => { if (!value) throw new Error(message); };
  const nodes = new Map();
  const document = { getElementById(id) {
    if (!nodes.has(id)) nodes.set(id, {
      value: '', textContent: '', disabled: false, hidden: false, options: [], handlers: {},
      classList: { toggle() {} },
      addEventListener(event, handler) { this.handlers[event] = handler; },
      replaceChildren(...items) { this.options = items; },
      add(option) { this.options.push(option); },
    });
    return nodes.get(id);
  }};
  const node = id => document.getElementById(id);
  let state = { state: 'ready', message: '', ip: '', token: 'TEST-TOKEN', scanning: false,
                scan_error: '', networks: [{ssid: '<img src=x onerror=alert(1)>', rssi: -40, secure: true}] };
  let failStatus = false, failPost = false, interval, statusRequests = 0;
  const posts = [];
  const fetch = async (url, options = {}) => {
    if (url === '/api/status') {
      statusRequests++;
      if (failStatus) throw new Error('offline');
      return { ok: true, json: async () => JSON.parse(JSON.stringify(state)) };
    }
    posts.push({url, options});
    if (failPost) throw new Error('lost acknowledgement');
    if (url === '/api/connect') state.state = 'connecting';
    return { ok: true };
  };
  class Encoder { encode(value) { return {length: unescape(encodeURIComponent(value)).length}; } }
  class Controller { constructor() { this.signal = {}; } abort() {} }
  function Option(text, value) { this.text = text; this.value = value; }
  new Function('document', 'fetch', 'TextEncoder', 'AbortController', 'Option',
               'setInterval', 'setTimeout', 'clearTimeout', script)(
    document, fetch, Encoder, Controller, Option, callback => {interval = callback;}, () => 1, () => {});
  // Drain the initial asynchronous poll, without a real timer.
  for (let i = 0; i < 12; i++) await Promise.resolve();
  check(!node('connect').disabled, 'initial status enables submit');
  check(node('networks').options[1].text.includes('<img'), 'hostile SSID stays option text');
  node('networks').value = state.networks[0].ssid;
  node('networks').handlers.change();
  check(node('ssid').value === state.networks[0].ssid, 'network selection fills SSID');
  node('show').checked = true; node('show').handlers.change();
  check(node('password').type === 'text', 'password visibility');
  const submit = () => node('form').handlers.submit({preventDefault() {}});
  node('ssid').value = '家'.repeat(11); node('password').value = '12345678';
  await submit(); check(posts.length === 0, 'UTF-8 SSID byte limit');
  node('ssid').value = 'home'; node('password').value = 'short';
  await submit(); check(posts.length === 0, 'short password rejected');
  node('password').value = '';
  await submit();
  check(posts.length === 1 && JSON.parse(posts[0].options.body).password === '', 'open network allowed');
  check(posts[0].options.headers['X-Setup-Token'] === 'TEST-TOKEN', 'setup token submitted');
  check(node('connect').disabled && node('scan').disabled, 'connecting disables actions');
  state.state = 'failed'; state.message = 'Authentication failed';
  await interval();
  check(!node('connect').disabled && node('status').textContent.includes('认证失败'), 'authentication failure allows retry');
  node('password').value = 'a'.repeat(64);
  failPost = true; failStatus = true;
  await submit();
  check(posts.length === 2 && node('connect').disabled, 'lost acknowledgement is not success');
  failPost = failStatus = false; state.state = 'connecting';
  await interval();
  state.state = 'connected'; state.ip = '192.168.1.27';
  await interval();
  check(node('form').hidden && node('password').value === '', 'success hides form and clears password');
  check(node('status').textContent.includes('192.168.1.27'), 'success shows assigned IP');
  const previous = statusRequests;
  await interval(); check(statusRequests === previous, 'success stops polling');
  return 'PASS: page script, UTF-8 boundaries, open network, safe SSID rendering, failure/retry, lost response, success';
}

if (typeof module !== 'undefined' && require.main === module) {
  const fs = require('fs');
  const path = require('path');
  testWifiPortal(fs.readFileSync(path.join(__dirname, '../main/wifi_portal.html'), 'utf8'))
    .then(console.log).catch(error => {console.error(error); process.exitCode = 1;});
}
