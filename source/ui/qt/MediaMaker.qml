// Chimera's Reproducible Media Maker (Tools), laid out as its form: what it
// is for at the top; Folder (read-only) with Browse..., Format with what
// the shape is for under it, what an installed core's recipe recognised the
// folder as, Save as with its own Save as...; the bar, the file being
// written, what came of it (the file, its SHA1 and size, or why not), and
// Make (Cancel while packing) and Close. A dialog of the main window, as
// Chimera's; Escape closes it, and closing stops a pack under way. Layout
// and bindings only, over the mediaMaker view model.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: win
    objectName: "mediaMakerWindow"
    required property QtObject vm
    property Window mainWindow: null
    title: qsTr("Reproducible Media Maker")
    width: 600
    height: 380
    minimumWidth: 600
    maximumWidth: 600
    minimumHeight: 380
    maximumHeight: 380
    modality: Qt.ApplicationModal
    transientParent: win.mainWindow
    flags: Qt.Dialog
    onClosing: if (win.vm.running) win.vm.run("cancel")
    // a path as a file dialog takes it
    function urlOf(path) { return path === "" ? "" : path.startsWith("/") ? "file://" + path : "file:///" + path.replace(/\\/g, "/") }
    readonly property var extensions: [".zip", ".iso", ".img"]

    Shortcut { sequence: "Escape"; onActivated: win.close() }

    component Caption: Label { Layout.preferredWidth: 80 }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 6
        Label {
            text: qsTr("A folder becomes one file, and the same folder becomes the same file on any machine - the dates, the permissions and the order it was read in are left out on purpose. That is what lets a project name it by hash.")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Layout.preferredHeight: 50
            verticalAlignment: Text.AlignTop
        }
        GridLayout {
            columns: 3
            columnSpacing: 0
            rowSpacing: 6
            Layout.fillWidth: true
            Caption { text: qsTr("Folder"); Layout.row: 0; Layout.column: 0 }
            TextField { text: win.vm.folder; readOnly: true; Layout.row: 0; Layout.column: 1; Layout.fillWidth: true }
            Button { text: qsTr("Browse..."); Layout.row: 0; Layout.column: 2; Layout.leftMargin: 8; Layout.preferredWidth: 96; onClicked: folderDialog.open() }
            Caption { text: qsTr("Format"); Layout.row: 1; Layout.column: 0 }
            ComboBox {
                Layout.row: 1
                Layout.column: 1
                Layout.fillWidth: true
                model: win.vm.run("shapes").split("\n")
                currentIndex: win.vm.format
                onActivated: (i) => win.vm.format = i
            }
            Label {
                text: win.vm.formatHelp
                wrapMode: Text.WordWrap
                Layout.row: 2
                Layout.column: 1
                Layout.fillWidth: true
                Layout.preferredHeight: 36
                verticalAlignment: Text.AlignTop
            }
            // what a core's recipe recognised the folder as; empty for most folders
            Label {
                text: win.vm.recognised
                wrapMode: Text.WordWrap
                Layout.row: 3
                Layout.column: 1
                Layout.fillWidth: true
                Layout.preferredHeight: 36
                verticalAlignment: Text.AlignTop
            }
            Caption { text: qsTr("Save as"); Layout.row: 4; Layout.column: 0 }
            TextField { text: win.vm.output; onEditingFinished: win.vm.output = text; Layout.row: 4; Layout.column: 1; Layout.fillWidth: true }
            Button { text: qsTr("Save as..."); Layout.row: 4; Layout.column: 2; Layout.leftMargin: 8; Layout.preferredWidth: 96; onClicked: outputDialog.open() }
        }
        ProgressBar { Layout.fillWidth: true; Layout.topMargin: 6; from: 0; to: 1; value: win.vm.progress }
        Label { text: win.vm.current; elide: Text.ElideRight; Layout.fillWidth: true; Layout.preferredHeight: 18 }
        Label {
            objectName: "mediaResult"
            text: win.vm.error !== "" ? win.vm.error : win.vm.result
            wrapMode: Text.WrapAnywhere
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            verticalAlignment: Text.AlignTop
        }
        Item { Layout.fillHeight: true }
        RowLayout {
            Item { Layout.fillWidth: true }
            Button {
                text: win.vm.running ? qsTr("Cancel") : qsTr("Make")
                Layout.preferredWidth: 96
                enabled: win.vm.running || (win.vm.folder !== "" && win.vm.output !== "")
                onClicked: win.vm.run(win.vm.running ? "cancel" : "make")
            }
            Button { text: qsTr("Close"); Layout.preferredWidth: 96; onClicked: win.close() }
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
        selectedFile: win.urlOf(win.vm.output)
        defaultSuffix: win.extensions[win.vm.format].substring(1)
        nameFilters: [win.vm.run("shapes").split("\n")[win.vm.format] + " (*" + win.extensions[win.vm.format] + ")", qsTr("All Files (*)")]
        onAccepted: win.vm.run("setOutput", [selectedFile])
    }
}
