package com.abdownloadmanager.service

import java.io.File
import java.util.concurrent.TimeUnit

/** Original queue power actions (PowerActionConfig.Type names, stored as powerActionTypeOnFinish). */
val powerActionTypes = setOf("Shutdown", "Hibernate", "Sleep")

/** Performs a power action the user saw counted down. Tests inject a recorder. */
fun interface PowerExecutor { fun initiate(type: String): Boolean }

/** Original PowerActionLinux: logind first (interactive, so polkit may ask), then systemctl. */
object LogindPowerExecutor : PowerExecutor {
    override fun initiate(type: String): Boolean {
        val (method, verb) = when (type) {
            "Shutdown" -> "PowerOff" to "poweroff"
            "Hibernate" -> "Hibernate" to "hibernate"
            "Sleep" -> "Suspend" to "suspend"
            else -> error("Unsupported power action")
        }
        return listOf(
            listOf("dbus-send", "--system", "--print-reply", "--dest=org.freedesktop.login1", "/org/freedesktop/login1",
                "org.freedesktop.login1.Manager.$method", "boolean:true"),
            listOf("systemctl", verb),
        ).any { command ->
            runCatching {
                val process = ProcessBuilder(command).redirectInput(ProcessBuilder.Redirect.from(File("/dev/null")))
                    .redirectOutput(ProcessBuilder.Redirect.DISCARD).redirectError(ProcessBuilder.Redirect.DISCARD).start()
                if (!process.waitFor(90, TimeUnit.SECONDS)) { process.destroyForcibly(); false } else process.exitValue() == 0
            }.getOrDefault(false)
        }
    }
}

/** A queue's power action waiting for the frontend's visible countdown. Never runs by itself. */
class PendingPowerAction(val serial: Long, val type: String, val reason: String, val queueId: Long, val queueName: String)
