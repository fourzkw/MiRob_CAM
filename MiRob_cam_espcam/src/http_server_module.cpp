#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <FS.h>
#include <vector>
#include <algorithm>

#include "config.h"
#include "camera_module.h"
#include "storage_module.h"
#include "http_server_module.h"
#include "log_module.h"

static WebServer server(80);
static uint8_t s_mode = DEVICE_MODE_STREAM;
static void (*s_modeChangeHandler)(uint8_t mode) = nullptr;

static uint8_t normalizeMode(uint8_t mode) {
    if (mode == DEVICE_MODE_PHOTO_ONLY) {
        return DEVICE_MODE_PHOTO_ONLY;
    }
    return DEVICE_MODE_STREAM;
}

void http_server_set_mode(uint8_t mode) {
    s_mode = normalizeMode(mode);
}

uint8_t http_server_get_mode() {
    return s_mode;
}

void http_server_set_mode_change_handler(void (*handler)(uint8_t mode)) {
    s_modeChangeHandler = handler;
}

static String modeText() {
    if (s_mode == DEVICE_MODE_PHOTO_ONLY) {
        return "Mode 2: Photo-only (stream disabled)";
    }
    return "Mode 1: Live preview + photo capture";
}

static void handleSetMode() {
    if (!server.hasArg("mode")) {
        server.send(400, "text/plain; charset=utf-8", "missing mode");
        return;
    }

    int m = server.arg("mode").toInt();
    uint8_t next = normalizeMode((uint8_t)m);
    http_server_set_mode(next);

    if (s_modeChangeHandler) {
        s_modeChangeHandler(next);
    }

    server.sendHeader("Cache-Control", "no-store");
    server.sendHeader("Location", "/");
    server.send(302, "text/plain; charset=utf-8", "ok");
}

static void handleRoot() {
    String html;
    html.reserve(3600);

    html += R"raw(
<!DOCTYPE html><html><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1"/>
<title>MiRob CAM</title>
<style>
body{font-family:sans-serif;background:#1a1a2e;color:#eee;margin:12px;}
h1{color:#0f3460;margin-bottom:6px;}
a{color:#e94560;}
.live{max-width:100%;border:2px solid #0f3460;border-radius:8px;}
.bt{display:inline-block;margin:8px 8px 8px 0;padding:10px 16px;background:#0f3460;color:#fff;text-decoration:none;border-radius:6px;}
.bt:hover{background:#e94560;}
.badge{display:inline-block;padding:4px 10px;border-radius:999px;background:#0f3460;color:#fff;font-size:12px;}
.hint{opacity:.85;line-height:1.5;}
</style></head><body>
<h1>MiRob CAM (OV3660)</h1>
)raw";

    html += "<p class=\"badge\">" + modeText() + "</p>";

    html += "<p>";
    html += "<a class=\"bt\" href=\"/setmode?mode=1\">Mode 1 (Preview)</a>";
    html += " <a class=\"bt\" href=\"/setmode?mode=2\">Mode 2 (Photo-only)</a>";
    html += "<br/>";
    html += "<a class=\"bt\" href=\"/capture\">Capture Photo</a>";
    html += " <a class=\"bt\" href=\"/list\">Photo Gallery</a>";
    html += "</p>";

    if (s_mode == DEVICE_MODE_STREAM) {
        html += R"raw(
<p class="hint">实时预览分辨率为 320x240（低延迟），保存的照片在模式1约为 640x480，在模式2约为 1600x1200。</p>
<img id="live" class="live" src="/stream?ts=0" alt="Live preview" style="max-height:70vh;">
<script>
(function(){
  var live = document.getElementById('live');
  function schedule(delayMs){
    setTimeout(function(){
      live.src = '/stream?ts=' + Date.now();
    }, delayMs);
  }
  live.onload = function(){ schedule(80); };
  live.onerror = function(){ schedule(400); };
  schedule(0);
})();
</script>
)raw";
    } else {
        html += R"raw(
<p class="hint">Photo-only mode: stream endpoint is disabled. Click "Capture Photo" to save one image to TF card.</p>
)raw";
    }

    html += R"raw(
<h2>Device Logs</h2>
<div class="hint">Recent runtime logs for quick diagnostics.</div>
<pre id="logBox" style="background:#0b1120;border-radius:8px;padding:8px;max-height:40vh;overflow:auto;font-size:12px;white-space:pre-wrap;"></pre>
<script>
function fetchLogs(){
  fetch('/logs').then(function(r){return r.text();}).then(function(t){
    var box=document.getElementById('logBox');
    box.textContent = t || '(no logs)';
    box.scrollTop = box.scrollHeight;
  }).catch(function(){
    // Ignore transient fetch errors.
  });
}
setInterval(fetchLogs, 2000);
fetchLogs();
</script>
)raw";

    html += "</body></html>";
    server.send(200, "text/html; charset=utf-8", html);
}

// Return one JPEG frame per request.
static void handleStream() {
    if (s_mode != DEVICE_MODE_STREAM) {
        server.send(404, "text/plain; charset=utf-8", "stream disabled (photo-only mode)");
        return;
    }

    camera_set_stream_profile();

    camera_fb_t* fb = camera_capture_frame();
    if (!fb) {
        log_append("[HTTP] /stream capture failed");
        server.send(503, "text/plain; charset=utf-8", "camera busy");
        return;
    }

    WiFiClient client = server.client();
    if (!client) {
        camera_return_frame(fb);
        return;
    }

    client.print("HTTP/1.1 200 OK\r\n");
    client.print("Content-Type: image/jpeg\r\n");
    client.print("Content-Length: ");
    client.print(fb->len);
    client.print("\r\n");
    client.print("Cache-Control: no-store, no-cache, must-revalidate, max-age=0\r\n");
    client.print("Pragma: no-cache\r\n");
    client.print("Connection: close\r\n");
    client.print("\r\n");
    client.write(fb->buf, fb->len);

    camera_return_frame(fb);
}

static void handleMode() {
    String json = String("{\"mode\":") + String((int)s_mode) + String("}");
    server.send(200, "application/json", json);
}

static void handleLogs() {
    String logs = log_get_all();
    server.send(200, "text/plain; charset=utf-8", logs);
}

static void handleCapture() {
    log_append("[HTTP] /capture called");

    if (!storage_ok()) {
        log_append("[HTTP] /capture SD card not ready");
        server.send(500, "text/plain; charset=utf-8", "SD card error");
        return;
    }

    String savedPath;
    if (!storage_capture_and_save(savedPath)) {
        log_append("[HTTP] /capture storage_capture_and_save failed");
        server.send(500, "text/plain; charset=utf-8", "capture failed");
        return;
    }

    log_append("[HTTP] /capture OK, savedPath=" + savedPath);

    int slash = savedPath.lastIndexOf('/');
    String name = (slash >= 0) ? savedPath.substring(slash + 1) : savedPath;

    server.sendHeader("Cache-Control", "no-store");
    server.sendHeader("Location", "/download?name=" + name);
    server.send(302, "text/plain; charset=utf-8", "captured");
}

static void handleList() {
    log_append("[HTTP] /list called");

    if (!storage_ok()) {
        String html = R"raw(
<!DOCTYPE html><html><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1"/>
<title>Photo Gallery</title>
<style>
body{font-family:sans-serif;background:#1a1a2e;color:#eee;margin:12px;}
h1{color:#0f3460;} a{color:#e94560;}
</style></head><body>
<h1>Photo Gallery</h1>
<p>SD card is not ready.</p>
<p><a href="/">Back to Home</a></p>
</body></html>
)raw";
        server.send(500, "text/html; charset=utf-8", html);
        return;
    }

    File root = storage_open_photo_dir();
    if (!root || !root.isDirectory()) {
        String html = R"raw(
<!DOCTYPE html><html><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1"/>
<title>Photo Gallery</title>
<style>
body{font-family:sans-serif;background:#1a1a2e;color:#eee;margin:12px;}
h1{color:#0f3460;} a{color:#e94560;}
</style></head><body>
<h1>Photo Gallery</h1>
<p>Cannot open photo directory.</p>
<p><a href="/">Back to Home</a></p>
</body></html>
)raw";
        server.send(500, "text/html; charset=utf-8", html);
        return;
    }

    // 简单分页：每页固定显示 5 张图片，通过 ?page=N 选择页码
    int page = 1;
    if (server.hasArg("page")) {
        int p = server.arg("page").toInt();
        if (p > 0) {
            page = p;
        }
    }
    const int kPageSize = 5;
    const int kOffset = (page - 1) * kPageSize;

    String html = R"raw(
<!DOCTYPE html><html><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1"/>
<title>Photo Gallery</title>
<style>
body{font-family:sans-serif;background:#1a1a2e;color:#eee;margin:12px;}
h1{color:#0f3460;} a{color:#e94560;}
.grid{display:flex;flex-wrap:wrap;gap:10px;margin-top:8px;}
.item{background:#111827;padding:6px;border-radius:6px;width:150px;box-sizing:border-box;text-align:center;font-size:12px;}
.item img{max-width:100%;max-height:110px;display:block;margin:0 auto 4px;border-radius:4px;}
.meta{margin-top:2px;opacity:.8;}
.btn-sm{display:inline-block;margin-top:4px;padding:4px 8px;background:#0f3460;color:#fff;text-decoration:none;border-radius:4px;font-size:11px;}
.btn-sm:hover{background:#e94560;}
</style></head><body>
<h1>Photo Gallery</h1>
<p><a href="/">Back to Home</a></p>
<div class="grid">
)raw";

    struct PhotoItem {
        String fullName;   // 包含目录的完整路径
        size_t fileSize;   // 文件大小（字节）
    };

    std::vector<PhotoItem> items;

    File f = root.openNextFile();
    while (f) {
        if (!f.isDirectory()) {
            String fullName = String(f.name());
            if (fullName.endsWith(".jpg") || fullName.endsWith(".jpeg")) {
                PhotoItem item;
                item.fullName = fullName;
                item.fileSize = f.size();
                items.push_back(item);
            }
        }
        f = root.openNextFile();
    }

    // 关闭目录句柄
    root.close();

    // 如果没有任何照片
    if (items.empty()) {
        html += "<p>No photos yet.</p>";
        html += "</div>";
        html += "</body></html>";
        server.send(200, "text/html; charset=utf-8", html);
        return;
    }

    // 按文件名倒序排序（新的编号 / 时间靠后的排在前面）
    std::sort(items.begin(), items.end(), [](const PhotoItem& a, const PhotoItem& b) {
        return a.fullName > b.fullName;
    });

    const int totalItems = static_cast<int>(items.size());
    int startIndex = kOffset;
    if (startIndex < 0) {
        startIndex = 0;
    }
    if (startIndex >= totalItems) {
        startIndex = totalItems;
    }
    int endIndex = startIndex + kPageSize;
    if (endIndex > totalItems) {
        endIndex = totalItems;
    }

    for (int i = startIndex; i < endIndex; ++i) {
        const PhotoItem& item = items[i];

        int slash = item.fullName.lastIndexOf('/');
        String baseName = (slash >= 0) ? item.fullName.substring(slash + 1) : item.fullName;

        // 文件大小（KB）
        unsigned long sizeKB = (item.fileSize + 1023) / 1024;

        // 根据当前设备模式显示分辨率：
        // Mode 1: 640x480（VGA），Mode 2: 1600x1200（UXGA）
        const char* resText = (s_mode == DEVICE_MODE_PHOTO_ONLY)
            ? "1600x1200"
            : "640x480";
        String meta = String("分辨率 ") + String(resText) + String("，大小 ") + String(sizeKB) + String(" KB");

        html += "<div class=\"item\">";
        html += "<img src=\"/download?name=" + baseName + "\" alt=\"" + baseName + "\">";
        html += "<div>" + baseName + "</div>";
        html += "<div class=\"meta\">" + meta + "</div>";
        html += "<a class=\"btn-sm\" href=\"/download?name=" + baseName + "\">Download</a>";
        html += "</div>";
    }

    bool hasMore = (endIndex < totalItems);

    html += "</div>";

    // 分页导航
    html += "<div style=\"margin-top:12px;\">";
    html += "<span>Page " + String(page) + "</span>";
    if (page > 1) {
        int prev = page - 1;
        html += " &nbsp; <a href=\"/list?page=" + String(prev) + "\">Prev</a>";
    }
    if (hasMore) {
        int next = page + 1;
        html += " &nbsp; <a href=\"/list?page=" + String(next) + "\">Next</a>";
    }
    html += "</div>";

    html += "</body></html>";
    server.send(200, "text/html; charset=utf-8", html);
}

static void handleDownload() {
    log_append("[HTTP] /download called");

    if (!storage_ok()) {
        log_append("[HTTP] /download SD card not ready");
        server.send(500, "text/plain", "SD card error");
        return;
    }

    if (!server.hasArg("name")) {
        log_append("[HTTP] /download missing name param");
        server.send(400, "text/plain", "missing name");
        return;
    }

    String name = server.arg("name");
    if (name.indexOf("/") >= 0 || name.indexOf("..") >= 0) {
        log_append("[HTTP] /download forbidden name=" + name);
        server.send(403, "text/plain", "forbidden");
        return;
    }

    String path = String(PHOTO_DIR "/") + name;
    if (!storage_file_exists(path)) {
        server.send(404, "text/plain", "not found");
        return;
    }

    File file = storage_open_file(path);
    if (!file) {
        log_append("[HTTP] /download open file failed");
        server.send(500, "text/plain", "open failed");
        return;
    }

    server.streamFile(file, "image/jpeg");
    file.close();
}

static void handleNotFound() {
    const String uri = server.uri();

    if (uri == "/favicon.ico") {
        server.send(204, "image/x-icon", "");
        return;
    }

    String msg = "not found: " + uri + " method=" + String((int)server.method());
    log_append("[HTTP] " + msg);
    server.send(404, "text/plain; charset=utf-8", msg);
}

void http_server_init() {
    server.on("/", handleRoot);
    server.on("/stream", handleStream);
    server.on("/mode", handleMode);
    server.on("/setmode", handleSetMode);
    server.on("/logs", handleLogs);
    server.on("/capture", handleCapture);
    server.on("/list", handleList);
    server.on("/download", handleDownload);
    server.onNotFound(handleNotFound);

    server.begin();
    log_append("HTTP server started. Open http://" + WiFi.softAPIP().toString());
}

void http_server_handle_client() {
    server.handleClient();
}
