// Chimera's BatchRun (Tools > Batch Runner), laid out as its form: files
// dropped into the list run in turn for a number of frames (0 loads each
// and runs none), each in the headless runner; Clear! empties the list (a
// run under way goes on with the games it started with), Go! starts, the
// status and a bar say how far it is, and Dump... writes what came of each
// game into a file, starting in the Tools folder. Chimera's own messages:
// "No files!", "Old one still running!", "No results to save!", and the
// window does not close while a run is under way. Layout and bindings
// only, over the batchRun view model.
//
// Beyond Chimera's: a right-click on the list adds files through a file
// dialog, for a desktop that cannot drag files.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: win
    objectName: "batchRunWindow"
    required property QtObject vm
    property var folderOf: null
    title: qsTr("BatchRun")
    width: 320
    height: 360
    minimumWidth: 300
    minimumHeight: 330

    function say(text) { told.text = text; told.open() }
    // a command whose refusal Chimera shows in a message box
    function run(command, args) {
        win.vm.run(command, args || [])
        if (win.vm.error !== "") win.say(win.vm.error)
    }
    onClosing: (close) => {
        if (win.vm.running) {
            close.accepted = false
            win.say(qsTr("Can't close while task is running!"))
        }
    }

    MessageDialog { id: told; buttons: MessageDialog.Ok }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 4
        Label { text: qsTr("Drag Files Here:") }
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 120
            color: palette.base
            border.color: drop.containsDrag ? palette.highlight : palette.mid
            ListView {
                id: files
                objectName: "batchFiles"
                anchors.fill: parent
                anchors.margins: 2
                clip: true
                model: win.vm.rows("files")
                ScrollBar.vertical: ScrollBar {}
                delegate: Label {
                    required property string path
                    text: path
                    elide: Text.ElideMiddle
                    width: ListView.view.width
                    leftPadding: 2
                }
            }
            DropArea {
                id: drop
                anchors.fill: parent
                onDropped: (d) => { for (const u of d.urls) win.vm.run("addFile", [u]); d.accept() }
            }
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: listMenu.popup()
            }
            Menu {
                id: listMenu
                MenuItem { text: qsTr("Add Files..."); onTriggered: addDialog.open() }
            }
        }
        GridLayout {
            columns: 2
            columnSpacing: 12
            rowSpacing: 4
            Label { text: qsTr("Number of files: %1").arg(win.vm.fileCount); Layout.row: 0; Layout.column: 0; Layout.fillWidth: true }
            Label { text: qsTr("Frames to run:"); Layout.row: 0; Layout.column: 1 }
            Button { text: qsTr("Clear!"); Layout.row: 1; Layout.column: 0; Layout.preferredWidth: 84; onClicked: win.vm.run("clear") }
            SpinBox {
                Layout.row: 1
                Layout.column: 1
                Layout.preferredWidth: 130
                from: 0
                to: 1000
                editable: true
                value: Math.min(win.vm.frames, 1000)
                onValueModified: win.vm.frames = value
            }
        }
        RowLayout {
            Button { text: qsTr("Go!"); Layout.preferredWidth: 84; onClicked: win.run("go") }
            Button {
                text: qsTr("Dump...")
                Layout.preferredWidth: 84
                onClicked: {
                    if (win.vm.rows("results").rowCount() === 0) win.say(qsTr("No results to save!"))
                    else dumpDialog.open()
                }
            }
        }
        Label { text: win.vm.status }
        ProgressBar { Layout.fillWidth: true; from: 0; to: Math.max(1, win.vm.total); value: win.vm.done }
    }
    FileDialog {
        id: addDialog
        fileMode: FileDialog.OpenFiles
        onAccepted: { for (const f of selectedFiles) win.vm.run("addFile", [f]) }
    }
    FileDialog {
        id: dumpDialog
        fileMode: FileDialog.SaveFile
        currentFolder: win.folderOf ? win.folderOf("Tools") : ""
        onAccepted: win.run("dump", [selectedFile])
    }
}
