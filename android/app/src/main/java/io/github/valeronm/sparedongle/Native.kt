package io.github.valeronm.sparedongle

object Native {
    init { System.loadLibrary("sparedongle") }
    @JvmStatic external fun open(fd: Int, inEndpoint: Int, outEndpoint: Int): Long
    @JvmStatic external fun readingJson(handle: Long): String
    @JvmStatic external fun reportText(handle: Long): String
    @JvmStatic external fun close(handle: Long)
    @JvmStatic external fun isDiagnostic(descriptor: ByteArray): Boolean
    @JvmStatic external fun reportDescriptorLength(raw: ByteArray, interfaceNumber: Int): Int
}
