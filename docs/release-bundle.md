# ThinkPad USB-C power button release bundle

This is an "it works for me" firmware snapshot for the ZY12PDN with an
STM32F030F4 and FUSB302B. See [the project README](source/README.md) for the
working setup, wiring, factory backup, usage and limitations.

## Flash

After backing up the original accessory firmware and connecting the
programmer, run from this directory:

```sh
sha256sum -c FIRMWARE-SHA256SUMS
openocd -f openocd-picoprobe.cfg -c "program firmware.elf verify reset exit"
```

The ELF includes its load address. The raw `firmware.bin` is an alternative
for programmers that need a binary; its flash base is `0x08000000`.

## Source and rebuilding

`source/` contains the project at the release commit, including the build
configuration. `source/vendor/framework-libopencm3/` contains the complete
framework package used to build this firmware, with its original notices.
The exact commit, compiler and application hashes are in `build-info.txt`.

To build from the project source with its pinned dependencies:

```sh
cd source
python3 -m venv .venv
.venv/bin/python -m pip install platformio==6.1.19
.venv/bin/pio run
```

To rebuild using the bundled libopencm3 source, including your own changes
to it, replace its versioned entry under `platform_packages` in
`source/platformio.ini` with:

```ini
    framework-libopencm3 @ symlink://vendor/framework-libopencm3
```

Keep the other package entries, then run `pio run` as above. The output is
`.pio/build/zy12pdn/firmware.elf`; it can be flashed with the same OpenOCD
command using that path. No signing key is required to rebuild and install
this accessory firmware.

## Licenses

The application is MIT-licensed; see `LICENSE` and `NOTICE`. The firmware
links libopencm3, whose runtime library is LGPL-3.0-or-later. Copies of that
license and GPLv3 are in `licenses/`, and the bundled dependency sources
retain their individual copyright/license notices. GCC ARM's notices,
including its runtime-library and newlib notices, are also in `licenses/`.
