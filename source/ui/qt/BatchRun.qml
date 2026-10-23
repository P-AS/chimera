// Chimera's Batch Runner: layout and bindings only, over the batchRun view
// model. Games dropped here (or added) run in turn for a number of frames,
// each in the headless runner, and what came of each is listed and dumped.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: win
    objectName: "batchRunWindow"
    required property QtObject vm
    title: qsTr("Batch Runner")
    width: 720
    height: 520

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        Label { text: qsTr("Drag files here (or add them):") }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 140
            border.color: drop.containsDrag ? palette.highlight : palette.mid
            color: "transparent"
            ListView {
                anchors.fill: parent
                anchors.margins: 2
                clip: true
                model: win.vm.rows("files")
                delegate: Label { required property string path; text: path; elide: Text.ElideMiddle; width: ListView.view.width }
            }
            DropArea {
                id: drop
                anchors.fill: parent
                onDropped: (d) => { for (const u of d.urls) win.vm.run("addFile", [u]); d.accept() }
            }
        }
        RowLayout {
            Label { text: qsTr("Number of files: %1").arg(win.vm.fileCount) }
            Button { text: qsTr("Add..."); enabled: !win.vm.running; onClicked: addDialog.open() }
            Button { text: qsTr("Clear!"); enabled: !win.vm.running; onClicked: win.vm.run("clear") }
            Item { Layout.fillWidth: true }
            Label { text: qsTr("Frames to run:") }
            SpinBox { from: 1; to: 1000000; editable: true; value: win.vm.frames; onValueModified: win.vm.frames = value }
            Button { text: qsTr("Go!"); enabled: !win.vm.running; onClicked: win.vm.run("go") }
            Button { text: qsTr("Stop"); enabled: win.vm.running; onClicked: win.vm.run("cancel") }
        }
        ProgressBar { Layout.fillWidth: true; from: 0; to: Math.max(1, win.vm.total); value: win.vm.done }
        Label { text: win.vm.error !== "" ? win.vm.error : win.vm.status }
        ListView {
            id: results
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: win.vm.rows("results")
            ScrollBar.vertical: ScrollBar {}
            delegate: Label {
                required property string file
                required property string status
                required property int frames
                required property int lagFrames
                required property string detail
                width: results.width - 16
                elide: Text.ElideMiddle
                text: qsTr("%1  %2  %3 frames, %4 lag  %5").arg(status).arg(file.replace(/^.*[\\/]/, "")).arg(frames).arg(lagFrames).arg(detail)
            }
        }
        RowLayout {
            Item { Layout.fillWidth: true }
            Button { text: qsTr("Dump..."); enabled: results.count > 0 && !win.vm.running; onClicked: dumpDialog.open() }
        }
    }
    FileDialog {
        id: addDialog
        title: qsTr("Games to run")
        fileMode: FileDialog.OpenFiles
        onAccepted: { for (const f of selectedFiles) win.vm.run("addFile", [f]) }
    }
    FileDialog {
        id: dumpDialog
        title: qsTr("Dump the results")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Text files (*.txt)")]
        onAccepted: win.vm.run("dump", [selectedFile])
    }
}
