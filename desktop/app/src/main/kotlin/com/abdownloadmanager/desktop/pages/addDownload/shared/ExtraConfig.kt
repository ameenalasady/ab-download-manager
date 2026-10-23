package com.abdownloadmanager.desktop.pages.addDownload.shared

import androidx.compose.foundation.layout.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.abdownloadmanager.desktop.ui.template.OptionPopup
import com.abdownloadmanager.resources.Res
import com.abdownloadmanager.shared.ui.configurable.Configurable
import com.abdownloadmanager.shared.ui.configurable.ConfigurableUiProps
import com.abdownloadmanager.shared.ui.configurable.RenderConfigurable
import com.abdownloadmanager.shared.ui.widget.Text
import com.abdownloadmanager.shared.util.ui.theme.myTextSizes
import ir.amirab.util.compose.resources.myStringResource

@Composable
fun ExtraConfig(
    onDismiss: () -> Unit,
    configurables: List<Configurable<*>>,
) {
    OptionPopup(onDismissRequest = onDismiss) {
        Column(Modifier.fillMaxWidth()) {
            Text(
                myStringResource(Res.string.settings),
                modifier = Modifier.padding(horizontal = 12.dp, vertical = 10.dp),
                fontWeight = FontWeight.Bold,
                fontSize = myTextSizes.base,
            )
            configurables.forEach { cfg ->
                RenderConfigurable(
                    cfg,
                    ConfigurableUiProps(
                        itemPaddingValues = PaddingValues(horizontal = 12.dp, vertical = 8.dp),
                    ),
                )
            }
        }
    }
}
