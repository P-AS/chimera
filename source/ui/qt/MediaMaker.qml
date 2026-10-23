// Chimera's Reproducible Media Maker: layout and bindings only, over the
// mediaMaker view model. A folder becomes one file that is the same on
// anybody's machine, so a game dumped as a folder can be named in a project.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: win
    objectName: "mediaMakerWindow"
    required property QtObject vm
    title: qsTr("Reproducible Media Maker")
    width: 600
    height: 360

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        Label {
            text: qsTr("Turns a folder into one file that is the same on anybody's machine, so a game that came as a folder can be part of a project.")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        GridLayout {
            columns: 3
            Layout.fillWidth: true
            Label { text: qsTr("Folder:") }
            TextField { text: win.vm.folder; readOnly: true; Layout.fillWidth: true }
            Button { text: qsTr("Browse..."); enabled: !win.vm.running; onClicked: folderDialog.open() }
            Label { text: qsTr("Shape:") }
            ComboBox {
                Layout.fillWidth: true
                model: win.vm.run("shapes").split("\n")
                currentIndex: win.vm.format
                enabled: !win.vm.running
                onActivated: (i) => win.vm.format = i
            }
            Item { width: 1 }
            Item { width: 1 }
            Label { text: win.vm.formatHelp; wrapMode: Text.WordWrap; Layout.fillWidth: true; opacity: 0.75 }
            Item { width: 1 }
            Label { text: qsTr("Write to:") }
            TextField { text: win.vm.output; onEditingFinished: win.vm.output = text; Layout.fillWidth: true; enabled: !win.vm.running }
            Button { text: qsTr("Save as..."); enabled: !win.vm.running; onClicked: outputDialog.open() }
        }
        Label { text: win.vm.recognised; visible: text !== ""; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.bold: true }
        ProgressBar { Layout.fillWidth: true; from: 0; to: 1; value: win.vm.progress; visible: win.vm.running }
        Label { text: win.vm.current; elide: Text.ElideMiddle; Layout.fillWidth: true }
        TextArea { text: win.vm.result; readOnly: true; wrapMode: TextEdit.Wrap; Layout.fillWidth: true; visible: text !== ""; font.family: "monospace" }
        Label { text: win.vm.error; color: "#b00020"; visible: text !== "" }
        Item { Layout.fillHeight: true }
        RowLayout {
            Item { Layout.fillWidth: true }
            Button {
                text: win.vm.running ? qsTr("Cancel") : qsTr("Make")
                enabled: win.vm.running || (win.vm.folder !== "" && win.vm.output !== "")
                onClicked: win.vm.run(win.vm.running ? "cancel" : "make")
            }
            Button { text: qsTr("Close"); onClicked: win.close() }
        }
    }
    FolderDialog {
        id: folderDialog
        title: qsTr("The folder to pack")
        onAccepted: win.vm.run("setFolder", [selectedFolder])
    }
    FileDialog {
        id: outputDialog
        title: qsTr("Write the image to")
        fileMode: FileDialog.SaveFile
        onAccepted: win.vm.run("setOutput", [selectedFile])
    }
}
