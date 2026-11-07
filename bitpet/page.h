// Pagina de configuracao do BitPet (servida pelo proprio aparelho; nada sai da rede local).
#pragma once
#include <Arduino.h>

const char PAGE[] PROGMEM = R"HTML(<!doctype html><html lang=pt><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>BitPet</title>
<style>
body{font-family:system-ui,sans-serif;background:#0d1117;color:#e6edf3;max-width:460px;margin:auto;padding:16px}
h2{margin:.2em 0}h3{margin:.2em 0 .5em;color:#f7931a}
.c{background:#161b22;border:1px solid #30363d;border-radius:12px;padding:14px;margin:12px 0}
input,button{width:100%;padding:12px;margin:6px 0;border-radius:8px;border:0;font-size:16px;box-sizing:border-box}
input{background:#0d1117;color:#e6edf3;border:1px solid #30363d}
button{background:#f7931a;font-weight:bold;color:#111}button.s{background:#30363d;color:#e6edf3}
#m{min-height:1.4em;color:#7ee787}#m.e{color:#ff7b72}small{color:#8b949e}canvas{width:100%;border-radius:8px;margin-top:8px}
.h{display:none}
</style></head><body>
<h2>BitPet</h2><small>Tudo fica salvo apenas neste aparelho, sem conta e sem nuvem.</small>
<div id=m></div>

<div class=c id=a1><h3>1. Acesso</h3>
<small>Para alterar qualquer dado, o aparelho mostra um PIN aleatorio na tela dele. Quem nao esta vendo o aparelho nao consegue mudar nada.</small>
<button id=gp>Mostrar PIN na tela do BitPet</button>
<input id=pin inputmode=numeric maxlength=6 placeholder="PIN de 6 digitos">
<button id=lg class=s>Entrar</button></div>

<div class="c h" id=a2><h3>2. Wi-Fi e carteira</h3>
<form id=f><input id=s placeholder="Nome do Wi-Fi (SSID)" required>
<input id=p type=password placeholder="Senha do Wi-Fi (vazio = manter)">
<input id=ad placeholder="Endereco Bitcoin (bc1... / 1... / 3...)" required>
<button>Salvar e reiniciar</button></form></div>

<div class="c h" id=a3><h3>3. Personagem (sprites)</h3>
<small>Envie uma imagem PNG com 5 linhas (Alimentado, Feliz, Normal, Com fome, Quase morto) x 4 quadros, fundo claro liso ou transparente. A conversao e feita aqui no seu navegador.</small>
<input id=fi type=file accept="image/*">
<canvas id=pv class=h></canvas>
<button id=up class=h>Enviar para o BitPet</button>
<button id=rs class=s>Restaurar personagem padrao</button></div>

<div class="c h" id=a4><h3>4. Capturar a tela</h3>
<small>Tira uma foto exata da tela do BitPet (320x240), boa para documentacao. Mostra o saldo que estiver na tela.</small>
<label style="display:block;margin:8px 0"><input id=mk type=checkbox checked style="width:auto;margin-right:8px">Ocultar saldo e IP</label>
<select id=ov style="width:100%;padding:10px;border-radius:8px;background:#0d1117;color:#e6edf3;border:1px solid #30363d"><option value="">Tela normal</option><option value=ip>Com a caixa do IP</option><option value=pin>Com a caixa do PIN (PIN de exemplo)</option></select>
<button id=cp class=s>Capturar tela</button>
<img id=sh style="display:none;width:100%;border-radius:8px;margin-top:8px;image-rendering:pixelated">
<a id=dl download=bitpet.png style="display:none;color:#f7931a">Baixar PNG (960x720)</a></div>

<script>
const $=i=>document.getElementById(i);let T='',BIN=null;
const msg=(t,e)=>{$('m').textContent=t;$('m').className=e?'e':''};
async function post(u,b){try{const r=await fetch(u,{method:'POST',body:b});return{ok:r.ok,t:await r.text()}}catch(x){return{ok:false,t:'Sem resposta do aparelho'}}}
$('gp').onclick=async()=>{const r=await post('/pin',new URLSearchParams());msg(r.ok?'Veja o PIN na tela do BitPet e digite abaixo.':r.t,!r.ok)};
$('lg').onclick=async()=>{const r=await post('/auth',new URLSearchParams({p:$('pin').value}));
 if(!r.ok){msg(r.t,1);return}
 T=r.t;msg('Acesso liberado por 10 minutos.');$('a1').classList.add('h');$('a2').classList.remove('h');$('a3').classList.remove('h');$('a4').classList.remove('h');
 try{const i=await (await fetch('/info?t='+T)).json();$('s').value=i.ssid;$('ad').value=i.addr}catch(x){}};
$('f').onsubmit=async e=>{e.preventDefault();
 const r=await post('/save?t='+T,new URLSearchParams({s:$('s').value,p:$('p').value,a:$('ad').value.trim()}));
 msg(r.ok?'Salvo! O aparelho vai reiniciar.':r.t,!r.ok)};
$('cp').onclick=async()=>{msg('Capturando (alguns segundos)...');try{
 const r=await fetch('/screen?t='+T+'&m='+($('mk').checked?1:0)+'&o='+$('ov').value);if(!r.ok){msg('Sem acesso ou sessao expirada',1);return}
 const bm=await createImageBitmap(await r.blob()),cv=document.createElement('canvas');cv.width=960;cv.height=720;
 const c=cv.getContext('2d');c.imageSmoothingEnabled=false;c.drawImage(bm,0,0,960,720);   // 3x sem suavizar: pixel art nitida
 cv.toBlob(b=>{const u=URL.createObjectURL(b);$('sh').src=u;$('sh').style.display='block';$('dl').href=u;$('dl').style.display='inline-block';msg('Captura pronta.')},'image/png')}
 catch(x){msg('Falha na captura',1)}};
$('rs').onclick=async()=>{const r=await post('/sprite/reset?t='+T,new URLSearchParams());msg(r.ok?'Personagem padrao restaurado.':r.t,!r.ok)};
$('up').onclick=async()=>{if(!BIN)return;msg('Enviando '+Math.round(BIN.length/1024)+' KB...');
 const fd=new FormData();fd.append('f',new Blob([BIN]),'spr.bin');
 const r=await post('/sprite?t='+T,fd);msg(r.ok?'Pronto! Personagem atualizado.':r.t,!r.ok)};

// ---------- conversor de sprite sheet (5 linhas x 4 quadros) ----------
function flood(m,w,h){const o=new Uint8Array(w*h),st=[];const push=i=>{if(m[i]&&!o[i]){o[i]=1;st.push(i)}};
 for(let x=0;x<w;x++){push(x);push((h-1)*w+x)}for(let y=0;y<h;y++){push(y*w);push(y*w+w-1)}
 while(st.length){const i=st.pop(),x=i%w,y=(i/w)|0;if(x>0)push(i-1);if(x<w-1)push(i+1);if(y>0)push(i-w);if(y<h-1)push(i+w)}return o}
function lcc(m,w,h){const lab=new Int32Array(w*h);let best=0,bn=0,n=0;const st=[];
 for(let s=0;s<w*h;s++){if(!m[s]||lab[s])continue;n++;let c=0;lab[s]=n;st.push(s);
  while(st.length){const i=st.pop();c++;const x=i%w,y=(i/w)|0;
   for(let dy=-1;dy<=1;dy++)for(let dx=-1;dx<=1;dx++){const nx=x+dx,ny=y+dy;if(nx<0||ny<0||nx>=w||ny>=h)continue;const j=ny*w+nx;if(m[j]&&!lab[j]){lab[j]=n;st.push(j)}}}
  if(c>best){best=c;bn=n}}
 const r=new Uint8Array(w*h);for(let i=0;i<w*h;i++)r[i]=lab[i]===bn?1:0;return r}
function cut(img){
 const W=img.naturalWidth,H=img.naturalHeight,c=document.createElement('canvas');c.width=W;c.height=H;
 const x=c.getContext('2d',{willReadFrequently:true});x.drawImage(img,0,0);const D=x.getImageData(0,0,W,H).data,out=[];
 for(let r=0;r<5;r++)for(let k=0;k<4;k++){
  const x0=Math.floor(k*W/4),x1=Math.floor((k+1)*W/4),y0=Math.floor(r*H/5),y1=Math.floor((r+1)*H/5),w=x1-x0,h=y1-y0;
  const bg=new Uint8Array(w*h);
  for(let y=0;y<h;y++)for(let xx=0;xx<w;xx++){const i=((y0+y)*W+x0+xx)*4,R=D[i],G=D[i+1],B=D[i+2],mn=Math.min(R,G,B),mx=Math.max(R,G,B);
   bg[y*w+xx]=(D[i+3]<128||(mn>=226&&mx-mn<=14))?1:0}
  const o1=flood(bg,w,h),fg=new Uint8Array(w*h);for(let i=0;i<w*h;i++)fg[i]=o1[i]?0:1;
  const m=lcc(fg,w,h),inv=new Uint8Array(w*h);for(let i=0;i<w*h;i++)inv[i]=m[i]?0:1;
  const o2=flood(inv,w,h);let a=w,b=h,cx=0,cy=0;
  for(let y=0;y<h;y++)for(let xx=0;xx<w;xx++)if(!o2[y*w+xx]){a=Math.min(a,xx);cx=Math.max(cx,xx);b=Math.min(b,y);cy=Math.max(cy,y)}
  const cw=cx-a+1,ch=cy-b+1,cv=document.createElement('canvas');cv.width=cw;cv.height=ch;const cc=cv.getContext('2d'),id=cc.createImageData(cw,ch);
  for(let y=0;y<ch;y++)for(let xx=0;xx<cw;xx++){if(o2[(y+b)*w+xx+a])continue;const s=((y0+y+b)*W+x0+xx+a)*4,d=(y*cw+xx)*4;id.data[d]=D[s];id.data[d+1]=D[s+1];id.data[d+2]=D[s+2];id.data[d+3]=255}
  cc.putImageData(id,0,0);out.push(cv)}
 return out}
function build(cvs){
 const mh=Math.max(...cvs.map(c=>c.height)),mw=Math.max(...cvs.map(c=>c.width)),sc=Math.min(126/mh,158/mw),fr=[];
 for(const cv of cvs){const w=Math.max(1,Math.round(cv.width*sc)),h=Math.max(1,Math.round(cv.height*sc)),t=document.createElement('canvas');t.width=w;t.height=h;
  const c=t.getContext('2d',{willReadFrequently:true});c.imageSmoothingEnabled=true;c.imageSmoothingQuality='high';c.drawImage(cv,0,0,w,h);
  fr.push({w,h,d:c.getImageData(0,0,w,h).data})}
 const hist=new Map(),key=(r,g,b)=>((r>>3)<<11)|((g>>2)<<5)|(b>>3);
 for(const f of fr)for(let i=0;i<f.d.length;i+=4)if(f.d[i+3]>128){const k=key(f.d[i],f.d[i+1],f.d[i+2]);hist.set(k,(hist.get(k)||0)+1)}
 const it=[...hist].map(([k,n])=>({k,n,c:[(k>>11&31)<<3,(k>>5&63)<<2,(k&31)<<3]}));
 let boxes=[it];
 while(boxes.length<255){let bi=-1,bc=0,br=0;
  boxes.forEach((b,i)=>{if(b.length<2)return;for(let c=0;c<3;c++){let lo=255,hi=0;for(const p of b){lo=Math.min(lo,p.c[c]);hi=Math.max(hi,p.c[c])}if(hi-lo>br){br=hi-lo;bi=i;bc=c}}});
  if(bi<0)break;const b=boxes[bi].sort((p,q)=>p.c[bc]-q.c[bc]);let tot=0;for(const p of b)tot+=p.n;let acc=0,cut=1;
  for(let i=0;i<b.length-1;i++){acc+=b[i].n;cut=i+1;if(acc>=tot/2)break}
  boxes.splice(bi,1,b.slice(0,cut),b.slice(cut))}
 const pal=boxes.map(b=>{let r=0,g=0,bl=0,t=0;for(const p of b){r+=p.c[0]*p.n;g+=p.c[1]*p.n;bl+=p.c[2]*p.n;t+=p.n}return[r/t,g/t,bl/t]});
 const cache=new Map(),near=(r,g,b)=>{const k=key(r,g,b);if(cache.has(k))return cache.get(k);let bd=1e9,bj=0;
  pal.forEach((p,j)=>{const d=(p[0]-r)**2+(p[1]-g)**2+(p[2]-b)**2;if(d<bd){bd=d;bj=j}});cache.set(k,bj+1);return bj+1};
 const frames=fr.map(f=>{const idx=new Uint8Array(f.w*f.h);for(let i=0;i<idx.length;i++)if(f.d[i*4+3]>128)idx[i]=near(f.d[i*4],f.d[i*4+1],f.d[i*4+2]);
  const rle=[];let i=0;while(i<idx.length){const v=idx[i];let n=1;while(i+n<idx.length&&idx[i+n]===v&&n<255)n++;rle.push(v,n);i+=n}
  return{w:f.w,h:f.h,idx,rle}});
 const total=4+510+20*6+frames.reduce((s,f)=>s+f.rle.length,0);
 const bin=new Uint8Array(total),dv=new DataView(bin.buffer);let o=0;
 'BPS1'.split('').forEach(ch=>bin[o++]=ch.charCodeAt(0));
 for(let j=0;j<255;j++){const p=pal[j]||[0,0,0];dv.setUint16(o,((p[0]|0)>>3<<11)|((p[1]|0)>>2<<5)|((p[2]|0)>>3),true);o+=2}
 for(const f of frames){bin[o++]=f.w;bin[o++]=f.h;dv.setUint32(o,f.rle.length,true);o+=4}
 for(const f of frames)for(const v of f.rle)bin[o++]=v;
 // previa
 const pv=$('pv'),g=pv.getContext('2d');pv.width=4*170;pv.height=5*135;g.fillStyle='#101828';g.fillRect(0,0,pv.width,pv.height);
 frames.forEach((f,n)=>{const t=document.createElement('canvas');t.width=f.w;t.height=f.h;const c=t.getContext('2d'),id=c.createImageData(f.w,f.h);
  for(let i=0;i<f.idx.length;i++){const v=f.idx[i];if(!v)continue;const p=pal[v-1];id.data[i*4]=p[0];id.data[i*4+1]=p[1];id.data[i*4+2]=p[2];id.data[i*4+3]=255}
  c.putImageData(id,0,0);g.drawImage(t,(n%4)*170+5,((n/4)|0)*135+5)});
 return bin}
$('fi').onchange=e=>{const f=e.target.files[0];if(!f)return;BIN=null;$('up').classList.add('h');$('pv').classList.add('h');msg('Convertendo...');
 const img=new Image();img.onload=()=>{try{BIN=build(cut(img));
   if(BIN.length>900000){msg('Imagem muito complexa ('+Math.round(BIN.length/1024)+' KB). Use um sprite mais simples.',1);BIN=null;return}
   $('pv').classList.remove('h');$('up').classList.remove('h');msg('Pronto: '+Math.round(BIN.length/1024)+' KB. Confira a previa e envie.')}
  catch(x){msg('Nao consegui ler a imagem: '+x.message,1)}};
 img.onerror=()=>msg('Arquivo de imagem invalido',1);img.src=URL.createObjectURL(f)};
</script></body></html>)HTML";
