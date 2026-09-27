// Build with the Codex bundled Node runtime. No firmware or generated FSP files are changed.
import fs from 'node:fs/promises';
import path from 'node:path';
import {pathToFileURL, fileURLToPath} from 'node:url';
import {createRequire} from 'node:module';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const runtime = process.env.ARTIFACT_RUNTIME || 'C:/Users/grace/.cache/codex-runtimes/codex-primary-runtime/dependencies';
process.env.RUNTIME_NODE_MODULES = path.join(runtime,'node/node_modules');
const requireRuntime = createRequire(path.join(runtime,'node/node_modules/package.json'));
const sharp = requireRuntime('sharp');
const skill = process.env.PRESENTATION_SKILL || 'C:/Users/grace/.codex/plugins/cache/openai-primary-runtime/presentations/26.905.11957/skills/presentations';
const build = process.env.PRESENTATION_BUILD || path.join(root, 'tmp/tron-presentation');
const output = path.join(root, 'doc/submission/presentation');
await fs.mkdir(build, {recursive:true});
const {Presentation, PresentationFile} = await import(pathToFileURL(path.join(runtime,'node/node_modules/@oai/artifact-tool/dist/artifact_tool.mjs')));
const {finalizePresentation} = await import(pathToFileURL(path.join(skill,'container_tools/artifact_tool_utils.mjs')));
const C={ink:'#17334A', teal:'#007F80', muted:'#536A78', paper:'#F8FAF9', pale:'#E4F1F0', green:'#23784D', amber:'#976900', red:'#B63B45', white:'#FFFFFF'};
const p=Presentation.create({slideSize:{width:1280,height:720}});
const records=[];
const commit='20765d02e267d293fd04438e8d31c72066786314';
const ref=(file,line)=>`${file}:${line}`;
let s, rec;
function txt(text,x,y,w,h,size=26,color=C.ink,bold=false,align='left') {
  const q=s.shapes.add({geometry:'textbox',position:{left:x,top:y,width:w,height:h},fill:'none',line:{fill:'none',width:0}});
  q.text=text; q.text.style={typeface:'Meiryo',fontSize:size,color,bold,alignment:align,verticalAlignment:'middle',autoFit:'none',wrap:true};
  rec.items.push({kind:'text',text,x,y,w,h,size,color,bold,align}); return q;
}
function box(text,x,y,w,h,color=C.teal,fill=C.pale,size=26){
  const q=s.shapes.add({geometry:'rect',position:{left:x,top:y,width:w,height:h},fill,line:{fill:color,width:2}});
  q.text=text; q.text.style={typeface:'Meiryo',fontSize:size,color,bold:true,alignment:'center',verticalAlignment:'middle',autoFit:'none',wrap:true};
  rec.items.push({kind:'box',text,x,y,w,h,size,color,fill,bold:true,align:'center'});return q;
}
function link(a,b,from='right',to='left'){
  s.shapes.connect(a,b,{kind:from==='right'&&a.position?.top!==b.position?.top?'elbow':'straight',fromSide:from,toSide:to,line:{fill:C.teal,width:2},tail:{type:'triangle',width:'med',length:'med'}});
}
async function photo(name,x,y,w,h){
  const file=path.join(root,'doc/submission/images',name+'.jpeg');
  const bytes=await sharp(file).rotate().jpeg({quality:95}).toBuffer();
  s.images.add({blob:new Uint8Array(bytes),contentType:'image/jpeg',alt:`MimamoriSense 実機写真 ${name}`,fit:'contain',position:{left:x,top:y,width:w,height:h}});
  rec.items.push({kind:'photo',file,x,y,w,h});
}
function slide(title,refs=[],foot=''){
  s=p.slides.add();s.background.fill=C.paper;
  rec={title,refs,items:[]};records.push(rec);
  txt(title,64,42,1152,74,42,C.ink,true);
  txt(String(records.length).padStart(2,'0'),1170,665,48,28,17,C.muted);
  if(foot)txt(foot,64,638,1080,50,15,C.muted);
}

slide('MimamoriSense', ['README.md:1','doc/product-requirements.md:5'], 'TRONプログラミングコンテスト2026　応募プログラム紹介資料　2026年9月26日');
txt('家庭内の転倒状態を\n画面と音で知らせる端末',64,176,650,142,39,C.ink,true);
txt('カメラによる人物検出と転倒判定を\nμT-Kernel 3.0上でつなぐ',64,373,650,96,28,C.teal);
txt('Renesas EK-RA8P1 / Ethos-U55 / LVGL',64,518,650,40,23,C.muted);
await photo('hardware',774,130,440,474);

slide('家庭内で異変に気づくための見守り', ['doc/product-requirements.md:5',ref('e2studio_CPU0/src/ai_inference_thread_entry.c',629),ref('e2studio_CPU0/src/audio_alarm.c',640)],'対象は屋内での見守り。今回紹介する通知先は、本体のLCDとスピーカーです。');
txt('家族が常に画面を見続けなくても、異変に気づける仕組みを目指します。',64,146,1148,76,29);
const a2=box('カメラ\n人物を撮影',64,284,240,124);
const b2=box('端末内の処理\n人物検出・転倒判定',382,284,350,124);
const c2=box('LCD\n状態を表示',854,235,344,94);
const d2=box('スピーカー\n警報音で通知',854,414,344,94);
link(a2,b2);link(b2,c2);link(b2,d2);
txt('身体にセンサーを装着しない、カメラ方式の試作です。',64,542,1120,60,27,C.teal);

slide('画像から転倒判定までの処理', [ref('e2studio_CPU0/src/camera_display.c',411),ref('e2studio_CPU0/src/ai_application/application_config.h',21),ref('e2studio_CPU0/src/ai_application/ai_application_config.h',41),ref('e2studio_CPU0/src/ai_inference_thread_entry.c',619),ref('e2studio_CPU0/src/fall_detection_logic.c',365)],'');
txt('AIは人物の位置を検出し、転倒状態は検出枠の形と連続性で判定します。',64,144,1150,70,29);
const pipe=[['画像の前処理','224 × 224\nRGB / INT8'],['AI人物検出','YOLO-Fastest V1\nEthos-U55 NPU'],['検出結果の整理','人物の枠・スコア\n重複する枠を整理'],['転倒判定','枠の幅 ÷ 高さ\n連続した判定結果']];
let prev;
for(let i=0;i<pipe.length;i++){
 const x=64+i*296; const q=box(pipe[i][0],x,278,256,80); if(prev)link(prev,q);prev=q;
 txt(pipe[i][1],x,388,256,100,25,C.ink,false,'center');
}
txt('人物検出モデルと、状態遷移による転倒判定を分離した構成',64,547,1130,59,29,C.teal,true);

slide('転倒判定の状態遷移', [ref('e2studio_CPU0/src/fall_detection_logic.h',62),ref('e2studio_CPU0/src/fall_detection_logic.c',118),ref('e2studio_CPU0/src/fall_detection_logic.c',365),ref('e2studio_CPU0/src/audio_alarm.c',640)],'既定設定。5回は推論後の判定回数であり、固定の秒数ではありません。');
txt('転倒候補：人物スコア 0.5以上、検出枠の 幅 ÷ 高さ が1.3以上',64,139,1148,64,27);
const n4=box('NORMAL\n監視中',64,301,270,119,C.green,'#E9F4ED');
const s4=box('SUSPECTED\n転倒の疑い',504,301,270,119,C.amber,'#FBF4DF');
const c4=box('CONFIRMED\n転倒確定・警報要求',944,301,270,119,C.red,'#FBECEE',24);
link(n4,s4);link(s4,c4);
txt('候補あり',351,270,142,45,22,C.muted,false,'center');
txt('候補が合計\n5回連続',792,247,136,72,22,C.muted,false,'center');
txt('疑いの段階で候補が途切れると、監視中に戻ります。',64,473,1136,47,26);
txt('確定後は、有効な人物検出があり転倒候補がない状態が5回続くと復帰。\n人物が画面から消えただけでは、転倒確定を解除しません。',64,539,1136,80,25,C.teal);

slide('実機画面で見る判定の変化', [ref('e2studio_CPU0/src/ui/fall_detection_screen.c',427),ref('e2studio_CPU0/src/ui/fall_detection_screen.c',494),'doc/submission/evaluation-guide.md:23','doc/submission/images/normal.jpeg','doc/submission/images/fall.jpeg'],'');
txt('通常の監視状態',64,147,552,48,29,C.green,true);
txt('転倒と判定した状態',664,147,552,48,29,C.red,true);
await photo('normal',64,217,552,366); await photo('fall',664,217,552,366);
txt('緑の枠 / Monitoring... / NORMAL',64,589,552,38,22,C.green);
txt('赤い枠 / FALL DETECTED / CONFIRMED',664,589,552,38,22,C.red);

slide('タッチ操作による日付・時刻設定', [ref('e2studio_CPU0/src/ui/ui_time_setting_screen.c',660),ref('e2studio_CPU0/src/ui/ui_time_setting_screen.c',743),ref('e2studio_CPU0/src/time_cache.c',170),ref('e2studio_CPU0/src/ui/ui_datetime.c',128)],'時刻保持にはRTCバックアップ電池を使用します。');
await photo('time-setting',64,165,735,437);
txt('01　歯車ボタンをタッチ',840,173,375,62,25,C.teal,true);
txt('02　年・月・日・時・分\n　　を選択',840,276,375,92,25,C.teal,true);
txt('03　OKを押して設定',840,397,375,62,25,C.teal,true);
txt('設定と読み戻しが成功すると\nメイン画面へ戻ります。',840,491,375,98,24);

slide('μT-Kernelによるタスク構成', [ref('e2studio_CPU0/src/hal_warmstart.c',177),ref('e2studio_CPU0/src/usermain.c',498),ref('e2studio_CPU0/src/ai_inference_thread_entry.c',587),ref('e2studio_CPU0/src/camera_display.c',390),ref('e2studio_CPU0/src/audio_alarm.c',961),ref('e2studio_CPU0/src/audio_alarm.c',1570),ref('e2studio_CPU0/src/time_cache.c',300)],'CPU0の主要処理を抜粋。');
txt('CPU0上で、画像・表示・警報・時刻処理を役割ごとに分けます。',64,143,1148,61,28);
const ca=box('カメラタスク\n撮影の初期化・制御',64,256,260,108);
const ui=box('LVGLタスク\n表示・前処理・操作',422,256,310,108);
const ai=box('AIタスク\n推論・転倒判定',932,256,282,108);
link(ui,ai);txt('画像準備の通知',744,216,175,37,20,C.muted,false,'center');
const ti=box('時刻タスク\nRTC設定・取得',422,480,310,105);
const al=box('警報タスク\n再生・停止を制御',932,480,282,105);
link(ui,ti,'bottom','top');link(ai,al,'bottom','top');
txt('設定要求',625,405,180,42,22,C.muted);
txt('転倒状態の通知',798,405,232,42,22,C.muted);
txt('μT-Kernel 3.0 BSP2\nタスクとイベントフラグで\n処理をつなぐ',64,450,320,119,25,C.teal,true);

slide('実機で確認する流れ', ['doc/submission/evaluation-guide.md:13',ref('e2studio_CPU0/src/fall_detection_logic.c',150),ref('e2studio_CPU0/src/audio_alarm.c',640)],'操作・期待結果の説明です。成功率や長時間の安定性を評価した結果ではありません。詳細は動作評価手順書を参照。');
const demo=[['起動','映像が更新されることを確認'],['通常姿勢','人物の枠とNORMALを確認'],['姿勢を変える','転倒表示と警報音を確認'],['姿勢を戻す','NORMALへの復帰と停止を確認']];
for(let i=0;i<4;i++){
 txt(String(i+1).padStart(2,'0'),64,170+i*104,75,58,37,C.teal,true);
 txt(demo[i][0],159,170+i*104,283,56,29,C.ink,true);
 txt(demo[i][1],468,170+i*104,723,56,27);
}
txt('再現条件の例：カメラから1m以内に座り、上体を前のめりに動かす。\n実際に転倒する必要はありません。1mは検出距離の上限ではありません。',64,574,1150,60,22,C.muted);

slide('現在の到達点と制約', ['README.md:1','doc/submission/evaluation-guide.md:1',ref('e2studio_CPU0/src/fall_detection_logic.c',365),'doc/design/issue-230.md:1043','doc/product-requirements.md:71'], '');
txt('到達点',64,151,540,58,32,C.teal,true);
txt('人物検出、転倒判定、LCD表示、\n警報連携、日時設定を実装。\n\n通常表示・転倒表示の実機写真と、\n審査用の操作手順を用意しています。',64,235,535,269,28);
txt('利用・評価上の制約',671,151,542,58,32,C.ink,true);
txt('横になった姿勢も転倒候補になり得ます。\n遠い人物・遮蔽・照明条件で見逃す\n可能性があります。',671,235,542,190,26);

slide('応募作品の特徴と関連資料', ['README.md:30','doc/submission/setup-guide.md:1','doc/submission/operation-manual.md:1','doc/submission/evaluation-guide.md:1','https://www.tron.org/ja/programming_contest-2026/apply/'],'');
txt('カメラ入力から、人物検出・転倒判定・表示・警報までを\nμT-Kernel上のアプリケーションとして統合',64,155,1140,120,35,C.teal,true);
txt('関連資料',64,330,300,54,30,C.ink,true);
txt('セットアップ手順書　　機材・ビルド・書き込み\n操作マニュアル　　　　画面・時刻設定・復帰操作\n動作評価手順書　　　　審査時の確認手順',64,405,1130,149,27);
txt('ソースと関連資料のリポジトリ',64,577,1140,35,22,C.muted);
txt('https://github.com/grace2riku/mimamori-sense',64,610,1100,31,23,C.teal);

await fs.writeFile(path.join(build,'slides.json'),JSON.stringify(records,null,2));
const candidate=path.join(build,'candidate.pptx');
await(await PresentationFile.exportPptx(p)).save(candidate);
console.log('Exported candidate',candidate);
// Render source slides at 1.5x for visual review and the PDF viewing edition.
for(let i=0;i<records.length;i++){
 const slide=p.slides.items[i];
 const blob=await p.export({slide,format:'png',scale:1.5});
 await fs.writeFile(path.join(build,`slide-${String(i+1).padStart(2,'0')}.png`),new Uint8Array(await blob.arrayBuffer()));
 console.log('Rendered',i+1);
}
await fs.mkdir(path.join(build,'publish'),{recursive:true});
const final=path.join(build,'publish/validated.pptx');
await finalizePresentation({workspaceDir:root,candidatePath:candidate,finalPath:final,
 pythonExecutable:path.join(runtime,'python/python.exe'),
 integrityValidatorPath:path.join(skill,'container_tools/inspect_presentation_package_integrity.py'),
 layoutValidatorPath:path.join(skill,'container_tools/inspect_presentation_layout_geometry.py'),
 layoutArgs:['--expected-slide-size-emu','12192000,6858000','--validate-heading-fit'],
 fontPolicy:{basis:'design',families:['Meiryo']},verifyArtifactToolImport:true,
 receiptPath:path.join(build,'validation.json')});
let md='# MimamoriSense 応募プログラム紹介資料\n\n2026年9月27日更新。\n\n';
for(let i=0;i<records.length;i++){
 const r=records[i];md+=`## ${i+1}. ${r.title}\n\n`;
 for(const it of r.items){if((it.kind==='text'||it.kind==='box')&&it.text!==r.title&&!/^\d{2}$/.test(it.text))md+=it.text.replaceAll('\n','  \n')+'\n\n';}
}
await fs.writeFile(path.join(output,'introduction.md'),md);
await fs.copyFile(final,path.join(output,'MimamoriSense-TRON2026.pptx'));
console.log('Final',path.join(output,'MimamoriSense-TRON2026.pptx'));
