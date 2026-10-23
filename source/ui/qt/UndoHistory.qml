// The piano roll's undo history, as Chimera's TAStudio shows it: layout and
// bindings only, over the pianoRoll view model. Each step by name; the ones
// taken back are greyed; a step picked can be undone or redone to, or the
// history forgotten up to it.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "undoHistoryWindow"
    required property QtObject vm
    title: qsTr("Undo History")
    width: 420
    height: 480
    property int picked: -1

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8

        ListView {
            id: steps
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: win.vm.rows("history")
            ScrollBar.vertical: ScrollBar {}
            onCountChanged: if (autoScroll.checked) positionViewAtEnd()
            delegate: ItemDelegate {
                id: step
                required property int index
                required property string name
                required property bool done
                width: steps.width - 16
                text: step.name
                highlighted: step.index === win.picked
                opacity: step.done ? 1.0 : 0.5
                onClicked: win.picked = step.index
                onDoubleClicked: win.vm.run("undoTo", [step.index])
            }
        }
        RowLayout {
            Button { text: qsTr("Undo"); enabled: win.vm.undoEnabled; onClicked: win.vm.run("undo") }
            Button { text: qsTr("Redo"); enabled: win.vm.redoEnabled; onClicked: win.vm.run("redo") }
            Button { text: qsTr("Clear"); onClicked: { win.vm.run("clearHistory"); win.picked = -1 } }
            Item { Layout.fillWidth: true }
            CheckBox { id: autoScroll; text: qsTr("Auto Scroll"); checked: true }
        }
        RowLayout {
            Button { text: qsTr("Undo To Selection"); enabled: win.picked >= 0; onClicked: win.vm.run("undoTo", [win.picked]) }
            Button { text: qsTr("Redo To Selection"); enabled: win.picked >= 0; onClicked: win.vm.run("redoTo", [win.picked]) }
        }
        RowLayout {
            Button { text: qsTr("Clear History To Selection"); enabled: win.picked >= 0; onClicked: { win.vm.run("clearHistoryTo", [win.picked]); win.picked = -1 } }
            Item { Layout.fillWidth: true }
            Label { text: qsTr("Max:") }
            SpinBox {
                from: 1; to: 100000; editable: true
                value: win.vm.maxUndoSteps
                onValueModified: win.vm.maxUndoSteps = value
            }
        }
        Label { text: win.vm.error; color: "#b00020"; visible: text !== "" }
    }
}
