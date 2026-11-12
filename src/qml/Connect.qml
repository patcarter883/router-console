import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RouterConsole

// Loader window: host + credentials, remembered hosts (milestone 1).
Dialog {
    modal: true
    title: qsTr("Connect")
    standardButtons: Dialog.Ok | Dialog.Cancel

    GridLayout {
        columns: 2
        Label { text: qsTr("Host") }
        TextField { id: host; placeholderText: "192.168.1.1" }
        Label { text: qsTr("Username") }
        TextField { id: username; text: "root" }
        Label { text: qsTr("Password") }
        TextField { id: password; echoMode: TextInput.Password }
    }

    onAccepted: AppEngine.connectTo(host.text, username.text, password.text)
}
