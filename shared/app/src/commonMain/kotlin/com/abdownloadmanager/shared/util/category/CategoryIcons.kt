package com.abdownloadmanager.shared.util.category

import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import ir.amirab.util.compose.IIconResolver
import ir.amirab.util.compose.IconSource
import ir.amirab.util.compose.LocalIconFromUriResolver

fun Category.iconSource(iconResolver: IIconResolver): IconSource? = iconResolver.resolve(icon)

@Composable
fun Category.rememberIconPainter(): IconSource? {
    val iconResolver = LocalIconFromUriResolver.current
    return remember(icon) { iconResolver.resolve(icon) }
}
