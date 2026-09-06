#include "web_server.hpp"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_http_server.h>

#include "app_config.hpp"
#include "camera_service.hpp"
#include "config_service.hpp"
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
    <button onclick="savePhoto()">Salvează fotografie</button>
    <a href="/capture" target="_blank">Snapshot curent</a>
    <a href="/photos/latest" target="_blank">Ultima fotografie salvată</a>
  </div>
  <pre id="status">Se încarcă...</pre>
  <script>
    document.getElementById('stream').src = `${location.protocol}//${location.hostname}:81/stream`;
    async function savePhoto(){
      const response=await fetch('/api/capture',{method:'POST'});
      alert(await response.text());
      setTimeout(loadStatus,1000);
    }
    async function loadStatus(){
      try{document.getElementById('status').textContent=JSON.stringify(await (await fetch('/api/status')).json(),null,2)}
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
label{display:block;margin:12px 0 5px}input[type=text],input[type=password],input[type=number]{box-sizing:border-box;width:100%;padding:10px;border:1px solid #555;border-radius:5px;background:#222;color:#fff}
.hint{color:#aaa;font-size:.9rem}.row{display:flex;gap:12px}.row>*{flex:1}#message{white-space:pre-wrap;padding:10px;border-radius:6px}
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
<div class="hint">Sub acest prag nu se mai salvează fotografii. Înregistrarea video va folosi directorul de filme într-o etapă ulterioară.</div>
</fieldset><button type="submit">Salvează și repornește</button></form><div id="message"></div>
<script>
const $=id=>document.getElementById(id);
async function load(){try{const c=await (await fetch('/api/config')).json();$('ssid').value=c.wifi_ssid;$('wifiPass').placeholder=c.wifi_password_set?'Parolă salvată; lasă gol pentru a o păstra':'Introduce parola';$('apPass').placeholder=c.ap_password_set?'Parolă salvată; lasă gol pentru a o păstra':'Minimum 8 caractere';$('sdEnabled').checked=c.storage_enabled;$('photoDir').value=c.photo_directory;$('videoDir').value=c.video_directory;$('minFree').value=c.minimum_free_mb}catch(e){$('message').textContent='Eroare: '+e}}
$('settings').addEventListener('submit',async e=>{e.preventDefault();const p=new URLSearchParams();p.set('wifi_ssid',$('ssid').value);p.set('wifi_password',$('wifiPass').value);p.set('clear_wifi',$('clearWifi').checked?'1':'0');p.set('ap_password',$('apPass').value);p.set('storage_enabled',$('sdEnabled').checked?'1':'0');p.set('photo_directory',$('photoDir').value);p.set('video_directory',$('videoDir').value);p.set('minimum_free_mb',$('minFree').value);$('message').textContent='Se salvează...';try{const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p});const j=await r.json();if(!r.ok)throw new Error(j.error||r.statusText);$('message').textContent='Setările au fost salvate. Placa repornește; reconectează-te la noua rețea.'}catch(e){$('message').textContent='Eroare: '+e.message}});
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
async function load(path){$('info').className='muted';$('info').textContent='Se încarcă...';$('preview').replaceChildren();try{const r=await fetch('/api/files?path='+enc(path));if(!r.ok)throw new Error(await r.text());const data=await r.json();current=data.path;$('path').textContent=current;$('up').disabled=current==='/';const body=$('items');body.replaceChildren();data.entries.sort((a,b)=>Number(b.directory)-Number(a.directory)||(b.saved_at||'').localeCompare(a.saved_at||'')||a.path.localeCompare(b.path)).forEach(e=>{const tr=document.createElement('tr'),name=basename(e.path),type=e.directory?'director':mediaType(e.path),nameCell=document.createElement('td'),open=document.createElement('button');open.className='name';open.textContent=name;open.onclick=()=>e.directory?load(e.path):openFile(e.path);nameCell.appendChild(open);const typeCell=document.createElement('td');typeCell.className='type';typeCell.textContent=type;const date=document.createElement('td');date.textContent=saved(e);const size=document.createElement('td');size.className='size';size.textContent=e.directory?'—':human(e.size);const actions=document.createElement('td');if(!e.directory){const download=document.createElement('a');download.href='/download?path='+enc(e.path);download.textContent='Descarcă';actions.appendChild(download)}tr.append(nameCell,typeCell,date,size,actions);body.appendChild(tr)});$('info').textContent=data.entries.length+' elemente'+(data.truncated?' (lista limitată)':'')}catch(e){$('info').className='error';$('info').textContent=e.message}}
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
  if (request->content_len == 0 || request->content_len >= 1024) {
    httpd_resp_set_status(request, "400 Bad Request");
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, "{\"error\":\"Cerere invalida sau prea mare.\"}");
  }

  char body[1024];
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
  if (!Storage.requestPhotoSave()) {
    return sendServiceUnavailable(request, "microSD unavailable or capture queue full");
  }
  httpd_resp_set_status(request, "202 Accepted");
  httpd_resp_set_type(request, "text/plain; charset=utf-8");
  return httpd_resp_sendstr(request, "Fotografia a fost pusă în coada de salvare.");
}

esp_err_t latestPhotoHandler(httpd_req_t* request) {
  return Storage.streamLatestPhoto(request) ? ESP_OK : ESP_FAIL;
}

esp_err_t statusHandler(httpd_req_t* request) {
  const StorageStatus storage = Storage.status();
  const RuntimeConfig settings = Settings.snapshot();
  const IPAddress ip = WiFi.getMode() == WIFI_MODE_AP ? WiFi.softAPIP() : WiFi.localIP();
  const uint32_t age = Camera.latestFrameAgeMs();
  char json[640];
  snprintf(json, sizeof(json),
           "{\"camera\":{\"ready\":%s,\"sequence\":%lu,\"frame_age_ms\":%lu},"
           "\"storage\":{\"mounted\":%s,\"total_bytes\":%llu,\"used_bytes\":%llu,"
           "\"latest_photo\":\"%s\",\"enabled\":%s,\"minimum_free_mb\":%lu},"
           "\"network\":{\"mode\":\"%s\",\"ip\":\"%s\"},"
           "\"memory\":{\"free_heap\":%u,\"free_psram\":%u}}",
           Camera.ready() ? "true" : "false", static_cast<unsigned long>(Camera.latestSequence()),
           static_cast<unsigned long>(age), storage.mounted ? "true" : "false",
           static_cast<unsigned long long>(storage.totalBytes),
           static_cast<unsigned long long>(storage.usedBytes), storage.latestPhoto,
           settings.storageEnabled ? "true" : "false",
           static_cast<unsigned long>(settings.minimumFreeMb),
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
