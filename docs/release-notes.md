First public release of the ZY12PDN ThinkPad USB-C power button.

**This is an "it works for me" project.** The physical button and automatic
sequence have started my NM-F261 ThinkPad P14s Gen 4 / T14 Gen 4 AMD-family
board. Other setups are unverified, and cold-attach timing has been sensitive.
This prerelease packages the existing firmware; it does not claim new
hardware validation or change the button behavior.

The firmware negotiates Lenovo's dock mode, attempts two automatic button
pulses, then waits for physical button presses. These are ordinary power
button events, including when the laptop is already running.

### Download

Get **thinkpad-usb-power-button-v0.1.0.tar.gz** and **SHA256SUMS** from the
assets below. The bundle contains:

- `firmware.elf` and `firmware.bin`, built for STM32F030F4 + FUSB302B.
- The CMSIS-DAP OpenOCD configuration and flashing instructions.
- Build identity, firmware checksums and license notices.
- The matching project source and the libopencm3 source used for the build.

GitHub's automatically generated "Source code" downloads contain the project
source only. Use the named bundle above for prebuilt firmware.

### Flash

Make your factory backup and connect the programmer as described in the
[README](https://github.com/MartinezTorres/thinkpad_usb_power_button/blob/v0.1.0/README.md).
With both downloaded assets in the same directory:

```sh
sha256sum -c SHA256SUMS
tar -xzf thinkpad-usb-power-button-v0.1.0.tar.gz
cd thinkpad-usb-power-button-v0.1.0
sha256sum -c FIRMWARE-SHA256SUMS
openocd -f openocd-picoprobe.cfg -c "program firmware.elf verify reset exit"
```

This programs the accessory's STM32. The laptop still needs a separate
charger. See the [protocol notes](https://github.com/MartinezTorres/thinkpad_usb_power_button/blob/v0.1.0/docs/protocol.md)
for the message exchange and acknowledgement details.
