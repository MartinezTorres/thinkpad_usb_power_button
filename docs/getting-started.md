# ThinkPad USB-C power button

Turn a ZY12PDN USB-PD trigger board into a ThinkPad dock-style power button.
The accessory talks to the laptop's embedded controller over the USB-C CC
wire, using Lenovo vendor messages. Its physical button sends a power-button
pulse; the current firmware also sends two pulses automatically after setup.

The application lives in [one C++ file](../src/main.cpp). There is no laptop
OS driver to install, and no laptop BIOS or EC flashing step in this project.
For the message exchange, see [protocol notes](protocol.md).

## What has worked

Development used an NM-F261 motherboard from the ThinkPad P14s Gen 4 / T14
Gen 4 AMD family. A charger powered one USB-C port and the accessory used the
other. The main laptop battery was absent; the RTC battery was connected.
The physical accessory button repeatedly started the board. An automatic
pulse also successfully started it during development.

Startup has been timing-sensitive. This is an experimental snapshot, not a
claim of reliable unattended startup on every cold attach. Other ThinkPads,
EC versions, stock-system configurations and ZY12PDN revisions need their own
verification. The publication build has not been newly flashed or retested
on hardware.

The accessory is a **5 V, 100 mA PD sink**, not a charger. The ThinkPad must
already have standby power and be willing to power its accessory port.
It cannot start a board with no available power.

## Hardware

- ZY12PDN with **STM32F030F4** (16 KiB flash, 4 KiB RAM) and **FUSB302B**.
- Raspberry Pi Pico running the official **CMSIS-DAP Debug Probe** firmware.
- Three SWD connections, a USB cable for the Pico, and a USB-C cable for the
  accessory. The laptop uses a separate charger.

Check the chip markings before flashing: the sales name alone does not
establish compatibility. The upstream [hardware analysis](https://github.com/manuelbl/zy12pdn-oss/wiki/Hardware-analysis)
has component and programming-pad illustrations. Identify pads from the
actual board rather than assuming an orientation from a photograph.

Load `debugprobe_on_pico.uf2` onto the Pico's BOOTSEL drive, following
[Raspberry Pi's Debug Probe instructions](https://www.raspberrypi.com/documentation/microcontrollers/debug-probe.html).
Use the file for your Pico model. The table below is for the original RP2040
Pico with the official default pin assignment, not an older or custom
picoprobe build.

| Pico connection | ZY12PDN connection |
| --- | --- |
| GP2, physical pin 4 | SWCLK |
| GP3, physical pin 5 | SWDIO |
| GND, e.g. physical pin 3 | GND |

These assignments are defined in the [Debug Probe Pico configuration](https://github.com/raspberrypi/debugprobe/blob/debugprobe-v2.3.1/include/board_pico_config.h).
Power the target through its own USB-C connector. Use 3.3 V SWD signals and a
common ground; do not connect the Pico's 5 V supply to a target signal or
3.3 V pad. The supplied OpenOCD configuration does not require NRST.

Keep the ZY12PDN's output terminals unconnected: VBUS is wired directly to
them, and the accessory does not use them. The original voltage-selection
function is replaced by this firmware. See the upstream hardware analysis
for that board's power wiring.

## Build

Install Python 3 with virtual-environment support, Git, and OpenOCD 0.12.0 or
a compatible version with `cmsis-dap` and `stm32f0x` support. From a terminal:

```sh
git clone https://github.com/MartinezTorres/thinkpad_usb_power_button.git
cd thinkpad_usb_power_button
python3 -m venv .venv
.venv/bin/python -m pip install platformio==6.1.19
.venv/bin/pio run
```

PlatformIO downloads the versions pinned in [platformio.ini](../platformio.ini):
ST STM32 19.0.0, GCC ARM 7.2.1, libopencm3 package 1.10000.260625, and
SCons package 4.40801.0. These commands use Linux/macOS virtual-environment
paths. The [PlatformIO installation documentation](https://docs.platformio.org/en/latest/core/installation/index.html)
describes other environments.

The outputs are `.pio/build/zy12pdn/firmware.elf` and `firmware.bin` in the
same directory. There is no prebuilt `release/` directory in this repository.
The initial publication build completed with 4,096 bytes of flash and
24 bytes of static RAM; stack use is additional. Building requires no
connected programmer or target. No automated tests or hardware operations
are part of that build.

## Back up the ZY12PDN before replacing its firmware

Connect the Pico to the programming computer and power the ZY12PDN. These
commands halt/read the accessory MCU; they do not program flash. Use a new
backup directory so an older backup is not overwritten:

```sh
mkdir backup
openocd -f openocd-picoprobe.cfg \
  -c "init; halt; flash probe 0; dump_image backup/factory-1.bin 0x08000000 0x4000; reset run; shutdown"
openocd -f openocd-picoprobe.cfg \
  -c "init; halt; flash probe 0; dump_image backup/factory-2.bin 0x08000000 0x4000; reset run; shutdown"
cmp backup/factory-1.bin backup/factory-2.bin
sha256sum backup/factory-1.bin backup/factory-2.bin
wc -c backup/factory-1.bin backup/factory-2.bin
```

Each file should contain 16,384 bytes and the two hashes should match.
Check that OpenOCD identified the expected STM32 and 16 KiB flash. If reads
are protected, stop before trying an unlock: removing STM32 read protection
can erase the contents you are trying to save. The
[OpenOCD flash documentation](https://openocd.org/doc/html/Flash-Commands.html)
describes these operations and the `stm32f1x` driver used for this F0 target.

Keep your backups somewhere safe. They are ignored by Git and are not
provided by this project. These commands back up main flash, not a complete
archive of the MCU's option bytes or other non-flash state; the programming
command below does not request changes to option bytes.

## Flash the accessory

After making the backup and building, run from the repository directory:

```sh
openocd -f openocd-picoprobe.cfg \
  -c "program .pio/build/zy12pdn/firmware.elf verify reset exit"
```

This writes the **ZY12PDN's STM32**, not the Pico or the laptop. The ELF
contains its load address. Look for `Verified OK`. The configuration uses
CMSIS-DAP at 100 kHz; despite its historical filename, it does not use the
obsolete proprietary picoprobe adapter protocol.

To restore your own original main-flash backup:

```sh
openocd -f openocd-picoprobe.cfg \
  -c "program backup/factory-1.bin verify reset exit 0x08000000"
```

## Use

Connect the laptop's normal charger, then connect the programmed accessory
to the other USB-C port. The Pico is only needed for programming. For initial
bring-up, let the laptop's standby power settle before attaching the
accessory; simultaneous charger/accessory attachment has been less reliable
during development.

The current `main()` does the following:

1. Negotiate 5 V, answer Lenovo discovery, enter vendor mode, and answer the
   first status request.
2. Service incoming messages for 8 seconds.
3. Send a neutral event group, a combined press/release, then another neutral
   event group.
4. Service messages for another 4 seconds, then send a second combined
   press/release.
5. Show green and wait for physical button presses, continuing to service
   the PD link. Each press requests one combined pulse; releasing the
   physical button rearms the next press.

`try_send()` adds its own waits and waits for exchanges with the laptop, so
these are not precise pulse times measured from plug-in. Holding the
physical button does not request a corresponding long press.

These are ordinary power-button events, not an idempotent "ensure on"
command. Attaching or resetting the accessory while the laptop is already
running can deliver another power-button event to that running system.
Green means the accessory reached its button loop, not proof of a boot.

## LED indications

The RGB LED is active low: PA5 is red, PA6 green, and PA7 blue. Startup and
event processing reuse solid colours as progress indicators. In particular,
green also appears briefly during negotiation; a solid colour alone does
not identify the stage without the preceding sequence.

| Repeating pattern | Meaning in the current source |
| --- | --- |
| Red/off, 500 ms each | FUSB302 I2C transaction received a NACK |
| Purple/off, 500 ms each | Received PD packet failed the software CRC check |
| Yellow 1 s / red 3 s | Pending event was not sent in a status reply before the deadline |
| Blue/red, 1 s each | Release acknowledgement still missing |
| Yellow/red, 1 s each | Press acknowledgement still missing |
| White/red, 1 s each | Dock-WoL acknowledgement still missing; current `main()` does not request WoL |

Failure patterns deliberately stop normal processing. Some earlier waits,
including transmit completion and initial negotiation, block indefinitely
without a failure pattern. This small firmware has no comprehensive detach,
hard-reset, timeout-recovery or reconnection state machine. A permanently
stalled accessory may need unplugging and reconnecting.
