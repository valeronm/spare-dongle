package io.github.valeronm.sparedongle

import java.io.File
import kotlin.enums.enumEntries
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Test

class CodesTest {
    private inline fun <reified T> codes(): List<String> where T : Enum<T>, T : Coded = enumEntries<T>().map { it.code }

    @Test
    fun everyEnumReadsTheCodesTheNativeLibraryWrites() {
        val shared = JSONObject(File(System.getProperty("sparedongle.codes")!!).readText())
        assertEquals(
            shared.keys().asSequence().associateWith { key ->
                shared.getJSONArray(key).let { codes -> (0 until codes.length()).map(codes::getString) }
            },
            mapOf(
                "protocol" to codes<ProtocolCode>(),
                "deviceKind" to codes<DeviceKind>(),
                "link" to codes<LinkCode>(),
                "usb" to codes<Usb>(),
                "charge" to codes<Charge>(),
                "signalQuality" to codes<SignalQuality>(),
                "counter" to codes<CounterKey>(),
                "resetCause" to codes<ResetCause>(),
                "side" to codes<Side>(),
                "softdeviceKind" to codes<SoftDeviceKind>(),
            ),
        )
    }
}
