# ThinkPad USB-C Power Button

Open firmware that turns a **ZY12PDN USB-PD trigger board** into an external
power button for compatible Lenovo ThinkPads. It uses the same USB-C vendor
messaging path as a Lenovo dock's power button, so it can request startup
while the laptop is off and receiving standby power.

The project grew out of running a ThinkPad motherboard without its original
power-button assembly. A small USB-C accessory provides a physical button
and can also send startup pulses automatically when connected.

**Experimental:** the button has successfully started an NM-F261 board from
the ThinkPad P14s Gen 4 / T14 Gen 4 AMD family. Automatic startup has also
worked, but cold-attach timing remains sensitive. Compatibility with other
models and firmware versions is unverified.

## What it does

- Negotiates a 5 V USB-PD contract and enters Lenovo's vendor mode.
- Sends two automatic power-button pulses after the initial handshake.
- Sends another pulse whenever the accessory's physical button is pressed.
- Uses the onboard RGB LED for progress and failure indications.

The application is contained in [one C++ file](src/main.cpp). It runs on the
accessory; setup requires no laptop OS driver or BIOS/EC flashing step.

## Hardware

| Component | Requirement |
| --- | --- |
| ZY12PDN board | STM32F030F4 MCU and FUSB302B USB-PD controller |
| Programmer | Raspberry Pi Pico running CMSIS-DAP Debug Probe firmware |
| Laptop power | Separate charger providing standby power |
| Connections | USB-C cable for the accessory and three SWD wires for programming |

The demonstrated setup uses a charger on one laptop USB-C port and the
ZY12PDN on the other. The accessory requests **5 V at 100 mA** from the
laptop; it does not supply power to the laptop. Its output terminals stay
unconnected. The Pico is only needed for programming.

Check the board's chip markings before flashing: products sold under the
same name may use different hardware. See the
[wiring guide](docs/getting-started.md#hardware) for the Pico pin assignments
and programming connections.

## Quick start

Install Git, Python 3 with virtual-environment support, and OpenOCD with
CMSIS-DAP and STM32F0 support. The commands below use Linux/macOS paths.

```sh
git clone https://github.com/MartinezTorres/thinkpad_usb_power_button.git
cd thinkpad_usb_power_button
python3 -m venv .venv
.venv/bin/python -m pip install platformio==6.1.19
.venv/bin/pio run
```

PlatformIO downloads the dependencies pinned in [platformio.ini](platformio.ini).
The build produces `.pio/build/zy12pdn/firmware.elf` and `firmware.bin`.

**Back up the ZY12PDN's factory firmware before replacing it.** Follow the
[backup instructions](docs/getting-started.md#back-up-the-zy12pdn-before-replacing-its-firmware),
then flash the accessory through the Pico:

```sh
openocd -f openocd-picoprobe.cfg \
  -c "program .pio/build/zy12pdn/firmware.elf verify reset exit"
```

Look for `Verified OK`. Disconnect the programmer, connect the laptop's
charger, let standby power settle, then plug the accessory into the other
USB-C port. It completes the handshake, attempts its automatic pulses, and
enters the physical-button loop. See the
[usage guide](docs/getting-started.md#use) for the current sequence and timing.

## How it works

The laptop discovers the accessory's Lenovo vendor mode over the USB-C CC
wire. The accessory sends an **Attention** message to request an exchange;
the laptop then asks for status. The accessory's status reply carries the
button event, and a later laptop status request carries its acknowledgement.

The combined event `0x0D000000` asks the inspected EC to generate its own
press/release pulse. USB-PD packet delivery, event acknowledgement and a
successful boot are separate outcomes. The
[protocol notes](docs/protocol.md) explain the handshake, event masks,
partial acknowledgements and timing considerations.

## Current limitations

- These are ordinary power-button events. Connecting or resetting the
  accessory can also send a button event to an already-running laptop.
- Holding the physical button does not produce a corresponding long press.
- Automatic startup is experimental; the second automatic pulse successfully
  booted the development board, but reliable startup across repeated cold
  power cycles has not been established.
- The firmware deliberately blocks on some failures. It does not implement
  comprehensive detach/reconnect or hard-reset recovery; a stalled accessory
  may need unplugging and reconnecting.

The [LED reference](docs/getting-started.md#led-indications) explains the
failure patterns. A green LED alone is not confirmation that the laptop
booted.

## Documentation

- [Getting started](docs/getting-started.md): hardware, wiring, backup,
  build, flashing, restoring factory firmware and LED indications.
- [Protocol notes](docs/protocol.md): Lenovo vendor messages and the
  reverse-engineered button exchange.
- [Source](src/main.cpp): the complete accessory application.

When reporting compatibility or a problem, include the laptop model, board
revision if known, accessory chip markings, power/connection sequence and
observed LED pattern.

## Credits and license

The application is [MIT-licensed](LICENSE). Its I2C implementation and board
mapping build on Manuel Bleichenbacher's
[zy12pdn-oss](https://github.com/manuelbl/zy12pdn-oss) work. The build uses
[libopencm3](https://github.com/libopencm3/libopencm3), whose runtime library
is LGPL-3.0-or-later. See [NOTICE](NOTICE) for attribution.

This is an independent project and is not affiliated with Lenovo.
