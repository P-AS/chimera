// Chimera's File > Project Settings (issue 370): the open project's settings,
// changed after it was made. The settings the New Project window shows for
// the project's files, the renderer among them, and when the machine's clock
// starts; not the machine, which goes with the project's files. Restart with
// these asks once more, then the machine starts again at frame 0 with them.
// Layout and bindings only, over the settings view model.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "projectSettingsWindow"
    required property QtObject vm
    property var themeColor: (role, fallback) => fallback
    property Window mainWindow: null
    title: qsTr("Project Settings")
    width: 600
    height: 520
    modality: Qt.ApplicationModal
    transientParent: win.mainWindow
    // opened again: what the machine was made with, not what was left last time
    onVisibleChanged: if (visible) win.vm.run("reset")

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: qsTr("A change restarts the machine at frame 0 with the new settings. The greenzone is cleared;"
                + " the inputs are kept and play again, but they may not do what they did before."
                + " The machine itself goes with the project's files and is not changed here.")
        }
        SettingsList {
            vm: win.vm
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
        // the machine's own, not a core's setting: under the settings, as in New Project
        RowLayout {
            Label { text: qsTr("The machine's clock starts at:") }
            TextField {
                objectName: "projectClockStart"
                text: win.vm.clockStart
                Layout.preferredWidth: 160
                onTextEdited: win.vm.clockStart = text
            }
            Label { text: qsTr("UTC") }
        }
        RowLayout {
            Label {
                Layout.fillWidth: true
                elide: Text.ElideRight
                text: win.vm.restartNote
                color: win.vm.restartNote.startsWith(qsTr("The clock")) ? win.themeColor("AccentError", "#b00020") : palette.windowText
            }
            Button {
                text: qsTr("Restart with these")
                enabled: win.vm.canRestart
                onClicked: restartQuestion.open()
            }
            Button { text: qsTr("Cancel"); onClicked: win.close() }
        }
    }
    Dialog {
        id: restartQuestion
        title: qsTr("Project Settings")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        Label {
            width: 420
            wrapMode: Text.Wrap
            text: qsTr("Restart the machine with the new settings?\n\nThe greenzone is cleared and the project goes back to frame 0."
                + " The inputs are kept and play again, but with other settings they may not do what they did before.")
        }
        onAccepted: { win.vm.run("restartWith"); win.close() }
    }
}
