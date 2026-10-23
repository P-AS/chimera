// Chimera's Debugger, its register box: the core's registers, read when the
// machine moves, set by hand. Layout and bindings only, over the debugger
// view model.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "debuggerWindow"
    required property QtObject vm
    required property QtObject session
    title: qsTr("Debugger")
    width: 420
    height: 480
    onVisibleChanged: if (visible) win.vm.run("refresh")
    // read again as the machine moves, while the window is open
    Connections {
        target: win.session
        enabled: win.visible
        // at most ten times a second while it runs
        function onFrameChanged() { if (!refreshTimer.running) refreshTimer.start() }
        function onRunningChanged() { win.vm.run("refresh") }
    }
    Timer { id: refreshTimer; interval: 100; onTriggered: win.vm.run("refresh") }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        Label {
            visible: !win.session.hasRegisters
            text: win.session.isOpen ? qsTr("This core does not show its registers.") : qsTr("No core is running.")
        }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: win.vm.rows("registers")
            ScrollBar.vertical: ScrollBar {}
            delegate: RowLayout {
                id: row
                required property int index
                required property string name
                required property string hex
                width: list.width - 16
                Label { text: row.name; font.family: "monospace"; Layout.preferredWidth: 90 }
                TextField {
                    text: row.hex
                    font.family: "monospace"
                    Layout.fillWidth: true
                    onEditingFinished: if (text !== row.hex) win.vm.run("setRegister", [row.index, "0x" + text])
                }
            }
        }
        Label { visible: win.vm.hasCycles; text: qsTr("Cycles run: %1").arg(win.vm.cycles) }
        RowLayout {
            Label { text: win.vm.error; color: "#b00020"; Layout.fillWidth: true; elide: Text.ElideRight }
            Button { text: qsTr("Refresh"); onClicked: win.vm.run("refresh") }
            Button { text: qsTr("Close"); onClicked: win.close() }
        }
    }
}
