package io.github.valeronm.sparedongle

sealed interface Notice {
    data class Switched(val protocol: ProtocolCode) : Notice
    data object SwitchFailed : Notice
}

sealed interface Step {
    data object Wait : Step
    data object Retry : Step

    /** [shown] replaces the page once the session stops; [notice] is announced on the Status screen. */
    data class End(val notice: Notice? = null, val shown: MessageKind? = null) : Step
}

/** [deadline] is on the `SystemClock.elapsedRealtime` clock. */
data class PendingSwitch(val target: ProtocolCode, val deadline: Long) {
    /** A restart passes through failed opens and reads, a detach and a new USB prompt. */
    fun onState(state: UiState): Step = when (state) {
        is UiState.Ready ->
            if (state.snapshot.status.protocol == target) Step.End(notice = Notice.Switched(target)) else Step.Wait
        is UiState.Message -> when (state.kind) {
            MessageKind.PlugIn, MessageKind.Waiting -> Step.Wait
            MessageKind.CannotOpen, is MessageKind.ReadFailed -> Step.Retry
            MessageKind.Denied, is MessageKind.Outdated, MessageKind.NotBack -> Step.End()
        }
    }

    /**
     * A dongle still answering at the deadline stayed on a protocol other than [target]; [ui] is an answer only if
     * [readBy] is the session still [open].
     */
    fun <S : Any> onDeadline(ui: UiState?, readBy: S?, open: S?): Step =
        if (open != null && readBy == open && ui is UiState.Ready) {
            Step.End(notice = Notice.SwitchFailed)
        } else {
            Step.End(shown = MessageKind.NotBack)
        }
}

/** Null when [deadline] has passed or the switch was not saved whole. */
fun restoredSwitch(target: ProtocolCode?, deadline: Long?, now: Long): PendingSwitch? =
    if (target != null && deadline != null && deadline > now) PendingSwitch(target, deadline) else null

/** A notice set while [shown] was showing outlives it. */
fun afterShown(current: Notice?, shown: Notice): Notice? = current.takeIf { it != shown }

fun switchTargets(status: StatusModel, dongle: DongleModel): List<ProtocolCode> =
    dongle.protocols.filter { it != status.protocol }
