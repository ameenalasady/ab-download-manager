package com.abdownloadmanager.desktop.pages.addDownload.single

import androidx.compose.animation.*
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.unit.dp
import arrow.core.Some
import com.abdownloadmanager.desktop.pages.addDownload.shared.*
import com.abdownloadmanager.desktop.ui.template.OptionPopup
import com.abdownloadmanager.desktop.ui.template.OptionPopupAnchor
import com.abdownloadmanager.resources.Res
import com.abdownloadmanager.shared.downloaderinui.add.CanAddResult
import com.abdownloadmanager.shared.pages.adddownload.shared.LastUsedLocationsProp
import com.abdownloadmanager.shared.pages.adddownload.shared.RememberFolderProp
import com.abdownloadmanager.shared.pages.adddownload.single.BaseAddSingleDownloadComponent
import com.abdownloadmanager.shared.ui.widget.*
import com.abdownloadmanager.shared.util.ClipboardUtil
import com.abdownloadmanager.shared.util.div
import com.abdownloadmanager.shared.util.downloaderror.DownloadErrorReason
import com.abdownloadmanager.shared.util.mvi.HandleEffects
import com.abdownloadmanager.shared.util.ui.WithContentAlpha
import com.abdownloadmanager.shared.util.ui.icon.MyIcons
import com.abdownloadmanager.shared.util.ui.myColors
import com.abdownloadmanager.shared.util.ui.theme.myShapes
import com.abdownloadmanager.shared.util.ui.theme.mySpacings
import com.abdownloadmanager.shared.util.ui.theme.myTextSizes
import com.abdownloadmanager.shared.util.ui.widget.MyIcon
import ir.amirab.downloader.connection.IResponseInfo
import ir.amirab.downloader.utils.OnDuplicateStrategy
import ir.amirab.util.compose.asStringSource
import ir.amirab.util.compose.resources.myStringResource

@Composable
fun AddDownloadPage(
    component: BaseAddSingleDownloadComponent,
) {
    Column(
        Modifier
            .fillMaxSize()
            .padding(horizontal = 20.dp, vertical = 12.dp)
    ) {
        val credentials by component.credentials.collectAsState()
        fun setLink(link: String) {
            component.setCredentials(
                credentials.copy(link = Some(link))
            )
        }

        HandleEffects(component) {
            when (it) {
                is BaseAddSingleDownloadComponent.Effects.Common -> {
                    when (it) {
                        is BaseAddSingleDownloadComponent.Effects.Common.SuggestUrl -> {
                            setLink(it.link)
                        }
                    }
                }

                is BaseAddSingleDownloadComponent.Effects.Platform -> {
                    // support platform effects if any
                }
            }
        }
        Column(
            Modifier.weight(1f).fillMaxWidth().verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            DownloadFormRow(myStringResource(Res.string.download_link)) {
                UrlTextField(text = credentials.link, setText = ::setLink)
            }
            Column {
                val useCategory by component.useCategory.collectAsState()
                Row(
                    modifier = Modifier.height(IntrinsicSize.Max),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    LabeledCheckbox(
                        modifier = Modifier.width(128.dp).padding(end = 12.dp),
                        value = useCategory,
                        onValueChange = component::setUseCategory,
                        description = myStringResource(Res.string.use_category),
                    )
                    CategorySelect(
                        modifier = Modifier.weight(1f),
                        enabled = useCategory,
                        categories = component.categories.collectAsState().value,
                        selectedCategory = component.selectedCategory.collectAsState().value,
                        onCategorySelected = {
                            component.setSelectedCategory(it)
                        },
                    )
                    Spacer(Modifier.width(8.dp))
                    CategoryAddButton(
                        enabled = useCategory,
                        modifier = Modifier.fillMaxHeight(),
                        onClick = {
                            component.addNewCategory()
                        },
                    )
                }
            }
            val isFolderChanged by component.isFolderChangedResult.collectAsState()
            val rememberFolderAsDefault by component.rememberFolderAsDefault.collectAsState()
            DownloadFormRow(myStringResource(Res.string.location)) {
                LocationTextField(
                    modifier = Modifier.fillMaxWidth(),
                    text = component.folder.collectAsState().value,
                    setText = {
                        component.setFolder(it)
                    },
                    lastUsedLocations = LastUsedLocationsProp(
                        locations = component.lastUsedLocations.collectAsState().value,
                        onRequestRemove = component::removeFromLastDownloadLocation,
                    ),
                    rememberFolderAsDefault = RememberFolderProp(
                        isEnabled = isFolderChanged.isChanged,
                        value = rememberFolderAsDefault,
                        onValueChange = component::setRememberFolderAsDefault,
                        categoryName = isFolderChanged.categoryOrNull()?.name,
                    ),
                )
            }
            val name by component.name.collectAsState()
            DownloadFormRow(myStringResource(Res.string.name)) {
                NameTextField(
                    text = name,
                    setText = {
                        component.setName(it)
                    },
                )
            }
            DownloadFormRow(myStringResource(Res.string.size)) {
                Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                    FlowRow(
                        modifier = Modifier.weight(1f),
                        horizontalArrangement = Arrangement.spacedBy(16.dp),
                        verticalArrangement = Arrangement.spacedBy(6.dp),
                        itemVerticalAlignment = Alignment.CenterVertically,
                    ) {
                        RenderFileTypeAndSize(component)
                        RenderResumeSupport(component)
                    }
                    Spacer(Modifier.width(12.dp))
                    ConfigActionsButtons(component)
                }
            }
        }
        DownloadStatus(component)
        Spacer(Modifier.height(8.dp))
        Spacer(Modifier.fillMaxWidth().height(1.dp).background(myColors.onBackground / 10))
        Spacer(Modifier.height(12.dp))
        MainActionButtons(component)
    }
}

@Composable
private fun DownloadFormRow(label: String, content: @Composable () -> Unit) {
    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.Top) {
        Text(
            label,
            modifier = Modifier.width(128.dp).padding(end = 12.dp, top = 8.dp),
            fontSize = myTextSizes.base,
        )
        Box(Modifier.weight(1f)) { content() }
    }
}

/** A reserved, non-scrolling status lane: validation never inserts rows into the form. */
@Composable
private fun DownloadStatus(component: BaseAddSingleDownloadComponent) {
    val result by component.canAddResult.collectAsState()
    val strategy by component.onDuplicateStrategy.collectAsState()
    val name by component.name.collectAsState()
    val duplicate = result is CanAddResult.DownloadAlreadyExists
    val message = when {
        result == CanAddResult.CantWriteInThisFolder -> myStringResource(Res.string.cant_write_to_this_folder)
        result == CanAddResult.InvalidFileName && name.isNotEmpty() -> myStringResource(Res.string.invalid_file_name)
        duplicate && strategy == null -> myStringResource(Res.string.download_already_exists)
        strategy == OnDuplicateStrategy.AddNumbered -> myStringResource(Res.string.download_strategy_add_a_numbered_file)
        strategy == OnDuplicateStrategy.OverrideDownload -> myStringResource(Res.string.download_strategy_override_existing_file)
        else -> null
    }
    Row(
        Modifier.fillMaxWidth().heightIn(min = 36.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Box(Modifier.weight(1f).semantics { liveRegion = LiveRegionMode.Polite }) {
            if (message != null) {
                Tooltip(message.asStringSource()) {
                    Text(
                        text = message,
                        fontSize = myTextSizes.sm,
                        color = if (strategy != null && (duplicate || result == CanAddResult.CanAdd)) {
                            myColors.onBackground / 70
                        } else {
                            myColors.error
                        },
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                }
            }
        }
        if (duplicate || strategy != null) {
            SolutionButton(component, myStringResource(
                if (strategy == null) Res.string.show_solutions else Res.string.change_solution
            ))
        }
    }
}

@Composable
private fun SolutionButton(component: BaseAddSingleDownloadComponent, text: String) {
    OptionPopupAnchor {
        ActionButton(
            text = text,
            backgroundColor = SolidColor(Color.Transparent),
            borderColor = SolidColor(Color.Transparent),
            contentColor = myColors.primary,
            contentPadding = PaddingValues(horizontal = 8.dp, vertical = 6.dp),
            end = {
                Spacer(Modifier.width(6.dp))
                MyIcon(MyIcons.down, null, Modifier.size(12.dp))
            },
            onClick = { component.showSolutionsOnDuplicateDownloadUi = true },
        )
        if (component.showSolutionsOnDuplicateDownloadUi) {
            ShowSolutionsOnDuplicateDownload(component)
        }
    }
}

@Composable
private fun ShowSolutionsOnDuplicateDownload(component: BaseAddSingleDownloadComponent) {
    val close = {
        component.showSolutionsOnDuplicateDownloadUi = false
    }
    OptionPopup(onDismissRequest = close) {
        OnDuplicateStrategySolutionItem(
            title = myStringResource(Res.string.download_strategy_add_a_numbered_file),
            description = myStringResource(Res.string.download_strategy_add_a_numbered_file_description),
        ) {
            component.setOnDuplicateStrategy(OnDuplicateStrategy.AddNumbered)
            close()
        }
        OnDuplicateStrategySolutionItem(
            title = myStringResource(Res.string.download_strategy_override_existing_file),
            description = myStringResource(Res.string.download_strategy_override_existing_file_description),
        ) {
            component.setOnDuplicateStrategy(OnDuplicateStrategy.OverrideDownload)
            close()
        }
        OnDuplicateStrategySolutionItem(
            title = myStringResource(Res.string.download_strategy_update_download_link),
            description = myStringResource(Res.string.download_strategy_update_download_link_description),
        ) {
            component.updateDownloadCredentialsOfOriginalDownload()
            close()
        }
        OnDuplicateStrategySolutionItem(
            title = myStringResource(Res.string.download_strategy_show_downloaded_file),
            description = myStringResource(Res.string.download_strategy_show_downloaded_file_description),
        ) {
            component.openDownloadFileForCurrentLink()
            close()
        }
    }
}

@Composable
private fun OnDuplicateStrategySolutionItem(
    title: String,
    description: String,
    onClick: () -> Unit,
) {
    Tooltip(description.asStringSource()) {
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .clip(myShapes.defaultRounded)
                .semantics { contentDescription = description }
                .clickable(role = androidx.compose.ui.semantics.Role.Button, onClick = onClick)
                .heightIn(min = 40.dp)
                .padding(horizontal = 12.dp, vertical = 10.dp),
            contentAlignment = Alignment.CenterStart,
        ) {
            Text(title, fontSize = myTextSizes.base)
        }
    }
}


@Composable
fun RenderResumeSupport(component: BaseAddSingleDownloadComponent) {
    val responseResult by component.checkResponseResult.collectAsState()
    val fileInfo = responseResult?.getOrNull()?.takeIf { it.isSuccessFul }
    if (fileInfo == null) return
    val label = myStringResource(Res.string.resume_support)
    val value = myStringResource(if (fileInfo.resumeSupport) Res.string.yes else Res.string.no)
    Tooltip("$label: $value".asStringSource()) {
        Row(
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(6.dp),
            modifier = Modifier.semantics(mergeDescendants = true) {
                contentDescription = "$label: $value"
            },
        ) {
            MyIcon(
                icon = if (fileInfo.resumeSupport) MyIcons.check else MyIcons.clear,
                contentDescription = null,
                modifier = Modifier.size(12.dp),
                tint = if (fileInfo.resumeSupport) myColors.success else myColors.error,
            )
            Text(label, fontSize = myTextSizes.sm, color = myColors.onBackground / 70)
        }
    }
}


@Composable
fun ConfigActionsButtons(component: BaseAddSingleDownloadComponent) {
    val responseResult by component.checkResponseResult.collectAsState()
    Row {
        IconActionButton(MyIcons.refresh, Res.string.refresh.asStringSource()) {
            component.refresh()
        }
        Spacer(Modifier.width(6.dp))
        OptionPopupAnchor {
            IconActionButton(
                MyIcons.settings,
                Res.string.settings.asStringSource(),
                indicateActive = component.showMoreSettings,
                requiresAttention = responseResult?.getOrNull()?.requireBasicAuth ?: false
            ) {
                component.showMoreSettings = true
            }
            if (component.showMoreSettings) {
                ExtraConfig(
                    onDismiss = { component.showMoreSettings = false },
                    configurables = component.configurables,
                )
            }
        }
        component.lastError.collectAsState().value?.let { downloadErrorReason ->
            Spacer(Modifier.width(6.dp))
            DownloadErrorInfoButton(
                onClick = component::openDownloadErrorDialog,
                responseInfo = responseResult?.getOrNull(),
                reason = downloadErrorReason,
            )
        }
    }
}

@Composable
private fun MainActionButtons(component: BaseAddSingleDownloadComponent) {
    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.Bottom) {
        FlowRow(
            modifier = Modifier.weight(1f),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            val onDuplicateStrategy by component.onDuplicateStrategy.collectAsState()
            val canAddResult by component.canAddResult.collectAsState()
            if (canAddResult is CanAddResult.DownloadAlreadyExists && onDuplicateStrategy == null) {
                if (component.shouldShowOpenFile.collectAsState().value) {
                    ActionButton(
                        text = myStringResource(Res.string.open_file),
                        onClick = { component.openExistingFile() },
                        onLongClick = { component.openExistingFile(useAppChooser = true) },
                    )
                }
            } else {
                val canAddToDownloads by component.canAddToDownloads.collectAsState()
                OptionPopupAnchor {
                    ActionButton(
                        text = myStringResource(Res.string.add),
                        enabled = canAddToDownloads,
                        onClick = { component.selectQueueComponent.openAddToQueueDialog() },
                        onLongClick = { component.selectQueueComponent.fastConfirm() },
                    )
                    ShowAddToQueueDialog(queueComponent = component.selectQueueComponent)
                }
                PrimaryMainActionButton(
                    text = myStringResource(Res.string.download),
                    modifier = Modifier,
                    enabled = canAddToDownloads,
                    onClick = { component.onRequestDownload() },
                )
            }
        }
        Spacer(Modifier.width(16.dp))
        ActionButton(
            text = myStringResource(Res.string.cancel),
            onClick = { component.onRequestClose() },
        )
    }
}

@Composable
fun RenderFileTypeAndSize(
    component: BaseAddSingleDownloadComponent,
) {
    val isLinkLoading by component.isLinkLoading.collectAsState()
    val linkCheckResult by component.checkResponseResult.collectAsState()
    val fileIconProvider = component.iconProvider
    val iconModifier = Modifier.size(mySpacings.iconSize)
    Box {
        AnimatedContent(
            targetState = isLinkLoading,
            transitionSpec = {
                fadeIn() togetherWith fadeOut()
            }
        ) { loading ->
            if (loading) {
                LoadingIndicator(iconModifier)
            } else {
//                val extension = getExtension(fileInfo?.fileName ?: usersSetFileName) ?: "unknown"
                val downloadItem by component.downloadItem.collectAsState()
                val icon = fileIconProvider.rememberIcon(downloadItem.name)

//                val bitmap = FileIconProvider.getIconOfFileExtension(extension)

                AnimatedContent(
                    linkCheckResult,
                ) { linkCheckResult ->
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        WithContentAlpha(1f) {
                            when (linkCheckResult) {
                                null -> {
                                    MyIcon(
                                        icon = MyIcons.question,
                                        contentDescription = null,
                                        modifier = iconModifier,
                                    )
                                }

                                else -> {
                                    MyIcon(
                                        icon,
                                        null,
                                        iconModifier
                                    )
                                    val size = component.getLengthString()
                                    Spacer(Modifier.width(8.dp))
                                    Text(
                                        size.rememberString(),
                                        fontSize = myTextSizes.sm,
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

fun getExtension(s: String): String? {
    if (s.isBlank()) return null
    return s.substringAfterLast(".", "")
        .takeIf { it.isNotBlank() }
}


@Composable
private fun DownloadErrorInfoButton(
    onClick: () -> Unit,
    reason: DownloadErrorReason,
    responseInfo: IResponseInfo?,
) {
    val tooltipStringSource = reason.title.asStringSource()
    Tooltip(tooltipStringSource) {
        IconActionButton(
            onClick = {
                onClick()
            },
            contentDescription = tooltipStringSource,
            icon = if (responseInfo?.requireBasicAuth == true) {
                MyIcons.lock
            } else {
                MyIcons.question
            },
            contentColor = myColors.error,
        )
    }
}

@Composable
private fun UrlTextField(
    text: String,
    setText: (String) -> Unit,
    errorText: String? = null,
    modifier: Modifier = Modifier,
) {
    MyTextFieldWithIcons(
        text,
        setText,
        myStringResource(Res.string.download_link),
        modifier = modifier.fillMaxWidth(),
        end = {
            MyTextFieldIcon(MyIcons.paste) {
                setText(
                    ClipboardUtil.read()
                        .orEmpty()
                )
            }
        },
        errorText = errorText
    )
}

@Composable
private fun NameTextField(
    text: String,
    setText: (String) -> Unit,
    errorText: String? = null,
) {
    MyTextFieldWithIcons(
        text,
        setText,
        myStringResource(Res.string.name),
        modifier = Modifier.fillMaxWidth(),
        errorText = errorText,
    )
}

