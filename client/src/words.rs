use crate::protocol::ProtocolInfo as _;
use crate::protocol::{Battery, ChargeState, Protocol, Slot, SlotStatus, SoftDevice, Status};

pub fn usb_state(live: bool, suspended: bool) -> &'static str {
    match (live, suspended) {
        (_, true) => "suspended",
        (true, false) => "live",
        (false, false) => "not configured",
    }
}

pub fn state_name(s: &Status) -> &'static str {
    health_name(link_health(s))
}

pub fn slot_state(s: &Status, slot: &Slot) -> &'static str {
    health_name(slot_health(s, slot))
}

pub fn slot_connection(slot: &Slot) -> &'static str {
    health_name(connection_health(slot))
}

/// A radio that failed to start overrides every slot's own state.
pub fn slot_health(s: &Status, slot: &Slot) -> LinkHealth {
    if s.radio_failed {
        LinkHealth::RadioFailed
    } else {
        connection_health(slot)
    }
}

pub fn connection_health(slot: &Slot) -> LinkHealth {
    match (slot.connected, slot.standby) {
        (true, false) => LinkHealth::Connected,
        (true, true) => LinkHealth::Standby,
        (false, _) if slot.stored => LinkHealth::Waiting,
        (false, _) => LinkHealth::NotPaired,
    }
}

/// A link is connected while any of its slots is.
pub fn link_health(s: &Status) -> LinkHealth {
    s.slots
        .iter()
        .map(|slot| slot_health(s, slot))
        .max()
        .unwrap_or(LinkHealth::NotPaired)
}

fn health_name(health: LinkHealth) -> &'static str {
    match health {
        LinkHealth::RadioFailed => "the radio failed to start",
        LinkHealth::NotPaired => "not paired",
        LinkHealth::Waiting => "not connected",
        LinkHealth::Standby => "standby",
        LinkHealth::Connected => "connected",
    }
}

/// Ordered from worst to best.
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, serde::Serialize)]
#[serde(rename_all = "kebab-case")]
pub enum LinkHealth {
    RadioFailed,
    NotPaired,
    Waiting,
    Standby,
    Connected,
}

pub fn battery(b: Battery) -> String {
    let name = |c| match c {
        ChargeState::Discharging => "discharging",
        ChargeState::Charging => "charging",
        ChargeState::Charged => "charged",
        ChargeState::Fault => "battery fault",
    };
    match b {
        Battery::Level(level, None | Some(ChargeState::Discharging)) => format!("{level}%"),
        Battery::Level(level, Some(c)) => format!("{level}%, {}", name(c)),
        Battery::Charge(c) => name(c).into(),
    }
}

pub fn version<T: std::fmt::Display>([major, minor, patch]: [T; 3]) -> String {
    format!("{major}.{minor}.{patch}")
}

pub fn duration(ms: u32) -> String {
    let s = ms / 1000;
    match ms {
        0..1_000 => format!("{ms} ms"),
        1_000..60_000 => format!("{s} s"),
        60_000..3_600_000 => format!("{} min {} s", s / 60, s % 60),
        _ => format!("{} h {} min", s / 3600, s / 60 % 60),
    }
}

pub fn last_packet(age_ms: Option<u32>) -> String {
    age_ms.map_or("no packet since boot".into(), |ms| {
        format!("last packet {} ago", duration(ms))
    })
}

/// A device that advertised no name shows as unnamed.
pub fn shown_name(name: Option<&str>) -> &str {
    name.unwrap_or(UNNAMED)
}

/// Stands in for a device that has not sent its name.
pub const UNNAMED: &str = "unnamed";

pub fn signal(d: &SlotStatus) -> String {
    let level = d.signal.map_or(String::new(), |dbm| format!("{dbm} dBm, "));
    format!("{level}{}", last_packet(d.last_packet_ms))
}

pub fn switch_question(to: Protocol) -> String {
    format!(
        "Switch to {}? The dongle reboots and history starts over.",
        to.name()
    )
}

const RESET_CAUSES: [&str; 17] = [
    "pin",
    "software",
    "brownout",
    "power-on",
    "watchdog",
    "debug",
    "security",
    "low-power wake",
    "cpu lockup",
    "parity",
    "pll",
    "clock",
    "hardware",
    "user",
    "temperature",
    "bootloader",
    "flash",
];

pub fn reset_causes(bits: u32) -> String {
    let names: Vec<_> = RESET_CAUSES
        .iter()
        .enumerate()
        .filter(|(i, _)| bits & (1 << i) != 0)
        .map(|(_, name)| *name)
        .collect();
    if names.is_empty() {
        "none".into()
    } else {
        names.join(", ")
    }
}

pub fn reset_by(bits: u32) -> String {
    format!("reset by {}", reset_causes(bits))
}

pub fn bootloader(s: &Status) -> String {
    s.bootloader_version.map_or("unknown".into(), version)
}

pub fn softdevice(s: &Status) -> Option<String> {
    match s.softdevice {
        SoftDevice::Absent => None,
        SoftDevice::Unnamed { fwid } => Some(format!("FWID {fwid:#06x}")),
        SoftDevice::Named { id, version } => {
            let version = version.map_or(String::new(), |v| format!(" {}", self::version(v)));
            Some(format!("S{id}{version}"))
        }
    }
}

pub fn bootloader_and_softdevice(s: &Status) -> [String; 2] {
    let softdevice = softdevice(s).map_or("no SoftDevice".into(), |d| format!("SoftDevice {d}"));
    [format!("bootloader {}", bootloader(s)), softdevice]
}

pub fn uptime(ms: u64) -> String {
    let s = ms / 1000;
    let (days, h, m, s) = (s / 86400, s / 3600 % 24, s / 60 % 60, s % 60);
    if days > 0 {
        format!("{days}d {h:02}:{m:02}:{s:02}")
    } else {
        format!("{h:02}:{m:02}:{s:02}")
    }
}

pub fn counters_line(s: &Status) -> String {
    let counters: Vec<String> = s
        .protocol()
        .counters()
        .iter()
        .filter_map(|&counter| Some(format!("{} {}", counter.name(), s.counters.get(counter)?)))
        .collect();
    counters.join(" ")
}

pub fn history_line(s: &Status) -> String {
    format!(
        "{} minutes, {} events held",
        s.minute_next.saturating_sub(s.minute_oldest),
        s.events_total.saturating_sub(s.events_oldest)
    )
}
