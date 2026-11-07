package myPlugins

plugins {
    kotlin("jvm")
}
repositories {
    mavenCentral()
}

kotlin {
    jvmToolchain(providers.gradleProperty("jvm.toolchain").get().toInt())
}
