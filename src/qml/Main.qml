// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RouterConsole

// WinBox-parity shell: the tree on the left is the launcher, the area on the
// right is an MDI desktop. Every view opens as a frame in that desktop, so
// several stay live at once; the frames own position, size and z-order and
// remember their geometry across restarts.
ApplicationWindow {
    id: root
    width: 1280
    height: 800
    visible: true
    title: qsTr("router-console")

    property bool interfacesExpanded: true
    property bool configExpanded: true
    property bool pluginsExpanded: true
    property bool routingExpanded: true
    property bool toolsExpanded: true

    // Open frames, oldest first. Each frame carries its stable windowKey.
    property var mdiWindows: []
    // Scalar mirror of mdiWindows.length: var-array mutations do not notify.
    property int openCount: 0
    property bool startupApplied: false

    readonly property var routingViews: [
        { id: "routes", title: qsTr("Routes") },
        { id: "ospf", title: qsTr("OSPF") },
        { id: "bgp", title: qsTr("BGP") }
    ]
    readonly property var tools: [
        { id: "ping", title: qsTr("Ping") },
        { id: "traceroute", title: qsTr("Traceroute") },
        { id: "portscan", title: qsTr("Port Scan") },
        { id: "torch", title: qsTr("Torch") },
        { id: "files", title: qsTr("Files") },
        { id: "sqm", title: qsTr("SQM / CAKE stats") },
        { id: "terminal", title: qsTr("Terminal") },
        { id: "log", title: qsTr("Log") },
        { id: "leases", title: qsTr("DHCP leases") },
        { id: "backup", title: qsTr("Config backup") }
    ]

    // Context properties from main.cpp (--open-* flags). Named with a boot*
    // prefix so no property shadows the import it reads.
    readonly property var bootInterfaces:
        typeof startupInterfaces === "undefined" ? [] : startupInterfaces
    readonly property string bootInterfaceRange:
        typeof startupInterfaceRange === "undefined" ? "" : startupInterfaceRange
    readonly property var bootConfigs:
        typeof startupConfigDomains === "undefined" ? [] : startupConfigDomains
    readonly property var bootRouting:
        typeof startupRoutingIds === "undefined" ? [] : startupRoutingIds
    readonly property var bootTools:
        typeof startupTools === "undefined" ? [] : startupTools
    readonly property var bootPlugins:
        typeof startupPlugins === "undefined" ? [] : startupPlugins
    readonly property string bootLayout:
        typeof startupMdiLayout === "undefined" ? "" : startupMdiLayout
    readonly property string bootStage:
        typeof startupStageValue === "undefined" ? "" : startupStageValue

    // --- MDI window manager ------------------------------------------------

    function findWindow(key) {
        for (let i = 0; i < mdiWindows.length; ++i) {
            if (mdiWindows[i].windowKey === key)
                return mdiWindows[i]
        }
        return null
    }

    function openWindow(key, title, component, props) {
        const existing = findWindow(key)
        if (existing) {
            existing.raise()
            return existing
        }
        const slot = mdiWindows.length % 8
        const frame = frameComponent.createObject(mdiArea, {
            windowKey: key,
            fallbackTitle: title,
            contentComponent: component,
            contentProperties: props || ({}),
            initialRect: Qt.rect(20 + slot * 26, 20 + slot * 26, 0, 0)
        })
        if (!frame)
            return null
        frame.closed.connect(function (closedKey) { root.dropWindow(closedKey) })
        mdiWindows.push(frame)
        root.openCount = mdiWindows.length
        return frame
    }

    function dropWindow(key) {
        for (let i = 0; i < mdiWindows.length; ++i) {
            if (mdiWindows[i] && mdiWindows[i].windowKey === key) {
                mdiWindows.splice(i, 1)
                root.openCount = mdiWindows.length
                return
            }
        }
    }

    function closeAllWindows() {
        const frames = mdiWindows.slice()
        mdiWindows = []
        root.openCount = 0
        for (let i = 0; i < frames.length; ++i)
            frames[i].closeFrame()
    }

    function cascadeWindows() {
        if (mdiWindows.length === 0)
            return
        const rects = MdiGeometry.cascade(mdiWindows.length,
                                          Qt.rect(0, 0, mdiArea.width, mdiArea.height),
                                          Qt.size(360, 240))
        for (let i = 0; i < mdiWindows.length; ++i)
            mdiWindows[i].applyRect(rects[i])
    }

    function tileWindows() {
        if (mdiWindows.length === 0)
            return
        const rects = MdiGeometry.tile(mdiWindows.length,
                                       Qt.rect(0, 0, mdiArea.width, mdiArea.height))
        for (let i = 0; i < mdiWindows.length; ++i)
            mdiWindows[i].applyRect(rects[i])
    }

    // A desktop that shrank (or a restored frame from a bigger screen) must
    // keep every window reachable by its title bar.
    function clampWindows() {
        if (mdiArea.width < 80 || mdiArea.height < 80)
            return
        for (let i = 0; i < mdiWindows.length; ++i) {
            const frame = mdiWindows[i]
            frame.width = Math.min(Math.max(frame.minimumFrameWidth, frame.width), mdiArea.width)
            frame.height = Math.min(Math.max(frame.minimumFrameHeight, frame.height), mdiArea.height)
            frame.x = Math.max(0, Math.min(frame.x, mdiArea.width - 60))
            frame.y = Math.max(0, Math.min(frame.y, mdiArea.height - frame.titleBarHeight - 8))
        }
    }

    // --- View openers ------------------------------------------------------

    function openInterface(name, range) {
        return openWindow("interface/" + name, qsTr("Interface %1").arg(name),
                          interfaceWindowComponent,
                          { ifName: name, initialRange: range || "10m" })
    }

    function openConfig(domainId, initialSection) {
        return openWindow("config/" + domainId, qsTr("Config — %1").arg(domainId),
                          configWindowComponent,
                          { domainId: domainId, initialSection: initialSection || "" })
    }

    function openPlugin(pluginId) {
        return openWindow("plugin/" + pluginId, pluginId, pluginWindowComponent,
                          { pluginId: pluginId })
    }

    function openTool(toolId) {
        const component = toolComponent(toolId)
        if (!component)
            return null
        return openWindow("tool/" + toolId, toolId, component, { toolId: toolId })
    }

    function openRouting(viewId) {
        const component = routingComponent(viewId)
        if (!component)
            return null
        return openWindow("routing/" + viewId, viewId, component, { viewId: viewId })
    }

    function routingComponent(viewId) {
        switch (viewId) {
        case "routes": return routesWindowComponent
        case "ospf": return ospfWindowComponent
        case "bgp": return bgpWindowComponent
        }
        return null
    }

    function toolComponent(toolId) {
        switch (toolId) {
        case "ping": return pingWindowComponent
        case "traceroute": return tracerouteWindowComponent
        case "portscan": return portScanWindowComponent
        case "torch": return torchWindowComponent
        case "files": return filesWindowComponent
        case "sqm": return sqmStatsWindowComponent
        case "terminal": return terminalWindowComponent
        case "log": return logWindowComponent
        case "leases": return leasesWindowComponent
        case "backup": return backupWindowComponent
        }
        return null
    }

    function applyStartupWindows() {
        for (let i = 0; i < bootInterfaces.length; ++i)
            root.openInterface(bootInterfaces[i], bootInterfaceRange)
        for (let i = 0; i < bootConfigs.length; ++i) {
            const frame = root.openConfig(bootConfigs[i], "")
            if (frame && bootStage.length > 0 && frame.content)
                frame.content.stageAfterLoad = bootStage
        }
        for (let i = 0; i < bootRouting.length; ++i)
            root.openRouting(bootRouting[i])
        for (let i = 0; i < bootTools.length; ++i)
            root.openTool(bootTools[i])
        for (let i = 0; i < bootPlugins.length; ++i)
            root.openPlugin(bootPlugins[i])
        if (bootLayout === "tile")
            root.tileWindows()
        else if (bootLayout === "cascade")
            root.cascadeWindows()
    }

    Component { id: frameComponent; MdiWindowFrame {} }
    Component { id: interfaceWindowComponent; InterfaceWindow {} }
    Component { id: configWindowComponent; ConfigWindow {} }
    Component { id: pluginWindowComponent; PluginWindow {} }
    Component { id: routesWindowComponent; RoutesWindow {} }
    Component { id: ospfWindowComponent; OspfWindow {} }
    Component { id: bgpWindowComponent; BgpWindow {} }
    Component { id: pingWindowComponent; PingWindow {} }
    Component { id: tracerouteWindowComponent; TracerouteWindow {} }
    Component { id: portScanWindowComponent; PortScanWindow {} }
    Component { id: torchWindowComponent; TorchWindow {} }
    Component { id: filesWindowComponent; FilesWindow {} }
    Component { id: sqmStatsWindowComponent; SqmStatsWindow {} }
    Component { id: terminalWindowComponent; TerminalWindow {} }
    Component { id: logWindowComponent; LogWindow {} }
    Component { id: leasesWindowComponent; LeasesWindow {} }
    Component { id: backupWindowComponent; BackupWindow {} }

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            Label {
                text: AppEngine.connected ? AppEngine.hostname : qsTr("router-console")
                font.bold: true
                Layout.leftMargin: 8
            }
            Item { Layout.fillWidth: true }
            Label {
                objectName: "windowCount"
                text: qsTr("%1 window(s)").arg(root.openCount)
                color: "#a0a0a0"
                font.pixelSize: 11
                Layout.rightMargin: 8
            }
            Button {
                objectName: "cascadeButton"
                text: qsTr("Cascade")
                enabled: root.openCount > 0
                onClicked: root.cascadeWindows()
            }
            Button {
                objectName: "tileButton"
                text: qsTr("Tile")
                enabled: root.openCount > 0
                onClicked: root.tileWindows()
            }
            Button {
                objectName: "closeAllButton"
                text: qsTr("Close all")
                enabled: root.openCount > 0
                onClicked: root.closeAllWindows()
            }
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

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 30
                    color: configHeaderHover.containsMouse ? "#333333" : "transparent"

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        text: (root.configExpanded ? "\u25be  " : "\u25b8  ") + qsTr("Config")
                        color: "#e8e8e8"
                        font.bold: true
                        font.pixelSize: 13
                    }

                    MouseArea {
                        id: configHeaderHover
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: root.configExpanded = !root.configExpanded
                    }
                }

                // One entry per registry domain; each opens its own staged editor.
                Column {
                    id: configList
                    Layout.fillWidth: true
                    Layout.leftMargin: 18
                    visible: root.configExpanded
                    spacing: 0

                    Repeater {
                        model: AppEngine.configDomains

                        delegate: Rectangle {
                            id: configRow
                            required property string domainId
                            required property string title

                            width: configList.width
                            height: 30
                            color: configRowHover.containsMouse ? "#333333" : "transparent"

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                anchors.leftMargin: 10
                                text: configRow.title
                                color: AppEngine.connected ? "#e8e8e8" : "#7a7a7a"
                                font.pixelSize: 13
                            }

                            MouseArea {
                                id: configRowHover
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: root.openConfig(configRow.domainId)
                            }
                        }
                    }
                }

                // Routing: the protocol state and the kernel table, one window
                // per protocol family (owner decision: routing is first-class).
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 30
                    color: routingHeaderHover.containsMouse ? "#333333" : "transparent"

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        text: (root.routingExpanded ? "\u25be  " : "\u25b8  ") + qsTr("Routing")
                        color: "#e8e8e8"
                        font.bold: true
                        font.pixelSize: 13
                    }

                    MouseArea {
                        id: routingHeaderHover
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: root.routingExpanded = !root.routingExpanded
                    }
                }

                Column {
                    id: routingList
                    Layout.fillWidth: true
                    Layout.leftMargin: 18
                    visible: root.routingExpanded
                    spacing: 0

                    Repeater {
                        model: root.routingViews

                        delegate: Rectangle {
                            id: routingRow
                            required property var modelData

                            width: routingList.width
                            height: 30
                            color: routingRowHover.containsMouse ? "#333333" : "transparent"

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                anchors.leftMargin: 10
                                text: routingRow.modelData.title
                                color: AppEngine.connected ? "#e8e8e8" : "#7a7a7a"
                                font.pixelSize: 13
                            }

                            MouseArea {
                                id: routingRowHover
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: root.openRouting(routingRow.modelData.id)
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 30
                    color: toolsHeaderHover.containsMouse ? "#333333" : "transparent"

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        text: (root.toolsExpanded ? "\u25be  " : "\u25b8  ") + qsTr("Tools")
                        color: "#e8e8e8"
                        font.bold: true
                        font.pixelSize: 13
                    }

                    MouseArea {
                        id: toolsHeaderHover
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: root.toolsExpanded = !root.toolsExpanded
                    }
                }

                Column {
                    id: toolsList
                    Layout.fillWidth: true
                    Layout.leftMargin: 18
                    visible: root.toolsExpanded
                    spacing: 0

                    Repeater {
                        model: root.tools

                        delegate: Rectangle {
                            id: toolRow
                            required property var modelData

                            width: toolsList.width
                            height: 30
                            color: toolRowHover.containsMouse ? "#333333" : "transparent"

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                anchors.leftMargin: 10
                                text: toolRow.modelData.title
                                color: AppEngine.connected ? "#e8e8e8" : "#7a7a7a"
                                font.pixelSize: 13
                            }

                            MouseArea {
                                id: toolRowHover
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: root.openTool(toolRow.modelData.id)
                            }
                        }
                    }
                }

                // Plugin entries come from plugins/*/plugin.json; a rejected
                // manifest is shown with its error instead of disappearing.
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 30
                    color: pluginHeaderHover.containsMouse ? "#333333" : "transparent"

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        text: (root.pluginsExpanded ? "\u25be  " : "\u25b8  ") + qsTr("Plugins")
                              + " (" + AppEngine.plugins.count + ")"
                        color: "#e8e8e8"
                        font.bold: true
                        font.pixelSize: 13
                    }

                    MouseArea {
                        id: pluginHeaderHover
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: root.pluginsExpanded = !root.pluginsExpanded
                    }
                }

                Column {
                    id: pluginList
                    Layout.fillWidth: true
                    Layout.leftMargin: 18
                    visible: root.pluginsExpanded
                    spacing: 0

                    Repeater {
                        model: AppEngine.plugins

                        delegate: Rectangle {
                            id: pluginRow
                            required property string pluginId
                            required property string title
                            required property bool available
                            required property string error

                            width: pluginList.width
                            height: 30
                            color: pluginRowHover.containsMouse ? "#333333" : "transparent"

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                anchors.leftMargin: 10
                                width: parent.width - 20
                                text: pluginRow.available ? pluginRow.title
                                                          : pluginRow.title + " — " + pluginRow.error
                                color: pluginRow.available
                                       ? (AppEngine.connected ? "#e8e8e8" : "#7a7a7a")
                                       : "#d97070"
                                font.pixelSize: 13
                                elide: Text.ElideRight
                            }

                            MouseArea {
                                id: pluginRowHover
                                anchors.fill: parent
                                hoverEnabled: true
                                enabled: pluginRow.available
                                onClicked: root.openPlugin(pluginRow.pluginId)
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }

        // The MDI desktop. The board card is the wallpaper until frames cover
        // it; frames live above it and are the only movable children.
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

            Label {
                anchors.centerIn: parent
                anchors.verticalCenterOffset: 120
                visible: AppEngine.connected && root.openCount === 0
                text: qsTr("Open a view from the tree — several can stay open at once")
                color: "#6f6f6f"
                font.pixelSize: 13
            }

            Item {
                id: mdiArea
                objectName: "mdiArea"
                anchors.fill: parent
                clip: true
                property int topZ: 1

                onWidthChanged: root.clampWindows()
                onHeightChanged: root.clampWindows()
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
                root.closeAllWindows()
                return
            }
            connectDialog.close()
            if (!root.startupApplied) {
                root.startupApplied = true
                root.applyStartupWindows()
            }
        }
    }
}
