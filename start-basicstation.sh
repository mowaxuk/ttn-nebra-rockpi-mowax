#!/bin/bash
set -e
echo "[basicstation] Waiting for NTP sync..."
while ! timedatectl show | grep -q "NTPSynchronized=yes"; do
  sleep 2
done
echo "[basicstation] NTP synced. Waiting 30s for clock calibration..."
sleep 30
echo "[basicstation] Starting container..."
docker rm -f basicstation 2>/dev/null || true
exec docker run \
  --name basicstation --rm --privileged --network host \
  -v /usr/local/lib/basicstation-reset.sh.gpiod:/app/reset.sh.gpiod:ro \
  -e MODEL="RAK2287" \
  -e DEVICE="/dev/spidev1.0" \
  -e GPIO_CHIP="gpiochip4" \
  -e RESET_GPIO="22" \
  -e POWER_EN_GPIO="3" \
  -e SPI_SPEED="2000000" \
  -e GATEWAY_EUI="7243C4FFFEDF60B9" \
  -e SERVER="eu1.cloud.thethings.network" \
  -e TC_KEY="<YOUR_TC_KEY>" \
  xoseperez/basicstation:latest
