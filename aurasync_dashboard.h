#pragma once

#include <Arduino.h>

static const char AURASYNC_DASHBOARD_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <meta name="theme-color" content="#07111f">
  <title>AuraSync Acoustic Intelligence</title>
  <style>
    :root{--bg:#06101c;--panel:#0d1b2a;--panel2:#10243a;--line:#20364c;--text:#ecf7ff;--muted:#8fa8bc;--cyan:#2ce6d0;--blue:#5fa8ff;--green:#42e695;--amber:#ffcb45;--orange:#ff8b3d;--red:#ff4f5e;--purple:#a78bfa}
    *{box-sizing:border-box}body{margin:0;background:radial-gradient(circle at 10% 0,#102d46 0,transparent 34%),radial-gradient(circle at 95% 10%,#142742 0,transparent 30%),var(--bg);color:var(--text);font-family:Inter,ui-sans-serif,system-ui,-apple-system,Segoe UI,sans-serif;min-height:100vh}
    header{height:72px;border-bottom:1px solid rgba(255,255,255,.08);display:flex;align-items:center;justify-content:space-between;padding:0 28px;background:rgba(6,16,28,.75);backdrop-filter:blur(14px);position:sticky;top:0;z-index:5}
    .brand{display:flex;align-items:center;gap:13px}.logo{width:38px;height:38px;border-radius:13px;background:linear-gradient(135deg,var(--cyan),var(--blue));box-shadow:0 0 30px rgba(44,230,208,.3);display:grid;place-items:center;color:#03131d;font-weight:900}.brand b{font-size:19px;letter-spacing:.2px}.brand span,.muted{color:var(--muted);font-size:12px}.live{display:flex;align-items:center;gap:8px;color:#b9cfdd;font-size:13px}.dot{width:9px;height:9px;border-radius:50%;background:var(--green);box-shadow:0 0 12px var(--green);animation:pulse 1.8s infinite}@keyframes pulse{50%{opacity:.45}}
    main{max-width:1280px;margin:auto;padding:24px}.source{padding:10px 14px;border:1px solid var(--line);background:rgba(16,36,58,.76);border-radius:12px;margin-bottom:18px;color:#c8dbea;font-size:13px;display:flex;justify-content:space-between;gap:12px}.source.demo{border-color:#7c5a18;background:#2d250f;color:#ffe39a}
    .hero{display:grid;grid-template-columns:1.15fr .85fr;gap:18px;margin-bottom:18px}.panel{background:linear-gradient(145deg,rgba(16,36,58,.96),rgba(9,23,37,.96));border:1px solid rgba(255,255,255,.085);border-radius:20px;box-shadow:0 18px 48px rgba(0,0,0,.22)}
    .statePanel{padding:26px;display:flex;align-items:center;justify-content:space-between;min-height:230px}.eyebrow{font-size:11px;text-transform:uppercase;letter-spacing:1.8px;color:var(--muted);margin-bottom:10px}.state{font-size:38px;font-weight:850;letter-spacing:-1px;margin-bottom:10px}.stateSub{color:#a9bfd0;max-width:460px;line-height:1.55}.chips{display:flex;gap:8px;flex-wrap:wrap;margin-top:20px}.chip{padding:7px 10px;border:1px solid var(--line);border-radius:999px;font-size:11px;color:#bcd0df;background:#0a1827}.gauge{--score:0;--gcolor:var(--green);width:156px;height:156px;border-radius:50%;background:conic-gradient(var(--gcolor) calc(var(--score)*1%),rgba(255,255,255,.07) 0);display:grid;place-items:center;box-shadow:0 0 38px color-mix(in srgb,var(--gcolor) 20%,transparent);position:relative;flex:0 0 auto}.gauge:after{content:"";position:absolute;inset:12px;border-radius:50%;background:#0b1c2d;border:1px solid rgba(255,255,255,.08)}.gaugeText{position:relative;z-index:1;text-align:center}.gaugeText strong{font-size:38px;display:block}.gaugeText span{font-size:10px;color:var(--muted);text-transform:uppercase;letter-spacing:1px}
    .cnn{padding:24px}.titleRow{display:flex;align-items:center;justify-content:space-between;margin-bottom:18px}.title{font-size:15px;font-weight:750}.badge{font-size:10px;padding:5px 8px;border:1px solid #2c5570;border-radius:999px;color:#8fd9ff}.barRow{margin:14px 0}.barLabel{display:flex;justify-content:space-between;font-size:12px;color:#c4d7e4;margin-bottom:7px}.bar{height:9px;background:#081522;border-radius:99px;overflow:hidden}.fill{height:100%;width:0;border-radius:99px;transition:width .4s ease}.f0{background:linear-gradient(90deg,#3ddc97,#2ce6d0)}.f1{background:linear-gradient(90deg,#5fa8ff,#a78bfa)}.f2{background:linear-gradient(90deg,#ffcb45,#ff8b3d)}.prediction{margin-top:18px;padding:12px;border-radius:12px;background:#081727;border:1px solid var(--line);font-size:12px;color:#bfd2df}
    .grid{display:grid;grid-template-columns:repeat(4,1fr);gap:14px;margin-bottom:18px}.card{padding:18px;min-height:116px}.label{font-size:11px;color:var(--muted);text-transform:uppercase;letter-spacing:1px}.value{font-size:27px;font-weight:800;margin:12px 0 3px}.unit{font-size:11px;color:#6f8ba0}.spark{height:3px;border-radius:3px;margin-top:12px;background:linear-gradient(90deg,var(--cyan),transparent)}
    .wide{display:grid;grid-template-columns:1.55fr .75fr;gap:18px}.chartPanel{padding:20px}.chartWrap{height:240px;position:relative}canvas{width:100%;height:100%;display:block}.side{padding:20px}.health{display:grid;grid-template-columns:1fr 1fr;gap:10px}.healthItem{padding:12px;border:1px solid var(--line);background:#081727;border-radius:12px}.healthItem span{display:block;color:var(--muted);font-size:10px;text-transform:uppercase}.healthItem b{display:block;font-size:15px;margin-top:6px}.controls{margin-top:18px;padding-top:16px;border-top:1px solid var(--line)}button,input{border-radius:10px;border:1px solid var(--line);background:#0a1b2c;color:var(--text);padding:10px 12px;font:inherit}button{cursor:pointer;background:linear-gradient(135deg,#153653,#10263d);font-weight:700;font-size:12px}button:hover{border-color:#47799c}input{width:90px}.controlRow{display:flex;gap:8px;margin-top:10px;flex-wrap:wrap}.foot{color:#668297;font-size:11px;text-align:center;padding:26px 0 10px;line-height:1.5}
    @media(max-width:900px){.hero,.wide{grid-template-columns:1fr}.grid{grid-template-columns:repeat(2,1fr)}.statePanel{min-height:0}.gauge{width:130px;height:130px}.state{font-size:31px}}
    @media(max-width:560px){header{padding:0 15px}.brand span{display:none}main{padding:14px}.statePanel{padding:20px;align-items:flex-start}.gauge{width:112px;height:112px}.gaugeText strong{font-size:29px}.state{font-size:27px}.grid{grid-template-columns:1fr 1fr;gap:10px}.card{padding:14px}.value{font-size:22px}.source{flex-direction:column}}
  </style>
</head>
<body>
<header><div class="brand"><div class="logo">A</div><div><b>AuraSync</b><br><span>Privacy-Preserving Acoustic Intelligence</span></div></div><div class="live"><i class="dot" id="dot"></i><span id="connection">Connecting</span></div></header>
<main>
  <div class="source" id="sourceBanner"><span id="sourceText">Waiting for device data...</span><span id="clock">--</span></div>
  <section class="hero">
    <div class="panel statePanel">
      <div><div class="eyebrow">Current acoustic state</div><div class="state" id="state">STARTING</div><div class="stateSub" id="stateSub">The edge pipeline is initializing DSP, CNN and microphone health checks.</div><div class="chips"><span class="chip" id="deviceChip">Device --</span><span class="chip" id="modelChip">Model --</span><span class="chip">No raw audio transmitted</span></div></div>
      <div class="gauge" id="gauge"><div class="gaugeText"><strong id="score">0</strong><span>Disturbance</span></div></div>
    </div>
    <div class="panel cnn"><div class="titleRow"><div class="title">Three-Class TinyCNN</div><div class="badge">EDGE INFERENCE</div></div>
      <div class="barRow"><div class="barLabel"><span>Stationary background</span><b id="p0t">0%</b></div><div class="bar"><div class="fill f0" id="p0"></div></div></div>
      <div class="barRow"><div class="barLabel"><span>Speech dominant</span><b id="p1t">0%</b></div><div class="bar"><div class="fill f1" id="p1"></div></div></div>
      <div class="barRow"><div class="barLabel"><span>Non-speech activity</span><b id="p2t">0%</b></div><div class="bar"><div class="fill f2" id="p2"></div></div></div>
      <div class="prediction">Detected context: <b id="prediction">waiting</b> · confidence <b id="confidence">0%</b> · inference <b id="infer">0 ms</b></div>
    </div>
  </section>
  <section class="grid">
    <div class="panel card"><div class="label">Estimated LAeq,1s</div><div class="value"><span id="laeq">--</span></div><div class="unit">dBA · calibrated estimate</div><div class="spark"></div></div>
    <div class="panel card"><div class="label">Peak level</div><div class="value"><span id="peak">--</span></div><div class="unit">dB · fast DSP</div><div class="spark"></div></div>
    <div class="panel card"><div class="label">Room baseline</div><div class="value"><span id="baseline">--</span></div><div class="unit">dBA · adaptive</div><div class="spark"></div></div>
    <div class="panel card"><div class="label">Signal above room</div><div class="value"><span id="snr">--</span></div><div class="unit">dB</div><div class="spark"></div></div>
    <div class="panel card"><div class="label">Speech-band ratio</div><div class="value"><span id="speechRatio">--</span></div><div class="unit">300–3400 Hz</div><div class="spark"></div></div>
    <div class="panel card"><div class="label">Spectral flux</div><div class="value"><span id="flux">--</span></div><div class="unit">fast change measure</div><div class="spark"></div></div>
    <div class="panel card"><div class="label">Crest factor</div><div class="value"><span id="crest">--</span></div><div class="unit">dB</div><div class="spark"></div></div>
    <div class="panel card"><div class="label">Spike events</div><div class="value"><span id="spikes">0</span></div><div class="unit">DSP-only detection</div><div class="spark"></div></div>
  </section>
  <section class="wide">
    <div class="panel chartPanel"><div class="titleRow"><div class="title">Live acoustic trend</div><div class="muted">last 60 seconds · score and LAeq</div></div><div class="chartWrap"><canvas id="chart"></canvas></div></div>
    <div class="panel side"><div class="titleRow"><div class="title">System health</div><div class="badge" id="healthBadge">CHECKING</div></div><div class="health">
      <div class="healthItem"><span>Microphone</span><b id="mic">--</b></div><div class="healthItem"><span>Mic quality</span><b id="micQuality">--</b></div>
      <div class="healthItem"><span>Free heap</span><b id="heap">--</b></div><div class="healthItem"><span>I2S errors</span><b id="errors">--</b></div>
      <div class="healthItem"><span>Uptime</span><b id="uptime">--</b></div><div class="healthItem"><span>Wi-Fi</span><b id="wifi">--</b></div>
    </div><div class="controls"><div class="label">Showcase controls</div><div class="controlRow"><button onclick="toggleDemo()" id="demoButton">Toggle labelled demo</button><input id="knownDb" type="number" min="30" max="120" step="0.1" value="65"><button onclick="calibrate()">Calibrate dBA</button></div><div class="muted" id="actionStatus" style="margin-top:9px">Calibration requires a reference sound-level meter beside the microphone.</div></div></div>
  </section>
  <div class="foot">AuraSync firmware <span id="firmware">--</span> · Hybrid DSP + TinyCNN · LOUD and SPIKE are generated only by DSP · Raw audio remains in volatile device memory.</div>
</main>
<script>
const q=id=>document.getElementById(id), histScore=[], histLaeq=[];let latest=null;
const stateInfo={SMOOTH:['Comfortable acoustic condition','#42e695'],PRE_WARNING:['Rising disturbance detected; observing persistence.','#ffcb45'],WARNING:['Persistent acoustic disturbance is present.','#ff8b3d'],LOUD:['Calibrated physical loudness threshold exceeded.','#ff4f5e'],RECOVERY:['Sound is reducing; waiting for stable recovery.','#5fa8ff'],SPIKE:['Fast impulsive event detected by DSP.','#a78bfa'],MIC_ERROR:['Microphone or wiring requires attention.','#ff4f5e']};
function pct(v){return Math.round((v||0)*100)}function num(v,n=1){return Number(v||0).toFixed(n)}
function formatUp(s){s=Math.floor(s||0);const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return(d?d+'d ':'')+h+'h '+m+'m'}
function apply(d){latest=d;const si=stateInfo[d.state]||['Waiting for classification','#5fa8ff'];q('state').textContent=(d.state||'UNKNOWN').replace('_','-');q('state').style.color=si[1];q('stateSub').textContent=si[0];q('score').textContent=Math.round(d.score);q('gauge').style.setProperty('--score',Math.max(0,Math.min(100,d.score)));q('gauge').style.setProperty('--gcolor',si[1]);
  const pp=[d.cnn.stationary_background,d.cnn.speech_dominant,d.cnn.non_speech_activity];pp.forEach((v,i)=>{q('p'+i).style.width=pct(v)+'%';q('p'+i+'t').textContent=pct(v)+'%'});q('prediction').textContent=(d.cnn.predicted||'waiting').replaceAll('_',' ');q('confidence').textContent=pct(d.cnn.confidence)+'%';q('infer').textContent=num(d.cnn.inference_ms,2)+' ms';
  q('laeq').textContent=num(d.dsp.laeq_1s_dba);q('peak').textContent=num(d.dsp.peak_db);q('baseline').textContent=num(d.dsp.baseline_dba);q('snr').textContent=num(d.dsp.snr_db);q('speechRatio').textContent=num(d.dsp.speech_band_ratio,2);q('flux').textContent=num(d.dsp.spectral_flux,3);q('crest').textContent=num(d.dsp.crest_db);q('spikes').textContent=d.counters.spikes;
  q('mic').textContent=d.health.mic_ok?'OK':'ERROR';q('micQuality').textContent=Math.round(d.health.mic_quality)+'%';q('heap').textContent=Math.round(d.health.free_heap/1024)+' KB';q('errors').textContent=d.health.i2s_errors;q('uptime').textContent=formatUp(d.uptime_s);q('wifi').textContent=d.network.mode;q('healthBadge').textContent=d.health.mic_ok?'HEALTHY':'CHECK MIC';q('healthBadge').style.color=d.health.mic_ok?'#42e695':'#ff4f5e';q('deviceChip').textContent='Device '+d.device_id;q('modelChip').textContent='Model '+d.model_version;q('firmware').textContent=d.firmware_version;
  q('sourceBanner').className='source '+(d.data_source==='simulation'?'demo':'');q('sourceText').textContent=d.data_source==='simulation'?'DEMO SIMULATION ACTIVE — values are visibly synthetic and not research measurements':'LIVE MICROPHONE — all audio processing is on-device';q('clock').textContent='Sequence '+d.sequence+' · '+new Date().toLocaleTimeString();
  histScore.push(d.score);histLaeq.push(d.dsp.laeq_1s_dba);if(histScore.length>120){histScore.shift();histLaeq.shift()}draw();}
async function update(){try{const r=await fetch('/api/status?'+Date.now());if(!r.ok)throw Error(r.status);apply(await r.json());q('connection').textContent='Live · '+new Date().toLocaleTimeString();q('dot').style.background='#42e695'}catch(e){q('connection').textContent='Reconnecting';q('dot').style.background='#ff4f5e'}}
async function post(url){const r=await fetch(url,{method:'POST'});return r.json()}
async function toggleDemo(){try{const d=await post('/api/demo/toggle');q('actionStatus').textContent=d.message;setTimeout(update,100)}catch(e){q('actionStatus').textContent='Action failed: '+e}}
async function calibrate(){const db=q('knownDb').value;if(!confirm('Use '+db+' dBA as the reference meter value?'))return;try{const d=await post('/api/calibrate?db='+encodeURIComponent(db));q('actionStatus').textContent=d.message}catch(e){q('actionStatus').textContent='Calibration failed: '+e}}
function draw(){const c=q('chart'),r=c.getBoundingClientRect(),ratio=devicePixelRatio||1;c.width=r.width*ratio;c.height=r.height*ratio;const x=c.getContext('2d');x.scale(ratio,ratio);const w=r.width,h=r.height,p=30;x.clearRect(0,0,w,h);x.strokeStyle='rgba(255,255,255,.07)';x.lineWidth=1;for(let i=0;i<=4;i++){let y=p+(h-2*p)*i/4;x.beginPath();x.moveTo(p,y);x.lineTo(w-p,y);x.stroke()}function line(data,color,min,max){if(data.length<2)return;x.strokeStyle=color;x.lineWidth=2;x.beginPath();data.forEach((v,i)=>{let px=p+(w-2*p)*i/Math.max(1,119),py=h-p-(Math.max(min,Math.min(max,v))-min)/(max-min)*(h-2*p);i?x.lineTo(px,py):x.moveTo(px,py)});x.stroke()}line(histScore,'#2ce6d0',0,100);line(histLaeq,'#ffcb45',30,100);x.fillStyle='#8fa8bc';x.font='11px sans-serif';x.fillText('Score',p,14);x.fillStyle='#2ce6d0';x.fillRect(p+36,7,20,2);x.fillStyle='#8fa8bc';x.fillText('LAeq',p+70,14);x.fillStyle='#ffcb45';x.fillRect(p+102,7,20,2)}
setInterval(update,500);addEventListener('resize',draw);update();
</script>
</body>
</html>
)HTML";

