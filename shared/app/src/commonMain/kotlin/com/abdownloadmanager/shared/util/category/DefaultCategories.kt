package com.abdownloadmanager.shared.util.category

import com.abdownloadmanager.shared.util.ui.IMyIcons

/** Compose icon adapter. IDs, filters and ordering live in the neutral implementation. */
class DefaultCategories(
    icons: IMyIcons,
    getDefaultDownloadFolder: () -> String,
) : CategoryDefaults {
    private val defaults = StandardCategoryDefaults(
        listOf(icons.zipFile, icons.applicationFile, icons.videoFile, icons.musicFile,
            icons.pictureFile, icons.documentFile).map { requireNotNull(it.uri) },
        getDefaultDownloadFolder,
    )
    override fun getDefaultCategories() = defaults.getDefaultCategories()
    fun getCategoryOfFileName(name: String) = defaults.getCategoryOfFileName(name)
    fun isDefault(categories: List<Category>) = defaults.isDefault(categories)
}
