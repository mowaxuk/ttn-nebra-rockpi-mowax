#include <Arduino.h>
#include <RadioLib.h>
#include <LoRaWAN_ESP32.h>
#include <SSD1306Wire.h>

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

uint64_t joinEUI = 0x0000000000000000;
uint64_t devEUI  = 0x70B3D57ED0077DAF;
uint8_t  appKey[] = { 0xD2, 0xAA, 0x7F, 0x6A, 0x39, 0xAF, 0xF3, 0x9D,
                      0xC2, 0x32, 0xF6, 0xAF, 0xE8, 0xB9, 0xD4, 0xF5 };
uint8_t  nwkKey[] = { 0xD2, 0xAA, 0x7F, 0x6A, 0x39, 0xAF, 0xF3, 0x9D,
                      0xC2, 0x32, 0xF6, 0xAF, 0xE8, 0xB9, 0xD4, 0xF5 };

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
        attempt++;
        char buf[20];
        snprintf(buf, sizeof(buf), "Try #%u", attempt);
        Serial.printf("Join attempt #%u\n", attempt);
        oledShow("Joining...", buf);

        int state = node.activateOTAA();

        if (state == RADIOLIB_LORAWAN_NEW_SESSION ||
            state == RADIOLIB_LORAWAN_SESSION_RESTORED ||
            state == RADIOLIB_ERR_NONE) {
            persist.saveSession(&node);
            const char* msg = (state == RADIOLIB_LORAWAN_SESSION_RESTORED) ? "Restored" : "New join";
            Serial.printf("*** JOINED (%s) ***\n", msg);
            oledShow("JOINED", msg);
        } else {
            Serial.printf("Join failed (%d), retrying\n", state);
            oledShow("Join FAIL", buf);
            // No long delay — burn through DevNonces at ~1 per 6s (RX window timeout)
            // until TTN's join server floor is passed. saveSession() after first success
            // captures the accepted nonce so future power cycles work.
            delay(100);
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
