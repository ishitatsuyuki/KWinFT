/*
 * Copyright (c) 2020 Ismael Asensio <isma.af@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License or (at your option) version 3 or any later version
 * accepted by the membership of KDE e.V. (or its successor approved
 * by the membership of KDE e.V.), which shall act as a proxy
 * defined in Section 14 of version 3 of the license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

import QtQuick 2.14
import QtQuick.Layouts 1.14
import QtQuick.Controls 2.14 as QQC2
import QtQml.Models 2.14
import org.kde.kcm 1.2
import org.kde.kirigami 2.12 as Kirigami

ScrollViewKCM {
    id: rulesListKCM

    // FIXME: ScrollViewKCM.qml:73:13: QML Control: Binding loop detected for property "implicitHeight"
    implicitWidth: Kirigami.Units.gridUnit * 35
    implicitHeight: Kirigami.Units.gridUnit * 25

    ConfigModule.columnWidth: Kirigami.Units.gridUnit * 23
    ConfigModule.buttons: ConfigModule.Apply

    property var selectedIndexes: []

    // Manage KCM pages
    Connections {
        target: kcm
        function onEditIndexChanged() {
            if (kcm.editIndex < 0) {
                // If no rule is being edited, hide RulesEdidor page
                kcm.pop();
            } else if (kcm.depth < 2) {
                // Add the RulesEditor page if it wasn't already
                kcm.push("RulesEditor.qml");
            }
        }
    }

    view: ListView {
        id: ruleBookView
        clip: true

        model: kcm.ruleBookModel
        currentIndex: kcm.editIndex
        delegate: Kirigami.DelegateRecycler {
            width: ruleBookView.width
            sourceComponent: ruleBookDelegate
        }

        highlightMoveDuration: Kirigami.Units.longDuration

        displaced: Transition {
            NumberAnimation { properties: "y"; duration: Kirigami.Units.longDuration }
        }

        Kirigami.PlaceholderMessage {
            visible: ruleBookView.count === 0
            anchors.centerIn: parent
            width: parent.width - (units.largeSpacing * 4)
            text: i18n("No rules for specific windows are currently set");
            explanation: xi18nc("@info", "Click the <interface>Add New...</interface> button below to add some")
        }
    }

    header: Kirigami.InlineMessage {
        id: exportInfo
        icon.source: "document-export"
        showCloseButton: true
        text: i18n("Select the rules to export")
        actions: [
            Kirigami.Action {
                iconName: "object-select-symbolic"
                text: checked ? i18n("Unselect All") : i18n("Select All")
                checkable: true
                checked: selectedIndexes.length === ruleBookView.count
                onToggled: {
                    if (checked) {
                        selectedIndexes = [...Array(ruleBookView.count).keys()]
                    } else {
                        selectedIndexes = [];
                    }
                }
            }
            ,
            Kirigami.Action {
                iconName: "document-save"
                text: i18n("Save Rules")
                enabled: selectedIndexes.length > 0
                onTriggered: {
                    exportDialog.active = true;
                }
            }
        ]
    }

    footer: RowLayout {
        QQC2.Button {
            text: i18n("Add New...")
            icon.name: "list-add-symbolic"
            enabled: !exportInfo.visible
            onClicked: {
                kcm.createRule();
            }
        }
        Item {
            Layout.fillWidth: true
        }
        QQC2.Button {
            text: i18n("Import...")
            icon.name: "document-import"
            enabled: !exportInfo.visible
            onClicked: {
                importDialog.active = true;
            }
        }
        QQC2.Button {
            text: checked ? i18n("Cancel Export") : i18n("Export...")
            icon.name: exportInfo.visible ? "dialog-cancel" : "document-export"
            enabled: ruleBookView.count > 0
            checkable: true
            checked: exportInfo.visible
            onToggled: {
                selectedIndexes = []
                exportInfo.visible = checked;
            }
        }
    }

    Component {
        id: ruleBookDelegate
        Kirigami.SwipeListItem {
            id: ruleBookItem

            RowLayout {
                //FIXME: If not used within DelegateRecycler, item goes on top of the first item when clicked
                Kirigami.ListItemDragHandle {
                    visible: !exportInfo.visible
                    listItem: ruleBookItem
                    listView: ruleBookView
                    onMoveRequested: {
                        kcm.moveRule(oldIndex, newIndex);
                    }
                }

                QQC2.TextField {
                    id: descriptionField
                    Layout.minimumWidth: Kirigami.Units.gridUnit * 2
                    Layout.fillWidth: true
                    background: Item {}
                    horizontalAlignment: Text.AlignLeft
                    text: model && model.display
                    onEditingFinished: {
                        kcm.setRuleDescription(index, text);
                    }
                    Keys.onPressed: {
                        switch (event.key) {
                        case Qt.Key_Escape:
                            // On <Esc> key reset to model data before losing focus
                            text = model.display;
                        case Qt.Key_Enter:
                        case Qt.Key_Return:
                        case Qt.Key_Tab:
                            ruleBookItem.forceActiveFocus();
                            event.accepted = true;
                            break;
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        enabled: exportInfo.visible
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.IBeamCursor
                        onClicked: {
                            itemSelectionCheck.toggle();
                            itemSelectionCheck.toggled();
                        }
                    }
                }

                QQC2.CheckBox {
                    id: itemSelectionCheck
                    visible: exportInfo.visible
                    checked: selectedIndexes.includes(index)
                    onToggled: {
                        var position = selectedIndexes.indexOf(index);
                        if (checked) {
                            if (position < 0) { selectedIndexes.push(index); }
                        } else {
                            if (position >= 0) { selectedIndexes.splice(position, 1); }
                        }
                        selectedIndexesChanged();
                    }
                }
            }

            actions: [
                Kirigami.Action {
                    text: i18n("Edit")
                    iconName: "edit-entry"
                    visible: !exportInfo.visible
                    onTriggered: {
                        kcm.editRule(index);
                    }
                }
                ,
                Kirigami.Action {
                    text: i18n("Duplicate")
                    iconName: "edit-duplicate"
                    visible: !exportInfo.visible
                    onTriggered: {
                        kcm.duplicateRule(index);
                    }
                }
                ,
                Kirigami.Action {
                    text: i18n("Delete")
                    iconName: "entry-delete"
                    visible: !exportInfo.visible
                    onTriggered: {
                        kcm.removeRule(index);
                    }
                }
            ]
        }
    }

    FileDialogLoader {
        id: importDialog
        title: i18n("Import Rules")
        isSaveDialog: false
        onLastFolderChanged: {
            exportDialog.lastFolder = lastFolder;
        }
        onFileSelected: {
            kcm.importFromFile(path);
        }
    }

    FileDialogLoader {
        id: exportDialog
        title: i18n("Export Rules")
        isSaveDialog: true
        onLastFolderChanged: {
            importDialog.lastFolder = lastFolder;
        }
        onFileSelected: {
            selectedIndexes.sort();
            kcm.exportToFile(path, selectedIndexes);
            exportInfo.visible = false;
        }
    }
}
