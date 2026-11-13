/*
 * ESP-NOW SENDER + SH1106 OLED
 * Sends a counting sequence (1, 2, 3, ...) once per second and shows
 * what is being sent on a 128x64 SH1106 OLED.
 * Uses the broadcast address, so you don't need the receiver's MAC.
 *
 * Wiring (default ESP32 I2C pins):
 *   OLED VCC -> 3V3
 *   OLED GND -> GND
 *   OLED SDA -> GPIO 21
 *   OLED SCL -> GPIO 22
 *
 * Libraries: Adafruit SH110X, Adafruit GFX Library
 */
#include <WiFi.h>
#include <esp_now.h>
#include <esp_idf_version.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C

Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Broadcast to every ESP32 in range
uint8_t receiverAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

typedef struct {
  uint32_t seq;      // sequence number
  char text[16];     // small label
} Message;           // must match the receiver's struct exactly

Message msg;
uint32_t counter = 0;
uint32_t okCount = 0;
uint32_t failCount = 0;

// Set by the send callback, read in loop()
volatile bool statusReady = false;
volatile bool lastSendOk = false;

// Send-status callback (signature differs between Arduino-ESP32 core versions)
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 3, 0)
void onSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
#else
void onSent(const uint8_t *mac, esp_now_send_status_t status) {
#endif
  lastSendOk = (status == ESP_NOW_SEND_SUCCESS);
  statusReady = true;
}

void showReady() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0);
  display.println("ESP-NOW Sender");
  display.drawLine(0, 10, 127, 10, SH110X_WHITE);
  display.setCursor(0, 24);
  display.println("Ready...");
  display.setCursor(0, 54);
  display.print(WiFi.macAddress());
  display.display();
}

void showSent(const Message &m, bool ok) {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);

  // Header
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Sending...");
  display.setCursor(104, 0);
  display.print("->");
  display.drawLine(0, 10, 127, 10, SH110X_WHITE);

  // Sequence number (big)
  display.setTextSize(1);
  display.setCursor(0, 16);
  display.print("Seq:");
  display.setTextSize(2);
  display.setCursor(30, 14);
  display.print(m.seq);

  // Text being sent
  display.setTextSize(1);
  display.setCursor(0, 34);
  display.print("Msg: ");
  display.print(m.text);

  // Delivery status
  display.setCursor(0, 44);
  display.print("Status: ");
  display.print(ok ? "OK" : "FAIL");

  // Counters
  display.setCursor(0, 54);
  display.print("OK:");
  display.print(okCount);
  display.print(" FAIL:");
  display.print(failCount);

  display.display();
}

void setup() {
  Serial.begin(115200);

  Wire.begin(21, 22);
  if (!display.begin(OLED_ADDR, true)) {
    Serial.println("SH1106 OLED not found (check wiring / address)");
  } else {
    display.clearDisplay();
    display.display();
  }

  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("ESP-NOW init failed");
    display.display();
    return;
  }
  esp_now_register_send_cb(onSent);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, receiverAddress, 6);
  peer.channel = 0;      // current channel
  peer.encrypt = false;

  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }
  Serial.println("Sender ready");
  showReady();
  delay(1000);
}

void loop() {
  msg.seq = ++counter;
  snprintf(msg.text, sizeof(msg.text), "hello #%lu", (unsigned long)msg.seq);

  statusReady = false;
  esp_err_t result = esp_now_send(receiverAddress, (uint8_t *)&msg, sizeof(msg));

  bool ok = false;
  if (result == ESP_OK) {
    // Wait briefly for the send-status callback
    unsigned long start = millis();
    while (!statusReady && millis() - start < 100) delay(1);
    ok = statusReady && lastSendOk;
  }

  if (ok) okCount++; else failCount++;

  Serial.printf("Sent #%lu -> %s\n", (unsigned long)msg.seq, ok ? "OK" : "FAIL");
  showSent(msg, ok);

  delay(1000);
}
