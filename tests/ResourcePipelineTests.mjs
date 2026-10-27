import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { buildInputHashes, fileHash, resourcePackStatus } from '../tools/resource-pack-inputs.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
test('freshness detects missing, stale and corrupt caches after a pull', () => {
    const parent = path.join(root, 'x64', 'RevisionChecks');
    fs.mkdirSync(parent, { recursive: true });
    const fixture = fs.mkdtempSync(path.join(parent, 'pack-freshness-'));
    try {
        assert.equal(resourcePackStatus(fixture).current, false); // No ignored Runtime on a fresh checkout.
        for (const name of Object.keys(buildInputHashes(root))) {
            fs.mkdirSync(path.dirname(path.join(fixture, name)), { recursive: true });
            fs.copyFileSync(path.join(root, name), path.join(fixture, name));
        }
        const source = path.join(fixture, 'Resource/Export/Characters/Test.glb');
        fs.mkdirSync(path.dirname(source), { recursive: true });
        fs.writeFileSync(source, 'source-version-a');
        const folder = path.join(fixture, 'Resource/Runtime');
        fs.mkdirSync(folder);
        const binary = path.join(folder, 'game-assets.bin');
        fs.writeFileSync(binary, Buffer.alloc(12));
        const manifest = { schema: 'game_resource_pack_v2', bytes: 12, sha256: fileHash(binary),
            assets: [{ name: 'test' }], sources: { 'Characters/Test': fileHash(source) },
            buildInputs: buildInputHashes(fixture) };
        const save = () => fs.writeFileSync(path.join(folder, 'manifest.json'), JSON.stringify(manifest));
        save();
        assert.equal(resourcePackStatus(fixture).current, true);
        const originalTime = fs.statSync(source);
        fs.writeFileSync(source, 'source-version-b');
        fs.utimesSync(source, originalTime.atime, originalTime.mtime);
        assert.equal(resourcePackStatus(fixture).current, false); // Same size and timestamp still changes content.
        fs.writeFileSync(source, 'source-version-a');
        assert.equal(resourcePackStatus(fixture).current, true);
        fs.appendFileSync(path.join(fixture, 'Resource/Documentation/assembly_recipes.json'), '\n');
        assert.equal(resourcePackStatus(fixture).current, false);
        manifest.buildInputs = buildInputHashes(fixture);
        save();
        assert.equal(resourcePackStatus(fixture).current, true);
        fs.writeFileSync(binary, Buffer.alloc(12, 1));
        assert.equal(resourcePackStatus(fixture).current, false);
        fs.writeFileSync(binary, Buffer.alloc(12));
        delete manifest.buildInputs;
        save();
        assert.equal(resourcePackStatus(fixture).current, false); // Legacy cache must regenerate once.
    } finally {
        // mkdtemp is confined to this test's ignored directory; never delete an arbitrary supplied path.
        fs.rmSync(fixture, { recursive: true, force: true });
    }
});
