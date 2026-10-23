// Chimera's Greenzone budgets (from the Cache Manager): what a history of
// states may weigh, for every project and for the open one. Layout and
// bindings only, over the session.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "greenzoneBudgetsWindow"
    required property QtObject session
    title: win.session.projectTitle !== "" ? qsTr("Greenzone budgets: %1").arg(win.session.projectTitle) : qsTr("Greenzone budgets")
    width: 520
    height: 470

    component Note: Label { wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.columnSpan: 3; opacity: 0.75 }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        Note { text: qsTr("A greenzone is kept in memory. Past this it is thinned; nothing is written to disk until the project is saved.") }
        Label { text: qsTr("Every project, unless it says otherwise"); font.bold: true }
        GridLayout {
            columns: 3
            Label { text: qsTr("Keep in memory") }
            SpinBox { from: 64; to: 1048576; stepSize: 256; editable: true; value: win.session.greenzoneBudgetMb; onValueModified: win.session.greenzoneBudgetMb = value }
            Label { text: qsTr("MB") }
            Label { text: qsTr("Behind the playhead, 1 frame in") }
            SpinBox { from: 1; to: 32; editable: true; value: win.session.greenzoneNearStride; onValueModified: win.session.greenzoneNearStride = value }
            Item { width: 1 }
            Note { text: qsTr("1 keeps every frame and plays slowest. Higher plays faster on a heavy core.") }
            Label { text: qsTr("After a load, show kept pictures for") }
            SpinBox { from: 0; to: 10; editable: true; value: win.session.pictureSettleFrames; onValueModified: win.session.pictureSettleFrames = value }
            Label { text: qsTr("frames") }
            Note { text: qsTr("On a GPU core the picture can be wrong right after a load. For this many frames a frame seen before shows the picture it had. 0 always shows what the core draws.") }
        }
        Label { text: qsTr("This project"); font.bold: true; visible: win.session.hasProject }
        CheckBox {
            id: own
            visible: win.session.hasProject
            text: qsTr("A different budget for %1").arg(win.session.projectTitle)
            checked: win.session.projectBudgetMb !== 0 || win.session.projectNearStride !== 0
            onToggled: win.session.run("setProjectBudgets", checked ? [ownMemory.value, ownStride.value] : [0, 0])
        }
        GridLayout {
            columns: 3
            visible: win.session.hasProject
            enabled: own.checked
            Label { text: qsTr("Keep in memory") }
            SpinBox {
                id: ownMemory
                from: 64; to: 1048576; stepSize: 256; editable: true
                value: win.session.projectBudgetMb !== 0 ? win.session.projectBudgetMb : win.session.greenzoneBudgetMb
                onValueModified: win.session.run("setProjectBudgets", [value, ownStride.value])
            }
            Label { text: qsTr("MB") }
            Label { text: qsTr("Behind the playhead, 1 frame in") }
            SpinBox {
                id: ownStride
                from: 1; to: 32; editable: true
                value: win.session.projectNearStride !== 0 ? win.session.projectNearStride : win.session.greenzoneNearStride
                onValueModified: win.session.run("setProjectBudgets", [ownMemory.value, value])
            }
            Item { width: 1 }
            Note { text: qsTr("Takes effect when the project is next opened: a history already holding states is not thrown away to obey a number that has just changed.") }
        }
        Label {
            visible: win.session.isOpen
            text: qsTr("The machine open now has %1 MB, 1 frame in %2 behind the playhead.").arg(win.session.budgetMbApplied).arg(win.session.nearStrideApplied)
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Item { Layout.fillHeight: true }
        RowLayout {
            Label { text: win.session.error; color: "#b00020"; Layout.fillWidth: true; elide: Text.ElideRight }
            Button { text: qsTr("Close"); onClicked: win.close() }
        }
    }
}
