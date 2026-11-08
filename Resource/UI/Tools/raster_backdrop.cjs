// Rasterize editable static vectors with geometric stroke widths at 1x and 2x.
const fs=require('node:fs'),path=require('node:path');
const bundle=process.env.UI_NODE_MODULES||path.resolve(path.dirname(process.execPath),'..','node_modules');
const sharp=require(require.resolve('sharp',{paths:[bundle]}));
const root=path.resolve(__dirname,'..');
(async()=>{
 for(const name of ['stars','gate_rings','rail'])for(const scale of [1,2]){
  const png=await sharp(path.join(root,'Source/Backdrop',name+'.svg'),{density:72*scale}).resize(1280*scale,800*scale).png().toBuffer();
  fs.writeFileSync(path.join(root,'Export/Backdrop',`${name}_${scale}x.png`),png);
 }
})().catch(e=>{console.error(e);process.exit(1)});
