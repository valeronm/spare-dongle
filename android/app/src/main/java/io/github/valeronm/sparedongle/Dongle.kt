package io.github.valeronm.sparedongle

import android.hardware.usb.UsbConstants
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbInterface
import android.hardware.usb.UsbManager

/** Holds a native handle until [take] hands it over to be freed. */
internal class Handle(private var value: Long) {
    /** Throws [IllegalStateException] once the handle is taken. */
    fun get(): Long = value.takeIf { it != 0L } ?: throw IllegalStateException("the dongle is closed")

    /** Returns null once the handle is taken. */
    fun take(): Long? = value.takeIf { it != 0L }?.also { value = 0 }
}

/** A call after [close] throws [IllegalStateException]; a second [close] does nothing. */
class Dongle private constructor(
    private val connection: UsbDeviceConnection,
    private val intf: UsbInterface,
    handle: Long,
) : AutoCloseable {
    private val handle = Handle(handle)

    fun readingJson(): String = Native.readingJson(handle.get())

    fun reportText(): String = Native.reportText(handle.get())

    fun switchProtocol(target: ProtocolCode): Boolean = Native.switchProtocol(handle.get(), target.code)

    override fun close() {
        Native.close(handle.take() ?: return)
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
            val candidates = (0 until device.interfaceCount).map(device::getInterface).mapNotNull { i ->
                if (i.interfaceClass != UsbConstants.USB_CLASS_HID) return@mapNotNull null
                val inEndpoint = endpoint(i, UsbConstants.USB_DIR_IN) ?: return@mapNotNull null
                val outEndpoint = endpoint(i, UsbConstants.USB_DIR_OUT) ?: return@mapNotNull null
                Candidate(i, inEndpoint.address, outEndpoint.address)
            }
            if (candidates.isEmpty()) return null
            val connection = manager.openDevice(device) ?: return null
            try {
                val descriptors = connection.rawDescriptors
                for ((intf, inAddress, outAddress) in candidates) {
                    if (!connection.claimInterface(intf, true)) continue
                    try {
                        if (isDiagnostic(connection, descriptors, intf)) {
                            val handle = Native.open(connection.fileDescriptor, inAddress, outAddress)
                            return Dongle(connection, intf, handle)
                        }
                    } catch (e: Throwable) {
                        connection.releaseInterface(intf)
                        throw e
                    }
                    connection.releaseInterface(intf)
                }
            } catch (e: Throwable) {
                connection.close()
                throw e
            }
            connection.close()
            return null
        }

        // usbfs refuses an interface-recipient request while a kernel driver holds that interface.
        // The dongle's USB stack allocates its reply at the requested length, from a small buffer pool.
        private fun isDiagnostic(connection: UsbDeviceConnection, descriptors: ByteArray, intf: UsbInterface): Boolean {
            val len = Native.reportDescriptorLength(descriptors, intf.id)
            if (len <= 0) return false
            val buf = ByteArray(len)
            val read = connection.controlTransfer(
                REQUEST_TYPE_INTERFACE_IN, GET_DESCRIPTOR, REPORT_DESCRIPTOR, intf.id, buf, len, TIMEOUT_MS)
            return read > 0 && Native.isDiagnostic(buf.copyOf(read))
        }

        private data class Candidate(val intf: UsbInterface, val inAddress: Int, val outAddress: Int)

        private fun endpoint(i: UsbInterface, dir: Int) = (0 until i.endpointCount).map(i::getEndpoint)
            .firstOrNull { it.type == UsbConstants.USB_ENDPOINT_XFER_INT && it.direction == dir && it.maxPacketSize == 64 }
    }
}
