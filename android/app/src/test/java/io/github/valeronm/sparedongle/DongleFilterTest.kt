package io.github.valeronm.sparedongle

import java.io.File
import javax.xml.parsers.DocumentBuilderFactory
import org.junit.Assert.assertEquals
import org.junit.Test

class DongleFilterTest {
    @Test
    fun theAttachFilterMatchesTheDongleTheAppOpens() {
        val filter = DocumentBuilderFactory.newInstance().newDocumentBuilder()
            .parse(File(System.getProperty("sparedongle.filter")!!))
            .getElementsByTagName("usb-device").item(0).attributes
        assertEquals(
            Dongle.VID to Dongle.PID,
            filter.getNamedItem("vendor-id").nodeValue.toInt() to filter.getNamedItem("product-id").nodeValue.toInt(),
        )
    }
}
