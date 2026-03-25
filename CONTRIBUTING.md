# Contributing to BACnetLight

Thanks for your interest in improving BACnetLight.

## Before You Start

- Open an issue for bug reports, feature ideas, or API changes
- Check the README and examples first to understand the intended public behavior
- Keep changes focused so they are easy to review

## Development Guidelines

- Preserve Arduino-friendly APIs and keep memory usage reasonable for ESP32 targets
- Avoid breaking example sketches unless the change clearly improves the library
- Update `README.md` when public setup, API behavior, or limitations change
- Add or update examples when a new feature benefits from a usage sketch

## Pull Requests

- Describe what changed and why
- Mention any hardware or tools used for testing
- Prefer one main change per pull request
- Include screenshots or serial logs only when they help explain behavior

## Reporting Issues

When possible, include:

- ESP32 board/core version
- Transport used: BACnet/IP, MSTP, or dual-port
- Ethernet or RS485 hardware used
- Minimal sketch that reproduces the problem
- What you expected and what happened instead
