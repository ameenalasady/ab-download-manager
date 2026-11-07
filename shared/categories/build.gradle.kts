plugins {
    id(MyPlugins.kotlinMultiplatform)
    id(Plugins.Kotlin.serialization)
}
kotlin {
    jvm("desktop")
    sourceSets {
        commonMain.dependencies {
            api(project(":downloader:core"))
            implementation(project(":shared:utils"))
            implementation(libs.kotlin.coroutines.core)
            implementation(libs.kotlin.serialization.json)
        }
    }
}
