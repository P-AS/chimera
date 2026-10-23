// Chimera's Trace Logger: layout and bindings only, over the traceLogger
// view model.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "traceLoggerWindow"
    required property QtObject vm
    required property QtObject session
    title: qsTr("Trace Logger")
    width: 760
    height: 520
    // tracing costs the core time: it stops with the window
    onVisibleChanged: if (!visible && win.vm.logging) win.vm.logging = false

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 6
        RowLayout {
            Button {
                text: win.vm.logging ? qsTr("Stop Logging") : qsTr("Start Logging")
                enabled: win.session.hasTrace
                onClicked: win.vm.logging = !win.vm.logging
            }
            Button {
                text: win.vm.file !== "" ? qsTr("Stop Logging to File") : qsTr("Log to File...")
                onClicked: win.vm.file !== "" ? win.vm.run("stopFile") : fileDialog.open()
            }
            Button { text: qsTr("Clear"); onClicked: win.vm.run("clear") }
            Label { text: qsTr("Keep:") }
            SpinBox { from: 100; to: 1000000; stepSize: 1000; editable: true; value: win.vm.capacity; onValueModified: win.vm.capacity = value }
            Label { text: qsTr("lines") }
            Item { Layout.fillWidth: true }
            Button { text: qsTr("Save Visible Lines..."); onClicked: saveDialog.open() }
        }
        Label {
            visible: !win.session.hasTrace
            text: win.session.isOpen ? qsTr("This core does not trace its CPU.") : qsTr("No core is running.")
        }
        Label { text: win.session.traceHeader; visible: text !== ""; font.family: "monospace"; opacity: 0.75 }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: win.vm.rows("lines")
            ScrollBar.vertical: ScrollBar {}
            // the newest at the bottom, as it comes
            onCountChanged: positionViewAtEnd()
            delegate: Row {
                required property string disassembly
                required property string registers
                spacing: 16
                Label { text: disassembly; font.family: "monospace"; width: 300; elide: Text.ElideRight }
                Label { text: registers; font.family: "monospace" }
            }
        }
        Label { text: win.vm.error !== "" ? win.vm.error : win.vm.status; color: win.vm.error !== "" ? "#b00020" : palette.text; elide: Text.ElideRight; Layout.fillWidth: true }
    }
    FileDialog {
        id: fileDialog
        title: qsTr("Log the trace to")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Log Files (*.log)")]
        onAccepted: win.vm.run("logToFile", [selectedFile])
    }
    FileDialog {
        id: saveDialog
        title: qsTr("Save the visible lines as")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Log Files (*.log)")]
        onAccepted: win.vm.run("saveVisible", [selectedFile])
    }
}
