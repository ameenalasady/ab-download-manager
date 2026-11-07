plugins {
    id(MyPlugins.kotlinMultiplatform)
    id(Plugins.Kotlin.serialization)
}
kotlin {
    jvm("desktop")
    sourceSets {
        commonMain {
            dependencies {
                implementation(project(":downloader:core"))
                implementation(project(":shared:utils"))
                implementation(libs.kotlin.coroutines.core)
            }
        }
    }
}
