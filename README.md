# Glyph Keeper

**Reclaiming Glyph control on Nothing Phone (2a).**

After the update to Android 17 (Nothing OS 5.0), the system service responsible for Glyph management started aggressively resetting the LED driver's operating mode. This breaks third-party apps, automation, and any manual control over the Glyphs.

Glyph Keeper is a root module (KernelSU / Magisk) that bypasses the Nothing system service and communicates directly with the `aw20036` LED driver via sysfs.

## Disclaimer

**Experimental module.** Use at your own risk. The author is not responsible for any damage to your device, Glyph LEDs, or any other hardware. This project is distributed under the WTFPL license. Manual commands (especially `led` and `progress`) are provided for testing and may behave unpredictably.

## Features

*   **Start / Stop:** Manual control to turn all Glyphs on or off.
*   **Brightness Control:** Smooth brightness adjustment (0–255) via WebUI or CLI.
*   **Wave Animation:** A software-based running wave effect.
*   **WebUI:** Convenient control directly from your root manager.
*   **CLI (Termux):** Direct access via the `glyphctl` binary, including experimental `led` and `progress` commands.

## Requirements

*   Nothing Phone (2a) (tested on Android 17 / Nothing OS 5.0).
*   **KernelSU** (or a fork like ReSukiSU / SukiSU) or **Magisk**.
*   *For KernelSU users:* The `meta-overlayfs` metamodule is required for files in `system/bin` to be mounted correctly.

> **Note on other devices:** Paths to the sysfs nodes may differ on Phone (1) or Phone (2). Check `/sys/devices/platform/soc/*.i2c/i2c-*/...` before installing.

## Installation

1. Download the latest ZIP from the [Releases](../../releases) page.
2. Install the ZIP via your root manager (KernelSU, ReSukiSU, or Magisk).
3. Reboot your device.

## Usage

### WebUI
Open the module in your root manager and launch the WebUI. You can control the brightness, start/stop the Glyphs, and trigger the wave animation.

### CLI (Termux)
After installation, the utility is available at `/system/bin/glyphctl`:

```bash
# Turn all Glyphs on (manual mode, brightness 255)
su -c 'glyphctl start'

# Turn all Glyphs off
su -c 'glyphctl stop'

# Set brightness (0-255)
su -c 'glyphctl brightness 128'

# Run the wave animation
su -c 'glyphctl wave'

# Experimental: control a single LED (ID 0-25)
su -c 'glyphctl led 5 255'

# Experimental: fill a percentage of the top strip (0-100)
su -c 'glyphctl progress 50'
```

How it works

The module consists of two parts:

1. glyphctl: A C utility that writes directly to the aw20036 driver sysfs nodes (/sys/devices/platform/soc/11e80000.i2c/i2c-3/3-003a/leds/aw20036_led/).
2. Daemon: A background process (started via service.sh) that holds operating_mode at 1 (manual mode), preventing the Nothing system service from resetting it. It also listens to a FIFO (/data/local/tmp/glyph_fifo) for commands from external apps.

Known Issues

· Individual LED mapping (IDs 0–25) may require further research. The progress command currently only affects the top 24 LEDs.
· Aggressive system services may still cause brief conflicts; the daemon tries to recover the mode as quickly as possible.

Credits

· The Nothing community for reverse-engineering the aw20036 driver and sharing sysfs paths.
· The KernelSU and Magisk developers for their module APIs and documentation.
· XDA Developers forum for ongoing discussions and testing.

License

This project is licensed under the WTFPL (Do What The Fuck You Want To Public License). See the LICENSE file for details.

