use diagclient::protocol::{Battery, ChargeState, Protocol, ProtocolInfo as _, SlotStatus, Status};
use diagclient::words::{
    LinkHealth, battery, bootloader, counters_line, history_line, link_health, reset_by,
    shown_name, signal, slot_health, slot_state, softdevice, state_name, switch_question, uptime,
    usb_state, version,
};
use serde::Serialize;

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct StatusView {
    pub protocol: &'static str,
    pub link: State,
    pub usb: &'static str,
    pub devices: Vec<DeviceView>,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct State {
    pub code: LinkHealth,
    pub text: &'static str,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DeviceView {
    pub kind: &'static str,
    pub name: Option<String>,
    pub state: State,
    pub signal_dbm: Option<i8>,
    pub signal_text: Option<String>,
    pub battery_percent: Option<u8>,
    pub charge: Option<ChargeState>,
    pub battery_text: Option<String>,
}

#[derive(Serialize)]
pub struct Snapshot {
    pub status: StatusView,
    pub dongle: DongleView,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DongleView {
    pub rows: Vec<Row>,
    pub protocol: u8,
    pub protocols: Vec<ProtocolChoice>,
}

#[derive(Serialize)]
pub struct Row {
    pub label: &'static str,
    pub value: String,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ProtocolChoice {
    pub code: u8,
    pub name: &'static str,
    pub question: String,
}

pub fn snapshot(s: &Status, slots: &[SlotStatus]) -> Snapshot {
    Snapshot {
        status: status_view(s, slots),
        dongle: dongle_view(s),
    }
}

pub fn status_view(s: &Status, slots: &[SlotStatus]) -> StatusView {
    StatusView {
        protocol: s.protocol().name(),
        link: State {
            code: link_health(s),
            text: state_name(s),
        },
        usb: usb_state(s.usb_live, s.usb_suspended),
        devices: slots.iter().filter_map(|d| device_view(s, d)).collect(),
    }
}

fn device_view(s: &Status, d: &SlotStatus) -> Option<DeviceView> {
    let slot = s.slot(d.slot)?;
    let signal_shown = s.signal_shown(d);
    let (battery_percent, charge_state) = match d.battery {
        Some(Battery::Level(level, c)) => (Some(level), c),
        Some(Battery::Charge(c)) => (None, Some(c)),
        None => (None, None),
    };
    Some(DeviceView {
        kind: slot.kind.name(),
        name: slot
            .holds_device()
            .then(|| shown_name(d.name.as_deref()).to_string()),
        state: State {
            code: slot_health(s, slot),
            text: slot_state(s, slot),
        },
        signal_dbm: d.signal.filter(|_| signal_shown),
        signal_text: signal_shown.then(|| signal(d)),
        battery_percent,
        charge: charge_state,
        battery_text: d.battery.map(battery),
    })
}

pub fn dongle_view(s: &Status) -> DongleView {
    DongleView {
        rows: vec![
            Row {
                label: "firmware",
                value: version(s.firmware_version),
            },
            Row {
                label: "bootloader",
                value: bootloader(s),
            },
            Row {
                label: "softdevice",
                value: softdevice(s).unwrap_or_else(|| "none".into()),
            },
            Row {
                label: "uptime",
                value: uptime(s.uptime_ms),
            },
            Row {
                label: "reset",
                value: reset_by(s.reset_cause),
            },
            Row {
                label: "counters",
                value: counters_line(s),
            },
            Row {
                label: "history",
                value: history_line(s),
            },
        ],
        protocol: s.protocol().into(),
        protocols: Protocol::ALL
            .into_iter()
            .map(|p| ProtocolChoice {
                code: p.into(),
                name: p.name(),
                question: switch_question(p),
            })
            .collect(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use diagclient::protocol::testing::{mouse_status, status_of};
    use serde_json::json;

    fn keyboard() -> SlotStatus {
        SlotStatus {
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
        }
    }

    #[test]
    fn a_bluetooth_keyboard_and_mouse_give_a_device_each() {
        let s = mouse_status();
        let view = serde_json::to_value(status_view(&s, &[keyboard(), s.device.clone()])).unwrap();
        assert_eq!(
            view,
            json!({
                "protocol": "Bluetooth",
                "link": {"code": "connected", "text": "connected"},
                "usb": "live",
                "devices": [
                    {
                        "kind": "keyboard",
                        "name": "MX Keys",
                        "state": {"code": "waiting", "text": "not connected"},
                        "signalDbm": null,
                        "signalText": null,
                        "batteryPercent": null,
                        "charge": null,
                        "batteryText": null,
                    },
                    {
                        "kind": "mouse",
                        "name": "MX Master 3S",
                        "state": {"code": "connected", "text": "connected"},
                        "signalDbm": -60,
                        "signalText": "-60 dBm, last packet 250 ms ago",
                        "batteryPercent": null,
                        "charge": null,
                        "batteryText": null,
                    },
                ],
            })
        );
    }

    #[test]
    fn a_charging_battery_gives_its_level_and_charge() {
        let mut s = mouse_status();
        s.device.battery = Some(Battery::Level(80, Some(ChargeState::Charging)));
        let view = status_view(&s, std::slice::from_ref(&s.device));
        let mouse = &view.devices[0];
        assert_eq!(mouse.battery_percent, Some(80));
        assert_eq!(json!(mouse.charge), "charging");
        assert_eq!(mouse.battery_text.as_deref(), Some("80%, charging"));
    }

    #[test]
    fn an_empty_slot_reports_a_failed_radio() {
        let mut s = status_of(Protocol::Bluetooth);
        s.slots[0].stored = false;
        s.slots[0].connected = false;
        s.radio_failed = true;
        let view = status_view(&s, std::slice::from_ref(&s.device));
        let keyboard = view.devices.iter().find(|d| d.kind == "keyboard").unwrap();
        assert_eq!(json!(keyboard.state.code), "radio-failed");
        assert_eq!(keyboard.state.text, "the radio failed to start");
        assert_eq!(keyboard.name, None);
    }

    #[test]
    fn an_empty_slot_says_not_paired() {
        let mut s = status_of(Protocol::Bluetooth);
        s.slots[0].stored = false;
        s.slots[0].connected = false;
        let view = status_view(&s, std::slice::from_ref(&s.device));
        let keyboard = view.devices.iter().find(|d| d.kind == "keyboard").unwrap();
        assert_eq!(
            (json!(keyboard.state.code), keyboard.state.text),
            (json!("not-paired"), "not paired")
        );
    }

    #[test]
    fn the_dongle_details_match_what_status_prints() {
        let view = dongle_view(&mouse_status());
        let rows: Vec<(&str, &str)> = view
            .rows
            .iter()
            .map(|r| (r.label, r.value.as_str()))
            .collect();
        assert_eq!(
            rows,
            [
                ("firmware", "1.2.3"),
                ("bootloader", "unknown"),
                ("softdevice", "none"),
                ("uptime", "50d 02:25:19"),
                ("reset", "reset by pin"),
                ("counters", "dropped 2 usb-retries 5 merges 6"),
                ("history", "1440 minutes, 512 events held"),
            ]
        );
        assert_eq!(view.protocol, Protocol::Bluetooth as u8);
        let choices: Vec<(u8, &str, &str)> = view
            .protocols
            .iter()
            .map(|p| (p.code, p.name, p.question.as_str()))
            .collect();
        assert_eq!(
            choices,
            [
                (
                    1,
                    "NuPhy",
                    "Switch to NuPhy? The dongle reboots and history starts over."
                ),
                (
                    2,
                    "Unifying",
                    "Switch to Unifying? The dongle reboots and history starts over."
                ),
                (
                    3,
                    "Bluetooth",
                    "Switch to Bluetooth? The dongle reboots and history starts over."
                ),
            ]
        );
    }
}
