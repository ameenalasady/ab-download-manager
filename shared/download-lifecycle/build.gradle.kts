plugins {
    id(MyPlugins.kotlinMultiplatform)
    id(Plugins.Kotlin.serialization)
    id(Plugins.Android.multiplatformLibrary)
}
kotlin {
    jvm("desktop")
    android {
        compileSdk = libs.versions.androidCompileSdk.get().toInt()
        namespace = "com.abdownloadmanager.downloadlifecycle"
        minSdk = 26
    }
    sourceSets {
        commonMain.dependencies {
            api(project(":downloader:core"))
            api(project(":downloader:monitor"))
            api(project(":shared:categories"))
            implementation(project(":shared:utils"))
            implementation(libs.kotlin.coroutines.core)
            implementation(libs.kotlin.serialization.json)
            compileOnly(libs.compose.runtime)
        }
    }
}
