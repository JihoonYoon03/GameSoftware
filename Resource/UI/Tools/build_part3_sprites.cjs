// Editable SVG masters and transparent PNGs for the Part 3 interaction kit.
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const bundle = process.env.UI_NODE_MODULES || path.resolve(path.dirname(process.execPath), '..', 'node_modules');
const sharp = require(require.resolve('sharp', {paths: [bundle]}));
const root = path.resolve(__dirname, '..');
const entries = [];
const cyan = '#8be9ed', pale = '#dbf9f8', amber = '#f6bc66', dim = '#456775';
const line = (d, color = cyan, width = 1) => `<path d="${d}" fill="none" stroke="${color}" stroke-width="${width}" stroke-linecap="round" stroke-linejoin="round"/>`;
const rect = (x,y,w,h,fill,extra='') => `<rect x="${x}" y="${y}" width="${w}" height="${h}" fill="${fill}" ${extra}/>`;
const circle = (cx,cy,r,col,w=1) => `<circle cx="${cx}" cy="${cy}" r="${r}" fill="none" stroke="${col}" stroke-width="${w}"/>`;
function add(name, w, h, art, group='Components', slice=0) {
    entries.push({name, w, h, art, group, slice});
}
function ticks(cx,cy,r,count,color,period=4) {
    return Array.from({length:count}, (_,i) => {
        const angle=i*2*Math.PI/count, length=i%period===0 ? 8 : 3;
        const c=Math.cos(angle), s=Math.sin(angle);
        return line(`M${cx+r*c} ${cy+r*s}l${-length*c} ${-length*s}`,color);
    }).join('');
}
function reticle(w,h,rx,ry) {
    const cx=w/2, cy=h/2;
    const ellipse = (x,y,col,dash='') => `<ellipse cx="${cx}" cy="${cy}" rx="${x}" ry="${y}" fill="none" stroke="${col}" ${dash}/>`;
    return ellipse(rx,ry,cyan)+ellipse(rx-5,ry-4,dim,'stroke-dasharray="5 7"')+
        line(`M${cx-5} ${cy-ry-4}h10m-5-3v7 M${cx-5} ${cy+ry+4}h10m-5-3v7 M${cx-rx-6} ${cy}h8 M${cx+rx-2} ${cy}h8`,pale,2);
}
add('world_prompt_pill',96,40,
    `<path d="M7.5 1.5h81l6 6v25l-6 6h-81l-6-6v-25z" fill="#0b1820" fill-opacity=".88" stroke="${cyan}"/>`+
    rect(2,12,4,16,cyan)+line('M12 35.5h72',dim), 'Components',12);
for (const active of [false,true]) {
    add('world_keycap_e'+(active?'_active':''),32,32,
        `<path d="M5 1.5h22l3.5 3.5v22L27 30.5H5L1.5 27V5z" fill="${active?cyan:'#172d38'}" stroke="${pale}"/>`+
        line('M5 7V5h22m0 22H5',active?'#429bac':'#436f80')+
        line('M21 9H12v14h9m-9-7h7',active?'#0a202d':pale,2.7));
}
add('world_focus_ring',128,64,reticle(128,64,54,20));
add('holo_wrist_emitter',64,64,circle(32,32,25,dim)+circle(32,32,18,cyan)+ticks(32,32,31,16,cyan)+
    line('M21 32h7l4-4 4 4h7m-11 4v5',pale,1.7));
add('globe_bezel_ring',512,512,circle(256,256,235,dim)+circle(256,256,222,cyan)+ticks(256,256,242,96,cyan,8)+
    line('M25 256h24m-12-12v24m426-12h24M246 21h20l-10 10zm0 470h20l-10-10z',pale,2.4)+
    line('M46 170l19 9m382 0 19-9M46 342l19-9m382 0 19 9',dim,2));
add('showcase_card_frame',128,128,
    `<path d="M8.5 1.5h111l7 7v111l-7 7h-111l-7-7v-111z" fill="none" stroke="${cyan}"/>`+
    rect(3,3,122,19,'#0b1820','fill-opacity=".9" rx="5"')+rect(3,102,122,23,'#0b1820','fill-opacity=".9" rx="5"')+
    line('M12 22h104M12 102h104m-35 1v13M1.5 49v30m125-30v30',dim,1),
    'Components',12);
add('wardrobe_turntable_ring',256,96,reticle(256,96,114,31)+
    line('M34 69l-10-3 9-4m189-35 10 3-9 4M102 84h52',pale,2));
for (const on of [true,false]) {
    add('part_chip_'+(on?'on':'off'),48,32,
        `<path d="M5.5 1.5h37l4 4v21l-4 4h-37l-4-4v-21z" fill="${on?'#16434c':'#111f28'}" stroke="${on?cyan:dim}"/>`+
        line(on?'M13 16l5 5 10-10m5 5h4':'M12 16h15m6 0h4',on?pale:'#a0bac2',2), 'Components',8);
}
let grid = rect(1,1,254,126,'#0b1820','fill-opacity=".84" stroke="#456775"');
for (let i=1;i<16;i++) grid+=line(`M${i*16} 2v124`,i===8?cyan:dim,i===8?1.4:.55);
for (let i=1;i<8;i++) grid+=line(`M2 ${i*16}h252`,i===4?cyan:dim,i===4?1.4:.55);
add('minigame_oscilloscope_grid',256,128,grid);
add('minigame_cipher_ring',256,256,[110,84,57].map((r,i)=>
    circle(128,128,r,i===0?cyan:dim)+ticks(128,128,r-2,48-8*i,i===1?dim:cyan,6)
).join('')+line('M122 8h12l-6 9z',amber,2));
add('minigame_timing_track',256,32,rect(1,3,254,26,'#12242e','rx="5" stroke="#698a94"')+
    rect(109,6,38,20,'#74572f')+line('M109 5v22m38-22v22m-19-20v18',amber,1.8)+
    Array.from({length:20},(_,i)=>line(`M${14+i*12} 20v5`,dim)).join(''));
add('minigame_circuit_node',48,48,
    `<path d="m13 3h22l10 10v22L35 45H13L3 35V13z" fill="#183742" stroke="${cyan}" stroke-width="1.5"/>`+
    circle(24,24,10,dim)+line('M3 24h11m20 0h11m-21-14v4m0 20v4m-4-14h8m-4-4v8',pale,1.8));
const icons = {
    interact_talk:'M5 6h22v15H14l-6 5v-5H5zm5 6h12m-12 4h8',
    interact_hack:'M5 7h22v16H5zm5 5 4 3-4 3m7 0h5m-11 9h10',
    interact_salvage:'M5 13l11-7 11 7v11l-11 4-11-4zm0 0 11 4 11-4m-11 4v10m-3-19 10 6',
    interact_door:'M5 27h22M8 27V5h15v22m-11-1V8l7 3v15m-3-9h.2',
    interact_vehicle:'M5 16l4-8h14l4 8v8H5zm0 0h22M8 24v3m16-3v3M8 20h4m8 0h4',
    interact_quest:'M8 26V5h17l-4 6 4 6H8m-4 9h8',
    map_planet_view:'M5 16a11 11 0 1 0 22 0 11 11 0 1 0-22 0zm11-11c-7 5-7 17 0 22m0-22c7 5 7 17 0 22M5 16h22',
    map_space_view:'M5 22c2 7 11 7 15 1S19 12 13 15c-3 2-2 6 2 5m6-16c9 2 10 11 5 15M6 6v8m-4-4h8',
    map_cluster_subquest:'M25 12c0 6-9 15-9 15S7 18 7 12a9 9 0 0 1 18 0zm-9-4v8m-4-4h8',
};
for (const [name,d] of Object.entries(icons)) add(name,32,32,line(d,name.endsWith('subquest')?amber:pale,2),'Icons');

async function main() {
    const manifestPath=path.join(root,'ui_manifest.json');
    const manifest=JSON.parse(fs.readFileSync(manifestPath,'utf8'));
    const assets={};
    for (const {name,w,h,art,group,slice} of entries) {
        const svg=`Source/${group}/${name}.svg`, png=`Export/${group}/${name}.png`;
        const content=`<svg xmlns="http://www.w3.org/2000/svg" width="${w}" height="${h}" viewBox="0 0 ${w} ${h}">${art}</svg>\n`;
        fs.writeFileSync(path.join(root,svg),content);
        const out=await sharp(Buffer.from(content)).png().toFile(path.join(root,png));
        assert.equal(out.width,w); assert.equal(out.height,h);
        const record={svg,png,width:w,height:h,role:'part3',...(slice?{nine_slice_inset:slice}:{})};
        manifest[group.toLowerCase()][name]=record;
        assets[`${group}/${name}`]=record;
    }
    fs.writeFileSync(manifestPath,JSON.stringify(manifest,null,2)+'\n');
    fs.writeFileSync(path.join(root,'Source/part3_ui_sprites.json'),JSON.stringify({
        requests:['UI-26','UI-27','UI-28','UI-29','UI-30'], pixels:'straight RGBA8', assets
    },null,2)+'\n');
    console.log(`Built ${entries.length} Part 3 sprites`);
}
main().catch(error=>{console.error(error);process.exitCode=1;});
