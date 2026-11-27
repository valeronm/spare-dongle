package io.github.valeronm.sparedongle

import android.app.Activity
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import android.os.Build
import android.os.Bundle
import android.view.View
import android.view.WindowInsets
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TableLayout
import android.widget.TableRow
import android.widget.TextView
import java.util.concurrent.Executors
import java.util.concurrent.ScheduledExecutorService
import java.util.concurrent.TimeUnit
import org.json.JSONObject

class StatusActivity : Activity() {
    private enum class Permission { ASK, PENDING, GRANTED, DENIED }

    private lateinit var manager: UsbManager
    private lateinit var message: TextView
    private lateinit var table: TableLayout
    private var session: Session? = null
    private var permission = Permission.ASK
    private var resumed = false
    private var attachedId: Int? = null

    private val receiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            when (intent.action) {
                ACTION_PERMISSION -> if (permission == Permission.PENDING) {
                    val device = findDevice()
                    val granted = device != null && manager.hasPermission(device)
                    permission = if (granted) Permission.GRANTED else Permission.DENIED
                    connect()
                }
                UsbManager.ACTION_USB_DEVICE_ATTACHED -> {
                    attached()
                    connect()
                }
                UsbManager.ACTION_USB_DEVICE_DETACHED -> if (findDevice() == null) {
                    stop()
                    showMessage(getString(R.string.plug_in))
                }
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        manager = getSystemService(UsbManager::class.java)
        // A request still pending when the state was saved answers a receiver that no longer exists.
        permission = when (val saved = savedInstanceState?.getString(KEY_PERMISSION)?.let(Permission::valueOf)) {
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
        message = TextView(this)
        table = TableLayout(this).apply { setColumnShrinkable(1, true) }
        val column = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            val pad = (16 * resources.displayMetrics.density).toInt()
            setPadding(pad, pad, pad, pad)
            addView(message)
            addView(table)
        }
        val content = ScrollView(this).apply { addView(column) }
        // From API 35 the window is edge-to-edge, and the system bar insets the content receives include the action bar.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.VANILLA_ICE_CREAM) {
            content.setOnApplyWindowInsetsListener { view, insets ->
                val bars = insets.getInsets(WindowInsets.Type.systemBars())
                view.setPadding(bars.left, bars.top, bars.right, bars.bottom)
                insets
            }
        }
        setContentView(content)
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
        attachedId?.let { outState.putInt(KEY_ATTACHED_ID, it) }
    }

    override fun onDestroy() {
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
    private fun attached() {
        val id = findDevice()?.deviceId ?: return
        if (id == attachedId) return
        attachedId = id
        permission = Permission.ASK
    }

    private fun connect() {
        if (!resumed || session != null) return
        val device = findDevice() ?: return showMessage(getString(R.string.plug_in))
        if (!manager.hasPermission(device)) {
            when (permission) {
                Permission.ASK -> {
                    permission = Permission.PENDING
                    val intent = Intent(ACTION_PERMISSION).setPackage(packageName)
                    manager.requestPermission(device, PendingIntent.getBroadcast(this, 0, intent, PendingIntent.FLAG_IMMUTABLE))
                    showMessage(getString(R.string.waiting))
                }
                Permission.PENDING -> showMessage(getString(R.string.waiting))
                Permission.GRANTED, Permission.DENIED -> showMessage(getString(R.string.access_denied))
            }
            return
        }
        val dongle = Dongle.open(manager, device) ?: return showMessage(getString(R.string.cannot_open))
        session = Session(dongle)
    }

    private fun stop() {
        session?.close()
        session = null
    }

    private fun showMessage(text: String) {
        table.removeAllViews()
        table.visibility = View.GONE
        message.text = text
        message.visibility = View.VISIBLE
    }

    private fun showRows(rows: List<Pair<String, String>>) {
        message.visibility = View.GONE
        table.visibility = View.VISIBLE
        table.removeAllViews()
        val gap = (16 * resources.displayMetrics.density).toInt()
        for ((label, value) in rows) {
            table.addView(TableRow(this).apply {
                addView(TextView(this@StatusActivity).apply { text = label; setPadding(0, 0, gap, 0) })
                addView(TextView(this@StatusActivity).apply { text = value })
            })
        }
    }

    private fun statusRows(dongle: JSONObject, status: JSONObject): List<Pair<String, String>> {
        val details = dongle.getJSONArray("rows")
        val firmware = (0 until details.length()).map(details::getJSONObject).first { it.getString("label") == "firmware" }
        val rows = mutableListOf(
            "firmware" to firmware.getString("value"),
            "protocol" to status.getString("protocol"),
            "link" to status.getJSONObject("link").getString("text"),
            "usb" to status.getString("usb"),
        )
        val devices = status.getJSONArray("devices")
        for (i in 0 until devices.length()) {
            val d = devices.getJSONObject(i)
            val state = d.getJSONObject("state").getString("text")
            val name = d.optString("name").takeUnless { d.isNull("name") }
            rows += d.getString("kind") to if (name != null) "$name, $state" else state
            if (!d.isNull("signalText")) rows += "signal" to d.getString("signalText")
            if (!d.isNull("batteryText")) rows += "battery" to d.getString("batteryText")
        }
        return rows
    }

    /** Owns a [Dongle] whose handle only its executor's thread touches. */
    private inner class Session(private val dongle: Dongle) {
        private val executor: ScheduledExecutorService = Executors.newSingleThreadScheduledExecutor()
        private var closed = false

        init {
            executor.scheduleWithFixedDelay(::poll, 0, 1, TimeUnit.SECONDS)
        }

        private fun poll() {
            if (closed) return
            try {
                val snapshot = JSONObject(dongle.snapshotJson())
                val rows = statusRows(snapshot.getJSONObject("dongle"), snapshot.getJSONObject("status"))
                runOnUiThread { if (session === this) showRows(rows) }
            } catch (e: Throwable) {
                closeDongle()
                runOnUiThread {
                    if (session === this) {
                        stop()
                        showMessage(e.message ?: e.javaClass.simpleName)
                    }
                }
            }
        }

        private fun closeDongle() {
            if (closed) return
            closed = true
            dongle.close()
        }

        fun close() {
            executor.execute(::closeDongle)
            executor.shutdown()
        }
    }

    private companion object {
        const val ACTION_PERMISSION = "io.github.valeronm.sparedongle.USB_PERMISSION"
        const val KEY_PERMISSION = "permission"
        const val KEY_ATTACHED_ID = "attachedId"
    }
}
