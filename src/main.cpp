#include <Arduino.h>
#include <WiFi.h>

#include "app_config.hpp"
#include "board_pins.hpp"
#include "camera_service.hpp"
#include "config_service.hpp"
#include "radar_service.hpp"
#include "storage_service.hpp"
#include "web_server.hpp"

namespace {

bool connectNetwork() {
  const RuntimeConfig settings = Settings.snapshot();
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.setHostname(AppConfig::kDeviceName);

  if (settings.wifiSsid[0] != '\0') {
    WiFi.mode(WIFI_STA);
    WiFi.begin(settings.wifiSsid, settings.wifiPassword);
    Serial.printf("[wifi] Connecting to %s", settings.wifiSsid);
    const uint32_t startedAt = millis();
    while (WiFi.status() != WL_CONNECTED &&
           millis() - startedAt < AppConfig::kWifiConnectTimeoutMs) {
      Serial.print('.');
      delay(250);
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("[wifi] Connected: http://%s/ (stream: http://%s:%u/stream)\n",
                    WiFi.localIP().toString().c_str(), WiFi.localIP().toString().c_str(),
                    AppConfig::kStreamPort);
      configTime(AppConfig::kGmtOffsetSeconds, AppConfig::kDaylightOffsetSeconds,
                 AppConfig::kNtpServer1, AppConfig::kNtpServer2);
      return true;
    }
    WiFi.disconnect(true);
  }

  WiFi.mode(WIFI_AP);
  const uint64_t chipId = ESP.getEfuseMac();
  char accessPointName[32];
  snprintf(accessPointName, sizeof(accessPointName), "CameraDoorbell-%06llX",
           static_cast<unsigned long long>(chipId & 0xFFFFFFULL));
  if (!WiFi.softAP(accessPointName, settings.accessPointPassword)) {
    Serial.println("[wifi] Access point startup failed");
    return false;
  }
  Serial.printf("[wifi] Access point %s: http://%s/\n", accessPointName,
                WiFi.softAPIP().toString().c_str());
  Serial.println("[wifi] Open /config to configure the home network");
  return true;
}

void configureApplicationPins() {
  const RuntimeConfig settings = Settings.snapshot();
  // GPIO12 is a boot strapping pin and must stay LOW while the board resets.
  pinMode(BoardPins::kDoorbellButton, INPUT_PULLDOWN);
  pinMode(BoardPins::kRfTransmit, OUTPUT);
  digitalWrite(BoardPins::kRfTransmit, LOW);
  constexpr uint8_t kFlashPwmChannel = 7;
  ledcSetup(kFlashPwmChannel, 5000, 8);
  ledcAttachPin(BoardPins::kFlashLed, kFlashPwmChannel);
  ledcWrite(kFlashPwmChannel, settings.flashEnabled ? settings.flashIntensity : 0);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.println();
  Serial.println("Camera Doorbell - PlatformIO baseline");

  Settings.begin();

  if (!Camera.begin()) {
    Serial.println("[fatal] Camera startup failed; rebooting in 5 seconds");
    delay(5000);
    ESP.restart();
  }

  Storage.begin();  // Web streaming remains available when the card is absent.
  configureApplicationPins();
  Radar.begin();
  connectNetwork();

  if (!startWebServers()) {
    Serial.println("[fatal] Web server startup failed; rebooting in 5 seconds");
    delay(5000);
    ESP.restart();
  }
}

void loop() {
  static uint32_t lastHealthReport = 0;
  if (millis() - lastHealthReport >= 30000) {
    lastHealthReport = millis();
    Serial.printf("[health] heap=%u psram=%u frame=%lu age=%lums wifi=%d\n",
                  ESP.getFreeHeap(), ESP.getFreePsram(),
                  static_cast<unsigned long>(Camera.latestSequence()),
                  static_cast<unsigned long>(Camera.latestFrameAgeMs()), WiFi.status());
  }
  delay(100);
}
