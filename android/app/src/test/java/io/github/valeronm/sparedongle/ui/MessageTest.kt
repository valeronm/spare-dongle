package io.github.valeronm.sparedongle.ui

import io.github.valeronm.sparedongle.MessageKind
import io.github.valeronm.sparedongle.R
import io.github.valeronm.sparedongle.Reading
import io.github.valeronm.sparedongle.Side
import org.junit.Assert.assertEquals
import org.junit.Test

class MessageTest {
    @Test
    fun aMismatchAsksToUpdateTheOlderSide() {
        assertEquals(
            R.string.update_app,
            message(MessageKind.Outdated(Reading.Mismatch(dongle = 4, app = 3, older = Side.App))).headline,
        )
        assertEquals(
            R.string.update_firmware,
            message(MessageKind.Outdated(Reading.Mismatch(dongle = 3, app = 4, older = Side.Dongle))).headline,
        )
    }
}
