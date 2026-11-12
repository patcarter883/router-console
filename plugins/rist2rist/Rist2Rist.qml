import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// rist2rist has no rpcd object of its own: its live state is procd's view of
// the daemon (service.list) and its configuration is the `rist2rist` UCI
// package. Both are read through this plugin's manifest, not through client code.
Item {
    id: view

    property var plugin: null

    readonly property var samples: plugin ? plugin.samples : ({})
    readonly property var service: samples.service && samples.service.ok ? samples.service.fields : ({})
    readonly property var config: samples.config && samples.config.ok ? samples.config.fields : ({})

    function sections(type) {
        var out = []
        var fields = view.config
        for (var key in fields) {
            var section = fields[key]
            if (section && section[".type"] === type)
                out.push(section)
        }
        return out
    }

    function instanceNames() {
        var names = []
        for (var key in view.service) {
            var entry = view.service[key]
            if (entry && entry.instances) {
                for (var instance in entry.instances)
                    names.push(instance)
            }
        }
        return names
    }

    function instanceRunning(name) {
        for (var key in view.service) {
            var entry = view.service[key]
            if (entry && entry.instances && entry.instances[name])
                return entry.instances[name].running === true
        }
        return false
    }

    function instancePid(name) {
        for (var key in view.service) {
            var entry = view.service[key]
            if (entry && entry.instances && entry.instances[name])
                return entry.instances[name].pid || 0
        }
        return 0
    }

    readonly property var main: sections("rist2rist").length > 0 ? sections("rist2rist")[0] : ({})
    readonly property var destinations: sections("destination")
    readonly property var instances: instanceNames()
    readonly property bool running: instances.length > 0 && instanceRunning(instances[0])
    readonly property bool enabled: main.enabled === "1"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        RowLayout {
            spacing: 10
            Rectangle {
                width: 10; height: 10; radius: 5
                color: view.running ? "#3fbf6a" : (view.enabled ? "#c9a227" : "#6f6f6f")
            }
            Label {
                text: view.running ? qsTr("running (pid %1)").arg(view.instancePid(view.instances[0]))
                                   : (view.enabled ? qsTr("enabled, not running") : qsTr("disabled"))
                color: "#e8e8e8"
                font.bold: true
            }
            Item { Layout.fillWidth: true }
            Label {
                text: samples.service && samples.service.at ? qsTr("updated %1").arg(samples.service.at) : ""
                color: "#6f6f6f"
                font.pixelSize: 11
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#333333" }

        GridLayout {
            columns: 4
            columnSpacing: 18
            rowSpacing: 4
            Layout.fillWidth: true

            Label { text: qsTr("Listen"); color: "#8c8c8c" }
            Label {
                text: view.main.listen_url || "—"
                color: "#e8e8e8"
                Layout.columnSpan: 3
                Layout.fillWidth: true
                elide: Text.ElideMiddle
            }

            Label { text: qsTr("Profile"); color: "#8c8c8c" }
            Label { text: (view.main.profile || "—") + " → " + (view.main.out_profile || "—"); color: "#e8e8e8" }
            Label { text: qsTr("Buffer"); color: "#8c8c8c" }
            Label { text: (view.main.buffer_min || "—") + "–" + (view.main.buffer_max || "—") + " ms"; color: "#e8e8e8" }

            Label { text: qsTr("RTT window"); color: "#8c8c8c" }
            Label { text: (view.main.rtt_min || "—") + "–" + (view.main.rtt_max || "—") + " ms"; color: "#e8e8e8" }
            Label { text: qsTr("Reorder"); color: "#8c8c8c" }
            Label { text: (view.main.reorder_buffer || "—") + " ms"; color: "#e8e8e8" }

            Label { text: qsTr("Telemetry"); color: "#8c8c8c" }
            Label {
                Layout.columnSpan: 3
                text: view.main.telemetry_enabled === "1"
                      ? (view.main.telemetry_target || qsTr("enabled, no target"))
                      : qsTr("off")
                color: view.main.telemetry_enabled === "1" ? "#4b9fe8" : "#8c8c8c"
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#333333" }

        Label {
            text: qsTr("WAN destinations (%1)").arg(view.destinations.length)
            color: "#8c8c8c"
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: view.destinations
            spacing: 2

            delegate: Rectangle {
                required property var modelData
                required property int index
                width: ListView.view.width
                height: 26
                color: index % 2 === 0 ? "#232323" : "transparent"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    spacing: 16

                    Label {
                        Layout.preferredWidth: 260
                        text: modelData.address || modelData.host || "—"
                        color: "#e8e8e8"
                        elide: Text.ElideMiddle
                    }
                    Label {
                        Layout.preferredWidth: 140
                        text: modelData.interface || "default route"
                        color: "#8c8c8c"
                        elide: Text.ElideRight
                    }
                    Label {
                        text: qsTr("weight %1").arg(modelData.weight || "—")
                        color: "#8c8c8c"
                    }
                    Item { Layout.fillWidth: true }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: view.destinations.length === 0
                text: qsTr("no destination sections")
                color: "#6f6f6f"
            }
        }

        Label {
            Layout.fillWidth: true
            text: {
                var problems = []
                for (var id in view.samples) {
                    if (view.samples[id] && !view.samples[id].ok)
                        problems.push(id + ": " + (view.samples[id].error || "failed"))
                }
                return problems.join("   ")
            }
            visible: text.length > 0
            color: "#d97070"
            font.pixelSize: 11
            wrapMode: Text.WrapAnywhere
        }
    }
}
