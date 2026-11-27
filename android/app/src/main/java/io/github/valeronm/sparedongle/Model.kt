package io.github.valeronm.sparedongle

import androidx.compose.runtime.Immutable
import kotlin.enums.enumEntries
import org.json.JSONArray
import org.json.JSONException
import org.json.JSONObject

interface Coded {
    val code: String
}

enum class LinkCode(override val code: String) : Coded {
    RadioFailed("radio-failed"),
    NotPaired("not-paired"),
    Waiting("waiting"),
    Standby("standby"),
    Connected("connected"),
}

enum class Usb(override val code: String) : Coded { Live("live"), Suspended("suspended"), NotConfigured("not-configured") }

enum class ProtocolCode(override val code: String) : Coded { Nuphy("nuphy"), Unifying("unifying"), Bluetooth("bluetooth") }

enum class DeviceKind(override val code: String) : Coded { Keyboard("keyboard"), Mouse("mouse") }

enum class Charge(override val code: String) : Coded {
    Discharging("discharging"),
    Charging("charging"),
    Charged("charged"),
    Fault("fault"),
}

enum class SignalQuality(override val code: String) : Coded { Good("good"), Fair("fair"), Weak("weak") }

enum class CounterKey(override val code: String) : Coded {
    Packets("packets"),
    Invalid("invalid"),
    Dropped("dropped"),
    LinkLosses("link-losses"),
    Gaps("gaps"),
    UsbRetries("usb-retries"),
    Merges("merges"),
    Surveys("surveys"),
}

enum class ResetCause(override val code: String) : Coded {
    Pin("pin"),
    Software("software"),
    Brownout("brownout"),
    PowerOn("power-on"),
    Watchdog("watchdog"),
    Debug("debug"),
    Security("security"),
    LowPowerWake("low-power-wake"),
    CpuLockup("cpu-lockup"),
    Parity("parity"),
    Pll("pll"),
    Clock("clock"),
    Hardware("hardware"),
    User("user"),
    Temperature("temperature"),
    Bootloader("bootloader"),
    Flash("flash"),
}

@Immutable
data class Signal(val dbm: Int, val quality: SignalQuality, val bars: Int)

@Immutable
data class BatteryLevel(val percent: Int, val low: Boolean)

@Immutable
data class DeviceStatus(
    val slot: Int,
    val kind: DeviceKind,
    val name: String?,
    val state: LinkCode,
    val signal: Signal?,
    val batteryLevel: BatteryLevel?,
    val charge: Charge?,
)

@Immutable
data class StatusModel(
    val protocol: ProtocolCode,
    val link: LinkCode,
    val usb: Usb,
    val devices: List<DeviceStatus>,
)

@Immutable
data class Version(val major: Long, val minor: Long, val patch: Long)

enum class SoftDeviceKind(override val code: String) : Coded { Absent("absent"), Unnamed("unnamed"), Named("named") }

sealed interface SoftDevice {
    data object Absent : SoftDevice

    data class Unnamed(val fwid: Int) : SoftDevice

    data class Named(val id: Long, val version: Version?) : SoftDevice
}

@Immutable
data class CounterValue(val key: CounterKey, val value: Long)

@Immutable
data class History(val minutes: Long, val events: Long)

@Immutable
data class DongleModel(
    val firmware: Version,
    val bootloader: Version?,
    val softdevice: SoftDevice,
    val uptimeMs: Long,
    val resetCauses: List<ResetCause>,
    val counters: List<CounterValue>,
    val history: History,
    val protocols: List<ProtocolCode>,
)

enum class Side(override val code: String) : Coded { Dongle("dongle"), App("app") }

sealed interface Reading {
    @Immutable
    data class Snapshot(val status: StatusModel, val dongle: DongleModel) : Reading

    data class Mismatch(val dongle: Int, val app: Int, val older: Side) : Reading
}

/** Throws [JSONException] when a field is missing or holds a value outside its enum. */
fun parseReading(json: String): Reading {
    val reading = JSONObject(json)
    reading.optJSONObject("mismatch")?.let {
        return Reading.Mismatch(it.getInt("dongle"), it.getInt("app"), it.enum("older"))
    }
    val snapshot = reading.getJSONObject("snapshot")
    return Reading.Snapshot(status(snapshot.getJSONObject("status")), dongle(snapshot.getJSONObject("dongle")))
}

private fun status(o: JSONObject) = StatusModel(
    protocol = o.enum("protocol"),
    link = o.enum("link"),
    usb = o.enum("usb"),
    devices = o.getJSONArray("devices").objects().map { d ->
        DeviceStatus(
            slot = d.getInt("slot"),
            kind = d.enum("kind"),
            name = d.stringOrNull("name"),
            state = d.enum("state"),
            signal = d.objectOrNull("signal")?.let {
                Signal(it.getInt("dbm"), it.enum("quality"), it.getInt("bars"))
            },
            batteryLevel = d.objectOrNull("batteryLevel")?.let { BatteryLevel(it.getInt("percent"), it.getBoolean("low")) },
            charge = d.enumOrNull("charge"),
        )
    },
)

private fun dongle(o: JSONObject) = DongleModel(
    firmware = version(o.getJSONObject("firmware")),
    bootloader = o.objectOrNull("bootloader")?.let(::version),
    softdevice = softdevice(o.getJSONObject("softdevice")),
    uptimeMs = o.getLong("uptimeMs"),
    resetCauses = o.getJSONArray("resetCauses").enums("reset cause"),
    counters = o.getJSONArray("counters").objects().map {
        CounterValue(it.enum("key"), it.getLong("value"))
    },
    history = o.getJSONObject("history").let { History(it.getLong("minutes"), it.getLong("events")) },
    protocols = o.getJSONArray("protocols").enums("protocol"),
)

private fun version(o: JSONObject) = Version(o.getLong("major"), o.getLong("minor"), o.getLong("patch"))

private fun softdevice(o: JSONObject): SoftDevice = when (enumOf<SoftDeviceKind>("softdevice kind", o.getString("kind"))) {
    SoftDeviceKind.Absent -> SoftDevice.Absent
    SoftDeviceKind.Unnamed -> SoftDevice.Unnamed(o.getInt("fwid"))
    SoftDeviceKind.Named -> SoftDevice.Named(o.getLong("id"), o.objectOrNull("version")?.let(::version))
}

private inline fun <reified T> JSONObject.enum(key: String): T where T : Enum<T>, T : Coded = enumOf(key, getString(key))

private inline fun <reified T> JSONArray.enums(what: String): List<T> where T : Enum<T>, T : Coded =
    (0 until length()).map { enumOf(what, getString(it)) }

private inline fun <reified T> enumOf(what: String, value: String): T where T : Enum<T>, T : Coded =
    enumEntries<T>().firstOrNull { it.code == value } ?: throw JSONException("unknown $what \"$value\"")

private fun JSONArray.objects(): List<JSONObject> = (0 until length()).map(::getJSONObject)

// `isNull` is also true of a key that is absent.
private fun JSONObject.isExplicitNull(key: String): Boolean {
    if (!has(key)) throw JSONException("missing $key")
    return isNull(key)
}

private fun JSONObject.stringOrNull(key: String): String? = if (isExplicitNull(key)) null else getString(key)

private fun JSONObject.objectOrNull(key: String): JSONObject? = if (isExplicitNull(key)) null else getJSONObject(key)

private inline fun <reified T> JSONObject.enumOrNull(key: String): T? where T : Enum<T>, T : Coded =
    if (isExplicitNull(key)) null else enum(key)
