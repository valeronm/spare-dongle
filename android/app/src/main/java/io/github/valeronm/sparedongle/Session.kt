package io.github.valeronm.sparedongle

import android.os.Handler
import android.os.Looper
import java.util.concurrent.CancellationException
import java.util.concurrent.Executors
import java.util.concurrent.ScheduledExecutorService
import java.util.concurrent.ScheduledFuture
import java.util.concurrent.TimeUnit

sealed interface UiState {
    data class Message(val kind: MessageKind) : UiState
    data class Ready(val snapshot: Reading.Snapshot) : UiState
}

sealed interface MessageKind {
    data object PlugIn : MessageKind
    data object Waiting : MessageKind
    data object Denied : MessageKind
    data object CannotOpen : MessageKind
    data class ReadFailed(val reason: String) : MessageKind
    data class Outdated(val mismatch: Reading.Mismatch) : MessageKind
    data object NotBack : MessageKind
}

enum class Screen { Status, Details }

/**
 * Opens a [Dongle] with [openDongle] on a thread every Session shares, the only thread that touches its handle, then
 * polls it once a second.
 *
 * [onState] runs on the main thread with each poll's [UiState.Ready], or once with a [UiState.Message] of
 * [MessageKind.CannotOpen], [MessageKind.ReadFailed] or [MessageKind.Outdated] after which polling stops; it never
 * runs after [close]. Call [readReport], [switchProtocol] and [close] on the main thread.
 */
class Session(
    private val openDongle: () -> Dongle?,
    private val onState: (UiState) -> Unit,
) : AutoCloseable {
    private val main = Handler(Looper.getMainLooper())
    private var open = true
    private val pending = mutableSetOf<Request>()
    private var dongle: Dongle? = null
    private val polling: ScheduledFuture<*> = executor.scheduleWithFixedDelay(::poll, 0, 1, TimeUnit.SECONDS)

    private fun poll() {
        val state = read()
        main.post { if (open) onState(state) }
        // A periodic task that throws is not run again.
        if (state is UiState.Message) throw CancellationException()
    }

    private fun read(): UiState = try {
        val dongle = dongle ?: connect() ?: return UiState.Message(MessageKind.CannotOpen)
        when (val reading = parseReading(dongle.readingJson())) {
            is Reading.Snapshot -> UiState.Ready(reading)
            is Reading.Mismatch -> UiState.Message(MessageKind.Outdated(reading))
        }
    } catch (e: Throwable) {
        UiState.Message(MessageKind.ReadFailed(e.message ?: e.javaClass.simpleName))
    }

    private fun connect(): Dongle? = try {
        openDongle()
    } catch (e: LinkageError) {
        // A LinkageError comes from an APK without the native library, not from the dongle.
        throw e
    } catch (_: Throwable) {
        null
    }?.also { dongle = it }

    /**
     * [onText] runs on the main thread with the report, or with null when it can't be read; never after [close].
     * A call while an earlier read is pending is ignored.
     */
    fun readReport(onText: (String?) -> Unit) = request(Request.Report, Dongle::reportText, onText)

    /**
     * [onReboot] runs on the main thread when the dongle reboots to run [target], and [onFailed] when the switch
     * fails; neither runs when the dongle already runs [target], or after [close]. A call while an earlier switch is
     * pending is ignored.
     */
    fun switchProtocol(target: ProtocolCode, onReboot: () -> Unit, onFailed: () -> Unit) =
        request(Request.Switch, { it.switchProtocol(target) }) { rebooting ->
            when (rebooting) {
                null -> onFailed()
                true -> onReboot()
                false -> Unit
            }
        }

    private fun <T : Any> request(kind: Request, call: (Dongle) -> T, onResult: (T?) -> Unit) {
        if (!open || !pending.add(kind)) return
        executor.execute {
            val result = try {
                dongle?.let(call)
            } catch (_: Throwable) {
                null
            }
            main.post {
                pending.remove(kind)
                if (open) onResult(result)
            }
        }
    }

    override fun close() {
        if (!open) return
        open = false
        polling.cancel(false)
        executor.execute { dongle?.close() }
    }

    private enum class Request { Report, Switch }

    private companion object {
        // Claiming an interface another connection holds takes its claim away, so a session's open has to queue behind
        // the previous session's close.
        val executor: ScheduledExecutorService = Executors.newSingleThreadScheduledExecutor()
    }
}
