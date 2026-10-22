// The installed cores and where firmware is looked for: layout and bindings
// only, over the cores view model.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "coresWindow"
    required property QtObject vm
    title: qsTr("Cores")
    width: 700
    height: 520

    FolderDialog { id: coresDialog; onAccepted: win.vm.coresFolder = selectedFolder.toString().replace(/^file:\/\//, "") }
    FolderDialog { id: firmwareDialog; onAccepted: win.vm.firmwareFolder = selectedFolder.toString().replace(/^file:\/\//, "") }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        GridLayout {
            columns: 3
            Label { text: qsTr("Cores folder") }
            TextField { text: win.vm.coresFolder; readOnly: true; Layout.fillWidth: true }
            Button { text: qsTr("Choose..."); onClicked: coresDialog.open() }
            Label { text: qsTr("Firmware folder") }
            TextField { text: win.vm.firmwareFolder; readOnly: true; Layout.fillWidth: true }
            Button { text: qsTr("Choose..."); onClicked: firmwareDialog.open() }
        }
        RowLayout {
            Label { text: win.vm.status; Layout.fillWidth: true }
            Button { text: qsTr("Look Again"); onClicked: win.vm.run("refresh") }
        }
        HorizontalHeaderView { syncView: table; Layout.fillWidth: true; clip: true }
        TableView {
            id: table
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: win.vm.rows("cores")
            columnWidthProvider: (c) => [180, 120, 180, table.width - 494][c]
            ScrollBar.vertical: ScrollBar {}
            delegate: Label { required property var display; text: display === undefined ? "" : display; padding: 3; elide: Text.ElideRight }
        }
        Label {
            text: qsTr("Chimera downloads nothing: a core is a .chimeraCore file put in the cores folder.")
            opacity: 0.7
        }
    }
}
