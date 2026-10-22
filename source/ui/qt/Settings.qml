// A machine's settings, one row each, as its package declares them: the
// control follows the declared type. Layout and bindings only, over the
// settings view model.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "settingsWindow"
    required property QtObject settings
    title: qsTr("Settings")
    width: 620
    height: 600

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        Label { text: win.settings.status }

        SettingsList {
            vm: win.settings
            Layout.fillWidth: true
            Layout.fillHeight: true
        }

        Label { text: win.settings.error; color: "#b00020"; visible: text !== "" }
        RowLayout {
            Item { Layout.fillWidth: true }
            Button { text: qsTr("Discard"); enabled: win.settings.discardEnabled; onClicked: win.settings.run("discard") }
            Button { text: qsTr("Apply"); enabled: win.settings.applyEnabled; onClicked: win.settings.run("apply") }
        }
    }
}
