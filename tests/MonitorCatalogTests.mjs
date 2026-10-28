import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { setImmediate } from 'node:timers/promises';
import vm from 'node:vm';
import test from 'node:test';

const source = await readFile(new URL('../monitor/models.js', import.meta.url), 'utf8');

async function dashboard(responses) {
    const timers = new Map();
    const status = { textContent: '' };
    let nextTimer = 0, renders = 0, requests = 0;
    const context = vm.createContext({
        location: { protocol: 'http:', port: '17707' },
        AbortController,
        document: {
            getElementById(id) {
                if (id === 'catalog-status') return status;
                if (id === 'model-grid') return { replaceChildren() { ++renders; } };
                return { addEventListener() {} };
            },
        },
        async fetch(url) {
            assert.equal(url, '/api/models');
            ++requests;
            assert.ok(responses.length, 'unexpected catalog request');
            const response = responses.shift();
            if (response instanceof Error) throw response;
            return { ok: true, async json() { return response; } };
        },
        setTimeout(callback, delay) {
            const id = ++nextTimer;
            timers.set(id, { callback, delay });
            return id;
        },
        clearTimeout(id) { timers.delete(id); },
    });
    vm.runInContext(source, context);
    await setImmediate();
    return {
        status, timers,
        get renders() { return renders; },
        get requests() { return requests; },
        async retry(delay) {
            assert.equal(timers.size, 1, 'fetch timeout must have been cleared');
            const [id, timer] = [...timers][0];
            assert.equal(timer.delay, delay);
            timers.delete(id);
            await timer.callback();
        },
    };
}

test('pending catalog polls without rendering and accepts the existing completed schema', async () => {
    const page = await dashboard([
        { schema: 'game_models_v1', ready: false, models: [] },
        { schema: 'game_models_v1', models: [] },
    ]);
    assert.equal(page.renders, 0);
    assert.match(page.status.textContent, /준비하고/);
    await page.retry(500);
    assert.equal(page.requests, 2);
    assert.equal(page.renders, 1);
    assert.equal(page.timers.size, 0);
    assert.match(page.status.textContent, /0종/);
});

test('serialization failure stops polling and explains recovery', async () => {
    const page = await dashboard([
        { schema: 'game_models_v1', ready: false, error: true, models: [] },
    ]);
    assert.equal(page.renders, 0);
    assert.equal(page.timers.size, 0);
    assert.match(page.status.textContent, /게임을 다시 실행/);
});

test('connection failure and invalid schema keep the existing reconnect behavior', async () => {
    const page = await dashboard([
        new Error('connection closed'),
        { schema: 'unexpected', models: [] },
        { schema: 'game_models_v1', models: [] },
    ]);
    assert.equal(page.renders, 0);
    await page.retry(2000);
    assert.match(page.status.textContent, /스키마 불일치/);
    await page.retry(2000);
    assert.equal(page.requests, 3);
    assert.equal(page.renders, 1);
    assert.equal(page.timers.size, 0);
});
