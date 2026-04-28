# Tomak79H, Hotswap ver

Ergonomics Split Keyboard powered by RP2040.

* Keyboard Maintainer: [ERA](https://github.com/eerraa)
* Hardware supported: SIRIND Tomak79
* Hardware availability: [Syryan](https://srind.mysoho.com/)

Make example for this keyboard (after setting up your build environment):

    make era/sirind/tomak79h:default

Flashing example for this keyboard:

    make era/sirind/tomak79h:default:flash

## Split Port Safety

The inter-half USB-C connector is not a USB data port. On affected Tomak79H
PCBs, the D+/RX line is used as a single-wire half-duplex serial line and the
D-/TX line is left as an input by firmware. This avoids driving the GP0/TX line
against the other half when a normal USB-C to USB-C cable connects D- to D-.
Firmware cannot fully protect against plugging this inter-half port into a PC
or other USB host.

Release firmware uses a TOMAK-specific split transport at 460800 baud. Matrix
state is sent through a small fast packet every scan, while timer, layer, LED,
and RGB Matrix state are sent through a slower packet only when needed or on a
periodic sync. Runtime diagnostics are disabled in release builds; uncomment
`TOMAK_SPLIT_DIAGNOSTICS` in `config.h` only for validation builds that need the
`TOMAK_DIAG` keycode output.

The firmware also debounces the USB VBUS sense pin used for split master
detection. This avoids a fast unplug/replug on boards with slower VBUS sense
settling being misdetected as a slave half, which can otherwise disconnect USB
during host descriptor enumeration.

Slave-side RGB Matrix output is held off until the first valid split sync from
the master. This prevents the slave half from lighting before the USB-connected
master half has finished starting, and keeps the slave LEDs off while the master
is in the RP2040 bootloader.

See the [build environment setup](https://docs.qmk.fm/#/getting_started_build_tools) and the [make instructions](https://docs.qmk.fm/#/getting_started_make_guide) for more information. Brand new to QMK? Start with our [Complete Newbs Guide](https://docs.qmk.fm/#/newbs).

## Bootloader

Enter the bootloader in 3 ways:

* **Bootmagic reset**: Hold down the 'top-left(ESC, F7)' key and plug in the keyboard.
* **Physical reset**: Short the 'RESET' and 'GND' holes twice within one second, or plug in the keyboard with the 'BOOT' and 'GND' holes shorted.
* **Keycode in layout**: Press the key mapped to `QK_BOOT` if it is available.
