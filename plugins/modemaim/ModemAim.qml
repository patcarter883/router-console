import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// modemaim: live MBIM metrics and the nearest licensed sites. Signal and
// serving cell come from `modemaim status`; the ACMA extract is a static
// /luci-static/ document fetched over HTTP (it is too big for a ubus reply),
// and the origin comes from `modemaim origin`.
Item {
    id: view

    property var plugin: null

    readonly property var samples: plugin ? plugin.samples : ({})
    readonly property var status: samples.status && samples.status.ok ? samples.status.fields : ({})
    readonly property var serving: status.serving ? status.serving : ({})
    readonly property var origin: samples.origin && samples.origin.ok ? samples.origin.fields : ({})
    readonly property var towers: samples.towers && samples.towers.ok ? samples.towers.fields : ({})

    function number(value) {
        var parsed = parseFloat(value)
        return isNaN(parsed) ? 0 : parsed
    }

    function hasOrigin() {
        return Math.abs(number(origin.lat)) > 0.0001 || Math.abs(number(origin.lon)) > 0.0001
    }

    function distanceKm(lat1, lon1, lat2, lon2) {
        var rad = Math.PI / 180
        var dLat = (lat2 - lat1) * rad
        var dLon = (lon2 - lon1) * rad
        var a = Math.sin(dLat / 2) * Math.sin(dLat / 2) +
                Math.cos(lat1 * rad) * Math.cos(lat2 * rad) *
                Math.sin(dLon / 2) * Math.sin(dLon / 2)
        return 6371.0 * 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a))
    }

    function bearingDeg(lat1, lon1, lat2, lon2) {
        var rad = Math.PI / 180
        var y = Math.sin((lon2 - lon1) * rad) * Math.cos(lat2 * rad)
        var x = Math.cos(lat1 * rad) * Math.sin(lat2 * rad) -
                Math.sin(lat1 * rad) * Math.cos(lat2 * rad) * Math.cos((lon2 - lon1) * rad)
        return (Math.atan2(y, x) / rad + 360) % 360
    }

    // Nearest sites in the band the modem is camped on: that is the set the
    // serving cell must come from, so it is the set worth aiming at.
    function nearestSites() {
        var list = towers.towers || []
        if (!hasOrigin() || list.length === 0)
            return []

        var lat = number(origin.lat)
        var lon = number(origin.lon)
        var out = []
        for (var i = 0; i < list.length; ++i) {
            var site = list[i]
            if (!site || site.lat === undefined)
                continue
            out.push({
                site: site.site,
                band: site.band,
                mhz: site.mhz,
                eirp: site.eirp,
                carrier: site.carrier,
                h: site.h,
                km: distanceKm(lat, lon, site.lat, site.lon),
                bearing: bearingDeg(lat, lon, site.lat, site.lon)
            })
        }
        out.sort(function (a, b) { return a.km - b.km })
        return out.slice(0, 10)
    }

    readonly property var nearest: nearestSites()

    function signalColor(db) {
        var value = number(db)
        if (value >= -85) return "#3fbf6a"
        if (value >= -100) return "#c9a227"
        if (value === 0) return "#6f6f6f"
        return "#d97070"
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        RowLayout {
            spacing: 10
            Label {
                text: status.operator && status.operator.length ? status.operator : (status.plmn || qsTr("no operator"))
                color: "#e8e8e8"
                font.bold: true
                font.pixelSize: 15
            }
            Label {
                text: [status.state, status.pstate, status.tech].filter(function (v) { return v && v !== "unknown" }).join(" · ")
                color: "#8c8c8c"
            }
            Item { Layout.fillWidth: true }
            Label {
                text: samples.status && samples.status.at ? qsTr("updated %1").arg(samples.status.at) : ""
                color: "#6f6f6f"
                font.pixelSize: 11
            }
        }

        RowLayout {
            spacing: 26

            Repeater {
                model: [
                    { label: "RSRP", value: status.rsrp, unit: "dBm" },
                    { label: "RSRQ", value: status.rsrq, unit: "dB" },
                    { label: "SINR", value: status.sinr, unit: "dB" },
                    { label: "RSSI", value: status.rssi, unit: "dBm" }
                ]

                ColumnLayout {
                    required property var modelData
                    spacing: 0
                    Label { text: modelData.label; color: "#8c8c8c"; font.pixelSize: 11 }
                    Label {
                        text: (modelData.value === "" || modelData.value === undefined)
                              ? "—" : modelData.value + " " + modelData.unit
                        color: view.signalColor(modelData.value)
                        font.bold: true
                        font.pixelSize: 20
                    }
                }
            }

            Item { Layout.fillWidth: true }

            ColumnLayout {
                spacing: 0
                Label { text: qsTr("Serving cell"); color: "#8c8c8c"; font.pixelSize: 11 }
                Label {
                    text: serving.pci ? qsTr("PCI %1 · EARFCN %2 · TA %3")
                                            .arg(serving.pci)
                                            .arg(serving.earfcn || "—")
                                            .arg(serving.ta || "—")
                                      : qsTr("none")
                    color: "#e8e8e8"
                    font.bold: true
                }
                Label {
                    text: serving.eci ? qsTr("ECI %1 · %2 neighbours").arg(serving.eci)
                                                                     .arg((status.neighbors || []).length)
                                     : ""
                    color: "#8c8c8c"
                    font.pixelSize: 11
                }
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#333333" }

        RowLayout {
            spacing: 12
            Label {
                text: towers.data_date
                      ? qsTr("ACMA extract %1 · %2 sites").arg(towers.data_date).arg((towers.towers || []).length)
                      : qsTr("no tower extract fetched")
                color: "#8c8c8c"
                font.pixelSize: 11
            }
            Item { Layout.fillWidth: true }
            Label {
                text: status.mapped && status.mapped.conf
                      ? qsTr("serving site %1 (%2)").arg(status.mapped.site || "—").arg(status.mapped.conf)
                      : qsTr("serving site inferred from the camped band, not identified")
                color: status.mapped && status.mapped.conf === "confirmed" ? "#3fbf6a" : "#c9a227"
                font.pixelSize: 11
            }
        }

        Label {
            Layout.fillWidth: true
            text: hasOrigin() ? qsTr("Nearest licensed sites to %1, %2")
                                .arg(number(origin.lat).toFixed(5))
                                .arg(number(origin.lon).toFixed(5))
                              : qsTr("No origin set on the router; tower distances unavailable")
            color: "#8c8c8c"
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            model: view.nearest

            header: RowLayout {
                width: ListView.view.width
                leftPadding: 6
                rightPadding: 6
                spacing: 12
                Label { Layout.preferredWidth: 110; text: qsTr("distance"); color: "#6f6f6f"; font.pixelSize: 11 }
                Label { Layout.preferredWidth: 90; text: qsTr("bearing"); color: "#6f6f6f"; font.pixelSize: 11 }
                Label { Layout.preferredWidth: 70; text: qsTr("band"); color: "#6f6f6f"; font.pixelSize: 11 }
                Label { Layout.preferredWidth: 90; text: qsTr("MHz"); color: "#6f6f6f"; font.pixelSize: 11 }
                Label { Layout.fillWidth: true; text: qsTr("carrier"); color: "#6f6f6f"; font.pixelSize: 11 }
                Label { Layout.preferredWidth: 110; text: qsTr("site"); color: "#6f6f6f"; font.pixelSize: 11 }
            }

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
                    spacing: 12

                    Label {
                        Layout.preferredWidth: 104
                        text: modelData.km.toFixed(2) + " km"
                        color: modelData.km < 4 ? "#3fbf6a" : "#e8e8e8"
                    }
                    Label {
                        Layout.preferredWidth: 84
                        text: modelData.bearing.toFixed(0) + "°"
                        color: "#e8e8e8"
                    }
                    Label { Layout.preferredWidth: 64; text: modelData.band || "—"; color: "#8c8c8c" }
                    Label { Layout.preferredWidth: 84; text: modelData.mhz !== undefined ? modelData.mhz : "—"; color: "#8c8c8c" }
                    Label {
                        Layout.fillWidth: true
                        text: modelData.carrier || "—"
                        color: "#e8e8e8"
                        elide: Text.ElideRight
                    }
                    Label { Layout.preferredWidth: 104; text: modelData.site || "—"; color: "#6f6f6f" }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: view.nearest.length === 0
                text: towers.towers ? qsTr("origin unset or extract empty") : qsTr("waiting for the tower extract")
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
