package com.blelock.app

import org.junit.Assert.*
import org.junit.Test

class InternetAuthTest {
    private val key = ByteArray(16) { 11 }
    @Test fun independentKnownHmacVector() {
        assertEquals("492ce020fe2534a5789dc3848806c78f4f6711397f08e7e7a12ca5a4483c8aa6",
            InternetAuth.sign(key, "Hi There"))
    }
    @Test fun commandBindsLockPhoneTimeIdAndAction() {
        val c = InternetAuth.command("a0:f2:62:f1:14:b8", "04c8e193a7065", 1000, 1001)
        val tag = InternetAuth.sign(key, c)
        assertTrue(InternetAuth.verify(key, c, tag))
        listOf(c.replace("a0:f2", "a1:f2"), c.replace("7065", "7066"), c.replace("|1000|", "|1002|"),
            c.replace("|1001|", "|1002|"), c.replace("|open", "|close")).forEach {
            assertFalse(InternetAuth.verify(key, it, tag))
        }
        assertFalse(InternetAuth.verify(ByteArray(16), c, tag))
    }
    @Test fun responseCannotBeUsedAsCommand() {
        val c = InternetAuth.command("a0:f2:62:f1:14:b8", "04c8e193a7065", 1000, 1001)
        val r = InternetAuth.result("a0:f2:62:f1:14:b8", "04c8e193a7065", 1000, 1001, "opened", 0)
        val tag = InternetAuth.sign(key, r)
        assertFalse(InternetAuth.verify(key, c, tag))
        assertFalse(InternetAuth.verify(key, r.replace("opened", "closed"), tag))
        assertFalse(InternetAuth.verify(key, r.dropLast(1) + "1", tag))
    }
    @Test fun freshnessBoundary() {
        assertTrue(InternetAuth.fresh(970,1000))
        assertTrue(InternetAuth.fresh(1005,1000))
        assertFalse(InternetAuth.fresh(969,1000))
        assertFalse(InternetAuth.fresh(1006,1000))
    }
    @Test fun invalidTagRejected() {
        assertFalse(InternetAuth.verify(key, "x", "0"))
        assertFalse(InternetAuth.verify(key, "x", "g".repeat(64)))
    }
}
