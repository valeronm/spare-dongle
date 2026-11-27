package io.github.valeronm.sparedongle.ui

import io.github.valeronm.sparedongle.MessageKind
import io.github.valeronm.sparedongle.ProtocolCode
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

    @Test
    fun aSwitchAsksForUsbAccessOnlyWhileAccessIsAsked() {
        assertEquals(
            listOf(Detail.Hint(R.string.switching_hint), Detail.Hint(R.string.allow_access)),
            listOf(false, true).map { switchingMessage(ProtocolCode.Unifying, it).detail },
        )
    }

    @Test
    fun aSwitchNamesItsTarget() {
        val message = switchingMessage(ProtocolCode.Bluetooth, false)
        assertEquals(R.string.switching_to to ProtocolCode.Bluetooth, message.headline to message.headlineArg)
    }

    @Test
    fun aDongleThatDidNotComeBackOffersTryAgain() {
        assertEquals(Retry.TryAgain, message(MessageKind.NotBack).retry)
    }
}
