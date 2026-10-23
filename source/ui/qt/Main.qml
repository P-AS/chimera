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
    required property QtObject ramWatch
    required property QtObject newProject
    required property QtObject hexEditor
    required property QtObject controls
    required property QtObject hotkeys
    required property QtObject keyNamer
    required property QtObject themes
    required property QtObject themeColors
    required property QtObject settings
    required property QtObject luaConsole
    required property QtObject branches
    required property QtObject cores
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
            Action { text: qsTr("&New Project..."); shortcut: "Ctrl+N"; onTriggered: { newProjectWindow.show(); newProjectWindow.raise() } }
            Action { text: qsTr("&Open Game..."); shortcut: "Ctrl+O"; onTriggered: gameDialog.open() }
            Action { text: qsTr("Open Game &With a Core Package..."); onTriggered: packageDialog.open() }
            Action { text: qsTr("Open &Project..."); shortcut: "Ctrl+Shift+O"; onTriggered: projectDialog.open() }
            Menu {
                id: recentMenu
                title: qsTr("&Recent Projects")
                enabled: root.session.recentProjects !== ""
                Instantiator {
                    model: root.session.recentProjects === "" ? [] : root.session.recentProjects.split("\n")
                    delegate: MenuItem {
                        required property string modelData
                        text: modelData
                        onTriggered: root.session.run("openProject", [modelData])
                    }
                    onObjectAdded: (index, object) => recentMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => recentMenu.removeItem(object)
                }
            }
            // a project not saved yet asks where it goes
            Action { text: qsTr("&Save Project"); shortcut: "Ctrl+S"; enabled: root.session.hasProject; onTriggered: root.session.projectPath === "" ? saveAsDialog.open() : root.session.run("saveProject") }
            Action { text: qsTr("Save Project &As..."); enabled: root.session.hasProject; onTriggered: saveAsDialog.open() }
            Action { text: qsTr("Make a &Project of This Game..."); enabled: root.session.isOpen && !root.session.hasProject; onTriggered: newProjectDialog.open() }
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
            Action { text: qsTr("&Controls and Hotkeys..."); onTriggered: { controlsWindow.show(); controlsWindow.raise() } }
            Action { text: qsTr("&Themes..."); onTriggered: { themesWindow.show(); themesWindow.raise() } }
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
            Action { text: qsTr("&Cores..."); onTriggered: { coresWindow.show(); coresWindow.raise() } }
            Action { text: qsTr("RAM &Search..."); onTriggered: { ramSearchWindow.show(); ramSearchWindow.raise() } }
            Action { text: qsTr("RAM &Watch..."); onTriggered: { ramWatchWindow.show(); ramWatchWindow.raise() } }
            Action { text: qsTr("&Hex Editor..."); onTriggered: { hexEditorWindow.show(); hexEditorWindow.raise() } }
            Action { text: qsTr("&Lua Console..."); onTriggered: { luaConsoleWindow.show(); luaConsoleWindow.raise() } }
        }
        Menu {
            // TAStudio's editing, on the frames picked (or the current one), with its keys
            title: qsTr("E&dit")
            readonly property bool picked: root.pianoRoll.selectFirst >= 0
            readonly property int at: picked ? root.pianoRoll.selectFirst : root.pianoRoll.currentFrame
            id: editMenu
            Action {
                text: qsTr("&Insert Frame"); shortcut: "Insert"; enabled: root.session.isOpen
                onTriggered: root.pianoRoll.run("insertFrames", [Math.min(editMenu.at, root.pianoRoll.frameCount), 1])
            }
            Action { text: qsTr("Insert &Number of Frames..."); shortcut: "Shift+Insert"; enabled: root.session.isOpen; onTriggered: insertDialog.open() }
            Action {
                text: qsTr("&Delete Frames"); shortcut: "Ctrl+Delete"; enabled: editMenu.picked
                onTriggered: root.pianoRoll.run("deleteFrames", [root.pianoRoll.selectFirst, root.pianoRoll.selectLast])
            }
            Action {
                text: qsTr("C&lone Frames"); shortcut: "Ctrl+Insert"; enabled: editMenu.picked
                onTriggered: root.pianoRoll.run("cloneFrames", [root.pianoRoll.selectFirst, root.pianoRoll.selectLast])
            }
            Action {
                text: qsTr("Cl&ear Frames"); shortcut: "Delete"; enabled: editMenu.picked
                onTriggered: root.pianoRoll.run("clearFrames", [root.pianoRoll.selectFirst, root.pianoRoll.selectLast])
            }
            MenuSeparator {}
            Action {
                text: qsTr("&Copy"); shortcut: StandardKey.Copy; enabled: editMenu.picked
                onTriggered: root.pianoRoll.run("copyFrames", [root.pianoRoll.selectFirst, root.pianoRoll.selectLast])
            }
            Action {
                text: qsTr("Cu&t"); shortcut: StandardKey.Cut; enabled: editMenu.picked
                onTriggered: root.pianoRoll.run("cutFrames", [root.pianoRoll.selectFirst, root.pianoRoll.selectLast])
            }
            Action {
                text: qsTr("&Paste"); shortcut: StandardKey.Paste; enabled: root.pianoRoll.clipboardFrames > 0
                onTriggered: root.pianoRoll.run("pasteFrames", [Math.min(editMenu.at, root.pianoRoll.frameCount)])
            }
            Action {
                text: qsTr("Paste In&sert"); shortcut: "Ctrl+Shift+V"; enabled: root.pianoRoll.clipboardFrames > 0
                onTriggered: root.pianoRoll.run("pasteInsertFrames", [Math.min(editMenu.at, root.pianoRoll.frameCount)])
            }
            MenuSeparator {}
            Action {
                text: qsTr("T&runcate From Here"); enabled: root.session.isOpen && editMenu.at < root.pianoRoll.frameCount
                onTriggered: root.pianoRoll.run("truncate", [editMenu.at])
            }
            Action {
                text: qsTr("Select &All"); shortcut: "Ctrl+Shift+A"; enabled: root.pianoRoll.frameCount > 0
                onTriggered: root.pianoRoll.run("select", [0, root.pianoRoll.frameCount - 1])
            }
            Action { text: qsTr("Select N&othing"); enabled: editMenu.picked; onTriggered: root.pianoRoll.run("select", [-1, -1]) }
            Action {
                text: qsTr("&Markers Move With the Frames")
                checkable: true
                checked: root.pianoRoll.bindMarkers
                onToggled: root.pianoRoll.bindMarkers = checked
            }
        }
        Menu {
            title: qsTr("Piano &Roll")
            Action { text: qsTr("&Undo"); shortcut: StandardKey.Undo; enabled: root.pianoRoll.undoEnabled; onTriggered: root.pianoRoll.run("undo") }
            Action { text: qsTr("&Redo"); shortcut: StandardKey.Redo; enabled: root.pianoRoll.redoEnabled; onTriggered: root.pianoRoll.run("redo") }
            MenuSeparator {}
            Action { text: qsTr("&Branches..."); shortcut: "Ctrl+B"; onTriggered: { branchesWindow.show(); branchesWindow.raise() } }
            Action { text: qsTr("Add &Marker Here..."); enabled: root.session.isOpen; onTriggered: markerDialog.open() }
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
            Menu {
                title: qsTr("&Greenzone")
                Action {
                    text: qsTr("Store &Every Frame"); checkable: true; checked: root.session.greenzonePeriod === 1
                    onTriggered: root.session.greenzonePeriod = 1
                }
                Action {
                    text: qsTr("Store Every &%1 Frames...").arg(root.session.greenzoneEvery); checkable: true
                    checked: root.session.greenzonePeriod > 1
                    onTriggered: { checked = root.session.greenzonePeriod > 1; everyDialog.open() }
                }
                Action {
                    text: qsTr("Store &None"); checkable: true; checked: root.session.greenzonePeriod === 0
                    onTriggered: root.session.greenzonePeriod = 0
                }
            }
            Action {
                text: qsTr("&Follow the Current Frame")
                checkable: true
                checked: root.pianoRoll.followPlayback
                onToggled: root.pianoRoll.followPlayback = checked
            }
        }
    }

    Dialog {
        id: everyDialog
        title: qsTr("Store every how many frames")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        SpinBox { id: everyCount; from: 2; to: 999; value: root.session.greenzoneEvery; editable: true }
        onAccepted: { root.session.greenzoneEvery = everyCount.value; root.session.greenzonePeriod = everyCount.value }
    }
    Dialog {
        id: insertDialog
        title: qsTr("Insert frames")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        RowLayout {
            Label { text: qsTr("How many") }
            SpinBox { id: insertCount; from: 1; to: 1000000; value: 1; editable: true }
        }
        onAccepted: root.pianoRoll.run("insertFrames", [Math.min(editMenu.at, root.pianoRoll.frameCount), insertCount.value])
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
    RamWatch { id: ramWatchWindow; vm: root.ramWatch; visible: false }
    NewProject { id: newProjectWindow; vm: root.newProject; visible: false }
    HexEditor { id: hexEditorWindow; vm: root.hexEditor; visible: false }
    Themes { id: themesWindow; vm: root.themes; colors: root.themeColors; visible: false }
    Controls { id: controlsWindow; controls: root.controls; hotkeys: root.hotkeys; keyNamer: root.keyNamer; visible: false }
    Settings { id: settingsWindow; settings: root.settings; visible: false }
    LuaConsole { id: luaConsoleWindow; vm: root.luaConsole; visible: false }
    Branches { id: branchesWindow; vm: root.branches; visible: false }
    Cores { id: coresWindow; vm: root.cores; visible: false }

    FileDialog {
        id: packageDialog
        title: qsTr("Choose a core package")
        nameFilters: [qsTr("Core packages (*.chimeraCore)"), qsTr("All files (*)")]
        onAccepted: { root.chosenPackage = selectedFile; gameWithDialog.open() }
    }
    FileDialog {
        id: gameWithDialog
        title: qsTr("Choose a game")
        onAccepted: root.session.run("open", [root.chosenPackage, selectedFile])
    }
    property url chosenProject
    FileDialog {
        id: projectDialog
        title: qsTr("Choose a project")
        nameFilters: [qsTr("Chimera projects (*.chimeraProject)"), qsTr("All files (*)")]
        // the project's core is found among the installed ones
        onAccepted: root.session.run("openProject", [selectedFile])
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
        id: newProjectDialog
        title: qsTr("Save the new project as (beside its game, so it finds it)")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Chimera projects (*.chimeraProject)")]
        onAccepted: root.session.run("saveAsProject", [selectedFile])
    }
    FileDialog {
        id: gameDialog
        title: qsTr("Choose a game")
        // the installed core that takes this kind of file opens it
        onAccepted: root.session.run("openGame", [selectedFile])
    }
    // several installed cores take it: the session offers them
    Dialog {
        id: chooseCoreDialog
        objectName: "chooseCoreDialog"
        title: qsTr("Open it with which core?")
        anchors.centerIn: parent
        modal: true
        visible: root.session.chooseFor !== ""
        standardButtons: Dialog.Open | Dialog.Cancel
        ColumnLayout {
            Label { text: root.session.chooseFor.replace(/^.*[\\/]/, ""); elide: Text.ElideMiddle; Layout.maximumWidth: 420 }
            ListView {
                id: coreChoices
                Layout.fillWidth: true
                implicitWidth: 320
                implicitHeight: Math.min(contentHeight, 240)
                clip: true
                model: root.session.choices === "" ? [] : root.session.choices.split("\n")
                delegate: ItemDelegate {
                    required property string modelData
                    required property int index
                    width: coreChoices.width
                    text: modelData
                    highlighted: ListView.isCurrentItem
                    onClicked: coreChoices.currentIndex = index
                    onDoubleClicked: chooseCoreDialog.accept()
                }
            }
        }
        onAccepted: root.session.run("chooseCore", [coreChoices.currentIndex])
        onRejected: root.session.run("chooseCore", [-1])
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
            // RAM Watch's on-screen watches, where Chimera puts them: from
            // the top left, 70 down, a line every 14
            Column {
                objectName: "onScreenWatches"
                x: 0
                y: 70
                visible: root.session.isOpen
                Repeater {
                    model: root.ramWatch.onScreenText === "" ? [] : root.ramWatch.onScreenText.split("\n")
                    delegate: Text {
                        required property string modelData
                        text: modelData
                        height: 14
                        color: "white"
                        style: Text.Outline
                        styleColor: "black"
                        font.family: "monospace"
                        font.pixelSize: 12
                    }
                }
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
                anchors { left: parent.left; top: parent.top; bottom: axisBar.top; right: scroll.left }
                rows: root.pianoRoll.rows("frames")
                currentRow: root.pianoRoll.currentFrame
                topRow: root.pianoRoll.topFrame
                onVisibleRowsChanged: root.pianoRoll.visibleFrames = visibleRows
                onTopRowRequested: (row) => root.pianoRoll.topFrame = row
                onCellPressed: (row, column) => root.pianoRoll.run("press", [row, column])
                onCellDragged: (row, column) => root.pianoRoll.run("drag", [row, column])
                onFrameClicked: (row) => root.session.run("seek", [row])
                selectFirst: root.pianoRoll.selectFirst
                selectLast: root.pianoRoll.selectLast
                // the analog cell being edited: its column is after the buttons
                pickedRow: root.pianoRoll.pickedFrame
                pickedColumn: root.pianoRoll.pickedAxis < 0 ? -1 : root.session.buttonCount + root.pianoRoll.pickedAxis
                onSelectionRequested: (first, last) => root.pianoRoll.run("select", [first, last])
            }
            // the analog cell picked: its value, set here
            RowLayout {
                id: axisBar
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: visible ? 4 : 0 }
                height: visible ? implicitHeight : 0
                visible: root.pianoRoll.pickedAxis >= 0
                Label { text: qsTr("%1, frame %2").arg(root.pianoRoll.axisName).arg(root.pianoRoll.pickedFrame) }
                SpinBox {
                    id: axisSpin
                    editable: true
                    from: root.pianoRoll.axisMin
                    to: root.pianoRoll.axisMax
                    value: root.pianoRoll.axisValue
                    Keys.onReturnPressed: axisSet.clicked()
                }
                Button { id: axisSet; text: qsTr("Set"); onClicked: root.pianoRoll.run("setAxis", [root.pianoRoll.pickedFrame, root.pianoRoll.pickedAxis, axisSpin.value]) }
                Item { Layout.fillWidth: true }
                Button { text: qsTr("Done"); onClicked: root.pianoRoll.run("unpick") }
            }
            ScrollBar {
                id: scroll
                anchors { right: parent.right; top: parent.top; bottom: axisBar.top }
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
            Label { text: qsTr("%1 rerecords").arg(root.session.rerecords) }
        }
    }
}
