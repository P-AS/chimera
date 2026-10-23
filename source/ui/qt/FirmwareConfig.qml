// Chimera's Firmware window (Config > Firmware): every installed core's
// declared firmware and what is on hand for it. Layout and bindings only,
// over the firmware view model.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "firmwareWindow"
    required property QtObject vm
    property var folderOf: null
    title: qsTr("Firmware")
    width: 760
    height: 480
    property int picked: -1
    onVisibleChanged: if (visible) { win.vm.run("refresh"); win.picked = -1 }
    readonly property var marks: ["#2e7d32", "#ef6c00", "#b00020", "#9e9e9e"]

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        Label {
            text: win.vm.missing === 0 ? qsTr("Every installed core has what it declares.")
                                      : qsTr("%1 declared file(s) with nothing that is right on hand.").arg(win.vm.missing)
            font.bold: true
        }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: win.vm.rows("firmware")
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                id: row
                required property int index
                required property string core
                required property string name
                required property int state
                required property string stateText
                required property string path
                required property string how
                width: list.width - 16
                highlighted: row.index === win.picked
                onClicked: win.picked = row.index
                contentItem: RowLayout {
                    Rectangle { width: 12; height: 12; radius: 6; color: win.marks[row.state] }
                    Label { text: row.core; Layout.preferredWidth: 140; elide: Text.ElideRight }
                    Label { text: row.name; Layout.preferredWidth: 180; elide: Text.ElideRight }
                    Label { text: row.stateText + (row.how !== "" ? " (" + row.how + ")" : ""); Layout.preferredWidth: 200; elide: Text.ElideRight }
                    Label { text: row.path; Layout.fillWidth: true; elide: Text.ElideMiddle; opacity: 0.75 }
                }
            }
        }
        RowLayout {
            Button { text: qsTr("Set..."); enabled: win.picked >= 0; onClicked: fileDialog.open() }
            Button { text: qsTr("Clear"); enabled: win.picked >= 0; onClicked: win.vm.run("clearFile", [win.picked]) }
            Button { text: qsTr("Scan Folder..."); onClicked: folderDialog.open() }
            CheckBox { id: subfolders; text: qsTr("Scan sub-folders too"); checked: true }
            Item { Layout.fillWidth: true }
            Button { text: qsTr("Refresh"); onClicked: win.vm.run("refresh") }
            Button { text: qsTr("Close"); onClicked: win.close() }
        }
        Label {
            text: win.vm.error !== "" ? win.vm.error : win.vm.status
            color: win.vm.error !== "" ? "#b00020" : palette.text
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
    }
    FileDialog {
        id: fileDialog
        title: qsTr("Choose the firmware file")
        currentFolder: win.folderOf ? win.folderOf("Firmware") : ""
        onAccepted: win.vm.run("setFile", [win.picked, selectedFile])
    }
    FolderDialog {
        id: folderDialog
        title: qsTr("Scan a folder for firmware files")
        currentFolder: win.folderOf ? win.folderOf("Firmware") : ""
        onAccepted: win.vm.run("scanFolder", [selectedFolder, subfolders.checked])
    }
}
