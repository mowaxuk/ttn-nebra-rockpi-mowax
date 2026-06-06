# Security Policy

## Supported versions

Only the `main` branch is actively maintained. Older commits are unsupported.

---

## What counts as a security issue

Please report privately if you find any of the following:

- **Credential exposure** — TC_KEY (Gateway API key) or TTN application API keys appearing in log output, example scripts, or committed files
- **Insecure credential handling** — scripts that write API keys to world-readable files or log them to journald/docker logs
- **Command injection** — any shellscript path where untrusted input could be executed
- **Unintended network exposure** — Docker port mappings or host-network configurations that expose services beyond what is documented

---

## Out of scope

- Vulnerabilities in The Things Network platform itself — report those to TTN
- Vulnerabilities in the xoseperez/basicstation Docker image — report to that project
- OS or kernel vulnerabilities in Armbian
- Problems arising from unsupported hardware or OS combinations

---

## How to report

Do **not** open a public GitHub issue for security vulnerabilities.

Use GitHub's private vulnerability reporting feature, or contact the maintainer directly. Include:

1. Description of the vulnerability
2. Steps to reproduce
3. Potential impact
4. Suggested fix if you have one

Expect a response within a few days.

---

## Notes on credentials in this repo

The `start-basicstation.sh` script and `docker create` command require a Gateway API key (`TC_KEY`). This key is **not committed** to this repository — it appears as a placeholder `<YOUR_API_KEY>` in all documented examples.

The actual key should be treated as a secret: do not commit it, do not log it, and rotate it via the TTN console if you believe it has been exposed.
