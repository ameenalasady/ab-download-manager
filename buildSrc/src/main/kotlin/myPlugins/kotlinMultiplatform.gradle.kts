package myPlugins

plugins {
    kotlin("multiplatform")
}
repositories {
    mavenCentral()
}

kotlin {
    jvmToolchain(providers.gradleProperty("jvm.toolchain").get().toInt())
}
