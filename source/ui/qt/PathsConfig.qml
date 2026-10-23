// Chimera's Paths window (Config > Paths): layout and bindings only, over
// the paths view model. A path is absolute or relative to the base folder;
// each is kept the moment it is changed.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "pathsWindow"
    required property QtObject vm
    title: qsTr("Path Configuration")
    width: 720
    height: 520
    property string browsing: ""

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        RowLayout {
            Label { text: qsTr("Base") }
            TextField {
                Layout.fillWidth: true
                text: win.vm.base
                placeholderText: win.vm.programFolder
                onEditingFinished: win.vm.base = text
            }
            Button { text: qsTr("Browse..."); onClicked: { win.browsing = ""; folderDialog.open() } }
        }
        Label { text: qsTr("Paths starting with . are relative to the base, the program's own folder when it is empty."); opacity: 0.75; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: win.vm.rows("paths")
            ScrollBar.vertical: ScrollBar {}
            delegate: RowLayout {
                id: row
                required property string name
                required property string path
                required property string folder
                required property bool isDefault
                width: list.width - 16
                Label { text: row.name; Layout.preferredWidth: 120; font.bold: !row.isDefault }
                TextField {
                    Layout.fillWidth: true
                    text: row.path
                    ToolTip.visible: hovered
                    ToolTip.text: row.folder
                    onEditingFinished: if (text !== row.path) win.vm.run("setPath", [row.name, text])
                }
                Button { text: qsTr("Browse..."); onClicked: { win.browsing = row.name; folderDialog.currentFolder = "file://" + row.folder; folderDialog.open() } }
                Button { text: qsTr("Default"); enabled: !row.isDefault; onClicked: win.vm.run("setPath", [row.name, ""]) }
            }
        }
        RowLayout {
            CheckBox { text: qsTr("Use recent for ROMs"); checked: win.vm.recentForRoms; onToggled: win.vm.recentForRoms = checked }
            Item { Layout.fillWidth: true }
            Label { text: win.vm.error; color: "#b00020"; elide: Text.ElideRight }
            Button { text: qsTr("All to Default"); onClicked: win.vm.run("resetAll") }
            Button { text: qsTr("Close"); onClicked: win.close() }
        }
    }
    FolderDialog {
        id: folderDialog
        onAccepted: {
            const local = decodeURIComponent(String(selectedFolder).replace(/^file:\/\/(?=\/[A-Za-z]:)/, "").replace(/^file:\/\//, ""))
            if (win.browsing === "") win.vm.base = local
            else win.vm.run("setPath", [win.browsing, local])
        }
    }
}
