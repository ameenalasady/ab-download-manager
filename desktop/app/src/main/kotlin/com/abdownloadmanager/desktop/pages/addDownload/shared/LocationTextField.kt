package com.abdownloadmanager.desktop.pages.addDownload.shared

import com.abdownloadmanager.shared.ui.widget.MyTextFieldWithIcons
import com.abdownloadmanager.shared.ui.widget.MyTextFieldIcon
import com.abdownloadmanager.shared.util.ui.icon.MyIcons
import com.abdownloadmanager.shared.util.ui.myColors
import com.abdownloadmanager.shared.util.ui.theme.myTextSizes
import com.abdownloadmanager.shared.ui.widget.Text
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.abdownloadmanager.desktop.ui.template.OptionPopup
import com.abdownloadmanager.desktop.ui.template.OptionPopupAnchor
import com.abdownloadmanager.resources.Res
import com.abdownloadmanager.shared.ui.widget.LabeledCheckbox
import com.abdownloadmanager.shared.util.div
import com.abdownloadmanager.shared.util.ui.WithContentColor
import com.abdownloadmanager.shared.util.ui.theme.myShapes
import com.abdownloadmanager.shared.util.ui.widget.MyIcon
import ir.amirab.util.compose.resources.myStringResource
import com.abdownloadmanager.desktop.ui.util.rememberMyDirectoryPickerLauncher
import com.abdownloadmanager.shared.pages.adddownload.shared.LastUsedLocationsProp
import com.abdownloadmanager.shared.pages.adddownload.shared.RememberFolderProp
import com.abdownloadmanager.shared.pages.adddownload.shared.rememberDescriptionString
import java.io.File

@Composable
fun LocationTextField(
    modifier: Modifier,
    text: String,
    setText: (String) -> Unit,
    errorText: String? = null,
    lastUsedLocations: LastUsedLocationsProp,
    rememberFolderAsDefault: RememberFolderProp,
) {
    var showLocationOptions by remember { mutableStateOf(false) }

    val downloadLauncherFolderPickerLauncher = rememberMyDirectoryPickerLauncher(
        title = myStringResource(Res.string.download_location),
        initialDirectory = remember(text) {
            runCatching {
                File(text).canonicalPath
            }.getOrNull()
        },
        attachToWindow = true
    ) { directory ->
        directory?.let(setText)
    }

    OptionPopupAnchor(modifier) {
        MyTextFieldWithIcons(
            text,
            setText,
            myStringResource(Res.string.location),
            modifier = Modifier.fillMaxWidth(),
            errorText = errorText,
            end = {
                Row {
                    MyTextFieldIcon(
                        icon = MyIcons.folder,
                        contentDescription = myStringResource(Res.string.download_location),
                    ) {
                        downloadLauncherFolderPickerLauncher.launch()
                    }
                    MyTextFieldIcon(
                        icon = MyIcons.down,
                        contentDescription = myStringResource(Res.string.download_location),
                    ) {
                        showLocationOptions = !showLocationOptions
                    }
                }
            }
        )
        if (showLocationOptions) {
            LocationOptionsDialog(
                onDismiss = {
                    showLocationOptions = false
                },
                lastUsedLocations = lastUsedLocations,
                onSuggestionSelected = {
                    setText(it)
                    showLocationOptions = false
                },
                rememberFolder = rememberFolderAsDefault,
            )
        }
    }
}

@Composable
private fun LocationOptionsDialog(
    onSuggestionSelected: (String) -> Unit,
    onDismiss: () -> Unit,
    lastUsedLocations: LastUsedLocationsProp,
    rememberFolder: RememberFolderProp,
) {
    OptionPopup(onDismissRequest = onDismiss, matchAnchorWidth = true) {
        Column(
            Modifier
                .fillMaxWidth()
        ) {
            WithContentColor(myColors.onBackground) {
                Column {
                    Box(Modifier.fillMaxWidth()) {
                        Row(
                            Modifier
                                .fillMaxWidth()
                                .padding(horizontal = 16.dp, vertical = 12.dp),
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            Text(
                                myStringResource(Res.string.download_location),
                                modifier = Modifier.weight(1f),
                                fontSize = myTextSizes.base,
                                fontWeight = FontWeight.Bold,
                            )
                            Spacer(Modifier.width(8.dp))
                            MyIcon(
                                MyIcons.clear,
                                null,
                                Modifier
                                    .clickable { onDismiss() }
                                    .size(16.dp)
                                    .alpha(0.5f)
                            )
                        }
                    }
                    Divider()
                    Column(
                        Modifier.padding(16.dp)
                    ) {
                        LabeledCheckbox(
                            value = rememberFolder.value,
                            onValueChange = rememberFolder.onValueChange,
                            enabled = rememberFolder.isEnabled,
                            description = rememberFolder.rememberDescriptionString(),
                        )
                    }
                    val hasLocations = lastUsedLocations.locations.isNotEmpty()
                    if (hasLocations) {
                        Divider()
                    }
                    if (hasLocations) {
                        Column(
                            Modifier
                                .fillMaxWidth()
                                .padding(horizontal = 16.dp, vertical = 10.dp)
                        ) {
                            Text(
                                text = myStringResource(Res.string.history),
                                fontSize = myTextSizes.base,
                                fontWeight = FontWeight.Bold,
                                modifier = Modifier.padding(bottom = 6.dp),
                            )
                            Column(
                                Modifier
                                    .fillMaxWidth()
                                    .heightIn(max = 180.dp)
                                    .verticalScroll(rememberScrollState())
                            ) {
                                for (l in lastUsedLocations.locations) {
                                    Row(
                                        Modifier
                                            .fillMaxWidth()
                                            .clip(myShapes.defaultRounded)
                                            .clickable {
                                                onSuggestionSelected(l)
                                            }
                                            .heightIn(min = 36.dp)
                                            .padding(vertical = 8.dp, horizontal = 8.dp),
                                        verticalAlignment = Alignment.CenterVertically,
                                    ) {
                                        Text(
                                            text = l,
                                            modifier = Modifier.weight(1f),
                                            fontSize = myTextSizes.base,
                                            maxLines = 2,
                                            overflow = TextOverflow.Ellipsis,
                                        )
                                        Spacer(Modifier.width(8.dp))
                                        MyIcon(
                                            MyIcons.clear,
                                            null,
                                            Modifier
                                                .clickable {
                                                    lastUsedLocations.onRequestRemove(l)
                                                }
                                                .padding(2.dp)
                                                .size(12.dp)
                                                .alpha(0.5f)
                                        )
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun Divider() {
    Spacer(
        Modifier
            .fillMaxWidth()
            .height(1.dp)
            .background(myColors.onBackground / 10)
    )
}
