use crate::protocol::ProtocolInfo as _;
use crate::protocol::{Link, PairingWindow, Slot, SlotStatus, Status};
use crate::words::{
    KEYBOARD_PAIRING, LinkHealth, asked_text, battery, bootloader_and_softdevice, counters_line,
    hex, history_line, latency, link_health, name_parts, paired, pairing, pairing_offer,
    pairing_picked, params_text, passkey_entry, protocol_security, reset_by, shown_name, signal,
    slot_connection, state_name, status_label, unifying_kind, uptime, usb_state, version,
    waiting_device, waits_for_passkey,
};

/// `s`'s link-wide rows, then a section for each of `devices` whose slot `s` lists.
pub fn status_lines(s: &Status, devices: &[SlotStatus]) -> Vec<String> {
    let (link, offer) = match &s.link {
        Link::Nuphy(nuphy) => (
            nuphy
                .own
                .map_or(String::new(), |own| format!(" ({})", paired(own))),
            format!(", {}", pairing_offer(nuphy.forced)),
        ),
        _ => (String::new(), String::new()),
    };
    let link = if s.protocol().has_esb() && link_health(s) > LinkHealth::NotPaired {
        format!("{} on channel {}{link}", state_name(s), s.channel)
    } else {
        state_name(s).to_string()
    };
    let rows = vec![
        (
            "firmware",
            std::iter::once(version(s.firmware_version))
                .chain(bootloader_and_softdevice(s))
                .collect::<Vec<_>>()
                .join(", "),
        ),
        ("protocol", s.protocol().name().to_string()),
        ("link", link),
        (
            "pairing",
            format!("{}{offer}", window_state(&s.pairing_window)),
        ),
        ("usb", usb_state(s.usb_live, s.usb_suspended).to_string()),
        (
            "uptime",
            format!("{}, {}", uptime(s.uptime_ms), reset_by(s.reset_cause)),
        ),
        ("counters", counters_line(s)),
        ("history", history_line(s)),
    ];
    let mut lines = labeled(rows);
    for d in devices {
        if let Some(slot) = s.slot(d.slot) {
            lines.push(String::new());
            lines.extend(labeled(slot_rows(s, slot, d)));
        }
    }
    lines
}

fn labeled(rows: Vec<(&str, String)>) -> Vec<String> {
    rows.into_iter()
        .map(|(label, value)| status_label(label) + &value)
        .collect()
}

fn slot_rows(s: &Status, slot: &Slot, d: &SlotStatus) -> Vec<(&'static str, String)> {
    let (label, details) = match &s.link {
        Link::Unifying(k) if slot.holds_device() => {
            let (label, kind) = match unifying_kind(k.kind) {
                Some(kind) => (kind, String::new()),
                None => ("device", format!(", type {:#04x}", k.kind)),
            };
            let details = format!(", serial {}, WPID {}{kind}", hex(&k.serial), hex(&k.wpid));
            (label, details)
        }
        Link::Nuphy(nuphy) => (
            slot.kind.name(),
            nuphy
                .keyboard_id
                .map_or(String::new(), |id| format!(", ID {}", hex(&id))),
        ),
        _ => (slot.kind.name(), String::new()),
    };
    let device = if slot.holds_device() {
        let state = slot_connection(slot);
        let name: String = name_parts(
            shown_name(d.name.as_deref()),
            d.pairing.is_some_and(|p| p.bolt),
            (),
            (),
        )
        .into_iter()
        .map(|(text, ())| text)
        .collect();
        format!("{name}{details}, {state}")
    } else {
        "not paired".into()
    };
    let mut rows = vec![(label, device)];
    if signal_shown(s, d) {
        rows.push(("signal", signal(d)));
    }
    if let Some(b) = d.battery {
        rows.push(("battery", battery(b)));
    }
    if let Some(p) = d.pairing {
        rows.push(("security", pairing(p)));
    } else if slot.holds_device()
        && let Some(rating) = protocol_security(s.protocol())
    {
        rows.push(("security", rating.into()));
    }
    if let Some(link) = d.link {
        rows.push(("params", params_text(&link.params)));
        rows.push(("asked", asked_text(link.asked.as_ref())));
    }
    if slot.holds_device() {
        let counters: Vec<String> = s
            .protocol()
            .counters()
            .iter()
            .filter_map(|&counter| Some(format!("{} {}", counter.name(), d.get(counter)?)))
            .collect();
        rows.push(("counters", counters.join(" ")));
    }
    if s.connected(d.slot) {
        rows.push(("latency", latency(d.latency)));
    }
    rows
}

fn signal_shown(s: &Status, device: &SlotStatus) -> bool {
    s.slot(device.slot).is_some_and(Slot::holds_device)
        && (s.protocol().has_esb() || device.signal.is_some())
}

fn window_state(window: &PairingWindow) -> String {
    match window {
        PairingWindow::Closed => "closed".into(),
        PairingWindow::Pairing => KEYBOARD_PAIRING.into(),
        PairingWindow::Open => "open".into(),
        PairingWindow::Picking => "open, waiting for a device to be picked".into(),
        PairingWindow::Picked { name } => {
            format!("open, {}", pairing_picked(name.as_deref()))
        }
        PairingWindow::PasskeyAwaited(a) => format!(
            "open, {}: {}",
            waits_for_passkey(waiting_device(a)),
            passkey_entry(a)
        ),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::protocol::testing::{
        awaited, clicked, keyboard_slot, mouse_status, nuphy, status_of, unifying, unpaired,
    };
    use crate::protocol::{Pairing, Protocol};

    #[test]
    fn the_pairing_row_names_the_picked_device() {
        assert_eq!(
            window_state(&PairingWindow::Picked {
                name: Some("MX Keys".into())
            }),
            "open, pairing MX Keys"
        );
    }

    /// The lines `status` prints for a dongle whose shown slots are all `s`'s own.
    fn lines(s: &Status) -> Vec<String> {
        status_lines(s, std::slice::from_ref(&s.device))
    }

    #[test]
    fn a_bluetooth_status_shows_no_channel_or_signal() {
        let mut s = status_of(Protocol::Bluetooth);
        s.device.signal = None;
        let lines = lines(&s);
        let link = lines.iter().find(|l| l.starts_with("link")).unwrap();
        assert!(link.ends_with("not connected"), "{link}");
        assert!(!lines.iter().any(|l| l.starts_with("signal")));
        assert!(
            lines
                .iter()
                .any(|l| l.starts_with("protocol") && l.ends_with("Bluetooth"))
        );
    }

    #[test]
    fn a_connected_bluetooth_keyboard_shows_its_signal_and_last_packet() {
        let mut s = status_of(Protocol::Bluetooth);
        s.slots[0].connected = true;
        s.device.signal = Some(-58);
        s.device.last_packet_ms = Some(400);
        let lines = lines(&s);
        let signal = lines.iter().find(|l| l.starts_with("signal")).unwrap();
        assert!(
            signal.ends_with(" -58 dBm, last packet 400 ms ago"),
            "{signal}"
        );
        let link = lines.iter().find(|l| l.starts_with("link")).unwrap();
        assert!(!link.contains("channel"), "{link}");
    }

    #[test]
    fn a_connected_bluetooth_keyboard_on_standby_is_shown_so() {
        let mut s = status_of(Protocol::Bluetooth);
        s.slots[0].connected = true;
        s.slots[0].standby = true;
        let lines = lines(&s);
        let keyboard = lines.iter().find(|l| l.starts_with("keyboard")).unwrap();
        assert!(keyboard.ends_with(", standby"), "{keyboard}");
    }

    #[test]
    fn a_nuphy_status_with_no_reading_shows_no_signal_level() {
        let mut s = status_of(Protocol::Nuphy);
        s.device.signal = None;
        s.device.last_packet_ms = None;
        let lines = lines(&s);
        let signal = lines.iter().find(|l| l.starts_with("signal")).unwrap();
        assert!(signal.ends_with(" no packet since boot"), "{signal}");
        assert!(!signal.contains("dBm"), "{signal}");
    }

    #[test]
    fn a_radio_that_failed_to_start_is_reported_in_any_protocol() {
        for protocol in Protocol::ALL {
            let mut s = status_of(protocol);
            s.radio_failed = true;
            let lines = lines(&s);
            let link = lines.iter().find(|l| l.starts_with("link")).unwrap();
            assert_eq!(link, "link      the radio failed to start", "{protocol:?}");
        }
    }

    #[test]
    fn a_device_not_connected_shows_no_latency() {
        let s = status_of(Protocol::Bluetooth);
        assert!(s.device.latency.is_some());
        let lines = lines(&s);
        assert!(
            lines.iter().any(|l| l.contains("not connected")),
            "{lines:#?}"
        );
        assert!(
            !lines.iter().any(|l| l.starts_with("latency")),
            "{lines:#?}"
        );
    }

    #[test]
    fn a_device_that_asked_nothing_says_so() {
        let mut s = mouse_status();
        if let Some(link) = &mut s.device.link {
            link.asked = None;
        }
        let lines = status_lines(&s, std::slice::from_ref(&s.device));
        assert!(
            lines.contains(&"asked     nothing".to_string()),
            "{lines:#?}"
        );
    }

    #[test]
    fn a_bolt_device_is_marked_after_its_name() {
        let mut s = mouse_status();
        s.device.pairing = Some(Pairing {
            secure: true,
            passkey: true,
            bolt: true,
        });
        let lines = status_lines(&s, std::slice::from_ref(&s.device));
        assert!(
            lines.contains(&"mouse     MX Master 3S · Bolt, connected".to_string()),
            "{lines:#?}"
        );
    }

    #[test]
    fn a_bluetooth_status_has_a_section_for_each_shown_slot() {
        let s = mouse_status();
        assert_eq!(
            status_lines(&s, &[keyboard_slot(), s.device.clone()]),
            [
                "firmware  1.2.3, bootloader unknown, no SoftDevice",
                "protocol  Bluetooth",
                "link      connected",
                "pairing   closed",
                "usb       live",
                "uptime    50d 02:25:19, reset by pin",
                "counters  dropped 2 usb-retries 5 merges 6",
                "history   1440 minutes, 512 events held",
                "",
                "keyboard  MX Keys, not connected",
                "counters  packets 10",
                "",
                "mouse     MX Master 3S, connected",
                "signal    -60 dBm, last packet 250 ms ago",
                "security  secure, no passkey",
                "params    7.5 ms, latency 44, timeout 2.16 s, 1M",
                "asked     7.5–11.25 ms, latency 44, timeout 2.16 s",
                "counters  packets 2000",
                "latency   no report yet",
            ]
        );
    }

    #[test]
    fn a_slot_with_nothing_paired_says_so() {
        let mut s = status_of(Protocol::Bluetooth);
        s.slots[0].stored = false;
        s.device.name = None;
        s.device.signal = None;
        let lines = lines(&s);
        assert!(
            lines.contains(&"keyboard  not paired".to_string()),
            "{lines:#?}"
        );
    }

    #[test]
    fn a_status_shows_the_passkey_a_keyboard_waits_for() {
        let mut s = status_of(Protocol::Bluetooth);
        s.pairing_window = awaited(42, Some("MX Keys"));
        assert!(
            lines(&s)
                .contains(&"pairing   open, MX Keys waits for the passkey: 000042".to_string()),
            "{:#?}",
            lines(&s)
        );
    }

    #[test]
    fn a_status_shows_the_clicks_a_mouse_waits_for() {
        let mut s = status_of(Protocol::Bluetooth);
        s.pairing_window = clicked(623, Some("MX Master"));
        let want = "pairing   open, MX Master waits for the passkey: \
                    right, left, left, right, right, left, right, right, right, right";
        assert!(lines(&s).contains(&want.to_string()), "{:#?}", lines(&s));
    }

    #[test]
    fn prints_an_unpaired_nuphy_link_without_a_channel() {
        let mut s = unpaired(Protocol::Nuphy);
        s.channel = 2;
        assert_eq!(lines(&s)[2], "link      not paired");
    }

    #[test]
    fn prints_the_signal_of_a_device_not_connected_as_of_a_connected_one() {
        let mut s = status_of(Protocol::Nuphy);
        s.slots[0].connected = false;
        s.device.signal = Some(-85);
        s.device.last_packet_ms = Some(45_000);
        let lines = lines(&s);
        assert!(
            lines.contains(&"signal    -85 dBm, last packet 45 s ago".to_string()),
            "{lines:#?}"
        );
    }

    #[test]
    fn prints_no_signal_or_counters_for_a_slot_with_nothing_paired() {
        let lines = lines(&unpaired(Protocol::Nuphy));
        assert!(!lines.iter().any(|l| l.starts_with("signal")), "{lines:#?}");
        assert_eq!(
            lines.iter().filter(|l| l.starts_with("counters")).count(),
            1,
            "{lines:#?}"
        );
    }

    #[test]
    fn prints_a_keyboard_pairing_on_the_pairing_row() {
        let mut s = status_of(Protocol::Nuphy);
        s.pairing_window = PairingWindow::Pairing;
        assert_eq!(
            lines(&s)[3],
            "pairing   a keyboard is pairing, offers channels 62 52 44, set by hand"
        );
    }

    #[test]
    fn prints_a_nuphy_status() {
        let s = status_of(Protocol::Nuphy);
        assert_eq!(
            lines(&s),
            [
                "firmware  1.2.3, bootloader 0.6.0, SoftDevice S140 6.1.1",
                "protocol  NuPhy",
                "link      connected on channel 26 (paired 26 30 34)",
                "pairing   open, offers channels 62 52 44, set by hand",
                "usb       live",
                "uptime    50d 02:25:19, reset by pin",
                "counters  invalid 1 dropped 2 usb-retries 5 merges 6 surveys 7",
                "history   1440 minutes, 512 events held",
                "",
                "keyboard  MX Keys, ID 7015a329, connected",
                "signal    -71 dBm, last packet 12 ms ago",
                "battery   85%, charging",
                "security  no protection",
                "counters  packets 1000 losses 4 gaps 8",
                "latency   0.9 ms average, 4.2 ms max",
            ]
        );
    }

    #[test]
    fn prints_closed_pairing_in_a_nuphy_status() {
        let mut s = status_of(Protocol::Nuphy);
        s.pairing_window = PairingWindow::Closed;
        assert!(
            lines(&s)
                .contains(&"pairing   closed, offers channels 62 52 44, set by hand".to_string())
        );
    }

    #[test]
    fn prints_a_unifying_status() {
        let s = status_of(Protocol::Unifying);
        assert_eq!(
            lines(&s),
            [
                "firmware  1.2.3, bootloader 0.6.0, SoftDevice S140 6.1.1",
                "protocol  Unifying",
                "link      connected on channel 62",
                "pairing   open",
                "usb       live",
                "uptime    50d 02:25:19, reset by pin",
                "counters  invalid 1 dropped 2 usb-retries 5 merges 6",
                "history   1440 minutes, 512 events held",
                "",
                "keyboard  MX Keys, serial b8bcd5b3, WPID 7015, connected",
                "signal    -71 dBm, last packet 12 ms ago",
                "battery   85%, charging",
                "security  weak at any time",
                "counters  packets 1000",
                "latency   0.9 ms average, 4.2 ms max",
            ]
        );
    }

    #[test]
    fn a_unifying_status_with_no_keyboard_paired_says_its_slot_is_not_paired() {
        let lines = lines(&unpaired(Protocol::Unifying));
        assert!(
            lines.contains(&"keyboard  not paired".to_string()),
            "{lines:#?}"
        );
        assert_eq!(lines[2], "link      not paired");
    }

    #[test]
    fn a_nuphy_keyboard_line_holds_what_is_known_of_the_keyboard() {
        let id = [0x70, 0x15, 0xa3, 0x29];
        for (id, name, line) in [
            (Some(id), None, "keyboard  unnamed, ID 7015a329, connected"),
            (
                None,
                Some("NuPhy Air75 V2"),
                "keyboard  NuPhy Air75 V2, connected",
            ),
            (None, None, "keyboard  unnamed, connected"),
        ] {
            let mut s = status_of(Protocol::Nuphy);
            nuphy(&mut s).keyboard_id = id;
            s.device.name = name.map(String::from);
            let lines = lines(&s);
            assert!(lines.contains(&line.to_string()), "{lines:#?}");
        }
    }

    #[test]
    fn a_unifying_device_line_is_labeled_with_its_type() {
        for (kind, line) in [
            (
                2,
                "mouse     MX Keys, serial b8bcd5b3, WPID 7015, connected",
            ),
            (
                4,
                "presenter MX Keys, serial b8bcd5b3, WPID 7015, connected",
            ),
            (
                0x7f,
                "device    MX Keys, serial b8bcd5b3, WPID 7015, type 0x7f, connected",
            ),
        ] {
            let mut s = status_of(Protocol::Unifying);
            unifying(&mut s).kind = kind;
            let lines = lines(&s);
            assert!(lines.contains(&line.to_string()), "{lines:#?}");
        }
    }

    #[test]
    fn a_keyboard_without_a_name_prints_as_unnamed() {
        let mut s = status_of(Protocol::Unifying);
        s.device.name = None;
        assert!(
            lines(&s)
                .contains(&"keyboard  unnamed, serial b8bcd5b3, WPID 7015, connected".to_string())
        );
    }
}
