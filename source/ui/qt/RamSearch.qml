// RAM Search: layout and bindings only, over the ramSearch view model. The
// list is Qt's own TableView on the generic adapter's table model; nothing
// here is drawn by hand.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "ramSearchWindow"
    required property QtObject search
    title: qsTr("RAM Search")
    width: 740
    height: 640

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        RowLayout {
            Label { text: qsTr("Domain") }
            ComboBox {
                model: win.search.domains === "" ? [] : win.search.domains.split(",")
                currentIndex: model.indexOf(win.search.domain)
                onActivated: win.search.domain = currentText
                Layout.preferredWidth: 140
            }
            Label { text: qsTr("Size") }
            ComboBox {
                // the view model's sizes are 1, 2 and 4 bytes, in that order here
                model: [qsTr("1 byte"), qsTr("2 bytes"), qsTr("4 bytes")]
                currentIndex: win.search.size === 4 ? 2 : win.search.size - 1
                onActivated: (i) => win.search.size = [1, 2, 4][i]
            }
            Label { text: qsTr("Shown") }
            ComboBox {
                model: [qsTr("Unsigned"), qsTr("Signed"), qsTr("Real")]
                currentIndex: win.search.display
                onActivated: (i) => win.search.display = i
            }
            Item { Layout.fillWidth: true }
            Button { text: qsTr("Start"); enabled: win.search.startEnabled; onClicked: win.search.run("start") }
        }

        RowLayout {
            ComboBox {
                model: [qsTr("Previous value"), qsTr("Value"), qsTr("Address"), qsTr("Changes"), qsTr("Difference")]
                currentIndex: win.search.compare
                onActivated: (i) => win.search.compare = i
                Layout.preferredWidth: 150
            }
            ComboBox {
                model: [qsTr("Equal to"), qsTr("More than"), qsTr("At least"), qsTr("Less than"), qsTr("At most"), qsTr("Not equal to"), qsTr("Different by")]
                currentIndex: win.search.op
                onActivated: (i) => win.search.op = i
                Layout.preferredWidth: 130
            }
            TextField {
                text: win.search.value
                validator: IntValidator {}
                onEditingFinished: win.search.value = Number(text)
                Layout.preferredWidth: 90
            }
            Item { Layout.fillWidth: true }
            Button { text: qsTr("Search"); enabled: win.search.searchEnabled; onClicked: win.search.run("search") }
            Button { text: qsTr("Undo"); enabled: win.search.undoEnabled; onClicked: win.search.run("undo") }
            Button { text: qsTr("Redo"); enabled: win.search.redoEnabled; onClicked: win.search.run("redo") }
        }

        Label { text: win.search.status }

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
            model: win.search.rows("results")
            columnWidthProvider: (column) => (table.width - 14) / 4
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOn }
            delegate: Label {
                required property var display
                text: display === undefined ? "" : display
                padding: 3
                font.family: "monospace"
            }
        }
    }
}
