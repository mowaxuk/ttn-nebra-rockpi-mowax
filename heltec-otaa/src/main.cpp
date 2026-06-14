#include <Arduino.h>
#include <RadioLib.h>
#include <LoRaWAN_ESP32.h>
#include <Preferences.h>
#include <SSD1306Wire.h>
#include "secrets.h"

#define LORA_SCK   9
#define LORA_MISO  11
#define LORA_MOSI  10
#define LORA_CS    8
#define LORA_RST   12
#define LORA_IRQ   14
#define LORA_GPIO  13
#define OLED_SDA   17
#define OLED_SCL   18
#define OLED_RST   21
#define VEXT_CTRL  36

SSD1306Wire display(0x3c, OLED_SDA, OLED_SCL);

// 128x64 display: 24pt title at y=0, 16pt detail at y=26, 16pt extra at y=46
static void showStatus(const char* line1, const String& line2 = "", const String& line3 = "") {
    display.clear();
    display.setFont(ArialMT_Plain_24);
    display.drawString(0, 0, line1);
    if (line2.length()) {
        display.setFont(ArialMT_Plain_16);
        display.drawString(0, 26, line2);
    }
    if (line3.length()) {
        display.setFont(ArialMT_Plain_16);
        display.drawString(0, 46, line3);
    }
    display.display();
}

SPIClass spi(FSPI);
SX1262 radio = new Module(LORA_CS, LORA_IRQ, LORA_RST, LORA_GPIO, spi);
LoRaWANNode node(&radio, &EU868);
NodePersistence myPersist;

// Must be above TTN's current DevNonce floor. Old firmware reached 6391 on 2026-06-14.
#define DEVNONCE_FLOOR 6400

// Build a nonces buffer that passes RadioLib's setBufferNonces() validation.
// RadioLib 6.6.0: beginOTAA() computes keyCheckSum then immediately calls
// clearNonces() which resets it to 0, so the runtime keyCheckSum is always 0.
// hton/ntoh are little-endian; checkSum16 reads pairs as big-endian.
static void buildNoncesBuffer(uint8_t* buf, uint16_t devNonce) {
    memset(buf, 0, RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
    buf[0] = 0x01; buf[1] = 0x00;                           // VERSION = 0x0001
    buf[2] = RADIOLIB_LORAWAN_MODE_OTAA & 0xFF;              // MODE low
    buf[3] = (RADIOLIB_LORAWAN_MODE_OTAA >> 8) & 0xFF;      // MODE high
    buf[4] = RADIOLIB_LORAWAN_CLASS_A;                       // CLASS
    buf[5] = (uint8_t)BandEU868;                             // PLAN
    // buf[6..7] = CHECKSUM = 0x0000 (keyCheckSum is always 0 in this version)
    buf[8] = devNonce & 0xFF;                                // DEV_NONCE low
    buf[9] = (devNonce >> 8) & 0xFF;                         // DEV_NONCE high
    // [10-13] zero: JOIN_NONCE=0, ACTIVE=false
    // SIGNATURE = checkSum16(buf[0..13]) stored little-endian
    uint16_t sig = 0;
    for (int i = 0; i < RADIOLIB_LORAWAN_NONCES_BUF_SIZE - 2; i += 2)
        sig ^= ((uint16_t)buf[i] << 8) | buf[i + 1];
    buf[14] = sig & 0xFF;
    buf[15] = (sig >> 8) & 0xFF;
}

// Read DevNonce from RadioLib's buffer (little-endian uint16 at NONCES_DEV_NONCE offset).
static uint16_t readDevNonce() {
    uint8_t* nb = node.getBufferNonces();
    return (uint16_t)nb[RADIOLIB_LORAWAN_NONCES_DEV_NONCE]
         | ((uint16_t)nb[RADIOLIB_LORAWAN_NONCES_DEV_NONCE + 1] << 8);
}

// Persist DevNonce to our own NVS key — safe to call after failed joins
// because we only save the number, not the incomplete nonces buffer.
static void saveDevNonce() {
    Preferences p;
    p.begin("mylorawan", false);
    p.putUShort("dn", readDevNonce());
    p.end();
}

// If loadSession() was discarded (DevNonce still 0), restore from our NVS key
// or fall back to DEVNONCE_FLOOR. Injects a synthetically valid nonces buffer
// so setBufferNonces() accepts it and DevNonce is correctly set.
static void restoreOrBootstrapDevNonce() {
    if (readDevNonce() > 0) {
        Serial.printf("[nonce] loadSession OK, DevNonce=%u\n", readDevNonce());
        return;
    }
    Preferences p;
    p.begin("mylorawan", true);
    uint16_t saved = p.getUShort("dn", 0);
    p.end();
    if (saved < DEVNONCE_FLOOR) saved = DEVNONCE_FLOOR;
    Serial.printf("[nonce] loadSession discarded — injecting DevNonce=%u\n", saved);
    uint8_t buf[RADIOLIB_LORAWAN_NONCES_BUF_SIZE];
    buildNoncesBuffer(buf, saved);
    int16_t rc = node.setBufferNonces(buf);
    Serial.printf("[nonce] setBufferNonces=%d, DevNonce now=%u\n", rc, readDevNonce());
}

void setup() {
    Serial.begin(115200);
    delay(1500);

    pinMode(VEXT_CTRL, OUTPUT);
    digitalWrite(VEXT_CTRL, LOW);   // enable 3.3V rail to OLED
    delay(100);

    pinMode(OLED_RST, OUTPUT);
    digitalWrite(OLED_RST, LOW);
    delay(20);
    digitalWrite(OLED_RST, HIGH);
    delay(20);

    display.init();
    display.flipScreenVertically();

    spi.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);

    int16_t state = radio.begin();
    if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("radio.begin failed: %d\n", state);
        showStatus("LoRa FAIL", "err:" + String(state));
        while (true) delay(1000);
    }

    uint8_t nwkKey[] = NWKKEY;
    uint8_t appKey[] = APPKEY;
    node.beginOTAA(JOINEUI, DEVEUI, nwkKey, appKey);
    myPersist.loadSession(&node);
    restoreOrBootstrapDevNonce();

    Serial.printf("[setup] Ready. DevNonce=%u\n", readDevNonce());
    showStatus("Ready", "DN:" + String(readDevNonce()));
}

void loop() {
    if (!node.isActivated()) {
        static uint32_t attempt = 0;
        static uint32_t backoffMs = 0;
        ++attempt;

        uint16_t dn = readDevNonce();
        showStatus("Joining", "DN:" + String(dn), "att #" + String(attempt));

        Serial.printf("[join] attempt=%u DevNonce=%u\n", attempt, dn);

        int16_t state = node.activateOTAA();
        saveDevNonce();  // always persist after each attempt — DevNonce has advanced

        if (state == RADIOLIB_LORAWAN_NEW_SESSION || state == RADIOLIB_LORAWAN_SESSION_RESTORED) {
            myPersist.saveSession(&node);  // only valid on success — buffer fully populated
            backoffMs = 0;
            Serial.printf("[join] SUCCESS state=%d DevNonce=%u\n", state, readDevNonce());
            showStatus("Joined!", "DN:" + String(readDevNonce()));
            return;
        }

        // Exponential backoff: 30s -> 60s -> 120s -> 240s -> 300s cap
        backoffMs = (backoffMs == 0) ? 30000 : min(backoffMs * 2, (uint32_t)300000);
        Serial.printf("[join] fail state=%d backoff=%lus\n", state, backoffMs / 1000);
        showStatus("Waiting", "DN:" + String(readDevNonce()), String(backoffMs / 1000) + "s");
        delay(backoffMs);
        return;
    }

    // Joined — send uplink every 60s
    static uint32_t lastTx = 0;
    if (millis() - lastTx >= 60000UL || lastTx == 0) {
        lastTx = millis();
        String msg = "hello from Heltec";
        int16_t state = node.sendReceive((uint8_t*)msg.c_str(), msg.length(), 1);
        myPersist.saveSession(&node);
        saveDevNonce();
        Serial.printf("[tx] state=%d DevNonce=%u\n", state, readDevNonce());
        showStatus("TX sent", "DN:" + String(readDevNonce()));
    }
}
