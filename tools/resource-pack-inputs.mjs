import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';

export const maxPackBytes = 512 * 1024 * 1024;
export const maxAnimationClips = 128;
const buildFiles = [
    'tools/pack-resources.mjs', 'tools/road-plan-catalog.mjs', 'tools/resource-pack-inputs.mjs',
    'Resource/Documentation/assembly_recipes.json', 'Resource/Documentation/accessory_fitting_offsets.json',
    'Resource/Documentation/character_detail_assemblies.json', 'Resource/Documentation/architecture_styles.json',
    'Resource/Documentation/scenery_assemblies.json', 'Resource/Documentation/transit_modules.json',
    'Resource/Documentation/road_plan_modules.json', 'SimpleGame/RoadPlanCatalog.h',
    'Resource/Documentation/aim_locomotion_blending.json'
];
export function fileHash(filename) {
    return crypto.createHash('sha256').update(fs.readFileSync(filename)).digest('hex');
}
export function buildInputHashes(root) {
    return Object.fromEntries(buildFiles.map(file => [file, fileHash(path.join(root, file))]));
}
export function writeBuildSourceList(root, manifest) {
    const files = [...Object.keys(manifest.sources).map(name => 'Resource/Export/' + name + '.glb'),
        ...Object.keys(manifest.buildInputs)];
    const filename = path.join(root, 'Resource', 'Runtime', 'build-sources.list');
    const content = files.join('\n') + '\n';
    if (!fs.existsSync(filename) || fs.readFileSync(filename, 'utf8') !== content)
        fs.writeFileSync(filename, content);
}
// A cheap freshness check hashes source bytes without repeating animation sampling/mesh conversion.
export function resourcePackStatus(root) {
    const folder = path.join(root, 'Resource', 'Runtime');
    try {
        const manifest = JSON.parse(fs.readFileSync(path.join(folder, 'manifest.json'), 'utf8'));
        const binary = path.join(folder, 'game-assets.bin');
        const size = fs.statSync(binary).size;
        if (manifest.schema !== 'game_resource_pack_v3' || size !== manifest.bytes ||
            size < 12 || size > maxPackBytes || !manifest.assets?.length || !manifest.sources ||
            !Object.keys(manifest.sources).length) return { current: false, reason: 'invalid resource pack' };
        if (JSON.stringify(manifest.buildInputs) !== JSON.stringify(buildInputHashes(root)))
            return { current: false, reason: 'converter, assembly settings or road catalogue changed' };
        for (const [source, hash] of Object.entries(manifest.sources)) {
            if (source.split(/[\\/]/).some(part => part === '..') || path.isAbsolute(source))
                return { current: false, reason: 'invalid resource source path' };
            if (fileHash(path.join(root, 'Resource', 'Export', source + '.glb')) !== hash)
                return { current: false, reason: 'source changed: ' + source };
        }
        if (fileHash(binary) !== manifest.sha256) return { current: false, reason: 'resource pack hash mismatch' };
        return { current: true, reason: 'resource pack matches all source files' };
    } catch (error) {
        return { current: false, reason: error.code === 'ENOENT' ? 'resource pack or source file is missing' : error.message };
    }
}
