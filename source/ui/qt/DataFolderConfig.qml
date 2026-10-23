// Chimera's Data Directory (Config > Data Directory): layout and bindings
// only, over the dataFolder view model. A change is recorded here and done
// when the window next starts.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "dataFolderWindow"
    required property QtObject vm
    title: qsTr("Data Directory")
    width: 600
    height: 320

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        Label {
            text: qsTr("Everything the window keeps for you is in one folder: each project's cache and the unsaved work kept against a crash. It can be moved off the system drive.")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Label { text: qsTr("In use: %1").arg(win.vm.current !== "" ? win.vm.current : win.vm.standard + qsTr(" (the system's place)")); wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
        Label {
            visible: win.vm.pending !== ""
            text: qsTr("At the next start: %1%2").arg(win.vm.pending).arg(win.vm.pendingMove ? qsTr(", what is there moved with it") : qsTr(", what is there left where it is"))
            wrapMode: Text.WrapAnywhere
            Layout.fillWidth: true
            font.bold: true
        }
        CheckBox { id: carry; text: qsTr("Move what is there now to the new folder"); checked: true }
        RowLayout {
            Button { text: qsTr("Choose a Folder..."); onClicked: folderDialog.open() }
            Button { text: qsTr("The System's Place"); onClicked: win.vm.run("request", ["", carry.checked]) }
            Button { text: qsTr("Withdraw"); enabled: win.vm.pending !== ""; onClicked: win.vm.run("withdraw") }
        }
        Label {
            text: win.vm.error !== "" ? win.vm.error : win.vm.status
            color: win.vm.error !== "" ? "#b00020" : palette.text
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Item { Layout.fillHeight: true }
        RowLayout {
            Item { Layout.fillWidth: true }
            Button { text: qsTr("Close"); onClicked: win.close() }
        }
    }
    FolderDialog {
        id: folderDialog
        title: qsTr("Where the data goes")
        onAccepted: win.vm.run("request", [selectedFolder, carry.checked])
    }
}
