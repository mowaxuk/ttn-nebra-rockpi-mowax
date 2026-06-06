# SPI Debugging — Rock Pi 4B+ / Armbian Trixie

This documents the full investigation into getting `/dev/spidev1.0` to appear reliably on Armbian Trixie with kernel 6.x, including a kernel upgrade that broke the working setup and how it was fixed permanently.

---

## Symptom

BasicStation container fails to start:

```
ERROR: /dev/spidev1.0 does not exist
```

```bash
ls /dev/spidev*
# ls: cannot access '/dev/spidev*': No such file or directory
```

---

## Root Cause

The base device tree (`rk3399-rock-pi-4b-plus.dtb`) has `spi@ff1d0000` (SPI bus 1) with `status = "disabled"`. Without an overlay enabling it, no `/dev/spidev*` nodes are created.

### Why the stock Armbian overlay doesn't work

The obvious fix is `overlays=rockchip-rk3399-spi-spidev` in `/boot/armbianEnv.txt`. This overlay exists at:

```
/boot/dtb-*/rockchip/overlay/rockchip-rk3399-spi-spidev.dtbo
```

But decompiling it reveals the problem:

```bash
dtc -I dtb -O dts /boot/dtb-*/rockchip/overlay/rockchip-rk3399-spi-spidev.dtbo
```

```dts
fragment@1 {
    target = <&spi1>;
    __overlay__ {
        spidev {
            compatible = "armbian,spi-dev";
            status = "disabled";    # ← broken: never enables anything
            ...
        };
    };
};
```

The overlay adds spidev child nodes with `status = "disabled"`. It also never sets the SPI controller itself to `status = "okay"`. Net effect: nothing changes. No `/dev/spidev1.0` appears.

### The naming trap

`armbianEnv.txt` uses `overlay_prefix=rockchip`. U-Boot constructs the overlay filename as `${overlay_prefix}-${overlay_file}.dtbo`. So:

- `overlays=rk3399-spi-spidev` → looks for `rockchip-rk3399-spi-spidev.dtbo` ✓ (correct name)
- `overlays=rockchip-rk3399-spi-spidev` → looks for `rockchip-rockchip-rk3399-spi-spidev.dtbo` ✗ (doubled prefix, file doesn't exist, silently ignored)

This naming trap is confirmed by reading the U-Boot bootscript:

```bash
strings /boot/boot.scr | grep overlay
# load ... ${prefix}dtb/rockchip/overlay/${overlay_prefix}-${overlay_file}.dtbo
```

---

## The Fix — Custom Overlay

Since the stock overlay is broken, we write a custom one that correctly enables SPI1:

```dts
/dts-v1/;
/plugin/;

&spi1 {
    status = "okay";
    #address-cells = <1>;
    #size-cells = <0>;

    spidev@0 {
        compatible = "armbian,spi-dev";
        status = "okay";
        reg = <0>;
        spi-max-frequency = <10000000>;
    };
};
```

Compile and place in the user overlay directory:

```bash
mkdir -p /boot/overlay-user
dtc -I dts -O dtb -o /boot/overlay-user/spi1-spidev.dtbo /tmp/spi1-spidev.dts
```

The pre-compiled `spi1-spidev.dtbo` is included in this repo.

### Why `user_overlays` instead of `overlays`

U-Boot loads user overlays from a separate path:

```
/boot/overlay-user/${overlay_file}.dtbo
```

Note: **no prefix** — the filename is used verbatim.

In `armbianEnv.txt`:

```
user_overlays=spi1-spidev
```

User overlays are loaded after system overlays and are separate from the `overlays=` line that Armbian package upgrades reset.

---

## Protecting Against apt Upgrades

`apt upgrade` triggers Armbian BSP package postinst scripts that regenerate `/boot/armbianEnv.txt`, wiping `user_overlays=`.

Install an apt post-invoke hook:

```bash
cat > /etc/apt/apt.conf.d/99-armbianenv << 'EOF'
DPkg::Post-Invoke { "grep -q '^user_overlays=spi1-spidev' /boot/armbianEnv.txt || { grep -q '^user_overlays=' /boot/armbianEnv.txt && sed -i 's/^user_overlays=.*/user_overlays=spi1-spidev/' /boot/armbianEnv.txt || echo 'user_overlays=spi1-spidev' >> /boot/armbianEnv.txt; }"; }
EOF
```

This runs after every dpkg operation. If `user_overlays=spi1-spidev` is missing or wrong, it restores it. The custom dtbo in `/boot/overlay-user/` is not touched by apt upgrades.

---

## Verification

After a power cycle (not reboot):

```bash
# SPI device should exist
ls /dev/spidev1.0

# Overlay was loaded (check dmesg)
dmesg | grep -i spi | grep -v GICv3

# BasicStation should start
systemctl is-active basicstation
docker logs --tail 5 basicstation
```

A working setup shows:

```
[S00:INFO] Concentrator started (3s95ms)
[S2E:VERB] RX 868.1MHz DR2 SF10/BW125 snr=... - updf mhdr=40 DevAddr=...
```

---

## Quick Diagnostic

```bash
# Check armbianEnv.txt
grep -E 'overlays|user_overlays' /boot/armbianEnv.txt

# Check overlay file exists
ls -la /boot/overlay-user/spi1-spidev.dtbo

# Check apt hook is in place
cat /etc/apt/apt.conf.d/99-armbianenv

# Check SPI controller status in base DTB
dtc -I dtb -O dts /boot/dtb/rockchip/rk3399-rock-pi-4b-plus.dtb 2>/dev/null \
    | awk '/spi@ff1d0000/,/^[[:space:]]*\};/' | grep status
# Should show: status = "disabled"; (overlay must enable it)
```

---

## History

This issue was triggered by an `apt upgrade` on 6 June 2026 that included a new kernel/BSP package. The upgrade:
1. Cleared `overlays=` in `armbianEnv.txt` (BSP postinst reset the file)
2. Revealed that the stock `rockchip-rk3399-spi-spidev.dtbo` overlay had never actually worked — the system had previously relied on the SPI bus being enabled in an older base DTB

The custom overlay and apt hook were written as a permanent fix.
