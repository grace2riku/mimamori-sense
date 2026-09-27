import fs from 'node:fs/promises';
import path from 'node:path';
import {pathToFileURL,fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../..');
const runtime=process.env.ARTIFACT_RUNTIME || 'C:/Users/grace/.cache/codex-runtimes/codex-primary-runtime/dependencies';
const build=process.env.PRESENTATION_BUILD || path.join(root,'tmp/tron-presentation');
const {PresentationFile,FileBlob}=await import(pathToFileURL(path.join(runtime,'node/node_modules/@oai/artifact-tool/dist/artifact_tool.mjs')));
const p=await PresentationFile.importPptx(await FileBlob.load(path.join(root,'doc/submission/presentation/MimamoriSense-TRON2026.pptx')));
for(let i=0;i<p.slides.items.length;i++){
 const png=await p.export({slide:p.slides.items[i],format:'png',scale:1.5});
 await fs.writeFile(path.join(build,`final-${String(i+1).padStart(2,'0')}.png`),new Uint8Array(await png.arrayBuffer()));
}
console.log('Rendered all final PPTX slides:',p.slides.items.length);
