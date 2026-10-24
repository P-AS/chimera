// Chimera's TAStudio: the project's own window, opened beside the main one
// when a project opens and closed with it. Its menus (Edit, Metadata,
// Settings, Columns, Window, << Hide), the piano roll, and the boxes at its
// side (Playback, Greenzone, Branches, Markers), as Chimera lays them out.
// Layout and bindings only, over the session, pianoRoll, branches and
// projectDetails view models.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Chimera

ApplicationWindow {
    id: tas
    objectName: "tastudioWindow"
    // the first input roll (the scroll bars and the measurements follow it)
    readonly property Item mainRoll: rollRepeater.count > 0 ? rollRepeater.itemAt(0) : null
    required property QtObject session
    required property QtObject pianoRoll
    required property QtObject branches
    required property QtObject projectDetails
    required property QtObject clipboard
    required property QtObject customize
    // the main window's menu item that shows a hotkey's keys
    required property Component keyedItem
    // a Paths folder for a file dialog to open in
    required property var folderOf
    // the window it sits beside
    required property var mainWindow
    // whether Shift is held (the main window dragged alone)
    required property QtObject keyNamer
    property QtObject toolWindows: null
    // Save Window Position (its size: it opens beside the main window), Stay on Top, Float From Main Window
    WindowOptions { id: windowOptions; window: tas; tool: "tastudio"; vm: tas.toolWindows; mainWindow: tas.mainWindow }
    // "<< Hide": the main window offers it back
    property bool hiddenByPerson: false
    title: !tas.session.hasProject ? qsTr("TAStudio")
         : qsTr("TAStudio - %1%2").arg(tas.session.projectTitle).arg(tas.session.unsaved ? "*" : "")
    width: 720
    height: 600
    minimumWidth: 200
    minimumHeight: 148

    // opened at the main window's right shoulder, tops aligned, on every
    // open, as far right as the screen allows; never off it
    function placeBesideMain() {
        const main = tas.mainWindow
        if (main.visibility !== Window.Windowed) return
        const right = main.screen.virtualX + main.screen.desktopAvailableWidth
        const bottom = main.screen.virtualY + main.screen.desktopAvailableHeight
        let x = main.x + main.width
        if (x + tas.width > right) x = Math.max(main.screen.virtualX, right - tas.width)
        tas.x = x
        tas.y = Math.max(main.screen.virtualY, Math.min(main.y, bottom - tas.height))
    }
    // Header Info, Edit Comments or Edit Subtitles, for the main window's right-click menu
    function showDetails(part) {
        const w = part === "comments" ? commentsWindow : part === "subtitles" ? subtitlesWindow : headerWindow
        w.show()
        w.raise()
    }
    function engage() {
        tas.hiddenByPerson = false
        if (!tas.visible) {
            windowOptions.placeBack(true)
            tas.placeBesideMain()
        }
        tas.show()
        tas.raise()
    }
    // Move with Main Window: the main window dragged brings this one along,
    // keeping its place beside it; this one dragged moves alone, and so does
    // the main window dragged with Shift held, as Chimera's
    property real mainX: 0
    property real mainY: 0
    Connections {
        target: tas.mainWindow
        function onXChanged() { tas.followMain() }
        function onYChanged() { tas.followMain() }
    }
    function followMain() {
        const main = tas.mainWindow
        const dx = main.x - tas.mainX, dy = main.y - tas.mainY
        tas.mainX = main.x
        tas.mainY = main.y
        if (!tas.visible || !tas.pianoRoll.moveWithMainWindow || tas.keyNamer.shiftHeld()) return
        if (main.visibility !== Window.Windowed || tas.visibility !== Window.Windowed) return
        tas.x += dx
        tas.y += dy
    }
    Component.onCompleted: { tas.mainX = tas.mainWindow.x; tas.mainY = tas.mainWindow.y }

    function selectBetweenMarkers() {
        const f = tas.pianoRoll.currentFrame
        const before = tas.pianoRoll.run("previousMarker", [f + 1])
        const after = tas.pianoRoll.run("nextMarker", [f])
        tas.pianoRoll.run("select", [before >= 0 ? before : 0, after >= 0 ? after - 1 : tas.pianoRoll.frameCount - 1])
    }
    // the hotkeys whose action is one of TAStudio's
    readonly property var hotkeyActions: ({
        "Undo": undoAction, "Redo": redoAction, "Sel. bet. Markers": betweenMarkersAction, "Select All": selectAllAction,
        "Clear Frames": clearFramesAction, "Delete Frames": deleteFramesAction, "Insert Frame": insertFrameAction,
        "Insert # Frames": insertFramesAction, "Clone Frames": cloneFramesAction, "Paste Insert": pasteInsertAction,
        "Clone # Times": cloneTimesAction, "Reselect Clip.": reselectAction
    })

    menuBar: RowLayout {
        spacing: 0
        MenuBar {
            Layout.fillWidth: true
            Menu {
                id: editMenu
                // Customize's "pause when a menu is open"
                onAboutToShow: tas.customize.run("menuOpened")
                onAboutToHide: tas.customize.run("menuClosed")
                delegate: tas.keyedItem
                // on the frames picked, or the current one
                title: qsTr("&Edit")
                readonly property bool picked: tas.pianoRoll.selectFirst >= 0
                readonly property int at: picked ? tas.pianoRoll.selectFirst : tas.pianoRoll.currentFrame
                Action { id: undoAction; property string hotkey: "Undo"; text: qsTr("&Undo"); enabled: tas.pianoRoll.undoEnabled; onTriggered: tas.pianoRoll.run("undo") }
                Action { id: redoAction; property string hotkey: "Redo"; text: qsTr("&Redo"); enabled: tas.pianoRoll.redoEnabled; onTriggered: tas.pianoRoll.run("redo") }
                Action { text: qsTr("Show Undo History"); onTriggered: { undoHistoryWindow.show(); undoHistoryWindow.raise() } }
                MenuSeparator {}
                Action { text: qsTr("Deselect"); enabled: editMenu.picked; onTriggered: tas.pianoRoll.run("select", [-1, -1]) }
                Action {
                    id: betweenMarkersAction; property string hotkey: "Sel. bet. Markers"
                    text: qsTr("Select between Markers"); enabled: tas.pianoRoll.frameCount > 0
                    onTriggered: tas.selectBetweenMarkers()
                }
                Action {
                    id: selectAllAction; property string hotkey: "Select All"
                    text: qsTr("Select &All"); enabled: tas.pianoRoll.frameCount > 0
                    onTriggered: tas.pianoRoll.run("select", [0, tas.pianoRoll.frameCount - 1])
                }
                Action { id: reselectAction; property string hotkey: "Reselect Clip."; text: qsTr("Reselect Clipboard"); enabled: tas.pianoRoll.clipboardFrames > 0; onTriggered: tas.pianoRoll.run("reselectClipboard") }
                Action { property string hotkey: "Seek To..."; text: qsTr("Go to Frame..."); enabled: tas.session.seekEnabled; onTriggered: seekDialog.open() }
                MenuSeparator {}
                Action {
                    text: qsTr("Copy"); shortcut: StandardKey.Copy; enabled: editMenu.picked
                    onTriggered: {
                        tas.pianoRoll.run("copyFrames", [tas.pianoRoll.selectFirst, tas.pianoRoll.selectLast])
                        tas.clipboard.setText(tas.pianoRoll.clipboardText)
                    }
                }
                Action {
                    // the lines on the system's clipboard, as Chimera's Paste reads them
                    text: qsTr("&Paste"); shortcut: StandardKey.Paste; enabled: tas.session.isOpen && tas.clipboard.hasText
                    onTriggered: tas.pianoRoll.run("pasteText", [Math.min(editMenu.at, tas.pianoRoll.frameCount), tas.clipboard.text(), false])
                }
                Action {
                    id: pasteInsertAction; property string hotkey: "Paste Insert"
                    text: qsTr("&Paste Insert"); enabled: tas.session.isOpen && tas.clipboard.hasText
                    onTriggered: tas.pianoRoll.run("pasteText", [Math.min(editMenu.at, tas.pianoRoll.frameCount), tas.clipboard.text(), true])
                }
                Action {
                    text: qsTr("&Cut"); shortcut: StandardKey.Cut; enabled: editMenu.picked
                    onTriggered: {
                        tas.pianoRoll.run("cutFrames", [tas.pianoRoll.selectFirst, tas.pianoRoll.selectLast])
                        tas.clipboard.setText(tas.pianoRoll.clipboardText)
                    }
                }
                MenuSeparator {}
                Action {
                    id: clearFramesAction; property string hotkey: "Clear Frames"
                    text: qsTr("Clear"); enabled: editMenu.picked
                    onTriggered: tas.pianoRoll.run("clearFrames", [tas.pianoRoll.selectFirst, tas.pianoRoll.selectLast])
                }
                Action {
                    id: deleteFramesAction; property string hotkey: "Delete Frames"
                    text: qsTr("&Delete"); enabled: editMenu.picked
                    onTriggered: tas.pianoRoll.run("deleteFrames", [tas.pianoRoll.selectFirst, tas.pianoRoll.selectLast])
                }
                Action {
                    id: insertFrameAction; property string hotkey: "Insert Frame"
                    text: qsTr("&Insert"); enabled: tas.session.isOpen
                    onTriggered: tas.pianoRoll.run("insertFrames", [Math.min(editMenu.at, tas.pianoRoll.frameCount), 1])
                }
                Action { id: insertFramesAction; property string hotkey: "Insert # Frames"; text: qsTr("Insert # of Frames"); enabled: tas.session.isOpen; onTriggered: insertDialog.open() }
                Action {
                    id: cloneFramesAction; property string hotkey: "Clone Frames"
                    text: qsTr("&Clone"); enabled: editMenu.picked
                    onTriggered: tas.pianoRoll.run("cloneFrames", [tas.pianoRoll.selectFirst, tas.pianoRoll.selectLast])
                }
                Action { id: cloneTimesAction; property string hotkey: "Clone # Times"; text: qsTr("Clone # Times"); enabled: editMenu.picked; onTriggered: cloneDialog.open() }
                MenuSeparator {}
                Action {
                    text: qsTr("&Truncate Movie"); enabled: tas.session.isOpen && editMenu.at < tas.pianoRoll.frameCount
                    onTriggered: tas.pianoRoll.run("truncate", [editMenu.at])
                }
                Action { text: qsTr("&Clear Greenzone"); enabled: tas.session.isOpen; onTriggered: tas.session.run("clearGreenzone") }
                MenuSeparator {}
                Action { text: qsTr("State History Integrity Check"); enabled: tas.session.isOpen; onTriggered: tas.session.run("checkStateHistory") }
                MenuSeparator {}
                Action { text: qsTr("Save Selection to Macro"); enabled: editMenu.picked; onTriggered: saveMacroDialog.open() }
                Action { text: qsTr("Place Macro at Selection"); enabled: editMenu.picked; onTriggered: placeMacroDialog.open() }
                Menu {
                    id: recentMacrosMenu
                    title: qsTr("Recent Macros")
                    enabled: editMenu.picked && tas.pianoRoll.recentMacros !== ""
                    Instantiator {
                        model: tas.pianoRoll.recentMacros === "" ? [] : tas.pianoRoll.recentMacros.split("\n")
                        delegate: MenuItem {
                            required property string modelData
                            text: modelData
                            onTriggered: tas.pianoRoll.run("placeMacro", [tas.pianoRoll.selectFirst, modelData])
                        }
                        onObjectAdded: (index, object) => recentMacrosMenu.insertItem(index, object)
                        onObjectRemoved: (index, object) => recentMacrosMenu.removeItem(object)
                    }
                }
                MenuSeparator {}
                Action {
                    text: qsTr("&Markers Move With the Frames")
                    checkable: true
                    checked: tas.pianoRoll.bindMarkers
                    onToggled: tas.pianoRoll.bindMarkers = checked
                }
                Action { text: qsTr("Make the Inputs 1,000,000 Frames Long"); onTriggered: tas.pianoRoll.run("fill", [1000000]) }
            }
            Menu {
                onAboutToShow: tas.customize.run("menuOpened")
                onAboutToHide: tas.customize.run("menuClosed")
                title: qsTr("&Metadata")
                enabled: tas.session.hasProject
                Action { text: qsTr("&Header..."); onTriggered: { headerWindow.show(); headerWindow.raise() } }
                Action { text: qsTr("&Comments..."); onTriggered: { commentsWindow.show(); commentsWindow.raise() } }
                Action { text: qsTr("&Subtitles..."); onTriggered: { subtitlesWindow.show(); subtitlesWindow.raise() } }
            }
            Menu {
                onAboutToShow: tas.customize.run("menuOpened")
                onAboutToHide: tas.customize.run("menuClosed")
                title: qsTr("&Settings")
                Action { text: qsTr("Open settings..."); onTriggered: { tastudioSettingsWindow.show(); tastudioSettingsWindow.raise() } }
            }
            // the columns shown: the console's first, then a menu a player's
            // and the keyboard keys', then a toggle for each group, as Chimera's
            Menu {
                id: columnsMenu
                onAboutToShow: tas.customize.run("menuOpened")
                onAboutToHide: tas.customize.run("menuClosed")
                title: qsTr("&Columns")
                enabled: tas.session.isOpen
                // which input roll the menu is for, when there are several, as Chimera's says it
                MenuItem {
                    text: qsTr("Current roll: #%1").arg(tas.pianoRoll.activeRoll + 1)
                    enabled: false
                    visible: tas.pianoRoll.rollCount > 1
                    height: visible ? implicitHeight : 0
                }
                Instantiator {
                    model: tas.pianoRoll.rows("columns")
                    delegate: MenuItem {
                        required property string name
                        required property string label
                        required property bool shown
                        required property string group
                        text: qsTr("%1 (%2)").arg(label).arg(name)
                        visible: group === ""
                        height: visible ? implicitHeight : 0
                        checkable: true
                        checked: shown
                        onTriggered: tas.pianoRoll.run(shown ? "hideColumn" : "showColumn", [name])
                    }
                    onObjectAdded: (index, object) => columnsMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => columnsMenu.removeItem(object)
                }
                Instantiator {
                    model: tas.pianoRoll.rows("columnGroups")
                    delegate: Menu {
                        id: groupMenu
                        required property string name
                        title: name
                        Instantiator {
                            model: tas.pianoRoll.rows("columns")
                            delegate: MenuItem {
                                required property string name
                                required property string label
                                required property bool shown
                                required property string group
                                text: qsTr("%1 (%2)").arg(label).arg(name)
                                visible: group === groupMenu.name
                                height: visible ? implicitHeight : 0
                                checkable: true
                                checked: shown
                                onTriggered: tas.pianoRoll.run(shown ? "hideColumn" : "showColumn", [name])
                            }
                            onObjectAdded: (index, object) => groupMenu.insertItem(index, object)
                            onObjectRemoved: (index, object) => groupMenu.removeItem(object)
                        }
                    }
                    // each before the separator, in the groups' order
                    onObjectAdded: (index, object) => {
                        let at = 0
                        while (at < columnsMenu.count && columnsMenu.itemAt(at) !== columnsSeparator) at++
                        columnsMenu.insertMenu(at, object)
                    }
                    onObjectRemoved: (index, object) => columnsMenu.removeMenu(object)
                }
                MenuSeparator { id: columnsSeparator }
                Action { text: qsTr("Show &All"); onTriggered: tas.pianoRoll.run("showAllColumns") }
                Instantiator {
                    model: tas.pianoRoll.rows("columnGroups")
                    delegate: MenuItem {
                        required property string toggle
                        required property bool shown
                        required property bool firstOfToggle
                        text: qsTr("Show %1").arg(toggle)
                        visible: firstOfToggle
                        height: visible ? implicitHeight : 0
                        checkable: true
                        checked: shown
                        onTriggered: tas.pianoRoll.run("showGroup", [toggle, !shown])
                    }
                    onObjectAdded: (index, object) => columnsMenu.addItem(object)
                    onObjectRemoved: (index, object) => columnsMenu.removeItem(object)
                }
            }
            Menu {
                onAboutToShow: tas.customize.run("menuOpened")
                onAboutToHide: tas.customize.run("menuClosed")
                title: qsTr("&Window")
                Action {
                    text: qsTr("&Move with Main Window")
                    checkable: true
                    checked: tas.pianoRoll.moveWithMainWindow
                    onToggled: tas.pianoRoll.moveWithMainWindow = checked
                }
                // a Window menu takes the window's options, with no line before them, as Chimera's
                MenuItem { text: qsTr("Save Window &Position"); checkable: true; checked: windowOptions.savePosition; onTriggered: windowOptions.set("save", checked) }
                MenuItem {
                    text: qsTr("Stay on &Top"); checkable: true; checked: windowOptions.onTop
                    visible: windowOptions.canStayOnTop; height: visible ? implicitHeight : 0
                    onTriggered: windowOptions.set("top", checked)
                }
                MenuItem { text: qsTr("&Float From Main Window"); checkable: true; checked: windowOptions.floating; onTriggered: windowOptions.set("float", checked) }
                MenuItem {
                    text: qsTr("Restore &Defaults")
                    // TAStudio's own: every column shown again
                    onTriggered: { tas.pianoRoll.run("showAllColumns"); windowOptions.restore() }
                }
            }
        }
        // at the right of the menus, as Chimera's
        ToolButton {
            objectName: "hideTAStudio"
            text: qsTr("<< Hide")
            flat: true
            onClicked: { tas.hiddenByPerson = true; tas.hide() }
        }
    }

    footer: RowLayout {
        Label {
            Layout.fillWidth: true
            padding: 4
            text: tas.branches.error !== "" ? tas.branches.error : tas.status
            elide: Text.ElideRight
        }
        // how long the roll took to draw, last time
        Label { padding: 4; text: qsTr("roll %1 ms").arg(Number(tas.mainRoll ? tas.mainRoll.lastPaintMs : 0).toFixed(2)) }
    }
    // the status line: what was last said here
    property string status: qsTr("TAStudio engaged")
    Connections { target: tas.branches; function onStatusChanged() { if (tas.branches.status !== "") tas.status = tas.branches.status } }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal

        Item {
            SplitView.fillWidth: true
            SplitView.minimumWidth: 120
            // TAStudio's column menu, on a column's name
            Menu {
                id: columnMenu
                objectName: "columnMenu"
                property string name: ""
                property bool autoHeld: false
                MenuItem {
                    text: qsTr("Auto-hold")
                    checkable: true
                    checked: columnMenu.autoHeld
                    // an analog column asks what value to hold, as Chimera does
                    onTriggered: {
                        const question = tas.pianoRoll.run("autoHoldQuestion", [columnMenu.name])
                        if (question) {
                            holdValueDialog.name = columnMenu.name
                            holdValueDialog.question = question
                            holdValueBox.from = tas.pianoRoll.autoHoldMin
                            holdValueBox.to = tas.pianoRoll.autoHoldMax
                            holdValueBox.value = tas.pianoRoll.autoHoldInitial
                            holdValueDialog.open()
                        } else tas.pianoRoll.run("toggleAutoHold", [columnMenu.name])
                    }
                }
                MenuItem { text: qsTr("Hide column"); onTriggered: tas.pianoRoll.run("hideColumn", [columnMenu.name]) }
                // Chimera's Show columns: the Columns menu's own list, for the input roll used
                Menu {
                    id: showColumnsMenu
                    title: qsTr("Show columns")
                    Instantiator {
                        model: tas.pianoRoll.rows("columns")
                        delegate: MenuItem {
                            required property string name
                            required property string label
                            required property bool shown
                            text: qsTr("%1 (%2)").arg(label).arg(name)
                            checkable: true
                            checked: shown
                            onTriggered: tas.pianoRoll.run(shown ? "hideColumn" : "showColumn", [name])
                        }
                        onObjectAdded: (index, object) => showColumnsMenu.insertItem(index, object)
                        onObjectRemoved: (index, object) => showColumnsMenu.removeItem(object)
                    }
                }
                MenuItem { text: qsTr("New input roll"); onTriggered: tas.pianoRoll.run("newRoll") }
                MenuItem {
                    text: qsTr("Delete input roll")
                    visible: tas.pianoRoll.rollCount > 1
                    height: visible ? implicitHeight : 0
                    onTriggered: tas.pianoRoll.run("deleteRoll")
                }
            }
            // TAStudio's input rolls, as Chimera's: the first and any made with New input roll, side by side
            // (one above another turned on its side), each with columns of its own; the one clicked is the one
            // used, and they scroll together unless Sync scroll is off
            GridLayout {
                id: rollsArea
                anchors { left: parent.left; top: parent.top; bottom: tas.pianoRoll.horizontal ? across.top : axisBar.top; right: tas.pianoRoll.horizontal ? parent.right : scroll.left }
                flow: tas.pianoRoll.horizontal ? GridLayout.TopToBottom : GridLayout.LeftToRight
                rows: tas.pianoRoll.horizontal ? Math.max(1, tas.pianoRoll.rollCount) : 1
                columns: tas.pianoRoll.horizontal ? 1 : Math.max(1, tas.pianoRoll.rollCount)
                rowSpacing: 3
                columnSpacing: 3
                Repeater {
                    id: rollRepeater
                    model: tas.pianoRoll.rollCount
                    delegate: PianoRollItem {
                        id: oneRoll
                        required property int index
                        view: index
                        objectName: index === 0 ? "pianoRoll" : "pianoRoll" + (index + 1)
                        // as Chimera's: each as wide as its columns (as tall, turned), the last taking the rest
                        readonly property bool last: index === tas.pianoRoll.rollCount - 1
                        Layout.fillWidth: tas.pianoRoll.horizontal || last
                        Layout.fillHeight: !tas.pianoRoll.horizontal || last
                        Layout.preferredWidth: tas.pianoRoll.horizontal || last ? -1 : contentExtent
                        Layout.preferredHeight: !tas.pianoRoll.horizontal || last ? -1 : contentExtent
                        // where an input roll is when it does not scroll with the first
                        property int ownTop: 0
                        readonly property bool synced: tas.pianoRoll.scrollSync || index === 0
                        // the input roll clicked becomes the one used: its columns are the menus'
                        function use() { if (tas.pianoRoll.activeRoll !== oneRoll.index) tas.pianoRoll.run("setActiveRoll", [oneRoll.index]) }
                        horizontal: tas.pianoRoll.horizontal
                        // a script's own columns, and the colours and text it gives cells
                        extraColumns: tas.pianoRoll.scriptColumns
                        queries: tas.pianoRoll.scriptQueries
                        iconEpoch: tas.pianoRoll.iconEpoch
                        rows: tas.pianoRoll.rows("frames")
                        currentRow: tas.pianoRoll.currentFrame
                        topRow: oneRoll.synced ? tas.pianoRoll.topFrame : oneRoll.ownTop
                        onVisibleRowsChanged: if (oneRoll.index === 0) tas.pianoRoll.visibleFrames = visibleRows
                        onTopRowRequested: (row) => { if (oneRoll.synced) tas.pianoRoll.topFrame = row; else oneRoll.ownTop = row }
                        wheelRows: tas.pianoRoll.wheelRows
                        denoteStatesColor: tas.pianoRoll.denoteStatesColor
                        denoteStatesIcons: tas.pianoRoll.denoteStatesIcons
                        denoteMarkersColor: tas.pianoRoll.denoteMarkersColor
                        denoteMarkersIcons: tas.pianoRoll.denoteMarkersIcons
                        colors: tas.pianoRoll.rollColors
                        rollFont: tas.pianoRoll.rollFont
                        onCellPressed: (row, column, alt) => { oneRoll.use(); tas.pianoRoll.run("press", [row, column, alt]) }
                        onColumnMenuRequested: (column, name, autoHeld, x, y) => { oneRoll.use(); columnMenu.name = name; columnMenu.autoHeld = autoHeld; columnMenu.popup() }
                        // Chimera's menu for the frames picked; nothing picked, no menu
                        onRowMenuRequested: (row) => { oneRoll.use(); if (tas.pianoRoll.selectFirst >= 0 && tas.pianoRoll.selectLast < tas.pianoRoll.frameCount) rowMenu.popup() }
                        onCellDragged: (row, column) => tas.pianoRoll.run("drag", [row, column])
                        onFrameClicked: (row) => { oneRoll.use(); tas.session.run("seek", [row]) }
                        selectFirst: tas.pianoRoll.selectFirst
                        selectLast: tas.pianoRoll.selectLast
                        // the analog cell being edited: its column is after the buttons
                        pickedRow: tas.pianoRoll.pickedFrame
                        pickedColumn: tas.pianoRoll.pickedAxis < 0 ? -1 : tas.session.buttonCount + tas.pianoRoll.pickedAxis
                        onSelectionRequested: (first, last) => { oneRoll.use(); tas.pianoRoll.run("select", [first, last]) }
                    }
                }
            }
            // the analog cell picked: its value, set here
            RowLayout {
                id: axisBar
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: visible ? 4 : 0 }
                height: visible ? implicitHeight : 0
                visible: tas.pianoRoll.pickedAxis >= 0
                Label { text: qsTr("%1, frame %2").arg(tas.pianoRoll.axisName).arg(tas.pianoRoll.pickedFrame) }
                SpinBox {
                    id: axisSpin
                    editable: true
                    from: tas.pianoRoll.axisMin
                    to: tas.pianoRoll.axisMax
                    value: tas.pianoRoll.axisValue
                    Keys.onReturnPressed: axisSet.clicked()
                }
                Button { id: axisSet; text: qsTr("Set"); onClicked: tas.pianoRoll.run("setAxis", [tas.pianoRoll.pickedFrame, tas.pianoRoll.pickedAxis, axisSpin.value]) }
                Item { Layout.fillWidth: true }
                Button { text: qsTr("Done"); onClicked: tas.pianoRoll.run("unpick") }
            }
            // turned on its side: the frames scroll across, along the bottom
            ScrollBar {
                id: across
                visible: tas.pianoRoll.horizontal
                anchors { left: parent.left; right: parent.right; bottom: axisBar.top }
                height: visible ? implicitHeight : 0
                orientation: Qt.Horizontal
                policy: ScrollBar.AlwaysOn
                size: tas.mainRoll && tas.mainRoll.rowCount > 0 ? Math.min(1, tas.mainRoll.visibleRows / tas.mainRoll.rowCount) : 1
                position: tas.mainRoll && tas.mainRoll.rowCount > 0 ? tas.mainRoll.topRow / tas.mainRoll.rowCount : 0
                onPositionChanged: if (pressed && tas.mainRoll) tas.pianoRoll.topFrame = Math.round(position * tas.mainRoll.rowCount)
            }
            ScrollBar {
                id: scroll
                visible: !tas.pianoRoll.horizontal
                anchors { right: parent.right; top: parent.top; bottom: axisBar.top }
                width: visible ? implicitWidth : 0
                orientation: Qt.Vertical
                policy: ScrollBar.AlwaysOn
                size: tas.mainRoll && tas.mainRoll.rowCount > 0 ? Math.min(1, tas.mainRoll.visibleRows / tas.mainRoll.rowCount) : 1
                position: tas.mainRoll && tas.mainRoll.rowCount > 0 ? tas.mainRoll.topRow / tas.mainRoll.rowCount : 0
                // only a drag of the bar moves the roll; the binding above moves the bar
                onPositionChanged: if (pressed && tas.mainRoll) tas.pianoRoll.topFrame = Math.round(position * tas.mainRoll.rowCount)
            }
        }

        // the boxes at the side: Playback, Greenzone, Branches and Markers,
        // as Chimera stacks them
        ColumnLayout {
            SplitView.preferredWidth: 270
            SplitView.minimumWidth: 200
            spacing: 2

            GroupBox {
                title: qsTr("Playback")
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    RowLayout {
                        spacing: 2
                        SmallButton {
                            text: "<<"; tip: qsTr("Previous marker")
                            enabled: tas.session.isOpen
                            onClicked: tas.session.run("seek", [Math.max(0, tas.pianoRoll.run("previousMarker", [tas.pianoRoll.currentFrame]))])
                        }
                        SmallButton {
                            text: "<"; tip: qsTr("A frame back")
                            enabled: tas.session.isOpen && tas.pianoRoll.currentFrame > 0
                            autoRepeat: true
                            onClicked: tas.session.run("seek", [tas.pianoRoll.currentFrame - 1])
                        }
                        SmallButton {
                            text: "> ||"; tip: qsTr("Pause or run")
                            enabled: tas.session.isOpen
                            onClicked: tas.session.run(tas.session.running ? "pause" : "play")
                        }
                        SmallButton {
                            text: ">"; tip: qsTr("A frame on")
                            enabled: tas.session.isOpen
                            autoRepeat: true
                            onClicked: tas.session.run("frameAdvance")
                        }
                        SmallButton {
                            text: ">>"; tip: qsTr("Next marker")
                            enabled: tas.session.isOpen
                            onClicked: {
                                const next = tas.pianoRoll.run("nextMarker", [tas.pianoRoll.currentFrame])
                                if (next >= 0) tas.session.run("seek", [next])
                            }
                        }
                    }
                    CheckBox {
                        text: qsTr("Follow cursor")
                        checked: tas.pianoRoll.followPlayback
                        onToggled: tas.pianoRoll.followPlayback = checked
                    }
                    CheckBox {
                        text: qsTr("Auto-restore last position")
                        checked: tas.session.autoRestore
                        onToggled: tas.session.autoRestore = checked
                    }
                    CheckBox {
                        objectName: "recordingMode"
                        text: qsTr("Recording mode")
                        checked: tas.session.recordingMode
                        enabled: tas.session.hasProject
                        onToggled: tas.session.recordingMode = checked
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("When enabled, you can record inputs while unpaused or when frame advancing.")
                    }
                }
            }

            // what the greenzone stores: every frame, every N, or nothing
            GroupBox {
                title: qsTr("Greenzone")
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    RadioButton {
                        text: qsTr("Every frame")
                        checked: tas.session.greenzonePeriod === 1
                        onToggled: if (checked) tas.session.greenzonePeriod = 1
                    }
                    RowLayout {
                        RadioButton {
                            id: everyN
                            text: qsTr("Every")
                            checked: tas.session.greenzonePeriod > 1
                            onToggled: if (checked) tas.session.greenzonePeriod = everyCount.value
                        }
                        SpinBox {
                            id: everyCount
                            from: 2; to: 999; editable: true
                            Layout.preferredWidth: 100
                            value: tas.session.greenzoneEvery
                            onValueModified: {
                                tas.session.greenzoneEvery = value
                                if (everyN.checked) tas.session.greenzonePeriod = value
                            }
                        }
                        Label { text: qsTr("frames") }
                    }
                    RadioButton {
                        text: qsTr("Off")
                        checked: tas.session.greenzonePeriod === 0
                        onToggled: if (checked) tas.session.greenzonePeriod = 0
                    }
                }
            }

            GroupBox {
                title: qsTr("Branches")
                Layout.fillWidth: true
                Layout.fillHeight: true
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 2
                    readonly property bool picked: tas.branches.selected >= 0 && tas.branches.selected < tas.branches.count
                    ListView {
                        id: branchList
                        objectName: "branchList"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 48
                        clip: true
                        model: tas.branches.rows("branches")
                        ScrollBar.vertical: ScrollBar {}
                        delegate: ItemDelegate {
                            id: branchRow
                            required property int index
                            required property string name
                            required property var frame
                            required property string time
                            width: branchList.width - 12
                            padding: 2
                            highlighted: branchRow.index === tas.branches.selected
                            contentItem: RowLayout {
                                Label { text: branchRow.index + 1; Layout.preferredWidth: 24 }
                                Label { text: branchRow.frame; Layout.preferredWidth: 56 }
                                Label { text: branchRow.name; Layout.fillWidth: true; elide: Text.ElideRight }
                            }
                            onClicked: tas.branches.selected = branchRow.index
                            // TAStudio's "load branch by double clicking"
                            onDoubleClicked: if (tas.branches.loadOnDoubleClick) tas.branches.run("load", [branchRow.index])
                            TapHandler {
                                acceptedButtons: Qt.RightButton
                                onTapped: { tas.branches.selected = branchRow.index; branchMenu.popup() }
                            }
                            // the branch's picture while the pointer is over it, as Chimera shows it
                            onHoveredChanged: {
                                if (hovered && tas.branches.run("preview", [branchRow.index])) {
                                    const at = branchRow.mapToItem(tas.contentItem, 0, 0)
                                    preview.x = Math.max(0, at.x - tas.branches.previewWidth - 8)
                                    preview.y = at.y
                                    preview.open()
                                } else if (!hovered && tas.branches.previewing === branchRow.index) {
                                    preview.close()
                                    tas.branches.run("preview", [-1])
                                }
                            }
                        }
                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: branchMenu.popup()
                        }
                    }
                    RowLayout {
                        spacing: 2
                        SmallButton { text: qsTr("Add"); enabled: tas.branches.createEnabled; onClicked: tas.branches.run("create", [""]) }
                        SmallButton { text: qsTr("Load"); enabled: parent.parent.picked; onClicked: tas.branches.run("load", [tas.branches.selected]) }
                        SmallButton { text: qsTr("Update"); enabled: parent.parent.picked; onClicked: tas.branches.run("saveSlot", [tas.branches.selected]) }
                        // Chimera's one step of branch undo
                        SmallButton { text: qsTr("Undo"); tip: tas.branches.undoText; enabled: tas.branches.undoText !== ""; onClicked: tas.branches.run("undo") }
                    }
                }
            }

            GroupBox {
                title: qsTr("Markers")
                Layout.fillWidth: true
                Layout.fillHeight: true
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 2
                    ListView {
                        id: markerList
                        objectName: "markerList"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 48
                        clip: true
                        currentIndex: -1
                        model: tas.pianoRoll.rows("markers")
                        ScrollBar.vertical: ScrollBar {}
                        property var pickedFrame: -1
                        property string pickedText: ""
                        // the run's own: nothing to rename, move or remove
                        property bool pickedPermanent: false
                        readonly property bool pickedEditable: pickedFrame > 0 && !pickedPermanent
                        onCountChanged: { currentIndex = -1; pickedFrame = -1; pickedPermanent = false }
                        delegate: ItemDelegate {
                            id: markerRow
                            required property int index
                            // its text is a role of the model's: "text" is the delegate's own
                            required property var model
                            width: markerList.width - 12
                            padding: 2
                            highlighted: ListView.isCurrentItem
                            // as Chimera colours its list: the run's own (start, last input, end), else
                            // the marker the machine is at or past, else its frame's greenzone or lag
                            // colour, the frame's column in the roll's frame-column shade
                            function shade(frameColumn) {
                                if (markerRow.highlighted) return markerRow.palette.highlight
                                if (markerRow.model.permanent) return tas.rollColor("TasPermanentMarker") || "transparent"
                                if (markerRow.model.frame === tas.pianoRoll.currentMarker) return tas.rollColor("TasCurrentFrame") || "transparent"
                                if (markerRow.model.zone === "") return "transparent"
                                const lag = markerRow.model.zone === "lag"
                                return tas.rollColor(frameColumn ? (lag ? "TasLagZone" : "TasGreenZone") : (lag ? "TasLagZoneInput" : "TasGreenZoneInput")) || "transparent"
                            }
                            background: Row {
                                Rectangle { width: markerRow.leftPadding + 58; height: markerRow.height; color: markerRow.shade(true) }
                                Rectangle { width: markerRow.width - markerRow.leftPadding - 58; height: markerRow.height; color: markerRow.shade(false) }
                            }
                            contentItem: RowLayout {
                                spacing: 4
                                Label { text: markerRow.model.frame; Layout.preferredWidth: 56 }
                                Label { text: markerRow.model.text; Layout.fillWidth: true; elide: Text.ElideRight; font.italic: markerRow.model.permanent }
                            }
                            function pick() {
                                markerList.currentIndex = markerRow.index
                                markerList.pickedFrame = markerRow.model.frame
                                markerList.pickedText = markerRow.model.text
                                markerList.pickedPermanent = markerRow.model.permanent
                            }
                            onClicked: markerRow.pick()
                            // Jump To, as a double click in Chimera's list
                            onDoubleClicked: tas.session.run("seek", [markerRow.model.frame])
                            TapHandler {
                                acceptedButtons: Qt.RightButton
                                onTapped: { markerRow.pick(); markerMenu.popup() }
                            }
                        }
                    }
                    RowLayout {
                        spacing: 2
                        SmallButton { text: qsTr("Add"); enabled: tas.session.isOpen; onClicked: markerDialog.openFor(tas.pianoRoll.currentFrame, "") }
                        SmallButton { text: qsTr("Remove"); enabled: markerList.pickedEditable; onClicked: tas.pianoRoll.run("removeMarker", [markerList.pickedFrame]) }
                        SmallButton { text: qsTr("Jump To"); enabled: markerList.pickedFrame >= 0; onClicked: tas.session.run("seek", [markerList.pickedFrame]) }
                        SmallButton { text: qsTr("Edit"); enabled: markerList.pickedEditable; onClicked: markerDialog.openFor(markerList.pickedFrame, markerList.pickedText) }
                    }
                }
            }
        }
    }

    component SmallButton: Button {
        property string tip: ""
        padding: 2
        leftPadding: 8
        rightPadding: 8
        implicitHeight: 28
        // as wide as its text, not the style's usual button
        implicitWidth: Math.max(32, contentItem.implicitWidth + leftPadding + rightPadding)
        ToolTip.text: tip
        ToolTip.visible: hovered && tip !== ""
        ToolTip.delay: 500
    }
    // the roll's menu on the frames picked, in Chimera's order
    Menu {
        id: rowMenu
        objectName: "rowMenu"
        delegate: tas.keyedItem
        Action {
            text: qsTr("Set Markers")
            onTriggered: {
                const count = tas.pianoRoll.selectLast - tas.pianoRoll.selectFirst + 1
                if (count > 50) manyMarkersDialog.open()
                else tas.setMarkers()
            }
        }
        Action { text: qsTr("Set Marker with Text"); onTriggered: markerDialog.openFor(tas.pianoRoll.selectFirst, "") }
        Action { text: qsTr("Remove Markers"); onTriggered: tas.removeMarkers() }
        MenuSeparator {}
        Action { text: qsTr("Deselect"); onTriggered: tas.pianoRoll.run("select", [-1, -1]) }
        Action { property string hotkey: "Sel. bet. Markers"; text: qsTr("Select between Markers"); onTriggered: tas.selectBetweenMarkers() }
        MenuSeparator {}
        Action { text: qsTr("Clear Greenzone"); onTriggered: tas.session.run("clearGreenzone") }
        MenuSeparator {}
        Action {
            text: qsTr("Copy")
            onTriggered: {
                tas.pianoRoll.run("copyFrames", [tas.pianoRoll.selectFirst, tas.pianoRoll.selectLast])
                tas.clipboard.setText(tas.pianoRoll.clipboardText)
            }
        }
        Action { text: qsTr("Paste"); enabled: tas.clipboard.hasText; onTriggered: tas.pianoRoll.run("pasteText", [tas.pianoRoll.selectFirst, tas.clipboard.text(), false]) }
        Action { property string hotkey: "Paste Insert"; text: qsTr("Paste Insert"); enabled: tas.clipboard.hasText; onTriggered: tas.pianoRoll.run("pasteText", [tas.pianoRoll.selectFirst, tas.clipboard.text(), true]) }
        Action {
            text: qsTr("Cut")
            onTriggered: {
                tas.pianoRoll.run("cutFrames", [tas.pianoRoll.selectFirst, tas.pianoRoll.selectLast])
                tas.clipboard.setText(tas.pianoRoll.clipboardText)
            }
        }
        MenuSeparator {}
        Action { property string hotkey: "Clear Frames"; text: qsTr("Clear"); onTriggered: clearFramesAction.trigger() }
        Action { property string hotkey: "Delete Frames"; text: qsTr("Delete"); onTriggered: deleteFramesAction.trigger() }
        Action { property string hotkey: "Insert Frame"; text: qsTr("Insert"); onTriggered: insertFrameAction.trigger() }
        Action { property string hotkey: "Insert # Frames"; text: qsTr("Insert # of Frames"); onTriggered: insertDialog.open() }
        Action { property string hotkey: "Clone Frames"; text: qsTr("Clone"); onTriggered: cloneFramesAction.trigger() }
        Action { property string hotkey: "Clone # Times"; text: qsTr("Clone # Times"); onTriggered: cloneDialog.open() }
        MenuSeparator {}
        Action { text: qsTr("Truncate Movie"); onTriggered: tas.pianoRoll.run("truncate", [tas.pianoRoll.selectFirst]) }
        Action { text: qsTr("&Branch"); enabled: tas.branches.createEnabled; onTriggered: tas.branches.run("create", [""]) }
    }
    // a marker on each frame picked; more than 50 asks first, as Chimera does
    function setMarkers() { tas.pianoRoll.run("addMarkers", [tas.pianoRoll.selectFirst, tas.pianoRoll.selectLast]) }
    function removeMarkers() { tas.pianoRoll.run("removeMarkers", [tas.pianoRoll.selectFirst, tas.pianoRoll.selectLast]) }
    Dialog {
        id: manyMarkersDialog
        title: qsTr("Add markers")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label { text: qsTr("Are you sure you want to add more than 50 markers?") }
        onAccepted: tas.setMarkers()
    }
    // Chimera's branch menu, on the branch picked
    Menu {
        id: branchMenu
        readonly property bool picked: tas.branches.selected >= 0 && tas.branches.selected < tas.branches.count
        Action { text: qsTr("Add"); enabled: tas.branches.createEnabled; onTriggered: tas.branches.run("create", [""]) }
        Action { text: qsTr("Add with Text"); enabled: tas.branches.createEnabled; onTriggered: branchTextDialog.openFor("add") }
        Action { text: qsTr("Load"); enabled: branchMenu.picked; onTriggered: tas.branches.run("load", [tas.branches.selected]) }
        Action { text: qsTr("&Update"); enabled: branchMenu.picked; onTriggered: tas.branches.run("saveSlot", [tas.branches.selected]) }
        Action { text: qsTr("Update with Text"); enabled: branchMenu.picked; onTriggered: branchTextDialog.openFor("update") }
        Action { text: qsTr("Edit Text"); enabled: branchMenu.picked; onTriggered: branchTextDialog.openFor("rename") }
        Action { text: qsTr("Jump To"); enabled: branchMenu.picked; onTriggered: tas.branches.run("jumpTo", [tas.branches.selected]) }
        Action { text: tas.branches.undoText !== "" ? tas.branches.undoText : qsTr("Undo"); enabled: tas.branches.undoText !== ""; onTriggered: tas.branches.run("undo") }
        MenuSeparator {}
        Action { text: qsTr("Remove"); enabled: branchMenu.picked; onTriggered: tas.branches.run("remove", [tas.branches.selected]) }
    }
    Dialog {
        id: branchTextDialog
        property string doing: "add"
        function openFor(what) {
            branchTextDialog.doing = what
            const row = what === "add" ? null : branchList.itemAtIndex(tas.branches.selected)
            branchTextField.text = row ? row.name : ""
            branchTextDialog.open()
        }
        title: qsTr("Branch text")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        TextField { id: branchTextField; width: 300; placeholderText: qsTr("Branch %1 if empty").arg(tas.branches.count + 1) }
        onOpened: branchTextField.forceActiveFocus()
        onAccepted: {
            if (doing === "add") tas.branches.run("create", [branchTextField.text])
            else {
                if (doing === "update") tas.branches.run("saveSlot", [tas.branches.selected])
                tas.branches.run("rename", [tas.branches.selected, branchTextField.text])
            }
        }
    }
    // Chimera's marker menu, on the marker picked
    Menu {
        id: markerMenu
        Action { text: qsTr("Jump To"); onTriggered: tas.session.run("seek", [markerList.pickedFrame]) }
        Action { text: qsTr("Scroll To"); onTriggered: tas.pianoRoll.topFrame = Math.max(0, markerList.pickedFrame - 2) }
        Action { text: qsTr("Edit Text"); enabled: markerList.pickedEditable; onTriggered: markerDialog.openFor(markerList.pickedFrame, markerList.pickedText) }
        Action { text: qsTr("Add"); enabled: tas.session.isOpen; onTriggered: markerDialog.openFor(tas.pianoRoll.currentFrame, "") }
        Action { text: qsTr("Remove"); enabled: markerList.pickedEditable; onTriggered: tas.pianoRoll.run("removeMarker", [markerList.pickedFrame]) }
    }
    Popup {
        id: preview
        objectName: "branchPreview"
        padding: 2
        closePolicy: Popup.NoAutoClose
        PictureItem {
            width: tas.branches.previewWidth
            height: tas.branches.previewHeight
            source: tas.branches.picture("preview")
        }
    }
    Dialog {
        id: cloneDialog
        title: qsTr("Clone the frames picked how many times")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        SpinBox { id: cloneTimes; from: 1; to: 100000; value: 1; editable: true }
        onAccepted: tas.pianoRoll.run("cloneFrames", [tas.pianoRoll.selectFirst, tas.pianoRoll.selectLast, cloneTimes.value])
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
        onAccepted: tas.pianoRoll.run("insertFrames", [Math.min(editMenu.at, tas.pianoRoll.frameCount), insertCount.value])
    }
    // Chimera's Go to Frame: the machine paused, a frame index typed
    Dialog {
        id: seekDialog
        title: qsTr("Go to Frame")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAboutToShow: { tas.session.run("pause"); seekTo.text = ""; seekTo.forceActiveFocus() }
        ColumnLayout {
            Label { text: qsTr("Jump/Seek to frame index:") }
            TextField {
                id: seekTo
                validator: IntValidator { bottom: 0 }
                Layout.preferredWidth: 200
                onAccepted: seekDialog.accept()
            }
        }
        onAccepted: if (seekTo.text !== "") tas.session.run("seek", [parseInt(seekTo.text)])
    }
    Dialog {
        id: markerDialog
        property int frame: 0
        function openFor(f, text) { markerDialog.frame = f; markerText.text = text; markerDialog.open() }
        title: qsTr("Marker on frame %1").arg(markerDialog.frame)
        standardButtons: Dialog.Ok | Dialog.Cancel
        anchors.centerIn: parent
        modal: true
        TextField { id: markerText; placeholderText: qsTr("What happens here"); width: 300; focus: true }
        onOpened: markerText.forceActiveFocus()
        onAccepted: tas.pianoRoll.run("addMarker", [markerDialog.frame, markerText.text])
    }
    Dialog {
        id: holdValueDialog
        objectName: "holdValueDialog"
        property string name: ""
        property string question: ""
        title: qsTr("Auto-hold")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        ColumnLayout {
            Label { text: holdValueDialog.question }
            SpinBox { id: holdValueBox; editable: true }
        }
        onAccepted: tas.pianoRoll.run("toggleAutoHold", [holdValueDialog.name, holdValueBox.value])
    }
    FileDialog {
        id: saveMacroDialog
        title: qsTr("Save the frames picked as a macro")
        currentFolder: tas.folderOf("Macros")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Movie Macros (*.chimeraMacro)")]
        onAccepted: tas.pianoRoll.run("saveMacro", [tas.pianoRoll.selectFirst, tas.pianoRoll.selectLast, selectedFile])
    }
    FileDialog {
        id: placeMacroDialog
        title: qsTr("Place a macro at the frames picked")
        currentFolder: tas.folderOf("Macros")
        nameFilters: [qsTr("Movie Macros (*.chimeraMacro)"), qsTr("All files (*)")]
        onAccepted: tas.pianoRoll.run("placeMacro", [tas.pianoRoll.selectFirst, selectedFile])
    }
    // Chimera's Header Info, Edit Comments and Edit Subtitles, each a window of its own
    ProjectDetails { id: headerWindow; part: "header"; vm: tas.projectDetails; visible: false }
    ProjectDetails { id: commentsWindow; part: "comments"; vm: tas.projectDetails; visible: false }
    ProjectDetails { id: subtitlesWindow; part: "subtitles"; vm: tas.projectDetails; visible: false }
    UndoHistory { id: undoHistoryWindow; vm: tas.pianoRoll; rollColor: tas.rollColor; visible: false }
    // a TAStudio colour as the roll draws it: its own (Set Colors), else the theme's
    function rollColor(role) {
        let own = {}, theme = {}
        try { own = JSON.parse(tas.pianoRoll.rollColors || "{}") } catch (e) {}
        if (own[role] !== undefined) return own[role]
        try { theme = JSON.parse(tas.mainRoll.themeColors()) } catch (e) {}
        return theme[role] || ""
    }
    TAStudioSettings { id: tastudioSettingsWindow; session: tas.session; pianoRoll: tas.pianoRoll; branches: tas.branches; themeColorsOf: () => tas.mainRoll ? tas.mainRoll.themeColors() : "{}"; ownerWindow: tas; visible: false }
}
