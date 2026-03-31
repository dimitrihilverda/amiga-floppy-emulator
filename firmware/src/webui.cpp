#include "webui.h"
#include "floppy.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

// External reference to the floppy drive instance
extern FloppyDrive floppy;

// Web server on port 80
static WebServer server(WEB_PORT);

// Upload buffer in PSRAM
static uint8_t* uploadBuffer = nullptr;
static size_t uploadSize = 0;
static bool uploadComplete = false;
static String uploadFilename = "";

// =============================================================================
// HTML Interface (embedded, mobile-friendly)
// =============================================================================
static const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>Amiga Floppy</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{background:#1a1a2e;color:#e0e0e0;font-family:sans-serif;padding:15px;min-height:100vh}
h1{color:#00d4ff;text-align:center;font-size:1.4em;margin-bottom:5px}
.sub{text-align:center;color:#888;font-size:.8em;margin-bottom:20px}
.card{background:#16213e;border:1px solid #0f3460;border-radius:10px;padding:15px;margin:10px 0}
.status{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.stat{background:#0f3460;border-radius:6px;padding:8px;text-align:center}
.stat .val{color:#00d4ff;font-size:1.3em;font-weight:bold}
.stat .lbl{color:#888;font-size:.75em}
.upload-zone{border:2px dashed #0f3460;border-radius:10px;padding:30px;text-align:center;
  transition:all .3s;cursor:pointer}
.upload-zone:hover,.upload-zone.drag{border-color:#00d4ff;background:#0f3460}
.upload-zone input{display:none}
.btn{display:block;width:100%;padding:12px;border:none;border-radius:8px;font-size:1em;
  cursor:pointer;margin:8px 0;font-weight:bold;transition:all .2s}
.btn-blue{background:#00d4ff;color:#1a1a2e}
.btn-red{background:#ff6b6b;color:#fff}
.btn:disabled{opacity:.4;cursor:not-allowed}
.progress{height:6px;background:#0f3460;border-radius:3px;margin:10px 0;overflow:hidden}
.progress-bar{height:100%;background:#00d4ff;border-radius:3px;width:0;transition:width .3s}
.log{background:#0d1117;border-radius:6px;padding:10px;font-family:monospace;font-size:.8em;
  max-height:150px;overflow-y:auto;margin-top:10px}
.ok{color:#4ade80}.err{color:#ff6b6b}.warn{color:#ffd93d}
</style>
</head>
<body>
<h1>AMIGA FLOPPY</h1>
<p class="sub">ESP32-S3 Floppy Emulator</p>

<div class="card">
  <div class="status">
    <div class="stat"><div class="val" id="state">IDLE</div><div class="lbl">Status</div></div>
    <div class="stat"><div class="val" id="track">0/0</div><div class="lbl">Cyl / Side</div></div>
    <div class="stat"><div class="val" id="disk">-</div><div class="lbl">Disk</div></div>
    <div class="stat"><div class="val" id="mem">-</div><div class="lbl">Free RAM</div></div>
  </div>
</div>

<div class="card">
  <div class="upload-zone" id="dropzone" onclick="document.getElementById('file').click()">
    <div style="font-size:2em;margin-bottom:8px">&#128190;</div>
    <div>Tap om een <strong>.ADF</strong> bestand te selecteren</div>
    <div style="color:#888;font-size:.8em;margin-top:5px">of sleep het bestand hierheen</div>
    <input type="file" id="file" accept=".adf,.ADF">
  </div>
  <div class="progress" id="progwrap" style="display:none">
    <div class="progress-bar" id="progbar"></div>
  </div>
  <button class="btn btn-blue" id="uploadbtn" disabled onclick="upload()">Upload ADF</button>
  <button class="btn btn-red" onclick="eject()">Eject Disk</button>
</div>

<div class="card">
  <div class="log" id="log"></div>
</div>

<script>
const $ = id => document.getElementById(id);
let selectedFile = null;

// File selection
$('file').onchange = e => {
  selectedFile = e.target.files[0];
  if (selectedFile) {
    if (selectedFile.size !== 901120) {
      log('Bestand is ' + selectedFile.size + ' bytes, verwacht 901120 (DD ADF)', 'warn');
    }
    log('Geselecteerd: ' + selectedFile.name + ' (' + (selectedFile.size/1024).toFixed(0) + ' KB)', 'ok');
    $('uploadbtn').disabled = false;
  }
};

// Drag & drop
const dz = $('dropzone');
dz.ondragover = e => { e.preventDefault(); dz.classList.add('drag'); };
dz.ondragleave = () => dz.classList.remove('drag');
dz.ondrop = e => {
  e.preventDefault(); dz.classList.remove('drag');
  if (e.dataTransfer.files.length) {
    $('file').files = e.dataTransfer.files;
    $('file').onchange({target:$('file')});
  }
};

// Upload
function upload() {
  if (!selectedFile) return;
  $('uploadbtn').disabled = true;
  $('progwrap').style.display = 'block';

  const xhr = new XMLHttpRequest();
  xhr.open('POST', '/upload');
  xhr.setRequestHeader('X-Filename', selectedFile.name);

  xhr.upload.onprogress = e => {
    if (e.lengthComputable) {
      const pct = (e.loaded / e.total * 100).toFixed(0);
      $('progbar').style.width = pct + '%';
      log('Uploading... ' + pct + '%');
    }
  };

  xhr.onload = () => {
    if (xhr.status === 200) {
      log('Upload compleet! Disk geladen.', 'ok');
    } else {
      log('Upload fout: ' + xhr.responseText, 'err');
    }
    $('uploadbtn').disabled = false;
    $('progwrap').style.display = 'none';
    $('progbar').style.width = '0';
    updateStatus();
  };

  xhr.onerror = () => {
    log('Verbindingsfout', 'err');
    $('uploadbtn').disabled = false;
  };

  xhr.send(selectedFile);
}

// Eject
function eject() {
  fetch('/eject').then(r => r.text()).then(t => {
    log(t, 'ok');
    updateStatus();
  });
}

// Status polling
function updateStatus() {
  fetch('/status').then(r => r.json()).then(d => {
    $('state').textContent = d.state;
    $('track').textContent = d.cylinder + '/' + d.side;
    $('disk').textContent = d.disk || '-';
    $('mem').textContent = (d.freeHeap / 1024).toFixed(0) + ' KB';
    $('state').style.color = d.state === 'READING' ? '#4ade80' : '#00d4ff';
  }).catch(() => {});
}

// Log
function log(msg, cls) {
  const el = $('log');
  const t = new Date().toLocaleTimeString();
  el.innerHTML += '<div class="' + (cls||'') + '">[' + t + '] ' + msg + '</div>';
  el.scrollTop = el.scrollHeight;
}

// Poll status every 2 seconds
setInterval(updateStatus, 2000);
updateStatus();
log('Interface gereed. Verbonden met ESP32-S3.', 'ok');
</script>
</body>
</html>
)rawliteral";

// =============================================================================
// Web Server Handlers
// =============================================================================

static void handleRoot() {
    server.send(200, "text/html", HTML_PAGE);
}

static void handleStatus() {
    String state;
    switch (floppy.getState()) {
        case DRIVE_IDLE:    state = "IDLE"; break;
        case DRIVE_SPIN_UP: state = "SPIN_UP"; break;
        case DRIVE_READY:   state = "READY"; break;
        case DRIVE_READING: state = "READING"; break;
        default:            state = "UNKNOWN"; break;
    }

    String json = "{";
    json += "\"state\":\"" + state + "\",";
    json += "\"cylinder\":" + String(floppy.getCylinder()) + ",";
    json += "\"side\":" + String(floppy.getSide()) + ",";
    json += "\"disk\":\"" + (floppy.isDiskLoaded() ? uploadFilename : String("-")) + "\",";
    json += "\"freeHeap\":" + String(ESP.getFreeHeap());
    json += "}";

    server.send(200, "application/json", json);
}

static void handleUpload() {
    // Read the raw POST body (ADF file)
    if (!server.hasHeader("X-Filename")) {
        server.send(400, "text/plain", "Missing filename header");
        return;
    }

    uploadFilename = server.header("X-Filename");

    // Get the raw body
    if (server.hasArg("plain")) {
        // Data came as request body
    }

    // The actual upload is handled via streaming
    server.send(200, "text/plain", "OK");
}

// Streaming upload handler
static void handleUploadData() {
    HTTPUpload& upload = server.upload();

    if (upload.status == UPLOAD_FILE_START) {
        uploadFilename = upload.filename;
        uploadSize = 0;
        uploadComplete = false;

        if (uploadBuffer) free(uploadBuffer);
        uploadBuffer = (uint8_t*)ps_malloc(ADF_FILE_SIZE + 1024);
        if (!uploadBuffer) {
            Serial.println("[WEB] Failed to allocate upload buffer!");
            return;
        }

        Serial.printf("[WEB] Upload start: %s\n", uploadFilename.c_str());
    }
    else if (upload.status == UPLOAD_FILE_WRITE) {
        if (uploadBuffer && uploadSize + upload.currentSize <= ADF_FILE_SIZE + 1024) {
            memcpy(&uploadBuffer[uploadSize], upload.buf, upload.currentSize);
            uploadSize += upload.currentSize;
        }
    }
    else if (upload.status == UPLOAD_FILE_END) {
        Serial.printf("[WEB] Upload complete: %d bytes\n", uploadSize);

        if (uploadSize == ADF_FILE_SIZE) {
            if (floppy.loadADF(uploadBuffer, uploadSize)) {
                Serial.println("[WEB] ADF loaded into emulator!");
                uploadComplete = true;
            } else {
                Serial.println("[WEB] Failed to load ADF!");
            }
        } else {
            Serial.printf("[WEB] Invalid ADF size: %d (expected %d)\n",
                          uploadSize, ADF_FILE_SIZE);
        }

        if (uploadBuffer) {
            free(uploadBuffer);
            uploadBuffer = nullptr;
        }
    }
}

static void handleRawUpload() {
    // For raw POST body uploads (from XHR)
    size_t contentLen = server.arg("plain").length();

    if (contentLen == 0) {
        // Try to read from client directly
        WiFiClient client = server.client();
        contentLen = server.header("Content-Length").toInt();

        if (contentLen != ADF_FILE_SIZE) {
            server.send(400, "text/plain",
                       "Invalid size: " + String(contentLen) + " (need 901120)");
            return;
        }

        uploadFilename = server.header("X-Filename");
        if (uploadFilename.length() == 0) uploadFilename = "unknown.adf";

        uint8_t* buf = (uint8_t*)ps_malloc(ADF_FILE_SIZE);
        if (!buf) {
            server.send(500, "text/plain", "Out of memory");
            return;
        }

        size_t received = 0;
        unsigned long timeout = millis() + 30000;  // 30s timeout

        while (received < ADF_FILE_SIZE && millis() < timeout) {
            if (client.available()) {
                size_t toRead = min((size_t)client.available(),
                                    ADF_FILE_SIZE - received);
                size_t read = client.read(&buf[received], toRead);
                received += read;
            }
            yield();
        }

        if (received == ADF_FILE_SIZE) {
            if (floppy.loadADF(buf, ADF_FILE_SIZE)) {
                server.send(200, "text/plain", "ADF loaded: " + uploadFilename);
            } else {
                server.send(500, "text/plain", "Failed to load ADF");
            }
        } else {
            server.send(400, "text/plain",
                       "Incomplete: " + String(received) + "/" + String(ADF_FILE_SIZE));
        }

        free(buf);
    }
}

static void handleEject() {
    floppy.ejectDisk();
    uploadFilename = "";
    server.send(200, "text/plain", "Disk ejected");
}

// =============================================================================
// Public API
// =============================================================================

void webui_begin() {
    // Start WiFi AP
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_SSID, WIFI_PASSWORD, WIFI_CHANNEL);

    IPAddress ip = WiFi.softAPIP();
    Serial.printf("[WEB] WiFi AP started: %s\n", WIFI_SSID);
    Serial.printf("[WEB] IP: %s\n", ip.toString().c_str());
    Serial.printf("[WEB] Password: %s\n", WIFI_PASSWORD);

    // mDNS
    if (MDNS.begin("amigafloppy")) {
        Serial.println("[WEB] mDNS: amigafloppy.local");
    }

    // Configure allowed headers for CORS and filename
    const char* headerkeys[] = {"X-Filename", "Content-Length"};
    server.collectHeaders(headerkeys, 2);

    // Routes
    server.on("/", HTTP_GET, handleRoot);
    server.on("/status", HTTP_GET, handleStatus);
    server.on("/upload", HTTP_POST, handleRawUpload);
    server.on("/upload_form", HTTP_POST, []() {
        server.send(200, "text/plain",
                    uploadComplete ? "ADF loaded!" : "Upload failed");
    }, handleUploadData);
    server.on("/eject", HTTP_GET, handleEject);

    server.begin();
    Serial.println("[WEB] Server started on port 80");
}

void webui_update() {
    server.handleClient();
}
