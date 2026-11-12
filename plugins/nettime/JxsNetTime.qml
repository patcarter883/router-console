import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// nettime serves the router's CLOCK_REALTIME as a GstNetTime provider on
// UDP 5637; the encode rig and the Pi camera nodes slave to it with
// GstNetClientClock. This view reads the daemon's status surface and procd's
// view of the service — it does not speak the provider protocol itself.
Item {
    id: view

    property var plugin: null

    readonly property var samples: plugin ? plugin.samples : ({})
    readonly property var status: samples.status && samples.status.ok ? samples.status.fields : ({})
    readonly property var service: samples.service && samples.service.ok ? samples.service.fields : ({})

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

    function instancePid(name) {
        for (var key in view.service) {
            var entry = view.service[key]
            if (entry && entry.instances && entry.instances[name])
                return entry.instances[name].pid || 0
        }
        return 0
    }

    readonly property var instances: instanceNames()
    readonly property bool running: instances.length > 0
    readonly property bool enabled: status.enabled === true || status.enabled === "true"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        RowLayout {
            spacing: 10
            Rectangle {
                width: 12; height: 12; radius: 6
                color: view.running ? "#3fbf6a" : "#6f6f6f"
            }
            ColumnLayout {
                spacing: 0
                Label {
                    text: view.running ? qsTr("provider running (pid %1)").arg(view.instancePid(view.instances[0]))
                                       : qsTr("provider not running")
                    color: "#e8e8e8"
                    font.bold: true
                    font.pixelSize: 15
                }
                Label {
                    text: status.clock
                          ? qsTr("%1 · UDP %2").arg(status.clock).arg(status.port || "—")
                          : (view.enabled ? qsTr("enabled") : qsTr("no status from the daemon"))
                    color: "#8c8c8c"
                }
            }
            Item { Layout.fillWidth: true }
            Label {
                text: samples.status && samples.status.at ? qsTr("updated %1").arg(samples.status.at) : ""
                color: "#6f6f6f"
                font.pixelSize: 11
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#333333" }

        GridLayout {
            columns: 4
            columnSpacing: 18
            rowSpacing: 6
            Layout.fillWidth: true

            Label { text: qsTr("Provider"); color: "#8c8c8c" }
            Label {
                Layout.columnSpan: 3
                Layout.fillWidth: true
                text: status.provider || "gst_net_time_provider"
                color: "#e8e8e8"
            }

            Label { text: qsTr("Endpoint"); color: "#8c8c8c" }
            Label {
                Layout.columnSpan: 3
                text: plugin ? "udp://" + plugin.host + ":" + (status.port || 5637) : "—"
                color: "#4b9fe8"
                font.bold: true
            }

            // The app exposes no start time and no client counter, so neither is
            // shown: the pid (procd's, cross-checked by the data test) is the
            // only per-instance figure it actually provides.
            Label { text: qsTr("Instance"); color: "#8c8c8c" }
            Label {
                Layout.columnSpan: 3
                text: status.pid ? qsTr("pid %1").arg(status.pid) : "—"
                color: "#e8e8e8"
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#333333" }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("The encoder and the camera nodes lock their pipeline clock to this provider with GstNetClientClock; "
                       + "every PTS on the video LAN is on this one timeline. Liveness here is the daemon's own status, "
                       + "not a probe of the UDP provider.")
            color: "#8c8c8c"
            font.pixelSize: 11
        }

        Item { Layout.fillHeight: true }

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
