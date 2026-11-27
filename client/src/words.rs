use crate::protocol::{Battery, ChargeState, Slot, SlotStatus, Status};

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
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
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
