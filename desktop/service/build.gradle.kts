plugins {
    id(MyPlugins.kotlin)
    id(Plugins.Kotlin.serialization)
    application
}

dependencies {
    implementation(project(":downloader:core"))
    implementation(project(":downloader:monitor"))
    implementation(project(":shared:categories"))
    implementation(project(":shared:download-lifecycle"))
    // Original browser-integration endpoints/models (Ktor CIO, no Compose).
    implementation(project(":integration:server"))
    implementation(libs.kotlin.coroutines.core)
    implementation(libs.kotlin.datetime)
    implementation(libs.kotlin.serialization.json)
    implementation(libs.dbusJava.core)
    runtimeOnly(libs.dbusJava.unixSocket)
    testImplementation(kotlin("test-junit"))
}

// Reuse original fallback wording without loading Compose's resource runtime.
tasks.processResources {
    from(rootProject.file("shared/resources/src/commonMain/resources/com/abdownloadmanager/resources/locales/en_US.properties")) {
        rename { "download-errors-en.properties" }
    }
}

// The real notification-server test may claim a bus name only on a fresh private bus.
// Changing that opt-in must invalidate Gradle's test cache.
tasks.test {
    inputs.property("privateNotificationBus", providers.environmentVariable("ABDM_TEST_PRIVATE_BUS").orElse("0"))
}

application { mainClass.set("com.abdownloadmanager.service.IsolatedServiceKt") }
java { toolchain { languageVersion.set(JavaLanguageVersion.of(25)) } }
