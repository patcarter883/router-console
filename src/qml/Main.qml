// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later

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

    // Tree state and the tool windows it opens (one per interface, one per
    // config domain, one per tool-tier window).
    property bool interfacesExpanded: true
    property bool configExpanded: true
    property bool pluginsExpanded: true
    property bool startupWindowOpened: false
    property bool startupConfigOpened: false
    property bool startupPluginOpened: false
    property var interfaceWindows: []
    property var configWindows: []
    property var pluginWindows: []
    property bool routingExpanded: true
    property bool startupRoutingOpened: false
    property var routingWindows: []
    readonly property var routingViews: [
        { id: "routes", title: qsTr("Routes") },
        { id: "ospf", title: qsTr("OSPF") },
        { id: "bgp", title: qsTr("BGP") }
    ]
    property bool toolsExpanded: true
    property bool startupToolOpened: false
    property var toolWindows: []
    readonly property var tools: [
        { id: "ping", title: qsTr("Ping") },
        { id: "traceroute", title: qsTr("Traceroute") },
        { id: "portscan", title: qsTr("Port Scan") },
        { id: "torch", title: qsTr("Torch") },
        { id: "files", title: qsTr("Files") },
        { id: "sqm", title: qsTr("SQM / CAKE stats") }
    ]
    readonly property string startupInterface:
        typeof startupInterfaceName === "undefined" ? "" : startupInterfaceName
    readonly property string startupConfig:
        typeof startupConfigDomain === "undefined" ? "" : startupConfigDomain
    readonly property string startupConfigSectionName:
        typeof startupConfigSection === "undefined" ? "" : startupConfigSection
    readonly property string startupRouting:
        typeof startupRoutingId === "undefined" ? "" : startupRoutingId
    readonly property string startupTool:
        typeof startupToolId === "undefined" ? "" : startupToolId
    readonly property string startupStage:
        typeof startupStageValue === "undefined" ? "" : startupStageValue
    readonly property string startupPlugin:
        typeof startupPluginId === "undefined" ? "" : startupPluginId

    function openConfig(domainId, initialSection) {
        for (let i = 0; i < configWindows.length; ++i) {
            if (configWindows[i].domainId === domainId) {
                configWindows[i].requestActivate()
                return configWindows[i]
            }
        }
        const window = configWindowComponent.createObject(root, {
            domainId: domainId,
            initialSection: initialSection || ""
        })
        if (!window)
            return null
        configWindows.push(window)
        window.closing.connect(function () {
            const index = root.configWindows.indexOf(window)
            if (index >= 0)
                root.configWindows.splice(index, 1)
        })
        return window
    }

    function closeConfigWindows() {
        while (configWindows.length > 0)
            configWindows.pop().close()
    }

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

    function openPlugin(pluginId) {
        for (let i = 0; i < pluginWindows.length; ++i) {
            if (pluginWindows[i].pluginId === pluginId) {
                pluginWindows[i].requestActivate()
                return
            }
        }
        const window = pluginWindowComponent.createObject(root, { pluginId: pluginId })
        if (!window)
            return
        pluginWindows.push(window)
        window.closing.connect(function () {
            const index = root.pluginWindows.indexOf(window)
            if (index >= 0)
                root.pluginWindows.splice(index, 1)
        })
    }

    function closePluginWindows() {
        while (pluginWindows.length > 0)
            pluginWindows.pop().close()
    }

    Component {
        id: pluginWindowComponent
        PluginWindow {}
    }

    // One window per tool id; reopening focuses the existing one.
    function openTool(toolId) {
        for (let i = 0; i < toolWindows.length; ++i) {
            if (toolWindows[i].toolId === toolId) {
                toolWindows[i].requestActivate()
                return toolWindows[i]
            }
        }
        const component = toolComponent(toolId)
        if (!component)
            return null
        const window = component.createObject(root, { toolId: toolId })
        if (!window)
            return null
        toolWindows.push(window)
        window.closing.connect(function () {
            const index = root.toolWindows.indexOf(window)
            if (index >= 0)
                root.toolWindows.splice(index, 1)
        })
        return window
    }

    // One window per routing view; reopening focuses the existing one.
    function openRouting(viewId) {
        for (let i = 0; i < routingWindows.length; ++i) {
            if (routingWindows[i].viewId === viewId) {
                routingWindows[i].requestActivate()
                return routingWindows[i]
            }
        }
        const component = routingComponent(viewId)
        if (!component)
            return null
        const window = component.createObject(root, { viewId: viewId })
        if (!window)
            return null
        routingWindows.push(window)
        window.closing.connect(function () {
            const index = root.routingWindows.indexOf(window)
            if (index >= 0)
                root.routingWindows.splice(index, 1)
        })
        return window
    }

    function routingComponent(viewId) {
        switch (viewId) {
        case "routes": return routesWindowComponent
        case "ospf": return ospfWindowComponent
        case "bgp": return bgpWindowComponent
        }
        return null
    }

    function closeRoutingWindows() {
        while (routingWindows.length > 0)
            routingWindows.pop().close()
    }

    Component { id: routesWindowComponent; RoutesWindow {} }
    Component { id: ospfWindowComponent; OspfWindow {} }
    Component { id: bgpWindowComponent; BgpWindow {} }

    function toolComponent(toolId) {
        switch (toolId) {
        case "ping": return pingWindowComponent
        case "traceroute": return tracerouteWindowComponent
        case "portscan": return portScanWindowComponent
        case "torch": return torchWindowComponent
        case "files": return filesWindowComponent
        case "sqm": return sqmStatsWindowComponent
        }
        return null
    }

    function closeToolWindows() {
        while (toolWindows.length > 0)
            toolWindows.pop().close()
    }

    Component { id: pingWindowComponent; PingWindow {} }
    Component { id: tracerouteWindowComponent; TracerouteWindow {} }
    Component { id: portScanWindowComponent; PortScanWindow {} }
    Component { id: torchWindowComponent; TorchWindow {} }
    Component { id: filesWindowComponent; FilesWindow {} }
    Component { id: sqmStatsWindowComponent; SqmStatsWindow {} }

    Component {
        id: interfaceWindowComponent
        InterfaceWindow {}
    }

    Component {
        id: configWindowComponent
        ConfigWindow {}
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
                root.closeConfigWindows()
                root.closePluginWindows()
                root.closeRoutingWindows()
                root.closeToolWindows()
                return
            }
            connectDialog.close()
            if (root.startupInterface.length > 0 && !root.startupWindowOpened) {
                root.startupWindowOpened = true
                root.openInterface(root.startupInterface)
            }
            if (root.startupPlugin.length > 0 && !root.startupPluginOpened) {
                root.startupPluginOpened = true
                root.openPlugin(root.startupPlugin)
            }
            if (root.startupConfig.length > 0 && !root.startupConfigOpened) {
                root.startupConfigOpened = true
                const window = root.openConfig(root.startupConfig, root.startupConfigSectionName)
                if (window && root.startupStage.length > 0)
                    window.stageAfterLoad = root.startupStage
            }
            if (root.startupRouting.length > 0 && !root.startupRoutingOpened) {
                root.startupRoutingOpened = true
                root.openRouting(root.startupRouting)
            }
            if (root.startupTool.length > 0 && !root.startupToolOpened) {
                root.startupToolOpened = true
                root.openTool(root.startupTool)
            }
        }
    }
}
