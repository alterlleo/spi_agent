# SPI Agent for MADS

This repository Produces a monolithic [MADS](https://github.com/pbosetti/mads) agent that can interface with the SPI peripheral of a Raspberry Pi.

> **NOTE: ** It can be used just on Raspberry Pi, since a classic Linux computer does not provide a SPI interface.
> Simulator for PCs is still work in progress.


## Build

As usual for MADS plugins and agents:

```sh
cmake -Bbuild
cmake --build build -j6
```

A few notes:

- The default install prefix is the MADS folder (as for `mads -p`)
- The option `MADS_INSTALL_AGENT` (default: off) enables installation in the prefix directory of the `mads-spi` agent, so that you can call it as `mads spi`

## SPI Configuration
- **Device**: `/dev/spidev0.0`
- **Mode**: SPI_MODE_0 (CPOL=0, CPHA=0)
- **Clock Speed**: 1 MHz
- **Word Size**: 8-bit
- **Logic**: Full-Duplex (simultaneous Read/Write)

---

## Binary Protocol Specification
To prevent memory padding issues across different architectures (ARM vs. MCU), all structures are forced to **1-byte alignment** using `#pragma pack(push, 1)`.

### TX: Raspberry Pi → Microcontroller
Sent whenever a valid JSON message is received from the MADS network.
- **Total Size**: 26 Bytes
- **Structure (`Pack`)**:
  
| Offset | Type | Field | Description |
| :--- | :--- | :--- | :--- |
| 0 | `uint8_t` | `start` | Start Byte: `0xAA` |
| 1 | `float` | `x` | X-axis coordinate (4 bytes) |
| 5 | `float` | `y` | Y-axis coordinate (4 bytes) |
| 9 | `float` | `z` | Z-axis coordinate (4 bytes) |
| 13 | `float` | `pitch` | Pitch/Rotation (4 bytes) |
| 17 | `float` | `yaw` | Yaw/Rotation (4 bytes) |
| 21 | `float` | `feedrate`| Target velocity (4 bytes) |
| 25 | `uint8_t` | `check` | XOR Checksum of bytes 0-24 |

### RX: Microcontroller → Raspberry Pi
Read during every agent loop cycle to update the machine status.
- **Total Size**: 22 Bytes
- **Structure (`PackFb`)**:

| Offset | Type | Field | Description |
| :--- | :--- | :--- | :--- |
| 0 | `uint8_t` | `start` | Start Byte: `0xBB` |
| 1 | `uint32_t`| `msg_id`| Sequence ID (only newer IDs are processed) |
| 5 | `float` | `x` | Current X position (4 bytes) |
| 9 | `float` | `y` | Current Y position (4 bytes) |
| 13 | `float` | `z` | Current Z position (4 bytes) |
| 17 | `float` | `error` | Current tracking error (4 bytes) |
| 21 | `uint8_t` | `check` | XOR Checksum of bytes 0-20 |

---

## MADS Interface

### Subscription (Input)
The agent listens for JSON messages on the network. It expects a `/machine/` object containing an array of setpoints.

Example Input:
```json
{
  "machine": {
    "x": 10.5,
    "y": 20.0,
    "z": 5.0,
    "pitch": 0.0,
    "yaw": 1.5,
    "feedrate": 1200.0
  }
}
```

### Publication (Output)
When valid feedback is received from the SPI bus (verified via `Start` Byte and Checksum), the agent publishes the machine status.

Example Output:
```json
{
  "xf": 10.49,
  "yf": 19.98,
  "zf": 5.0,
  "e": 0.02
}
```