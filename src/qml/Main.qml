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

    // WinBox-parity shell: left tree of domains, main area hosts dockable tool
    // windows (implemented after the connect milestone).
    RowLayout {
        anchors.fill: parent
        spacing: 0

        ListView {
            id: domainTree
            Layout.preferredWidth: 240
            Layout.fillHeight: true
            model: [qsTr("Interfaces"), qsTr("Config"), qsTr("Tools"), qsTr("Plugins")]
            delegate: ItemDelegate {
                required property string modelData
                width: domainTree.width
                text: modelData
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
            if (AppEngine.connected)
                connectDialog.close()
        }
    }
}
