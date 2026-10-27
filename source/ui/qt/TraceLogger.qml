// The Trace Logger, as Chimera's: a File menu (Save Visible Lines...), a
// toolbar (Start Logging and Log to File... as buttons that stay down,
// Clear, how many lines to keep), the lines in two columns (Disassembly,
// Registers), and the status line with the core's header and the counts.
// Tracing costs the core time: it stops with the window. Layout and
// bindings only, over the traceLogger view model.
import QtQuick
import QtQuick.Controls
import ChimeraDialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "traceLoggerWindow"
    required property QtObject vm
    required property QtObject session
    property var folderOf: null
    property QtObject customize: null
    property QtObject toolWindows: null
    property Window mainWindow: null
    // the theme's colour for a role (Main hands it its lookup)
    property var themeColor: (role, fallback) => fallback
    title: qsTr("Trace Logger")
    width: 620
    height: 520
    onVisibleChanged: {
        if (visible) windowOptions.placeBack(false)
        else if (win.vm.logging) win.vm.logging = false
    }

    WindowOptions { id: windowOptions; window: win; tool: "traceLogger"; vm: win.toolWindows; mainWindow: win.mainWindow }

    FileDialog {
        id: fileDialog
        title: qsTr("Log the trace to")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "log"
        currentFolder: win.folderOf ? win.folderOf("Debug Logs") : ""
        selectedFile: win.folderOf ? win.folderOf("Debug Logs") + "/" + (win.session.gameName || "trace") + ".log" : ""
        nameFilters: [qsTr("Log Files (*.log)")]
        onAccepted: win.vm.run("logToFile", [selectedFile])
    }
    FileDialog {
        id: saveDialog
        title: qsTr("Save Visible Lines")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "log"
        currentFolder: win.folderOf ? win.folderOf("Debug Logs") : ""
        selectedFile: win.folderOf ? win.folderOf("Debug Logs") + "/" + (win.session.gameName || "trace") + ".log" : ""
        nameFilters: [qsTr("Log Files (*.log)")]
        onAccepted: win.vm.run("saveVisible", [selectedFile])
    }

    menuBar: MenuBar {
        Menu {
            delegate: MenuEntry {}
            title: qsTr("&File")
            MenuItem { text: qsTr("&Save Visible Lines..."); onTriggered: saveDialog.open() }
            MenuDivider {}
            MenuItem { text: qsTr("&Close"); onTriggered: win.close() }
        }
        // Chimera adds a Settings menu for the window's items to a tool that has none
        Menu {
            delegate: MenuEntry {}
            title: qsTr("&Settings")
            MenuItem { text: qsTr("Save Window &Position"); checkable: true; checked: windowOptions.savePosition; onTriggered: windowOptions.set("save", checked) }
            MenuItem {
                text: qsTr("Stay on &Top"); checkable: true; checked: windowOptions.onTop
                visible: windowOptions.canStayOnTop; height: visible ? implicitHeight : 0
                onTriggered: windowOptions.set("top", checked)
            }
            MenuItem { text: qsTr("&Float From Main Window"); checkable: true; checked: windowOptions.floating; onTriggered: windowOptions.set("float", checked) }
            MenuItem { text: qsTr("Restore &Defaults"); onTriggered: { win.vm.capacity = 10000; windowOptions.restore() } }
        }
    }

    header: ToolBar {
        RowLayout {
            spacing: 2
            ToolButton {
                objectName: "traceLogging"
                text: win.vm.logging ? qsTr("Stop Logging") : qsTr("Start Logging")
                checkable: true
                checked: win.vm.logging
                enabled: win.session.hasTrace
                onClicked: win.vm.logging = checked
            }
            // down while the lines go to a file; pressed again, the file is closed
            ToolButton {
                text: qsTr("Log to File...")
                checkable: true
                checked: win.vm.file !== ""
                onClicked: { if (win.vm.file !== "") win.vm.run("stopFile"); else { checked = false; fileDialog.open() } }
            }
            ToolButton { text: qsTr("Clear"); onClicked: win.vm.run("clear") }
            Label { text: qsTr("Keep:") }
            SpinBox { from: 100; to: 1000000; stepSize: 1000; editable: true; value: win.vm.capacity; onValueModified: win.vm.capacity = value; Layout.preferredWidth: 110 }
            Label { text: qsTr("lines") }
        }
    }

    footer: Label {
        objectName: "traceStatus"
        text: win.vm.error !== "" ? win.vm.error
            : !win.session.hasTrace ? (win.session.isOpen ? qsTr("This core does not trace its CPU.") : qsTr("No core is running."))
            : win.vm.status
        color: win.vm.error !== "" ? win.themeColor("AccentError", "#b00020") : palette.text
        padding: 4
        elide: Text.ElideRight
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Row {
            Layout.fillWidth: true
            Label { text: qsTr("Disassembly"); width: 340; padding: 3; font.bold: true }
            Label { text: qsTr("Registers"); padding: 3; font.bold: true }
        }
        ListView {
            id: list
            objectName: "traceLines"
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
                Label { text: disassembly; font.family: "monospace"; width: 340; leftPadding: 3; elide: Text.ElideRight }
                Label { text: registers; font.family: "monospace"; leftPadding: 3 }
            }
        }
    }
}
