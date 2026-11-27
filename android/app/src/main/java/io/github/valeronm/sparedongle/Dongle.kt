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

        /** The diagnostic interface is the only HID interface with an OUT endpoint. */
        fun open(manager: UsbManager, device: UsbDevice): Dongle? {
            val intf = (0 until device.interfaceCount).map(device::getInterface).firstOrNull { i ->
                i.interfaceClass == UsbConstants.USB_CLASS_HID && endpoint(i, UsbConstants.USB_DIR_IN) != null &&
                    endpoint(i, UsbConstants.USB_DIR_OUT) != null
            } ?: return null
            val connection = manager.openDevice(device) ?: return null
            if (!connection.claimInterface(intf, true)) { connection.close(); return null }
            val handle = Native.open(connection.fileDescriptor,
                endpoint(intf, UsbConstants.USB_DIR_IN)!!.address,
                endpoint(intf, UsbConstants.USB_DIR_OUT)!!.address)
            return Dongle(connection, intf, handle)
        }

        private fun endpoint(i: UsbInterface, dir: Int) = (0 until i.endpointCount).map(i::getEndpoint)
            .firstOrNull { it.type == UsbConstants.USB_ENDPOINT_XFER_INT && it.direction == dir && it.maxPacketSize == 64 }
    }
}
