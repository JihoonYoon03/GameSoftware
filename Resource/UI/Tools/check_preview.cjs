const fs=require('node:fs'),path=require('node:path');
const {pathToFileURL}=require('node:url');
const root=path.resolve(__dirname,'..');
const bundle=process.env.UI_NODE_MODULES||'C:/Users/wlgns/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const {chromium}=require(require.resolve('playwright',{paths:[bundle]}));
(async()=>{
 const browser=await chromium.launch({headless:true,executablePath:process.env.UI_BROWSER_PATH||'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
 try{
  const page=await browser.newPage();await page.goto(pathToFileURL(path.join(root,'preview.html')).href);
  await page.selectOption('#scene','05_dialogue');await page.click('#flowNext');
  const next=await page.locator('#scene').inputValue();
  await page.keyboard.press('PageUp');const previous=await page.locator('#scene').inputValue();
  await page.selectOption('#scene','47_notice_page_01');await page.click('#flowNext');
  const noticeNext=await page.locator('#scene').inputValue();
  const result={status:next.startsWith('45_dialogue_page_')&&previous==='05_dialogue'&&noticeNext==='47_notice_page_02'?'PASS':'FAIL',dialogueNext:next,dialoguePrevious:previous,noticeNext};
  fs.writeFileSync(path.join(root,'Validation/preview_navigation.json'),JSON.stringify(result,null,2)+'\n');
  console.log(result);if(result.status!=='PASS')process.exitCode=1;
 }finally{await browser.close()}
})().catch(e=>{console.error(e);process.exit(1)});
