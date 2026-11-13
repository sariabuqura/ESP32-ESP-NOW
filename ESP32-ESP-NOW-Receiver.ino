/*
 * ESP-NOW RECEIVER + SH1106 OLED
 * Prints every message to Serial and shows it on a 128x64 SH1106 OLED.
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
#define NO_SIGNAL_TIMEOUT_MS 3000

Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

typedef struct {
  uint32_t seq;
  char text[16];
} Message;   // must match the sender's struct exactly

// Shared between the ESP-NOW callback and loop()
volatile bool newData = false;
Message lastMsg;
uint8_t lastMac[6];
unsigned long lastRecvTime = 0;
uint32_t totalReceived = 0;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

// Receive callback (signature differs between Arduino-ESP32 core versions)
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  const uint8_t *mac = info->src_addr;
#else
void onReceive(const uint8_t *mac, const uint8_t *data, int len) {
#endif
  if (len != sizeof(Message)) return;

  portENTER_CRITICAL(&mux);
  memcpy(&lastMsg, data, sizeof(Message));
  memcpy(lastMac, mac, 6);
  lastMsg.text[sizeof(lastMsg.text) - 1] = '\0';
  lastRecvTime = millis();
  totalReceived++;
  newData = true;
  portEXIT_CRITICAL(&mux);
}

void showWaiting() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0);
  display.println("ESP-NOW Receiver");
  display.drawLine(0, 10, 127, 10, SH110X_WHITE);
  display.setCursor(0, 24);
  display.println("Waiting for data...");
  display.setCursor(0, 54);
  display.print(WiFi.macAddress());
  display.display();
}

void showMessage(const Message &m, const uint8_t *mac, uint32_t total) {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);

  // Header
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Receiving...");
  display.setCursor(104, 0);
  display.print("<-");
  display.drawLine(0, 10, 127, 10, SH110X_WHITE);

  // Sequence number (big)
  display.setTextSize(1);
  display.setCursor(0, 16);
  display.print("Seq:");
  display.setTextSize(2);
  display.setCursor(30, 14);
  display.print(m.seq);

  // Text
  display.setTextSize(1);
  display.setCursor(0, 34);
  display.print("Msg: ");
  display.print(m.text);

  // Count + sender MAC
  display.setCursor(0, 44);
  display.print("Total: ");
  display.print(total);

  display.setCursor(0, 54);
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  display.print(macStr);

  display.display();
}

void showNoSignal() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0);
  display.println("ESP-NOW Receiver");
  display.drawLine(0, 10, 127, 10, SH110X_WHITE);
  display.setTextSize(2);
  display.setCursor(0, 24);
  display.println("NO SIGNAL");
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
  Serial.print("Receiver MAC: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("ESP-NOW init failed");
    display.display();
    return;
  }
  esp_now_register_recv_cb(onReceive);

  Serial.println("Receiver ready, waiting for data...");
  showWaiting();
}

void loop() {
  static bool signalLost = false;

  if (newData) {
    Message m;
    uint8_t mac[6];
    uint32_t total;

    portENTER_CRITICAL(&mux);
    memcpy(&m, &lastMsg, sizeof(Message));
    memcpy(mac, lastMac, 6);
    total = totalReceived;
    newData = false;
    portEXIT_CRITICAL(&mux);

    Serial.printf("From %02X:%02X:%02X:%02X:%02X:%02X -> seq=%lu text=%s\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                  (unsigned long)m.seq, m.text);

    showMessage(m, mac, total);
    signalLost = false;
  }

  // Show "NO SIGNAL" if the sender stops
  if (totalReceived > 0 && !signalLost &&
      millis() - lastRecvTime > NO_SIGNAL_TIMEOUT_MS) {
    signalLost = true;
    showNoSignal();
  }

  delay(10);
}
