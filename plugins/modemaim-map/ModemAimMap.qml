import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RouterConsole

// Site map for the modemaim plugin: the ACMA sites from the router's own tower
// extract, drawn on a local equirectangular grid centred on the router's origin.
// No tiles and no network beyond the router: rings are true kilometres, so a
// site's position on the canvas is its bearing and distance from this router.
Item {
    id: root

    implicitWidth: 990
    implicitHeight: 640

    property QtObject plugin: null
    property string selectedId: ""
    property string bandFilter: ""
    property real zoomKm: 0 // 0 fits the whole extract
    property real panKmX: 0
    property real panKmY: 0

    readonly property color ink: "#e6e6e6"
    readonly property color muted: "#8f9499"
    readonly property color panel: "#242526"
    readonly property color line: "#3a3c3e"

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

    Component.onCompleted: sync()

    // Opens on the nearest site: the operator almost always wants the one they
    // can see first. Clicking the canvas replaces it.
    Connections {
        target: aim
        function onUpdated() {
            if (!root.selectedId && aim.sites.length > 0 && aim.hasOrigin)
                root.selectedId = aim.sites[0].site
        }
    }

    readonly property var originLat: aim.origin.set ? aim.origin.lat : 0
    readonly property var originLon: aim.origin.set ? aim.origin.lon : 0

    function kmEast(lon) {
        return (lon - originLon) * 111.1949 * Math.cos(originLat * Math.PI / 180)
    }

    function kmNorth(lat) {
        return (lat - originLat) * 111.1949
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

    readonly property real fitKm: {
        var maxKm = 2
        for (var i = 0; i < drawnSites.length; ++i) {
            var d = Math.hypot(kmEast(drawnSites[i].lon), kmNorth(drawnSites[i].lat))
            if (d > maxKm)
                maxKm = d
        }
        return Math.ceil(maxKm * 1.08)
    }

    readonly property real rangeKm: zoomKm > 0 ? zoomKm : fitKm
    readonly property real ringStep: niceStep(rangeKm)

    function siteById(id) {
        var all = aim.sites
        for (var i = 0; i < all.length; ++i) {
            if (all[i].site === id)
                return all[i]
        }
        return null
    }

    readonly property var selected: selectedId ? siteById(selectedId) : null

    // Australian bands get distinct hues; anything else hashes into the tail.
    readonly property var bandPalette: ({
        "B1": "#5b9bd5", "B3": "#d9a441", "B5": "#7fb069", "B7": "#c07ab0",
        "B8": "#8fa6c0", "B20": "#c96f4a", "B28": "#e8d06a", "B40": "#9b8ad8",
        "n78": "#4fc3b0", "B38": "#b0b0b0", "B39": "#d98ab0"
    })
    readonly property var bandFallback: ["#7f9fb5", "#b58f7f", "#8fb58f", "#b59f8f"]

    function bandColorOf(band) {
        if (bandPalette[band])
            return bandPalette[band]
        var sum = 0
        for (var i = 0; i < band.length; ++i)
            sum += band.charCodeAt(i)
        return bandFallback[sum % bandFallback.length]
    }

    function bandColor(site) {
        return bandColorOf(site.bands.length ? site.bands[0] : "")
    }

    function bearingText(deg) {
        return (Math.round(deg) % 360) + "°"
    }

    function niceStep(km) {
        var steps = [0.5, 1, 2, 5, 10, 20, 25, 50, 100, 200]
        for (var i = 0; i < steps.length; ++i) {
            if (steps[i] >= km / 4)
                return steps[i]
        }
        return 500
    }

    function kmToPx() {
        var radius = Math.min(map.width, map.height) / 2 - 24
        return radius / rangeKm
    }

    function siteAt(px, py) {
        var scale = kmToPx()
        var cx = map.width / 2
        var cy = map.height / 2
        var best = ""
        var bestPx = 12
        for (var i = 0; i < drawnSites.length; ++i) {
            var site = drawnSites[i]
            var x = cx + (kmEast(site.lon) - panKmX) * scale
            var y = cy - (kmNorth(site.lat) - panKmY) * scale
            var d = Math.hypot(x - px, y - py)
            if (d < bestPx) {
                bestPx = d
                best = site.site
            }
        }
        return best
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
                      ? qsTr("%1 sites · ACMA extract %2").arg(aim.siteCount).arg(aim.dataDate)
                      : qsTr("waiting for the tower extract")
                color: root.ink
                font.pixelSize: 12
            }
            Label {
                text: aim.hasOrigin
                      ? qsTr("origin %1, %2").arg(root.originLat.toFixed(5)).arg(root.originLon.toFixed(5))
                      : qsTr("origin not set on the router")
                color: root.muted
                font.pixelSize: 11
            }
            Item { Layout.fillWidth: true }

            RowLayout {
                spacing: 8
                Repeater {
                    model: root.bands
                    delegate: RowLayout {
                        required property string modelData
                        spacing: 3
                        Rectangle {
                            width: 7
                            height: 7
                            radius: 1
                            color: root.bandColorOf(modelData)
                        }
                        Label {
                            text: modelData
                            color: root.muted
                            font.pixelSize: 10
                        }
                    }
                }
            }

            ComboBox {
                id: bandBox
                implicitWidth: 120
                implicitHeight: 26
                model: [qsTr("All bands")].concat(root.bands)
                onActivated: root.bandFilter = currentIndex === 0 ? "" : model[currentIndex]
            }
            Button {
                text: qsTr("Fit")
                implicitHeight: 26
                onClicked: {
                    root.zoomKm = 0
                    root.panKmX = 0
                    root.panKmY = 0
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 4

            Rectangle {
                id: map
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#171819"
                border.color: root.line
                clip: true

                Canvas {
                    id: canvas
                    anchors.fill: parent
                    onPaint: root.paintMap(getContext("2d"))
                    Component.onCompleted: requestPaint()

                    Connections {
                        target: aim
                        function onUpdated() {
                            canvas.requestPaint()
                        }
                    }
                    Connections {
                        target: root
                        function onSelectedIdChanged() {
                            canvas.requestPaint()
                        }
                        function onPanKmXChanged() {
                            canvas.requestPaint()
                        }
                        function onPanKmYChanged() {
                            canvas.requestPaint()
                        }
                        function onZoomKmChanged() {
                            canvas.requestPaint()
                        }
                        function onBandFilterChanged() {
                            canvas.requestPaint()
                        }
                    }
                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()
                }

                MouseArea {
                    anchors.fill: parent
                    property real pressX: 0
                    property real pressY: 0
                    property real travel: 0
                    property bool dragging: false

                    onPressed: (mouse) => {
                        pressX = mouse.x
                        pressY = mouse.y
                        travel = 0
                        dragging = false
                    }
                    onPositionChanged: (mouse) => {
                        if (!pressed)
                            return
                        var dx = mouse.x - pressX
                        var dy = mouse.y - pressY
                        travel += Math.abs(mouse.x - pressX) + Math.abs(mouse.y - pressY)
                        if (travel > 6)
                            dragging = true
                        if (dragging) {
                            var scale = root.kmToPx()
                            root.panKmX -= dx / scale
                            root.panKmY += dy / scale
                            pressX = mouse.x
                            pressY = mouse.y
                        }
                    }
                    onReleased: (mouse) => {
                        if (!dragging)
                            root.selectedId = root.siteAt(mouse.x, mouse.y)
                    }
                    onWheel: (wheel) => {
                        var factor = wheel.angleDelta.y > 0 ? 0.8 : 1.25
                        var next = Math.max(1, Math.min(root.fitKm, root.rangeKm * factor))
                        root.zoomKm = next
                        wheel.accepted = true
                    }
                }

                // Top-left corner: the ring is centred, so the corners stay clear
                // of the compass labels and the ring captions.
                Label {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.margins: 8
                    color: root.muted
                    font.pixelSize: 10
                    text: qsTr("N up · equirectangular · rings %1 km · click a site · scroll to zoom")
                              .arg(root.ringStep)
                }

            }

            Rectangle {
                Layout.preferredWidth: 300
                Layout.fillHeight: true
                color: root.panel
                border.color: root.line

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 6

                    Label {
                        text: root.selected ? qsTr("Site %1").arg(root.selected.site) : qsTr("No site selected")
                        color: root.ink
                        font.pixelSize: 15
                        font.bold: true
                    }
                    Label {
                        visible: !!root.selected
                        text: root.selected ? root.selected.carrier : ""
                        color: root.muted
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    GridLayout {
                        visible: !!root.selected
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 4

                        Label { text: qsTr("Bearing"); color: root.muted; font.pixelSize: 11 }
                        Label {
                            text: root.selected ? qsTr("%1°").arg(root.selected.bearing.toFixed(1)) : ""
                            color: root.ink
                            font.pixelSize: 12
                        }
                        Label { text: qsTr("Distance"); color: root.muted; font.pixelSize: 11 }
                        Label {
                            text: root.selected
                                  ? (root.selected.km < 10 ? qsTr("%1 km").arg(root.selected.km.toFixed(2))
                                                           : qsTr("%1 km").arg(root.selected.km.toFixed(1)))
                                  : ""
                            color: root.ink
                            font.pixelSize: 12
                        }
                        Label { text: qsTr("Bands"); color: root.muted; font.pixelSize: 11 }
                        Label {
                            text: root.selected ? root.selected.bands.join(", ") : ""
                            color: root.ink
                            font.pixelSize: 12
                        }
                        Label { text: qsTr("MHz"); color: root.muted; font.pixelSize: 11 }
                        Label {
                            text: root.selected ? root.selected.mhz.join(", ") : ""
                            color: root.ink
                            font.pixelSize: 12
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                        }
                        Label { text: qsTr("Panels"); color: root.muted; font.pixelSize: 11 }
                        Label {
                            text: root.selected && root.selected.azs.length
                                  ? qsTr("%1° ± %2°").arg(root.selected.azs.join("/")).arg(root.selected.hbw)
                                  : qsTr("not published")
                            color: root.ink
                            font.pixelSize: 12
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                        }
                        Label { text: qsTr("Height"); color: root.muted; font.pixelSize: 11 }
                        Label {
                            text: root.selected && root.selected.h > 0 ? qsTr("%1 m").arg(root.selected.h) : qsTr("—")
                            color: root.ink
                            font.pixelSize: 12
                        }
                        Label { text: qsTr("EIRP"); color: root.muted; font.pixelSize: 11 }
                        Label {
                            text: root.selected && root.selected.eirp > 0 ? qsTr("%1 W").arg((root.selected.eirp / 1000).toFixed(2)) : qsTr("—")
                            color: root.ink
                            font.pixelSize: 12
                        }
                        Label { text: qsTr("Serving"); color: root.muted; font.pixelSize: 11 }
                        Label {
                            text: root.selected && root.selected.served ? qsTr("yes (mapped eNB)") : qsTr("no")
                            color: root.selected && root.selected.served ? "#6fbf7a" : root.muted
                            font.pixelSize: 12
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: root.line
                    }

                    Label {
                        text: qsTr("Nearest sites")
                        color: root.ink
                        font.pixelSize: 12
                    }

                    ListView {
                        id: nearestList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: root.drawnSites
                        spacing: 1
                        delegate: Rectangle {
                            required property var modelData
                            width: ListView.view.width
                            height: 24
                            color: root.selectedId === modelData.site ? "#2f3438" : "transparent"
                            radius: 3

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 6
                                anchors.rightMargin: 6
                                spacing: 8
                                Rectangle {
                                    width: 7
                                    height: 7
                                    radius: 3.5
                                    color: root.bandColor(modelData)
                                }
                                Label {
                                    text: modelData.site
                                    color: root.ink
                                    font.pixelSize: 11
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: modelData.km < 10 ? modelData.km.toFixed(1) + " km" : modelData.km.toFixed(0) + " km"
                                    color: root.muted
                                    font.pixelSize: 11
                                }
                                Label {
                                    text: root.bearingText(modelData.bearing)
                                    color: root.muted
                                    font.pixelSize: 11
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    root.selectedId = modelData.site
                                    root.zoomKm = Math.max(2, Math.min(root.fitKm, modelData.km * 3))
                                    root.panKmX = root.kmEast(modelData.lon)
                                    root.panKmY = root.kmNorth(modelData.lat)
                                }
                            }
                        }
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    }
                }
            }
        }
    }

    function paintMap(ctx) {
        var w = map.width
        var h = map.height
        var cx = w / 2
        var cy = h / 2
        var scale = kmToPx()
        var ringStep = niceStep(rangeKm)
        var radius = Math.min(w, h) / 2 - 30

        ctx.reset()
        ctx.fillStyle = "#171819"
        ctx.fillRect(0, 0, w, h)

        ctx.lineWidth = 1
        ctx.strokeStyle = "#2b2e31"
        ctx.beginPath()
        ctx.moveTo(cx - radius, cy)
        ctx.lineTo(cx + radius, cy)
        ctx.moveTo(cx, cy - radius)
        ctx.lineTo(cx, cy + radius)
        ctx.stroke()

        ctx.font = "10px sans-serif"
        ctx.fillStyle = root.muted
        for (var r = ringStep; r <= rangeKm + 0.001; r += ringStep) {
            var px = r * scale
            ctx.strokeStyle = "#2b2e31"
            ctx.beginPath()
            ctx.arc(cx, cy, px, 0, 2 * Math.PI)
            ctx.stroke()
            ctx.fillText(r + " km", cx + px * 0.707 + 3, cy - px * 0.707 - 2)
        }

        ctx.fillText("N", cx - 3, cy - radius - 6)
        ctx.fillText("E", cx + radius + 6, cy + 3)
        ctx.fillText("S", cx - 3, cy + radius + 12)
        ctx.fillText("W", cx - radius - 14, cy + 3)

        for (var i = 0; i < drawnSites.length; ++i) {
            var site = drawnSites[i]
            var x = cx + (kmEast(site.lon) - panKmX) * scale
            var y = cy - (kmNorth(site.lat) - panKmY) * scale
            if (x < -20 || y < -20 || x > w + 20 || y > h + 20)
                continue
            if (Math.hypot(x - cx, y - cy) > radius + 6)
                continue

            var selected = site.site === selectedId
            ctx.fillStyle = bandColor(site)
            ctx.fillRect(x - 2.2, y - 2.2, 4.4, 4.4)
            if (site.served) {
                ctx.strokeStyle = "#6fbf7a"
                ctx.beginPath()
                ctx.arc(x, y, 6, 0, 2 * Math.PI)
                ctx.stroke()
            }
            if (selected) {
                ctx.strokeStyle = "#ffffff"
                ctx.lineWidth = 1.5
                ctx.beginPath()
                ctx.arc(x, y, 8, 0, 2 * Math.PI)
                ctx.stroke()
                ctx.fillStyle = "#ffffff"
                ctx.fillText(site.site + "  " + bearingText(site.bearing), x + 10, y + 3)
            }
        }

        // The router: this is the origin every bearing is measured from.
        ctx.fillStyle = "#e6e6e6"
        ctx.beginPath()
        ctx.arc(cx, cy, 3.5, 0, 2 * Math.PI)
        ctx.fill()
    }
}
