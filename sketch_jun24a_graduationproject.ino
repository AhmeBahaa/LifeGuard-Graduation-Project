/*
 * LifeGuard Smart Health Monitor — Production Version
 * Sensors: MLX90614 (temp), MAX30105 (HR/SpO2), MPU6050 (motion), AD8232 (ECG)
 * Features: Live web dashboard, n8n production webhook, SSL bypass, status LEDs
 */

#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h> 
#include <Adafruit_Sensor.h>
#include <math.h>
#include "MAX30105.h"
#include "heartRate.h"
#include <Adafruit_MLX90614.h>

// ── WiFi & Production Webhook ──────────────────────────────────────────────
const char* n8n_webhook = "https://d2c96ee7.kube-ops.com/webhook/sensor-data"; 
const char* ssid        = "Mohamed Adel";
const char* password    = "22222222";

// ── Pin Definitions ─────────────────────────────────────────────────────────
#define ECG_OUTPUT_PIN   35   
#define ECG_LO_PLUS_PIN  32   
#define ECG_LO_MINUS_PIN 33   

// ── LED Pins ──────────────────────────────────────────────────────────────
#define LED_POWER_PIN 26
#define LED_N8N_PIN   27
#define LED_WIFI_PIN  14
unsigned long n8nLedTimer = 0; 

// ── MPU6050 direct I2C ──────────────────────────────────────────────────────
#define MPU_ADDR 0x68

// ── Global Objects ──────────────────────────────────────────────────────────
WebServer         server(80);
MAX30105          particleSensor;
Adafruit_MLX90614 mlx;

// ── Sensor Status Flags ─────────────────────────────────────────────────────
bool mpuOK = false;
bool mlxOK = false;
bool maxOK = false;

// ── Vitals ──────────────────────────────────────────────────────────────────
float  objectTemp  = 0.0;
float  ambientTemp = 0.0;
String motion      = "Stable";
int    beatAvg     = 0;
int    approxSpO2  = 0;
long   lastBeat    = 0;
long   irRawVal    = 0;

const byte RATE_SIZE = 4;
byte rates[RATE_SIZE];
byte rateSpot = 0;

// ── ECG circular buffer ─────────────────────────────────────────────────────
#define ECG_BUF_SIZE 200
volatile int  ecgBuf[ECG_BUF_SIZE];
volatile int  ecgHead = 0;   
int           ecgValue  = 0;
String        ecgStatus = "Normal";

// ── Timing ──────────────────────────────────────────────────────────────────
unsigned long lastPostTime  = 0;
const long    postInterval  = 5000;

// ═══════════════════════════════════════════════════════════════════════════
// HTML DASHBOARD
// ═══════════════════════════════════════════════════════════════════════════
const char INDEX_HTML[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1.0">
<title>LifeGuard Monitor</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
:root{
  --bg:#0d1117;--card:#161b22;--border:#30363d;
  --red:#ff4d6d;--cyan:#00e5ff;--orange:#ffb300;
  --green:#00e676;--grey:#8b949e;--purple:#c084fc
}
body{background:var(--bg);color:#e6edf3;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;min-height:100vh}
header{background:#0d1117;border-bottom:1px solid var(--border);padding:14px 18px;
  display:flex;align-items:center;justify-content:space-between;position:sticky;top:0;z-index:10}
.title{font-size:1.1rem;font-weight:700;display:flex;align-items:center;gap:8px}
.badge{font-size:.7rem;font-weight:800;letter-spacing:.6px;padding:4px 10px;border-radius:20px;transition:all .4s}
.badge.online{background:rgba(0,230,118,.12);color:var(--green);border:1px solid rgba(0,230,118,.4)}
.badge.offline{background:rgba(255,77,109,.12);color:var(--red);border:1px solid rgba(255,77,109,.4)}
.ts{font-size:.7rem;color:var(--grey);margin-top:2px;text-align:right}
main{padding:16px;display:grid;grid-template-columns:1fr 1fr;gap:14px;max-width:600px;margin:0 auto}
.card{background:var(--card);border:1.5px solid var(--border);border-radius:18px;padding:16px;
  display:flex;flex-direction:column;gap:6px;transition:box-shadow .35s,border-color .35s;overflow:hidden}
.card.full{grid-column:span 2}
.card.warn-red   {border-color:rgba(255,77,109,.7);box-shadow:0 0 22px rgba(255,77,109,.18)}
.card.warn-cyan  {border-color:rgba(0,229,255,.7); box-shadow:0 0 22px rgba(0,229,255,.18)}
.card.warn-orange{border-color:rgba(255,179,0,.7); box-shadow:0 0 22px rgba(255,179,0,.18)}
.card.warn-purple{border-color:rgba(192,132,252,.7);box-shadow:0 0 22px rgba(192,132,252,.18)}
.card.warn-fall  {border-color:var(--red);animation:fp 1s infinite alternate}
@keyframes fp{from{box-shadow:0 0 10px rgba(255,77,109,.3)}to{box-shadow:0 0 28px rgba(255,77,109,.7)}}
.card-top{display:flex;align-items:center;justify-content:space-between;gap:6px;width:100%}
.card-label{display:flex;align-items:center;gap:6px}
.emoji{font-size:1.2rem;line-height:1}
.label{font-size:.72rem;font-weight:700;letter-spacing:.3px;text-transform:uppercase;opacity:.8}
.stag{font-size:.7rem;font-weight:600;padding:2px 8px;border-radius:10px}
.stag.ok{background:rgba(0,230,118,.1);color:var(--green)}
.stag.warn{background:rgba(255,179,0,.1);color:var(--orange)}
.stag.err{background:rgba(255,77,109,.1);color:var(--red)}
.value{font-size:2.1rem;font-weight:800;font-family:'Courier New',monospace;line-height:1.1}
.unit{font-size:.75rem;color:var(--grey);margin-top:-4px}
.wt{font-size:.68rem;font-weight:700;border-radius:6px;padding:2px 6px;margin-top:2px;display:none}
.card.warn-red     .wt,.card.warn-cyan .wt,.card.warn-orange .wt,
.card.warn-purple .wt,.card.warn-fall .wt{display:inline-block}
canvas{width:100%;height:36px;display:block;margin-top:4px;border-radius:4px}
.mlx-row{display:flex;gap:12px;margin-top:4px}
.mlx-sub{flex:1;background:#0d1117;border-radius:10px;padding:8px 10px}
.mlx-sub-lbl{font-size:.62rem;color:var(--grey);text-transform:uppercase;letter-spacing:.3px}
.mlx-sub-val{font-size:1.3rem;font-weight:800;font-family:'Courier New',monospace}
.ecg-wrap{position:relative;margin-top:8px;border-radius:8px;overflow:hidden;background:#0a0f14;border:1px solid var(--border)}
canvas.ecg{width:100%;height:140px;display:block}
#fo{display:none;position:fixed;inset:0;background:rgba(200,0,40,.9);z-index:100;flex-direction:column;align-items:center;justify-content:center;gap:18px}
#fo.show{display:flex}
#fo h1{font-size:2rem;font-weight:900;text-align:center}
#fo p{font-size:1rem;opacity:.85;text-align:center;max-width:280px}
#fo button{background:#fff;color:#c0002a;font-weight:800;border:none;border-radius:14px;padding:14px 36px;font-size:1rem;cursor:pointer}
</style>
</head>
<body>
<header>
  <div>
    <div class="title">
      <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="#00e5ff" stroke-width="2" stroke-linecap="round">
        <polyline points="22 12 18 12 15 21 9 3 6 12 2 12"/>
      </svg>
      LifeGuard Monitor
    </div>
    <div class="ts" id="ts">--</div>
  </div>
  <div><div class="badge offline" id="badge">OFFLINE</div></div>
</header>
<main>
  <div class="card" id="c-hr">
    <div class="card-top">
      <div class="card-label"><span class="emoji">❤️</span><span class="label" style="color:var(--red)">Heart Rate</span></div>
      <span class="stag ok" id="st-max">OK</span>
    </div>
    <div class="value" id="v-hr">--</div>
    <div class="unit">BPM</div>
    <div class="wt" style="background:rgba(255,77,109,.18);color:var(--red)" id="wt-hr"></div>
    <canvas id="sp-hr"></canvas>
  </div>
  <div class="card" id="c-spo2">
    <div class="card-top">
      <div class="card-label"><span class="emoji">💨</span><span class="label" style="color:var(--cyan)">SpO2</span></div>
    </div>
    <div class="value" id="v-spo2">--</div>
    <div class="unit">%</div>
    <div class="wt" style="background:rgba(0,229,255,.18);color:var(--cyan)" id="wt-spo2"></div>
    <canvas id="sp-spo2"></canvas>
  </div>
  <div class="card full" id="c-mlx">
    <div class="card-top">
      <div class="card-label"><span class="emoji">🌡️</span><span class="label" style="color:var(--purple)">IR Temperature (MLX90614)</span></div>
      <span class="stag ok" id="st-mlx">OK</span>
    </div>
    <div class="mlx-row">
      <div class="mlx-sub">
        <div class="mlx-sub-lbl">Object (Body)</div>
        <div class="mlx-sub-val" id="v-obj" style="color:var(--purple)">--</div>
        <div style="font-size:.7rem;color:var(--grey)">°C</div>
      </div>
      <div class="mlx-sub">
        <div class="mlx-sub-lbl">Ambient</div>
        <div class="mlx-sub-val" id="v-amb" style="color:var(--grey)">--</div>
        <div style="font-size:.7rem;color:var(--grey)">°C</div>
      </div>
    </div>
    <div class="wt" style="background:rgba(192,132,252,.18);color:var(--purple)" id="wt-mlx"></div>
    <canvas id="sp-mlx"></canvas>
  </div>
  <div class="card" id="c-mot">
    <div class="card-top">
      <div class="card-label"><span class="emoji">🚶</span><span class="label" style="color:var(--green)">Motion</span></div>
      <span class="stag ok" id="st-mpu">OK</span>
    </div>
    <div class="value" id="v-mot" style="color:#fff;font-size:1.1rem;margin-top:6px">--</div>
    <div class="wt" style="background:rgba(255,77,109,.18);color:var(--red)" id="wt-mot">⚠️ FALL DETECTED!</div>
    <canvas id="sp-mot"></canvas>
  </div>
  <div class="card full" id="c-ecg">
    <div class="card-top">
      <div class="card-label"><span class="emoji">⚡</span><span class="label" style="color:var(--green)">ECG Waveform</span></div>
      <span class="stag ok" id="st-ecg">Normal</span>
    </div>
    <div class="ecg-wrap"><canvas class="ecg" id="ecg-cv"></canvas></div>
  </div>
</main>
<div id="fo">
  <h1>🚨 FALL DETECTED!</h1>
  <p>Patient may have fallen. Check immediately.</p>
  <button onclick="dismissFall()">Dismiss Alert</button>
</div>
<script>
const HIST=24; const hist={hr:[],spo2:[],mlx:[],mot:[]}; let failCount=0, fallDismissed=false;
function spark(id,data,color){
  const c=document.getElementById(id); if(!c||data.length<2)return;
  const dpr=devicePixelRatio||1,W=c.clientWidth,H=36; c.width=W*dpr; c.height=H*dpr;
  const ctx=c.getContext('2d'); ctx.scale(dpr,dpr);
  const mn=Math.min(...data),mx=Math.max(...data),rng=mx-mn||1;
  const pts=data.map((v,i)=>({x:(i/(data.length-1))*W,y:H-((v-mn)/rng)*(H-4)-2}));
  const g=ctx.createLinearGradient(0,0,0,H); g.addColorStop(0,color+'55'); g.addColorStop(1,color+'00');
  ctx.beginPath(); ctx.moveTo(pts[0].x,H); pts.forEach(p=>ctx.lineTo(p.x,p.y)); ctx.lineTo(pts[pts.length-1].x,H); ctx.closePath(); ctx.fillStyle=g; ctx.fill();
  ctx.beginPath(); pts.forEach((p,i)=>i===0?ctx.moveTo(p.x,p.y):ctx.lineTo(p.x,p.y)); ctx.strokeStyle=color; ctx.lineWidth=1.8; ctx.lineJoin='round'; ctx.stroke();
}
function push(arr,v,lim=HIST){arr.push(v);if(arr.length>lim)arr.shift();}
const ECG_WIN = 300; const ecgSamples = []; let ecgFetching = false;
async function fetchEcg(){
  if(ecgFetching) return; ecgFetching = true;
  try{
    const d = await (await fetch('/ecg',{cache:'no-store'})).json(); const stEl = document.getElementById('st-ecg');
    if(d.status !== 'Normal'){ stEl.className='stag err'; stEl.textContent='Leads Off'; } 
    else { stEl.className='stag ok'; stEl.textContent='Normal'; d.samples.forEach(v=>{ ecgSamples.push(v); if(ecgSamples.length>ECG_WIN*3) ecgSamples.shift(); }); }
    drawEcg();
  }catch(_){} ecgFetching = false;
}
function drawEcg(){
  const c = document.getElementById('ecg-cv'); if(!c) return;
  const dpr=devicePixelRatio||1, W=c.clientWidth, H=140; c.width=W*dpr; c.height=H*dpr; const ctx=c.getContext('2d'); ctx.scale(dpr,dpr);
  ctx.fillStyle='#0a0f14'; ctx.fillRect(0,0,W,H); ctx.strokeStyle='rgba(255,255,255,0.04)'; ctx.lineWidth=1;
  for(let x=0;x<W;x+=20){ctx.beginPath();ctx.moveTo(x,0);ctx.lineTo(x,H);ctx.stroke();}
  for(let y=0;y<H;y+=20){ctx.beginPath();ctx.moveTo(0,y);ctx.lineTo(W,y);ctx.stroke();}
  ctx.strokeStyle='rgba(0,230,118,0.08)'; ctx.lineWidth=1; ctx.beginPath(); ctx.moveTo(0,H/2); ctx.lineTo(W,H/2); ctx.stroke();
  if(ecgSamples.length < 2) return; const view = ecgSamples.slice(-W); const mn=Math.min(...view), mx=Math.max(...view), rng=mx-mn||1;
  for(let pass=0;pass<2;pass++){
    ctx.beginPath(); view.forEach((v,i)=>{ const x=i; const y=H-((v-mn)/rng)*(H*0.85)-(H*0.075); i===0?ctx.moveTo(x,y):ctx.lineTo(x,y); });
    ctx.strokeStyle = pass===0 ? 'rgba(0,230,118,0.25)' : '#00e676'; ctx.lineWidth = pass===0 ? 4 : 1.5; ctx.lineJoin='round'; ctx.stroke();
  }
}
async function fetchData(){
  try{ const d=await(await fetch('/data',{cache:'no-store'})).json(); failCount=0; updateUI(d); }catch(e){ if(++failCount>=3) setOffline(); }
}
function setOffline(){ document.getElementById('badge').className='badge offline'; document.getElementById('badge').textContent='OFFLINE'; document.getElementById('ts').textContent='Connection lost'; }
function updateUI(d){
  const badge=document.getElementById('badge'); badge.className='badge online'; badge.textContent='ONLINE'; document.getElementById('ts').textContent='Updated '+new Date().toLocaleTimeString();
  const maxSt=document.getElementById('st-max'); if(d.max_error){maxSt.className='stag err';maxSt.textContent='Error';} else if(d.ir_raw<7000){maxSt.className='stag warn';maxSt.textContent='No Finger';} else{maxSt.className='stag ok';maxSt.textContent='OK';}
  const hr=d.heart_rate??0; document.getElementById('v-hr').textContent=hr; push(hist.hr,hr); const cHr=document.getElementById('c-hr'),wHr=document.getElementById('wt-hr'); cHr.className='card'; if(hr>100){cHr.classList.add('warn-red');wHr.textContent='⚠️ Too High!';} else if(hr<60&&hr>0){cHr.classList.add('warn-red');wHr.textContent='⚠️ Too Low!';} spark('sp-hr',hist.hr,'#ff4d6d');
  const spo2=d.spo2??0; document.getElementById('v-spo2').textContent=spo2; push(hist.spo2,spo2); const cS=document.getElementById('c-spo2'),wS=document.getElementById('wt-spo2'); cS.className='card'; if(spo2>0&&spo2<95){cS.classList.add('warn-cyan');wS.textContent='⚠️ Low Oxygen!';} spark('sp-spo2',hist.spo2,'#00e5ff');
  const mlxSt=document.getElementById('st-mlx'); const cMlx=document.getElementById('c-mlx'),wMlx=document.getElementById('wt-mlx');
  if(d.mlx_error){ mlxSt.className='stag err'; mlxSt.textContent='Error'; document.getElementById('v-obj').textContent='ERR'; document.getElementById('v-amb').textContent='ERR'; }
  else{ mlxSt.className='stag ok'; mlxSt.textContent='OK'; const obj=parseFloat(d.mlx_object??0), amb=parseFloat(d.mlx_ambient??0); document.getElementById('v-obj').textContent=obj.toFixed(1); document.getElementById('v-amb').textContent=amb.toFixed(1); push(hist.mlx,obj); cMlx.className='card full'; if(obj>37.5){cMlx.classList.add('warn-purple');wMlx.textContent='⚠️ High Body Temp!';} spark('sp-mlx',hist.mlx,'#c084fc'); }
  const mpuSt=document.getElementById('st-mpu'); const cM=document.getElementById('c-mot'),mEl=document.getElementById('v-mot'); const motion=d.motion??'--';
  if(d.mpu_error){ mpuSt.className='stag err'; mpuSt.textContent='Error'; mEl.textContent='Sensor Error'; mEl.style.color='var(--red)'; }
  else{ mpuSt.className='stag ok'; mpuSt.textContent='OK'; mEl.textContent=motion; cM.className='card'; const mv=motion==='Stable'?0:motion==='Moving'?1:motion==='Abnormal Shaking'?2:3; push(hist.mot,mv); if(motion==='FALL DETECTED!'){ cM.classList.add('warn-fall'); mEl.style.color='var(--red)'; if(!fallDismissed)document.getElementById('fo').classList.add('show'); }else{ mEl.style.color='#fff'; } spark('sp-mot',hist.mot,'#00e676'); }
}
function dismissFall(){ document.getElementById('fo').classList.remove('show'); fallDismissed=true; setTimeout(()=>fallDismissed=false,10000); }
fetchData(); fetchEcg(); setInterval(fetchData, 500); setInterval(fetchEcg,  20);
</script>
</body>
</html>
)=====";

// ═══════════════════════════════════════════════════════════════════════════
// HTTP HANDLERS
// ═══════════════════════════════════════════════════════════════════════════
void handleRoot() { server.send_P(200, "text/html", INDEX_HTML); }

void handleData() {
  String json = "{";
  json += "\"heart_rate\":"   + String(beatAvg)          + ",";
  json += "\"spo2\":"         + String(approxSpO2)        + ",";
  json += "\"motion\":\""     + motion                    + "\",";
  json += "\"mlx_object\":"   + String(objectTemp,  1)   + ",";
  json += "\"mlx_ambient\":"  + String(ambientTemp, 1)   + ",";
  json += "\"mlx_error\":"    + String(mlxOK ? "false" : "true") + ",";
  json += "\"mpu_error\":"    + String(mpuOK ? "false" : "true") + ",";
  json += "\"max_error\":"    + String(maxOK ? "false" : "true") + ",";
  json += "\"ir_raw\":"       + String(irRawVal);
  json += "}";
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

void handleEcg() {
  String json = "{\"status\":\""; json += ecgStatus; json += "\",\"samples\":[";
  noInterrupts();
  int head = ecgHead; int snap[ECG_BUF_SIZE];
  for (int i = 0; i < ECG_BUF_SIZE; i++) { snap[i] = ecgBuf[(head + i) % ECG_BUF_SIZE]; }
  interrupts();
  for (int i = 0; i < ECG_BUF_SIZE; i++) { json += String(snap[i]); if (i < ECG_BUF_SIZE - 1) json += ","; }
  json += "]}";
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

// ═══════════════════════════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);

  pinMode(LED_POWER_PIN, OUTPUT);
  pinMode(LED_N8N_PIN, OUTPUT);
  pinMode(LED_WIFI_PIN, OUTPUT);

  digitalWrite(LED_POWER_PIN, HIGH); 

  Wire.begin(21, 22);
  Wire.setClock(100000);

  // ── MPU6050 ──────────────────────────────────
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B); Wire.write(0x00);
  if (Wire.endTransmission(true) == 0) { mpuOK = true; Serial.println("MPU6050 OK"); } 
  else { Serial.println("MPU6050 FAIL"); }

  // ── AD8232 ───────────────────────────────────
  pinMode(ECG_LO_PLUS_PIN,  INPUT); pinMode(ECG_LO_MINUS_PIN, INPUT);

  // ── WiFi Connection Loop with LED Blink ──────────────────────────────────
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) { 
    digitalWrite(LED_WIFI_PIN, !digitalRead(LED_WIFI_PIN)); 
    delay(250); Serial.print("."); 
  }
  digitalWrite(LED_WIFI_PIN, LOW); 
  Serial.println("\nWiFi Connected! IP: " + WiFi.localIP().toString());

  // ── MAX30105 ──────────────────────────────────────────
  if (!particleSensor.begin(Wire, I2C_SPEED_STANDARD)) { Serial.println("MAX30105 FAIL"); maxOK = false; } 
  else { maxOK = true; particleSensor.setup(60, 4, 2, 100, 411, 4096); Serial.println("MAX30105 OK"); }

  // ── MLX90614 ──────────────────────────────────────────
  if (!mlx.begin()) { Serial.println("MLX90614 FAIL"); mlxOK = false; } 
  else { mlxOK = true; Serial.println("MLX90614 OK"); }

  // ── Web server ────────────────────────────────────────
  server.on("/",    handleRoot);
  server.on("/data",handleData);
  server.on("/ecg", handleEcg);
  server.begin();
  Serial.println("HTTP server started.");
}

// ═══════════════════════════════════════════════════════════════════════════
// LOOP
// ═══════════════════════════════════════════════════════════════════════════
void loop() {
  server.handleClient();

  // ── 1. ECG (AD8232) ────
  if ((digitalRead(ECG_LO_PLUS_PIN) == HIGH) || (digitalRead(ECG_LO_MINUS_PIN) == HIGH)) {
    ecgValue  = 0; ecgStatus = "Leads Disconnected";
  } else {
    ecgValue  = analogRead(ECG_OUTPUT_PIN); ecgStatus = "Normal";
  }
  ecgBuf[ecgHead] = ecgValue; ecgHead = (ecgHead + 1) % ECG_BUF_SIZE;

  // ── 2. MAX30105 ───────────────────────────────────
  if (maxOK) {
    long irValue = particleSensor.getIR(); irRawVal = irValue;
    if (irValue > 7000) {
      if (checkForBeat(irValue)) {
        long delta = millis() - lastBeat; lastBeat = millis();
        float bpm  = 60.0f / (delta / 1000.0f);
        if (bpm > 20 && bpm < 255) {
          rates[rateSpot++] = (byte)bpm; rateSpot %= RATE_SIZE;
          beatAvg = 0; for (byte x = 0; x < RATE_SIZE; x++) beatAvg += rates[x]; beatAvg /= RATE_SIZE;
        }
      }
      int rawSpO2 = 104 - (int)(17.0f * ((float)particleSensor.getRed() / (float)irValue));
      rawSpO2 = constrain(rawSpO2, 60, 100);
      approxSpO2 = (approxSpO2 == 0) ? rawSpO2 : (int)(approxSpO2 * 0.9f + rawSpO2 * 0.1f);
    } else { beatAvg = 0; approxSpO2 = 0; irRawVal = irValue; }
  }

  // ── 3. MLX90614 ──────────────────────────
  static unsigned long lastTempRead = 0;
  if (millis() - lastTempRead >= 500) {
    if (mlxOK) {
      float newObj = mlx.readObjectTempC(); float newAmb = mlx.readAmbientTempC();
      if (!isnan(newObj)) objectTemp  = newObj; if (!isnan(newAmb)) ambientTemp = newAmb;
    }
    lastTempRead = millis();
  }

  // ── 4. MPU6050 ────────────────────────
  static unsigned long lastMpuRead = 0;
  if (mpuOK && millis() - lastMpuRead >= 100) {
    Wire.beginTransmission(MPU_ADDR); Wire.write(0x3B); Wire.endTransmission(false); Wire.requestFrom(MPU_ADDR, 6, true);
    if (Wire.available() >= 6) {
      int16_t AcX = (Wire.read() << 8) | Wire.read(); int16_t AcY = (Wire.read() << 8) | Wire.read(); int16_t AcZ = (Wire.read() << 8) | Wire.read();
      float ax = AcX / 16384.0f; float ay = AcY / 16384.0f; float az = AcZ / 16384.0f;
      float netAcc = sqrt(ax*ax + ay*ay + az*az);
      if      (netAcc > 2.5f)                  motion = "FALL DETECTED!";
      else if (netAcc > 1.8f)                  motion = "Abnormal Shaking";
      else if (netAcc < 0.8f || netAcc > 1.3f) motion = "Moving";
      else                                     motion = "Stable";
    }
    lastMpuRead = millis();
  }

  // ── 5. POST to n8n webhook (مع تأمين تخطي الـ SSL لضمان عمل الـ Publish تلقائياً) ──
  if (millis() - lastPostTime >= (unsigned long)postInterval) {
    if (WiFi.status() == WL_CONNECTED) {
      
      WiFiClientSecure client;
      client.setInsecure(); // السطر السحري لتخطي حماية شهادة الأمان لربط الـ Production

      HTTPClient http;
      http.begin(client, n8n_webhook); // ربط العميل الآمن بالهيدر
      http.addHeader("Content-Type", "application/json");
      http.setTimeout(1500);

      String payload = "{";
      payload += "\"heart_rate\":"   + String(beatAvg)              + ",";
      payload += "\"spo2\":"         + String(approxSpO2)            + ",";
      payload += "\"motion\":\""     + motion                        + "\",";
      payload += "\"mlx_object\":"   + String(objectTemp,  1)       + ",";
      payload += "\"mlx_ambient\":"  + String(ambientTemp, 1)       + ",";
      payload += "\"ecg_raw\":"      + String(ecgValue)              + ",";
      payload += "\"ecg_status\":\"" + ecgStatus                     + "\",";
      payload += "\"mlx_error\":"    + String(mlxOK ? "false" : "true")   + ",";
      payload += "\"mpu_error\":"    + String(mpuOK ? "false" : "true")   + ",";
      payload += "\"max_error\":"    + String(maxOK ? "false" : "true")   + ",";
      payload += "\"ir_raw\":"       + String(irRawVal);
      payload += "}";

      int code = http.POST(payload);
      Serial.print("[n8n] HTTP response: "); Serial.println(code);
      
      // ليد الـ n8n ينور أخضر لما الرد يكون 200 (تمت العملية بنجاح)
      if (code == 200) {
        digitalWrite(LED_N8N_PIN, HIGH);
        n8nLedTimer = millis(); 
      }
      
      http.end();
    } else {
      Serial.println("[n8n] WiFi not connected — skipping post");
    }
    lastPostTime = millis();
  }

  // إطفاء ليد الـ n8n بعد نصف ثانية بدون استخدام delay لضمان سلاسة قراءة الـ ECG
  if (digitalRead(LED_N8N_PIN) == HIGH && (millis() - n8nLedTimer >= 500)) {
    digitalWrite(LED_N8N_PIN, LOW);
  }
}