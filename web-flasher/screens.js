// WarHog screen demos. Pig scenes are ported from PigWorld.cpp timings; sprites
// are decoded straight from pig_sprites.h (window.PIG, see pig.js). 20 fps.
(function(){
var W0=2,W1=3,W2=4,W3=5,STAND=0,BLINK=1,SNIFF1=6,CROUCH=8,JUMP=9,LAND=10,HAPPY=11;
function spr(ctx,bmp,ox,oy,color,flip,w){
  for(var y=0;y<bmp.length;y++){var row=bmp[y];for(var x=0;x<row.length;x++){if(row[x]==='1'){
    var dx=flip?(w-1-x):x; ctx.fillStyle=(typeof color==='function')?color(ox+dx,oy+y):color; ctx.fillRect(ox+dx,oy+y,1,1);
  }}}
}
function mountPig(el,o){
  var W=o.W,H=o.H,P=PIG[o.size],pw=P.w,ph=P.h;
  var cv=document.createElement('canvas');cv.width=W;cv.height=H;
  cv.style.width='100%';cv.style.imageRendering='pixelated';cv.style.display='block';
  el.appendChild(cv);var ctx=cv.getContext('2d');
  var two=o.two, split=16, g=o.ground, px=o.pigX, hopH=o.hopH;
  var ink=function(x,y){ return two ? (y<split? o.sky : o.land) : o.land; };
  var trees=[]; for(var i=0;i<4;i++) trees.push({wx:40+i*52, t:(i%2?'oak':'pine')});
  var clouds=[{x:W*0.2,y:3,s:0.18,sp:'cloud1'},{x:W*0.7,y:7,s:0.10,sp:'cloud2'},{x:W*1.1,y:2,s:0.07,sp:'cloud3'}];
  var birds=null, birdT=200+Math.random()*400;
  var tick=0, worldX=0, hopAt=60+Math.random()*60, st=null;
  function pigFrame(){
    if(st){ var k=st.k;
      if(k<2) return {f:CROUCH,dy:0,bang:false};
      if(k<14){var p=(k-2)/12; return {f:JUMP,dy:-Math.round(Math.sin(Math.PI*p)*hopH),bang:true};}
      if(k<16) return {f:LAND,dy:0,bang:false};
      if(k<26) return {f:HAPPY,dy:0,bang:false};
      return {f:STAND,dy:0,bang:false};
    }
    var ph=Math.floor(tick/2)%4; var f=[W0,W1,W2,W3][ph];
    if(tick%140<2) f=BLINK;
    return {f:f,dy:0,bang:false};
  }
  function drawGround(){
    ctx.fillStyle=o.land; ctx.fillRect(0,g,W,1);
    for(var x=-(worldX%6);x<W;x+=6){ ctx.fillRect(x,g+2,1,1); }
  }
  function step(){
    ctx.fillStyle=o.bg; ctx.fillRect(0,0,W,H);
    if(two){ ctx.fillStyle=o.skyTint; ctx.fillRect(0,0,W,split-1);
      ctx.fillStyle=o.sky; var sx=W-26, sy=8, r=4;
      ctx.beginPath(); ctx.arc(sx,sy,r,0,7); ctx.fill();
      for(var d=0;d<8;d++){var ang=d*Math.PI/4; ctx.fillRect(Math.round(sx+Math.cos(ang)*(r+2)),Math.round(sy+Math.sin(ang)*(r+2)),1,1);}
    }
    for(var c=0;c<clouds.length;c++){var cl=clouds[c]; var S=PIG[cl.sp]; var cx=Math.round(cl.x);
      spr(ctx,S.d,cx,cl.y,o.sky,false,S.w); cl.x-=cl.s; if(cl.x< -S.w) cl.x=W+Math.random()*10;}
    drawGround();
    for(var t=0;t<trees.length;t++){var tr=trees[t]; var T=PIG[tr.t]; var sx2=Math.round(tr.wx-worldX*1.0);
      var span=trees.length*52; var rx=((sx2%span)+span)%span - 10;
      if(rx>-T.w && rx<W) spr(ctx,T.d,rx,g-T.h+1,o.land,false,T.w);
    }
    if(two){ if(!birds && tick>birdT){birds={x:W+4,y:4+Math.random()*7}; }
      if(birds){var bf=[PIG.birdU,PIG.birdM,PIG.birdD][Math.floor(tick/2)%3]; spr(ctx,bf.d,Math.round(birds.x),birds.y,o.sky,false,bf.w); birds.x-=0.8; if(birds.x<-8){birds=null;birdT=tick+200+Math.random()*400;}}}
    var pf=pigFrame();
    spr(ctx,P.f[pf.f],px,g-ph+1+pf.dy,ink,false,pw);
    if(pf.bang){ ctx.fillStyle=o.land; ctx.fillRect(px+pw+1,g-ph-3+pf.dy,1,3); ctx.fillRect(px+pw+1,g-ph+1+pf.dy,1,1);}
    if(!st){ worldX+=o.scroll; }
    tick++;
    if(st){ st.k++; if(st.k>=28){ st=null; hopAt=tick+60+Math.random()*80; } }
    else if(tick>hopAt){ st={k:0}; }
  }
  step(); return setInterval(step, 50);
}
function mountCyd(el){
  el.innerHTML=''
  +'<div class="cyd">'
  +'<div class="cyd-hd"><span class="cyd-pig">🐷</span><span class="cyd-ttl">WARHOG<b>CORE</b></span>'
  +'<span class="cyd-bat"><i></i>4.12V</span>'
  +'<span class="cyd-dots"><u class="y"></u>GPS <u></u>SD <u></u>RF</span></div>'
  +'<div class="cyd-sec">RADIO / MESH ROLE</div>'
  +'<div class="cyd-seg"><s>SOLO</s><s class="on">CORE</s><s>NODE</s><s>WEB</s><s>OFF</s></div>'
  +'<div class="cyd-sec">STATUS</div>'
  +'<div class="cyd-stat">CORE&nbsp;&nbsp;<em>0</em> workers<br>RX <em>0</em>&nbsp;&nbsp;&nbsp;admin ch 6</div>'
  +'<div class="cyd-sec">CAPTURE</div>'
  +'<div class="cyd-cap"><s class="on">2.4G ON</s><s class="on">5G ON</s><s class="on">BLE ON</s></div>'
  +'<div class="cyd-cards">'
  +'<div class="cc a"><label>2.4 GHZ</label><b>0</b></div>'
  +'<div class="cc b"><label>5 GHZ</label><b>0</b></div>'
  +'<div class="cc p"><label>BLE</label><b>0</b></div></div>'
  +'<div class="cyd-cards2">'
  +'<div class="cc a"><label>SCANS</label><b>0</b></div>'
  +'<div class="cc a"><label>CSV ROWS</label><b>0</b></div></div>'
  +'<div class="cyd-nav"><u class="on">HOME</u><u>FILE</u><u>SYS</u><u>MRDR</u></div>'
  +'</div>';
}
function mountWave(el){
  el.innerHTML=''
  +'<div class="wave">'
  +'<div class="wv-hd"><span class="wv-ttl">WarHog</span>'
  +'<span class="wv-ic"><b class="arr"></b><b class="sd"></b><b class="gps"></b></span></div>'
  +'<div class="wv-l"><span>Scan:</span> ACTIVE</div>'
  +'<div class="wv-l"><span>SD:</span> OK</div>'
  +'<div class="wv-l"><span>GPS:</span> LOCK</div>'
  +'<div class="wv-l"><span>STA:</span> NO</div>'
  +'<div class="wv-l"><span>AP:</span> 192.168.4.1 <em class="ap">37</em>s</div>'
  +'<div class="wv-wig">WiGLE: Unknown<br><span>-</span></div>'
  +'<div class="wv-big">2.4G: <em class="n2g">0</em></div>'
  +'<div class="wv-nav"><span class="wv-tri"></span><em>0 km/h</em></div>'
  +'</div>';
  var ap=37,n=1203,k=0;
  return setInterval(function(){ k++;
    if(k%10===0&&ap>0){ap--; var e=el.querySelector('.ap'); if(e)e.textContent=ap;}
    if(k%4===0){ n+=Math.floor(Math.random()*3); var e2=el.querySelector('.n2g'); if(e2)e2.textContent=n; }
  },500);
}
window.mountPigScreens=function(root){
  (root||document).querySelectorAll('[data-screen]').forEach(function(el){
    if(el.dataset.mounted)return; el.dataset.mounted='1';
    var k=el.getAttribute('data-screen');
    if(k==='cyd'){ mountCyd(el); return; }
    if(k==='wave'){ mountWave(el); return; }
    if(k==='warhog'){ mountPig(el,{size:'L',W:128,H:64,two:true,bg:'#060709',skyTint:'#1a1403',sky:'#ffcf3a',land:'#39bffb',ground:52,pigX:22,hopH:13,scroll:1}); return; }
    if(k==='c3'){ mountPig(el,{size:'S',W:72,H:40,two:false,bg:'#060709',sky:'#a9e7ff',land:'#a9e7ff',ground:32,pigX:14,hopH:8,scroll:1}); return; }
  });
};
})();
