use diagclient::protocol::{ProtocolInfo as _, SlotStatus, Status};
use diagclient::words::{battery, shown_name, signal, slot_state, state_name, usb_state, version};

pub fn status_rows(s: &Status, slots: &[SlotStatus]) -> Vec<(&'static str, String)> {
    let mut rows = vec![
        ("firmware", version(s.firmware_version)),
        ("protocol", s.protocol().name().to_string()),
        ("link", state_name(s).to_string()),
        ("usb", usb_state(s.usb_live, s.usb_suspended).to_string()),
    ];
    for d in slots {
        let Some(slot) = s.slot(d.slot) else { continue };
        let device = if slot.holds_device() {
            format!("{}, {}", shown_name(d.name.as_deref()), slot_state(s, slot))
        } else {
            slot_state(s, slot).to_string()
        };
        rows.push((slot.kind.name(), device));
        if s.signal_shown(d) {
            rows.push(("signal", signal(d)));
        }
        if let Some(b) = d.battery {
            rows.push(("battery", battery(b)));
        }
    }
    rows
}

#[cfg(test)]
mod tests {
    use super::*;
    use diagclient::protocol::testing::mouse_status;

    #[test]
    fn a_bluetooth_keyboard_and_mouse_show_a_row_each() {
        let s = mouse_status();
        let keyboard = SlotStatus {
            slot: 0,
            name: Some("MX Keys".into()),
            signal: None,
            last_packet_ms: None,
            packets: 10,
            gaps: None,
            link_losses: None,
            latency: None,
            pairing: None,
            battery: None,
            link: None,
        };
        let rows: Vec<(&str, String)> = status_rows(&s, &[keyboard, s.device.clone()]);
        let expected: Vec<(&str, String)> = [
            ("firmware", "1.2.3"),
            ("protocol", "Bluetooth"),
            ("link", "connected"),
            ("usb", "live"),
            ("keyboard", "MX Keys, not connected"),
            ("mouse", "MX Master 3S, connected"),
            ("signal", "-60 dBm, last packet 250 ms ago"),
        ]
        .map(|(l, v)| (l, v.to_string()))
        .into();
        assert_eq!(rows, expected);
    }

    #[test]
    fn a_reported_battery_follows_its_device() {
        let mut s = mouse_status();
        s.device.battery = Some(diagclient::protocol::Battery::Level(80, None));
        let rows = status_rows(&s, std::slice::from_ref(&s.device));
        let at = rows.iter().position(|(l, _)| *l == "mouse").unwrap();
        assert_eq!(rows[at + 2], ("battery", "80%".to_string()));
    }

    #[test]
    fn an_empty_slot_reports_a_failed_radio() {
        let mut s =
            diagclient::protocol::testing::status_of(diagclient::protocol::Protocol::Bluetooth);
        s.slots[0].stored = false;
        s.slots[0].connected = false;
        s.radio_failed = true;
        let rows = status_rows(&s, std::slice::from_ref(&s.device));
        assert!(
            rows.contains(&("keyboard", "the radio failed to start".to_string())),
            "{rows:#?}"
        );
    }

    #[test]
    fn an_empty_slot_says_not_paired() {
        let mut s =
            diagclient::protocol::testing::status_of(diagclient::protocol::Protocol::Bluetooth);
        s.slots[0].stored = false;
        s.slots[0].connected = false;
        let rows = status_rows(&s, std::slice::from_ref(&s.device));
        assert!(
            rows.contains(&("keyboard", "not paired".to_string())),
            "{rows:#?}"
        );
    }
}
