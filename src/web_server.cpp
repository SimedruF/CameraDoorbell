#include "web_server.hpp"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_http_server.h>

#include "app_config.hpp"
#include "camera_service.hpp"
#include "config_service.hpp"
#include "radar_service.hpp"
#include "storage_service.hpp"

namespace {

httpd_handle_t webServer = nullptr;
httpd_handle_t streamServer = nullptr;

constexpr char kStreamType[] = "multipart/x-mixed-replace;boundary=doorbellframe";
constexpr char kStreamBoundary[] = "\r\n--doorbellframe\r\n";
constexpr char kStreamPart[] =
    "Content-Type: image/jpeg\r\nContent-Length: %u\r\nX-Sequence: %lu\r\n\r\n";

const char kIndexHtml[] PROGMEM = R"HTML(
<!doctype html>
<html lang="ro">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Camera Doorbell</title>
  <style>
    body{font-family:system-ui,sans-serif;max-width:900px;margin:auto;padding:16px;background:#111;color:#eee}
    img{width:100%;height:auto;background:#222;border-radius:8px}
    button,a{display:inline-block;margin:8px 8px 8px 0;padding:10px 14px;border:0;border-radius:6px;background:#0b72e7;color:#fff;text-decoration:none;cursor:pointer}
    pre{background:#222;padding:12px;border-radius:6px;overflow:auto}
    nav{margin-bottom:12px} nav a{background:#333}
  </style>
</head>
<body>
  <h1>Camera Doorbell</h1>
  <nav><a href="/">Cameră</a><a href="/files">microSD</a><a href="/config">Configurare</a></nav>
  <img id="stream" alt="Flux MJPEG">
  <div>
    <button id="photoSave" onclick="savePhoto()">Salvează fotografie</button>
    <button id="recordStart" onclick="videoCommand('start')">Pornește înregistrarea</button>
    <button id="recordStop" onclick="videoCommand('stop')">Oprește înregistrarea</button>
    <a href="/capture" target="_blank">Snapshot curent</a>
    <a href="/photos/latest" target="_blank">Ultima fotografie salvată</a>
    <a href="/videos/latest">Ultimul film AVI</a>
  </div>
  <pre id="status">Se încarcă...</pre>
  <script>
    document.getElementById('stream').src = `${location.protocol}//${location.hostname}:81/stream`;
    async function savePhoto(){
      const response=await fetch('/api/capture',{method:'POST'});
      alert(await response.text());
      setTimeout(loadStatus,1000);
    }
    async function videoCommand(command){
      const response=await fetch('/api/record/'+command,{method:'POST'});
      alert(await response.text());
      setTimeout(loadStatus,500);
    }
    async function loadStatus(){
      try{const status=await (await fetch('/api/status')).json();document.getElementById('status').textContent=JSON.stringify(status,null,2);document.getElementById('photoSave').disabled=!status.storage.mounted||status.storage.recording;document.getElementById('recordStart').disabled=!status.storage.mounted||!status.storage.video_enabled||status.storage.recording;document.getElementById('recordStop').disabled=!status.storage.recording}
      catch(e){document.getElementById('status').textContent=String(e)}
    }
    loadStatus(); setInterval(loadStatus,5000);
  </script>
</body>
</html>
)HTML";

const char kConfigHtml[] PROGMEM = R"HTML(
<!doctype html><html lang="ro"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Configurare</title>
<style>
body{font-family:system-ui,sans-serif;max-width:760px;margin:auto;padding:16px;background:#111;color:#eee}
a,button{display:inline-block;margin:8px 8px 8px 0;padding:10px 14px;border:0;border-radius:6px;background:#0b72e7;color:white;text-decoration:none;cursor:pointer}
nav a{background:#333}fieldset{border:1px solid #444;border-radius:8px;margin:16px 0;padding:16px}
label{display:block;margin:12px 0 5px}input[type=text],input[type=password],input[type=number],select{box-sizing:border-box;width:100%;padding:10px;border:1px solid #555;border-radius:5px;background:#222;color:#fff}
.hint{color:#aaa;font-size:.9rem}.row,.grid{display:flex;gap:12px;flex-wrap:wrap}.row>*,.grid>*{flex:1;min-width:180px}.checks label{display:inline-block;margin-right:18px}#message{white-space:pre-wrap;padding:10px;border-radius:6px}details{margin-top:12px}summary{cursor:pointer;color:#7db8ff}
</style></head><body><h1>Configurare</h1>
<nav><a href="/">Cameră</a><a href="/files">microSD</a></nav>
<form id="settings"><fieldset><legend>Rețea Wi-Fi</legend>
<label for="ssid">SSID</label><input id="ssid" name="wifi_ssid" maxlength="32" autocomplete="off">
<label for="wifiPass">Parolă Wi-Fi</label><input id="wifiPass" name="wifi_password" type="password" maxlength="64" placeholder="Lasă gol pentru a păstra parola">
<label><input id="clearWifi" type="checkbox"> Șterge rețeaua salvată și pornește în mod access point</label>
<label for="apPass">Parolă access point</label><input id="apPass" name="ap_password" type="password" minlength="8" maxlength="63" placeholder="Lasă gol pentru a păstra parola">
<div class="hint">După salvare placa repornește. Dacă noua rețea nu este accesibilă, revine automat la access point.</div>
</fieldset><fieldset><legend>Stocare microSD</legend>
<label><input id="sdEnabled" type="checkbox"> Activează cardul microSD</label>
<div class="row"><div><label for="photoDir">Director fotografii</label><input id="photoDir" maxlength="31"></div>
<div><label for="videoDir">Director filme</label><input id="videoDir" maxlength="31"></div></div>
<label for="minFree">Spațiu liber minim (MB)</label><input id="minFree" type="number" min="0" max="16384">
<label><input id="videoEnabled" type="checkbox"> Activează recorderul AVI/MJPEG</label>
<div class="grid"><div><label for="videoFrameRate">Cadre/secundă în AVI</label><input id="videoFrameRate" type="number" min="1" max="20"></div><div><label for="videoDuration">Durată maximă (secunde)</label><input id="videoDuration" type="number" min="1" max="600"></div><div><label for="videoMaxSize">Dimensiune maximă (MB)</label><input id="videoMaxSize" type="number" min="8" max="1024"></div></div>
<div class="hint">AVI conține cadre JPEG individuale. Rata efectivă este limitată de rata camerei și poate scădea la rezoluții mari. Salvarea se oprește înainte de a încălca pragul de spațiu liber sau dimensiunea maximă.</div>
</fieldset><fieldset><legend>Cameră OV2640</legend>
<div class="grid"><div><label for="camProfile">Profil</label><select id="camProfile"><option value="0">Manual / zi</option><option value="1">Streaming</option><option value="2">Fotografie</option><option value="3">Noapte</option></select></div>
<div><label for="frameSize">Rezoluție stream, fotografii și AVI</label><select id="frameSize"><option value="1">QQVGA 160×120</option><option value="3">HQVGA 240×176</option><option value="5">QVGA 320×240</option><option value="6">CIF 400×296</option><option value="8">VGA 640×480</option><option value="9">SVGA 800×600</option><option value="10">XGA 1024×768</option><option value="11">HD 1280×720</option><option value="12">SXGA 1280×1024</option><option value="13">UXGA 1600×1200</option></select></div></div>
<div class="grid"><div><label for="jpegQuality">Calitate JPEG (4 foarte bună, 63 foarte comprimată)</label><input id="jpegQuality" type="number" min="4" max="63"></div><div><label for="frameRate">Cadre/secundă</label><input id="frameRate" type="number" min="1" max="30"></div></div>
<div class="grid"><div><label for="brightness">Luminozitate (-2…2)</label><input id="brightness" type="number" min="-2" max="2"></div><div><label for="contrast">Contrast (-2…2)</label><input id="contrast" type="number" min="-2" max="2"></div><div><label for="saturation">Saturație (-2…2)</label><input id="saturation" type="number" min="-2" max="2"></div></div>
<label for="specialEffect">Efect</label><select id="specialEffect"><option value="0">Normal</option><option value="1">Negativ</option><option value="2">Alb-negru</option><option value="3">Tentă roșie</option><option value="4">Tentă verde</option><option value="5">Tentă albastră</option><option value="6">Sepia</option></select>
<details open><summary>White balance, expunere și gain</summary>
<div class="checks"><label><input id="whiteBalance" type="checkbox"> White balance automat</label><label><input id="awbGain" type="checkbox"> AWB gain</label></div>
<label for="wbMode">Mod white balance</label><select id="wbMode"><option value="0">Auto</option><option value="1">Soare</option><option value="2">Înnorat</option><option value="3">Birou</option><option value="4">Acasă</option></select>
<div class="checks"><label><input id="autoExposure" type="checkbox"> Expunere automată AEC</label><label><input id="aec2" type="checkbox"> AEC DSP</label></div>
<div class="grid"><div><label for="exposureLevel">Compensare expunere (-2…2)</label><input id="exposureLevel" type="number" min="-2" max="2"></div><div><label for="exposureValue">Expunere manuală (0…1200)</label><input id="exposureValue" type="number" min="0" max="1200"></div></div>
<label><input id="autoGain" type="checkbox"> Gain automat AGC</label>
<div class="grid"><div><label for="gainValue">Gain manual (0…30)</label><input id="gainValue" type="number" min="0" max="30"></div><div><label for="gainCeiling">Limită gain automat</label><select id="gainCeiling"><option value="0">2×</option><option value="1">4×</option><option value="2">8×</option><option value="3">16×</option><option value="4">32×</option><option value="5">64×</option><option value="6">128×</option></select></div></div></details>
<details><summary>Procesare, orientare și test</summary><div class="checks"><label><input id="bpc" type="checkbox"> Corecție pixeli negri</label><label><input id="wpc" type="checkbox"> Corecție pixeli albi</label><label><input id="rawGamma" type="checkbox"> Raw gamma</label><label><input id="lensCorrection" type="checkbox"> Corecție lentilă</label><label><input id="downsize" type="checkbox"> Downsize DCW</label><label><input id="horizontalMirror" type="checkbox"> Oglindire</label><label><input id="verticalFlip" type="checkbox"> Rotire verticală</label><label><input id="colorBar" type="checkbox"> Bare de test</label></div></details>
<details><summary>LED flash</summary><label><input id="flashEnabled" type="checkbox"> LED aprins continuu</label><label for="flashIntensity">Intensitate (0…255)</label><input id="flashIntensity" type="number" min="0" max="255"><div class="hint">GPIO4 este liber deoarece microSD funcționează în modul 1-bit. LED-ul poate încălzi placa dacă rămâne aprins puternic.</div></details>
</fieldset><fieldset><legend>Radar HLK-LD2410C</legend>
<label><input id="radarEnabled" type="checkbox"> Activează radarul pe GPIO3 (UART RX, 256000 baud)</label>
<div class="grid"><div><label for="radarTriggerMode">Declanșare eveniment</label><select id="radarTriggerMode"><option value="0">Orice prezență</option><option value="1">Doar țintă în mișcare</option><option value="2">Doar țintă staționară</option></select></div><div><label for="radarCooldown">Pauză între evenimente (secunde)</label><input id="radarCooldown" type="number" min="1" max="3600"></div></div>
<label><input id="radarAutoPhoto" type="checkbox"> Salvează automat o fotografie la începutul prezenței</label>
<label><input id="radarAutoVideo" type="checkbox"> Pornește automat o înregistrare AVI la începutul prezenței</label>
<div class="hint">Conectare: LD2410C UART_TX → GPIO3, VCC → 5 V, GND comun. Deconectează UART_TX de la GPIO3 în timpul programării; pinul este folosit și de adaptorul USB-UART.</div>
</fieldset><button type="submit">Salvează și repornește</button></form><div id="message"></div>
<script>
const $=id=>document.getElementById(id);
const boolIds=['sdEnabled','videoEnabled','whiteBalance','awbGain','autoExposure','aec2','autoGain','bpc','wpc','rawGamma','lensCorrection','downsize','horizontalMirror','verticalFlip','colorBar','flashEnabled','radarEnabled','radarAutoPhoto','radarAutoVideo'];
const cameraIds=['videoFrameRate','videoDuration','videoMaxSize','camProfile','frameSize','jpegQuality','frameRate','brightness','contrast','saturation','specialEffect','wbMode','exposureLevel','exposureValue','gainValue','gainCeiling','flashIntensity','radarTriggerMode','radarCooldown'];
const apiNames={sdEnabled:'storage_enabled',videoEnabled:'video_enabled',videoFrameRate:'video_fps',videoDuration:'video_duration_seconds',videoMaxSize:'video_maximum_size_mb',camProfile:'camera_profile',frameSize:'frame_size',jpegQuality:'jpeg_quality',frameRate:'frame_rate',specialEffect:'special_effect',whiteBalance:'white_balance',awbGain:'awb_gain',wbMode:'wb_mode',autoExposure:'auto_exposure',exposureLevel:'exposure_level',exposureValue:'exposure_value',autoGain:'auto_gain',gainValue:'gain_value',gainCeiling:'gain_ceiling',rawGamma:'raw_gamma',lensCorrection:'lens_correction',horizontalMirror:'horizontal_mirror',verticalFlip:'vertical_flip',flashEnabled:'flash_enabled',flashIntensity:'flash_intensity',radarEnabled:'radar_enabled',radarAutoPhoto:'radar_auto_photo',radarAutoVideo:'radar_auto_video',radarTriggerMode:'radar_trigger_mode',radarCooldown:'radar_cooldown_seconds'};
async function load(){try{const c=await (await fetch('/api/config')).json();$('ssid').value=c.wifi_ssid;$('wifiPass').placeholder=c.wifi_password_set?'Parolă salvată; lasă gol pentru a o păstra':'Introduce parola';$('apPass').placeholder=c.ap_password_set?'Parolă salvată; lasă gol pentru a o păstra':'Minimum 8 caractere';$('photoDir').value=c.photo_directory;$('videoDir').value=c.video_directory;$('minFree').value=c.minimum_free_mb;for(const id of boolIds)$(id).checked=c[apiNames[id]||id.replace(/[A-Z]/g,m=>'_'+m.toLowerCase())];for(const id of cameraIds)$(id).value=c[apiNames[id]||id]}catch(e){$('message').textContent='Eroare: '+e}}
$('camProfile').onchange=()=>{const p=+$('camProfile').value;if(p===1){$('frameSize').value=8;$('jpegQuality').value=12;$('frameRate').value=10;$('brightness').value=0;$('contrast').value=0;$('saturation').value=0}else if(p===2){$('frameSize').value=13;$('jpegQuality').value=8;$('frameRate').value=2;$('brightness').value=0;$('contrast').value=0;$('saturation').value=0}else if(p===3){$('frameSize').value=8;$('jpegQuality').value=14;$('frameRate').value=5;$('brightness').value=1;$('contrast').value=1;$('saturation').value=-1;$('autoExposure').checked=true;$('aec2').checked=true;$('exposureLevel').value=1;$('autoGain').checked=true;$('gainCeiling').value=6}};
$('settings').addEventListener('submit',async e=>{e.preventDefault();const p=new URLSearchParams();p.set('wifi_ssid',$('ssid').value);p.set('wifi_password',$('wifiPass').value);p.set('clear_wifi',$('clearWifi').checked?'1':'0');p.set('ap_password',$('apPass').value);p.set('photo_directory',$('photoDir').value);p.set('video_directory',$('videoDir').value);p.set('minimum_free_mb',$('minFree').value);for(const id of boolIds)p.set(apiNames[id]||id.replace(/[A-Z]/g,m=>'_'+m.toLowerCase()),$(id).checked?'1':'0');for(const id of cameraIds)p.set(apiNames[id]||id,$(id).value);$('message').textContent='Se salvează...';try{const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p});const j=await r.json();if(!r.ok)throw new Error(j.error||r.statusText);$('message').textContent='Setările au fost salvate. Placa repornește; reconectează-te la noua rețea.'}catch(e){$('message').textContent='Eroare: '+e.message}});
load();
</script></body></html>
)HTML";

const char kFilesHtml[] PROGMEM = R"HTML(
<!doctype html><html lang="ro"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Fișiere microSD</title>
<style>
body{font-family:system-ui,sans-serif;max-width:960px;margin:auto;padding:16px;background:#111;color:#eee}a,button{display:inline-block;margin:6px;padding:8px 12px;border:0;border-radius:6px;background:#0b72e7;color:white;text-decoration:none;cursor:pointer}nav a{background:#333}table{width:100%;border-collapse:collapse;background:#1c1c1c}th,td{text-align:left;padding:9px;border-bottom:1px solid #444}tr:hover{background:#292929}.name{background:none;padding:0;margin:0;color:#7db8ff}#preview img,#preview video{display:block;max-width:100%;max-height:70vh;margin:12px auto}.muted{color:#aaa}.error{color:#ff8a8a}@media(max-width:650px){.size,.type{display:none}}
</style></head><body><h1>Fișiere microSD</h1><nav><a href="/">Cameră</a><a href="/config">Configurare</a></nav>
<div><button id="up">⬆ Sus</button><strong id="path">/</strong> <span id="info" class="muted"></span></div>
<table><thead><tr><th>Nume</th><th class="type">Tip</th><th>Data salvării</th><th class="size">Dimensiune</th><th>Acțiuni</th></tr></thead><tbody id="items"></tbody></table><div id="preview"></div>
<script>
const $=id=>document.getElementById(id);let current='/';
const enc=p=>encodeURIComponent(p);const basename=p=>p==='/'?'/':p.substring(p.lastIndexOf('/')+1);const human=n=>n<1024?n+' B':n<1048576?(n/1024).toFixed(1)+' KB':(n/1048576).toFixed(1)+' MB';
const saved=e=>e.saved_at?(new Date(e.saved_at).toLocaleString('ro-RO')+(e.date_source==='filename'?' (din nume)':'')):(e.directory?'—':'Dată indisponibilă');
function mediaType(p){p=p.toLowerCase();if(/\.(jpg|jpeg)$/.test(p))return'image';if(/\.(avi|mjpeg|mjpg)$/.test(p))return'video';return'file'}
function openFile(p){const type=mediaType(p),url='/media?path='+enc(p),box=$('preview');box.replaceChildren();if(type==='file'){window.open(url,'_blank');return}const title=document.createElement('h2');title.textContent=basename(p);box.appendChild(title);const media=document.createElement(type==='image'?'img':'video');media.src=url;if(type==='video')media.controls=true;box.appendChild(media);if(type==='video'){const note=document.createElement('p');note.className='muted';note.textContent='Redarea AVI/MJPEG depinde de browser; folosește Descărcare dacă nu pornește.';box.appendChild(note)}}
async function load(path){$('info').className='muted';$('info').textContent='Se încarcă...';$('preview').replaceChildren();try{const r=await fetch('/api/files?path='+enc(path));if(!r.ok)throw new Error(await r.text());const data=await r.json();current=data.path;$('path').textContent=current;$('up').disabled=current==='/';const body=$('items');body.replaceChildren();data.entries.sort((a,b)=>Number(b.directory)-Number(a.directory)||(b.saved_at||'').localeCompare(a.saved_at||'')||a.path.localeCompare(b.path)).forEach(e=>{const tr=document.createElement('tr'),name=basename(e.path),type=e.directory?'director':mediaType(e.path),nameCell=document.createElement('td'),open=document.createElement('button');open.className='name';open.textContent=name;open.onclick=()=>e.directory?load(e.path):openFile(e.path);nameCell.appendChild(open);const typeCell=document.createElement('td');typeCell.className='type';typeCell.textContent=type;const date=document.createElement('td');date.textContent=saved(e);const size=document.createElement('td');size.className='size';size.textContent=e.directory?'—':human(e.size);const actions=document.createElement('td');if(!e.directory){const download=document.createElement('a');download.href='/download?path='+enc(e.path);download.download=name;download.textContent='Descarcă';actions.appendChild(download)}tr.append(nameCell,typeCell,date,size,actions);body.appendChild(tr)});$('info').textContent=data.entries.length+' elemente'+(data.truncated?' (lista limitată)':'')}catch(e){$('info').className='error';$('info').textContent=e.message}}
$('up').onclick=()=>load(current==='/'?'/':(current.substring(0,current.lastIndexOf('/'))||'/'));load('/');
</script></body></html>
)HTML";

esp_err_t sendServiceUnavailable(httpd_req_t* request, const char* message) {
  httpd_resp_set_status(request, "503 Service Unavailable");
  httpd_resp_set_type(request, "text/plain");
  return httpd_resp_sendstr(request, message);
}

esp_err_t indexHandler(httpd_req_t* request) {
  httpd_resp_set_type(request, "text/html; charset=utf-8");
  return httpd_resp_send(request, kIndexHtml, HTTPD_RESP_USE_STRLEN);
}

String jsonQuoted(const char* value) {
  String result;
  result.reserve(strlen(value) + 8);
  result += '"';
  for (const unsigned char* cursor = reinterpret_cast<const unsigned char*>(value);
       *cursor != '\0'; ++cursor) {
    switch (*cursor) {
      case '"': result += "\\\""; break;
      case '\\': result += "\\\\"; break;
      case '\b': result += "\\b"; break;
      case '\f': result += "\\f"; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default:
        if (*cursor >= 0x20) result += static_cast<char>(*cursor);
        break;
    }
  }
  result += '"';
  return result;
}

int hexDigit(char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  return -1;
}

bool urlDecode(char* value) {
  char* source = value;
  char* destination = value;
  while (*source != '\0') {
    if (*source == '+') {
      *destination++ = ' ';
      ++source;
    } else if (*source == '%') {
      if (source[1] == '\0' || source[2] == '\0') return false;
      const int high = hexDigit(source[1]);
      const int low = hexDigit(source[2]);
      if (high < 0 || low < 0) return false;
      *destination++ = static_cast<char>((high << 4) | low);
      source += 3;
    } else {
      *destination++ = *source++;
    }
  }
  *destination = '\0';
  return true;
}

bool formValue(const char* body, const char* key, char* output, size_t outputSize) {
  if (httpd_query_key_value(body, key, output, outputSize) != ESP_OK) return false;
  return urlDecode(output);
}

template <typename T>
bool formInteger(const char* body, const char* key, long minimum, long maximum, T& output) {
  char value[24];
  if (!formValue(body, key, value, sizeof(value)) || value[0] == '\0') return false;
  char* end = nullptr;
  const long parsed = strtol(value, &end, 10);
  if (end == nullptr || *end != '\0' || parsed < minimum || parsed > maximum) return false;
  output = static_cast<T>(parsed);
  return true;
}

bool formBoolean(const char* body, const char* key, bool& output) {
  char value[8];
  if (!formValue(body, key, value, sizeof(value))) return false;
  if (strcmp(value, "1") != 0 && strcmp(value, "0") != 0) return false;
  output = strcmp(value, "1") == 0;
  return true;
}

bool requestPath(httpd_req_t* request, char* path, size_t pathSize) {
  const size_t queryLength = httpd_req_get_url_query_len(request);
  if (queryLength == 0 || queryLength >= 255) return false;
  char query[256];
  if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
      httpd_query_key_value(query, "path", path, pathSize) != ESP_OK) {
    return false;
  }
  return urlDecode(path);
}

esp_err_t configPageHandler(httpd_req_t* request) {
  httpd_resp_set_type(request, "text/html; charset=utf-8");
  return httpd_resp_send(request, kConfigHtml, HTTPD_RESP_USE_STRLEN);
}

esp_err_t filesPageHandler(httpd_req_t* request) {
  httpd_resp_set_type(request, "text/html; charset=utf-8");
  return httpd_resp_send(request, kFilesHtml, HTTPD_RESP_USE_STRLEN);
}

esp_err_t configGetHandler(httpd_req_t* request) {
  const RuntimeConfig config = Settings.snapshot();
  String json = "{\"wifi_ssid\":";
  json += jsonQuoted(config.wifiSsid);
  json += ",\"wifi_password_set\":";
  json += config.wifiPassword[0] == '\0' ? "false" : "true";
  json += ",\"ap_password_set\":true,\"storage_enabled\":";
  json += config.storageEnabled ? "true" : "false";
  json += ",\"photo_directory\":";
  json += jsonQuoted(config.photoDirectory);
  json += ",\"video_directory\":";
  json += jsonQuoted(config.videoDirectory);
  json += ",\"minimum_free_mb\":";
  json += config.minimumFreeMb;
  json += ",\"video_enabled\":"; json += config.videoEnabled ? "true" : "false";
  json += ",\"video_fps\":"; json += static_cast<unsigned>(config.videoFrameRate);
  json += ",\"video_duration_seconds\":";
  json += static_cast<unsigned>(config.videoDurationSeconds);
  json += ",\"video_maximum_size_mb\":";
  json += static_cast<unsigned>(config.videoMaximumSizeMb);
  json += ",\"camera_profile\":"; json += static_cast<unsigned>(config.cameraProfile);
  json += ",\"frame_size\":"; json += static_cast<unsigned>(config.frameSize);
  json += ",\"jpeg_quality\":"; json += static_cast<unsigned>(config.jpegQuality);
  json += ",\"frame_rate\":"; json += static_cast<unsigned>(config.frameRate);
  json += ",\"brightness\":"; json += static_cast<int>(config.brightness);
  json += ",\"contrast\":"; json += static_cast<int>(config.contrast);
  json += ",\"saturation\":"; json += static_cast<int>(config.saturation);
  json += ",\"special_effect\":"; json += static_cast<unsigned>(config.specialEffect);
  json += ",\"white_balance\":"; json += config.whiteBalance ? "true" : "false";
  json += ",\"awb_gain\":"; json += config.awbGain ? "true" : "false";
  json += ",\"wb_mode\":"; json += static_cast<unsigned>(config.whiteBalanceMode);
  json += ",\"auto_exposure\":"; json += config.autoExposure ? "true" : "false";
  json += ",\"aec2\":"; json += config.aec2 ? "true" : "false";
  json += ",\"exposure_level\":"; json += static_cast<int>(config.exposureLevel);
  json += ",\"exposure_value\":"; json += static_cast<unsigned>(config.exposureValue);
  json += ",\"auto_gain\":"; json += config.autoGain ? "true" : "false";
  json += ",\"gain_value\":"; json += static_cast<unsigned>(config.gainValue);
  json += ",\"gain_ceiling\":"; json += static_cast<unsigned>(config.gainCeiling);
  json += ",\"bpc\":"; json += config.blackPixelCorrection ? "true" : "false";
  json += ",\"wpc\":"; json += config.whitePixelCorrection ? "true" : "false";
  json += ",\"raw_gamma\":"; json += config.rawGamma ? "true" : "false";
  json += ",\"lens_correction\":"; json += config.lensCorrection ? "true" : "false";
  json += ",\"downsize\":"; json += config.downsize ? "true" : "false";
  json += ",\"horizontal_mirror\":"; json += config.horizontalMirror ? "true" : "false";
  json += ",\"vertical_flip\":"; json += config.verticalFlip ? "true" : "false";
  json += ",\"color_bar\":"; json += config.colorBar ? "true" : "false";
  json += ",\"flash_enabled\":"; json += config.flashEnabled ? "true" : "false";
  json += ",\"flash_intensity\":"; json += static_cast<unsigned>(config.flashIntensity);
  json += ",\"radar_enabled\":"; json += config.radarEnabled ? "true" : "false";
  json += ",\"radar_auto_photo\":"; json += config.radarAutoPhoto ? "true" : "false";
  json += ",\"radar_auto_video\":"; json += config.radarAutoVideo ? "true" : "false";
  json += ",\"radar_trigger_mode\":"; json += static_cast<unsigned>(config.radarTriggerMode);
  json += ",\"radar_cooldown_seconds\":";
  json += static_cast<unsigned>(config.radarCooldownSeconds);
  json += '}';

  httpd_resp_set_type(request, "application/json");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  return httpd_resp_send(request, json.c_str(), json.length());
}

void restartTask(void*) {
  vTaskDelay(pdMS_TO_TICKS(1500));
  ESP.restart();
}

esp_err_t configPostHandler(httpd_req_t* request) {
  if (request->content_len == 0 || request->content_len >= 2048) {
    httpd_resp_set_status(request, "400 Bad Request");
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, "{\"error\":\"Cerere invalida sau prea mare.\"}");
  }

  char body[2048];
  size_t received = 0;
  while (received < request->content_len) {
    const int count = httpd_req_recv(request, body + received, request->content_len - received);
    if (count <= 0) {
      httpd_resp_set_status(request, "400 Bad Request");
      return httpd_resp_sendstr(request, "Cannot read request body");
    }
    received += static_cast<size_t>(count);
  }
  body[received] = '\0';

  RuntimeConfig candidate = Settings.snapshot();
  char value[192];
  if (!formValue(body, "wifi_ssid", value, sizeof(value)) || strlen(value) >= sizeof(candidate.wifiSsid)) {
    httpd_resp_set_status(request, "400 Bad Request");
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, "{\"error\":\"SSID invalid.\"}");
  }
  strlcpy(candidate.wifiSsid, value, sizeof(candidate.wifiSsid));

  if (formValue(body, "clear_wifi", value, sizeof(value)) && strcmp(value, "1") == 0) {
    candidate.wifiSsid[0] = '\0';
    candidate.wifiPassword[0] = '\0';
  } else if (formValue(body, "wifi_password", value, sizeof(value)) && value[0] != '\0') {
    if (strlen(value) >= sizeof(candidate.wifiPassword)) {
      httpd_resp_set_status(request, "400 Bad Request");
      httpd_resp_set_type(request, "application/json");
      return httpd_resp_sendstr(request, "{\"error\":\"Parola Wi-Fi este prea lunga.\"}");
    }
    strlcpy(candidate.wifiPassword, value, sizeof(candidate.wifiPassword));
  }

  if (formValue(body, "ap_password", value, sizeof(value)) && value[0] != '\0') {
    if (strlen(value) >= sizeof(candidate.accessPointPassword)) {
      httpd_resp_set_status(request, "400 Bad Request");
      httpd_resp_set_type(request, "application/json");
      return httpd_resp_sendstr(request, "{\"error\":\"Parola access point este prea lunga.\"}");
    }
    strlcpy(candidate.accessPointPassword, value, sizeof(candidate.accessPointPassword));
  }

  if (!formValue(body, "storage_enabled", value, sizeof(value))) strlcpy(value, "0", sizeof(value));
  candidate.storageEnabled = strcmp(value, "1") == 0;
  if (!formValue(body, "photo_directory", value, sizeof(value)) ||
      strlen(value) >= sizeof(candidate.photoDirectory)) {
    httpd_resp_set_status(request, "400 Bad Request");
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, "{\"error\":\"Directorul foto este invalid.\"}");
  }
  strlcpy(candidate.photoDirectory, value, sizeof(candidate.photoDirectory));
  if (!formValue(body, "video_directory", value, sizeof(value)) ||
      strlen(value) >= sizeof(candidate.videoDirectory)) {
    httpd_resp_set_status(request, "400 Bad Request");
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, "{\"error\":\"Directorul video este invalid.\"}");
  }
  strlcpy(candidate.videoDirectory, value, sizeof(candidate.videoDirectory));
  if (!formValue(body, "minimum_free_mb", value, sizeof(value))) strlcpy(value, "128", sizeof(value));
  candidate.minimumFreeMb = static_cast<uint32_t>(strtoul(value, nullptr, 10));

  const bool cameraValuesValid =
      formInteger(body, "camera_profile", 0, 3, candidate.cameraProfile) &&
      formInteger(body, "frame_size", 1, 13, candidate.frameSize) &&
      formInteger(body, "jpeg_quality", 4, 63, candidate.jpegQuality) &&
      formInteger(body, "frame_rate", 1, 30, candidate.frameRate) &&
      formBoolean(body, "video_enabled", candidate.videoEnabled) &&
      formInteger(body, "video_fps", 1, 20, candidate.videoFrameRate) &&
      formInteger(body, "video_duration_seconds", 1, 600,
                  candidate.videoDurationSeconds) &&
      formInteger(body, "video_maximum_size_mb", 8, 1024,
                  candidate.videoMaximumSizeMb) &&
      formInteger(body, "brightness", -2, 2, candidate.brightness) &&
      formInteger(body, "contrast", -2, 2, candidate.contrast) &&
      formInteger(body, "saturation", -2, 2, candidate.saturation) &&
      formInteger(body, "special_effect", 0, 6, candidate.specialEffect) &&
      formBoolean(body, "white_balance", candidate.whiteBalance) &&
      formBoolean(body, "awb_gain", candidate.awbGain) &&
      formInteger(body, "wb_mode", 0, 4, candidate.whiteBalanceMode) &&
      formBoolean(body, "auto_exposure", candidate.autoExposure) &&
      formBoolean(body, "aec2", candidate.aec2) &&
      formInteger(body, "exposure_level", -2, 2, candidate.exposureLevel) &&
      formInteger(body, "exposure_value", 0, 1200, candidate.exposureValue) &&
      formBoolean(body, "auto_gain", candidate.autoGain) &&
      formInteger(body, "gain_value", 0, 30, candidate.gainValue) &&
      formInteger(body, "gain_ceiling", 0, 6, candidate.gainCeiling) &&
      formBoolean(body, "bpc", candidate.blackPixelCorrection) &&
      formBoolean(body, "wpc", candidate.whitePixelCorrection) &&
      formBoolean(body, "raw_gamma", candidate.rawGamma) &&
      formBoolean(body, "lens_correction", candidate.lensCorrection) &&
      formBoolean(body, "downsize", candidate.downsize) &&
      formBoolean(body, "horizontal_mirror", candidate.horizontalMirror) &&
      formBoolean(body, "vertical_flip", candidate.verticalFlip) &&
      formBoolean(body, "color_bar", candidate.colorBar) &&
      formBoolean(body, "flash_enabled", candidate.flashEnabled) &&
      formInteger(body, "flash_intensity", 0, 255, candidate.flashIntensity) &&
      formBoolean(body, "radar_enabled", candidate.radarEnabled) &&
      formBoolean(body, "radar_auto_photo", candidate.radarAutoPhoto) &&
      formBoolean(body, "radar_auto_video", candidate.radarAutoVideo) &&
      formInteger(body, "radar_trigger_mode", 0, 2, candidate.radarTriggerMode) &&
      formInteger(body, "radar_cooldown_seconds", 1, 3600,
                  candidate.radarCooldownSeconds);
  if (!cameraValuesValid) {
    httpd_resp_set_status(request, "400 Bad Request");
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, "{\"error\":\"Configuratia camerei este incompleta sau invalida.\"}");
  }

  String error;
  if (!Settings.save(candidate, error)) {
    httpd_resp_set_status(request, "400 Bad Request");
    httpd_resp_set_type(request, "application/json");
    const String json = String("{\"error\":") + jsonQuoted(error.c_str()) + '}';
    return httpd_resp_send(request, json.c_str(), json.length());
  }

  httpd_resp_set_type(request, "application/json");
  const esp_err_t result = httpd_resp_sendstr(request, "{\"saved\":true,\"restarting\":true}");
  xTaskCreate(restartTask, "restart", 1536, nullptr, 1, nullptr);
  return result;
}

esp_err_t filesApiHandler(httpd_req_t* request) {
  char path[160];
  if (!requestPath(request, path, sizeof(path))) {
    httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Missing or invalid path");
    return ESP_FAIL;
  }
  return Storage.listDirectory(path, request) ? ESP_OK : ESP_FAIL;
}

esp_err_t mediaHandler(httpd_req_t* request) {
  char path[160];
  if (!requestPath(request, path, sizeof(path))) {
    httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Missing or invalid path");
    return ESP_FAIL;
  }
  return Storage.streamFile(path, request, false) ? ESP_OK : ESP_FAIL;
}

esp_err_t downloadHandler(httpd_req_t* request) {
  char path[160];
  if (!requestPath(request, path, sizeof(path))) {
    httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Missing or invalid path");
    return ESP_FAIL;
  }
  return Storage.streamFile(path, request, true) ? ESP_OK : ESP_FAIL;
}

esp_err_t snapshotHandler(httpd_req_t* request) {
  FrameCopy frame;
  if (!Camera.copyLatest(frame, pdMS_TO_TICKS(500))) {
    return sendServiceUnavailable(request, "Camera frame unavailable");
  }

  httpd_resp_set_type(request, "image/jpeg");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  const esp_err_t result =
      httpd_resp_send(request, reinterpret_cast<const char*>(frame.data), frame.length);
  Camera.release(frame);
  return result;
}

esp_err_t savePhotoHandler(httpd_req_t* request) {
  const StorageStatus storage = Storage.status();
  if (!storage.mounted) {
    return sendServiceUnavailable(request, "microSD este dezactivat sau nu a fost montat.");
  }
  if (storage.recording) {
    httpd_resp_set_status(request, "409 Conflict");
    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(
        request, "Salvarea fotografiilor este suspendată în timpul înregistrării AVI.");
  }
  if (!Storage.requestPhotoSave()) {
    return sendServiceUnavailable(request, "Coada de salvare a fotografiilor este plină.");
  }
  httpd_resp_set_status(request, "202 Accepted");
  httpd_resp_set_type(request, "text/plain; charset=utf-8");
  return httpd_resp_sendstr(request, "Fotografia a fost pusă în coada de salvare.");
}

esp_err_t latestPhotoHandler(httpd_req_t* request) {
  return Storage.streamLatestPhoto(request) ? ESP_OK : ESP_FAIL;
}

esp_err_t startVideoHandler(httpd_req_t* request) {
  if (!Storage.requestVideoStart()) {
    return sendServiceUnavailable(request,
                                  "Recorder indisponibil, deja activ sau coada este plina.");
  }
  httpd_resp_set_status(request, "202 Accepted");
  httpd_resp_set_type(request, "text/plain; charset=utf-8");
  return httpd_resp_sendstr(request, "Pornirea recorderului a fost pusă în coadă.");
}

esp_err_t stopVideoHandler(httpd_req_t* request) {
  if (!Storage.requestVideoStop()) {
    return sendServiceUnavailable(request, "Recorder indisponibil sau coada este plina.");
  }
  httpd_resp_set_status(request, "202 Accepted");
  httpd_resp_set_type(request, "text/plain; charset=utf-8");
  return httpd_resp_sendstr(request, "Oprirea recorderului a fost pusă în coadă.");
}

esp_err_t latestVideoHandler(httpd_req_t* request) {
  return Storage.streamLatestVideo(request) ? ESP_OK : ESP_FAIL;
}

esp_err_t statusHandler(httpd_req_t* request) {
  const StorageStatus storage = Storage.status();
  const RadarStatus radar = Radar.status();
  const RuntimeConfig settings = Settings.snapshot();
  const IPAddress ip = WiFi.getMode() == WIFI_MODE_AP ? WiFi.softAPIP() : WiFi.localIP();
  const uint32_t age = Camera.latestFrameAgeMs();
  char json[1536];
  snprintf(json, sizeof(json),
           "{\"camera\":{\"ready\":%s,\"sequence\":%lu,\"frame_age_ms\":%lu,"
           "\"capture_failures\":%lu,\"recoveries\":%lu,"
           "\"stack_high_watermark\":%lu},"
           "\"storage\":{\"mounted\":%s,\"total_bytes\":%llu,\"used_bytes\":%llu,"
           "\"latest_photo\":\"%s\",\"latest_video\":\"%s\",\"enabled\":%s,"
           "\"minimum_free_mb\":%lu,\"video_enabled\":%s,\"recording\":%s,"
           "\"video_frames\":%lu,\"video_dropped_frames\":%lu,"
           "\"video_bytes\":%lu,\"video_elapsed_ms\":%lu},"
           "\"radar\":{\"enabled\":%s,\"connected\":%s,\"presence\":%s,"
           "\"moving\":%s,\"stationary\":%s,\"moving_distance_cm\":%u,"
           "\"moving_energy\":%u,\"stationary_distance_cm\":%u,"
           "\"stationary_energy\":%u,\"detection_distance_cm\":%u,"
           "\"last_frame_age_ms\":%lu,\"event_sequence\":%lu,"
           "\"photos_triggered\":%lu,\"videos_triggered\":%lu},"
           "\"network\":{\"mode\":\"%s\",\"ip\":\"%s\"},"
           "\"memory\":{\"free_heap\":%u,\"free_psram\":%u}}",
           Camera.ready() ? "true" : "false", static_cast<unsigned long>(Camera.latestSequence()),
           static_cast<unsigned long>(age),
           static_cast<unsigned long>(Camera.captureFailures()),
           static_cast<unsigned long>(Camera.recoveries()),
           static_cast<unsigned long>(Camera.stackHighWatermark()),
           storage.mounted ? "true" : "false",
           static_cast<unsigned long long>(storage.totalBytes),
           static_cast<unsigned long long>(storage.usedBytes), storage.latestPhoto,
           storage.latestVideo,
           settings.storageEnabled ? "true" : "false",
           static_cast<unsigned long>(settings.minimumFreeMb),
           settings.videoEnabled ? "true" : "false",
           storage.recording ? "true" : "false",
           static_cast<unsigned long>(storage.videoFrames),
           static_cast<unsigned long>(storage.videoDroppedFrames),
           static_cast<unsigned long>(storage.videoBytes),
           static_cast<unsigned long>(storage.videoElapsedMs),
           radar.enabled ? "true" : "false", radar.connected ? "true" : "false",
           radar.presence ? "true" : "false", radar.moving ? "true" : "false",
           radar.stationary ? "true" : "false",
           static_cast<unsigned>(radar.movingDistanceCm),
           static_cast<unsigned>(radar.movingEnergy),
           static_cast<unsigned>(radar.stationaryDistanceCm),
           static_cast<unsigned>(radar.stationaryEnergy),
           static_cast<unsigned>(radar.detectionDistanceCm),
           static_cast<unsigned long>(radar.lastFrameAgeMs),
           static_cast<unsigned long>(radar.eventSequence),
           static_cast<unsigned long>(radar.photosTriggered),
           static_cast<unsigned long>(radar.videosTriggered),
           WiFi.getMode() == WIFI_MODE_AP ? "ap" : "station", ip.toString().c_str(),
           ESP.getFreeHeap(), ESP.getFreePsram());
  httpd_resp_set_type(request, "application/json");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  return httpd_resp_sendstr(request, json);
}

esp_err_t streamHandler(httpd_req_t* request) {
  if (httpd_resp_set_type(request, kStreamType) != ESP_OK) {
    return ESP_FAIL;
  }
  httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");

  FrameCopy frame;
  uint32_t previousSequence = 0;
  esp_err_t result = ESP_OK;

  while (result == ESP_OK) {
    if (!Camera.copyLatest(frame, pdMS_TO_TICKS(500))) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    if (frame.sequence == previousSequence) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    previousSequence = frame.sequence;

    char header[128];
    const int headerLength = snprintf(header, sizeof(header), kStreamPart,
                                      static_cast<unsigned>(frame.length),
                                      static_cast<unsigned long>(frame.sequence));
    result = httpd_resp_send_chunk(request, kStreamBoundary, strlen(kStreamBoundary));
    if (result == ESP_OK) {
      result = httpd_resp_send_chunk(request, header, headerLength);
    }
    if (result == ESP_OK) {
      result = httpd_resp_send_chunk(request, reinterpret_cast<const char*>(frame.data),
                                     frame.length);
    }
  }

  Camera.release(frame);
  return result;
}

bool registerUri(httpd_handle_t server, const char* uri, httpd_method_t method,
                 esp_err_t (*handler)(httpd_req_t*)) {
  httpd_uri_t descriptor = {};
  descriptor.uri = uri;
  descriptor.method = method;
  descriptor.handler = handler;
  return httpd_register_uri_handler(server, &descriptor) == ESP_OK;
}

}  // namespace

bool startWebServers() {
  httpd_config_t webConfig = HTTPD_DEFAULT_CONFIG();
  webConfig.server_port = AppConfig::kWebPort;
  webConfig.stack_size = 8192;
  webConfig.max_uri_handlers = 16;
  webConfig.lru_purge_enable = true;

  if (httpd_start(&webServer, &webConfig) != ESP_OK) {
    Serial.println("[web] Cannot start HTTP server on port 80");
    return false;
  }

  bool success = true;
  success &= registerUri(webServer, "/", HTTP_GET, indexHandler);
  success &= registerUri(webServer, "/capture", HTTP_GET, snapshotHandler);
  success &= registerUri(webServer, "/api/capture", HTTP_POST, savePhotoHandler);
  success &= registerUri(webServer, "/api/status", HTTP_GET, statusHandler);
  success &= registerUri(webServer, "/photos/latest", HTTP_GET, latestPhotoHandler);
  success &= registerUri(webServer, "/videos/latest", HTTP_GET, latestVideoHandler);
  success &= registerUri(webServer, "/api/record/start", HTTP_POST, startVideoHandler);
  success &= registerUri(webServer, "/api/record/stop", HTTP_POST, stopVideoHandler);
  success &= registerUri(webServer, "/config", HTTP_GET, configPageHandler);
  success &= registerUri(webServer, "/api/config", HTTP_GET, configGetHandler);
  success &= registerUri(webServer, "/api/config", HTTP_POST, configPostHandler);
  success &= registerUri(webServer, "/files", HTTP_GET, filesPageHandler);
  success &= registerUri(webServer, "/api/files", HTTP_GET, filesApiHandler);
  success &= registerUri(webServer, "/media", HTTP_GET, mediaHandler);
  success &= registerUri(webServer, "/download", HTTP_GET, downloadHandler);

  httpd_config_t streamConfig = HTTPD_DEFAULT_CONFIG();
  streamConfig.server_port = AppConfig::kStreamPort;
  streamConfig.ctrl_port = webConfig.ctrl_port + 1;
  streamConfig.max_open_sockets = 2;
  streamConfig.lru_purge_enable = true;

  if (httpd_start(&streamServer, &streamConfig) != ESP_OK) {
    Serial.println("[web] Cannot start MJPEG server on port 81");
    return false;
  }
  success &= registerUri(streamServer, "/stream", HTTP_GET, streamHandler);
  success &= registerUri(streamServer, "/", HTTP_GET, streamHandler);

  Serial.println("[web] UI/snapshot: port 80; MJPEG stream: port 81");
  return success;
}
