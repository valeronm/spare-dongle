use diagclient::device::{Device, Transport};
use diagclient::protocol::{
    Battery, ChargeState, Counter, DeviceKind, Protocol, ResetCause, SlotStatus, SoftDevice,
    Status, VersionMismatch,
};
use diagclient::report::status_lines;
use diagclient::words::{
    LinkHealth, SignalQuality, Usb, battery_low, counters, level_charge, link_health, signal_bars,
    signal_quality, slot_health, usb,
};
use diagwire::VERSION;
use serde::Serialize;

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct StatusView {
    protocol: Protocol,
    link: LinkHealth,
    usb: Usb,
    devices: Vec<DeviceView>,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct DeviceView {
    slot: u8,
    kind: DeviceKind,
    name: Option<String>,
    state: LinkHealth,
    signal: Option<SignalView>,
    battery_level: Option<LevelView>,
    charge: Option<ChargeState>,
}

#[derive(Serialize)]
struct SignalView {
    dbm: i8,
    quality: SignalQuality,
    bars: u8,
}

#[derive(Serialize)]
struct LevelView {
    percent: u8,
    low: bool,
}

#[derive(Serialize)]
pub struct Snapshot {
    status: StatusView,
    dongle: DongleView,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct DongleView {
    firmware: Version<u8>,
    /// `None` when no bootloader left its version for the firmware.
    bootloader: Option<Version<u8>>,
    softdevice: SoftDeviceView,
    uptime_ms: u64,
    reset_causes: Vec<ResetCause>,
    counters: Vec<CounterView>,
    history: History,
    protocols: Vec<Protocol>,
}

#[derive(Serialize)]
struct Version<T> {
    major: T,
    minor: T,
    patch: T,
}

impl<T> From<[T; 3]> for Version<T> {
    fn from([major, minor, patch]: [T; 3]) -> Self {
        Self {
            major,
            minor,
            patch,
        }
    }
}

#[derive(Serialize)]
#[serde(tag = "kind", rename_all = "kebab-case")]
enum SoftDeviceView {
    Absent,
    Unnamed {
        fwid: u16,
    },
    Named {
        id: u32,
        version: Option<Version<u32>>,
    },
}

impl From<SoftDevice> for SoftDeviceView {
    fn from(d: SoftDevice) -> Self {
        match d {
            SoftDevice::Absent => Self::Absent,
            SoftDevice::Unnamed { fwid } => Self::Unnamed { fwid },
            SoftDevice::Named { id, version } => Self::Named {
                id,
                version: version.map(Version::from),
            },
        }
    }
}

#[derive(Serialize)]
struct CounterView {
    key: Counter,
    value: u32,
}

#[derive(Serialize)]
struct History {
    minutes: u32,
    events: u32,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub enum Reading {
    Snapshot(Snapshot),
    Mismatch(Mismatch),
}

#[derive(Serialize)]
pub struct Mismatch {
    dongle: u8,
    app: u8,
    older: Side,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
enum Side {
    Dongle,
    App,
}

impl From<VersionMismatch> for Mismatch {
    fn from(m: VersionMismatch) -> Self {
        Self {
            dongle: m.dongle,
            app: VERSION,
            older: if m.dongle_older() {
                Side::Dongle
            } else {
                Side::App
            },
        }
    }
}

pub fn read<T: Transport>(dev: &mut Device<T>) -> anyhow::Result<Reading> {
    Ok(match dev.read_status()? {
        Ok(s) => Reading::Snapshot(Snapshot {
            status: status_view(&s, &dev.shown(&s)?),
            dongle: dongle_view(&s),
        }),
        Err(m) => Reading::Mismatch(m.into()),
    })
}

/// Fails on a dongle in another diagnostic protocol.
pub fn report_text<T: Transport>(dev: &mut Device<T>) -> anyhow::Result<String> {
    let s = dev.status()?;
    Ok(status_lines(&s, &dev.shown(&s)?).join("\n"))
}

fn status_view(s: &Status, slots: &[SlotStatus]) -> StatusView {
    StatusView {
        protocol: s.protocol(),
        link: link_health(s),
        usb: usb(s.usb_live, s.usb_suspended),
        devices: slots.iter().filter_map(|d| device_view(s, d)).collect(),
    }
}

fn device_view(s: &Status, d: &SlotStatus) -> Option<DeviceView> {
    let slot = s.slot(d.slot)?;
    let (percent, charge) = match d.battery {
        Some(Battery::Level(level, c)) => (Some(level), level_charge(c)),
        Some(Battery::Charge(c)) => (None, Some(c)),
        None => (None, None),
    };
    Some(DeviceView {
        slot: d.slot,
        kind: slot.kind,
        name: d.name.clone().filter(|_| slot.holds_device()),
        state: slot_health(s, slot),
        signal: s.live_signal(d).map(|dbm| SignalView {
            dbm,
            quality: signal_quality(dbm),
            bars: signal_bars(dbm),
        }),
        battery_level: percent.map(|percent| LevelView {
            percent,
            low: battery_low(percent),
        }),
        charge,
    })
}

fn dongle_view(s: &Status) -> DongleView {
    DongleView {
        firmware: s.firmware_version.into(),
        bootloader: s.bootloader_version.map(Version::from),
        softdevice: s.softdevice.into(),
        uptime_ms: s.uptime_ms,
        reset_causes: ResetCause::from_bits(s.reset_cause),
        counters: counters(s)
            .into_iter()
            .map(|(key, value)| CounterView { key, value })
            .collect(),
        history: History {
            minutes: s.minutes_held(),
            events: s.events_held(),
        },
        protocols: Protocol::ALL.to_vec(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use diagclient::device::testing::{Fake, parts};
    use diagclient::protocol::testing::{
        edited, fixture, keyboard_slot, mouse_status, status_in_protocol, status_of,
    };
    use serde_json::{Value, json};

    fn status_json(s: &Status) -> Value {
        serde_json::to_value(status_view(s, std::slice::from_ref(&s.device))).unwrap()
    }

    fn device_json(s: &Status) -> Value {
        status_json(s)["devices"][0].clone()
    }

    fn dongle_json(s: &Status) -> Value {
        serde_json::to_value(dongle_view(s)).unwrap()
    }

    fn keyboard_json(s: &Status) -> Value {
        status_json(s)["devices"]
            .as_array()
            .unwrap()
            .iter()
            .find(|d| d["kind"] == "keyboard")
            .unwrap()
            .clone()
    }

    #[test]
    fn a_bluetooth_keyboard_and_mouse_give_a_device_each() {
        let s = mouse_status();
        let view =
            serde_json::to_value(status_view(&s, &[keyboard_slot(), s.device.clone()])).unwrap();
        assert_eq!(
            view,
            json!({
                "protocol": "bluetooth",
                "link": "connected",
                "usb": "live",
                "devices": [
                    {
                        "slot": 0,
                        "kind": "keyboard",
                        "name": "MX Keys",
                        "state": "waiting",
                        "signal": null,
                        "batteryLevel": null,
                        "charge": null,
                    },
                    {
                        "slot": 1,
                        "kind": "mouse",
                        "name": "MX Master 3S",
                        "state": "connected",
                        "signal": {"dbm": -60, "quality": "good", "bars": 4},
                        "batteryLevel": null,
                        "charge": null,
                    },
                ],
            })
        );
    }

    #[test]
    fn a_charging_battery_gives_its_level_and_charge() {
        let mut s = mouse_status();
        s.device.battery = Some(Battery::Level(80, Some(ChargeState::Charging)));
        let mouse = device_json(&s);
        assert_eq!(
            (&mouse["batteryLevel"], &mouse["charge"]),
            (&json!({"percent": 80, "low": false}), &json!("charging"))
        );
    }

    #[test]
    fn a_discharging_battery_gives_its_charge_only_without_a_level() {
        let shown: Vec<_> = [
            Battery::Level(80, Some(ChargeState::Discharging)),
            Battery::Charge(ChargeState::Discharging),
        ]
        .into_iter()
        .map(|battery| {
            let mut s = mouse_status();
            s.device.battery = Some(battery);
            device_json(&s)["charge"].clone()
        })
        .collect();
        assert_eq!(shown, [json!(null), json!("discharging")]);
    }

    #[test]
    fn a_battery_at_15_percent_or_below_is_low() {
        let levels: Vec<_> = [16, 15, 0]
            .into_iter()
            .map(|percent| {
                let mut s = mouse_status();
                s.device.battery = Some(Battery::Level(percent, None));
                device_json(&s)["batteryLevel"].clone()
            })
            .collect();
        assert_eq!(
            levels,
            [
                json!({"percent": 16, "low": false}),
                json!({"percent": 15, "low": true}),
                json!({"percent": 0, "low": true}),
            ]
        );
    }

    #[test]
    fn a_battery_of_unknown_level_gives_its_charge_alone() {
        let mut s = mouse_status();
        s.device.battery = Some(Battery::Charge(ChargeState::Fault));
        let mouse = device_json(&s);
        assert_eq!(
            (mouse.get("batteryLevel"), &mouse["charge"]),
            (Some(&json!(null)), &json!("fault"))
        );
    }

    #[test]
    fn the_usb_is_suspended_whenever_the_bus_is() {
        let usb: Vec<_> = [(true, false), (false, false), (true, true), (false, true)]
            .into_iter()
            .map(|(live, suspended)| {
                let mut s = mouse_status();
                s.usb_live = live;
                s.usb_suspended = suspended;
                status_json(&s)["usb"].clone()
            })
            .collect();
        assert_eq!(
            usb,
            [
                json!("live"),
                json!("not-configured"),
                json!("suspended"),
                json!("suspended")
            ]
        );
    }

    #[test]
    fn an_empty_slot_reports_a_failed_radio() {
        let mut s = status_of(Protocol::Bluetooth);
        s.slots[0].stored = false;
        s.slots[0].connected = false;
        s.radio_failed = true;
        let keyboard = keyboard_json(&s);
        assert_eq!(
            (
                &keyboard["state"],
                &keyboard["name"],
                &status_json(&s)["link"]
            ),
            (&json!("radio-failed"), &json!(null), &json!("radio-failed"))
        );
    }

    #[test]
    fn the_signal_level_gives_its_quality_and_bars() {
        let shown: Vec<_> = [-60, -70, -71, -78, -79]
            .into_iter()
            .map(|dbm| {
                let mut s = mouse_status();
                s.device.signal = Some(dbm);
                device_json(&s)["signal"].clone()
            })
            .collect();
        assert_eq!(
            shown,
            [
                json!({"dbm": -60, "quality": "good", "bars": 4}),
                json!({"dbm": -70, "quality": "good", "bars": 3}),
                json!({"dbm": -71, "quality": "fair", "bars": 2}),
                json!({"dbm": -78, "quality": "fair", "bars": 2}),
                json!({"dbm": -79, "quality": "weak", "bars": 1}),
            ]
        );
    }

    #[test]
    fn a_device_without_a_reading_has_no_signal() {
        let mut s = status_of(Protocol::Nuphy);
        s.device.signal = None;
        assert_eq!(device_json(&s).get("signal"), Some(&json!(null)));
    }

    fn not_connected(protocol: Protocol) -> Status {
        let mut s = status_of(protocol);
        s.slots[0].stored = true;
        s.slots[0].connected = false;
        s.device.signal = Some(-58);
        s
    }

    fn connected(protocol: Protocol) -> Status {
        let mut s = not_connected(protocol);
        s.slots[0].connected = true;
        s
    }

    #[test]
    fn only_a_connected_device_has_a_signal() {
        for protocol in Protocol::ALL {
            let mut standby = connected(protocol);
            standby.slots[0].standby = true;
            let mut radio_failed = connected(protocol);
            radio_failed.radio_failed = true;
            let shown: Vec<_> = [
                connected(protocol),
                standby,
                radio_failed,
                not_connected(protocol),
            ]
            .iter()
            .map(|s| device_json(s)["signal"]["dbm"].clone())
            .collect();
            assert_eq!(
                shown,
                [json!(-58), json!(-58), json!(-58), json!(null)],
                "{protocol:?}"
            );
        }
    }

    #[test]
    fn a_device_that_sent_no_name_has_none() {
        let mut s = mouse_status();
        s.device.name = None;
        assert_eq!(device_json(&s)["name"], json!(null));
    }

    fn dongle_answering(payload: Vec<u8>) -> Device<Fake> {
        Device::new(Fake::new(move |req| parts(req[0], req[1], &payload)), 0)
    }

    #[test]
    fn a_dongle_in_this_protocol_gives_a_snapshot() {
        let reading =
            serde_json::to_value(read(&mut dongle_answering(fixture("status.hex"))).unwrap())
                .unwrap();
        assert_eq!(reading["snapshot"]["status"]["protocol"], "nuphy");
    }

    #[test]
    fn a_dongle_in_another_protocol_gives_both_versions_and_the_older_side() {
        let app = fixture("status.hex")[0];
        let readings: Vec<_> = [app - 1, app + 1]
            .into_iter()
            .map(|dongle| {
                serde_json::to_value(
                    read(&mut dongle_answering(status_in_protocol(dongle))).unwrap(),
                )
                .unwrap()
            })
            .collect();
        assert_eq!(
            readings,
            [
                json!({"mismatch": {"dongle": app - 1, "app": app, "older": "dongle"}}),
                json!({"mismatch": {"dongle": app + 1, "app": app, "older": "app"}}),
            ]
        );
    }

    #[test]
    fn the_report_has_a_section_for_each_shown_slot() {
        let mut dev = Device::new(
            Fake::new(|req| {
                let reply = edited("status-slot1.hex", |s| s.slot = req[6]);
                parts(req[0], req[1], &reply)
            }),
            0,
        );
        let report = report_text(&mut dev).unwrap();
        let sections: Vec<&str> = report
            .split("\n\n")
            .map(|section| section.split_whitespace().next().unwrap())
            .collect();
        assert_eq!(sections, ["firmware", "keyboard", "mouse"]);
    }

    #[test]
    fn a_report_from_a_dongle_in_another_protocol_fails() {
        let app = fixture("status.hex")[0];
        let mut dev = dongle_answering(status_in_protocol(app + 1));
        let err = report_text(&mut dev).unwrap_err();
        assert!(err.downcast_ref::<VersionMismatch>().is_some(), "{err:#}");
    }

    #[test]
    fn an_empty_slot_is_not_paired() {
        let mut s = status_of(Protocol::Bluetooth);
        s.slots[0].stored = false;
        s.slots[0].connected = false;
        assert_eq!(keyboard_json(&s)["state"], json!("not-paired"));
    }

    #[test]
    fn the_dongle_details_hold_what_status_prints() {
        assert_eq!(
            dongle_json(&mouse_status()),
            json!({
                "firmware": {"major": 1, "minor": 2, "patch": 3},
                "bootloader": null,
                "softdevice": {"kind": "absent"},
                "uptimeMs": 4_328_719_365_u64,
                "resetCauses": ["pin"],
                "counters": [
                    {"key": "dropped", "value": 2},
                    {"key": "usb-retries", "value": 5},
                    {"key": "merges", "value": 6},
                ],
                "history": {"minutes": 1440, "events": 512},
                "protocols": ["nuphy", "unifying", "bluetooth"],
            })
        );
    }

    #[test]
    fn a_nuphy_dongle_lists_its_own_link_wide_counters() {
        let keys: Vec<_> = dongle_json(&status_of(Protocol::Nuphy))["counters"]
            .as_array()
            .unwrap()
            .iter()
            .map(|c| c["key"].clone())
            .collect();
        assert_eq!(
            keys,
            [
                json!("invalid"),
                json!("dropped"),
                json!("usb-retries"),
                json!("merges"),
                json!("surveys")
            ]
        );
    }

    #[test]
    fn the_bootloader_gives_its_version_when_it_left_one() {
        let mut s = mouse_status();
        s.bootloader_version = Some([0, 6, 0]);
        assert_eq!(
            dongle_json(&s)["bootloader"],
            json!({"major": 0, "minor": 6, "patch": 0})
        );
    }

    #[test]
    fn the_softdevice_gives_what_identifies_it() {
        let shown: Vec<_> = [
            SoftDevice::Unnamed { fwid: 0x00b6 },
            SoftDevice::Named {
                id: 140,
                version: None,
            },
            SoftDevice::Named {
                id: 140,
                version: Some([6, 1, 1]),
            },
        ]
        .into_iter()
        .map(|softdevice| {
            let mut s = mouse_status();
            s.softdevice = softdevice;
            dongle_json(&s)["softdevice"].clone()
        })
        .collect();
        assert_eq!(
            shown,
            [
                json!({"kind": "unnamed", "fwid": 0x00b6}),
                json!({"kind": "named", "id": 140, "version": null}),
                json!({"kind": "named", "id": 140, "version": {"major": 6, "minor": 1, "patch": 1}}),
            ]
        );
    }

    #[test]
    fn the_reset_causes_follow_the_bits_in_bit_order() {
        let shown: Vec<_> = [0, 0x11, 1 << 16]
            .into_iter()
            .map(|bits| {
                let mut s = mouse_status();
                s.reset_cause = bits;
                dongle_json(&s)["resetCauses"].clone()
            })
            .collect();
        assert_eq!(
            shown,
            [json!([]), json!(["pin", "watchdog"]), json!(["flash"])]
        );
    }

    #[test]
    fn a_history_whose_oldest_passed_its_next_holds_nothing() {
        let mut s = mouse_status();
        s.minute_oldest = s.minute_next + 1;
        s.events_oldest = s.events_total + 1;
        assert_eq!(
            dongle_json(&s)["history"],
            json!({"minutes": 0, "events": 0})
        );
    }
}
