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
    property url chosenPackage

    width: 1280
    height: 760
    visible: true
    title: session.isOpen ? qsTr("Chimera - %1 (%2)").arg(session.coreName).arg(session.systemName) : qsTr("Chimera")

    menuBar: MenuBar {
        Menu {
            title: qsTr("&File")
            Action { text: qsTr("&Open..."); shortcut: "Ctrl+O"; onTriggered: packageDialog.open() }
            Action { text: qsTr("&Restart"); enabled: root.session.restartEnabled; onTriggered: root.session.run("restart") }
            Action { text: qsTr("&Close"); enabled: root.session.closeEnabled; onTriggered: root.session.run("close") }
            MenuSeparator {}
            Action { text: qsTr("E&xit"); shortcut: "Ctrl+Q"; onTriggered: Qt.quit() }
        }
        Menu {
            title: qsTr("&Emulation")
            Action { text: qsTr("&Play"); shortcut: "Ctrl+P"; enabled: root.session.playEnabled && !root.session.running; onTriggered: root.session.run("play") }
            Action { text: qsTr("P&ause"); shortcut: "Ctrl+Space"; enabled: root.session.running; onTriggered: root.session.run("pause") }
            Action { text: qsTr("&Frame Advance"); shortcut: "Ctrl+F"; enabled: root.session.frameAdvanceEnabled; onTriggered: root.session.run("frameAdvance") }
        }
        Menu {
            title: qsTr("Piano &Roll")
            Action { text: qsTr("Make the Inputs 1,000,000 Frames Long"); onTriggered: root.pianoRoll.run("fill", [1000000]) }
            Action {
                text: qsTr("&Follow the Current Frame")
                checkable: true
                checked: root.pianoRoll.followPlayback
                onToggled: root.pianoRoll.followPlayback = checked
            }
        }
    }

    FileDialog {
        id: packageDialog
        title: qsTr("Choose a core package")
        nameFilters: [qsTr("Core packages (*.chimeraCore)"), qsTr("All files (*)")]
        onAccepted: { root.chosenPackage = selectedFile; gameDialog.open() }
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
            Label { text: root.session.error !== "" ? root.session.error : root.pianoRoll.error; color: "#b00020" }
            Label {
                text: qsTr("Frame %1 of %2").arg(Number(root.pianoRoll.currentFrame).toLocaleString(Qt.locale(), "f", 0))
                                            .arg(Number(root.pianoRoll.frameCount).toLocaleString(Qt.locale(), "f", 0))
            }
            Label { text: qsTr("%1 fps").arg(Number(root.session.fps).toFixed(1)) }
            Label { text: qsTr("roll %1 ms").arg(Number(roll.lastPaintMs).toFixed(2)) }
        }
    }
}
