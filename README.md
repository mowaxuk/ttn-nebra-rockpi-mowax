# TTN BasicStation on Rock Pi 4B+ with Nebra Indoor LoRa HAT

A complete, battle-tested setup guide for running a LoRaWAN BasicStation gateway on a Rock Pi 4B+ with a Nebra Indoor LoRa HAT, plus a companion Heltec V3 TTN Mapper node. Documents every non-obvious problem encountered on the way to a working system: module misidentification, GPIO polarity inversions, libgpiod v2 syntax changes, NTP timing, and TTN DevNonce floor behaviour.

**Who this is for:** Anyone running BasicStation on a Rock Pi with a Nebra Indoor HAT — especially if the fitted module is a GL5712-UX (black PCB, no status LEDs). Most of the failure modes here are undocumented elsewhere and will cost you days without a guide like this.

* * *

## Hardware

| Component | Detail |
|-----------|--------|
| SBC | Rock Pi 4B+ |
| HAT | Nebra Indoor LoRa HAT |
| LoRa module | GL5712-UX (SX1301 + SX1257, black PCB, no status LEDs) |
| OS | Armbian Trixie, kernel 6.18.x, eMMC boot |
| SPI bus | `/dev/spidev1.0` |
| Frequency plan | Europe 863–870 MHz (SF9 for RX2) |
| Protocol | BasicStation via [xoseperez/basicstation](https://github.com/xoseperez/basicstation) Docker image |
| TTN region | `eu1.cloud.thethings.network` |

> **This is not a RAK2246 or RAK2287.** The Nebra HAT ships with different modules depending on batch. If you assume RAK2246 and follow the standard Nebra/RAK docs, the gateway will never start. See [Module Identification](#1-module-identification--gl5712-ux-not-rak2246rak2287) first.

* * *

## Quick Start

Prerequisites: Rock Pi 4B+ with Nebra HAT, Armbian Trixie installed to eMMC, Docker installed, TTN Sandbox account.

### 1. Register your gateway on TTN

TTN Console → Gateways → Add gateway. Frequency plan: Europe 863–870 MHz SF9 for RX2. Note the Gateway EUI — it is derived from the Rock Pi MAC address in EUI-64 format (`XXXXXXFFFEXXXXXX`).

### 2. Generate a Gateway API key

TTN Console → your gateway → API Keys → Add API key → grant "Link as gateway to a Gateway Server".

### 3. Install dependencies

```bash
apt-get install -y docker.io gpiod
```

### 4. Deploy the no-op reset script

```bash
cat > /usr/local/lib/basicstation-reset.sh.gpiod << 'EOF'
#!/bin/sh
# GPIO managed externally by start-basicstation.sh before container starts.
exit 0
EOF
chmod +x /usr/local/lib/basicstation-reset.sh.gpiod
```

### 5. Create the container

```bash
docker create \
  --name basicstation --restart no --privileged --network host \
  -v /usr/local/lib/basicstation-reset.sh.gpiod:/app/reset.sh.gpiod:ro \
  -e MODEL="RAK833" \
  -e DEVICE="/dev/spidev1.0" \
  -e GPIO_CHIP="gpiochip4" \
  -e RESET_GPIO="0" \
  -e POWER_EN_GPIO="0" \
  -e GATEWAY_EUI="<YOUR_GATEWAY_EUI>" \
  -e SERVER="eu1.cloud.thethings.network" \
  -e TC_KEY="<YOUR_API_KEY>" \
  xoseperez/basicstation:latest
```

### 6. Install the startup script

```bash
cat > /usr/local/bin/start-basicstation.sh << 'SCRIPT'
#!/bin/bash
set -e

echo "[basicstation] Waiting for NTP sync..."
while ! timedatectl show | grep -q "NTPSynchronized=yes"; do
    sleep 2
done
echo "[basicstation] NTP synced."

# Hold GPIO lines: POWER_EN=1 (on), NRESET=0 (inverter → chip runs)
gpioset -c gpiochip4 3=1 22=0 &
sleep 1

# Self-heal: recreate container if it was pruned
docker create \
  --name basicstation --restart no --privileged --network host \
  -v /usr/local/lib/basicstation-reset.sh.gpiod:/app/reset.sh.gpiod:ro \
  -e MODEL="RAK833" \
  -e DEVICE="/dev/spidev1.0" \
  -e GPIO_CHIP="gpiochip4" \
  -e RESET_GPIO="0" \
  -e POWER_EN_GPIO="0" \
  -e GATEWAY_EUI="<YOUR_GATEWAY_EUI>" \
  -e SERVER="eu1.cloud.thethings.network" \
  -e TC_KEY="<YOUR_API_KEY>" \
  xoseperez/basicstation:latest 2>/dev/null || true

docker start basicstation
SCRIPT
chmod +x /usr/local/bin/start-basicstation.sh
```

### 7. Install the systemd service

```bash
cat > /etc/systemd/system/basicstation.service << 'EOF'
[Unit]
Description=BasicStation LoRa Gateway
After=network-online.target time-sync.target docker.service
Wants=network-online.target time-sync.target

[Service]
Type=simple
ExecStart=/usr/local/bin/start-basicstation.sh
Restart=on-failure
RestartSec=30

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable basicstation
```

### 8. Configure NTP

```bash
mkdir -p /etc/systemd/timesyncd.conf.d
cat > /etc/systemd/timesyncd.conf.d/override.conf << 'EOF'
[Time]
NTP=0.pool.ntp.org 1.pool.ntp.org 2.pool.ntp.org 3.pool.ntp.org
FallbackNTP=time.cloudflare.com
EOF
systemctl restart systemd-timesyncd
```

### 9. First boot

**Power cycle the Rock Pi** — unplug from mains, wait 30 seconds, plug back in. Do NOT use `reboot`. See [Power Cycle Warning](#power-cycle-warning).

Wait 90 seconds, then SSH in and check:

```bash
docker logs basicstation -f
```

A working gateway shows `lgw_start succeeded` followed by regular `RXJV`/`RXJN` entries.

* * *

## Power Cycle Warning

> **NEVER use the `reboot` command on this machine.**
>
> The GL5712-UX module does not survive a software reboot on this hardware. The SX1301 ends up in an undefined state and `lgw_start` fails consistently until the hardware is physically power-cycled.
>
> **Always:** unplug from mains → wait 30+ seconds → plug back in → wait 60 seconds before SSHing.

This is a hardware behaviour, not a fixable software bug.

* * *

## The Hard Parts

### 1. Module Identification — GL5712-UX, not RAK2246/RAK2287

The Nebra Indoor HAT ships with different LoRa modules depending on manufacturing batch:

| Module | PCB colour | Status LEDs | Chip | NRESET polarity |
|--------|-----------|-------------|------|-----------------|
| RAK2246 | Green/blue | Yes (3) | SX1301 + SX1257 | Normal (active-low: 0 = reset) |
| RAK2287 | Green | Yes | SX1302 | Normal (active-low: 0 = reset) |
| **GL5712-UX** | **Black** | **None** | SX1301 + SX1257 | **Inverted (0 = running, 1 = reset)** |

**How to identify:** Remove the Nebra HAT from the Rock Pi. Look at the smaller daughter board on top of the HAT. Black PCB with no visible LEDs and no "RAK" silk-screen = GL5712-UX. Physical inspection is the only reliable method. The HAT's own silk-screen gives no indication of which module is fitted.

**Why it matters:** All Nebra documentation assumes RAK2246. The GL5712-UX has an inverted NRESET line (hardware inverter on the HAT PCB), which means every standard reset script asserts the wrong polarity and holds the chip permanently in reset.

### 2. NRESET Inverter Polarity

The GL5712-UX incorporates a hardware inverter between the Rock Pi GPIO and the module's NRESET pin:

```
Rock Pi GPIO line 22
        │
        ▼
  [Hardware inverter on HAT PCB]
        │
        ▼
  SX1301 NRESET pin
```

Consequence:

| GPIO line 22 | After inverter | Module state |
|:---:|:---:|---|
| **0** (low) | HIGH | **Chip running** ✓ |
| **1** (high) | LOW | **Chip held in reset** ✗ |

This is the exact opposite of RAK2246, RAK2287, and RAK833. Every reset script in existence — including both scripts shipped inside the basicstation Docker image — pulses NRESET to assert-reset then releases. On the GL5712-UX through this inverter, that sequence holds the chip in reset and never releases it.

**Fix:** Don't let the container touch GPIO at all. Set `RESET_GPIO=0 POWER_EN_GPIO=0` so the container skips its reset logic. Mount a no-op `reset.sh.gpiod`. Manage GPIO from `start-basicstation.sh` on the host, before `docker start`:

```bash
# POWER_EN=1 (powered), NRESET line=0 (→ inverter → chip NRESET HIGH → chip runs)
gpioset -c gpiochip4 3=1 22=0 &
sleep 1
docker start basicstation
```

The `&` is not an oversight — it keeps the `gpioset` process alive, which is required by libgpiod v2 to hold the lines (see below).

### 3. MODEL=RAK833 and Why

The xoseperez basicstation image selects firmware blobs and GPIO behaviour via the `MODEL` environment variable. We use `MODEL=RAK833` even though the module is not a RAK833:

- RAK833 selects **SX1301** firmware blobs — correct, the GL5712-UX uses SX1301
- We set `RESET_GPIO=0` and `POWER_EN_GPIO=0` to disable all container-side GPIO
- Other MODEL values select different firmware paths: RAK2287/RAK5146 use SX1302 blobs which will not work with the GL5712-UX SX1301

Effective configuration: RAK833 SX1301 blobs + all GPIO disabled in container + GPIO driven externally by `start-basicstation.sh`.

### 4. NTP Timing Fix

BasicStation requires accurate wall-clock time to schedule Class A receive windows. `systemd-timesyncd` on a fresh Armbian boot can take 30–90 seconds after the network is up before completing its first sync.

If BasicStation starts before NTP sync:
- TLS certificate validation may fail (certificate time out of range)
- Or the gateway connects but logs `XDNT TOO_EARLY` on every attempted downlink

The `basicstation.service` unit declares `After=time-sync.target` but this target fires when `timesyncd` has *started*, not when it has *synchronised*. The explicit wait loop in `start-basicstation.sh` is required:

```bash
while ! timedatectl show | grep -q "NTPSynchronized=yes"; do
    sleep 2
done
```

### 5. libgpiod v2 — Syntax Changes and the -p Flag

Armbian Trixie ships libgpiod **2.x**. Many tutorials, forum posts, and container scripts were written for libgpiod 1.x. The CLI changed:

| Operation | libgpiod v1 | libgpiod v2 |
|-----------|------------|------------|
| Set GPIO | `gpioset gpiochip4 3=1` | `gpioset -c gpiochip4 3=1` |
| Hold after set | `gpioset -p gpiochip4 3=1 &` | `gpioset -c gpiochip4 3=1 &` (keep process alive) |
| Read GPIO | `gpioget gpiochip4 3` | `gpioget -c gpiochip4 3` |
| List lines | `gpioinfo gpiochip4` | `gpioinfo -c gpiochip4` |

**The `-p` flag gotcha:** libgpiod v1 released GPIO lines immediately when `gpioset` exited unless you used `-p` (persistent/keep) or `--mode=signal`. The `-p` flag **does not exist in v2** and will error:

```
gpioset: unknown option '-p'
```

In libgpiod v2 the line stays driven for as long as the process lives. Backgrounding with `&` keeps the process alive and the lines held. If the parent script exits and kills the gpioset process, the lines are released.

**Check your version:**

```bash
gpioset --version
# v1: gpioset (libgpiod) v1.6.x
# v2: gpioset (libgpiod) v2.x.x
```

### 6. GPIO Line Mapping — Rock Pi 4B+ / GL5712-UX

The RK3399 SoC exposes multiple GPIO banks as separate gpiochip devices. Nebra HAT signals fall on gpiochip4:

| Signal | gpiochip | Line | Header pin | Active level (GL5712-UX) |
|--------|----------|------|------------|--------------------------|
| POWER_EN | gpiochip4 | 3 | pin 12 (GPIO4_A3) | 1 = powered |
| NRESET | gpiochip4 | 22 | pin 11 (GPIO4_C6) | **0 = chip running** (inverted) |
| SPI bus | — | — | SPI1 | `/dev/spidev1.0` |

List all lines to verify before changing anything:

```bash
gpiodetect
gpioinfo -c gpiochip4
```

### 7. reset.sh Timing Bug

The basicstation container ships two reset scripts:

- `reset.sh` — uses `/sys/class/gpio` (sysfs). **Not available** on Armbian Trixie with a 6.x kernel; the kernel does not export sysfs GPIO by default.
- `reset.sh.gpiod` — uses libgpiod v1 syntax (`-p` flag, no `-c`). Fails on v2. Also has wrong polarity for GL5712-UX even if the syntax were correct.

Both scripts run inside the container (which is `--privileged`, so they can reach the GPIO device), but both fail before they can do anything useful. The failure is silent in older container versions — `lgw_start` is never reached.

**Fix:** Mount a no-op over `reset.sh.gpiod`:

```bash
# /usr/local/lib/basicstation-reset.sh.gpiod
#!/bin/sh
exit 0
```

```bash
-v /usr/local/lib/basicstation-reset.sh.gpiod:/app/reset.sh.gpiod:ro
```

Set `RESET_GPIO=0 POWER_EN_GPIO=0` so the container does not attempt any GPIO. Drive the lines from `start-basicstation.sh` on the host before `docker start`.

### 8. TOO_EARLY Downlink Fix

BasicStation logs `XDNT TOO_EARLY` when it cannot schedule a downlink into the required Class A RX1 or RX2 window. Three causes:

**1. NTP not synced at startup** — most common on fresh install. Fix: the NTP wait loop in `start-basicstation.sh`. This eliminates the majority of `TOO_EARLY` occurrences.

**2. SX1257 crystal drift** — the GL5712-UX SX1257 crystal runs 3–10 ppm. This is within tolerance for SF9 RX2 (±20 ppm). Monitor with:

```bash
docker logs basicstation | grep SYN:WARN
```

Persistent `SYN:WARN` with drift values over 15 ppm indicates a hardware issue.

**3. Network Server latency** — the TTN NS sends the downlink command too close to the RX window deadline. Nothing to fix gateway-side. Track frequency with:

```bash
docker logs basicstation | grep TOO_EARLY | wc -l
```

If `TOO_EARLY` appears only sporadically after the gateway has been running stably for >24h, network latency is the cause and it is benign.

* * *

## Boot File Reference

All files required for autonomous operation after power cycle:

| File | Purpose |
|------|---------|
| `/etc/systemd/system/basicstation.service` | systemd unit; waits on network + time-sync |
| `/usr/local/bin/start-basicstation.sh` | NTP wait → GPIO hold → self-heal `docker create` → `docker start` |
| `/etc/systemd/timesyncd.conf.d/override.conf` | Public NTP pool configuration |
| `/usr/local/lib/basicstation-reset.sh.gpiod` | No-op reset script mounted into container |

* * *

## Heltec V3 TTN Mapper Node

A [Heltec WiFi LoRa 32 V3](https://heltec.org/project/wifi-lora-32-v3/) running PlatformIO firmware for TTN coverage mapping around the gateway. Sends a 1-byte uplink every 60 seconds via OTAA, OLED status display, NVS session persistence across power cycles.

### Hardware Pin Mapping

| Signal | GPIO |
|--------|------|
| NSS (SPI CS) | 8 |
| DIO1 (IRQ) | 14 |
| RST (LoRa reset) | 12 |
| BUSY | 13 |
| Vext (powers SX1262 and OLED) | 36 — drive **LOW** to power on |
| OLED SDA | 17 |
| OLED SCL | 18 |
| OLED RST | 21 |
| OLED I2C address | 0x3C, 128×64 SSD1306 |

### PlatformIO Setup

```ini
[env:heltec_wifi_lora_32_V3]
platform = espressif32
board = heltec_wifi_lora_32_V3
framework = arduino
upload_port = COM4
monitor_port = COM4
monitor_speed = 115200
lib_deps =
    jgromes/RadioLib@6.6.0
    ropg/LoRaWAN_ESP32@1.1.0
    thingpulse/ESP8266 and ESP32 OLED driver for SSD1306 displays@^4.4.0
```

Flash and monitor:

```
pio run --target upload
pio device monitor
```

Source: [`heltec-otaa/src/main.cpp`](heltec-otaa/src/main.cpp)

### NVS Session Persistence

`LoRaWAN_ESP32` (ropg) provides `persist.loadSession()` / `persist.saveSession()` across two storage tiers:

| Storage | Content | Survives |
|---------|---------|---------|
| RTC RAM (`RTC_DATA_ATTR`) | Full session (keys, counters, MAC state) | RST button, deep sleep |
| NVS (Preferences flash) | DevNonce counter only | Power cycle |

On **RST press**: full session restored from RTC RAM instantly, no RF join required. OLED shows `JOINED / Restored`.

On **power cycle**: DevNonce loaded from NVS, one RF join attempt, session saved on success. OLED shows `JOINED / New join`.

**Critical rule:** call `persist.loadSession(&node)` in `setup()`. Never call `activateOTAA()` in `setup()`. Calling both in setup burns two DevNonces per boot.

### activateOTAA() Return Codes

| Code | Value | Meaning |
|------|-------|---------|
| `RADIOLIB_LORAWAN_NEW_SESSION` | −1118 | New RF join succeeded |
| `RADIOLIB_LORAWAN_SESSION_RESTORED` | −1117 | Session restored from RTC RAM, no RF |
| `RADIOLIB_ERR_NONE` | 0 | Already active — `isActivated()` was true |
| Any other negative | — | No JoinAccept received; retry |

**Do not test for `RADIOLIB_ERR_NONE` as join success.** It means the node was already activated before `activateOTAA()` was called, which should not happen in normal flow.

### TTN DevNonce Floor Behaviour

This is the hardest part of getting LoRaWAN working reliably on TTN when there is any join history on the DevEUI.

- TTN's join server tracks a **DevNonce floor** per DevEUI **permanently** — it is not part of the session
- "Reset session and MAC state" in the TTN console does **not** reset the floor
- Deleting and re-registering the device does **not** reset the floor (join server retains history tied to DevEUI across registrations)
- TTN issues a JoinAccept for every valid join request above the floor, advancing the floor — even if the device never receives that JoinAccept
- Each missed JoinAccept advances the floor further, widening the gap between the device's saved nonce and the current floor

**Symptom:** gateway log shows `JREQ` received, TTN console shows join attempt, device logs `Join failed (-1106)` or similar on every attempt.

**Fix:** Remove `delay(100)` from the join-retry loop so the device burns through DevNonces as fast as the LoRaWAN RX windows allow (~1 per 6 seconds). Once a JoinAccept is received and `persist.saveSession()` called, future power cycles are self-healing via NVS.

The device in this repo settled at DevNonce floor ≈35 after initial session history. NVS now persists the accepted nonce across all power cycles.

### TTN Mapper Android App

[TTN Mapper](https://ttnmapper.org) collects uplinks from your TTN application and plots coverage on a public map.

Setup:

1. Install **TTN Mapper** from the Google Play Store
2. In the app: Menu → Settings → MQTT connection:
   - Server: `eu1.cloud.thethings.network`
   - Port: `1883`
   - Username: your TTN application ID (e.g. `mowax-ttn-mapper`)
   - Password: TTN API key with "Read application traffic (uplink)" permission
3. Select your device from the application list
4. Walk the coverage area — the app uploads RSSI/SNR per uplink to ttnmapper.org in real time

The current firmware sends a 1-byte dummy payload (`0x01`) on port 1 every 60 seconds. To produce meaningful mapper data, extend the payload to include GPS coordinates from an attached GPS module.

* * *

## Repository Contents

```
.
├── README.md
└── heltec-otaa/
    ├── platformio.ini
    └── src/
        └── main.cpp
```

* * *

## See Also

- [xoseperez/basicstation](https://github.com/xoseperez/basicstation) — Docker image used
- [jgromes/RadioLib](https://github.com/jgromes/RadioLib) — LoRa/LoRaWAN library for Heltec V3
- [ropg/LoRaWAN_ESP32](https://github.com/ropg/LoRaWAN_ESP32) — NVS session persistence helper
- [TTN Mapper](https://ttnmapper.org) — coverage mapping platform
- [The Things Network Sandbox](https://www.thethingsnetwork.org) — free community LoRaWAN network
