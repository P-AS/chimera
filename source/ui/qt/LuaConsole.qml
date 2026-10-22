// The Lua console: the script above, its output below. Layout and bindings
// only, over the luaConsole view model.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "luaConsoleWindow"
    required property QtObject vm
    title: win.vm.path === "" ? qsTr("Lua Console") : qsTr("Lua Console - %1").arg(win.vm.path)
    width: 680
    height: 620

    FileDialog {
        id: openDialog
        nameFilters: [qsTr("Lua scripts (*.lua)"), qsTr("All files (*)")]
        onAccepted: win.vm.run("load", [selectedFile])
    }
    FileDialog {
        id: saveDialog
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Lua scripts (*.lua)")]
        onAccepted: win.vm.run("save", [selectedFile])
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        RowLayout {
            Button { text: qsTr("Open..."); onClicked: openDialog.open() }
            Button { text: qsTr("Save"); enabled: win.vm.path !== ""; onClicked: win.vm.run("save") }
            Button { text: qsTr("Save As..."); onClicked: saveDialog.open() }
            Item { Layout.fillWidth: true }
            Button { text: qsTr("Run"); highlighted: true; onClicked: win.vm.run("run") }
        }
        SplitView {
            orientation: Qt.Vertical
            Layout.fillWidth: true
            Layout.fillHeight: true
            ScrollView {
                SplitView.preferredHeight: 320
                TextArea {
                    text: win.vm.script
                    font.family: "monospace"
                    placeholderText: qsTr("chimera.session.play(), chimera.pianoRoll.toggle(10, 0), chimera.onFrame(function() ... end)")
                    onTextChanged: if (text !== win.vm.script) win.vm.script = text
                }
            }
            ColumnLayout {
                RowLayout {
                    Label { text: qsTr("Output (%1 lines)").arg(win.vm.lines) }
                    Item { Layout.fillWidth: true }
                    Button { text: qsTr("Clear"); onClicked: win.vm.run("clear") }
                }
                ListView {
                    id: out
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: win.vm.rows("output")
                    ScrollBar.vertical: ScrollBar {}
                    onCountChanged: positionViewAtEnd()
                    delegate: Label {
                        required property string line
                        text: line
                        font.family: "monospace"
                        color: line.startsWith("error:") || line.startsWith("onFrame function dropped") ? "#d03030" : palette.text
                        width: out.width
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
    }
}
