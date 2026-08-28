# LS1 CDC Protocol

The RP2040 and macOS bridge communicate over one USB CDC ACM port. Frames are
ASCII so they are easy to inspect, but they are versioned and protected by a
CRC to reject truncated or stale serial data.

## Framing

```text
@<payload>*<CRC16>\r\n
```

- `CRC16` is four hexadecimal digits using CRC-16/CCITT-FALSE.
- The CRC covers only `<payload>`: initial value `0xFFFF`, polynomial `0x1021`.
- The current protocol version is `LS1`.

## Sensor frame: device to host

```text
LS1,S,<sequence>,<uptime_ms>,<lux_millilux>,<full>,<ir>,<gain>,<integration_ms>,<flags>,<errors>
```

`flags` is a bit field:

- bit 0: TSL2591 detected
- bit 1: at least one valid reading exists
- bit 2: the reading is fresh (five seconds or less)

The host must not publish `lux_millilux` unless bits 1 and 2 are set. Invalid or
stale sensor state is not converted into a fabricated zero-lux event.

## Command frame: host to device

```text
LS1,C,<nonce>,BOOTSEL
```

On a valid CRC and command, the device responds first:

```text
LS1,A,<nonce>,BOOTSEL,OK
```

It then waits 500 ms and enters the RP2040 ROM USB bootloader. The delay lets
the ACK leave the CDC endpoint before USB disconnects.
