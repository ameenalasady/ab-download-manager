package com.abdownloadmanager.service

import kotlinx.coroutines.*
import org.freedesktop.dbus.connections.impl.DBusConnectionBuilder
import org.freedesktop.dbus.types.UInt32
import org.freedesktop.dbus.types.Variant
import org.junit.Assume.assumeTrue
import java.util.concurrent.CopyOnWriteArrayList
import kotlin.test.*

/** Must run under a NEW dbus-run-session; never claims the user's notification service. */
class DesktopNotificationBusTest {
    @Test fun realDbusMarshallingEscapesBodiesAndForwardsOnlyServerActivationTokens() = runBlocking {
        assumeTrue("Private bus test only", System.getenv("ABDM_TEST_PRIVATE_BUS") == "1")
        val connection = DBusConnectionBuilder.forSessionBus().withShared(false).build()
        val notices = CopyOnWriteArrayList<List<Any>>()
        val target = object : FreedesktopNotifications {
            override fun getObjectPath() = "/org/freedesktop/Notifications"
            override fun Notify(appName: String, replacesId: UInt32, appIcon: String, summary: String, body: String,
                actions: List<String>, hints: Map<String, Variant<*>>, expireTimeout: Int): UInt32 {
                notices += listOf(summary, body, actions, hints)
                return UInt32(7)
            }
        }
        connection.requestBusName("org.freedesktop.Notifications")
        connection.exportObject(target)
        val activated = CopyOnWriteArrayList<Pair<DesktopNotice, String?>>()
        val notifier = FreedesktopNotifier()
        try {
            notifier.onActivated = { notice, token -> activated += notice to token }
            val notice = DesktopNotice("key", "a<&b", "body<&>", false, false, "review")
            notifier.notify(notice)
            withTimeout(10_000) { while (notices.isEmpty()) delay(20) }
            assertEquals("a<&b", notices.single()[0]) // Summary is always plain text.
            assertEquals("body&lt;&amp;&gt;", notices.single()[1])
            @Suppress("UNCHECKED_CAST")
            val hints = notices.single()[3] as Map<String, Variant<*>>
            assertEquals(true, hints.getValue("suppress-sound").value)
            assertFalse("sound-name" in hints)
            // A different sender may not spoof the real server's action/token signals.
            DBusConnectionBuilder.forSessionBus().withShared(false).build().use { foreign ->
                foreign.sendMessage(FreedesktopNotifications.ActivationToken(target.objectPath, UInt32(7), "foreign-token"))
                foreign.sendMessage(FreedesktopNotifications.ActionInvoked(target.objectPath, UInt32(7), "default"))
                delay(150)
                assertTrue(activated.isEmpty())
            }
            connection.sendMessage(FreedesktopNotifications.ActivationToken(target.objectPath, UInt32(7), "fresh-token"))
            connection.sendMessage(FreedesktopNotifications.ActionInvoked(target.objectPath, UInt32(7), "default"))
            withTimeout(10_000) { while (activated.isEmpty()) delay(20) }
            assertEquals(notice, activated.single().first)
            assertEquals("fresh-token", activated.single().second)
            // A configured sound file replaces the themed sound name.
            notifier.notify(DesktopNotice("file", "t", "b", false, true, "show", 1, soundFile = "/fixture/done.wav"))
            notifier.notify(DesktopNotice("themed", "t", "b", true, true, "show", 1))
            withTimeout(10_000) { while (notices.size < 3) delay(20) }
            @Suppress("UNCHECKED_CAST") val fileHints = notices[1][3] as Map<String, Variant<*>>
            @Suppress("UNCHECKED_CAST") val themedHints = notices[2][3] as Map<String, Variant<*>>
            assertEquals("/fixture/done.wav", fileHints.getValue("sound-file").value); assertFalse("sound-name" in fileHints)
            assertEquals("dialog-error", themedHints.getValue("sound-name").value); assertFalse("sound-file" in themedHints)
        } finally { notifier.close(); connection.close() }
    }
}
