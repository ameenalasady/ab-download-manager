pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "CategoryIcons.js" as CategoryIcons

// Original category Name / optional file types / URL patterns / location hierarchy.
// Persistence and matching belong solely to the Kotlin CategoryManager.
Item {
    id: root
    required property var service
    readonly property bool modalOpen: dialog.visible || confirmDelete.visible
    readonly property bool destinationEditable: service.capabilities.indexOf("category.destination") >= 0 && service.runtime.mode === "explicit-runtime"
    readonly property bool iconEditable: service.capabilities.indexOf("category.icon") >= 0
    PreviewTheme { id: theme }
    function openCategories() { dialog.choose(""); dialog.open() }
    component Body: TextBlock { colors: theme }
    component Hint: Body { tone: "secondary" }
    component Field: FormField { Layout.fillWidth: true }
    UiDialog {
        id: dialog; objectName: "serviceCategoriesDialog"
        property string selectedCategory: ""
        property int iconIndex: -1
        readonly property int categoryIndex: {
            const all = root.service.categories
            for (let i = 0; i < all.length; ++i) if (all[i].id === selectedCategory) return i
            return -1
        }
        readonly property var category: {
            const all = root.service.categories
            for (let i = 0; i < all.length; ++i) if (all[i].id === selectedCategory) return all[i]
            return null
        }
        function choose(id: string) {
            selectedCategory = id
            const c = category
            nameField.text = c ? c.name : ""
            typesEnabled.checked = c ? c.acceptedFileTypes.length > 0 : true
            types.text = c ? c.acceptedFileTypes.join(" ") : ""
            patternsEnabled.checked = c ? c.acceptedUrlPatterns.length > 0 : false
            patterns.text = c ? c.acceptedUrlPatterns.join(" ") : ""
            // A stored icon the picker does not offer stays unselected and unchanged.
            iconIndex = CategoryIcons.indexOf(c ? c.icon : "icon:folder")
            usePath.checked = c ? c.usePath : false
            path.text = c ? c.path : (root.service.runtime.destination || "")
        }
        function send(op: string, args: var): bool { return root.service.categoryCommand(op, selectedCategory, args || {}) }
        title: qsTr("Categories")
        preferredWidth: Tokens.dialogWidthLarge; preferredHeight: Tokens.dialogHeightLarge
        contentItem: ColumnLayout {
            spacing: Tokens.contentSpacing
            RowLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: Tokens.paneSpacing
                ColumnLayout {
                    Layout.preferredWidth: Tokens.listPaneWidth; Layout.minimumWidth: Tokens.listPaneWidth; Layout.maximumWidth: Tokens.listPaneWidth
                    Layout.fillHeight: true; spacing: Tokens.spacingS
                    ListView {
                        id: categoryList
                        Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: Tokens.spacingXS
                        Accessible.role: Accessible.List; Accessible.name: qsTr("Categories")
                        model: root.service.categories
                        QQC2.ScrollBar.vertical: QQC2.ScrollBar {}
                        delegate: SidebarItem {
                            required property var modelData
                            required property int index
                            colors: theme
                            width: ListView.view.width; text: modelData.name
                            trailing: modelData.items ? String(modelData.items.length) : ""
                            highlighted: dialog.selectedCategory === modelData.id
                            onClicked: dialog.choose(modelData.id)
                        }
                    }
                    // List actions sit together under the list they act on.
                    RowLayout {
                        Layout.fillWidth: true; spacing: 0
                        IconButton { text: qsTr("New category"); icon.name: "list-add"; onClicked: { dialog.choose(""); nameField.forceActiveFocus() } }
                        Item { Layout.fillWidth: true }
                        IconButton { text: qsTr("Move category up"); icon.name: "go-up"; enabled: !root.service.busy && dialog.category !== null && dialog.categoryIndex > 0; onClicked: dialog.send("category.move", {direction: -1}) }
                        IconButton { text: qsTr("Move category down"); icon.name: "go-down"; enabled: !root.service.busy && dialog.category !== null && dialog.categoryIndex < categoryList.count - 1; onClicked: dialog.send("category.move", {direction: 1}) }
                        IconButton { text: qsTr("Delete category…"); icon.name: "edit-delete"; enabled: !root.service.busy && dialog.category !== null; onClicked: { confirmDelete.targetId = dialog.selectedCategory; confirmDelete.targetName = dialog.category.name; confirmDelete.open() } }
                    }
                }
                PaneSeparator {}
                QQC2.ScrollView {
                    Layout.fillWidth: true; Layout.fillHeight: true; contentWidth: availableWidth; clip: true
                    ColumnLayout {
                        width: parent.width; spacing: Tokens.spacingM
                        SectionHeading { level: Tokens.headingPage; text: dialog.category ? qsTr("Edit Category") : qsTr("New Category") }
                        Body { text: qsTr("Name:") }
                        Field { id: nameField; objectName: "categoryName"; maximumLength: 120; Accessible.name: qsTr("Category name") }
                        Body { text: qsTr("Icon:"); visible: root.iconEditable }
                        // Same grid choice as the original IconPick; -1 is a stored icon it does not offer.
                        RowLayout {
                            visible: root.iconEditable; spacing: Tokens.spacingM
                            // The desktop style may hide push-button icons; show the choice beside it.
                            Kirigami.Icon {
                                implicitWidth: Tokens.iconMedium; implicitHeight: Tokens.iconMedium
                                source: iconButton.icon.name
                            }
                            UiButton {
                                id: iconButton; objectName: "categoryIconButton"
                                icon.name: dialog.iconIndex >= 0 ? CategoryIcons.choices[dialog.iconIndex].icon : "text-x-generic"
                                text: dialog.iconIndex >= 0 ? CategoryIcons.choices[dialog.iconIndex].label : qsTr("Custom")
                                Accessible.name: qsTr("Category icon: %1").arg(text)
                                onClicked: iconPopup.open()
                                QQC2.Popup {
                                    id: iconPopup; objectName: "categoryIconPopup"
                                    popupType: QQC2.Popup.Window
                                    y: iconButton.height; padding: Tokens.spacingS
                                    contentItem: GridLayout {
                                        columns: 8; columnSpacing: 0; rowSpacing: 0
                                        Repeater {
                                            model: CategoryIcons.choices
                                            IconButton {
                                                required property var modelData
                                                required property int index
                                                objectName: "categoryIconChoice-" + index
                                                text: modelData.label; icon.name: modelData.icon
                                                checkable: true; checked: dialog.iconIndex === index
                                                onClicked: { dialog.iconIndex = index; iconPopup.close() }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        QQC2.CheckBox { id: typesEnabled; objectName: "categoryTypesEnabled"; Layout.topMargin: Tokens.spacingS; text: qsTr("File Types") }
                        Field { id: types; objectName: "categoryTypes"; enabled: typesEnabled.checked; maximumLength: 5248; placeholderText: "zip rar 7z"; Accessible.name: qsTr("File types, space separated") }
                        QQC2.CheckBox { id: patternsEnabled; Layout.topMargin: Tokens.spacingS; text: qsTr("URL Patterns") }
                        Field { id: patterns; objectName: "categoryPatterns"; enabled: patternsEnabled.checked; maximumLength: 16416; placeholderText: "http://127.0.0.1:*/files/*"; Accessible.name: qsTr("URL patterns, space separated") }
                        QQC2.CheckBox {
                            id: usePath; objectName: "categoryUsePath"; visible: root.destinationEditable
                            text: qsTr("Download Location"); Layout.topMargin: Tokens.spacingS
                        }
                        Field {
                            id: path; objectName: "categoryPath"; visible: root.destinationEditable
                            enabled: usePath.checked; maximumLength: 4096
                            Accessible.name: qsTr("Category download location")
                        }
                        Hint {
                            visible: root.destinationEditable
                            text: qsTr("Applies to new downloads unless you override Save in. Choose an existing writable folder within the allowed roots. Existing downloads are not moved.")
                        }
                        SectionHeading { Layout.topMargin: Tokens.spacingL; text: qsTr("Selected Download") }
                        Hint {
                            wrapMode: Text.NoWrap; elide: Text.ElideMiddle
                            text: root.service.selectedId !== "" ? root.service.selectedName : qsTr("Select a download in the main list to assign it.")
                        }
                        Flow {
                            Layout.fillWidth: true; spacing: Tokens.spacingM
                            UiButton {
                                objectName: "assignSelectedCategory"; text: qsTr("Assign to This Category")
                                Accessible.name: qsTr("Assign selected download to this category")
                                enabled: !root.service.busy && dialog.category !== null && root.service.selectedId !== ""
                                onClicked: dialog.send("category.assign", {id: root.service.selectedId})
                            }
                            UiButton {
                                text: qsTr("Remove from Category"); Accessible.name: qsTr("Uncategorize selected download")
                                enabled: !root.service.busy && root.service.selectedId !== ""
                                onClicked: root.service.categoryCommand("category.assign", "none", {id: root.service.selectedId})
                            }
                        }
                    }
                }
            }
            StatusMessage { type: Kirigami.MessageType.Error; message: root.service.errorMessage }
        }
        footer: DialogFooter {
                UiButton {
                    objectName: "applyServiceCategory"; text: dialog.category ? qsTr("Apply") : qsTr("Add Category")
                    icon.name: dialog.category ? "dialog-ok-apply" : "list-add"
                    QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.ApplyRole
                    enabled: !root.service.busy && nameField.text.trim() !== ""
                    onClicked: {
                        const args = {name: nameField.text,
                            acceptedFileTypes: typesEnabled.checked ? types.text.trim().split(/\s+/).filter(s => s.length > 0) : [],
                            acceptedUrlPatterns: patternsEnabled.checked ? patterns.text.trim().split(/\s+/).filter(s => s.length > 0) : []}
                        if (root.destinationEditable) {
                            args.usePath = usePath.checked
                            if (!dialog.category || path.text !== dialog.category.path) args.path = path.text
                        }
                        if (root.iconEditable && dialog.iconIndex >= 0) args.icon = CategoryIcons.choices[dialog.iconIndex].uri
                        dialog.send(dialog.category ? "category.update" : "category.create", args)
                    }
                }
                UiButton { text: qsTr("Close"); icon.name: "dialog-close"; QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.RejectRole; onClicked: dialog.close() }
        }
    }
    UiDialog {
        id: confirmDelete; objectName: "confirmDeleteCategory"
        property string targetId: ""; property string targetName: ""
        title: qsTr("Delete Category?")
        preferredWidth: Tokens.dialogWidthSmall
        contentItem: ColumnLayout {
            Body { text: qsTr("Delete “%1”? Downloads and files will be kept.").arg(confirmDelete.targetName) }
        }
        footer: DialogFooter {
            UiButton {
                text: qsTr("Delete"); icon.name: "edit-delete"; enabled: !root.service.busy
                QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.DestructiveRole
                onClicked: { if (root.service.categoryCommand("category.delete", confirmDelete.targetId, {})) { confirmDelete.close(); dialog.choose("") } }
            }
            UiButton { text: qsTr("Cancel"); icon.name: "dialog-cancel"; QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.RejectRole; onClicked: confirmDelete.close() }
        }
    }
}
