# Logi Bolt

This page compares SpareDongle with a Logi Bolt receiver, then describes the link on air. Logi Bolt
is Bluetooth LE with Logitech's conventions on top, and the receiver's firmware update carries
Nordic's stock S122 SoftDevice. SpareDongle takes Bolt devices in its Bluetooth mode: `spare-dongle
pair` opens pairing and prints the clicks to make ([Bluetooth mode](tool.md#bluetooth-mode)). Only
the MX Master 3S was tested.

- [Features](#features)
- [Reliability](#reliability)
- [Security](#security)
- [On air](#on-air)

In the comparison tables, 🟢 marks a row where SpareDongle does better, ⚪ one where both do the same,
🔴 one where the Bolt receiver does better, and no mark one where the other side was not checked.

## Features

| | Bolt receiver | SpareDongle |  |
|---|---|---|---|
| **Devices** | | | |
| Keyboards | yes | as any Bluetooth keyboard, not checked with a Bolt one | |
| Mice | yes | yes | ⚪ |
| Devices paired | up to six[^white-paper] | up to eight | 🟢 |
| Devices connected at once | three[^white-paper] | up to four | 🟢 |
| Mouse wheel | in notches until Logi Options+ or Solaar turns on high resolution | high resolution on Logitech mice | 🟢 |
| **Keys** | | | |
| Keyboard keys (6KRO) | yes | yes | ⚪ |
| More than six held keys | not checked | yes | |
| Media keys | yes | yes | ⚪ |
| Power and sleep keys | not checked | yes | |
| Vendor-specific keys | with Logi Options+ | no, besides the brightness keys it diverts | 🔴 |
| **Back to the keyboard** | | | |
| Lock LEDs | not checked | yes, where the device has them | |
| Wakes a suspended PC | on any key or mouse movement; on Linux also right after the PC suspends, unless Solaar runs[^linux-wake] | on any key or mouse movement | 🟢 |
| Tells the keyboard the PC suspended | not checked | yes | |
| **Pairing** | | | |
| Pairing opens | in Logi Options+ or Solaar; devices sold with the receiver come paired[^white-paper] | on request from the tool | ⚪ |
| Forgets a device | in Logi Options+ or Solaar | yes, with the tool | ⚪ |
| **Use** | | | |
| Boot menus | yes, on a USB 2 port without a hub or dock[^bios] | yes | ⚪ |
| Moves to another PC with the dongle | yes | yes | ⚪ |
| **Shown by the tool** | | | |
| Name | in Logi Options+ and Solaar | yes | ⚪ |
| Battery | in Logi Options+ and Solaar | yes, with charging on Logitech devices | ⚪ |
| Signal | not shown by Solaar | yes | |
| Signal history | no | per minute | 🟢 |
| Noise history | no | per minute, across the data channels | 🟢 |
| Noise on the keyboard's channels | no | no: the devices hop across every channel | ⚪ |
| Noise map | no | no | ⚪ |
| Latency | no | yes | 🟢 |
| Events | no | connects, disconnects, pairings and refused pairings | 🟢 |
| **[Logitech HID++](unifying.md#hid)** | | | |
| HID++ used by the dongle | no: left to software on the PC | brightness keys, battery and wheel resolution | 🟢 |
| HID++ passed to Logitech Options and Solaar | yes | no | 🔴 |
| **Bolt only** | | | |
| Devices that are not Bolt | no[^white-paper] | yes, in the same list | 🟢 |
| Bolt keyboards | yes | not tried | |
| A mouse's passkey | ten clicks, shown by Logi Options+ or Solaar | ten clicks, shown by the tool with the next one highlighted | ⚪ |
| Connection interval | 7.5 ms[^white-paper] | 7.5 ms | ⚪ |

Logitech names three devices in use at once, but the Linux input maintainers read the receiver as
unable to tell its devices' input apart, and Linux's HID++ driver does not take it[^linux-bolt].
The mouse, its wheel and its battery behave as on the [Bluetooth page](bluetooth.md#features).

## Reliability

| | Bolt receiver | SpareDongle |  |
|---|---|---|---|
| Channel hopping | Logitech's own, for crowded rooms[^white-paper] | the Bluetooth controller's, across every data channel | |

Logitech gives latency under 8 ms on a quiet channel and a reconnect within 300 ms after the device
sleeps[^white-paper]; no independent measurement of either was found. A test of the MX Master 4 on
a Bolt receiver found its latency in the range of other Bluetooth mice[^lttlabs].

## Security

A Bolt receiver pairs every device with LE Secure Connections and a passkey, ten clicks on a mouse
and six digits on a keyboard[^white-paper]. Ten clicks give 1024 passkeys, so one standing in the
middle of the pairing succeeds once in 1024 tries rather than once in a million. While pairing is
open, SpareDongle also takes a device that is not Bolt.

No independent security research on Bolt was found. The published attacks on Logitech's receivers
are on Unifying.

| | Bolt receiver | SpareDongle |  |
|---|---|---|---|
| Passkey at pairing | always[^white-paper] | always | ⚪ |
| Firmware | Logitech's, with rollback protection[^white-paper] | any image, see [Firmware updates](../README.md#firmware-updates) | 🔴 |

## On air

A Bolt device is a Bluetooth LE peripheral, and SpareDongle pairs it as a Bolt receiver does before
taking it as any other Bluetooth device.

Writing the Bolt mode and the clicked passkey are in `src/ble.c`, the choice of a clicked passkey
and the click count in `src/btpolicy.c`, and the Zephyr fix the build applies in `patches/zephyr`.

What the code does not show:

- A Bolt device lists Logitech's service `0xFD72` in its advert. A Bolt receiver reads its
  characteristics and writes `02` to `fd720003` in the clear before pairing; a Bolt mouse then pairs
  as a keyboard-only device and takes the passkey as ten clicks, the most significant bit first,
  right for 1, closed by both buttons.
- A Bolt mouse sends a keypress notification for each click and one when both buttons close the
  entry, which give the tool its progress. Its first, that the entry started, comes straight after
  its pairing response, before the public keys; Zephyr fails such a pairing unless patched, so the
  build carries `patches/zephyr/smp-accept-early-keypress.patch`.

[^white-paper]: Logitech, [Logi Bolt white paper](https://www.logitech.com/content/dam/logitech/en/business/pdf/logi-bolt-white-paper.pdf):
    six devices paired and three connected at once, devices sold with the receiver paired at the
    factory, Security Mode 1 Level 4 with a passkey on every pairing, Bolt devices only, 7.5 ms
    connection interval, latency under 8 ms, reconnect within 300 ms, firmware with rollback
    protection.
[^linux-bolt]: Benjamin Tissoires on the Linux kernel list, [on adding the Bolt receiver to
    `hid-logitech-dj`](https://lkml.iu.edu/hypermail/linux/kernel/2404.1/10947.html): the receiver
    does not tell which paired device input comes from.
[^linux-wake]: [Bolt receiver causes Linux to immediately wake](https://www.reddit.com/r/logitech/comments/1dk98qa/bolt_receiver_causes_linux_to_immediately_wake/):
    reproduced by Logitech; Solaar or a udev rule stops it.
[^bios]: Logitech, [Using Logi Bolt devices in BIOS](https://support.logi.com/hc/articles/4662409470871).
[^lttlabs]: LTT Labs, [Logi Bolt Receiver: Not Just a Bluetooth Dongle](https://www.lttlabs.com/articles/2025/10/24/logi-bolt-receiver-not-just-a-bluetooth-dongle).
