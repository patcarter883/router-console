import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RouterConsole

ApplicationWindow {
    id: root
    width: 1280
    height: 800
    visible: true
    title: qsTr("router-console")

    // Interfaces tree state and the tool windows it opens (one per interface).
    property bool interfacesExpanded: true
    property bool startupWindowOpened: false
    property var interfaceWindows: []
    readonly property string startupInterface:
        typeof startupInterfaceName === "undefined" ? "" : startupInterfaceName

    function openInterface(name) {
        for (let i = 0; i < interfaceWindows.length; ++i) {
            if (interfaceWindows[i].ifName === name) {
                interfaceWindows[i].requestActivate()
                return
            }
        }
        const window = interfaceWindowComponent.createObject(root, { ifName: name })
        if (!window)
            return
        interfaceWindows.push(window)
        window.closing.connect(function () {
            const index = root.interfaceWindows.indexOf(window)
            if (index >= 0)
                root.interfaceWindows.splice(index, 1)
        })
    }

    function closeInterfaceWindows() {
        while (interfaceWindows.length > 0)
            interfaceWindows.pop().close()
    }

    Component {
        id: interfaceWindowComponent
        InterfaceWindow {}
    }

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            Label {
                text: AppEngine.connected ? AppEngine.hostname : qsTr("router-console")
                font.bold: true
                Layout.leftMargin: 8
            }
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("Connect…")
                onClicked: connectDialog.open()
            }
            Button {
                text: qsTr("Disconnect")
                enabled: AppEngine.connected
                onClicked: AppEngine.disconnectFromRouter()
            }
        }
    }

    // WinBox-parity shell: left tree of domains, main area hosts the board card
    // until a tool window covers it. Interfaces expand into their list; each
    // entry opens its own live graph window.
    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.preferredWidth: 250
            Layout.fillHeight: true
            color: "#252525"

            ColumnLayout {
                anchors.fill: parent
                spacing: 2

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 30
                    color: headerHover.containsMouse ? "#333333" : "transparent"

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        text: (root.interfacesExpanded ? "\u25be  " : "\u25b8  ") + qsTr("Interfaces")
                              + " (" + AppEngine.interfaces.count + ")"
                        color: "#e8e8e8"
                        font.bold: true
                        font.pixelSize: 13
                    }

                    MouseArea {
                        id: headerHover
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: root.interfacesExpanded = !root.interfacesExpanded
                    }
                }

                Column {
                    id: interfaceList
                    Layout.fillWidth: true
                    Layout.leftMargin: 18
                    visible: root.interfacesExpanded
                    spacing: 0

                    Repeater {
                        model: AppEngine.interfaces

                        delegate: Rectangle {
                            id: interfaceRow
                            required property string ifName
                            required property string rxRateText
                            required property bool up

                            width: interfaceList.width
                            height: 30
                            color: rowHover.containsMouse ? "#333333" : "transparent"

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 8
                                spacing: 8

                                Text {
                                    text: interfaceRow.ifName
                                    color: interfaceRow.up ? "#e8e8e8" : "#7a7a7a"
                                    font.pixelSize: 13
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                Text {
                                    text: "\u2193 " + interfaceRow.rxRateText
                                    color: "#3fbf6a"
                                    font.pixelSize: 10
                                }
                            }

                            MouseArea {
                                id: rowHover
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: root.openInterface(interfaceRow.ifName)
                            }
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: 18
                    Layout.topMargin: 4
                    visible: root.interfacesExpanded && AppEngine.interfaces.count === 0
                    text: AppEngine.connected ? qsTr("no interfaces") : qsTr("not connected")
                    color: "#7a7a7a"
                    font.pixelSize: 11
                }

                Repeater {
                    model: [qsTr("Config"), qsTr("Tools"), qsTr("Plugins")]
                    delegate: Text {
                        required property string modelData
                        Layout.fillWidth: true
                        Layout.leftMargin: 8
                        Layout.topMargin: 6
                        text: modelData
                        color: "#6f6f6f"
                        font.pixelSize: 13
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#1e1e1e"

            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - 48, 560)
                spacing: 12

                Label {
                    Layout.fillWidth: true
                    visible: !AppEngine.connected
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Not connected — use Connect…")
                    color: "#c8c8c8"
                    font.pixelSize: 18
                }

                Rectangle {
                    Layout.fillWidth: true
                    visible: AppEngine.connected
                    color: "#262626"
                    radius: 6
                    implicitHeight: boardColumn.implicitHeight + 40

                    ColumnLayout {
                        id: boardColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 20
                        spacing: 10

                        RowLayout {
                            Label { text: qsTr("Hostname"); font.bold: true; Layout.preferredWidth: 110 }
                            Label { objectName: "hostnameValue"; text: AppEngine.hostname; Layout.fillWidth: true }
                        }
                        RowLayout {
                            Label { text: qsTr("Model"); font.bold: true; Layout.preferredWidth: 110 }
                            Label { text: AppEngine.model; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        }
                        RowLayout {
                            Label { text: qsTr("Firmware"); font.bold: true; Layout.preferredWidth: 110 }
                            Label { objectName: "firmwareValue"; text: AppEngine.firmware; Layout.fillWidth: true }
                        }
                        RowLayout {
                            Label { text: qsTr("Kernel"); font.bold: true; Layout.preferredWidth: 110 }
                            Label { text: AppEngine.kernel; Layout.fillWidth: true }
                        }
                        RowLayout {
                            Label { text: qsTr("Target"); font.bold: true; Layout.preferredWidth: 110 }
                            Label { text: AppEngine.target; Layout.fillWidth: true }
                        }
                    }
                }
            }
        }
    }

    footer: Label {
        padding: 6
        text: AppEngine.errorText.length > 0
              ? AppEngine.statusText + " — " + AppEngine.errorText
              : AppEngine.statusText
        color: AppEngine.errorText.length > 0 ? "#e05252" : "#a0a0a0"
    }

    Connect { id: connectDialog }

    Component.onCompleted: connectDialog.open()

    Connections {
        target: AppEngine
        function onStateChanged() {
            if (!AppEngine.connected) {
                root.closeInterfaceWindows()
                return
            }
            connectDialog.close()
            if (root.startupInterface.length > 0 && !root.startupWindowOpened) {
                root.startupWindowOpened = true
                root.openInterface(root.startupInterface)
            }
        }
    }
}
