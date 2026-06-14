# SPI Agent for MADS

This repository Produces a monolithic [MADS](https://github.com/pbosetti/mads) agent that can interface with the SPI peripheral of a Raspberry Pi.

> **NOTE:** It can be used just on Raspberry Pi, since a classic Linux computer does not provide a SPI interface.
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
- **Speed**: 5 MHz (5,000,000 Hz)
- **Mode**: SPI_MODE_0 (CPOL=0, CPHA=0)
- **Word Size**: 8-bit
- **Logic**: Full-Duplex (Simultaneous Read/Write)
- **Execution**: Runs with `SCHED_FIFO` (Priority 98) for Hard Real-Time performance (requires root/sudo and preferably a `PREEMPT_RT` patched kernel).

---

## Dynamic Configuration (`mads.ini`)

To define what data is exchanged, configure the `mosi` (Transmission) and `miso` (Reception) arrays in the agent's settings. The order of the strings in these arrays dictates the exact binary memory layout.

**Example `mads.ini`**:
```
  [spi_agent]
  sub_topic = "setpoint"
  pub_topic = "feedback"
  speed = 5000000 # SPI clock speed: 5Mhz in this case
  mosi: ["x", "y", "z", "a", "c", "vx", "vy"]
  miso: ["x", "y", "z", "a", "c", "vx", "vy", "vz", "va", "vc", "ax", "ay", "az", "aa", "ac", "error"]

```

*Note: All variables mapped in these arrays are strictly treated as 32-bit `float`.*

> No `period` field is required, since the agent is triggered when an input message occurs.

---

## Binary Protocol Specification

The agent automatically calculates the size of the payload based on whichever array (`mosi` or `miso`) is larger, and pads the total size to the nearest **multiple of 32 bytes**. This ensures perfect alignment with the L1 Data Cache lines of modern microcontrollers (like the Cortex-M7), preventing memory corruption during DMA transfers. Since Raspberry Pi can only be configured as Master, `MISO` and `MOSI` are defined accordingly.

### MOSI Layout (Raspberry Pi → Microcontroller)
| Offset | Type | Field | Description |
| :--- | :--- | :--- | :--- |
| `0` | `uint8_t` | `start` | Start Byte: `0xAA` (Normal) or `0xCC` (Homing) |
| `1` | `uint32_t`| `msg_id` | Sequence ID echo |
| `5` | `float` | `var_1` | First variable defined in `mosi` |
| `...`| `float` | `var_N` | Subsequent variables |
| `IDX`| `uint8_t` | `check` | XOR Checksum of all previous bytes (0 to IDX-1) |
| `...`| `uint8_t[]`| `padding`| Zeros added to reach the 32-byte multiple boundary |

### MISO Layout (Microcontroller → Raspberry Pi)
| Offset | Type | Field | Description |
| :--- | :--- | :--- | :--- |
| `0` | `uint8_t` | `start` | Start Byte: `0xBB` |
| `1` | `uint32_t`| `msg_id` | Sequence ID (agent only publishes newer IDs) |
| `5` | `float` | `var_1` | First variable defined in `miso` |
| `...`| `float` | `var_N` | Subsequent variables |
| `IDX`| `uint8_t` | `check` | XOR Checksum of all previous bytes (0 to IDX-1) |
| `...`| `uint8_t[]`| `padding`| Unused bytes matching the 32-byte boundary |

---

The `msg_id` field acts as an essential sequence counter for Real-Time systems:
* Deduplication (Stale Data prevention): The Raspberry Pi agent loop might run asynchronously compared to the MCU. The agent ensures that it only publishes data to the MADS network if the incoming `msg_id` is strictly greater than the last one. This prevents flooding the network with duplicate, stale feedback.
* Packet Loss Detection: By monitoring skips in the sequence (e.g., jumping from ID 10 to 13), you can easily detect dropped packets or starvation issues on the SPI bus.

## MADS Interface

### Subscription (Input)
The agent listens to the subscribed topic. It expects a JSON payload containing an `spi_input` object. It also supports an optional general-purpose boolean flag. It may be usefult for example as trigger for some procedures in microcontrollers.

**Example Input:**
{
  "spi_input": {
    "flag": false,
    "x": 10.5,
    "y": 20.0,
    "z": 5.0,
    "a": 0.0,
    "c": 1.5,
    "vx": 12.0,
    "vy": 13.0
  }
}

### Publication (Output)
When a valid SPI transfer completes (verified by Start Byte `0xBB` and the Checksum) AND the `msg_id` is strictly greater than the last received ID, the agent unpacks the binary data and publishes a flat JSON object.

**Example Output:**
{
  "x": 10.49,
  "y": 19.98,
  "z": 5.0,
  "a": 0.0,
  "c": 1.5,
  "vx": 11.9,
  "vy": 13.1,
  "vz": 0.0,
  "va": 0.0,
  "vc": 0.0,
  "ax": -0.1,
  "ay": 0.1,
  "az": 0.0,
  "aa": 0.0,
  "ac": 0.0,
  "error": 0.02
}

---

## Notes on Microcontroller Firmware

While the Raspberry Pi dynamically maps memory at runtime, **the microcontroller requires a hardcoded C struct**. 

The struct on your MCU **must perfectly mirror** the order of variables defined in your MADS JSON configuration, including the exact padding to reach the 32-byte multiple. Always use `__attribute__((packed))` to prevent compiler-induced misalignments.

**Example C Struct (Matching the settings above = 96 Bytes total):**

```
#pragma pack(push, 1)
typedef struct __attribute__((packed)) {
    uint8_t  start;       // 1 byte
    uint32_t msg_id;      // 4 bytes
    
    // Variables must exactly match the JSON array order
    float    x;           // 4 bytes
    float    y;           // 4 bytes
    // ... insert all other floats ...
    float    error;       // 4 bytes
    
    uint8_t  check;       // 1 byte
    uint8_t  padding[26]; // Adjust to reach 96 bytes (32 * 3)
} SPIPacket;
#pragma pack(pop)
```

### DMA Usage
A convenient communication implementation on microcontrollers deals with DMA (Direct Memory Access): It serves as an automatic mapping from a declared memory section to an interested internal bus (i.e. SPI bus in this case, that tipically is APB1, but it depends on the microcontroller family).