// RAM Watch: layout and bindings only, over the ramWatch view model. The
// list is Qt's own TableView on the generic adapter's table model.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "ramWatchWindow"
    required property QtObject vm
    property int current: -1
    title: qsTr("RAM Watch") + (win.vm.path !== "" ? " - " + win.vm.path : "") + (win.vm.changed ? " *" : "")
    width: 820
    height: 600

    FileDialog {
        id: openDialog
        title: qsTr("Open a watch list")
        nameFilters: [qsTr("Watch lists (*.wch)"), qsTr("All files (*)")]
        onAccepted: win.vm.run("open", [selectedFile])
    }
    FileDialog {
        id: saveDialog
        title: qsTr("Save the watch list as")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Watch lists (*.wch)")]
        onAccepted: win.vm.run("save", [selectedFile])
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        RowLayout {
            Button { text: qsTr("New"); onClicked: { win.vm.run("newList"); win.current = -1 } }
            Button { text: qsTr("Open..."); onClicked: openDialog.open() }
            Button { text: qsTr("Save"); enabled: win.vm.path !== ""; onClicked: win.vm.run("save") }
            Button { text: qsTr("Save As..."); onClicked: saveDialog.open() }
            Item { Layout.fillWidth: true }
            Label { text: qsTr("Previous value") }
            ComboBox {
                // the view model's numbers for them: 2, 3 and 0
                model: [qsTr("Last frame"), qsTr("Before the last change"), qsTr("First seen")]
                currentIndex: [2, 3, 0].indexOf(win.vm.previousType)
                onActivated: (i) => win.vm.previousType = [2, 3, 0][i]
            }
        }

        RowLayout {
            ComboBox {
                id: domain
                model: win.vm.domains === "" ? [] : win.vm.domains.split(",")
                Layout.preferredWidth: 110
            }
            TextField { id: address; placeholderText: qsTr("Address (hex)"); Layout.preferredWidth: 110 }
            ComboBox {
                model: [qsTr("1 byte"), qsTr("2 bytes"), qsTr("4 bytes")]
                currentIndex: win.vm.formSize === 4 ? 2 : win.vm.formSize - 1
                onActivated: (i) => win.vm.formSize = [1, 2, 4][i]
            }
            ComboBox {
                model: win.vm.formDisplays === "" ? [] : win.vm.formDisplays.split("\n")
                currentIndex: win.vm.formDisplay
                onActivated: (i) => win.vm.formDisplay = i
                Layout.preferredWidth: 150
            }
            CheckBox { id: bigEndian; text: qsTr("Big endian") }
            TextField { id: note; placeholderText: qsTr("Note"); Layout.fillWidth: true }
            Button {
                text: qsTr("Watch")
                onClicked: if (win.vm.run("addFromForm", [domain.currentText, address.text, bigEndian.checked, note.text]) !== undefined) note.text = ""
            }
        }

        RowLayout {
            Button { text: qsTr("Separator"); onClicked: { win.vm.run("addSeparator", [note.text]); note.text = "" } }
            Button { text: qsTr("Remove"); enabled: win.current >= 0; onClicked: { win.vm.run("remove", [win.current]); win.current = -1 } }
            Button {
                text: qsTr("Up")
                enabled: win.current > 0
                onClicked: { win.vm.run("move", [win.current, win.current - 1]); win.current-- }
            }
            Button {
                text: qsTr("Down")
                enabled: win.current >= 0 && win.current < win.vm.count - 1
                onClicked: { win.vm.run("move", [win.current, win.current + 1]); win.current++ }
            }
            Button {
                text: qsTr("On Screen")
                enabled: win.current >= 0
                onClicked: win.vm.run("toggleOnScreen", [win.current])
            }
            Button { text: qsTr("Clear Changes"); onClicked: win.vm.run("clearChangeCounts") }
            Item { Layout.fillWidth: true }
            Label { text: win.vm.status; elide: Text.ElideRight; Layout.maximumWidth: 300 }
        }

        HorizontalHeaderView {
            syncView: table
            Layout.fillWidth: true
            clip: true
        }
        TableView {
            id: table
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: win.vm.rows("watches")
            // address, value, previous, changes, difference, domain, note, size,
            // display, on screen: the size and display are in the form, so hidden
            columnWidthProvider: (c) => [90, 150, 150, 70, 70, 80, Math.max(120, table.width - 700), 0, 0, 70][c]
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOn }
            delegate: Rectangle {
                id: cell
                required property var display
                required property int row
                implicitHeight: label.implicitHeight
                color: row === win.current ? palette.highlight : "transparent"
                Label {
                    id: label
                    anchors.fill: parent
                    text: typeof cell.display === "boolean" ? (cell.display ? qsTr("shown") : "")
                        : cell.display === undefined ? "" : cell.display
                    color: cell.row === win.current ? palette.highlightedText : palette.text
                    padding: 3
                    font.family: "monospace"
                    elide: Text.ElideRight
                }
                TapHandler { onTapped: win.current = cell.row }
            }
        }
        Label { text: win.vm.error; color: "#b00020"; visible: text !== "" }
    }
}
