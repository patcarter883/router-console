import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RouterConsole

// Aiming view for the modemaim plugin. There is no rotator on this antenna, so
// nothing here reconstructs a heading from a timed sweep: the rose shows where
// the known ACMA sites are, the log holds the running peak of each signal
// metric, and the operator drops a mark while turning the mast by hand.
Item {
    id: root

    implicitWidth: 990
    implicitHeight: 640

    property QtObject plugin: null
    property string bandFilter: ""
    property string rangeKmText: "25"
    property int selectedMark: -1
    property string bearingGuess: ""
    property string markNote: ""

    readonly property color ink: "#e6e6e6"
    readonly property color muted: "#8f9499"
    readonly property color panel: "#242526"
    readonly property color line: "#3a3c3e"
    readonly property color markColor: "#d9a441"

    AimModel {
        id: aim
    }

    function sync() {
        if (plugin)
            aim.ingest(plugin.samples)
    }

    Connections {
        target: root.plugin
        function onUpdated() {
            root.sync()
        }
    }

    Component.onCompleted: {
        sync()
        // The operator opens this window to turn a mast: the bearing field is
        // where they type first, and Enter marks without reaching for the mouse.
        bearingField.forceActiveFocus()
    }

    readonly property real rangeKm: rangeKmText === "all" ? Math.max(5, maxKm()) : Number(rangeKmText)

    function maxKm() {
        var max = 0
        var all = aim.sites
        for (var i = 0; i < all.length; ++i) {
            if (all[i].km > max)
                max = all[i].km
        }
        return Math.ceil(max)
    }

    function hasBand(site, band) {
        if (!band)
            return true
        return site.bands.indexOf(band) >= 0
    }

    readonly property var drawnSites: {
        var out = []
        var all = aim.sites
        for (var i = 0; i < all.length; ++i) {
            if (hasBand(all[i], bandFilter))
                out.push(all[i])
        }
        return out
    }

    readonly property var bands: {
        var seen = []
        var all = aim.sites
        for (var i = 0; i < all.length; ++i) {
            var b = all[i].bands
            for (var j = 0; j < b.length; ++j) {
                if (seen.indexOf(b[j]) < 0)
                    seen.push(b[j])
            }
        }
        return seen.sort()
    }

    function markBearing(text) {
        var digits = String(text).match(/-?[0-9]+(\.[0-9]+)?/)
        if (!digits)
            return -1
        var value = parseFloat(digits[0])
        if (isNaN(value))
            return -1
        value = ((value % 360) + 360) % 360
        return value
    }

    function metric(value, digits) {
        if (value === undefined || value === null)
            return "—"
        return Number(value).toFixed(digits)
    }

    function bandColor(site) {
        var palette = ["#d9a441", "#5b9bd5", "#7fb069", "#c07ab0", "#c96f4a", "#6f8fb0"]
        var band = site.bands.length ? site.bands[0] : ""
        var sum = 0
        for (var i = 0; i < band.length; ++i)
            sum += band.charCodeAt(i)
        return palette[sum % palette.length]
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 4

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 6
            Layout.rightMargin: 6
            spacing: 10

            Label {
                text: aim.hasSites
                      ? qsTr("%1 sites around this router · %2 readings").arg(aim.siteCount).arg(aim.readingCount)
                      : qsTr("waiting for the tower extract")
                color: root.ink
                font.pixelSize: 12
            }
            Label {
                text: qsTr("no rotator: bearings are the operator's estimate")
                color: root.muted
                font.pixelSize: 11
            }
            Item { Layout.fillWidth: true }

            ComboBox {
                implicitHeight: 26
                implicitWidth: 116
                model: [qsTr("All bands")].concat(root.bands)
                onActivated: root.bandFilter = currentIndex === 0 ? "" : model[currentIndex]
            }
            ComboBox {
                implicitHeight: 26
                implicitWidth: 108
                model: ["5", "10", "25", "50", "all"]
                currentIndex: 2
                onActivated: root.rangeKmText = model[currentIndex]
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 4

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#171819"
                border.color: root.line
                clip: true

                Canvas {
                    id: rose
                    anchors.fill: parent
                    onPaint: root.paintRose(getContext("2d"))
                    Component.onCompleted: requestPaint()

                    Connections {
                        target: aim
                        function onUpdated() {
                            rose.requestPaint()
                        }
                    }
                    Connections {
                        target: root
                        function onSelectedMarkChanged() {
                            rose.requestPaint()
                        }
                        function onBandFilterChanged() {
                            rose.requestPaint()
                        }
                        function onRangeKmTextChanged() {
                            rose.requestPaint()
                        }
                    }
                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()
                }

                Label {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.margins: 8
                    color: root.muted
                    font.pixelSize: 10
                    text: qsTr("N up · rings %1 km apart · dots are ACMA sites, ticks are your marks")
                              .arg((root.rangeKm / 4).toFixed(root.rangeKm >= 20 ? 0 : 1))
                }
            }

            Rectangle {
                Layout.preferredWidth: 336
                Layout.fillHeight: true
                color: root.panel
                border.color: root.line

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 6

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 4
                        columnSpacing: 8
                        rowSpacing: 2

                        Label { text: ""; font.pixelSize: 10 }
                        Label { text: qsTr("live"); color: root.muted; font.pixelSize: 10 }
                        Label { text: qsTr("peak"); color: root.muted; font.pixelSize: 10 }
                        Label { text: qsTr("at"); color: root.muted; font.pixelSize: 10 }

                        Label { text: "RSRP"; color: root.ink; font.pixelSize: 12 }
                        Label {
                            text: root.metric(aim.live.rsrp, 0) + " dBm"
                            color: "#8fc4e8"
                            font.pixelSize: 17
                            font.bold: true
                        }
                        Label {
                            text: root.metric(aim.peaks.rsrp, 0) + " dBm"
                            color: "#cfe3f1"
                            font.pixelSize: 13
                        }
                        Label {
                            text: aim.peaks.rsrpAt ? aim.peaks.rsrpAt.substring(11, 19) : "—"
                            color: root.muted
                            font.pixelSize: 10
                        }

                        Label { text: "RSRQ"; color: root.ink; font.pixelSize: 12 }
                        Label {
                            text: root.metric(aim.live.rsrq, 0) + " dB"
                            color: "#a8d3a8"
                            font.pixelSize: 17
                            font.bold: true
                        }
                        Label {
                            text: root.metric(aim.peaks.rsrq, 0) + " dB"
                            color: "#d6ead6"
                            font.pixelSize: 13
                        }
                        Label {
                            text: aim.peaks.rsrqAt ? aim.peaks.rsrqAt.substring(11, 19) : "—"
                            color: root.muted
                            font.pixelSize: 10
                        }

                        Label { text: "SINR"; color: root.ink; font.pixelSize: 12 }
                        Label {
                            text: root.metric(aim.live.sinr, 0) + " dB"
                            color: "#e2c58a"
                            font.pixelSize: 17
                            font.bold: true
                        }
                        Label {
                            text: root.metric(aim.peaks.sinr, 0) + " dB"
                            color: "#efe0c2"
                            font.pixelSize: 13
                        }
                        Label {
                            text: aim.peaks.sinrAt ? aim.peaks.sinrAt.substring(11, 19) : "—"
                            color: root.muted
                            font.pixelSize: 10
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: aim.serving.present
                              ? qsTr("serving PCI %1 · ECI %2 · TA %3%4")
                                    .arg(aim.serving.pci).arg(aim.serving.eci).arg(aim.serving.ta)
                                    .arg(aim.serving.matched
                                         ? qsTr(" · site %1 at %2° / %3 km")
                                               .arg(aim.serving.site)
                                               .arg(Number(aim.serving.bearing).toFixed(0))
                                               .arg(Number(aim.serving.km).toFixed(1))
                                         : " · no ACMA site mapped")
                              : qsTr("no serving cell reported")
                        color: aim.serving.matched ? "#6fbf7a" : root.muted
                        font.pixelSize: 10
                        wrapMode: Text.Wrap
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: root.line
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        TextField {
                            id: bearingField
                            Layout.preferredWidth: 84
                            implicitHeight: 28
                            placeholderText: qsTr("bearing 045")
                            text: root.bearingGuess
                            onTextChanged: root.bearingGuess = text
                            Keys.onReturnPressed: (event) => {
                                root.dropMark()
                                event.accepted = true
                            }
                        }
                        TextField {
                            id: noteField
                            Layout.fillWidth: true
                            Layout.minimumWidth: 70
                            implicitHeight: 28
                            placeholderText: qsTr("note")
                            text: root.markNote
                            onTextChanged: root.markNote = text
                            Keys.onReturnPressed: (event) => {
                                root.dropMark()
                                event.accepted = true
                            }
                        }
                        Button {
                            id: markButton
                            Layout.preferredWidth: 62
                            text: qsTr("Mark")
                            implicitHeight: 28
                            enabled: root.plugin !== null
                            onClicked: root.dropMark()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Label {
                            text: aim.markCount === 1 ? qsTr("1 mark") : qsTr("%1 marks").arg(aim.markCount)
                            color: root.muted
                            font.pixelSize: 10
                        }
                        Item { Layout.fillWidth: true }
                        Button {
                            text: qsTr("Reset peaks")
                            implicitHeight: 24
                            font.pixelSize: 11
                            onClicked: aim.clearPeaks()
                        }
                        Button {
                            text: qsTr("Clear marks")
                            implicitHeight: 24
                            font.pixelSize: 11
                            onClicked: {
                                aim.clearMarks()
                                root.selectedMark = -1
                            }
                        }
                    }

                    ListView {
                        id: markList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: aim.marks
                        spacing: 2
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            width: ListView.view.width
                            height: 40
                            radius: 3
                            color: root.selectedMark === index ? "#33383c" : "#2a2c2e"
                            border.color: root.selectedMark === index ? root.markColor : "transparent"

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 5
                                spacing: 6

                                Rectangle {
                                    width: 4
                                    height: parent.height
                                    color: root.markColor
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Label {
                                        text: (modelData.bearing ? modelData.bearing + "°" : qsTr("bearing —"))
                                              + (modelData.note ? "  ·  " + modelData.note : "")
                                        color: root.ink
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                    Label {
                                        text: qsTr("RSRP %1 · SINR %2 · RSRQ %3 · %4")
                                                  .arg(root.metric(modelData.rsrp, 0))
                                                  .arg(root.metric(modelData.sinr, 0))
                                                  .arg(root.metric(modelData.rsrq, 0))
                                                  .arg(modelData.at.substring(11, 19))
                                        color: root.muted
                                        font.pixelSize: 10
                                    }
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: root.selectedMark = index
                            }
                        }
                    }
                }
            }
        }
    }

    // The Mark button and Enter in the fields both land here: one snapshot of the
    // bearing the operator is on, the note they typed, and the metrics as they
    // stand.
    function dropMark() {
        root.selectedMark = aim.addMark(root.bearingGuess, root.markNote)
        root.markNote = ""
        root.bearingGuess = ""
        noteField.text = ""
        bearingField.text = ""
    }

    function paintRose(ctx) {
        var w = rose.width
        var h = rose.height
        var cx = w / 2
        var cy = h / 2
        var radius = Math.min(w, h) / 2 - 26
        var km = rangeKm

        ctx.reset()
        ctx.fillStyle = "#171819"
        ctx.fillRect(0, 0, w, h)
        ctx.font = "10px sans-serif"

        // Quarter rings, labelled with true kilometres.
        ctx.strokeStyle = "#2b2e31"
        ctx.lineWidth = 1
        for (var q = 1; q <= 4; ++q) {
            var r = radius * q / 4
            ctx.beginPath()
            ctx.arc(cx, cy, r, 0, 2 * Math.PI)
            ctx.stroke()
            ctx.fillStyle = root.muted
            ctx.fillText((km * q / 4).toFixed(km >= 20 ? 0 : 1) + " km", cx + 4, cy - r - 3)
        }

        // Ticks every 15 degrees, labels every 45.
        for (var deg = 0; deg < 360; deg += 15) {
            var a = (deg - 90) * Math.PI / 180
            var inner = deg % 45 === 0 ? radius - 10 : radius - 5
            ctx.strokeStyle = deg % 45 === 0 ? "#4a4e52" : "#313437"
            ctx.beginPath()
            ctx.moveTo(cx + Math.cos(a) * inner, cy + Math.sin(a) * inner)
            ctx.lineTo(cx + Math.cos(a) * radius, cy + Math.sin(a) * radius)
            ctx.stroke()
            if (deg % 45 === 0) {
                ctx.fillStyle = root.ink
                ctx.fillText(deg === 0 ? "N" : String(deg) + "°",
                             cx + Math.cos(a) * (radius + 9) - 8,
                             cy + Math.sin(a) * (radius + 9) + 3)
            }
        }

        // Known sites at their bearing, radius by distance.
        var beyond = 0
        for (var i = 0; i < drawnSites.length; ++i) {
            var site = drawnSites[i]
            var siteAngle = (site.bearing - 90) * Math.PI / 180
            var ratio = site.km / km
            var clipped = ratio > 1
            if (clipped) {
                ratio = 1
                ++beyond
            }
            var px = cx + Math.cos(siteAngle) * radius * ratio
            var py = cy + Math.sin(siteAngle) * radius * ratio
            ctx.fillStyle = root.bandColor(site)
            ctx.globalAlpha = clipped ? 0.45 : 1
            ctx.fillRect(px - 1.8, py - 1.8, 3.6, 3.6)
            ctx.globalAlpha = 1
            if (site.served) {
                ctx.strokeStyle = "#6fbf7a"
                ctx.lineWidth = 1.4
                ctx.beginPath()
                ctx.arc(px, py, 6, 0, 2 * Math.PI)
                ctx.stroke()
                ctx.fillStyle = "#6fbf7a"
                ctx.fillText(qsTr("serving %1").arg(site.bearing.toFixed(0) + "°"), px + 8, py + 3)
            }
        }

        // The router.
        ctx.fillStyle = "#e6e6e6"
        ctx.beginPath()
        ctx.arc(cx, cy, 3.5, 0, 2 * Math.PI)
        ctx.fill()

        // Operator marks: radial ticks at the bearing the operator wrote down.
        var marks = aim.marks
        for (var m = 0; m < marks.length; ++m) {
            var bearing = markBearing(marks[m].bearing)
            if (bearing < 0)
                continue
            var angle = (bearing - 90) * Math.PI / 180
            var accent = root.selectedMark === m
            ctx.strokeStyle = accent ? "#f2c15c" : root.markColor
            ctx.lineWidth = accent ? 2.4 : 1.4
            ctx.beginPath()
            ctx.moveTo(cx + Math.cos(angle) * radius * 0.15, cy + Math.sin(angle) * radius * 0.15)
            ctx.lineTo(cx + Math.cos(angle) * radius, cy + Math.sin(angle) * radius)
            ctx.stroke()
            ctx.fillStyle = accent ? "#f2c15c" : root.markColor
            // Inside the rim, so the mark's bearing does not land on the same
            // number in the degree ring.
            ctx.fillText(bearing.toFixed(0) + "°",
                         cx + Math.cos(angle) * radius * 0.82 - 8,
                         cy + Math.sin(angle) * radius * 0.82 + 3)
        }
    }
}
