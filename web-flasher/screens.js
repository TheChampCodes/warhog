// PigWorld screen demos. window.PIG holds decoded sprites (see pig.js).
(function(){
function spr(ctx,bmp,ox,oy,color){ // color: fn(x,y)->css or string
  for(let y=0;y<bmp.length;y++){const row=bmp[y];for(let x=0;x<row.length;x++){if(row[x]==='1'){ctx.fillStyle=(typeof color==='function')?color(ox+x,oy+y):color;ctx.fillRect(ox+x,oy+y,1,1);}}}
}
function mountOled(el,opts){
  const W=opts.W,H=opts.H,pig=PIG[opts.pig];
  const cv=document.createElement('canvas');cv.width=W;cv.height=H;
  cv.style.width='100%';cv.style.imageRendering='pixelated';cv.style.display='block';
  el.appendChild(cv);const ctx=cv.getContext('2d');
  const ground=opts.ground, split=opts.split;
  const inkTop=opts.inkTop, inkBot=opts.inkBot;
  const ink=(x,y)=> (split!=null && y<split)?inkTop:inkBot;
  let t=0, px=-pig.w, frame=0, cloudX=W;
  const pine=PIG.pine, cloud=PIG.cloud, truf=PIG.truffle;
  function step(){
    ctx.fillStyle=opts.bg;ctx.fillRect(0,0,W,H);
    if(split!=null){ctx.fillStyle=opts.skyTint;ctx.fillRect(0,0,W,split-1);}
    const cy=split!=null?2:3;
    spr(ctx,cloud.d, (cloudX|0), cy, inkTop);
    spr(ctx,cloud.d, ((cloudX+ (split!=null?70:40))%(W+20)) -10|0, cy+ (split!=null?4:0), inkTop);
    ctx.fillStyle=inkBot;ctx.fillRect(0,ground,W,1);
    if(opts.pine!==false) spr(ctx,pine.d, opts.pineX, ground-pine.h+1, inkBot);
    if(opts.truffle!==false) spr(ctx,truf.d, opts.trufX, ground-truf.h+1, inkBot);
    const f = (px< -pig.w+2)?0 : (1+(frame%4));
    spr(ctx, pig.f[f], px|0, ground-pig.h+1, ink);
    px+=opts.speed; if(px>W+2){px=-pig.w;}
    frame++; cloudX-=0.35; if(cloudX< -16) cloudX=W+4; t++;
  }
  step();return setInterval(step, opts.fps||140);
}
function mountTft(el){
  el.innerHTML=
  '<div class="tft">'
  +'<div class="tft-hd"><span class="tft-pig">🐷 PIGLET</span><span class="tft-sig">▮▮▮▯</span></div>'
  +'<div class="tft-body"><div class="radar"><span></span><span></span><span></span><b></b></div>'
  +'<div class="tft-kv"><div><label>NETS</label><em class="_nets">0</em></div><div><label>GPS</label><em>3D FIX</em></div><div><label>SATS</label><em class="_sats">7</em></div></div></div>'
  +'<div class="tft-nav"><u class="on">HOME</u><u>FILE</u><u>SYS</u><u>MRDR</u></div>'
  +'</div>';
  let n=1428, sats=7, k=0;
  return setInterval(()=>{ n+=Math.floor(Math.random()*3); el.querySelector('._nets').textContent=n.toLocaleString();
    k++; if(k%7===0){sats=6+Math.floor(Math.random()*4); el.querySelector('._sats').textContent=sats;}
  }, 700);
}
window.mountPigScreens=function(root){
  (root||document).querySelectorAll('[data-screen]').forEach(el=>{
    if(el.dataset.mounted)return; el.dataset.mounted='1';
    const k=el.getAttribute('data-screen');
    if(k==='tft'){mountTft(el);return;}
    if(k==='oled2'){mountOled(el,{W:128,H:64,pig:'L',bg:'#060709',skyTint:'#1a1403',split:16,inkTop:'#ffcf3a',inkBot:'#39bffb',ground:52,pineX:96,trufX:60,speed:0.9,fps:150});return;}
    if(k==='oledmono'){mountOled(el,{W:72,H:40,pig:'S',bg:'#060709',split:null,inkTop:'#a9e7ff',inkBot:'#a9e7ff',ground:32,pine:false,trufX:56,speed:0.7,fps:160});return;}
  });
};
})();
