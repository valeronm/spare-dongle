use crate::protocol::ProtocolInfo as _;
use crate::protocol::{
    AskedParams, AwaitedPasskey, Battery, ChargeState, Counter, Latency, LinkParams, Pairing, Phy,
    Protocol, ResetCause, Slot, SlotStatus, SoftDevice, Status,
};

crate::with_all! {
    #[derive(Clone, Copy, Debug, PartialEq, Eq, serde::Serialize)]
    #[serde(rename_all = "kebab-case")]
    pub enum Usb {
        Live,
        Suspended,
        NotConfigured,
    }
}

/// A suspended bus keeps its configuration.
pub fn usb(live: bool, suspended: bool) -> Usb {
    match (live, suspended) {
        (_, true) => Usb::Suspended,
        (true, false) => Usb::Live,
        (false, false) => Usb::NotConfigured,
    }
}

pub fn usb_state(live: bool, suspended: bool) -> &'static str {
    match usb(live, suspended) {
        Usb::Suspended => "suspended",
        Usb::Live => "live",
        Usb::NotConfigured => "not configured",
    }
}

pub fn state_name(s: &Status) -> &'static str {
    health_name(link_health(s))
}

pub(crate) fn slot_connection(slot: &Slot) -> &'static str {
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

pub fn health_name(health: LinkHealth) -> &'static str {
    match health {
        LinkHealth::RadioFailed => "the radio failed to start",
        LinkHealth::NotPaired => "not paired",
        LinkHealth::Waiting => "not connected",
        LinkHealth::Standby => "standby",
        LinkHealth::Connected => "connected",
    }
}

crate::with_all! {
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
}

pub fn battery(b: Battery) -> String {
    let name = |c| match c {
        ChargeState::Discharging => "discharging",
        ChargeState::Charging => "charging",
        ChargeState::Charged => "charged",
        ChargeState::Fault => "battery fault",
    };
    match b {
        Battery::Level(level, c) => match level_charge(c) {
            None => format!("{level}%"),
            Some(c) => format!("{level}%, {}", name(c)),
        },
        Battery::Charge(c) => name(c).into(),
    }
}

/// The charge state worth showing beside a known level, which alone implies discharging.
pub fn level_charge(c: Option<ChargeState>) -> Option<ChargeState> {
    c.filter(|c| *c != ChargeState::Discharging)
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

pub const UNNAMED: &str = "unnamed";

/// A link that averaged -70 dBm had no gaps, and gaps followed packets at -78 dBm or weaker.
pub const SIGNAL_GOOD_DBM: i8 = -70;
pub const SIGNAL_WEAK_DBM: i8 = -78;
/// The top bar's cutoff is unmeasured, since a link at `SIGNAL_GOOD_DBM` already had no gaps.
pub const SIGNAL_STRONG_DBM: i8 = -60;

crate::with_all! {
    #[derive(Clone, Copy, Debug, PartialEq, Eq, serde::Serialize)]
    #[serde(rename_all = "kebab-case")]
    pub enum SignalQuality {
        Good,
        Fair,
        Weak,
    }
}

pub fn signal_quality(dbm: i8) -> SignalQuality {
    match dbm {
        SIGNAL_GOOD_DBM.. => SignalQuality::Good,
        SIGNAL_WEAK_DBM.. => SignalQuality::Fair,
        _ => SignalQuality::Weak,
    }
}

/// Out of 4.
pub fn signal_bars(dbm: i8) -> u8 {
    match dbm {
        SIGNAL_STRONG_DBM.. => 4,
        SIGNAL_GOOD_DBM.. => 3,
        SIGNAL_WEAK_DBM.. => 2,
        _ => 1,
    }
}

pub const BATTERY_LOW_PERCENT: u8 = 15;

pub fn battery_low(percent: u8) -> bool {
    percent <= BATTERY_LOW_PERCENT
}

pub fn signal(d: &SlotStatus) -> String {
    let level = d.signal.map_or(String::new(), |dbm| format!("{dbm} dBm, "));
    format!("{level}{}", last_packet(d.last_packet_ms))
}

fn reset_cause(cause: ResetCause) -> &'static str {
    match cause {
        ResetCause::Pin => "pin",
        ResetCause::Software => "software",
        ResetCause::Brownout => "brownout",
        ResetCause::PowerOn => "power-on",
        ResetCause::Watchdog => "watchdog",
        ResetCause::Debug => "debug",
        ResetCause::Security => "security",
        ResetCause::LowPowerWake => "low-power wake",
        ResetCause::CpuLockup => "cpu lockup",
        ResetCause::Parity => "parity",
        ResetCause::Pll => "pll",
        ResetCause::Clock => "clock",
        ResetCause::Hardware => "hardware",
        ResetCause::User => "user",
        ResetCause::Temperature => "temperature",
        ResetCause::Bootloader => "bootloader",
        ResetCause::Flash => "flash",
    }
}

pub fn reset_causes(bits: u32) -> String {
    let names: Vec<_> = ResetCause::from_bits(bits)
        .into_iter()
        .map(reset_cause)
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

/// The link-wide counters `s`'s protocol counts, in the order status lists them.
pub fn counters(s: &Status) -> Vec<(Counter, u32)> {
    s.protocol()
        .counters()
        .iter()
        .filter_map(|&counter| Some((counter, s.counters.get(counter)?)))
        .collect()
}

pub fn counters_line(s: &Status) -> String {
    let counters: Vec<String> = counters(s)
        .into_iter()
        .map(|(counter, value)| format!("{} {value}", counter.name()))
        .collect();
    counters.join(" ")
}

pub fn history_line(s: &Status) -> String {
    format!(
        "{} minutes, {} events held",
        s.minutes_held(),
        s.events_held()
    )
}

/// Follows the name of a device that paired in Logi Bolt mode.
const BOLT: &str = "Bolt";

/// Between the parts of a device title, and before a name's Bolt mark.
pub const TITLE_SEPARATOR: &str = " · ";

/// The name with its Bolt mark, split where the mark starts, the mark tagged `mark` and the rest
/// `plain`.
pub fn name_parts<T: Copy>(name: &str, bolt: bool, plain: T, mark: T) -> Vec<(String, T)> {
    let mut parts = vec![(name.to_string(), plain)];
    if bolt {
        parts.extend([(TITLE_SEPARATOR.into(), plain), (BOLT.into(), mark)]);
    }
    parts
}

pub const KEYBOARD_PAIRING: &str = "a keyboard is pairing";

pub fn pairing_picked(name: Option<&str>) -> String {
    format!("pairing {}", shown_name(name))
}

/// The passkey as entered: six digits, or one click per bit, the most significant first.
pub fn passkey_entry(a: &AwaitedPasskey) -> String {
    passkey_entries(a).join(" ")
}

/// The six digits as one entry, or one entry per click, each but the last with its comma.
pub fn passkey_entries(a: &AwaitedPasskey) -> Vec<String> {
    if !a.clicked {
        return vec![format!("{:06}", a.passkey)];
    }
    (0..diagwire::CLICK_PASSKEY_BITS)
        .rev()
        .map(|bit| {
            let side = if a.passkey >> bit & 1 == 1 {
                "right"
            } else {
                "left"
            };
            if bit == 0 {
                side.to_string()
            } else {
                format!("{side},")
            }
        })
        .collect()
}

/// The device's advertised name, or a word for it where it gave none.
pub fn waiting_device(a: &AwaitedPasskey) -> &str {
    a.name
        .as_deref()
        .unwrap_or(if a.clicked { "device" } else { "keyboard" })
}

pub fn waits_for_passkey(who: &str) -> String {
    format!("{who} waits for the passkey")
}

pub fn pairing_offer(forced: Option<[u8; 3]>) -> String {
    forced.map_or("picks channels by survey".into(), |f| {
        format!("offers channels {} {} {}, set by hand", f[0], f[1], f[2])
    })
}

pub fn paired(own: [u8; 3]) -> String {
    format!("paired {} {} {}", own[0], own[1], own[2])
}

const STATUS_LABEL_WIDTH: usize = 10;

pub fn status_label(label: &str) -> String {
    format!("{:<STATUS_LABEL_WIDTH$}", format!("{label} "))
}

pub fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

pub fn unifying_kind(kind: u8) -> Option<&'static str> {
    Some(match kind {
        0x01 => "keyboard",
        0x02 => "mouse",
        0x03 => "numpad",
        0x04 => "presenter",
        0x07 => "remote",
        0x08 => "trackball",
        0x09 => "touchpad",
        0x0a => "tablet",
        0x0b => "gamepad",
        0x0c => "joystick",
        _ => return None,
    })
}

/// The rating the protocol sets for every device it carries; Bluetooth rates each device by how
/// it paired instead.
pub fn protocol_security(protocol: Protocol) -> Option<&'static str> {
    match protocol {
        Protocol::Nuphy => Some("no protection"),
        Protocol::Unifying => Some("weak at any time"),
        Protocol::Bluetooth => None,
    }
}

pub fn pairing(p: Pairing) -> String {
    let method = if p.secure { "secure" } else { "legacy" };
    let passkey = if p.passkey { "passkey" } else { "no passkey" };
    format!("{method}, {passkey}")
}

fn decimal(whole: u32, thousandths: u32) -> String {
    let rest = format!("{thousandths:03}");
    match rest.trim_end_matches('0') {
        "" => whole.to_string(),
        r => format!("{whole}.{r}"),
    }
}

pub fn interval_ms(units: u16) -> String {
    let us = u32::from(units) * 1250;
    decimal(us / 1000, us % 1000)
}

fn timeout_text(units: u16) -> String {
    let ms = u32::from(units) * 10;
    if ms < 1000 {
        format!("{ms} ms")
    } else {
        format!("{} s", decimal(ms / 1000, ms % 1000))
    }
}

fn phy_name(phy: Phy) -> &'static str {
    match phy {
        Phy::Le1M => "1M",
        Phy::Le2M => "2M",
        Phy::Coded => "Coded",
    }
}

fn timing(interval: String, latency: u16, timeout: u16) -> String {
    format!(
        "{interval} ms, latency {latency}, timeout {}",
        timeout_text(timeout)
    )
}

pub fn phy_text(p: &LinkParams) -> String {
    if p.tx_phy == p.rx_phy {
        phy_name(p.tx_phy).to_string()
    } else {
        format!("{}/{}", phy_name(p.tx_phy), phy_name(p.rx_phy))
    }
}

pub fn params_text(p: &LinkParams) -> String {
    format!(
        "{}, {}",
        timing(interval_ms(p.interval), p.latency, p.timeout),
        phy_text(p)
    )
}

pub fn asked_text(a: Option<&AskedParams>) -> String {
    let Some(a) = a else {
        return "nothing".into();
    };
    let interval = if a.interval_min == a.interval_max {
        interval_ms(a.interval_min)
    } else {
        format!(
            "{}–{}",
            interval_ms(a.interval_min),
            interval_ms(a.interval_max)
        )
    };
    timing(interval, a.latency, a.timeout)
}

/// In milliseconds to a tenth, rounded half up.
pub fn latency(l: Option<Latency>) -> String {
    let Some(l) = l else {
        return "no report yet".into();
    };
    let tenths = |us: u32| {
        let t = (u64::from(us) + 50) / 100;
        format!("{}.{} ms", t / 10, t % 10)
    };
    format!("{} average, {} max", tenths(l.average_us), tenths(l.max_us))
}
