package com.abdownloadmanager.desktop.experimental

import com.abdownloadmanager.desktop.main as standardAppMain
import ir.amirab.util.desktop.window.ExperimentalDesktopProfile

/** Unique desktop identity, but the real AppKt CLI/boot/DI/engine/UI startup path. */
fun main(args: Array<String>) {
    check(ExperimentalDesktopProfile.active) { "Use the isolated experimental bundle launcher" }
    standardAppMain(args)
}
