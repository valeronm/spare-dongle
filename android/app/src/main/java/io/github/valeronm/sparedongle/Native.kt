package io.github.valeronm.sparedongle

object Native {
    init { System.loadLibrary("sparedongle") }
    @JvmStatic external fun open(fd: Int, inEndpoint: Int, outEndpoint: Int): Long
    @JvmStatic external fun readingJson(handle: Long): String
    @JvmStatic external fun reportText(handle: Long): String
    /** Returns whether the dongle reboots to run the protocol whose [ProtocolCode.code] is [code]. */
    @JvmStatic external fun switchProtocol(handle: Long, code: String): Boolean
    @JvmStatic external fun close(handle: Long)
    @JvmStatic external fun isDiagnostic(descriptor: ByteArray): Boolean
    @JvmStatic external fun reportDescriptorLength(raw: ByteArray, interfaceNumber: Int): Int
}
