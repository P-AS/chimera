// The main window: layout and bindings only. Every decision is the view
// models' (session, pianoRoll); this file says where things go and which
// command a gesture is.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Chimera

ApplicationWindow {
    id: root
    required property QtObject session
    required property QtObject pianoRoll
    required property QtObject ramSearch
    required property QtObject settings
    required property QtObject luaConsole
    property url chosenPackage

    width: 1280
    height: 760
    visible: true
    title: !session.isOpen ? qsTr("Chimera")
         : session.projectTitle !== "" ? qsTr("Chimera - %1 - %2 (%3)").arg(session.projectTitle).arg(session.coreName).arg(session.systemName)
         : qsTr("Chimera - %1 (%2)").arg(session.coreName).arg(session.systemName)

    menuBar: MenuBar {
        Menu {
            title: qsTr("&File")
            Action { text: qsTr("&Open Game..."); shortcut: "Ctrl+O"; onTriggered: packageDialog.open() }
            Action { text: qsTr("Open &Project..."); shortcut: "Ctrl+Shift+O"; onTriggered: projectDialog.open() }
            Action { text: qsTr("&Save Project"); shortcut: "Ctrl+S"; enabled: root.session.projectPath !== ""; onTriggered: root.session.run("saveProject") }
            Action { text: qsTr("Save Project &As..."); enabled: root.session.projectPath !== ""; onTriggered: saveAsDialog.open() }
            Action { text: qsTr("&Restart"); enabled: root.session.restartEnabled; onTriggered: root.session.run("restart") }
            Action { text: qsTr("&Close"); enabled: root.session.closeEnabled; onTriggered: root.session.run("close") }
            MenuSeparator {}
            Action { text: qsTr("E&xit"); shortcut: "Ctrl+Q"; onTriggered: Qt.quit() }
        }
        Menu {
            title: qsTr("&Emulation")
            Action { text: qsTr("&Play"); shortcut: "Ctrl+P"; enabled: root.session.playEnabled && !root.session.running; onTriggered: root.session.run("play") }
            Action { text: qsTr("P&ause"); shortcut: "Ctrl+Space"; enabled: root.session.running; onTriggered: root.session.run("pause") }
            Action { text: qsTr("&Settings..."); onTriggered: { settingsWindow.show(); settingsWindow.raise() } }
            Action { text: qsTr("&Frame Advance"); shortcut: "Ctrl+F"; enabled: root.session.frameAdvanceEnabled; onTriggered: root.session.run("frameAdvance") }
            Action { text: qsTr("Go to Frame &0"); shortcut: "Ctrl+Home"; enabled: root.session.seekEnabled; onTriggered: root.session.run("seek", [0]) }
            Action {
                text: qsTr("After an Edit, &Return to Where It Was")
                checkable: true
                checked: root.session.autoRestore
                onToggled: root.session.autoRestore = checked
            }
        }
        Menu {
            title: qsTr("&Tools")
            Action { text: qsTr("RAM &Search..."); onTriggered: { ramSearchWindow.show(); ramSearchWindow.raise() } }
            Action { text: qsTr("&Lua Console..."); onTriggered: { luaConsoleWindow.show(); luaConsoleWindow.raise() } }
        }
        Menu {
            title: qsTr("Piano &Roll")
            Action { text: qsTr("Add &Marker Here..."); shortcut: "Ctrl+M"; enabled: root.session.isOpen; onTriggered: markerDialog.open() }
            Action { text: qsTr("Remove Marker Here"); enabled: root.session.isOpen; onTriggered: root.pianoRoll.run("removeMarker", [root.pianoRoll.currentFrame]) }
            Action {
                text: qsTr("&Next Marker"); shortcut: "Ctrl+Down"; enabled: root.session.isOpen
                onTriggered: root.session.run("seek", [root.pianoRoll.run("nextMarker", [root.pianoRoll.currentFrame])])
            }
            Action {
                text: qsTr("&Previous Marker"); shortcut: "Ctrl+Up"; enabled: root.session.isOpen
                onTriggered: root.session.run("seek", [root.pianoRoll.run("previousMarker", [root.pianoRoll.currentFrame])])
            }
            MenuSeparator {}
            Action { text: qsTr("Make the Inputs 1,000,000 Frames Long"); onTriggered: root.pianoRoll.run("fill", [1000000]) }
            Action {
                text: qsTr("&Follow the Current Frame")
                checkable: true
                checked: root.pianoRoll.followPlayback
                onToggled: root.pianoRoll.followPlayback = checked
            }
        }
    }

    Dialog {
        id: markerDialog
        title: qsTr("Marker on frame %1").arg(root.pianoRoll.currentFrame)
        standardButtons: Dialog.Ok | Dialog.Cancel
        anchors.centerIn: parent
        modal: true
        TextField { id: markerText; placeholderText: qsTr("What happens here"); width: 300; focus: true }
        onOpened: { markerText.text = ""; markerText.forceActiveFocus() }
        onAccepted: root.pianoRoll.run("addMarker", [root.pianoRoll.currentFrame, markerText.text])
    }
    RamSearch { id: ramSearchWindow; search: root.ramSearch; visible: false }
    Settings { id: settingsWindow; settings: root.settings; visible: false }
    LuaConsole { id: luaConsoleWindow; vm: root.luaConsole; visible: false }

    FileDialog {
        id: packageDialog
        title: qsTr("Choose a core package")
        nameFilters: [qsTr("Core packages (*.chimeraCore)"), qsTr("All files (*)")]
        onAccepted: { root.chosenPackage = selectedFile; gameDialog.open() }
    }
    property url chosenProject
    FileDialog {
        id: projectDialog
        title: qsTr("Choose a project")
        nameFilters: [qsTr("Chimera projects (*.chimeraProject)"), qsTr("All files (*)")]
        onAccepted: { root.chosenProject = selectedFile; projectPackageDialog.open() }
    }
    FileDialog {
        id: projectPackageDialog
        title: qsTr("Choose the core package to open it with")
        nameFilters: [qsTr("Core packages (*.chimeraCore)"), qsTr("All files (*)")]
        onAccepted: root.session.run("openProject", [root.chosenProject, selectedFile])
    }
    FileDialog {
        id: saveAsDialog
        title: qsTr("Save the project as")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Chimera projects (*.chimeraProject)")]
        onAccepted: root.session.run("saveProject", [selectedFile])
    }
    FileDialog {
        id: gameDialog
        title: qsTr("Choose a game")
        onAccepted: root.session.run("open", [root.chosenPackage, selectedFile])
    }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal

        Rectangle {
            SplitView.preferredWidth: root.width * 0.58
            SplitView.minimumWidth: 200
            color: "black"
            PictureItem {
                objectName: "picture"
                anchors.fill: parent
                source: root.session.picture("screen")
            }
            Label {
                anchors.centerIn: parent
                visible: !root.session.isOpen
                color: "#a0a0a0"
                text: qsTr("File > Open... to start")
            }
        }

        Item {
            SplitView.fillWidth: true
            SplitView.minimumWidth: 200
            PianoRollItem {
                id: roll
                objectName: "pianoRoll"
                anchors { left: parent.left; top: parent.top; bottom: parent.bottom; right: scroll.left }
                rows: root.pianoRoll.rows("frames")
                currentRow: root.pianoRoll.currentFrame
                topRow: root.pianoRoll.topFrame
                onVisibleRowsChanged: root.pianoRoll.visibleFrames = visibleRows
                onTopRowRequested: (row) => root.pianoRoll.topFrame = row
                onCellPressed: (row, column) => root.pianoRoll.run("press", [row, column])
                onCellDragged: (row, column) => root.pianoRoll.run("drag", [row, column])
                onFrameClicked: (row) => root.session.run("seek", [row])
            }
            ScrollBar {
                id: scroll
                anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
                orientation: Qt.Vertical
                policy: ScrollBar.AlwaysOn
                size: roll.rowCount > 0 ? Math.min(1, roll.visibleRows / roll.rowCount) : 1
                position: roll.rowCount > 0 ? roll.topRow / roll.rowCount : 0
                // only a drag of the bar moves the roll; the binding above moves the bar
                onPositionChanged: if (pressed) root.pianoRoll.topFrame = Math.round(position * roll.rowCount)
            }
        }
    }

    footer: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            Label { text: root.session.status; elide: Text.ElideRight; Layout.fillWidth: true }
            Label { text: root.session.gamepads; elide: Text.ElideRight; Layout.maximumWidth: 260 }
            Label { text: root.session.sound; elide: Text.ElideRight; Layout.maximumWidth: 200 }
            Label { text: root.session.error !== "" ? root.session.error : root.pianoRoll.error; color: "#b00020" }
            Label {
                text: qsTr("Frame %1 of %2").arg(Number(root.pianoRoll.currentFrame).toLocaleString(Qt.locale(), "f", 0))
                                            .arg(Number(root.pianoRoll.frameCount).toLocaleString(Qt.locale(), "f", 0))
            }
            Label { text: qsTr("%1 fps").arg(Number(root.session.fps).toFixed(1)) }
            Label { text: qsTr("roll %1 ms").arg(Number(roll.lastPaintMs).toFixed(2)) }
            Label { text: qsTr("seek %1 frames, %2 ms").arg(root.session.lastSeekFrames).arg(Number(root.session.lastSeekMs).toFixed(0)) }
            Label { text: qsTr("%1 states").arg(root.session.greenzoneStates) }
        }
    }
}
