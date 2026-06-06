#include <Arduino.h>
#include <RadioLib.h>
#include <LoRaWAN_ESP32.h>
#include <SSD1306Wire.h>
#include "secrets.h"

#define VEXT_PIN    36
#define NSS_PIN      8
#define DIO1_PIN    14
#define RST_PIN     12
#define BUSY_PIN    13
#define OLED_SDA    17
#define OLED_SCL    18
#define OLED_RST    21
#define OLED_ADDR 0x3C

SSD1306Wire display(OLED_ADDR, OLED_SDA, OLED_SCL);
SX1262 radio = new Module(NSS_PIN, DIO1_PIN, RST_PIN, BUSY_PIN);
LoRaWANNode node(&radio, &EU868);

uint64_t joinEUI = JOINEUI;
uint64_t devEUI  = DEVEUI;
uint8_t  appKey[] = APPKEY;
uint8_t  nwkKey[] = NWKKEY;

uint32_t uplinkCount = 0;

static void oledShow(const char* top, const char* bottom = nullptr) {
    display.clear();
    display.setFont(ArialMT_Plain_16);
    display.drawString(0, 0, top);
    if (bottom) {
        display.setFont(ArialMT_Plain_10);
        display.drawString(0, 22, bottom);
    }
    display.display();
}

void setup() {
    Serial.begin(115200);

    pinMode(VEXT_PIN, OUTPUT);
    digitalWrite(VEXT_PIN, LOW);
    delay(100);

    pinMode(OLED_RST, OUTPUT);
    digitalWrite(OLED_RST, LOW);
    delay(50);
    digitalWrite(OLED_RST, HIGH);
    delay(50);

    display.init();
    display.flipScreenVertically();
    display.setFont(ArialMT_Plain_10);
    oledShow("Mowax TTN", "Starting...");

    Serial.println("\n--- Heltec V3 OTAA ---");

    int state = radio.begin();
    Serial.print("radio.begin(): ");
    Serial.println(state);
    if (state != RADIOLIB_ERR_NONE) {
        char buf[16];
        snprintf(buf, sizeof(buf), "err %d", state);
        oledShow("Radio FAIL", buf);
        while (true) { delay(1000); }
    }

    node.beginOTAA(joinEUI, devEUI, nwkKey, appKey);

    // Prime nonces buffer from NVS (cold boot) or RTC RAM (RST press).
    // activateOTAA() is called in loop() so it runs exactly once per attempt.
    persist.loadSession(&node);

    oledShow("TTN OTAA", "Joining...");
}

void loop() {
    if (!node.isActivated()) {
        static uint16_t attempt = 0;
        static uint32_t nextAttempt = 0;

        if (millis() < nextAttempt) return;

        attempt++;
        uint32_t shift = (attempt - 1) < 4 ? (attempt - 1) : 3;
        uint32_t backoff = 30000UL * (1UL << shift);
        if (backoff > 300000UL) backoff = 300000UL;

        char buf[20]; snprintf(buf, sizeof(buf), "Try #%u", attempt);
        Serial.printf("Join attempt #%u (backoff %lus)\n", attempt, backoff/1000);
        oledShow("Joining...", buf);

        int state = node.activateOTAA();
        if (state == RADIOLIB_LORAWAN_NEW_SESSION ||
            state == RADIOLIB_LORAWAN_SESSION_RESTORED ||
            state == RADIOLIB_ERR_NONE) {
            persist.saveSession(&node);
            const char* msg = (state == RADIOLIB_LORAWAN_SESSION_RESTORED) ? "Restored" : "New join";
            Serial.printf("*** JOINED (%s) ***\n", msg);
            oledShow("JOINED", msg);
            attempt = 0;
        } else {
            Serial.printf("Join failed (%d), next in %lus\n", state, backoff/1000);
            oledShow("Join FAIL", buf);
            persist.saveSession(&node);
            nextAttempt = millis() + backoff;
        }
        return;
    }

    uint8_t payload[] = { 0x01 };
    int state = node.sendReceive(payload, sizeof(payload), 1);
    if (state == RADIOLIB_ERR_NONE || state == RADIOLIB_LORAWAN_NO_DOWNLINK) {
        uplinkCount++;
        char buf[20];
        snprintf(buf, sizeof(buf), "Uplinks: %lu", uplinkCount);
        Serial.printf("Uplink #%lu sent\n", uplinkCount);
        oledShow("JOINED", buf);
    } else {
        char buf[20];
        snprintf(buf, sizeof(buf), "TX err %d", state);
        Serial.printf("Uplink error: %d\n", state);
        oledShow("TX Error", buf);
    }

    delay(60000);
}
