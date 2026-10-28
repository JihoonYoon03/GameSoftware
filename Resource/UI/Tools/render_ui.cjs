// Rasterize text-free assets; render and inspect Korean SVG screens in Chromium.
const fs = require('node:fs');
const path = require('node:path');
const crypto=require('node:crypto');
const {pathToFileURL} = require('node:url');
const bundle = process.env.UI_NODE_MODULES || 'C:/Users/wlgns/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const sharp = require(require.resolve('sharp', {paths:[bundle]}));
const {chromium} = require(require.resolve('playwright', {paths:[bundle]}));
const root = path.resolve(__dirname, '..');
const read = p => JSON.parse(fs.readFileSync(path.join(root,p),'utf8'));
const save = (p,v) => fs.writeFileSync(path.join(root,p),JSON.stringify(v,null,2)+'\n');

(async()=>{
  const manifest=read('ui_manifest.json');
  const priorHashes=fs.existsSync(path.join(root,'Validation/sha256.json'))?read('Validation/sha256.json'):{};
  const unchanged=p=>fs.existsSync(path.join(root,p))&&priorHashes[p]===crypto.createHash('sha256').update(fs.readFileSync(path.join(root,p))).digest('hex');
  const productionAssets=[...Object.values(manifest.icons),...Object.values(manifest.hud_icons||{}),...Object.values(manifest.components)];
  for(const asset of productionAssets){
    if(unchanged(asset.svg)&&unchanged(asset.png))continue;
    await sharp(path.join(root,asset.svg)).resize(asset.width,asset.height).png().toFile(path.join(root,asset.png));
  }
  // 4px gutters make linear-filter sampling safe; pixel rectangles exclude gutters.
  const names=Object.keys(manifest.icons),cell=56,cols=8,rows=Math.ceil(names.length/cols);
  const atlas={width:cols*cell,height:rows*cell,padding:4,alpha:'straight',entries:{}};
  const composites=[];
  names.forEach((name,i)=>{
    const x=(i%cols)*cell+4,y=Math.floor(i/cols)*cell+4;
    composites.push({input:path.join(root,manifest.icons[name].png),left:x,top:y});
    atlas.entries[name]={x,y,width:48,height:48,uv:[x/atlas.width,y/atlas.height,(x+48)/atlas.width,(y+48)/atlas.height]};
  });
  if(!Object.values(manifest.icons).every(a=>unchanged(a.svg)&&unchanged(a.png))||!fs.existsSync(path.join(root,'Export/Atlases/icons.png')))
    await sharp({create:{width:atlas.width,height:atlas.height,channels:4,background:'#00000000'}}).composite(composites).png().toFile(path.join(root,'Export/Atlases/icons.png'));
  save('Export/Atlases/icons.json',atlas);
  const assetChecks=[];
  for(const asset of productionAssets){
    const m=await sharp(path.join(root,asset.png)).metadata();
    const {data,info}=await sharp(path.join(root,asset.png)).ensureAlpha().raw().toBuffer({resolveWithObject:true});
    let transparent=0,opaque=0,partial=0;
    for(let i=3;i<data.length;i+=info.channels){if(data[i]===0)transparent++;else if(data[i]===255)opaque++;else partial++;}
    assetChecks.push({file:asset.png,width:m.width,height:m.height,hasAlpha:m.hasAlpha,transparent,opaque,partial});
  }
  const browserPath=process.env.UI_BROWSER_PATH || 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe';
  const browser=await chromium.launch({headless:true,executablePath:browserPath});
  const page=await browser.newPage({viewport:{width:1280,height:800},deviceScaleFactor:1});
  const errors=[];page.on('pageerror',e=>errors.push(e.message));
  await page.goto(pathToFileURL(path.join(root,'preview.html')).href);
  await page.evaluate(()=>document.fonts.ready);
  const font=await page.evaluate(()=>({loaded:document.fonts.check('18px "Malgun Gothic"'),family:getComputedStyle(document.querySelector('svg text')).fontFamily}));
  const scenes=[];
  for(const [name,s] of Object.entries(manifest.screens)){
    await page.selectOption('#scene',name);
    // Standalone render avoids preview chrome and shows exactly the production canvas.
    const svg=await page.locator('#stage').innerHTML();
    const tab=await browser.newPage({viewport:{width:1280,height:800},deviceScaleFactor:1});
    await tab.setContent('<style>html,body{margin:0;background:#0b1720}svg{display:block}</style>'+svg);
    await tab.evaluate(()=>document.fonts.ready);
    const inspection=await tab.evaluate(()=>{
      const texts=[...document.querySelectorAll('text')].map(e=>{
        const b=e.getBoundingClientRect();return {text:e.textContent,x:b.x,y:b.y,w:b.width,h:b.height,size:+e.getAttribute('font-size'),maxWidth:+e.dataset.maxWidth||null};
      });
      const overlaps=[];
      for(let i=0;i<texts.length;i++)for(let j=i+1;j<texts.length;j++){
        const a=texts[i],b=texts[j];const iw=Math.min(a.x+a.w,b.x+b.w)-Math.max(a.x,b.x),ih=Math.min(a.y+a.h,b.y+b.h)-Math.max(a.y,b.y);
        if(iw>1&&ih>1)overlaps.push({a:a.text,b:b.text,width:iw,height:ih});
      }
      return {textCount:texts.length,overflow:texts.filter(t=>t.x<-.5||t.y<-.5||t.x+t.w>1280.5||t.y+t.h>800.5||(t.maxWidth&&t.w>t.maxWidth+.5)),overlaps,texts};
    });
    if(!unchanged(s.svg)||!fs.existsSync(path.join(root,s.png)))fs.writeFileSync(path.join(root,s.png),await tab.screenshot());
    scenes.push({name,...inspection});
    await tab.close();
  }
  // Real text metrics, not character-count estimates. Includes long Korean and big numbers.
  const samples=['헤일로 외곽 수신망 통신·보안·의료 지원 담당자','N-0 관문과 N-0/17 외곽 설비는 서로 다른 대상입니다.',
    '확인하지 못한 내용은 추정으로 남기고, 안전한 곳에서 중단된 대화를 다시 읽으세요.',
    '가까이 다가가 기록을 대조한 다음, 헤일로 수신망에서 조사 결과를 보고한다.',
    '체력 9,999 / 9,999','탄창 12 / 12 · 예비 탄창 무제한','크레딧 999,999,999','부품 999,999','경험치 999,999 / 999,999',
    '가나다라마바사아자차카타파하 ABC xyz 0123456789 / · - → ↗ ★ ☆ △ ½ ¼'];
  const stress=await page.evaluate(samples=>{
    const c=document.createElement('canvas'),ctx=c.getContext('2d');ctx.font='18px "Malgun Gothic"';
    return samples.map(text=>({text,width:ctx.measureText(text).width,font:'18px Malgun Gothic'}));
  },samples);
  // Inspect letterbox scaling at the two additional supported review sizes.
  const resolutions=[];
  for(const [width,height] of [[1280,800],[1920,1080],[2560,1440]]){
    const scale=Math.min(width/1280,height/800);
    const tab=await browser.newPage({viewport:{width,height},deviceScaleFactor:1});
    const scene=await page.evaluate(()=>window.uiScenes['05_dialogue'].svg);
    await tab.setContent(`<style>html,body{margin:0;background:#071016}body{display:flex;align-items:center;justify-content:center;width:100vw;height:100vh}svg{display:block;width:${1280*scale}px;height:${800*scale}px}</style>`+scene);
    await tab.evaluate(()=>document.fonts.ready);
    const file=`Validation/v1_2_r2_dialogue_${width}x${height}.png`;
    fs.writeFileSync(path.join(root,file),await tab.screenshot());await tab.close();
    resolutions.push({width,height,uniformScale:scale,offsetX:(width-1280*scale)/2,offsetY:(height-800*scale)/2,file,
      note:'Browser-rendered uniform canvas-fit design preview. Live-game integration untested.'});
  }
  // Background opacity stress captures in the same renderer.
  const opacity=[];
  for(const name of ['01_city','02_combat','04_mine','05_dialogue']){
    for(const alpha of [0.6,0.8,0.94,1]){
      const tab=await browser.newPage({viewport:{width:1280,height:800}});
      const scene=await page.evaluate(n=>window.uiScenes[n].svg,name);
      await tab.setContent(`<style>html,body{margin:0}svg{display:block;--panel-opacity:${alpha}}</style>`+scene);
      await tab.evaluate(()=>document.fonts.ready);
      const filename=`Validation/v1_2_${name}_alpha_${Math.round(alpha*100)}${name==='05_dialogue'?'_r2':''}.png`;
      if(!unchanged(manifest.screens[name].svg)||!fs.existsSync(path.join(root,filename)))fs.writeFileSync(path.join(root,filename),await tab.screenshot());await tab.close();
      opacity.push({scene:name,backgroundAlpha:alpha,file:filename});
    }
  }
  await page.selectOption('#scene','01_city');
  await page.locator('#opacity').fill('60');await page.locator('#opacity').dispatchEvent('input');
  await page.locator('#content').fill('85');await page.locator('#content').dispatchEvent('input');
  const controlCheck=await page.evaluate(()=>({panelOpacity:getComputedStyle(document.querySelector('.panel-fill')).opacity,contentOpacity:getComputedStyle(document.querySelector('[data-hud]')||document.querySelector('#stage text')).opacity}));
  await page.click('#background');
  controlCheck.backgroundHidden=await page.locator('.world-background').evaluate(e=>getComputedStyle(e).display==='none');
  // Compare authored UI coverage, excluding the schematic world, before/after this revision.
  const coverage=[];
  for(const name of ['01_city','02_combat','04_mine']){
    const counts={};
    for(const variant of ['before','after']){
      const source=variant==='before'?`Validation/Before_Refinement/${name}.svg`:manifest.screens[name].svg;
      const tab=await browser.newPage({viewport:{width:1280,height:800}});
      await tab.setContent('<style>html,body{margin:0;background:transparent}svg{display:block}</style>'+fs.readFileSync(path.join(root,source),'utf8'));
      await tab.evaluate(()=>{document.querySelector('.world-background')?.remove()});
      await tab.evaluate(()=>document.fonts.ready);
      const png=await tab.screenshot({omitBackground:true});await tab.close();
      const {data}=await sharp(png).ensureAlpha().raw().toBuffer({resolveWithObject:true});
      let occupied=0;for(let i=3;i<data.length;i+=4)if(data[i]>=26)occupied++;
      counts[variant]=occupied;
    }
    coverage.push({scene:name,...counts,beforePercent:100*counts.before/(1280*800),afterPercent:100*counts.after/(1280*800),reductionPercent:100*(1-counts.after/counts.before),definition:'Pixels with UI alpha >= 0.1 after removing world-background. Includes text, map, gauges and prompts; not a live-game measurement.'});
  }
  await browser.close();
  const comparisons=[];
  for(let i=0;i<coverage.length;i++){
    const n=coverage[i].scene;
    for(const [j,file] of [[0,`Validation/Before_Refinement/${n}.png`],[1,manifest.screens[n].png]]){
      comparisons.push({input:await sharp(path.join(root,file)).resize(800,500).png().toBuffer(),left:j*800,top:i*530});
      const label=`${j?'AFTER / HUD':'BEFORE / PANELS'} - ${n}`;
      comparisons.push({input:await sharp(Buffer.from(`<svg width="800" height="30"><rect width="800" height="30" fill="#07131c"/><text x="16" y="21" fill="white" font-family="sans-serif" font-size="15">${label}</text></svg>`)).png().toBuffer(),left:j*800,top:i*530+500});
    }
  }
  if(!fs.existsSync(path.join(root,'Preview/HUD_before_after_20261028.png'))||coverage.some(c=>!unchanged(manifest.screens[c.scene].svg)))
    await sharp({create:{width:1600,height:1590,channels:4,background:'#07131c'}}).composite(comparisons).png().toFile(path.join(root,'Preview/HUD_before_after_20261028.png'));
  // Contact sheet uses rasterized SVG review screens, not live-game images.
  const thumbs=[];const sheetW=1600,tileW=400,tileH=280;
  for(let i=0;i<scenes.length;i++){
    const n=scenes[i].name;
    const thumb=await sharp(path.join(root,manifest.screens[n].png)).resize(400,250).png().toBuffer();
    thumbs.push({input:thumb,left:(i%4)*tileW,top:Math.floor(i/4)*tileH});
    const caption=Buffer.from(`<svg width="400" height="30"><rect width="400" height="30" fill="#10242d"/><text x="12" y="20" fill="#eef5ef" font-size="12" font-family="Malgun Gothic">${n} · ${manifest.screens[n].requirements.join(' / ')}</text></svg>`);
    thumbs.push({input:await sharp(caption).png().toBuffer(),left:(i%4)*tileW,top:Math.floor(i/4)*tileH+250});
  }
  await sharp({create:{width:sheetW,height:Math.ceil(scenes.length/4)*tileH,channels:4,background:'#071016'}}).composite(thumbs).png().toFile(path.join(root,'Preview/contact_sheet_20261028_r2.png'));
  const report={generated:new Date().toISOString(),font,screenCount:scenes.length,assetCount:assetChecks.length,
    pageErrors:errors,overflowCount:scenes.reduce((n,s)=>n+s.overflow.length,0),overlapCount:scenes.reduce((n,s)=>n+s.overlaps.length,0),controlCheck,coverage,scenes,assets:assetChecks,stress,resolutions,opacity,
    scope:'Production raster asset checks and browser-rendered design previews. No game code, Runtime cache, gameplay or menu behavior tested.'};
  save('Validation/browser_report.json',report);
  console.log(JSON.stringify({screens:scenes.length,assets:assetChecks.length,font,errors,overflowCount:report.overflowCount,overlapCount:report.overlapCount,issues:scenes.filter(s=>s.overflow.length||s.overlaps.length).map(s=>({name:s.name,overflow:s.overflow,overlaps:s.overlaps})),controlCheck},null,2));
  if(errors.length||report.overflowCount||report.overlapCount||!controlCheck.backgroundHidden)process.exitCode=1;
})().catch(e=>{console.error(e);process.exit(1)});
