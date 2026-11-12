import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Interface check: this whole view exists only as files under plugins/hello/.
// It reads system.board through the manifest's data source, so if this window
// shows a hostname, the plugin interface (manifest, polling, QML loading) works
// without a single core change.
Item {
    id: view

    property var plugin: null

    readonly property var samples: plugin ? plugin.samples : ({})
    readonly property var board: samples.board && samples.board.ok ? samples.board.fields : ({})

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        Label {
            text: qsTr("Hello from a plugin")
            color: "#e8e8e8"
            font.bold: true
            font.pixelSize: 18
        }
        Label {
            text: qsTr("id %1 · view %2").arg(plugin ? plugin.pluginId : "?").arg(plugin ? plugin.viewUrl : "?")
            color: "#8c8c8c"
            font.pixelSize: 11
            elide: Text.ElideMiddle
            Layout.fillWidth: true
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#333333" }

        GridLayout {
            columns: 2
            columnSpacing: 18
            rowSpacing: 4

            Label { text: qsTr("Board"); color: "#8c8c8c" }
            Label { text: board.model || "—"; color: "#e8e8e8" }
            Label { text: qsTr("Hostname"); color: "#8c8c8c" }
            Label { text: board.hostname || "—"; color: "#e8e8e8" }
            Label { text: qsTr("Release"); color: "#8c8c8c" }
            Label {
                text: (board.release && board.release.description)
                      ? board.release.description
                      : [board.release && board.release.distribution,
                         board.release && board.release.version,
                         board.release && board.release.revision]
                            .filter(function (v) { return !!v }).join(" ")
                color: "#e8e8e8"
            }
            Label { text: qsTr("Sources"); color: "#8c8c8c" }
            Label {
                text: Object.keys(view.samples).map(function (id) {
                    return id + (view.samples[id].ok ? " ok" : " " + (view.samples[id].error || "failed"))
                }).join(", ")
                color: "#8c8c8c"
            }
        }

        Item { Layout.fillHeight: true }
    }
}
