// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RouterConsole

// One MDI child frame: title bar (drag + close), a resizable body, and the view
// it hosts. The body is the existing view component, unchanged apart from being
// rooted on an Item; the frame owns position, size and z-order and persists the
// geometry under the view's stable windowKey.
Rectangle {
    id: frame

    property string windowKey: ""
    property string fallbackTitle: ""
    property Component contentComponent
    property var contentProperties: ({})
    property rect initialRect: Qt.rect(24, 24, 760, 520)
    property int minimumFrameWidth: 240
    property int minimumFrameHeight: 160
    property Item content: null
    property bool active: false

    signal closed(string windowKey)

    readonly property string titleText:
        content && content.windowTitle !== undefined && content.windowTitle.length > 0
        ? content.windowTitle : fallbackTitle
    readonly property int titleBarHeight: 26

    color: "#2b2b2b"
    border.width: 1
    border.color: frame.active ? "#5b87ad" : "#3f3f3f"
    clip: true

    function raise() {
        if (!parent)
            return
        const siblings = parent.children
        for (let i = 0; i < siblings.length; ++i) {
            if (siblings[i] !== frame && siblings[i].active !== undefined)
                siblings[i].active = false
        }
        if (parent.topZ !== undefined) {
            parent.topZ += 1
            frame.z = parent.topZ
        }
        frame.active = true
    }

    function applyRect(rect) {
        frame.x = rect.x
        frame.y = rect.y
        frame.width = rect.width
        frame.height = rect.height
        frame.persist()
        frame.raise()
    }

    function persist() {
        if (frame.windowKey.length === 0)
            return
        MdiGeometry.save(frame.windowKey, Qt.rect(frame.x, frame.y, frame.width, frame.height))
    }

    function closeFrame() {
        frame.persist()
        if (frame.content && frame.content.releaseResources)
            frame.content.releaseResources()
        frame.closed(frame.windowKey)
        frame.destroy()
    }

    function adoptContent() {
        if (!frame.contentComponent)
            return
        frame.content = frame.contentComponent.createObject(contentArea,
                                                           frame.contentProperties || ({}))
        if (!frame.content)
            return
        frame.content.width = Qt.binding(function () { return contentArea.width })
        frame.content.height = Qt.binding(function () { return contentArea.height })
        if (frame.content.minimumWidth !== undefined)
            frame.minimumFrameWidth = Math.max(frame.minimumFrameWidth, frame.content.minimumWidth)
        if (frame.content.minimumHeight !== undefined)
            frame.minimumFrameHeight = Math.max(frame.minimumFrameHeight, frame.content.minimumHeight)
    }

    // The view is built first so the frame can fall back to the view's own
    // preferred size and minimum size before asking the store for geometry.
    Component.onCompleted: {
        frame.adoptContent()
        const fallback = Qt.rect(frame.initialRect.x, frame.initialRect.y,
                                 frame.initialRect.width > 0
                                 ? frame.initialRect.width
                                 : (frame.content ? frame.content.implicitWidth : 760),
                                 frame.initialRect.height > 0
                                 ? frame.initialRect.height
                                 : (frame.content ? frame.content.implicitHeight : 520))
        const saved = MdiGeometry.load(frame.windowKey, fallback)
        frame.x = saved.x
        frame.y = saved.y
        frame.width = Math.max(frame.minimumFrameWidth, saved.width)
        frame.height = Math.max(frame.minimumFrameHeight, saved.height)
        frame.raise()
    }

    Rectangle {
        id: titleBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: frame.titleBarHeight
        color: frame.active ? "#3a5a78" : "#333333"

        Label {
            anchors.left: parent.left
            anchors.right: closeButton.left
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 8
            anchors.rightMargin: 6
            text: frame.titleText
            color: "#e8e8e8"
            font.pixelSize: 12
            font.bold: true
            elide: Text.ElideRight
        }

        // Raised on press so a click anywhere on the chrome brings the window
        // forward; the body keeps its own input untouched.
        MouseArea {
            id: dragArea
            anchors.fill: parent
            drag.target: frame
            drag.minimumX: 0
            drag.maximumX: frame.parent ? Math.max(0, frame.parent.width - frame.width) : 100000
            drag.minimumY: 0
            drag.maximumY: frame.parent ? Math.max(0, frame.parent.height - frame.titleBarHeight) : 100000
            drag.smoothed: false
            onPressed: frame.raise()
            onReleased: frame.persist()
        }

        Button {
            id: closeButton
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 26
            flat: true
            text: "\u2715"
            font.pixelSize: 11
            onClicked: frame.closeFrame()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Close window")
        }
    }

    Rectangle {
        id: contentArea
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: titleBar.bottom
        anchors.bottom: parent.bottom
        anchors.margins: 1
        color: "#1e1e1e"
        clip: true
    }

    // Resize grip: bottom-right corner, the only resize handle (WinBox drags
    // any edge; the corner is what this shell exposes).
    Rectangle {
        width: 14
        height: 14
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        color: resizeHover.hovered ? "#5b87ad" : "#3f3f3f"

        HoverHandler {
            id: resizeHover
            cursorShape: Qt.SizeFDiagCursor
        }

        MouseArea {
            anchors.fill: parent
            property real startWidth: 0
            property real startHeight: 0
            property real startX: 0
            property real startY: 0
            cursorShape: Qt.SizeFDiagCursor
            onPressed: {
                frame.raise()
                startWidth = frame.width
                startHeight = frame.height
                startX = mouse.x
                startY = mouse.y
            }
            onPositionChanged: {
                const maxWidth = frame.parent ? frame.parent.width - frame.x : frame.width
                const maxHeight = frame.parent ? frame.parent.height - frame.y : frame.height
                frame.width = Math.min(Math.max(frame.minimumFrameWidth,
                                                startWidth + mouse.x - startX), maxWidth)
                frame.height = Math.min(Math.max(frame.minimumFrameHeight,
                                                 startHeight + mouse.y - startY), maxHeight)
            }
            onReleased: frame.persist()
        }
    }

    Component.onDestruction: frame.persist()
}
