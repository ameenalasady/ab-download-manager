package com.abdownloadmanager.shared.ui.theme

import androidx.compose.ui.graphics.Color
import com.abdownloadmanager.shared.util.ui.MyColors

object LayanTheme {
    val colors = MyColors(
        id = "layan",
        name = "Layan",

        // Primary is used as text, focus, cursor and selection color on the background,
        // so it is lightened for readability. The directed brand purple is kept as primaryVariant.
        primary = Color(0xFF9D9EFF),
        primaryVariant = Color(0xFF5657F5),
        onPrimary = Color(0xFF1B1B22),

        secondary = Color(0xFFD5A6FF),
        onSecondary = Color(0xFF211A2B),

        background = Color(0xFF31313A),
        onBackground = Color(0xFFEAEAEA),

        surface = Color(0xFF3B3B45),
        onSurface = Color(0xFFEAEAEA),

        error = Color(0xFFFF99A4),
        onError = Color(0xFF2B1216),
        success = Color(0xFF8ED6A0),
        onSuccess = Color(0xFF10251A),
        warning = Color(0xFFF2CC81),
        onWarning = Color(0xFF2B2410),
        info = Color(0xFF89B4FF),
        onInfo = Color(0xFF121A2B),

        isLight = false,
    )
}
