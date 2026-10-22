plugins {
    id(MyPlugins.kotlinMultiplatform)
    id(Plugins.Kotlin.serialization)
    id(Plugins.Android.multiplatformLibrary)
}
kotlin {
    jvm("desktop")
    android {
        compileSdk = libs.versions.androidCompileSdk.get().toInt()
        namespace = "com.abdownloadmanager.categories"
        minSdk = 26
    }
    sourceSets {
        commonMain.dependencies {
            api(project(":downloader:core"))
            implementation(project(":shared:utils"))
            implementation(libs.kotlin.coroutines.core)
            implementation(libs.kotlin.serialization.json)
            // Retain Compose's existing binary stability annotations without a UI runtime.
            compileOnly(libs.compose.runtime)
        }
    }
}
