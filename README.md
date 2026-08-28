# Lunar Sensor for RP2040-Zero

A wired ambient-light sensor for a Waveshare RP2040-Zero and TSL2591. USB is a
CDC serial link, not an Ethernet adapter. A small native macOS service decodes
the versioned `LS1` protocol and serves Lunar-compatible HTTP/SSE on
`127.0.0.1:4765`.

```text
TSL2591 -> I2C -> RP2040 -> USB CDC -> lunar-sensor-bridge -> Lunar
                                             127.0.0.1:4765
```

This avoids extra macOS network interfaces, DHCP, route priority, VPN, and
sleep/wake behavior associated with USB Ethernet.

## Wiring

| RP2040-Zero | TSL2591 |
| --- | --- |
| 3V3 | VIN |
| GND | GND |
| GP6 | SDA |
| GP7 | SCL |
| not connected | 3Vo, INT |

The firmware uses I2C1 at 100 kHz. Every I2C operation has a 10 ms timeout so a
bad sensor or cable cannot permanently block USB recovery. The onboard WS2812
on GP16 is driven black at startup to avoid contaminating low-light readings.

## Build

Requirements: CMake, Ninja, an Arm GNU embedded toolchain with Newlib, and Go
1.24 or newer for the host bridge. The repository pins Pico SDK 2.3.0.

The shortest reproducible path is:

```bash
make sdk
make verify
```

`make verify` runs the host race tests and vet, then builds both deliverables.
Generated files live in `build/` and `build-host/` and are intentionally not
committed.

Firmware:

```bash
export PICO_SDK_PATH=/absolute/path/to/pico-sdk
cmake -S . -B build -G Ninja -DPICO_BOARD=waveshare_rp2040_zero
cmake --build build
```

The flash image is `build/lunar_rp2040_zero.uf2`.

Native macOS bridge (Go is only needed for building, not running):

```bash
./scripts/build-host.sh
```

The resulting `build-host/lunar-sensor-bridge` has no third-party runtime
dependencies.

Useful targets:

```bash
make firmware          # UF2, ELF, and map
make host              # native macOS bridge
make test vet          # host checks
make install-service   # install/start the per-user LaunchAgent
make uninstall-service # stop/remove it; logs are retained
```

## First flash

Hold BOOT, tap RESET, then release BOOT. Either copy the UF2 to `RPI-RP2` or use
picotool:

```bash
picotool load -v -x build/lunar_rp2040_zero.uf2
```

## Run and configure Lunar

```bash
./scripts/run-host.sh
./scripts/configure-lunar.sh
```

The bridge auto-detects the newest `/dev/cu.usbmodem*`. Pin a port when needed:

```bash
./scripts/run-host.sh --serial /dev/cu.usbmodem5303284728876A1C1
```

Then verify the same contract used by Lunar's reference sensor server:

```bash
curl http://127.0.0.1:4765/healthz
curl http://127.0.0.1:4765/sensor/ambient_light
curl -N http://127.0.0.1:4765/events
```

SSE messages use `event: state` and the `sensor-ambient_light` entity ID.

For normal use, install the bridge as a per-user macOS LaunchAgent so it starts
after login and reconnects automatically when the board is replugged:

```bash
make install-service
./scripts/configure-lunar.sh
```

Logs are written to
`~/Library/Logs/LunarRP2040Sensor/bridge.log`. The HTTP listener remains bound
to loopback only.

## Enter BOOTSEL from software

The normal path sends the custom `BOOTSEL` command over CDC:

```bash
curl -X POST http://127.0.0.1:4765/system/bootloader
```

The native bridge can send the same CDC command without starting HTTP:

```bash
./build-host/lunar-sensor-bridge --bootsel
```

Independent recovery paths are also retained:

```bash
# Pico SDK vendor-reset interface
picotool reboot --vid 51966 --pid 16416 -f -u

# Standard CDC 1200-baud touch
stty -f /dev/cu.usbmodem... 1200
```

The physical BOOT button remains the final fallback. See
[`docs/protocol.md`](docs/protocol.md) for the CDC frame and CRC format.

## CI and releases

Every push and pull request builds the firmware, runs the Go race tests and
vet, and cross-builds the macOS arm64 bridge. Pushing a `v*` tag publishes a
GitHub Release containing the UF2, a compressed arm64 bridge, and SHA-256
checksums. The tag should match [`VERSION`](VERSION), for example `v0.2.0`.
