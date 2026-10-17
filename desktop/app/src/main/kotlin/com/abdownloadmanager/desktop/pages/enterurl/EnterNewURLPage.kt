package com.abdownloadmanager.desktop.pages.enterurl

import androidx.compose.foundation.layout.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.input.key.*
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.abdownloadmanager.desktop.ui.template.DialogFooter
import com.abdownloadmanager.resources.Res
import com.abdownloadmanager.shared.downloaderinui.TADownloaderInUI
import com.abdownloadmanager.shared.pages.enterurl.DownloaderSelection
import com.abdownloadmanager.shared.ui.configurable.RenderSpinner
import com.abdownloadmanager.shared.ui.widget.*
import com.abdownloadmanager.shared.util.ClipboardUtil
import com.abdownloadmanager.shared.util.ui.WithContentAlpha
import com.abdownloadmanager.shared.util.ui.icon.MyIcons
import com.abdownloadmanager.shared.util.ui.theme.myTextSizes
import com.abdownloadmanager.shared.util.ui.widget.MyIcon
import ir.amirab.util.compose.resources.myStringResource

@Composable
fun EnterNewURLPage(component: DesktopEnterNewURLComponent) {
    val linkFocus = remember { FocusRequester() }
    LaunchedEffect(Unit) {
        linkFocus.requestFocus()
        component.onPageOpen()
    }
    val text by component.url.collectAsState()
    val canAdd by component.canAdd.collectAsState()
    Column(Modifier.fillMaxSize()) {
        Column(
            Modifier.weight(1f).padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            Text(
                myStringResource(Res.string.download_link),
                fontSize = myTextSizes.lg,
                fontWeight = FontWeight.Medium,
            )
            MyTextFieldWithIcons(
                text = text,
                onTextChange = component::setURL,
                placeholder = "https://",
                modifier = Modifier
                    .focusRequester(linkFocus)
                    .fillMaxWidth()
                    .onPreviewKeyEvent {
                        if (it.type == KeyEventType.KeyDown && (it.key == Key.Enter || it.key == Key.NumPadEnter) && canAdd) {
                            component.newDownloadEntered()
                            true
                        } else false
                    },
                start = { MyIcon(MyIcons.link, null, Modifier.padding(horizontal = 12.dp).size(16.dp)) },
                end = {
                    MyTextFieldIcon(MyIcons.paste, contentDescription = myStringResource(Res.string.paste)) {
                        component.setURL(ClipboardUtil.read().orEmpty())
                    }
                },
            )
            WithContentAlpha(0.7f) {
                Text(myStringResource(Res.string.download_link_hint), fontSize = myTextSizes.base)
            }
        }
        DialogFooter {
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                DownloaderSelectionSection(component)
                Spacer(Modifier.weight(1f))
                ActionButton(myStringResource(Res.string.cancel), onClick = component::close)
                Spacer(Modifier.width(8.dp))
                PrimaryMainActionButton(
                    text = myStringResource(Res.string.continue_download),
                    modifier = Modifier,
                    enabled = canAdd,
                    onClick = component::newDownloadEntered,
                )
            }
        }
    }
}

@Composable
private fun DownloaderSelectionSection(component: DesktopEnterNewURLComponent) {
    val selection by component.downloaderSelection.collectAsState()
    val bestDownloader by component.bestDownloader.collectAsState()
    RenderSpinner(
        possibleValues = component.possibleValues,
        value = selection,
        onSelect = component::selectDownloader,
        modifier = Modifier.widthIn(min = 140.dp, max = 200.dp),
        render = {
            Text(
                rememberDownloaderSelectionItemString(it, bestDownloader),
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        },
    )
}

@Composable
private fun rememberDownloaderSelectionItemString(
    downloaderSelection: DownloaderSelection,
    bestDownloader: TADownloaderInUI?,
): String = when (downloaderSelection) {
    DownloaderSelection.Auto -> {
        val autoText = myStringResource(Res.string.auto)
        val name = bestDownloader?.name?.rememberString()
        if (name == null) autoText else "$autoText ($name)"
    }
    is DownloaderSelection.Fixed -> downloaderSelection.downloaderInUi.name.rememberString()
}
