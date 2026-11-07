package com.abdownloadmanager.shared.util.category


sealed interface CategorySelectionMode {
    data class Fixed(val categoryId: Long) : CategorySelectionMode
    data object Auto : CategorySelectionMode
}
