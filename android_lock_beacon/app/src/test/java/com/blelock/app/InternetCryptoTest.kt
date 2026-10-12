package com.blelock.app

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertFalse
import org.junit.Test

class InternetCryptoTest {
    @Test fun keyMatchesFirmwareVectorAndDirectionsAreSeparated() {
        val ltk = ByteArray(16) { 11 }
        val command = InternetCrypto.deriveKey(ltk, "command", "a0:f2:62:f1:14:b8", "04c8e193a7065")
        assertArrayEquals(byteArrayOf(0x62,0x07,0x0d,0x12,0xe3.toByte(),0xca.toByte(),0x6a,0xf3.toByte(),
            0x9a.toByte(),0x17,0x12,0xae.toByte(),0xb7.toByte(),0x96.toByte(),0x63,0xde.toByte()), command)
        val result = InternetCrypto.deriveKey(ltk, "result", "a0:f2:62:f1:14:b8", "04c8e193a7065")
        assertFalse(command.contentEquals(result))
    }
}
