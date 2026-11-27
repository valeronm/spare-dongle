# Bluetooth LE

This page compares SpareDongle with the PC's own Bluetooth, then describes the link as the dongle
uses it. The Air75 V2, the MX Keys and the MX Master 3S were tested.

The dongle keeps up to four devices connected at once, keyboards and mice in any mix and from
different makers, and passes on every key press, click and movement from each as it comes. Keys and
buttons stay held for one keyboard and one mouse, the one of each kind used last: using another of
the kind releases them.

- [Features](#features)
- [Reliability](#reliability)
- [Security](#security)
- [On air](#on-air)

In the comparison tables, 🟢 marks a row where SpareDongle does better, ⚪ one where both do the same,
🔴 one where the PC's Bluetooth does better, and no mark one where the other side was not checked.

## Features

| | PC's Bluetooth | SpareDongle |  |
|---|---|---|---|
| **Devices** | | | |
| Keyboards | yes | yes | ⚪ |
| Mice | yes | yes | ⚪ |
| Devices paired | as many as the PC pairs | up to eight | 🔴 |
| Devices connected at once | as many as the PC pairs | up to four | 🔴 |
| Mouse wheel | high resolution with Logitech Options or Linux's HID++ driver[^hidpp-driver] | high resolution on Logitech mice | ⚪ |
| **Keys** | | | |
| Keyboard keys (6KRO) | yes | yes | ⚪ |
| More than six held keys | yes | yes | ⚪ |
| Media keys | yes | yes | ⚪ |
| Power and sleep keys | yes | yes | ⚪ |
| Vendor-specific keys | yes | no, besides the brightness keys it diverts | 🔴 |
| **Back to the keyboard** | | | |
| Lock LEDs | yes | yes, where the device has them | ⚪ |
| Wakes a suspended PC | on any key or mouse movement | the same | ⚪ |
| Tells the keyboard the PC suspended | on Linux | yes | ⚪ |
| **Pairing** | | | |
| Pairing opens | in the PC's settings | on request from the tool | ⚪ |
| Forgets a device | in the PC's settings | yes, with the tool | ⚪ |
| **Use** | | | |
| Boot menus | no | yes | 🟢 |
| Moves to another PC with the dongle | no: a pairing per PC | yes | 🟢 |
| **Shown by the tool** | | | |
| Name | in the PC's settings | yes | ⚪ |
| Battery | in the PC's settings | yes, with charging on Logitech devices ([Reports](#reports)) | ⚪ |
| Signal | in the PC's settings | yes | ⚪ |
| Signal history | no | per minute | 🟢 |
| Noise history | no | per minute, across the data channels | 🟢 |
| Noise on the keyboard's channels | no | no: the devices hop across every channel | ⚪ |
| Noise map | no | no | ⚪ |
| Latency | no | yes | 🟢 |
| Events | no | connects, disconnects, pairings, refused pairings and switches of the keyboard and mouse used last | 🟢 |
| **[Logitech HID++](unifying.md#hid)** | | | |
| HID++ used by the dongle | no: left to software on the PC | brightness keys, battery and wheel resolution ([Reports](#reports)) | 🟢 |
| HID++ passed to Logitech Options and Solaar | yes | no | 🔴 |
| **Bluetooth only** | | | |
| PCs without Bluetooth | no | yes | 🟢 |
| Logi Bolt mice pair with a passkey | no | yes, entered as clicks ([Logi Bolt](bolt.md#security)) | 🟢 |

A report map the dongle cannot parse refuses the device, and so does a keyboard's map with no
keyboard in it or a mouse's map with no relative pointer, such as a tablet's.

The dongle switches a Logitech mouse's vertical wheel to high resolution when the PC turns on the
dongle's high-resolution wheel, and the thumb wheel stays in notches.

When the PC sleeps the dongle drops every link, and the Air75 V2 turns its lights off. A device
that reconnects while the PC sleeps, as the Air75 V2 does within seconds, wakes it only with a key
press or mouse movement. Whether input wakes the PC depends on its Bluetooth settings for its own
Bluetooth and on its USB settings for the dongle. On the PC, Logitech Options, Solaar or Linux's
HID++ driver[^hidpp-driver] speaks HID++.

## Reliability

The dongle connects to stored devices as it hears them, up to four at a time, and a stored device
heard with four connected waits until one goes. A device switched on usually reconnects within a
second or two, but a connect the device never joins can keep it away for up to a couple of minutes,
which the Mi Silent Mouse does often. After the dongle restarts, the stored devices, up to the four
most recently connected, show as not connected until they return, and the MX Keys can come back only
at its next key press, which reaches the PC a second or two late. Pairing a new device with four
connected drops the device used longest ago, never the keyboard or mouse used last.

## Security

A Bluetooth link is as secure as the way its device paired, and the device decides that way. The
dongle accepts all four, as the PC's Bluetooth does, so the table compares the ways rather than the
two hosts; its marks are the colors the [tool](tool.md#bluetooth-mode) shows for each way. Every way
encrypts the link, and whoever holds the link's key can read its keys and type keys of their own as
the device.

| Pairing | Who can read and type as the device | Tool shows |
|---|---|---|
| LE Secure Connections, passkey | no one | 🟢 secure, passkey |
| LE Secure Connections, no passkey | one who stood in the middle of the pairing | 🟡 secure, no passkey |
| Legacy, passkey | one who recorded the pairing, by trying every passkey[^crackle] | 🔴 legacy, passkey |
| Legacy, no passkey | one who recorded the pairing[^crackle] | 🔴 legacy, no passkey |

Each attack needs someone in radio range during the pairing itself, and a recorded legacy pairing
also opens the traffic recorded before the key was found. Later, an attacker can only make the
link drop by jamming the radio, whatever the way, and the device reconnects once the jamming stops,
at the latest when it is next used.

Outside pairing, only a stored device connects. Pairing opens only from the tool and pairs only the
device picked from the list of those heard, so a device nearby cannot slip in; a device that spoofs
the picked one's name and address can. The dongle offers every picked device a passkey, which a
device that asks for one has to type, and offers each passkey to one attempt only, since a failed
attempt can reveal it. A keyboard that cannot take a passkey pairs without one. Each pairing shows
in the events and in `spare-dongle devices`.

The MX Master 3S, a Logi Bolt mouse, pairs with LE Secure Connections and a passkey of ten clicks,
one of only 1024 ([Logi Bolt](bolt.md#security)). The Air75 V2 pairs legacy with no passkey step,
and the MX Keys legacy with the passkey typed. Pairing again does not change a device's way.

| | PC's Bluetooth | SpareDongle |  |
|---|---|---|---|
| Firmware | the adapter maker's, updated with the PC's system | any image, see [Firmware updates](../README.md#firmware-updates) | 🔴 |

Firmware written to the dongle can read the bond keys it keeps in flash.

## On air

The dongle is the Bluetooth LE central and each device a peripheral. As a HID over GATT host it
reads each device's report map at every connection and translates a keyboard's keyboard, media and
system reports and a mouse's pointer reports into the dongle's own USB reports. It holds each device
in one of four slots, and a device returns to the slot it last held so its signal and history stay
together. Per kind, one device is active, the one last pressed or moved, and only its keys and
buttons stay held in the dongle's report. A press or movement on another of the kind goes through
and makes that one active, releasing only what the previous device of the same kind held, so
switching keyboards leaves a held mouse button alone.

Scanning, connecting, pairing and the GATT reads are in `src/ble.c`, the choice of device, the
active device per kind and the pairing policy in `src/btpolicy.c`, the report-map parse in
`src/hidmap.c`, and the forwarding and HID++ in `src/bluetooth_main.c` and `src/hidpp.c`.

### Scanning and connecting

What the code does not show:

- While a slot is free and pairing is closed, the dongle scans all the time, and while pairing is
  open it scans actively for 30 ms of every 60. A scan of 11.25 ms every 1.28 s took tens of seconds
  to hear the Air75 V2, which advertises about every 650 ms; the continuous scan cost a moving mouse
  no reports it could measure.
- The MX Keys reconnects to its bonded host with a short burst of directed adverts only, which
  Zephyr's host drops unless the central scans under its identity address. The Air75 V2 reconnects
  with undirected adverts carrying the HID service.
- Between one connect in six and one in two to the Air75 V2 or the Mi Silent Mouse fails with
  reason `0x3e`: the dongle runs the first six connection events and hears nothing in any of them,
  at a 7.5 ms and a 30 ms connection interval alike and with fresh batteries in the mouse. Sometimes
  the device advertises again at once, and at other times it stays silent for 5 to 100 seconds, and
  the Mi Silent Mouse also falls silent that long after being switched off and on.
- After the dongle connects at 7.5 ms, the MX Keys asks for 20 to 25 ms with peripheral latency 20
  and the MX Master 3S for 7.5 to 11.25 ms with latency 44. The dongle grants the shortest of each.
  The Air75 V2 and the Mi Silent Mouse ask for nothing and stay at 7.5 ms; moved to 15 ms, the Air75
  V2 asks for 7.5 ms with latency 6 within five seconds. With the controller's default 7.5 ms
  connection event, two links at 7.5 ms took turns and a mouse sent half its reports, and with
  2.5 ms three of them filled the interval and one of them ran one event in two or three, sending
  its reports in bursts. `prj.conf` caps the event at 1.25 ms, which fits four, but an event that
  short carries one report when another link's event follows it: placed so, the Mi Silent Mouse
  sent fewer reports than it made and fell about 100 ms behind. Which link loses out changes with
  every reconnect.
- A key pressed on the MX Keys while it is disconnected is sent once the dongle has subscribed,
  late by the connection setup.
- When the PC suspends, the dongle drops every link with reason `0x15`, as Linux does with its own
  Bluetooth. The Air75 V2's radio module ignores the HID Control Point's suspend, so losing its
  link is the only notice of a suspend it acts on.
- After its link is dropped the Air75 V2 advertises for over 13 minutes, through a suspend and
  whether or not anyone types on it, with gaps of up to 4 s between the adverts a continuous scan
  heard. Once reconnected it stays silent until a key is pressed. An advert cannot tell a keyboard
  in use from one looking for its host, so the dongle keeps every link and passes on the input of
  each.

### Pairing

What the code does not show:

- Zephyr refuses security level 3 for a legacy pairing with a passkey, so a security level cannot
  demand the passkey step.
- The MX Keys and the MX Master 3S take a new address at every pairing, which leaves the old bond
  stored.
- Zephyr keeps the pairing method in the bond's keys, so it reads the same after a reboot and for a
  device that is not connected.
- The MX Keys sends no keypress notifications for typed digits, so the tool shows no progress while
  a passkey is typed.

### Reports

What the code does not show:

- The MX Keys's keyboard report is 7 bytes, the modifiers and six keys with no reserved byte, and
  its consumer report carries two 16-bit usages. Its report map also declares a mouse.
- The MX Master 3S sends every button, its motion and both wheels in report 2, and HID++ both ways
  in report 17 on a vendor page.
- The MX Master 3S's vertical wheel reports notches until high resolution is switched on over HID++
  feature `0x2121`, which reports 15 steps per notch.
- The MX Master 3S's Battery Service carries only the level; its charging state comes over HID++.

[^hidpp-driver]: Linux [`hid-logitech-hidpp.c`](https://github.com/torvalds/linux/blob/master/drivers/hid/hid-logitech-hidpp.c):
    reports a HID++ device's battery level and charging state, and turns on a Logitech mouse's
    high-resolution wheel.
[^crackle]: Mike Ryan, [crackle](https://github.com/mikeryan/crackle): recovers the key of a recorded
    LE legacy pairing, by brute force when a passkey was used.
