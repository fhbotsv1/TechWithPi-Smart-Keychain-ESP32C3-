
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <RTClib.h>
#include <DHT.h>
#include <math.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

// =============== PIN CONFIGURATION =================

#define OLED_SDA 16
#define OLED_SCL 17
#define BUTTON_PIN 27
#define DHT_PIN 26
#define DHT_TYPE DHT11

#define SCREEN_TIMEOUT 10000
#define DEBOUNCE_TIME 40

// =============== MODULES ===========================

Adafruit_SH1106G display(128, 64, &Wire, -1);
RTC_DS1307 rtc;
DHT dht(DHT_PIN, DHT_TYPE);

// Phone control and persistent settings
const char* AP_SSID = "ESP32-Robot";
const char* AP_PASS = "12345678";
WebServer server(80);
Preferences prefs;
bool screenOff = false, savedImageAvailable = false, imageMode = false;
uint8_t faceSelection = 0, animationSpeed = 2, photoAnimation = 4;
uint8_t imageBuffer[1024], activePhotoAnim = 1, autoPhotoIndex = 0;
bool photoAnimRunning = false;
unsigned long photoAnimStart = 0, nextPhotoAnim = 0, lastAnimFrame = 0;
const unsigned long PHOTO_GAP = 5000;

// Forward declaration: used by button and web handlers before its definition.
void drawPhoto();

const char WEB_PAGE[] PROGMEM = R"rawliteral(
<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Robot</title><style>
body{font-family:system-ui;background:#10141a;color:#eef;margin:0;padding:16px;max-width:480px;margin:auto}
h2{margin:0}h3{margin:20px 0 7px;font-size:16px}.sub{color:#9aabba;font-size:13px}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:7px}.grid button,button{background:#26364a;color:white;border:0;border-radius:9px;padding:11px;font-size:14px}
button{width:100%;margin:4px 0}.grid button.on{background:#2563eb}.row{display:flex;align-items:center;gap:10px;margin:10px 0}
.row span{width:85px;color:#aab}select{background:#26364a;color:white;padding:8px;border:0;border-radius:6px}
input[type=range]{flex:1}canvas{width:100%;background:#000;image-rendering:pixelated;aspect-ratio:2/1;border:1px solid #456}
.btns{display:flex;gap:8px}.btns label{flex:1;background:#2563eb;text-align:center;padding:11px;border-radius:8px}
input[type=file]{display:none}#st{color:#fbbf24;min-height:20px;font-size:13px}
</style></head><body><h2>ESP32 Robot Control</h2><p class="sub">Connect to the ESP32 hotspot. Internet is not required.</p>
<h3>Robot expression</h3><div class="grid" id="faces"></div>
<h3>Animation speed</h3><div class="row"><span>Speed</span><select id="speed"><option value="1">Slow</option><option value="2">Normal</option><option value="3">Fast</option></select></div>
<h3>Photo upload</h3><p class="sub">Convert a photo to a 128 × 64 monochrome image.</p>
<div class="btns"><label>Camera<input id="cam" type="file" accept="image/*" capture="environment"></label><label>Gallery<input id="gal" type="file" accept="image/*"></label></div>
<canvas id="cv" width="128" height="64"></canvas>
<div class="row"><span>Fit</span><select id="fit"><option value="cover">Crop</option><option value="contain">Fit</option><option value="stretch">Stretch</option></select></div>
<div class="row"><span>Contrast</span><input id="con" type="range" min=".5" max="3" step=".05" value="1.2"></div>
<div class="row"><span>Brightness</span><input id="bri" type="range" min="-100" max="100" value="0"></div>
<div class="row"><label><input id="dith" type="checkbox" checked>Dither</label><label><input id="inv" type="checkbox">Invert</label></div>
<button id="send">Send photo to OLED</button><h3>Photo animation</h3><div class="grid" id="anims"></div>
<button id="eyes" style="background:#475569">Return to robot eyes</button>
<h3>Display power</h3><div class="grid"><button id="off">Screen OFF</button><button id="on">Screen ON</button></div><div id="st"></div>
<script>
const $=id=>document.getElementById(id),st=t=>$('st').textContent=t;
async function req(u){let r=await fetch(u);if(!r.ok)throw Error(await r.text());return r.text()}
function chips(id,url,labels){let box=$(id);labels.forEach((v,i)=>{let b=document.createElement('button');b.textContent=v;b.onclick=()=>req(url+'?m='+i).then(()=>{[...box.children].forEach(x=>x.classList.remove('on'));b.classList.add('on');st('Saved: '+v)}).catch(()=>st('Connection error'));box.appendChild(b)});req(url).then(v=>{if(box.children[+v])box.children[+v].classList.add('on')}).catch(()=>{})}
chips('faces','/face',['Auto moods','Normal','Angry','Funny','Funny wacky','Love','Sleepy','Sad']);
chips('anims','/anim',['Static','Blink','Slide','Zoom','Auto cycle']);
req('/speed').then(v=>$('speed').value=v).catch(()=>{});
$('speed').onchange=()=>req('/speed?m='+$('speed').value).then(()=>st('Speed saved')).catch(()=>st('Connection error'));
let cv=$('cv'),ctx=cv.getContext('2d',{willReadFrequently:true}),img=null,bytes=new Uint8Array(1024);
function load(f){if(!f)return;let im=new Image();im.onload=()=>{img=im;render();st('Photo ready')};im.src=URL.createObjectURL(f)}
$('cam').onchange=e=>load(e.target.files[0]);$('gal').onchange=e=>load(e.target.files[0]);['fit','con','bri','dith','inv'].forEach(x=>$(x).oninput=render);
function render(){if(!img)return;let W=128,H=64;ctx.fillStyle='#000';ctx.fillRect(0,0,W,H);let fit=$('fit').value;
if(fit==='stretch')ctx.drawImage(img,0,0,W,H);else{let s=fit==='cover'?Math.max(W/img.width,H/img.height):Math.min(W/img.width,H/img.height),w=img.width*s,h=img.height*s;ctx.drawImage(img,(W-w)/2,(H-h)/2,w,h)}
let d=ctx.getImageData(0,0,W,H),p=d.data,c=+$('con').value,b=+$('bri').value,g=new Float32Array(W*H);
for(let i=0;i<W*H;i++)g[i]=Math.max(0,Math.min(255,((.299*p[i*4]+.587*p[i*4+1]+.114*p[i*4+2]-128)*c+128+b)));
let bits=new Uint8Array(W*H);if($('dith').checked){for(let y=0;y<H;y++)for(let x=0;x<W;x++){let i=y*W+x,o=g[i],n=o<128?0:255;bits[i]=n?1:0;let e=o-n;if(x+1<W)g[i+1]+=e*7/16;if(y+1<H){if(x)g[i+W-1]+=e*3/16;g[i+W]+=e*5/16;if(x+1<W)g[i+W+1]+=e/16}}}else for(let i=0;i<W*H;i++)bits[i]=g[i]>=128?1:0;
if($('inv').checked)for(let i=0;i<bits.length;i++)bits[i]^=1;bytes.fill(0);
for(let y=0;y<H;y++)for(let x=0;x<W;x++){let i=y*W+x,v=bits[i];if(v)bytes[y*16+(x>>3)]|=128>>(x&7);let o=i*4,z=v?255:0;p[o]=p[o+1]=p[o+2]=z;p[o+3]=255}ctx.putImageData(d,0,0)}
$('send').onclick=async()=>{if(!img){st('Choose a photo first');return}let hex=Array.from(bytes,v=>v.toString(16).padStart(2,'0')).join('');st('Uploading...');try{let r=await fetch('/upload',{method:'POST',headers:{'Content-Type':'text/plain'},body:hex});if(!r.ok)throw Error(await r.text());st('Photo saved and displayed')}catch(e){st('Upload failed: '+e)}};
$('eyes').onclick=()=>req('/eyes').then(()=>st('Robot eyes restored')).catch(()=>st('Connection error'));
$('off').onclick=()=>req('/power?m=off').then(()=>st('Screen off; use Screen ON or device button')).catch(()=>st('Connection error'));
$('on').onclick=()=>req('/power?m=on').then(()=>st('Screen on')).catch(()=>st('Connection error'));
</script></body></html>
)rawliteral";

// =============== SYSTEM VARIABLES ==================

bool rtcOK = false;
bool dhtOK = false;

float temperature = NAN;
float humidity = NAN;

int currentPage = 0;

// Pages: 0 = Clock, 1 = Environment, 2 = System
// Page 3 = Idle animation

bool idleMode = true;

unsigned long lastActivity = 0;
unsigned long lastDHTRead = 0;
unsigned long lastPageUpdate = 0;
unsigned long lastFrame = 0;

int buttonState = HIGH;
int lastButtonReading = HIGH;
unsigned long lastDebounceTime = 0;

// =============== ROBOT ANIMATION ===================

const int EYE_W = 36;
const int EYE_H = 40;
const int EYE_LX = 20;
const int EYE_RX = 72;
const int EYE_CY = 30;

float lookX = 0;
float lookY = 0;
float targetX = 0;
float targetY = 0;

unsigned long nextLook = 0;
unsigned long nextBlink = 0;
unsigned long blinkStart = 0;
bool blinking = false;

enum Mood {
  NORMAL,
  ANGRY,
  FUNNY,
  LOVE,
  SLEEPY,
  SAD
};

Mood mood = NORMAL;
Mood previousMood = NORMAL;

unsigned long nextMood = 0;
unsigned long moodEnd = 0;

int funnyStyle = 0;

// =============== CLOCK PAGE ========================

void drawClock() {

  DateTime now = rtc.now();

  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("REAL TIME CLOCK");

  display.drawLine(0, 12, 127, 12, SH110X_WHITE);

  display.setTextSize(2);
  display.setCursor(13, 23);

  if (now.hour() < 10) display.print("0");
  display.print(now.hour());
  display.print(":");

  if (now.minute() < 10) display.print("0");
  display.print(now.minute());
  display.print(":");

  if (now.second() < 10) display.print("0");
  display.print(now.second());

  display.setTextSize(1);
  display.setCursor(20, 50);

  if (now.day() < 10) display.print("0");
  display.print(now.day());
  display.print("/");

  if (now.month() < 10) display.print("0");
  display.print(now.month());
  display.print("/");
  display.print(now.year());

  display.display();
}

// =============== ENVIRONMENT PAGE ==================

void drawEnvironment() {

  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("ENVIRONMENT");

  display.drawLine(0, 12, 127, 12, SH110X_WHITE);

  display.setCursor(0, 20);
  display.print("TEMP");

  display.setTextSize(2);
  display.setCursor(58, 17);

  if (isnan(temperature)) {
    display.print("--");
  } else {
    display.print(temperature, 1);
  }

  display.print(" C");

  display.setTextSize(1);
  display.setCursor(0, 46);
  display.print("HUMIDITY");

  display.setTextSize(2);
  display.setCursor(58, 42);

  if (isnan(humidity)) {
    display.print("--");
  } else {
    display.print(humidity, 0);
  }

  display.print("%");

  display.display();
}

// =============== SYSTEM PAGE =======================

void drawSystem() {

  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("SYSTEM STATUS");

  display.drawLine(0, 12, 127, 12, SH110X_WHITE);

  display.setCursor(0, 19);
  display.print("OLED : READY");

  display.setCursor(0, 31);
  display.print("RTC  : ");
  display.print(rtcOK ? "ONLINE" : "ERROR");

  display.setCursor(0, 43);
  display.print("DHT11: ");
  display.print(dhtOK ? "ONLINE" : "ERROR");

  display.setCursor(0, 55);
  display.print("ESP32: RUNNING");

  display.display();
}

// =============== PAGE MANAGER ======================

void updatePage() {

  if (idleMode) return;

  switch (currentPage) {

    case 0:
      if (rtcOK) drawClock();
      break;

    case 1:
      drawEnvironment();
      break;

    case 2:
      drawSystem();
      break;
  }
}

// =============== ROBOT EYE DRAWING =================

void drawEye(int x, int y, int w, int h) {

  int r = min(10, min(w / 2, h / 2));

  display.fillRoundRect(
    x, y, w, h, r, SH110X_WHITE
  );
}

void drawHeart(int cx, int cy, int s) {

  int r = s / 2;

  display.fillCircle(cx-r, cy-r/2, r, SH110X_WHITE);
  display.fillCircle(cx+r, cy-r/2, r, SH110X_WHITE);

  display.fillTriangle(
    cx-s, cy-r/3,
    cx+s, cy-r/3,
    cx, cy+s,
    SH110X_WHITE
  );
}

// =============== MOOD MANAGEMENT ===================

void updateMood(unsigned long now) {
  if (faceSelection != 0) {
    switch (faceSelection) {
      case 1: mood = NORMAL; funnyStyle = 0; break;
      case 2: mood = ANGRY; break;
      case 3: mood = FUNNY; funnyStyle = 0; break;
      case 4: mood = FUNNY; funnyStyle = 1; break;
      case 5: mood = LOVE; break;
      case 6: mood = SLEEPY; break;
      case 7: mood = SAD; break;
    }
    return;
  }

  if (mood == NORMAL) {

    if (now >= nextMood) {

      Mood m;

      do {
        m = (Mood)random(1, 6);
      } while (m == previousMood);

      mood = m;
      previousMood = m;

      funnyStyle = random(0, 2);

      moodEnd = now + random(2800, 5000);
    }

  } else if (now >= moodEnd) {

    mood = NORMAL;
    nextMood = now + random(6000, 12000);
  }
}

// =============== ANIMATED ROBOT FACE ===============

void drawRobotEyes() {

  unsigned long now = millis();

  updateMood(now);

  // Eye direction
  if (now >= nextLook) {

    targetX = random(-10, 11);
    targetY = random(-6, 7);

    if (random(0, 4) == 0) {
      targetX = 0;
      targetY = 0;
    }

    nextLook = now + random(1200, 3500);
  }

  lookX += (targetX - lookX) * 0.25;
  lookY += (targetY - lookY) * 0.25;

  // Blinking
  int h = EYE_H;

  if (!blinking && now >= nextBlink) {

    blinking = true;
    blinkStart = now;
  }

  if (blinking) {

    unsigned long t = now - blinkStart;

    if (t < 80) {
      h = EYE_H - (36 * t) / 80;
    }

    else if (t < 160) {
      h = 4 + (36 * (t - 80)) / 80;
    }

    else {

      blinking = false;
      nextBlink = now + random(2000, 5500);
    }
  }

  h = max(4, h);

  display.clearDisplay();

  int cy = EYE_CY + (int)lookY;
  int top = cy - h / 2;

  int lx = EYE_LX + (int)lookX;
  int rx = EYE_RX + (int)lookX;

  // NORMAL
  if (mood == NORMAL) {

    drawEye(lx, top, EYE_W, h);
    drawEye(rx, top, EYE_W, h);
  }

  // ANGRY
  else if (mood == ANGRY) {

    drawEye(lx, top, EYE_W, h);
    drawEye(rx, top, EYE_W, h);

    int slant = (22 * h) / EYE_H;

    display.fillTriangle(
      lx, top, lx+EYE_W, top,
      lx+EYE_W, top+slant, SH110X_BLACK
    );

    display.fillTriangle(
      rx, top, rx+EYE_W, top,
      rx, top+slant, SH110X_BLACK
    );
  }

  // FUNNY
  else if (mood == FUNNY) {

    if (funnyStyle == 0) {

      int c = EYE_CY + (int)(3 * sin(now / 120.0));

      drawEye(EYE_LX, c-20, EYE_W, 40);
      drawEye(EYE_RX, c-20, EYE_W, 40);

      display.fillCircle(38, c+14, 15, SH110X_BLACK);
      display.fillCircle(90, c+14, 15, SH110X_BLACK);

    } else {

      bool swap = ((now / 450) % 2) == 0;

      int lw = swap ? 36 : 26;
      int lh = swap ? 40 : 22;

      int rw = swap ? 26 : 36;
      int rh = swap ? 22 : 40;

      drawEye(38-lw/2, EYE_CY-lh/2, lw, lh);
      drawEye(90-rw/2, EYE_CY-rh/2, rw, rh);
    }
  }

  // LOVE
  else if (mood == LOVE) {

    int s = 15 + (int)(3 * sin(now / 120.0));

    drawHeart(38, EYE_CY-3, s);
    drawHeart(90, EYE_CY-3, s);
  }

  // SLEEPY
  else if (mood == SLEEPY) {

    drawEye(EYE_LX, 15, EYE_W, 30);
    drawEye(EYE_RX, 15, EYE_W, 30);

    int droop = 10 + (int)(10 * (0.5 + 0.5 * sin(now / 500.0)));

    display.fillRect(19, 14, 38, droop, SH110X_BLACK);
    display.fillRect(71, 14, 38, droop, SH110X_BLACK);

    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(100, 10);
    display.print("zZ");
  }

  // SAD
  else {

    drawEye(lx, top, EYE_W, h);
    drawEye(rx, top, EYE_W, h);

    display.fillTriangle(
      lx, top, lx+EYE_W, top,
      lx, top+18, SH110X_BLACK
    );

    display.fillTriangle(
      rx, top, rx+EYE_W, top,
      rx+EYE_W, top+18, SH110X_BLACK
    );

    int ty = 35 + (int)((now / 60) % 20);

    if (ty < 62) {
      display.fillCircle(lx+8, ty, 2, SH110X_WHITE);
    }
  }

  display.display();
}

// =============== BUTTON HANDLER ====================

void handleButton() {

  unsigned long now = millis();

  int reading = digitalRead(BUTTON_PIN);

  if (reading != lastButtonReading) {
    lastDebounceTime = now;
  }

  if (now - lastDebounceTime > DEBOUNCE_TIME) {

    if (reading != buttonState) {

      buttonState = reading;

      if (buttonState == LOW) {

        lastActivity = now;

        if (screenOff) {
          display.oled_command(SH110X_DISPLAYON);
          screenOff = false;
          if (idleMode) {
            if (imageMode && savedImageAvailable) drawPhoto();
            else drawRobotEyes();
          } else {
            updatePage();
          }
        } else if (idleMode) {

          idleMode = false;
          imageMode = false;
          currentPage = 0;
          updatePage();

        } else {

          currentPage++;

          if (currentPage >= 3) {
            currentPage = 0;
          }
          updatePage();
        }

        Serial.print("Page: ");
        Serial.println(currentPage + 1);
      }
    }
  }

  lastButtonReading = reading;
}

// =============== SETUP =============================

// =============== WEB SERVER AND PHOTO SUPPORT =======
int frameDelayMs() { return animationSpeed == 1 ? 50 : (animationSpeed == 3 ? 15 : 30); }

void drawPhoto() {
  display.clearDisplay();
  display.drawBitmap(0, 0, imageBuffer, 128, 64, SH110X_WHITE);
  display.display();
}
void handleRoot() { server.send_P(200, "text/html", WEB_PAGE); }
void handleFace() {
  if (server.hasArg("m")) { int v=server.arg("m").toInt(); if(v<0||v>7)v=0;
    faceSelection=v; prefs.putUChar("face",faceSelection); if(!v){mood=NORMAL;nextMood=millis()+2000;} }
  server.send(200,"text/plain",String(faceSelection));
}
void handleSpeed() {
  if(server.hasArg("m")){int v=server.arg("m").toInt();if(v<1||v>3)v=2;animationSpeed=v;prefs.putUChar("speed",v);}
  server.send(200,"text/plain",String(animationSpeed));
}
void handleAnim() {
  if(server.hasArg("m")){int v=server.arg("m").toInt();if(v<0||v>4)v=0;photoAnimation=v;prefs.putUChar("anim",v);photoAnimRunning=false;nextPhotoAnim=millis()+500;if(imageMode)drawPhoto();}
  server.send(200,"text/plain",String(photoAnimation));
}
uint8_t fromHex(char x){if(x>='0'&&x<='9')return x-'0';if(x>='a'&&x<='f')return x-'a'+10;if(x>='A'&&x<='F')return x-'A'+10;return 0;}
void handleUpload() {
  String s=server.arg("plain");if(s.length()!=2048){server.send(400,"text/plain","Expected 2048 hex characters");return;}
  for(int i=0;i<1024;i++)imageBuffer[i]=(fromHex(s[i*2])<<4)|fromHex(s[i*2+1]);
  prefs.putBytes("bmp",imageBuffer,1024);prefs.putBool("img",true);savedImageAvailable=true;imageMode=true;idleMode=true;
  photoAnimRunning=false;nextPhotoAnim=millis()+PHOTO_GAP;drawPhoto();server.send(200,"text/plain","OK");
}
void handleEyes(){imageMode=false;photoAnimRunning=false;prefs.putBool("img",false);savedImageAvailable=false;mood=NORMAL;nextMood=millis()+2500;idleMode=true;server.send(200,"text/plain","OK");}
void handlePower(){
  String m=server.arg("m");
  if(m=="off"){screenOff=true;display.oled_command(SH110X_DISPLAYOFF);}
  if(m=="on"){display.oled_command(SH110X_DISPLAYON);screenOff=false;if(idleMode){if(imageMode&&savedImageAvailable)drawPhoto();else drawRobotEyes();}else updatePage();}
  server.send(200,"text/plain",screenOff?"off":"on");
}
void startPhotoAnim(unsigned long now){activePhotoAnim=photoAnimation==4?(autoPhotoIndex++%3)+1:photoAnimation;photoAnimRunning=true;photoAnimStart=now;}
void runPhotoAnim(unsigned long now){
  unsigned long t=now-photoAnimStart;bool done=false;display.clearDisplay();
  if(activePhotoAnim==1){
    if(t>=360)done=true;else{float p=t<150?t/150.0f:(t<210?1.0f:1.0f-(t-210)/150.0f);display.drawBitmap(0,0,imageBuffer,128,64,SH110X_WHITE);int k=(int)(p*32);display.fillRect(0,0,128,k,SH110X_BLACK);display.fillRect(0,64-k,128,k,SH110X_BLACK);}
  }else if(activePhotoAnim==2){
    if(t>=900)done=true;else{int x=t<450?(int)(128.0f*(t/450.0f)*(t/450.0f)):-128+(int)(128.0f*(1.0f-pow(1.0f-(t-450)/450.0f,2)));display.drawBitmap(x,0,imageBuffer,128,64,SH110X_WHITE);}
  }else{
    if(t>=700)done=true;else{float p=t/700.0f,s=.05f+.95f*(1.0f-pow(1.0f-p,2));int w=max(1,(int)(128*s)),h=max(1,(int)(64*s)),x0=(128-w)/2,y0=(64-h)/2;
      for(int y=0;y<h;y++)for(int x=0;x<w;x++){int sx=x*128/w,sy=y*64/h;if(imageBuffer[sy*16+(sx>>3)]&(0x80>>(sx&7)))display.drawPixel(x0+x,y0+y,SH110X_WHITE);}}
  }
  if(done){drawPhoto();photoAnimRunning=false;nextPhotoAnim=now+PHOTO_GAP;}else display.display();
}

void setup() {

  Serial.begin(115200);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Wire.begin(OLED_SDA, OLED_SCL);

  if (!display.begin(0x3C, true)) {

    Serial.println("OLED ERROR");
    while (1);
  }

  display.clearDisplay();
  display.display();

  randomSeed(micros());

  prefs.begin("robot", false);
  faceSelection=prefs.getUChar("face",0);if(faceSelection>7)faceSelection=0;
  photoAnimation=prefs.getUChar("anim",4);if(photoAnimation>4)photoAnimation=4;
  animationSpeed=prefs.getUChar("speed",2);if(animationSpeed<1||animationSpeed>3)animationSpeed=2;
  if(prefs.getBool("img",false)&&prefs.getBytesLength("bmp")==1024){prefs.getBytes("bmp",imageBuffer,1024);savedImageAvailable=true;imageMode=true;}

  // RTC
  rtcOK = rtc.begin();

  if (rtcOK && !rtc.isrunning()) {

    Serial.println("RTC STOPPED - SETTING COMPILE TIME");

    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // DHT11
  dht.begin();

  delay(2000);

  temperature = dht.readTemperature();
  humidity = dht.readHumidity();

  dhtOK = !isnan(temperature) && !isnan(humidity);

  unsigned long now = millis();

  nextBlink = now + 2000;
  nextLook = now + 1000;
  nextMood = now + random(5000, 9000);

  lastActivity = now;
  lastDHTRead = now;

  idleMode = true;
  if(imageMode&&savedImageAvailable)drawPhoto();

  WiFi.mode(WIFI_AP);WiFi.softAP(AP_SSID,AP_PASS);
  Serial.print("Wi-Fi hotspot: ");Serial.println(AP_SSID);
  Serial.print("Control page: http://");Serial.println(WiFi.softAPIP());
  server.on("/",HTTP_GET,handleRoot);
  server.on("/face",HTTP_GET,handleFace);
  server.on("/speed",HTTP_GET,handleSpeed);
  server.on("/anim",HTTP_GET,handleAnim);
  server.on("/upload",HTTP_POST,handleUpload);
  server.on("/eyes",HTTP_GET,handleEyes);
  server.on("/power",HTTP_GET,handlePower);
  server.begin();

  Serial.println("SYSTEM READY");
  Serial.println("ROBOT ANIMATION ACTIVE");
}

// =============== MAIN LOOP =========================

void loop() {

  server.handleClient();
  unsigned long now = millis();

  handleButton();

  // Read DHT11 every 2 seconds
  if (now - lastDHTRead >= 2000) {

    lastDHTRead = now;

    float t = dht.readTemperature();
    float h = dht.readHumidity();

    if (!isnan(t) && !isnan(h)) {

      temperature = t;
      humidity = h;
      dhtOK = true;

    } else {

      dhtOK = false;
    }
  }

  // Return to animation after inactivity
  if (!idleMode &&
      now - lastActivity >= SCREEN_TIMEOUT) {

    idleMode = true;
    imageMode = savedImageAvailable;

    mood = NORMAL;
    nextMood = now + 3000;
    if(imageMode)drawPhoto();

    Serial.println("IDLE ANIMATION");
  }

  // Idle display: uploaded photo or animated robot face
  if (screenOff) return;
  if (idleMode && imageMode && savedImageAvailable) {
    if(photoAnimRunning){if(now-lastAnimFrame>=(unsigned long)frameDelayMs()){lastAnimFrame=now;runPhotoAnim(now);}}
    else if(photoAnimation!=0&&now>=nextPhotoAnim)startPhotoAnim(now);
  } else if (idleMode && now - lastFrame >= (unsigned long)frameDelayMs()) {
    lastFrame=now;drawRobotEyes();
  }

  // Information page refresh
  if (!idleMode && now - lastPageUpdate >= 500) {

    lastPageUpdate = now;
    updatePage();
  }
}
