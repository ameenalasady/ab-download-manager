package com.abdownloadmanager.shared.ui.widget.menu.custom

import com.abdownloadmanager.shared.util.ui.myColors
import com.abdownloadmanager.shared.util.ui.theme.myTextSizes
import ir.amirab.util.compose.action.MenuItem
import com.abdownloadmanager.shared.util.div
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import com.abdownloadmanager.shared.ui.widget.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp

@Composable
expect fun ShowOptionsInPopup(
    menu: MenuItem.SubMenu,
    onDismissRequest: () -> Unit,
)

/**
 * this is only used by expect actual ShowOptionsInPopup if their actual implementations need other style remove it here
 */
@Composable
internal fun RenderOptions(
    menu: MenuItem.SubMenu,
    onDismissRequest: () -> Unit
) {
    SubMenu(menu,onDismissRequest) {
        Column(
            Modifier
                .widthIn(min = 200.dp, max = 400.dp)
        ) {
            val itemPadding = Modifier.padding(horizontal = 12.dp, vertical = 8.dp)
            val title by menu.title.collectAsState()
            Text(
                title.rememberString(),
                Modifier.then(itemPadding),
                color = myColors.onMenuColor / 0.65f,
                fontWeight = FontWeight.Medium,
                fontSize = myTextSizes.sm,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            Spacer(Modifier
                .fillMaxWidth()
                .height(1.dp)
                .background(myColors.menuBorderColor))
        }
    }
}
