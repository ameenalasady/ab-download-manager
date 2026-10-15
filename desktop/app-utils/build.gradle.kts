plugins {
    id(MyPlugins.kotlin)
    id(MyPlugins.composeDesktop)
}
dependencies {
    api(project(":desktop:shared"))
    api(project(":shared:app"))
    implementation(libs.jbrApi)
    // EWMH fullscreen detection/control on X11, see window/linux/X11Ewmh.kt
    implementation(libs.jna.platform)
    testImplementation(kotlin("test-junit"))
}
