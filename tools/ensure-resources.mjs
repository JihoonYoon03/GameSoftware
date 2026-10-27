// Build-time only: Runtime is intentionally excluded from Git. No Blender/npm packages are required.
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import { resourcePackStatus, writeBuildSourceList } from './resource-pack-inputs.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const status = resourcePackStatus(root);
if (status.current) {
    if (!process.argv.includes('--check'))
        writeBuildSourceList(root, JSON.parse(fs.readFileSync(path.join(root, 'Resource/Runtime/manifest.json'), 'utf8')));
    console.log('Game resources are up to date.');
} else if (process.argv.includes('--check')) {
    console.error('Game resources need regeneration: ' + status.reason + '. Run node tools/ensure-resources.mjs');
    process.exitCode = 1;
} else {
    console.log('Preparing game resources: ' + status.reason);
    const result = spawnSync(process.execPath, [path.join(root, 'tools', 'pack-resources.mjs')],
        { cwd: root, stdio: 'inherit' });
    if (result.error) throw result.error;
    if (result.status !== 0) process.exitCode = result.status ?? 1;
    else {
        const generated = resourcePackStatus(root);
        if (!generated.current) throw Error('Generated resource pack failed validation: ' + generated.reason);
    }
}
