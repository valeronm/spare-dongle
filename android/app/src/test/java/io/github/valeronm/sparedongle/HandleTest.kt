package io.github.valeronm.sparedongle

import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test

class HandleTest {
    @Test
    fun aHandleIsTakenOnce() {
        val handle = Handle(42)
        assertEquals(listOf(42L, null), listOf(handle.take(), handle.take()))
    }

    @Test
    fun aTakenHandleThrowsOnUse() {
        val handle = Handle(42)
        assertEquals(42L, handle.get())
        handle.take()
        assertThrows(IllegalStateException::class.java) { handle.get() }
    }
}
