package com.abdownloadmanager.shared.util.category

import java.io.File

fun interface CategoryDefaults {
    fun getDefaultCategories(): List<Category>
}

/** Original default IDs, filters and order, shared by both frontends. */
class StandardCategoryDefaults(
    private val iconUris: List<String>,
    private val getDefaultDownloadFolder: () -> String,
) : CategoryDefaults {

    fun getCategoryOfFileName(name: String): Category? {
        return getDefaultCategories()
            .firstOrNull { it.acceptFileName(name) }
    }

    override fun getDefaultCategories(): List<Category> {
        require(iconUris.size == 6)

        fun relative(path: String): String {
            return File(getDefaultDownloadFolder(), path).path
        }

        val compressed = Category(
            id = 0,
            name = "Compressed",
            path = relative("Compressed"),
            icon = iconUris[0],
            acceptedFileTypes = listOf(
                "zip",
                "rar",
                "7z",
                "tar",
                "gz",
                "bz2",
                "xz",
                "iso",
                "dmg",
                "tgz",
            ),
        )

        val programs = Category(
            id = 1,
            name = "Programs",
            path = relative("Programs"),
            icon = iconUris[1],
            acceptedFileTypes = listOf(
                "apk",
                "exe",
                "msi",
                "bat",
                "sh",
                "jar",
                "app",
                "deb",
                "rpm",
                "bin",
            ),
        )
        val videos = Category(
            id = 2,
            name = "Videos",
            path = relative("Videos"),
            icon = iconUris[2],
            acceptedFileTypes = listOf(
                "mp4",
                "avi",
                "mkv",
                "mov",
                "wmv",
                "flv",
                "webm",
                "m4v",
                "3gp",
                "mpeg",
                "ts",
            ),
        )

        val music = Category(
            id = 3,
            name = "Music",
            path = relative("Music"),
            icon = iconUris[3],
            acceptedFileTypes = listOf(
                "mp3",
                "wav",
                "aac",
                "flac",
                "ogg",
                "aiff",
                "wma",
                "m4a",
            ),
        )

        val pictures = Category(
            id = 4,
            name = "Pictures",
            path = relative("Pictures"),
            icon = iconUris[4],
            acceptedFileTypes = listOf(
                "jpg",
                "jpeg",
                "png",
                "gif",
                "bmp",
                "tiff",
                "tif",
                "svg",
                "webp",
                "heic",
                "ico",
                "raw",
                "psd",
            ),
        )
        val documents = Category(
            id = 5,
            name = "Documents",
            path = relative("Documents"),
            icon = iconUris[5],
            acceptedFileTypes = listOf(
                "doc",
                "docx",
                "pdf",
                "txt",
                "rtf",
                "odt",
                "xls",
                "xlsx",
                "ppt",
                "pptx",
                "csv",
                "epub",
                "pages",
            ),
        )
        return listOf(
            compressed,
            programs,
            videos,
            music,
            pictures,
            documents,
        )
    }

    fun isDefault(categories: List<Category>): Boolean {
        return getDefaultCategories() == categories.map {
            it.copy(items = emptyList())
        }
    }
}
