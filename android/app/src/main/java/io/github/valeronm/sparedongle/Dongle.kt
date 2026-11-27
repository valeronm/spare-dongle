package io.github.valeronm.sparedongle

import android.hardware.usb.UsbConstants
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbInterface
import android.hardware.usb.UsbManager

class Dongle private constructor(
    private val connection: UsbDeviceConnection,
    private val intf: UsbInterface,
    private val handle: Long,
) : AutoCloseable {
    fun rows(): List<Pair<String, String>> =
        Native.statusRows(handle).toList().chunked(2) { (l, v) -> l to v }

    override fun close() {
        Native.close(handle)
        connection.releaseInterface(intf)
        connection.close()
    }

    companion object {
        const val VID = 0x2fe3
        const val PID = 0x4e44
        private const val REQUEST_TYPE_INTERFACE_IN = 0x81
        private const val GET_DESCRIPTOR = 0x06
        private const val REPORT_DESCRIPTOR = 0x2200
        private const val TIMEOUT_MS = 1000

        /**
         * The diagnostic interface is the one whose report descriptor sets the diagnostic usage page, checked on the
         * HID interfaces with 64-byte interrupt IN and OUT endpoints.
         */
        fun open(manager: UsbManager, device: UsbDevice): Dongle? {
            val candidates = (0 until device.interfaceCount).map(device::getInterface).filter { i ->
                i.interfaceClass == UsbConstants.USB_CLASS_HID && endpoint(i, UsbConstants.USB_DIR_IN) != null &&
                    endpoint(i, UsbConstants.USB_DIR_OUT) != null
            }
            if (candidates.isEmpty()) return null
            val connection = manager.openDevice(device) ?: return null
            for (intf in candidates) {
                if (!connection.claimInterface(intf, true)) continue
                if (isDiagnostic(connection, intf)) {
                    val handle = Native.open(connection.fileDescriptor,
                        endpoint(intf, UsbConstants.USB_DIR_IN)!!.address,
                        endpoint(intf, UsbConstants.USB_DIR_OUT)!!.address)
                    return Dongle(connection, intf, handle)
                }
                connection.releaseInterface(intf)
            }
            connection.close()
            return null
        }

        // usbfs refuses an interface-recipient request while a kernel driver holds that interface.
        // The dongle's USB stack allocates its reply at the requested length, from a small buffer pool.
        private fun isDiagnostic(connection: UsbDeviceConnection, intf: UsbInterface): Boolean {
            val len = Native.reportDescriptorLength(connection.rawDescriptors, intf.id)
            if (len <= 0) return false
            val buf = ByteArray(len)
            val read = connection.controlTransfer(
                REQUEST_TYPE_INTERFACE_IN, GET_DESCRIPTOR, REPORT_DESCRIPTOR, intf.id, buf, len, TIMEOUT_MS)
            return read > 0 && Native.isDiagnostic(buf.copyOf(read))
        }

        private fun endpoint(i: UsbInterface, dir: Int) = (0 until i.endpointCount).map(i::getEndpoint)
            .firstOrNull { it.type == UsbConstants.USB_ENDPOINT_XFER_INT && it.direction == dir && it.maxPacketSize == 64 }
    }
}
