#include "api_routes.h"

#include "../../config.h"
#include <Update.h>

static bool g_webUpdateIsFilesystem = false;

void registerOtaRoutes(AsyncWebServer &server) {
  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request) {
    String html = F(
        "<!DOCTYPE html><html><head>"
        "<meta charset='utf-8'><meta name='viewport' "
        "content='width=device-width,initial-scale=1'>"
        "<title>Smart-Column S3 - OTA Update</title>"
        "<style>"
        "body{font-family:Arial,sans-serif;max-width:600px;margin:50px "
        "auto;padding:20px;background:#f5f5f5}"
        ".container{background:white;padding:30px;border-radius:10px;box-"
        "shadow:0 2px 10px rgba(0,0,0,0.1)}"
        "h1{color:#333;margin-bottom:20px}"
        ".info{background:#e3f2fd;padding:15px;border-radius:5px;margin-bottom:"
        "20px}"
        "input[type=file]{width:100%;padding:10px;margin:10px 0;border:2px "
        "dashed #ccc;border-radius:5px;cursor:pointer}"
        "input[type=submit]{background:#4CAF50;color:white;padding:15px "
        "30px;border:none;border-radius:5px;cursor:pointer;font-size:16px;"
        "width:100%}"
        "input[type=submit]:hover{background:#45a049}"
        ".progress{display:none;margin-top:20px}"
        ".progress-bar{width:100%;height:30px;background:#ddd;border-radius:"
        "15px;overflow:hidden}"
        ".progress-fill{height:100%;background:#4CAF50;transition:width 0.3s}"
        ".status{margin-top:15px;padding:10px;border-radius:5px;text-align:"
        "center}"
        ".success{background:#d4edda;color:#155724}"
        ".error{background:#f8d7da;color:#721c24}"
        "</style>"
        "</head><body>"
        "<div class='container'>"
        "<h1>🔧 Firmware Update</h1>"
        "<div class='info'>"
        "<strong>Current version:</strong> " FW_VERSION "<br>"
        "<strong>Build date:</strong> " __DATE__ " " __TIME__ "<br>"
        "<strong>Platform:</strong> ESP32-S3"
        "</div>"
        "<form method='POST' action='/update' enctype='multipart/form-data' "
        "id='upload_form'>"
        "<input type='file' name='update' accept='.bin' required>"
        "<input type='submit' value='Upload Firmware'>"
        "</form>"
        "<div class='progress' id='progress'>"
        "<div class='progress-bar'><div class='progress-fill' "
        "id='progress-fill'></div></div>"
        "<div id='status'></div>"
        "</div>"
        "</div>"
        "<script>"
        "document.getElementById('upload_form').addEventListener('submit',"
        "function(e){"
        "e.preventDefault();"
        "var formData=new FormData(this);"
        "var xhr=new XMLHttpRequest();"
        "document.getElementById('progress').style.display='block';"
        "xhr.upload.addEventListener('progress',function(e){"
        "if(e.lengthComputable){"
        "var percent=(e.loaded/e.total)*100;"
        "document.getElementById('progress-fill').style.width=percent+'%';"
        "document.getElementById('status').textContent=Math.round(percent)+'%';"
        "}"
        "});"
        "xhr.addEventListener('load',function(){"
        "if(xhr.status===200){"
        "document.getElementById('status').className='status success';"
        "document.getElementById('status').textContent='✓ Update successful! "
        "Rebooting...';"
        "setTimeout(function(){location.href='/';},5000);"
        "}else{"
        "document.getElementById('status').className='status error';"
        "document.getElementById('status').textContent='✗ Update failed: "
        "'+xhr.responseText;"
        "}"
        "});"
        "xhr.open('POST','/update');"
        "xhr.send(formData);"
        "});"
        "</script>"
        "</body></html>");
    request->send(200, "text/html", html);
  });

  server.on(
      "/update", HTTP_POST,
      [](AsyncWebServerRequest *request) {
        bool shouldReboot = !Update.hasError();
        AsyncWebServerResponse *response = request->beginResponse(
            200, "text/plain", shouldReboot ? "OK" : "FAIL");
        response->addHeader("Connection", "close");
        request->send(response);

        if (shouldReboot) {
          LOG_I("OTA: %s update successful, rebooting...",
                g_webUpdateIsFilesystem ? "filesystem" : "firmware");
          g_webUpdateIsFilesystem = false;
          delay(1000);
          ESP.restart();
        } else {
          LOG_E("OTA: Update failed!");
          g_webUpdateIsFilesystem = false;
        }
      },
      [](AsyncWebServerRequest *request, String filename, size_t index,
         uint8_t *data, size_t len, bool final) {
        if (!index) {
          String lowerFilename = filename;
          lowerFilename.toLowerCase();
          g_webUpdateIsFilesystem =
              lowerFilename.indexOf("littlefs") >= 0 ||
              lowerFilename.indexOf("spiffs") >= 0 ||
              lowerFilename.endsWith(".fs.bin");

          LOG_I("OTA: Update start: %s (%s)", filename.c_str(),
                g_webUpdateIsFilesystem ? "filesystem" : "firmware");

          int command = g_webUpdateIsFilesystem ? U_SPIFFS : U_FLASH;
          if (!Update.begin(UPDATE_SIZE_UNKNOWN, command)) {
            Update.printError(Serial);
          }
        }

        if (Update.write(data, len) != len) {
          Update.printError(Serial);
        }

        if (final) {
          if (Update.end(true)) {
            LOG_I("OTA: %s update success: %u bytes",
                  g_webUpdateIsFilesystem ? "filesystem" : "firmware",
                  index + len);
          } else {
            Update.printError(Serial);
          }
        }
      });
}
