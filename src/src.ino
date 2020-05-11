#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <WebSocketsServer.h>
#include <ESP8266mDNS.h>

#define TAG_LENGTH 14

SoftwareSerial mySerial(4, 5); // RX, TX

ESP8266WiFiMulti WiFiMulti;
WebSocketsServer webSocket = WebSocketsServer(81);

void log(String entry) {
  if (true) {
    Serial.println(entry);
    delay(10);
  }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {

  switch (type) {
    case WStype_DISCONNECTED:
      log(printf("[WEB] [%u] Disconnected!\n", num));
      break;
    case WStype_CONNECTED: {
        IPAddress ip = webSocket.remoteIP(num);
        log(printf("[WEB] [%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload));

        // send message to client
        webSocket.sendTXT(num, "Connected");
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
    log("e:[DEBUG] Flushed " + String(flushCount) + " bytes.");
    flushCount = 0;
  }

}

void readTag() {
  if (mySerial.available()) {
    char buf[TAG_LENGTH];

    if (mySerial.peek() != 0x02) {
      log("e:[READ] Read failed, packet does not start with 0x02.");
      while (mySerial.peek() != 0x02) {
        mySerial.read();
      }
      return;
    }

    mySerial.readBytes(buf, TAG_LENGTH);

    if (buf[13] != 0x03) {
      log("e:[READ] Read failed, packet does not end with 0x03.");
      return;
    }

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
      Serial.println(cardId);
      webSocket.sendTXT(0, String(cardId).c_str());
      delay(3000);
      flushSerial();
    } else {
      log("[READ] Read failed, checksum is invalid. ");
      flushSerial();
    }
  }
}

void setup() {
  mySerial.begin(9600);
  Serial.begin(115200);
  Serial.println();

  WiFiMulti.addAP("Bill Wi The Science Fi", "225261007622");

  while (WiFiMulti.run() != WL_CONNECTED) {
    log(".");
    delay(500);
  }

  // start webSocket server
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  if (MDNS.begin("cardreader")) {
    log("[WEB] MDNS responder started");
  }

  Serial.println("\nSetup Done");
}

void loop() {
  webSocket.loop();
  MDNS.update();
  readTag();
  delay(100);
}
