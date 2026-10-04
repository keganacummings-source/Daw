import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { test } from 'node:test';

const source = await readFile(new URL('../src/worker.js', import.meta.url), 'utf8');
const { default: worker } = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);

const THEME_IDS = [
  'amber', 'ash', 'bloodmoon', 'bone', 'default', 'goonr', 'ice', 'light',
  'moss', 'neon', 'rust', 'sulfur', 'trippah', 'violet', 'void', 'wine'
];

function post(body) {
  return new Request('https://dreamshare-api.test/', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body)
  });
}

test('plugin capabilities require a valid session and preserve existing routes', async () => {
  let accounts = { users: {}, sessions: {} };
  const env = {
    DREAMSHARE_KV: {
      async get(key, type) {
        if (key === 'accounts-v1') return type === 'json' ? accounts : JSON.stringify(accounts);
        return null;
      },
      async put(key, value) {
        if (key === 'accounts-v1') accounts = JSON.parse(value);
      }
    }
  };
  const originalFetch = globalThis.fetch;
  globalThis.fetch = async (url) => {
    if (String(url).startsWith('https://extendsclass.com/api/json-storage/bin/ffedede')) {
      return new Response('', { status: 200 });
    }
    return new Response(JSON.stringify({ threads: [], chat: [] }), { status: 200 });
  };

  try {
    const anonymous = await worker.fetch(post({ action: 'plugin_capabilities' }), env);
    assert.equal(anonymous.status, 401);
    assert.deepEqual(await anonymous.json(), { ok: false, error: 'Login required', code: 'auth' });

    const invalid = await worker.fetch(post({ action: 'plugin_capabilities', token: 'a'.repeat(48) }), env);
    assert.equal(invalid.status, 401);
    assert.deepEqual(await invalid.json(), { ok: false, error: 'Login required', code: 'auth' });

    const expiredToken = 'b'.repeat(48);
    accounts.sessions[expiredToken] = { user: 'ExpiredUser', exp: Date.now() - 1 };
    const expired = await worker.fetch(post({ action: 'plugin_capabilities', token: expiredToken }), env);
    assert.equal(expired.status, 401);
    assert.deepEqual(await expired.json(), { ok: false, error: 'Login required', code: 'auth' });

    const login = await worker.fetch(post({ action: 'login', user: 'PluginTestUser', pass: 'test-password' }), env);
    const loginBody = await login.json();
    assert.equal(login.status, 200);
    assert.equal(loginBody.ok, true);
    assert.equal(loginBody.created, true);
    assert.equal(loginBody.theme, 'trippah');
    assert.equal(loginBody.themes.length, THEME_IDS.length);
    assert.equal('capabilities' in loginBody, false);

    const capabilityResponse = await worker.fetch(post({
      action: 'plugin_capabilities',
      token: loginBody.token
    }), env);
    assert.equal(capabilityResponse.status, 200);
    assert.deepEqual(await capabilityResponse.json(), {
      ok: true,
      capabilities: { fxBuilder: true, themes: THEME_IDS }
    });

    const session = await worker.fetch(post({ action: 'session', token: loginBody.token }), env);
    const sessionBody = await session.json();
    assert.equal(session.status, 200);
    assert.equal(sessionBody.ok, true);
    assert.equal(sessionBody.user, 'PluginTestUser');
    assert.equal(sessionBody.themes.length, THEME_IDS.length);

    const feed = await worker.fetch(new Request('https://dreamshare-api.test/'), env);
    const feedBody = await feed.json();
    assert.equal(feed.status, 200);
    assert.equal(feedBody.ok, true);
    assert.equal('capabilities' in feedBody, false);
    assert.equal('fxBuilder' in feedBody, false);
  } finally {
    globalThis.fetch = originalFetch;
  }
});
