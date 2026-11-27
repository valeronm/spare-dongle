package io.github.valeronm.sparedongle

import org.junit.Assert.assertEquals
import org.junit.Test

class SwitchTest {
    private fun snapshot(protocol: ProtocolCode) = Reading.Snapshot(
        StatusModel(protocol, LinkCode.Connected, Usb.Live, emptyList()),
        DongleModel(
            firmware = Version(1, 2, 3),
            bootloader = null,
            softdevice = SoftDevice.Absent,
            uptimeMs = 0,
            resetCauses = emptyList(),
            counters = emptyList(),
            history = History(0, 0),
            protocols = ProtocolCode.entries,
        ),
    )

    private fun ready(protocol: ProtocolCode) = UiState.Ready(snapshot(protocol))

    private val switch = PendingSwitch(ProtocolCode.Unifying, deadline = 15_000)

    @Test
    fun theFirstReadyOnTheTargetEndsTheSwitchAnnouncingIt() {
        assertEquals(Step.End(notice = Notice.Switched(ProtocolCode.Unifying)), switch.onState(ready(ProtocolCode.Unifying)))
    }

    @Test
    fun aReadyOnAnotherProtocolWaits() {
        assertEquals(
            listOf(Step.Wait, Step.Wait),
            listOf(ProtocolCode.Nuphy, ProtocolCode.Bluetooth).map { switch.onState(ready(it)) },
        )
    }

    @Test
    fun aDetachOrAUsbPromptWaits() {
        val kinds = listOf(MessageKind.PlugIn, MessageKind.Waiting)
        assertEquals(kinds.map { Step.Wait }, kinds.map { switch.onState(UiState.Message(it)) })
    }

    @Test
    fun aFailedOpenOrReadRetries() {
        val kinds = listOf(MessageKind.CannotOpen, MessageKind.ReadFailed("timed out"))
        assertEquals(kinds.map { Step.Retry }, kinds.map { switch.onState(UiState.Message(it)) })
    }

    @Test
    fun aDenialOrAVersionMismatchEndsTheSwitchShowingItself() {
        val kinds = listOf(MessageKind.Denied, MessageKind.Outdated(Reading.Mismatch(dongle = 4, app = 3, older = Side.App)))
        assertEquals(kinds.map { Step.End() }, kinds.map { switch.onState(UiState.Message(it)) })
    }

    @Test
    fun theDeadlineWithAStatusInHandLeavesTheDongleUnswitched() {
        assertEquals(Step.End(notice = Notice.SwitchFailed), switch.onDeadline(ready(ProtocolCode.Nuphy), sessionOpen = true))
    }

    @Test
    fun theDeadlineWithAStatusLeftFromAClosedSessionFindsTheDongleNotBack() {
        assertEquals(Step.End(shown = MessageKind.NotBack), switch.onDeadline(ready(ProtocolCode.Nuphy), sessionOpen = false))
    }

    @Test
    fun theDeadlineWithoutAStatusInHandFindsTheDongleNotBack() {
        val states = listOf(UiState.Message(MessageKind.ReadFailed("timed out")), UiState.Message(MessageKind.PlugIn), null)
        assertEquals(
            states.map { Step.End(shown = MessageKind.NotBack) },
            states.map { switch.onDeadline(it, sessionOpen = true) },
        )
    }

    @Test
    fun theDialogListsTheProtocolsTheDongleRunsBesidesTheRunningOne() {
        val shown = snapshot(ProtocolCode.Nuphy)
        assertEquals(
            listOf(ProtocolCode.Unifying, ProtocolCode.Bluetooth),
            switchTargets(shown.status, shown.dongle),
        )
        assertEquals(
            listOf(ProtocolCode.Bluetooth),
            switchTargets(shown.status, shown.dongle.copy(protocols = listOf(ProtocolCode.Nuphy, ProtocolCode.Bluetooth))),
        )
    }

    @Test
    fun whileSwitchingThePageShowsTheSwitchAndWhetherUsbAccessIsAsked() {
        assertEquals(
            listOf(
                Page.Switching(ProtocolCode.Unifying, askingAccess = false),
                Page.Switching(ProtocolCode.Unifying, askingAccess = true),
                Page.Switching(ProtocolCode.Unifying, askingAccess = false),
            ),
            listOf(
                ready(ProtocolCode.Nuphy),
                UiState.Message(MessageKind.Waiting),
                UiState.Message(MessageKind.PlugIn),
            ).map { page(it, Screen.Details, switch) },
        )
    }

    @Test
    fun withoutASwitchThePageFollowsTheState() {
        val ready = ready(ProtocolCode.Nuphy)
        assertEquals(
            listOf(Page.Details(ready.snapshot), Page.Message(MessageKind.Waiting)),
            listOf(ready, UiState.Message(MessageKind.Waiting)).map { page(it, Screen.Details, null) },
        )
    }

    @Test
    fun aShownNoticeClearsOnlyItself() {
        val switched = Notice.Switched(ProtocolCode.Unifying)
        assertEquals(
            listOf(null, Notice.SwitchFailed),
            listOf(switched, Notice.SwitchFailed).map { afterShown(it, shown = switched) },
        )
    }
}
