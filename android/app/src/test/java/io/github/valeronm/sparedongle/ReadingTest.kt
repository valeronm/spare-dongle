package io.github.valeronm.sparedongle

import org.json.JSONException
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Test

class ReadingTest {
    private val status = """
        {
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
                    "charge": null
                },
                {
                    "slot": 1,
                    "kind": "mouse",
                    "name": "MX Master 3S",
                    "state": "connected",
                    "signal": {"dbm": -60, "quality": "good", "bars": 4},
                    "batteryLevel": {"percent": 80, "low": false},
                    "charge": "charging"
                }
            ]
        }
    """

    private val dongle = """
        {
            "firmware": {"major": 1, "minor": 2, "patch": 3},
            "bootloader": {"major": 0, "minor": 6, "patch": 0},
            "softdevice": {"kind": "named", "id": 140, "version": {"major": 6, "minor": 1, "patch": 1}},
            "uptimeMs": 4328719365,
            "resetCauses": ["pin", "watchdog"],
            "counters": [
                {"key": "dropped", "value": 2},
                {"key": "usb-retries", "value": 5},
                {"key": "merges", "value": 6}
            ],
            "history": {"minutes": 1440, "events": 512},
            "protocols": ["nuphy", "unifying", "bluetooth"]
        }
    """

    private fun snapshot(edit: JSONObject.() -> Unit = {}): String =
        JSONObject().put("snapshot", JSONObject().put("status", JSONObject(status)).put("dongle", JSONObject(dongle)))
            .apply(edit).toString()

    private val JSONObject.snapshotStatus get() = getJSONObject("snapshot").getJSONObject("status")
    private val JSONObject.mouse get() = snapshotStatus.getJSONArray("devices").getJSONObject(1)

    @Test
    fun aSnapshotParsesIntoItsModel() {
        assertEquals(
            Reading.Snapshot(
                StatusModel(
                    protocol = ProtocolCode.Bluetooth,
                    link = LinkCode.Connected,
                    usb = Usb.Live,
                    devices = listOf(
                        DeviceStatus(0, DeviceKind.Keyboard, "MX Keys", LinkCode.Waiting, null, null, null),
                        DeviceStatus(
                            1,
                            DeviceKind.Mouse,
                            "MX Master 3S",
                            LinkCode.Connected,
                            Signal(-60, SignalQuality.Good, 4),
                            BatteryLevel(80, false),
                            Charge.Charging,
                        ),
                    ),
                ),
                DongleModel(
                    firmware = Version(1, 2, 3),
                    bootloader = Version(0, 6, 0),
                    softdevice = SoftDevice.Named(140, Version(6, 1, 1)),
                    uptimeMs = 4_328_719_365,
                    resetCauses = listOf(ResetCause.Pin, ResetCause.Watchdog),
                    counters = listOf(
                        CounterValue(CounterKey.Dropped, 2),
                        CounterValue(CounterKey.UsbRetries, 5),
                        CounterValue(CounterKey.Merges, 6),
                    ),
                    history = History(1440, 512),
                    protocols = listOf(ProtocolCode.Nuphy, ProtocolCode.Unifying, ProtocolCode.Bluetooth),
                ),
            ),
            parseReading(snapshot()),
        )
    }

    @Test
    fun aMissingKeyFails() {
        assertThrows(JSONException::class.java) { parseReading(snapshot { mouse.remove("charge") }) }
    }

    @Test
    fun anExplicitNullReadsAsNull() {
        val reading = parseReading(snapshot { mouse.put("charge", JSONObject.NULL) }) as Reading.Snapshot
        assertNull(reading.status.devices[1].charge)
    }

    @Test
    fun anUnknownEnumValueFails() {
        assertThrows(JSONException::class.java) { parseReading(snapshot { snapshotStatus.put("link", "asleep") }) }
    }

    @Test
    fun aVersionMismatchParsesIntoBothVersionsAndTheOlderSide() {
        assertEquals(
            Reading.Mismatch(dongle = 3, app = 4, older = Side.Dongle),
            parseReading("""{"mismatch": {"dongle": 3, "app": 4, "older": "dongle"}}"""),
        )
    }
}
