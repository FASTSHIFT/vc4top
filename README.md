# VC4TOP

A lightweight terminal-based performance monitoring tool for the Broadcom VC4 GPU, commonly found in Raspberry Pi devices.

## Overview

VC4TOP reads hardware performance counters directly from the VC4 GPU registers to display real-time GPU utilization metrics. Unlike the original GTK-based version, this terminal version has **zero external dependencies** (no GTK, no X11) and runs purely in the console.

## How It Works

### Hardware Access

The tool works by memory-mapping the VC4 GPU's register space through `/dev/mem`. The VC4 GPU exposes 16 hardware performance counters that can be configured to monitor different aspects of GPU activity.

**Register Base Address:** `0x3fc00000` (for Raspberry Pi)

### Performance Counters

The VC4 GPU has a set of programmable performance counters located at registers `V3D_PCTR0` through `V3D_PCTR15`. Each counter can be configured to count different events by writing to the corresponding source register (`V3D_PCTRS0` - `V3D_PCTRS15`).

This tool monitors 7 key metrics (counter sources 13-19):

| Metric | Description |
|--------|-------------|
| **Idle** | QPU is idle (power-gated or no work) |
| **Vertex** | Cycles spent on vertex shading |
| **Fragment** | Cycles spent on fragment shading |
| **Valid** | Cycles with valid QPU instructions |
| **TMU Stall** | Stalled waiting for Texture Memory Unit |
| **Scoreboard Stall** | Stalled on scoreboard (memory dependency) |
| **Varying Stall** | Stalled waiting for varying interpolation |

### Power-Off Detection

When the GPU is power-gated (no active work), reading the counters returns `0xDEADBEEF`. The tool detects this and counts it as idle time.

### Sampling Method

1. Configure performance counter sources
2. Enable counters and clear values
3. Sample counter values at regular intervals (default: 100ms)
4. Accumulate counts between update intervals (default: 500ms)
5. Calculate percentages and display

## Requirements

- Raspberry Pi with VC4 GPU (Pi 1, 2, 3, Zero, etc.)
- Linux with `/dev/mem` access
- **Root privileges** (required for `/dev/mem` access)
- No other dependencies!

## Building

```bash
mkdir build && cd build
cmake ..
make
sudo make install
```

Or simply:

```bash
cmake . && make
```

## Usage

```bash
sudo ./vc4top [options]
```

### Options

| Option | Description | Default |
|--------|-------------|---------|
| `-s <ms>` | Sampling interval in milliseconds | 100 |
| `-u <ms>` | Update/display interval in milliseconds | 500 |
| `-h` | Show help message | - |

### Examples

```bash
# Default settings
sudo ./vc4top

# Faster updates (250ms)
sudo ./vc4top -u 250

# Higher precision sampling (50ms sample, 1s update)
sudo ./vc4top -s 50 -u 1000
```

### Output

```
============================================
       VC4 GPU Performance Monitor          
============================================
  Frequency: 250.0 MHz
--------------------------------------------
Idle         [####################] 100.0 %
Vertex       [....................] 0.0 %
Fragment     [....................] 0.0 %
Valid        [....................] 0.0 %
--------------------------------------------
  Stalls:
TMU          [....................] 0.0 %
Scoreboard   [....................] 0.0 %
Varying      [....................] 0.0 %
============================================
Press Ctrl+C to exit
```

## Troubleshooting

### "Open of /dev/mem failed"
You need root privileges. Run with `sudo`.

### "Mapping register file failed"
The VC4 base address may be different on your device, or the kernel may restrict `/dev/mem` access. Check your kernel config for `CONFIG_STRICT_DEVMEM`.

### All counters show 0% or strange values
- Ensure no other application is using the VC4 performance counters
- The GPU might be in a low-power state with no active rendering

## License

GNU General Public License v3.0 - see [LICENSE.txt](LICENSE.txt)

## Credits

- Original author: Jonas Pfeil
- Terminal version: Simplified for headless/embedded use

## References

- [VideoCore IV 3D Architecture Reference Guide](https://docs.broadcom.com/doc/12358545)
- Raspberry Pi firmware source code

## Fork Information

This is a **terminal-only fork** of the original [vc4top by Jonas Pfeil](https://github.com/jonasarrow/vc4top).

### Differences from Original

| Feature | Original | This Fork |
|---------|----------|-----------|
| **UI** | GTK3 graphical window | Terminal/console output |
| **Dependencies** | GTK3, Cairo, GLib, X11 | None (pure C, libc only) |
| **Language** | C++ | C |
| **Display** | Stacked timeline graphs | ASCII progress bars |
| **Use Case** | Desktop with display | Headless/embedded/SSH |
| **Binary Size** | Larger (GTK linked) | Minimal |

### Why This Fork?

The original vc4top requires GTK3 and a display server, making it unsuitable for:
- Headless Raspberry Pi servers
- SSH sessions without X forwarding
- Embedded systems without GUI
- Minimal/lightweight installations

This fork provides the same GPU monitoring functionality in a **dependency-free, terminal-friendly** format.
