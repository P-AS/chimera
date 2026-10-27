// Chimera's Tools > MCP Server... (docs/mcp-server.md): where a person starts
// and stops the server that lets their AI agent work on the open project,
// allows it to make changes, and reads what it did. The server is the
// engine's; this window only shows it. It stays open beside the main window,
// and closing it does not stop the server. Layout and bindings only, over the
// mcp view model.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "mcpServerWindow"
    required property QtObject vm
    property QtObject clipboard: null
    property Window mainWindow: null
    title: qsTr("MCP Server")
    width: 620
    height: 480
    transientParent: win.mainWindow
    // brought up to date with the engine four times a second while it is open
    Timer { interval: 250; running: win.visible; repeat: true; onTriggered: win.vm.run("refresh") }
    onVisibleChanged: if (visible) win.vm.run("refresh")

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 6
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: qsTr("An AI agent can look at the project you have open, and change it if you allow that, while this server is started.")
        }
        RowLayout {
            Label { objectName: "mcpState"; text: win.vm.stateText; elide: Text.ElideRight; Layout.fillWidth: true }
            Button {
                objectName: "mcpStartStop"
                text: win.vm.started ? qsTr("Stop") : qsTr("Start")
                Layout.preferredWidth: 110
                onClicked: win.vm.run("toggle")
            }
        }
        CheckBox {
            text: qsTr("Allow the agent to make changes (edit input, move the playback position, write memory)")
            checked: win.vm.allowedNow
            // the person's choice is remembered for every later start (user-decided, 2026-10-22, issue 355)
            onToggled: win.vm.allowChanges = checked
        }
        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 18
            wrapMode: Text.Wrap
            opacity: 0.8
            text: qsTr("Remembered, and applied every time the server is started. While it is off the agent can only look.")
        }
        Label {
            Layout.fillWidth: true
            Layout.topMargin: 6
            wrapMode: Text.Wrap
            text: qsTr("To set up an agent, once: give it this command. This one is for Claude Code; other programs run the same file as an MCP server.")
        }
        RowLayout {
            TextField { text: win.vm.setupCommand; readOnly: true; Layout.fillWidth: true; selectByMouse: true }
            Button {
                text: qsTr("Copy")
                Layout.preferredWidth: 110
                onClicked: {
                    if (win.clipboard) { win.clipboard.setText(win.vm.setupCommand); win.vm.note = qsTr("The command is on the clipboard.") }
                }
            }
        }
        Label { Layout.topMargin: 6; text: qsTr("What happened, one line a call:") }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            TextArea {
                objectName: "mcpLog"
                text: win.vm.log
                readOnly: true
                wrapMode: TextEdit.NoWrap
                font.family: "monospace"
                // the newest line in view
                onTextChanged: cursorPosition = length
            }
        }
        RowLayout {
            Button { text: qsTr("Save Log..."); onClicked: saveLogDialog.open() }
            Label { text: win.vm.note !== "" ? win.vm.note : qsTr("Closing this window does not stop the server."); elide: Text.ElideRight; Layout.fillWidth: true }
            Button { text: qsTr("Close"); Layout.preferredWidth: 110; onClicked: win.close() }
        }
    }
    FileDialog {
        id: saveLogDialog
        title: qsTr("Save the MCP server's log")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Text file (*.txt)")]
        defaultSuffix: "txt"
        selectedFile: "file:///chimera-mcp-log-" + Qt.formatDateTime(new Date(), "yyyy-MM-dd-HHmm") + ".txt"
        onAccepted: win.vm.run("saveLog", [localFiles.localPath(selectedFile)])
    }
}
