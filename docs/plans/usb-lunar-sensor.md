# RP2040-Zero Wired Lunar Sensor Plan

## Scope

Build a wired Lunar ambient-light sensor using a Waveshare RP2040-Zero and a
TSL2591 on I2C1/GP6/GP7. Avoid Wi-Fi and avoid creating a USB Ethernet interface
on macOS.

## Architecture

1. TSL2591 is sampled at 1 Hz with 200 ms integration and adaptive gain.
2. Valid samples feed a 15-sample moving average. Failed or stale readings are
   never replaced with fabricated lux.
3. TinyUSB exposes one CDC ACM port plus the Pico SDK vendor-reset interface.
4. Firmware emits versioned `LS1` sensor frames with CRC-16/CCITT and accepts a
   CRC-protected CDC `BOOTSEL` command.
5. A dependency-free native macOS bridge parses CDC and serves Lunar's reference
   contract only on `127.0.0.1:4765`:
   - `GET /sensor/ambient_light`
   - `GET /events`
   - `GET /healthz`
   - `POST /system/bootloader`

## Recovery design

- Primary: localhost POST -> bridge -> CDC `BOOTSEL` command -> ACK -> delayed
  ROM reset.
- Secondary: direct bridge `--bootsel` mode.
- Independent: Pico vendor reset through picotool.
- Independent: CDC 1200-baud touch.
- Physical fallback: BOOT plus RESET.

USB initialization precedes sensor initialization, and each I2C transaction is
bounded to 10 ms. A disconnected or wedged TSL2591 therefore cannot take away
the software recovery paths.

## Failure policy

- Missing or disconnected TSL2591 produces HTTP 503 and no fake SSE state.
- The last real reading expires after five seconds without a successful sample.
- A malformed, oversized, or CRC-invalid CDC frame is discarded.
- The local server binds loopback only; it is not exposed to the LAN.

## Validation gates

- Build firmware for `waveshare_rp2040_zero` with warnings treated as errors.
- Run Go tests/vet and build the native macOS bridge.
- Flash and observe a CDC device without an added macOS network interface.
- Verify TSL2591 detection, plausible changing lux, HTTP one-shot output, and
  two-second SSE state events.
- Verify CDC-command, vendor-interface, and 1200-baud BOOTSEL entry paths.
- Verify unplug/replug auto-reconnection and stale-reading suppression.

## Repository delivery

- Pico SDK 2.3.0 is bootstrapped explicitly; generated dependencies and build
  artifacts are excluded from Git.
- A root Makefile provides firmware, host, verification, and LaunchAgent
  lifecycle entry points.
- GitHub Actions rebuild both deliverables for changes and publishes versioned
  UF2 plus macOS arm64 bridge assets from `v*` tags.

## Rejected alternatives

- CDC-ECM: removes the host bridge, but adds DHCP, routes, interface ordering,
  VPN interaction, and macOS sleep/wake failure modes.
- Python bridge: quick to prototype, but adds a runtime dependency; the shipped
  bridge is a single native binary.
