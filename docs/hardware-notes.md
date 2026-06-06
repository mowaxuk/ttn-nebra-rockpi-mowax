# Hardware Notes — Rock Pi 4B+ / Nebra Indoor LoRa HAT

---

## Rock Pi 4B+ 40-Pin Header (LoRa-relevant pins)

```
Pin 11  GPIO4_C6  gpiochip4:22  →  NRESET (GL5712-UX via HAT)  [0 = chip running, inverted]
Pin 12  GPIO4_A3  gpiochip4:3   →  POWER_EN (HAT TCXO power)   [1 = powered]
Pin 19  SPI1_TXD  (MOSI)
Pin 21  SPI1_RXD  (MISO)
Pin 23  SPI1_CLK  (CLK)
Pin 24  SPI1_CS0  →  /dev/spidev1.0
```

---

## Nebra Indoor LoRa HAT — Module Identification

The HAT does not contain the LoRa chip directly. It has a Mini PCIe (mPCIe) slot that accepts a concentrator module. Two modules have been used in different Nebra HAT batches:

| Module | PCB colour | Status LEDs | Chip | NRESET polarity |
|---|---|---|---|---|
| GL5712-UX (MAXIIOT) | Black | None | SX1301 + SX1257 | **Inverted** — 0 = running |
| RAK2287 | Green | Green + Red | SX1302 + SX1250 | Normal — 0 = reset |

**To identify your module:** Remove the mPCIe card from the HAT slot and read the label on the PCB. Black PCB with "GL5712" or "MAXIIOT" silk-screen and no LEDs = GL5712-UX.

Physical inspection is the only reliable method. The HAT's own silk-screen gives no indication of which module is fitted.

See [gl5712-nreset-inverter.md](gl5712-nreset-inverter.md) for the full explanation of why the inverted polarity matters.

---

## SPI NOR Flash Recall

All Nebra Rock Pi Indoor Hotspot units had a modification applied: the `XT25F32BWIG` SPI NOR flash chip was removed from the Rock Pi 4B+ PCB.

| Detail | Value |
|---|---|
| Chip | XT25F32BWIG (XTX 32Mbit SPI NOR flash) |
| Package | 8-pin SOIC |
| SPI bus | SPI1 — same bus as the LoRa concentrator |
| Effect | Bare pads leave unterminated stub traces on SPI1 |

The stub traces cause signal reflections. If you source a Rock Pi from a used Nebra unit or eBay Nebra part, assume the flash chip has been removed. This is why 2 MHz SPI speed is used in concentrator configs rather than the default 8–16 MHz.

---

## SPI Device Tree Overlay

On Armbian Trixie with kernel 6.x, `/dev/spidev1.0` requires a device tree overlay to be loaded at boot. The base DTB has `spi@ff1d0000` with `status = "disabled"`.

**The stock Armbian overlay is broken.** `rockchip-rk3399-spi-spidev.dtbo` adds spidev child nodes with `status = "disabled"` and never enables the SPI controller. It does not work.

**Use the custom overlay in this repo** (`basicstation-reset.sh.gpiod` is not the overlay — see below).

### Setup

The correct overlay is compiled and placed in `/boot/overlay-user/`:

```bash
# /boot/overlay-user/spi1-spidev.dtbo  (pre-built, in this repo)
# Enables spi@ff1d0000 and registers a spidev node
```

`/boot/armbianEnv.txt` must contain:

```
user_overlays=spi1-spidev
```

**Not** `overlays=` — that line is reset by Armbian package upgrades. `user_overlays` survives upgrades.

### Protecting against apt upgrades

`apt upgrade` can reset `armbianEnv.txt`, wiping `user_overlays`. An apt hook prevents this:

```
# /etc/apt/apt.conf.d/99-armbianenv
DPkg::Post-Invoke { "grep -q '^user_overlays=spi1-spidev' /boot/armbianEnv.txt || ..."; }
```

See [spi-debugging.md](spi-debugging.md) for the full investigation.

---

## Gateway EUI Derivation

The Gateway EUI is derived from the `end0` (wired Ethernet) MAC address using the EUI-64 method:

```
MAC:  b0:02:47:df:60:b9  (example)
EUI:  b00247fffEdf60b9
      ^^^^^^      ^^^^
      first 3     last 3 bytes of MAC
            ^^^^
            fffe inserted in middle
```

Verify with:

```bash
ip link show end0 | grep ether
# Construct: first 3 octets + fffe + last 3 octets, capitalised
```

---

## SX1301 vs SX1302 Differences

| | SX1301 (GL5712-UX) | SX1302 (RAK2287) |
|---|---|---|
| BasicStation MODEL | `RAK833` | `RAK2287` or `RAK5146` |
| NRESET logic | **Inverted** — 0 = running | Normal — 0 = reset |
| POWER_EN required | Yes | Yes |
| Cold boot sensitivity | High — warm reboot often fails | Lower |
| SPI bus | `/dev/spidev1.0` | `/dev/spidev1.0` |
