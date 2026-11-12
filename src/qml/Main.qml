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

    // WinBox-parity shell: left tree of domains, main area hosts dockable tool
    // windows (implemented after the connect milestone).
    RowLayout {
        anchors.fill: parent

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

        Label {
            Layout.fillWidth: true
            Layout.fillHeight: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: AppEngine.connected ? qsTr("connected") : qsTr("not connected")
        }
    }
}
