plugins {
    `kotlin-dsl`
}
repositories {
    gradlePluginPortal()
    mavenCentral()
}
dependencies {
    implementation(libs.pluginKotlin)
    implementation(libs.pluginSerialization)
}
