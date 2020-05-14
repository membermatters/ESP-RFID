#include <ESP8266WebServer.h>
#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <WebSocketsServer.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include "secrets.h"

#define TAG_LENGTH 14
#define HOST_NAME "cardreader"
#define DISPLAY_NAME "ESP RFID Card Reader"
#define SERIAL_DEBUG true
#define BUZZER_PIN 4

SoftwareSerial mySerial(5, 6); // RX, TX

ESP8266WiFiMulti WiFiMulti;
WebSocketsServer webSocket = WebSocketsServer(81);
ESP8266WebServer http(80);

int wsDebug = false;

void log(String entry) {
  if (SERIAL_DEBUG) {
    Serial.println(entry);
  }
  
  if (wsDebug) {
    webSocket.broadcastTXT(entry);
  }
}

void httpRoot() {
  String html = "<html>"

                "<head>"

                "<title>" + String(DISPLAY_NAME) + "</title>"
                "<link rel='stylesheet' href='https://maxcdn.bootstrapcdn.com/bootstrap/4.0.0/css/bootstrap.min.css'>"

                "</head>"

                "<body class='p-3' style='max-width: 800px; margin: 0 auto;'>"

                "<h1>" + String(DISPLAY_NAME) + "</h1>"

                "<a onclick='fetch(`/debug`) && addLog(`[LOCAL] Requesting debug mode...`, `alert alert-primary`);' type='button' class='mr-3 mb-3 text-white btn btn-primary'>Debug mode</a>"
                "<a onclick='fetch(`/reboot`) && addLog(`[LOCAL] Requesting reboot...`, `alert alert-warning`);' type='button' class='mb-3 text-white btn btn-danger'>Reboot device</a>"

                "\n<div class='card'><div class='card-header'>Logs</div><div id='logs' class='list-group list-group-flush'></div></div>"

                "\n<script>"

                "function addLog(msg, c=' ') {\n"
                "let logObj = document.getElementById('logs');\n"
                "let d = new Date();\n"
                "logObj.insertAdjacentHTML('afterend', `<li class='list-group-item'><div class='${c} m-0'>${d.getHours().toString().padStart(2, '0')}:${d.getMinutes().toString().padStart(2, '0')}:${d.getSeconds().toString().padStart(2, '0')} ${msg}</div></li>`);\n"
                "};\n"

                "function wsConnect() {\n"
                "let ws = new WebSocket('ws://'+location.hostname+':81/', ['arduino']);\n"
                "ws.onopen = function() { ws.send('Connect ' + new Date()); addLog('[LOCAL] Websocket connected!', 'alert alert-primary'); };\n"
                "ws.onerror = function(err) { addLog('[LOCAL] Socket error! (check console)', 'alert alert-danger'); console.log('[LOCAL] Socket error: ', err); };\n"
                "ws.onclose = function() { setTimeout(function() {addLog('[LOCAL] Disconnected. Reconnecting in a few seconds.', 'alert alert-primary'); wsConnect();}, 2000); };\n"
                "ws.onmessage = function(msg) { addLog(msg.data); };\n"
                "};\n"

                "wsConnect();"
                "addLog('[LOCAL] Waiting for websocket connection...', 'alert alert-primary');"

                "</script>"

                "</body>"

                "</html>";
  http.send(200, "text/html", html);
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {

  switch (type) {
    case WStype_DISCONNECTED:
      log(printf("[WEB] [%u] Disconnected!\n", num));
      break;
    case WStype_CONNECTED: {
        IPAddress ip = webSocket.remoteIP(num);
        log(printf("[WEB] [%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload));
      }
      break;
    case WStype_TEXT:
      log(printf("[%u] get Text: %s\n", num, payload));

      if (payload[0] == '#') {
      }

      break;
  }

}

void flushSerial () {
  int flushCount = 0;
  while (  mySerial.available() ) {
    char t = mySerial.read();  // flush any remaining bytes.
    flushCount++;
    // Serial.println("flushed a byte");
  }
  if (flushCount > 0) {
    log("[DEBUG] Flushed " + String(flushCount) + " bytes.");
    flushCount = 0;
  }

}

void buzz() {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(150);
  digitalWrite(BUZZER_PIN, 0);
}

void readTag() {
  if (mySerial.available()) {
    char buf[TAG_LENGTH];

    if (mySerial.peek() != 0x02) {
      log("[READ] Read failed, packet does not start with 0x02.");
      while (mySerial.peek() != 0x02) {
        mySerial.read();
      }
      return;
    }

    int len = mySerial.readBytesUntil(0x03, buf, TAG_LENGTH);

    if (len != TAG_LENGTH - 1) {
      log("[READ] Read failed, incomplete packet.");
      Serial.println(buf);
      return;
    }e

    buf[13] = 0; // null terminate it for convenience if we want to print it

    uint32_t cardId;
    uint8_t checksum;

    checksum = strtol(buf + 11, NULL, 16);

    buf[11] = 0; // get rid of the checksum

    cardId = strtol(buf + 3, NULL, 16); // This is the parsed card id

    buf[3] = 0;

    checksum ^= strtol(buf + 1, NULL, 16);

    for (uint8_t i = 0; i < 32; i += 8) {
      checksum ^= ((cardId >> i) & 0xFF);
    }

    if (!checksum)
    {
      log("[READ] Successfully read card: " + String(cardId));
      Serial.println("c:" + String(cardId));
      webSocket.sendTXT(0, "c:" + String(cardId));
      buzz();
      delay(1000);
      flushSerial();
    } else {
      log("[READ] Read failed, checksum is invalid. ");
      flushSerial();
    }
  }
}

void setup() {
  pinMode(BUZZER_PIN, OUTPUT);
  buzz();
  mySerial.begin(9600);
  Serial.begin(115200);
  Serial.println();

  WiFiMulti.addAP(WIFI_SSID, WIFI_PASS);

  log("\n\nConnecting to WiFi...");

  while (WiFiMulti.run() != WL_CONNECTED) {
    log(".");
    delay(500);
  }

  Serial.print("[WEB] WiFi connected! IP address: ");
  Serial.println(WiFi.localIP());


  ArduinoOTA.setHostname(HOST_NAME);
  ArduinoOTA.setPassword(OTA_PASS);

  ArduinoOTA.onStart([]() {
    Serial.println("[OTA] Start");
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\n[OTA] End");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("[OTA] Progress: %u%%\n", (progress / (total / 100)));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("[OTA] Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("[OTA] Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("[OTA] Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("[OTA] Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("[OTA] End Failed");
  });

  ArduinoOTA.begin();

  log("[OTA] Ready.");

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  http.on("/", httpRoot);

  http.on("/reboot", []() {
    webSocket.disconnect();
    http.send(200, "text/html", "<html><head><meta http-equiv='refresh' content='5;url=/' /></head>[DEBUG] Rebooting. Page will refresh in 5 seconds...</html>");
    log("[DEBUG] Rebooting");
    webSocket.loop();
    http.handleClient();
    ESP.reset();
  });

  http.on("/debug", []() {
    wsDebug = true;
    log("[DEBUG] Websocket debug mode turned on!");
  });

  http.begin();

  buzz();
  delay(100);
  buzz();
  Serial.println("\nSetup Done");
}

void loop() {
  ArduinoOTA.handle();
  MDNS.update();
  webSocket.loop();
  http.handleClient();

  readTag();
}
