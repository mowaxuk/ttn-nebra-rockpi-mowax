# Contributing to ttn-nebra-rockpi-mowax

Thanks for wanting to help. This project exists because setting up a TTN BasicStation gateway on a Rock Pi with a Nebra HAT is genuinely hard, and every documented fix makes it easier for the next person.

Contributions are welcome — whether that's a bug report, a hardware compatibility note, a fix to the startup scripts, or documentation of a problem you spent days solving.

---

## Before you open an issue or PR

This repo targets a specific hardware combination:

| Component | Supported |
|---|---|
| SBC | Radxa Rock Pi 4B+ (RK3399) |
| OS | Armbian Trixie (Debian 13, kernel 6.x) |
| HAT | Nebra Indoor LoRa HAT (Pi Supply) |
| Concentrator | GL5712-UX (SX1301) |
| Protocol | BasicStation via xoseperez/basicstation Docker image |
| Network | The Things Network (TTN Sandbox) |
| Frequency | EU868 |

If you're on a different Rock Pi model, OS, or concentrator, the scripts may not work as-is. Issues for unsupported hardware are welcome — they may lead to new branches — but please describe what you're running.

---

## Reporting a bug

Open an issue and include all of the following. Without this, hardware problems are almost impossible to reproduce remotely.

**System info**
```bash
uname -a
cat /etc/armbian-release
```

**Concentrator module**
- GL5712-UX (black PCB, "GL5712" or "MAXIIOT" printed, no LEDs)
- Other — describe it

**SPI device present?**
```bash
ls -la /dev/spidev*
cat /boot/armbianEnv.txt | grep -E 'overlays|user_overlays'
```

**Service and container state**
```bash
systemctl status basicstation
docker inspect --format='{{.State.Status}}' basicstation
docker logs --tail 50 basicstation
```

**GPIO state**
```bash
ps aux | grep gpioset
gpioinfo -c gpiochip4
```

**Symptom**
Exact error message or log line. If you're seeing `ERROR: /dev/spidev1.0 does not exist`, `lgw_start failed`, or repeated `SYN:WARN`, say so — those are documented failure modes with known causes.

---

## Contributing a fix or improvement

1. Fork the repo and create a branch from `main`
2. Make your changes
3. Test on real hardware — see the testing checklist below
4. Open a pull request

### What makes a good PR

- **Script changes** — test a full cold power cycle after your change. Run `bash -n start-basicstation.sh` (syntax check) before submitting.
- **Documentation fixes** — typos, clarifications, and additional debugging steps are always welcome. No hardware test needed for docs-only changes.
- **New hardware support** — if you've got a different concentrator or Rock Pi model working, a PR adding a new section or branch is very welcome. Include your hardware details in the PR description.
- **GPIO or device tree changes** — explain what broke and what you changed. Reference the relevant section of `docs/` if applicable.

### Testing checklist

Before submitting a PR that touches scripts or config files:

- [ ] Cold power cycle performed after change (not just reboot)
- [ ] `/dev/spidev1.0` present after boot
- [ ] `docker logs basicstation` shows `Concentrator started` followed by `RX` entries
- [ ] TTN console shows gateway as connected
- [ ] Concentrator module and Armbian version noted in PR description

If you can't test everything (e.g. you only have one hardware configuration), say so in the PR.

---

## Commit messages

Short and descriptive. No strict convention required, but aim for:

```
fix: hold POWER_EN before docker start
docs: add SPI overlay apt-upgrade protection
feat: add exponential backoff to join retry
```

One logical change per commit.

---

## Adding to the docs

The `docs/` folder contains hardware debugging notes. If you spent hours solving a problem that isn't documented yet, a new `.md` file there is one of the most valuable contributions you can make.

Format doesn't matter much — include:
- What the symptom was (exact log output)
- What you tried that didn't work
- What actually fixed it
- Your hardware and OS versions

---

## Questions

Not sure if something is a bug or a config issue?

- [The Things Network forum](https://www.thethingsnetwork.org/forum)
- [r/LoRa](https://reddit.com/r/LoRa)
- Open a GitHub discussion

---

## Licence

By contributing, you agree that your contributions will be licensed under the MIT licence, the same as this project.
