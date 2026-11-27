package io.github.valeronm.sparedongle.ui

import androidx.annotation.PluralsRes
import androidx.annotation.StringRes
import androidx.compose.runtime.Composable
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import io.github.valeronm.sparedongle.Charge
import io.github.valeronm.sparedongle.CounterKey
import io.github.valeronm.sparedongle.DeviceKind
import io.github.valeronm.sparedongle.History
import io.github.valeronm.sparedongle.LinkCode
import io.github.valeronm.sparedongle.ProtocolCode
import io.github.valeronm.sparedongle.R
import io.github.valeronm.sparedongle.ResetCause
import io.github.valeronm.sparedongle.Signal
import io.github.valeronm.sparedongle.SignalQuality
import io.github.valeronm.sparedongle.SoftDevice
import io.github.valeronm.sparedongle.Usb
import io.github.valeronm.sparedongle.Version

@Composable
fun stateName(code: LinkCode): String = stringResource(
    when (code) {
        LinkCode.Connected -> R.string.state_connected
        LinkCode.Standby -> R.string.state_standby
        LinkCode.Waiting -> R.string.state_waiting
        LinkCode.NotPaired -> R.string.state_not_paired
        LinkCode.RadioFailed -> R.string.state_radio_failed
    },
)

@Composable
fun usbName(usb: Usb): String = stringResource(
    when (usb) {
        Usb.Live -> R.string.usb_live
        Usb.Suspended -> R.string.usb_suspended
        Usb.NotConfigured -> R.string.usb_not_configured
    },
)

@Composable
fun protocolName(code: ProtocolCode): String = stringResource(
    when (code) {
        ProtocolCode.Nuphy -> R.string.protocol_nuphy
        ProtocolCode.Unifying -> R.string.protocol_unifying
        ProtocolCode.Bluetooth -> R.string.protocol_bluetooth
    },
)

@Composable
fun kindName(kind: DeviceKind): String = stringResource(
    when (kind) {
        DeviceKind.Keyboard -> R.string.keyboard
        DeviceKind.Mouse -> R.string.mouse
    },
)

@Composable
fun qualityName(quality: SignalQuality): String = stringResource(
    when (quality) {
        SignalQuality.Good -> R.string.signal_good
        SignalQuality.Fair -> R.string.signal_fair
        SignalQuality.Weak -> R.string.signal_weak
    },
)

@Composable
fun dbmText(signal: Signal): String = stringResource(R.string.signal_dbm, signal.dbm)

@Composable
fun percentText(value: Int): String = stringResource(R.string.percent, value)

@Composable
fun chargeSpoken(charge: Charge): String = stringResource(
    when (charge) {
        Charge.Discharging -> R.string.charge_discharging_spoken
        Charge.Charging -> R.string.charge_charging_spoken
        Charge.Charged -> R.string.charge_charged_spoken
        Charge.Fault -> R.string.charge_fault_spoken
    },
)

@Composable
fun counterName(key: CounterKey): String = stringResource(
    when (key) {
        CounterKey.Packets -> R.string.counter_packets
        CounterKey.Invalid -> R.string.counter_invalid
        CounterKey.Dropped -> R.string.counter_dropped
        CounterKey.LinkLosses -> R.string.counter_link_losses
        CounterKey.Gaps -> R.string.counter_gaps
        CounterKey.UsbRetries -> R.string.counter_usb_retries
        CounterKey.Merges -> R.string.counter_merges
        CounterKey.Surveys -> R.string.counter_surveys
    },
)

@Composable
private fun resetCauseName(cause: ResetCause): String = stringResource(
    when (cause) {
        ResetCause.Pin -> R.string.reset_pin
        ResetCause.Software -> R.string.reset_software
        ResetCause.Brownout -> R.string.reset_brownout
        ResetCause.PowerOn -> R.string.reset_power_on
        ResetCause.Watchdog -> R.string.reset_watchdog
        ResetCause.Debug -> R.string.reset_debug
        ResetCause.Security -> R.string.reset_security
        ResetCause.LowPowerWake -> R.string.reset_low_power_wake
        ResetCause.CpuLockup -> R.string.reset_cpu_lockup
        ResetCause.Parity -> R.string.reset_parity
        ResetCause.Pll -> R.string.reset_pll
        ResetCause.Clock -> R.string.reset_clock
        ResetCause.Hardware -> R.string.reset_hardware
        ResetCause.User -> R.string.reset_user
        ResetCause.Temperature -> R.string.reset_temperature
        ResetCause.Bootloader -> R.string.reset_bootloader
        ResetCause.Flash -> R.string.reset_flash
    },
)

@Composable
fun resetCausesText(causes: List<ResetCause>): String =
    if (causes.isEmpty()) stringResource(R.string.reset_none) else causes.map { resetCauseName(it) }.joinToString(", ")

@Composable
fun countText(value: Long): String = stringResource(R.string.count, value)

@Composable
fun versionText(v: Version): String = latinString(R.string.version, v.major, v.minor, v.patch)

@Composable
fun bootloaderText(v: Version?): String = v?.let { versionText(it) } ?: stringResource(R.string.bootloader_unknown)

@Composable
fun softdeviceText(d: SoftDevice): String = when (d) {
    SoftDevice.Absent -> stringResource(R.string.softdevice_none)
    is SoftDevice.Unnamed -> latinString(R.string.softdevice_fwid, d.fwid)
    is SoftDevice.Named -> when (val version = d.version) {
        null -> latinString(R.string.softdevice_named, d.id)
        else -> latinString(R.string.softdevice_named_version, d.id, versionText(version))
    }
}

@Composable
fun uptimeText(ms: Long): String =
    formatUptime(ms, stringResource(R.string.uptime_days), stringResource(R.string.uptime))

@Composable
fun latinString(@StringRes id: Int, vararg args: Any): String = latinFormat(stringResource(id), *args)

@Composable
fun historyText(history: History): String = stringResource(
    R.string.history,
    quantity(R.plurals.history_minutes, history.minutes),
    quantity(R.plurals.history_events, history.events),
)

@Composable
private fun quantity(@PluralsRes id: Int, count: Long): String =
    pluralStringResource(id, count.coerceAtMost(Int.MAX_VALUE.toLong()).toInt(), count)
