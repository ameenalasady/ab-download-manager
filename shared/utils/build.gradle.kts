plugins {
    id(MyPlugins.kotlinMultiplatform)
    id(Plugins.Kotlin.serialization)
}
kotlin {
    jvm("desktop")
    sourceSets {
        commonMain.dependencies {
            implementation(libs.kotlin.serialization.json)
            api(libs.okio.okio)
            api(libs.okhttp.okhttp)
            api(libs.kotlin.coroutines.core)
            api(libs.kotlin.datetime)
            api(libs.semver)
            api(libs.arrow.optics)
            api(libs.kermit)
            api("ir.amirab.util:platform:1")
        }
        val desktopMain = getByName("desktopMain")
        desktopMain.dependencies {
            api(libs.jna.platform)

            api(libs.dbusJava.core)
            runtimeOnly(libs.dbusJava.unixSocket)
        }
    }
}
