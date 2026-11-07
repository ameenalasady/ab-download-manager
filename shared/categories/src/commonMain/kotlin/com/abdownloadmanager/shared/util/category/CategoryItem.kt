package com.abdownloadmanager.shared.util.category


interface ICategoryItem {
    val fileName: String
    val url: String
}

data class CategoryItem(
    override val fileName: String,
    override val url: String,
) : ICategoryItem

data class CategoryItemWithId(
    val id: Long,
    override val fileName: String,
    override val url: String,
) : ICategoryItem
