object MyPlugins {
    private const val namespace = "myPlugins"
    const val kotlin = "$namespace.kotlin"
    const val kotlinMultiplatform = "$namespace.kotlinMultiplatform"
}

object Plugins {
    object Kotlin {
        private const val baseName = "org.jetbrains.kotlin"
        const val serialization = "$baseName.plugin.serialization"
    }
}
