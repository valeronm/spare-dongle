package io.github.valeronm.sparedongle.ui

import java.io.File
import java.util.Locale
import javax.xml.parsers.DocumentBuilderFactory
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Test

class FormatsTest {
    private val strings: Map<String, String> by lazy {
        val nodes = DocumentBuilderFactory.newInstance().newDocumentBuilder()
            .parse(File(System.getProperty("sparedongle.strings")!!))
            .getElementsByTagName("string")
        (0 until nodes.length).map(nodes::item).associate { it.attributes.getNamedItem("name").nodeValue to it.textContent }
    }
    private lateinit var locale: Locale

    @Before
    fun setUp() {
        locale = Locale.getDefault()
        Locale.setDefault(Locale.forLanguageTag("ar-EG"))
    }

    @After
    fun tearDown() {
        Locale.setDefault(locale)
    }

    private fun uptime(ms: Long) = formatUptime(ms, strings.getValue("uptime_days"), strings.getValue("uptime"))

    @Test
    fun uptimeShowsDaysOnlyFromTheFirstDayAndPadsTheClock() {
        assertEquals(
            listOf("00:00:59", "23:59:59", "1d 00:00:00", "50d 02:25:19"),
            listOf(59_999L, 86_399_999, 86_400_000, 4_328_719_365).map(::uptime),
        )
    }

    @Test
    fun identifiersKeepLatinDigitsInAnyLocale() {
        assertEquals("S140 6.1.1", latinFormat(strings.getValue("softdevice_named_version"), 140L, "6.1.1"))
        assertEquals("1.2.3", latinFormat(strings.getValue("version"), 1L, 2L, 3L))
        assertEquals("FWID 0x00b6", latinFormat(strings.getValue("softdevice_fwid"), 0xb6))
    }

    @Test
    fun theBatteryIconRoundsThePercentToTheNearestBar() {
        assertEquals(listOf(0, 0, 4, 7, 7), listOf(0, 7, 50, 100, 120).map { batteryBarIndex(it, 8) })
    }
}
