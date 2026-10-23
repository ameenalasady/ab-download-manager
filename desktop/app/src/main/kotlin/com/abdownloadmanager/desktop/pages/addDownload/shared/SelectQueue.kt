package com.abdownloadmanager.desktop.pages.addDownload.shared

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.selection.selectable
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.abdownloadmanager.desktop.actions.newQueueAction
import com.abdownloadmanager.desktop.ui.template.OptionPopup
import com.abdownloadmanager.resources.Res
import com.abdownloadmanager.shared.pages.adddownload.addToQueue.SelectQueueComponent
import com.abdownloadmanager.shared.ui.widget.*
import com.abdownloadmanager.shared.util.ui.icon.MyIcons
import com.abdownloadmanager.shared.util.ui.myColors
import com.abdownloadmanager.shared.util.ui.theme.myShapes
import com.abdownloadmanager.shared.util.ui.theme.myTextSizes
import ir.amirab.util.compose.asStringSource
import ir.amirab.util.compose.resources.myStringResource

@Composable
fun ShowAddToQueueDialog(queueComponent: SelectQueueComponent) {
    if (!queueComponent.shouldShowAddToQueue) return
    val queueList by queueComponent.queueList.collectAsState()
    val selectedQueue by queueComponent.selectedQueue.collectAsState()
    val startQueue by queueComponent.startQueue.collectAsState()
    val rememberThisChoice by queueComponent.rememberThisChoice.collectAsState()
    OptionPopup(onDismissRequest = queueComponent::closeAddToQueue) {
        Column(Modifier.fillMaxWidth()) {
            Text(
                myStringResource(Res.string.select_queue),
                modifier = Modifier.padding(horizontal = 12.dp, vertical = 10.dp),
                fontSize = myTextSizes.base,
                fontWeight = FontWeight.Bold,
            )
            // The shared surface owns scrolling, including long queue lists. Do not
            // put weighted/infinite-height scrollbar layouts inside a scrolling menu.
            QueueItemToSelect(
                name = myStringResource(Res.string.without_queue),
                isSelected = selectedQueue == null,
                onSelect = { queueComponent.setSelectedQueue(null) },
            )
            for (queue in queueList) {
                key(queue.id) {
                    val model by queue.queueModel.collectAsState()
                    QueueItemToSelect(
                        name = model.name,
                        isSelected = selectedQueue == queue.id,
                        onSelect = { queueComponent.setSelectedQueue(model.id) },
                    )
                }
            }
            Column(Modifier.fillMaxWidth().padding(12.dp)) {
                FlowRow(
                    itemVerticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    LabeledCheckbox(
                        value = startQueue,
                        onValueChange = queueComponent::setStartQueue,
                        enabled = selectedQueue != null,
                        description = myStringResource(Res.string.start_queue),
                    )
                    LabeledCheckbox(
                        value = rememberThisChoice,
                        onValueChange = queueComponent::setRememberThisChoice,
                        description = myStringResource(Res.string.remember_this),
                    )
                }
                Spacer(Modifier.height(12.dp))
                Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                    IconActionButton(
                        icon = MyIcons.add,
                        contentDescription = Res.string.add_new_queue.asStringSource(),
                        onClick = newQueueAction,
                    )
                    Spacer(Modifier.weight(1f))
                    PrimaryMainActionButton(
                        modifier = Modifier,
                        onClick = queueComponent::onConfirm,
                        text = myStringResource(Res.string.ok),
                    )
                }
            }
        }
    }
}

@Composable
private fun QueueItemToSelect(name: String, isSelected: Boolean, onSelect: () -> Unit) {
    Row(
        Modifier.fillMaxWidth()
            .clip(myShapes.defaultRounded)
            .then(if (isSelected) Modifier.background(myColors.selectionGradient()) else Modifier)
            .selectable(selected = isSelected, role = Role.RadioButton, onClick = onSelect)
            .heightIn(min = 36.dp)
            .padding(horizontal = 12.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        RadioButton(isSelected, onValueChange = { if (it) onSelect() })
        Spacer(Modifier.width(8.dp))
        Text(name, fontSize = myTextSizes.base)
    }
}
