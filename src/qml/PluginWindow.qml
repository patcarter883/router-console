import QtQuick
import QtQuick.Controls
import RouterConsole

// Hosts one plugin's QML, loaded from its own file under plugins/<id>/ — the
// view is not part of this application's QML module, so a new plugin needs no
// core file. The plugin view gets the session as its `plugin` property.
Rectangle {
    id: win

    property string pluginId: ""
    property QtObject session: null

    implicitWidth: 760
    implicitHeight: 560
    property int minimumWidth: 560
    property int minimumHeight: 360
    property string windowTitle: session ? session.title : pluginId
    color: "#1e1e1e"

    Component.onCompleted: session = AppEngine.openPlugin(pluginId)

    // Polling stops with the window; reopening resumes from the engine's cache.
    function releaseResources() { if (session) session.stop() }

    Loader {
        id: viewLoader
        anchors.fill: parent
        anchors.margins: 4
        source: win.session ? win.session.viewUrl : ""
        onLoaded: if (item)
                      item.plugin = win.session
    }

    Label {
        anchors.centerIn: parent
        width: parent.width - 48
        visible: win.session === null
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: qsTr("Plugin '%1' has no data session: not connected, or its manifest was rejected.").arg(win.pluginId)
        color: "#c8c8c8"
    }

    Label {
        anchors.centerIn: parent
        width: parent.width - 48
        visible: win.session !== null && viewLoader.status === Loader.Error
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: qsTr("Plugin view failed to load: %1").arg(viewLoader.source)
        color: "#d97070"
    }
}
