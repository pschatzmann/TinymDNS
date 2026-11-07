#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#elif defined(ARDUINO_UNOR4_WIFI)
#include <WiFiS3.h>
#include <WiFiUdp.h>
#else
#include <WiFi.h>
#include <WiFiUdp.h>
#endif
#include <TinymDNS.h>

const char *ssid = "your-ssid";
const char *password = "your-password";

WiFiUDP udp;
TinymDNS mdns(udp);

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);

  // Disable WiFi power save so the device can receive multicast queries reliably
#if defined(ESP8266)
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
#elif defined(ESP32)
  WiFi.setSleep(false);
#endif

  // Answer queries for esp32.local
  if (mdns.begin(udp, "esp32", WiFi.localIP())) {
    Serial.print("Registered ");
    Serial.println(mdns.fqdn());
  } else {
    Serial.println("mDNS start failed");
  }

  // Resolve another device (blocking, uses the same socket)
  IPAddress ip;
  if (mdns.resolve("printer", ip)) {
    Serial.print("printer.local = ");
    Serial.println(ip);
  } else {
    Serial.print("resolve failed, result = ");
    Serial.println((int)mdns.lastResult());
  }
}

void loop() {
  mdns.loop();
}
