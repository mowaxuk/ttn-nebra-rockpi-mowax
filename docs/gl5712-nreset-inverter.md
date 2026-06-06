# GL5712-UX NRESET Inverter — The Critical Discovery

This is the single most important thing to understand about the Nebra HAT with the GL5712-UX concentrator module. Getting this wrong causes the chip to appear permanently dead — SPI returns `0x00` for everything, `lgw_start` fails, and no amount of software debugging helps.

---

## The Hardware

The GL5712-UX is a third-party SX1301-based concentrator module made by MAXIIOT. It is found in the older Nebra Indoor LoRa HAT (the one with no status LEDs on the mPCIe card).

Unlike the standard Semtech SX1301 reference design, the GL5712-UX has a **hardware inverter on the NRESET line**.

---

## What the Inverter Does

```
Standard SX1301 logic:
  NRESET HIGH = reset RELEASED = chip running
  NRESET LOW  = reset ASSERTED = chip in reset

GL5712-UX logic (INVERTED):
  NRESET HIGH = reset ASSERTED = chip stuck in reset  ← BACKWARDS
  NRESET LOW  = reset RELEASED = chip running
```

If anything holds NRESET HIGH — including a GPIO daemon, a script, or a line left floating in a pulled-up state — the chip is permanently in reset. SPI communication appears to partially work (you may see non-zero reads for some registers) but the MCU never starts, `cal_status` returns `0x00`, and `lgw_start` fails.

---

## How This Manifests

```
ERROR: /dev/spidev1.0 does not exist
# or, if SPI is present but chip is stuck in reset:
[S00:ERRO] lgw_start failed
# SPI returns 0x00 for all register reads
# cal_status = 0x00
# fw_version = 0x00
```

---

## The Fix for BasicStation

POWER_EN and NRESET must be held by the **host** before the container starts. The xoseperez/basicstation container's internal reset scripts cannot be used with the GL5712-UX — they pulse NRESET in the wrong direction.

### What to do

Set `RESET_GPIO=0` and `POWER_EN_GPIO=0` in the container so it skips all GPIO logic. Mount a no-op `reset.sh.gpiod`. Drive the lines from `start-basicstation.sh` on the host:

```bash
# POWER_EN=1 (TCXO powered), NRESET line=0 (→ inverter → chip NRESET HIGH released → chip runs)
gpioset -c gpiochip4 3=1 22=0 &
sleep 1
docker start basicstation
```

The `&` is not an oversight — libgpiod v2 requires the process to stay alive to hold the line state. If `gpioset` exits, the GPIO lines are released.

### Container environment variables

```
RESET_GPIO=0        # disables container-side reset GPIO
POWER_EN_GPIO=0     # disables container-side power GPIO
MODEL=RAK833        # selects SX1301 firmware blobs (correct for GL5712-UX)
```

### No-op reset script

```bash
# /usr/local/lib/basicstation-reset.sh.gpiod
#!/bin/sh
exit 0
```

Mount it into the container:

```
-v /usr/local/lib/basicstation-reset.sh.gpiod:/app/reset.sh.gpiod:ro
```

---

## What Happens if You Get It Wrong

### Wrong polarity: NRESET held HIGH

```bash
# WRONG — inverted logic means HIGH holds chip in reset
gpioset -c gpiochip4 3=1 22=1 &
```

Result: chip never comes out of reset. SPI is present but returns `0x00`. Looks identical to a dead module. This is what every standard reset script does, and why they all fail.

### Letting the container manage GPIO

The container's internal `reset.sh.gpiod` assumes normal (non-inverted) NRESET polarity. On the GL5712-UX, it pulses NRESET to 1 and leaves it there — chip stuck in reset.

### Floating NRESET

Depending on pull-up resistors on the HAT PCB, the chip may or may not come out of reset. Behaviour is inconsistent across power cycles.

---

## Cold Boot Requirement

Even with correct GPIO setup, rapid warm reboots can leave the GL5712-UX in an indeterminate state. The chip needs a clean power cycle to initialise reliably.

After any setup or service failure: physically unplug mains power, wait 30 seconds, plug back in.

See [README.md Power Cycle Warning](../README.md#power-cycle-warning).

---

## GPIO Reference

| Signal | Header Pin | gpiochip4 Line | Active level (GL5712-UX) |
|---|---|---|---|
| POWER_EN | Pin 12 (GPIO4_A3) | 3 | **1** = TCXO powered |
| NRESET | Pin 11 (GPIO4_C6) | 22 | **0** = chip running (inverted) |

---

## Verifying Correct Operation

```bash
# Check GPIO lines are held
ps aux | grep gpioset
# Expected: gpioset -c gpiochip4 3=1 22=0

# Check container is running
docker inspect --format='{{.State.Status}}' basicstation

# Check for successful concentrator start
docker logs basicstation | grep 'Concentrator started'
# Then check for incoming packets
docker logs basicstation | grep 'S2E:VERB' | tail -5
```

---

## Summary

| | POWER_EN (line 3) | NRESET (line 22) |
|---|---|---|
| **Owner** | `start-basicstation.sh` (host) | `start-basicstation.sh` (host) |
| **Method** | `gpioset -c gpiochip4 3=1 22=0 &` | Same command — line 22=0 |
| **Required state** | 1 (HIGH) | **0 (LOW)** — inverted to HIGH on chip |
| **If wrong** | Chip unpowered, SPI dead | Chip stuck in reset, SPI returns 0x00 |
