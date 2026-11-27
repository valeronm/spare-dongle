package io.github.valeronm.sparedongle

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.ContentTransform
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.IntOffset
import io.github.valeronm.sparedongle.ui.DetailsScreen
import io.github.valeronm.sparedongle.ui.MessageScreen
import io.github.valeronm.sparedongle.ui.Motion
import io.github.valeronm.sparedongle.ui.SpareDongleTheme
import io.github.valeronm.sparedongle.ui.StatusScreen
import io.github.valeronm.sparedongle.ui.message
import io.github.valeronm.sparedongle.ui.switchingMessage

class StatusActivity : ComponentActivity() {
    private enum class Permission { ASK, PENDING, DENIED }

    private lateinit var manager: UsbManager
    private var ui by mutableStateOf<UiState?>(null)
    private var screen by mutableStateOf(Screen.Status)
    private var session: Session? = null
    private var permission = Permission.ASK
    private var resumed = false
    private var attachedId: Int? = null
    private var switch by mutableStateOf<PendingSwitch?>(null)
    private var notice by mutableStateOf<Notice?>(null)
    private val timer = Handler(Looper.getMainLooper())
    private val retry = Runnable { connect(::attached) }
    private val timeout = Runnable { switch?.let { perform(it.onDeadline(ui, sessionOpen = session != null)) } }

    private val receiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            when (intent.action) {
                ACTION_PERMISSION -> if (permission == Permission.PENDING) {
                    permission = Permission.DENIED
                    connect()
                }
                UsbManager.ACTION_USB_DEVICE_ATTACHED -> {
                    attached()
                    connect()
                }
                UsbManager.ACTION_USB_DEVICE_DETACHED -> if (findDevice() == null) {
                    stop()
                    show(MessageKind.PlugIn)
                }
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        enableEdgeToEdge()
        super.onCreate(savedInstanceState)
        manager = getSystemService(UsbManager::class.java)
        // A request still pending when the state was saved answers a receiver that no longer exists.
        val savedPermission = savedInstanceState?.getString(KEY_PERMISSION)
        permission = when (val saved = Permission.entries.firstOrNull { it.name == savedPermission }) {
            null, Permission.PENDING -> Permission.ASK
            else -> saved
        }
        attachedId = if (savedInstanceState == null) {
            findDevice()?.deviceId
        } else if (savedInstanceState.containsKey(KEY_ATTACHED_ID)) {
            savedInstanceState.getInt(KEY_ATTACHED_ID)
        } else {
            null
        }
        val savedScreen = savedInstanceState?.getString(KEY_SCREEN)
        screen = Screen.entries.firstOrNull { it.name == savedScreen } ?: Screen.Status
        val savedTarget = savedInstanceState?.getString(KEY_SWITCH_TARGET)
        val target = ProtocolCode.entries.firstOrNull { it.name == savedTarget }
        val deadline = savedInstanceState?.getLong(KEY_SWITCH_DEADLINE)
        if (target != null && deadline != null) startSwitch(PendingSwitch(target, deadline))
        setContent {
            SpareDongleTheme {
                Content(
                    page(ui, screen, switch),
                    notice,
                    ::showScreen,
                    ::askAgain,
                    ::connect,
                    ::shareReport,
                    ::switchProtocol,
                    onNoticeShown = { notice = afterShown(notice, it) },
                )
            }
        }
        val filter = IntentFilter().apply {
            addAction(ACTION_PERMISSION)
            addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED)
            addAction(UsbManager.ACTION_USB_DEVICE_DETACHED)
        }
        // The permission answer can arrive while the permission dialog still pauses the activity.
        registerReceiver(receiver, filter, if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) RECEIVER_NOT_EXPORTED else 0)
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        outState.putString(KEY_PERMISSION, permission.name)
        outState.putString(KEY_SCREEN, screen.name)
        attachedId?.let { outState.putInt(KEY_ATTACHED_ID, it) }
        switch?.let {
            outState.putString(KEY_SWITCH_TARGET, it.target.name)
            outState.putLong(KEY_SWITCH_DEADLINE, it.deadline)
        }
    }

    override fun onDestroy() {
        cancelTimers()
        unregisterReceiver(receiver)
        super.onDestroy()
    }

    override fun onResume() {
        super.onResume()
        resumed = true
        connect()
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        if (intent.action == UsbManager.ACTION_USB_DEVICE_ATTACHED) attached()
        connect()
    }

    override fun onPause() {
        resumed = false
        stop()
        super.onPause()
    }

    private fun findDevice(): UsbDevice? =
        manager.deviceList.values.firstOrNull { it.vendorId == Dongle.VID && it.productId == Dongle.PID }

    // Both the attach intent and the attach broadcast report one plug-in, which a new device id marks.
    private fun attached(): UsbDevice? {
        val device = findDevice() ?: return null
        if (device.deviceId != attachedId) {
            attachedId = device.deviceId
            permission = Permission.ASK
        }
        return device
    }

    private fun connect(find: () -> UsbDevice? = ::findDevice) {
        if (!resumed || session != null) return
        val device = find() ?: return show(MessageKind.PlugIn)
        if (!manager.hasPermission(device)) {
            when (permission) {
                Permission.ASK -> {
                    permission = Permission.PENDING
                    val intent = Intent(ACTION_PERMISSION).setPackage(packageName)
                    manager.requestPermission(device, PendingIntent.getBroadcast(this, 0, intent, PendingIntent.FLAG_IMMUTABLE))
                    show(MessageKind.Waiting)
                }
                Permission.PENDING -> show(MessageKind.Waiting)
                Permission.DENIED -> show(MessageKind.Denied)
            }
            return
        }
        session = Session({ Dongle.open(manager, device) }) { state ->
            if (state is UiState.Message) stop()
            update(state)
        }
    }

    private fun askAgain() {
        permission = Permission.ASK
        connect()
    }

    private fun shareReport(onFailed: () -> Unit) {
        val session = session ?: return onFailed()
        session.readReport { text ->
            if (text == null) return@readReport onFailed()
            val send = Intent(Intent.ACTION_SEND).setType("text/plain").putExtra(Intent.EXTRA_TEXT, text)
            startActivity(Intent.createChooser(send, getString(R.string.share_report)))
        }
    }

    private fun switchProtocol(target: ProtocolCode) {
        session?.switchProtocol(
            target,
            onReboot = { startSwitch(PendingSwitch(target, SystemClock.elapsedRealtime() + SWITCH_WAIT_MS)) },
            onFailed = { notice = Notice.SwitchFailed },
        ) ?: run { notice = Notice.SwitchFailed }
    }

    private fun startSwitch(next: PendingSwitch) {
        switch = next
        timer.postDelayed(timeout, (next.deadline - SystemClock.elapsedRealtime()).coerceAtLeast(0))
    }

    private fun perform(step: Step) {
        when (step) {
            Step.Wait -> Unit
            Step.Retry -> {
                timer.removeCallbacks(retry)
                timer.postDelayed(retry, RETRY_MS)
            }
            is Step.End -> {
                cancelTimers()
                switch = null
                step.notice?.let {
                    screen = Screen.Status
                    notice = it
                }
                step.shown?.let {
                    stop()
                    show(it)
                }
            }
        }
    }

    private fun cancelTimers() {
        timer.removeCallbacks(timeout)
        timer.removeCallbacks(retry)
    }

    private fun stop() {
        session?.close()
        session = null
    }

    private fun show(kind: MessageKind) = update(UiState.Message(kind))

    // The outgoing page of a transition still takes taps.
    private fun showScreen(next: Screen) {
        if (ui is UiState.Ready) screen = next
    }

    private fun update(state: UiState) {
        if (state is UiState.Message) screen = Screen.Status
        ui = state
        switch?.let { perform(it.onState(state)) }
    }

    private companion object {
        const val ACTION_PERMISSION = "io.github.valeronm.sparedongle.USB_PERMISSION"
        const val KEY_PERMISSION = "permission"
        const val KEY_ATTACHED_ID = "attachedId"
        const val KEY_SCREEN = "screen"
        const val KEY_SWITCH_TARGET = "switchTarget"
        const val KEY_SWITCH_DEADLINE = "switchDeadline"

        // The dongle's reboot-to-enumeration time on a phone is unmeasured; this is a guess.
        const val SWITCH_WAIT_MS = 15_000L
        const val RETRY_MS = 300L
    }
}

internal sealed interface Page {
    data object Blank : Page
    data class Message(val kind: MessageKind) : Page
    data class Switching(val target: ProtocolCode, val askingAccess: Boolean) : Page
    data class Status(val snapshot: Reading.Snapshot) : Page
    data class Details(val snapshot: Reading.Snapshot) : Page
}

internal fun page(ui: UiState?, screen: Screen, switch: PendingSwitch?): Page {
    if (switch != null) {
        val askingAccess = ui is UiState.Message && ui.kind == MessageKind.Waiting
        return Page.Switching(switch.target, askingAccess)
    }
    return when (ui) {
        null -> Page.Blank
        is UiState.Message -> Page.Message(ui.kind)
        is UiState.Ready -> when (screen) {
            Screen.Status -> Page.Status(ui.snapshot)
            Screen.Details -> Page.Details(ui.snapshot)
        }
    }
}

private fun sharedAxis(forward: Boolean): ContentTransform {
    val enter = tween<Float>(Motion.MEDIUM4, easing = Motion.EmphasizedDecelerate)
    val slide = tween<IntOffset>(Motion.MEDIUM4, easing = Motion.EmphasizedDecelerate)
    return (slideInHorizontally(slide) { if (forward) it / 10 else -it / 10 } + fadeIn(enter)) togetherWith
        fadeOut(tween(Motion.SHORT4, easing = Motion.EmphasizedAccelerate))
}

private fun fade(): ContentTransform {
    val spec = tween<Float>(Motion.MEDIUM2, easing = Motion.Standard)
    return fadeIn(spec) togetherWith fadeOut(spec)
}

@Composable
private fun Content(
    page: Page,
    notice: Notice?,
    onScreen: (Screen) -> Unit,
    onAskAgain: () -> Unit,
    onTryAgain: () -> Unit,
    onShare: (onFailed: () -> Unit) -> Unit,
    onSwitch: (ProtocolCode) -> Unit,
    onNoticeShown: (Notice) -> Unit,
) {
    BackHandler(enabled = page is Page.Details || page is Page.Switching) {
        if (page is Page.Details) onScreen(Screen.Status)
    }
    AnimatedContent(
        targetState = page,
        contentKey = { it::class },
        transitionSpec = {
            when {
                initialState is Page.Status && targetState is Page.Details -> sharedAxis(forward = true)
                initialState is Page.Details && targetState is Page.Status -> sharedAxis(forward = false)
                else -> fade()
            }
        },
        label = "screen",
    ) { shown ->
        when (shown) {
            Page.Blank -> Box(Modifier.fillMaxSize().background(MaterialTheme.colorScheme.background))
            is Page.Message -> MessageScreen(message(shown.kind), onAskAgain, onTryAgain)
            is Page.Switching -> MessageScreen(switchingMessage(shown.target, shown.askingAccess), onAskAgain, onTryAgain)
            is Page.Status -> StatusScreen(
                shown.snapshot.status,
                shown.snapshot.dongle,
                onDetails = { onScreen(Screen.Details) },
                onSwitch = onSwitch,
                notice = notice,
                onNoticeShown = onNoticeShown,
            )
            is Page.Details -> DetailsScreen(shown.snapshot.dongle, onBack = { onScreen(Screen.Status) }, onShare)
        }
    }
}
