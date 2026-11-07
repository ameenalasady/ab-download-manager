dependencyResolutionManagement {
    repositories {
        mavenCentral()
    }
}

rootProject.name = "ABDownloadManager"

include("desktop:service")
include("downloader:core")
include("downloader:monitor")
include("integration:server")
include("shared:utils")
include("shared:categories")
include("shared:download-lifecycle")
includeBuild("./compositeBuilds/shared") {
    name = "build-shared"
}
