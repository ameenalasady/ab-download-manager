package ir.amirab.util.desktop.poweraction

import ir.amirab.util.execAndWait

class PowerActionLinux : PowerAction {
    override fun initiate(config: PowerActionConfig): Boolean {
        return when (config.type) {
            PowerActionConfig.Type.Shutdown -> run(
                loginManagerMethod = "PowerOff",
                systemctlVerb = "poweroff",
            )

            PowerActionConfig.Type.Hibernate -> run(
                loginManagerMethod = "Hibernate",
                systemctlVerb = "hibernate",
            )

            PowerActionConfig.Type.Sleep -> run(
                loginManagerMethod = "Suspend",
                systemctlVerb = "suspend",
            )
        }
    }

    /**
     * logind is what every systemd based desktop uses, and it is the only one of the two that
     * works without asking for a password. `systemctl` is there for the rest.
     */
    private fun run(
        loginManagerMethod: String,
        systemctlVerb: String,
    ): Boolean {
        val commands = listOf(
            arrayOf(
                "dbus-send", "--system", "--print-reply",
                "--dest=org.freedesktop.login1",
                "/org/freedesktop/login1",
                "org.freedesktop.login1.Manager.$loginManagerMethod",
                // "interactive", let logind put up its own authentication prompt if it needs one
                "boolean:true",
            ),
            arrayOf(
                "systemctl", systemctlVerb
            ),
        )
        return commands.any { command ->
            runCatching {
                execAndWait(command)
            }.getOrElse { false }
        }
    }
}
