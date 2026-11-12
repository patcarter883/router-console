// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RouterConsole

// One config domain window: sections on the left, the domain's fields on the
// right, and the staged-changes list with Apply / Revert at the bottom. Every
// edit is local until Apply; the router is only touched by the editor.
Rectangle {
    id: win

    property string domainId: ""
    property var editor: null
    // Automation hook (--config-section): select this section once loaded.
    property string initialSection: ""
    property bool initialSectionApplied: false
    // Automation hook (--stage option=value): staged once the package is read.
    property string stageAfterLoad: ""
    property bool stageApplied: false

    implicitWidth: 920
    implicitHeight: 800
    property int minimumWidth: 700
    property int minimumHeight: 460
    property string windowTitle: win.editor ? qsTr("Config — %1").arg(win.editor.title) : qsTr("Config")
    color: "#1e1e1e"

    Component.onCompleted: {
        win.editor = AppEngine.configEditor(win.domainId)
        if (win.editor)
            win.editor.reload()
    }

    // The engine owns the editor (one per domain), so closing the window only
    // drops this window's reference.
    function releaseResources() { win.editor = null }

    Connections {
        target: win.editor
        function onStateChanged() {
            if (!win.editor || !win.editor.loaded) {
                return
            }
            if (!win.initialSectionApplied && win.initialSection.length > 0) {
                win.initialSectionApplied = true
                win.editor.selectSection(win.initialSection)
            }
            if (win.stageApplied || win.stageAfterLoad.length === 0)
                return
            const parts = win.stageAfterLoad.split("=")
            if (parts.length !== 2)
                return
            win.stageApplied = true
            win.editor.stageValue(parts[0], parts[1])
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 14

            Label {
                text: win.editor ? win.editor.sourceLabel : ""
                color: "#8c8c8c"
                font.pixelSize: 12
            }
            Label {
                objectName: "configStatus"
                text: win.editor ? win.editor.statusText : ""
                color: "#e8e8e8"
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Button {
                text: qsTr("Reload")
                enabled: win.editor && !win.editor.busy
                onClicked: win.editor.reload()
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#333333" }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 14

            Rectangle {
                Layout.preferredWidth: 220
                Layout.fillHeight: true
                color: "#242424"
                radius: 4

                ListView {
                    id: sectionList
                    anchors.fill: parent
                    anchors.margins: 4
                    clip: true
                    model: win.editor ? win.editor.sections : null

                    delegate: Rectangle {
                        id: sectionRow
                        required property string name
                        required property string type
                        required property string summary

                        width: sectionList.width
                        height: 46
                        color: win.editor && win.editor.currentSection === sectionRow.name
                               ? "#3a3a3a"
                               : (sectionHover.containsMouse ? "#303030" : "transparent")

                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 8
                            spacing: 2

                            Text {
                                text: sectionRow.summary.length > 0 ? sectionRow.summary
                                                                    : sectionRow.name
                                color: "#e8e8e8"
                                font.pixelSize: 13
                                font.bold: true
                                width: 200
                                elide: Text.ElideRight
                            }
                            Text {
                                text: sectionRow.type + " · " + sectionRow.name
                                color: "#7a7a7a"
                                font.pixelSize: 10
                                width: 190
                                elide: Text.ElideRight
                            }
                        }

                        MouseArea {
                            id: sectionHover
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: win.editor.selectSection(sectionRow.name)
                        }
                    }
                }
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                ColumnLayout {
                    width: parent.width
                    spacing: 6

                    Repeater {
                        model: win.editor ? win.editor.fields : null

                        delegate: RowLayout {
                            id: fieldRow
                            required property string option
                            required property string label
                            required property string kind
                            required property string suffix
                            required property var choices
                            required property string value
                            required property string original
                            required property bool changed
                            required property bool readOnly

                            Layout.fillWidth: true
                            spacing: 10

                            Label {
                                text: fieldRow.label
                                color: fieldRow.changed ? "#e8b84b" : "#c8c8c8"
                                Layout.preferredWidth: 180
                            }

                            TextField {
                                visible: fieldRow.kind === "text" || fieldRow.kind === "integer"
                                Layout.preferredWidth: 240
                                text: fieldRow.value
                                selectByMouse: true
                                onTextEdited: win.editor.stageValue(fieldRow.option, text)
                            }

                            CheckBox {
                                visible: fieldRow.kind === "boolean"
                                checked: fieldRow.value === "1"
                                onToggled: win.editor.stageValue(fieldRow.option, checked ? "1" : "0")
                            }

                            ComboBox {
                                visible: fieldRow.kind === "choice" || fieldRow.kind === "chain"
                                enabled: !fieldRow.readOnly
                                Layout.preferredWidth: 240
                                model: fieldRow.choices
                                currentIndex: Math.max(0, fieldRow.choices.indexOf(fieldRow.value))
                                onActivated: win.editor.stageValue(fieldRow.option, currentText)
                            }

                            // Interface references: the live interface names, with
                            // free text still accepted for anything outside the list.
                            ComboBox {
                                visible: fieldRow.kind === "interface-ref"
                                Layout.preferredWidth: 240
                                editable: true
                                model: fieldRow.choices
                                currentIndex: fieldRow.choices.indexOf(fieldRow.value)
                                Component.onCompleted: {
                                    if (currentIndex < 0 && fieldRow.value.length > 0)
                                        editText = fieldRow.value
                                }
                                onActivated: win.editor.stageValue(fieldRow.option, currentText)
                                onAccepted: win.editor.stageValue(fieldRow.option, editText)
                            }

                            // Zone references: the zones this package declares.
                            ComboBox {
                                visible: fieldRow.kind === "zone-ref"
                                Layout.preferredWidth: 240
                                model: fieldRow.choices
                                currentIndex: Math.max(0, fieldRow.choices.indexOf(fieldRow.value))
                                onActivated: win.editor.stageValue(fieldRow.option, currentText)
                            }

                            Label {
                                text: fieldRow.suffix
                                color: "#7a7a7a"
                                font.pixelSize: 11
                            }

                            Label {
                                text: fieldRow.changed ? qsTr("was %1").arg(fieldRow.original) : ""
                                color: "#8c8c8c"
                                font.pixelSize: 11
                                Layout.fillWidth: true
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 168
            color: "#242424"
            radius: 4

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true

                    Label {
                        objectName: "pendingCountLabel"
                        text: qsTr("Pending changes (%1)").arg(win.editor ? win.editor.pendingCount : 0)
                        color: "#e8e8e8"
                        font.bold: true
                    }
                    Item { Layout.fillWidth: true }
                    Label {
                        text: win.editor ? win.editor.errorText : ""
                        color: "#e05252"
                        elide: Text.ElideRight
                        Layout.maximumWidth: 380
                    }
                    Button {
                        text: qsTr("Revert")
                        enabled: win.editor && !win.editor.busy
                        onClicked: win.editor.revert()
                    }
                    Button {
                        objectName: "applyButton"
                        text: qsTr("Apply")
                        enabled: win.editor && win.editor.pendingCount > 0 && !win.editor.busy
                        onClicked: win.editor.apply()
                    }
                }

                ListView {
                    id: pendingList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: win.editor ? win.editor.pendingChanges : null

                    delegate: Text {
                        required property string description
                        text: "•  " + description
                        color: "#e8b84b"
                        font.pixelSize: 12
                        font.family: "monospace"
                    }
                }
            }
        }
    }
}
