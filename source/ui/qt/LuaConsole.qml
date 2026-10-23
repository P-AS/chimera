// The Lua Console, as Chimera's: its menus and toolbar; the scripts on the
// left, their count above them; the output on the right, with the line to
// type Lua into under it; the session's name at the bottom. Active
// Registered Functions and Lua Functions are windows of their own. Layout
// and bindings only, over the luaConsole view model.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "luaConsoleWindow"
    required property QtObject vm
    // the main window's: Paths folders, file URLs, the clipboard, theme colours
    property var folderOf: null
    property var fileUrl: (path) => "file://" + path
    property var localPath: (url) => url.toString()
    property QtObject clipboard: null
    property var themeColor: (role, fallback) => fallback
    property QtObject customize: null
    property QtObject toolWindows: null
    property Window mainWindow: null
    title: qsTr("Lua Console")
    width: 760
    height: 460

    function editFiles(paths) {
        for (const p of paths.split("\n")) if (p !== "") Qt.openUrlExternally(win.fileUrl(p))
    }
    function duplicateScript() {
        const name = win.vm.run("duplicateName")
        // a separator is copied at once, as Chimera's
        if (name === "") { win.vm.run("duplicate", [""]); return }
        duplicateDialog.currentFolder = win.fileUrl(name.substring(0, Math.max(name.lastIndexOf("/"), name.lastIndexOf("\\"))))
        duplicateDialog.selectedFile = win.fileUrl(name)
        duplicateDialog.open()
    }
    // Save Window Position, Stay on Top, Float From Main Window
    WindowOptions { id: windowOptions; window: win; tool: "luaConsole"; vm: win.toolWindows; mainWindow: win.mainWindow }
    onVisibleChanged: if (visible) windowOptions.placeBack(false)

    function menuOpened() { if (win.customize) win.customize.run("menuOpened") }
    function menuClosed() { if (win.customize) win.customize.run("menuClosed") }

    // Chimera asks before a saved session with changes is let go
    property var afterAsk: null
    property bool closingAsked: false
    function leaveSession(then) {
        if (!win.vm.changes || win.vm.sessionPath === "") { then(); return }
        win.afterAsk = then
        askSave.open()
    }
    MessageDialog {
        id: askSave
        title: qsTr("Closing with Unsaved Changes")
        text: qsTr("Save Lua Console session?")
        buttons: MessageDialog.Yes | MessageDialog.No | MessageDialog.Cancel
        onButtonClicked: (button) => {
            const then = win.afterAsk
            win.afterAsk = null
            if (button === MessageDialog.Cancel) return
            if (button === MessageDialog.Yes) win.vm.run("saveSession")
            if (then) then()
        }
    }
    // closing it stops every script, as Chimera's
    onClosing: (close) => {
        if (win.closingAsked) { win.closingAsked = false; return }
        if (win.vm.changes && win.vm.sessionPath !== "") {
            close.accepted = false
            win.leaveSession(() => { win.vm.run("closed"); win.closingAsked = true; win.close() })
            return
        }
        win.vm.run("closed")
    }

    FileDialog {
        id: openScriptDialog
        title: qsTr("Open Script")
        fileMode: FileDialog.OpenFiles
        currentFolder: win.folderOf ? win.folderOf("Lua") : ""
        nameFilters: [qsTr("Lua Scripts (*.lua *.txt)"), qsTr("All files (*)")]
        onAccepted: { for (const f of selectedFiles) win.vm.run("openScript", [f]) }
    }
    FileDialog {
        id: newScriptDialog
        title: qsTr("New Script")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "lua"
        currentFolder: win.folderOf ? win.folderOf("Lua") : ""
        nameFilters: [qsTr("Lua Scripts (*.lua)")]
        onAccepted: {
            const made = win.vm.run("newScript", [selectedFile])
            if (made) win.editFiles(made)
        }
    }
    FileDialog {
        id: duplicateDialog
        title: qsTr("Duplicate Script")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "lua"
        nameFilters: [qsTr("Lua Scripts (*.lua)")]
        onAccepted: {
            win.vm.run("duplicate", [selectedFile])
            win.editFiles(win.localPath(selectedFile))
        }
    }
    FileDialog {
        id: openSessionDialog
        title: qsTr("Open Session")
        currentFolder: win.folderOf ? win.folderOf("Lua") : ""
        nameFilters: [qsTr("Lua Session Files (*.luases)"), qsTr("All files (*)")]
        onAccepted: win.vm.run("openSession", [selectedFile])
    }
    FileDialog {
        id: saveSessionDialog
        title: qsTr("Save Session As")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "luases"
        currentFolder: win.folderOf ? win.folderOf("Lua") : ""
        nameFilters: [qsTr("Lua Session Files (*.luases)")]
        onAccepted: win.vm.run("saveSessionAs", [selectedFile])
    }

    // a menu item showing its own shortcut, as Chimera's menus do
    Component {
        id: shortcutItem
        MenuItem {
            id: item
            readonly property string keys: action && typeof action.shortcut === "string" ? action.shortcut
                : action && action.keysShown ? action.keysShown : ""
            rightPadding: padding + (keys !== "" ? keyLabel.implicitWidth + 24 : 0)
            Label {
                id: keyLabel
                text: item.keys
                anchors.right: parent.right
                anchors.rightMargin: item.padding
                anchors.verticalCenter: parent.verticalCenter
                color: item.highlighted ? item.palette.highlightedText : item.palette.text
            }
        }
    }

    // the actions, shared by the menus, the toolbar and the list's menu
    readonly property bool anyFile: win.vm.selectedScripts > 0
    readonly property bool anyRow: win.vm.selectedCount > 0
    Action { id: newScriptAction; text: qsTr("New Script"); icon.source: "qrc:/chimera/icons/NewFile.png"; shortcut: "Ctrl+N"; onTriggered: newScriptDialog.open() }
    Action { id: openScriptAction; text: qsTr("&Open Script..."); icon.source: "qrc:/chimera/icons/OpenFile.png"; shortcut: "Ctrl+O"; onTriggered: openScriptDialog.open() }
    Action { id: refreshAction; text: qsTr("Re&fresh"); icon.source: "qrc:/chimera/icons/Refresh.png"; shortcut: "F5"; onTriggered: win.vm.run("refresh") }
    Action { id: toggleAction; text: qsTr("&Toggle"); icon.source: "qrc:/chimera/icons/checkbox.png"; shortcut: "Ctrl+T"; onTriggered: win.vm.run("toggle") }
    Action { id: pauseAction; text: qsTr("Pause or Resume"); icon.source: "qrc:/chimera/icons/Pause.png"; enabled: win.anyFile; onTriggered: win.vm.run("pause") }
    Action { id: editAction; text: qsTr("&Edit Script"); icon.source: "qrc:/chimera/icons/pencil.png"; shortcut: "Ctrl+E"; enabled: win.anyFile; onTriggered: win.editFiles(win.vm.run("editPaths")) }
    Action { id: removeAction; text: qsTr("&Remove Script"); icon.source: "qrc:/chimera/icons/Delete.png"; shortcut: "Delete"; enabled: win.anyRow; onTriggered: win.vm.run("remove") }
    Action { id: duplicateAction; text: qsTr("&Duplicate Script"); icon.source: "qrc:/chimera/icons/Duplicate.png"; enabled: win.anyRow; onTriggered: win.duplicateScript() }
    Action { id: clearAction; text: qsTr("&Clear Output"); icon.source: "qrc:/chimera/icons/clear_console.png"; onTriggered: win.vm.run("clear") }
    Action { id: separatorAction; text: qsTr("Insert Separator"); icon.source: "qrc:/chimera/icons/InsertSeparator.png"; shortcut: "Ctrl+I"; onTriggered: win.vm.run("insertSeparator") }
    Action { id: upAction; text: qsTr("Move &Up"); icon.source: "qrc:/chimera/icons/MoveUp.png"; shortcut: "Ctrl+U"; enabled: win.anyRow; onTriggered: win.vm.run("moveUp") }
    Action { id: downAction; text: qsTr("Move &Down"); icon.source: "qrc:/chimera/icons/MoveDown.png"; shortcut: "Ctrl+D"; enabled: win.anyRow; onTriggered: win.vm.run("moveDown") }
    Action { id: selectAllAction; property string keysShown: "Ctrl+A"; text: qsTr("Select &All"); enabled: scriptList.count > 0; onTriggered: win.vm.run("selectAll") }
    Action { id: stopAllAction; text: qsTr("Stop All Scripts"); icon.source: "qrc:/chimera/icons/Stop.png"; enabled: win.vm.anyEnabled; onTriggered: win.vm.run("stopAll") }
    Action { id: functionsAction; text: qsTr("&Registered Functions..."); shortcut: "F12"; onTriggered: { functionsWindow.show(); functionsWindow.raise() } }
    Action { id: eraseAction; text: qsTr("Erase Stale/Stuck Lua Drawing Layers"); icon.source: "qrc:/chimera/icons/Erase.png"; onTriggered: win.vm.run("eraseDrawings") }
    Action { id: clearFunctionsAction; text: qsTr("Clear Registered Functions"); icon.source: "qrc:/chimera/icons/Delete.png"; enabled: win.vm.functionCount > 0; onTriggered: win.vm.run("removeAllFunctions") }

    menuBar: MenuBar {
        Menu {
            title: qsTr("&File")
            delegate: shortcutItem
            onAboutToShow: win.menuOpened()
            onAboutToHide: win.menuClosed()
            Action {
                text: qsTr("&New Session"); shortcut: "Ctrl+Shift+N"; icon.source: "qrc:/chimera/icons/NewFile.png"
                onTriggered: win.leaveSession(() => win.vm.run("newSession"))
            }
            Action {
                text: qsTr("&Open Session..."); shortcut: "Ctrl+Shift+O"; icon.source: "qrc:/chimera/icons/OpenFile.png"
                onTriggered: win.leaveSession(() => openSessionDialog.open())
            }
            Action {
                text: qsTr("&Save Session"); shortcut: "Ctrl+S"; icon.source: "qrc:/chimera/icons/SaveAs.png"
                enabled: win.vm.changes
                onTriggered: win.vm.sessionPath === "" ? saveSessionDialog.open() : win.vm.run("saveSession")
            }
            Action { text: qsTr("Save Session &As..."); shortcut: "Ctrl+Shift+S"; onTriggered: saveSessionDialog.open() }
            MenuSeparator {}
            Menu {
                id: recentSessionsMenu
                title: qsTr("Recent Sessions")
                enabled: win.vm.recentSessions !== ""
                Instantiator {
                    model: win.vm.recentSessions === "" ? [] : win.vm.recentSessions.split("\n")
                    delegate: MenuItem {
                        required property string modelData
                        text: modelData
                        onTriggered: win.leaveSession(() => win.vm.run("openSession", [modelData]))
                    }
                    onObjectAdded: (index, object) => recentSessionsMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => recentSessionsMenu.removeItem(object)
                }
            }
            Menu {
                id: recentScriptsMenu
                title: qsTr("Recent Scripts")
                icon.source: "qrc:/chimera/icons/Recent.png"
                enabled: win.vm.recentScripts !== ""
                Instantiator {
                    model: win.vm.recentScripts === "" ? [] : win.vm.recentScripts.split("\n")
                    delegate: MenuItem {
                        required property string modelData
                        text: modelData
                        onTriggered: win.vm.run("openScript", [modelData])
                    }
                    onObjectAdded: (index, object) => recentScriptsMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => recentScriptsMenu.removeItem(object)
                }
            }
            MenuSeparator {}
            Action { property string keysShown: "Alt+F4"; text: qsTr("&Close"); onTriggered: win.close() }
        }
        Menu {
            title: qsTr("&Script")
            delegate: shortcutItem
            onAboutToShow: win.menuOpened()
            onAboutToHide: win.menuClosed()
            MenuItem { action: newScriptAction }
            MenuItem { action: openScriptAction }
            MenuItem { action: refreshAction }
            MenuItem { action: toggleAction; enabled: win.anyFile }
            MenuItem { action: pauseAction }
            MenuItem { action: editAction }
            MenuItem { action: removeAction }
            MenuItem { action: duplicateAction }
            MenuItem { action: clearAction }
            MenuSeparator {}
            MenuItem { action: separatorAction }
            MenuItem { action: upAction }
            MenuItem { action: downAction }
            MenuItem { action: selectAllAction }
            MenuSeparator {}
            MenuItem { action: stopAllAction }
            MenuItem { action: functionsAction }
        }
        Menu {
            title: qsTr("&Settings")
            onAboutToShow: win.menuOpened()
            onAboutToHide: win.menuClosed()
            MenuItem { text: qsTr("Disable Scripts on Load"); checkable: true; checked: win.vm.disableOnLoad; onTriggered: win.vm.disableOnLoad = checked }
            MenuItem { text: qsTr("Toggle All if None Selected"); checkable: true; checked: win.vm.toggleAllIfNoneSelected; onTriggered: win.vm.toggleAllIfNoneSelected = checked }
            MenuItem { text: qsTr("Reload When Script File Changes"); checkable: true; checked: win.vm.reloadOnChange; onTriggered: win.vm.reloadOnChange = checked }
            MenuItem { text: qsTr("Duplicate to Stdout"); checkable: true; checked: win.vm.duplicateToStdout; onTriggered: win.vm.duplicateToStdout = checked }
            MenuSeparator {}
            // Chimera's: Sublime Text 2's Lua completions written for it; said as found ("detected") or written ("installed")
            Menu {
                title: qsTr("Register To Text Editors")
                onAboutToShow: win.vm.run("textEditors")
                MenuItem {
                    text: win.vm.sublimeState === "installed" ? qsTr("Sublime Text 2 (installed)")
                        : win.vm.sublimeState === "detected" ? qsTr("Sublime Text 2 (detected)") : qsTr("&Sublime Text 2")
                    font.italic: win.vm.sublimeState === "detected"
                    checkable: true
                    checked: win.vm.sublimeState === "installed"
                    onTriggered: {
                        win.vm.run("registerSublime")
                        checked = Qt.binding(() => win.vm.sublimeState === "installed")
                    }
                }
            }
            MenuSeparator {}
            MenuItem { text: qsTr("Save Window &Position"); checkable: true; checked: windowOptions.savePosition; onTriggered: windowOptions.set("save", checked) }
            MenuItem {
                text: qsTr("Stay on &Top"); checkable: true; checked: windowOptions.onTop
                visible: windowOptions.canStayOnTop; height: visible ? implicitHeight : 0
                onTriggered: windowOptions.set("top", checked)
            }
            MenuItem { text: qsTr("&Float From Main Window"); checkable: true; checked: windowOptions.floating; onTriggered: windowOptions.set("float", checked) }
            MenuItem { text: qsTr("Restore &Defaults"); onTriggered: { win.vm.run("restoreDefaults"); windowOptions.restore() } }
        }
        Menu {
            title: qsTr("&Help")
            delegate: shortcutItem
            onAboutToShow: win.menuOpened()
            onAboutToHide: win.menuClosed()
            Action { text: qsTr("&Lua Functions List"); shortcut: "F1"; onTriggered: { functionsListWindow.show(); functionsListWindow.raise() } }
        }
    }

    header: ToolBar {
        RowLayout {
            spacing: 0
            component Tool: ToolButton {
                property string tip: action ? action.text.replace("&", "") : ""
                display: AbstractButton.IconOnly
                ToolTip.visible: hovered
                ToolTip.text: tip
                ToolTip.delay: 500
            }
            Tool { action: newScriptAction; tip: qsTr("New Lua Script") }
            Tool { action: openScriptAction; tip: qsTr("Open Script") }
            Tool { action: toggleAction; tip: qsTr("Toggle Script") }
            Tool { action: refreshAction; tip: qsTr("Refresh") }
            Tool { action: pauseAction }
            Tool { action: editAction; tip: qsTr("Edit Script") }
            Tool { action: removeAction; tip: qsTr("Remove Script") }
            Tool { action: duplicateAction; tip: qsTr("Duplicate Script") }
            ToolSeparator {}
            Tool { action: upAction; tip: qsTr("Move Up") }
            Tool { action: downAction; tip: qsTr("Move Down") }
            Tool { action: separatorAction }
            ToolSeparator {}
            Tool { action: clearAction; tip: qsTr("Clear Output") }
            Tool { action: eraseAction }
        }
    }

    footer: Label {
        objectName: "luaConsoleStatus"
        text: win.vm.error !== "" ? win.vm.error : win.vm.status
        padding: 4
        elide: Text.ElideRight
    }

    // the list's right-click menu
    Menu {
        id: scriptMenu
        onAboutToShow: win.menuOpened()
        onAboutToHide: win.menuClosed()
        MenuItem { text: qsTr("&Toggle"); icon.source: "qrc:/chimera/icons/Refresh.png"; enabled: win.anyFile; onTriggered: win.vm.run("toggle") }
        MenuItem { text: qsTr("Pause or Resume"); icon.source: "qrc:/chimera/icons/Pause.png"; enabled: win.anyFile; onTriggered: win.vm.run("pause") }
        MenuItem { text: qsTr("&Edit"); icon.source: "qrc:/chimera/icons/Cut.png"; enabled: win.anyFile; onTriggered: win.editFiles(win.vm.run("editPaths")) }
        MenuItem { text: qsTr("&Remove"); icon.source: "qrc:/chimera/icons/Close.png"; enabled: win.anyRow; onTriggered: win.vm.run("remove") }
        MenuItem { text: qsTr("Insert Seperator"); icon.source: "qrc:/chimera/icons/InsertSeparator.png"; onTriggered: win.vm.run("insertSeparator") }
        MenuSeparator { visible: win.vm.anyEnabled; height: visible ? implicitHeight : 0 }
        MenuItem { action: stopAllAction; visible: win.vm.anyEnabled; height: visible ? implicitHeight : 0 }
        MenuItem { action: clearFunctionsAction }
    }
    // the output's right-click menu
    Menu {
        id: outputMenu
        onAboutToShow: win.menuOpened()
        onAboutToHide: win.menuClosed()
        MenuItem {
            text: qsTr("Copy"); enabled: win.vm.outputFirst >= 0
            onTriggered: if (win.clipboard) win.clipboard.setText(win.vm.run("copyOutput"))
        }
        MenuItem { text: qsTr("Select &All"); enabled: win.vm.lines > 0; onTriggered: win.vm.run("selectAllOutput") }
        MenuItem { text: qsTr("&Clear"); enabled: win.vm.lines > 0; onTriggered: win.vm.run("clear") }
        MenuSeparator {}
        MenuItem { text: qsTr("&Registered Functions"); enabled: win.vm.functionCount > 0; onTriggered: functionsAction.trigger() }
        MenuItem { action: clearFunctionsAction }
    }

    // Reload When Script File Changes: the files looked at twice a second
    Timer { interval: 500; repeat: true; running: win.vm.reloadOnChange && scriptList.count > 0; onTriggered: win.vm.run("checkFiles") }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal

        ColumnLayout {
            SplitView.preferredWidth: 300
            SplitView.minimumWidth: 120
            spacing: 0
            Label { objectName: "luaScriptCount"; text: win.vm.summary; padding: 4 }
            // the columns: the state's icon, the script's name, its path;
            // a click on a heading sorts by it
            RowLayout {
                spacing: 0
                Layout.fillWidth: true
                Repeater {
                    model: [{key: "Path", text: " ", width: 24}, {key: "Script", text: qsTr("Script"), width: 92}, {key: "Path", text: qsTr("Path"), width: -1}]
                    delegate: Button {
                        required property var modelData
                        text: modelData.text
                        flat: true
                        padding: 2
                        Layout.preferredWidth: modelData.width > 0 ? modelData.width : -1
                        Layout.fillWidth: modelData.width < 0
                        onClicked: win.vm.run("sort", [modelData.key])
                    }
                }
            }
            ListView {
                id: scriptList
                objectName: "luaScriptList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                focus: true
                model: win.vm.rows("scripts")
                ScrollBar.vertical: ScrollBar {}
                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_A && (event.modifiers & Qt.ControlModifier)) { win.vm.run("selectAll"); event.accepted = true }
                }
                delegate: Rectangle {
                    id: row
                    required property int index
                    required property var model
                    readonly property color ink: row.model.selected ? palette.highlightedText : palette.text
                    width: scriptList.width
                    height: 22
                    color: row.model.selected ? palette.highlight
                         : row.model.separator ? palette.window
                         : row.model.state === "paused" ? win.themeColor("RowPaused", "#FFB6C1")
                         : row.model.state === "running" ? win.themeColor("RowActive", "#E0FFFF")
                         : palette.base
                    Image {
                        x: 4
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !row.model.separator
                        source: row.model.state === "running" ? "qrc:/chimera/icons/arrow_green.png"
                              : row.model.state === "paused" ? "qrc:/chimera/icons/Pause.png" : "qrc:/chimera/icons/Stop.png"
                    }
                    Label {
                        x: 26; width: 88
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.model.name
                        elide: Text.ElideRight
                        color: row.ink
                    }
                    Label {
                        x: 118; width: parent.width - 120
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.model.path
                        elide: Text.ElideLeft
                        color: row.ink
                    }
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onPressed: (mouse) => {
                            scriptList.forceActiveFocus()
                            if (mouse.button === Qt.RightButton) {
                                if (!row.model.selected) win.vm.run("select", [row.index, "only"])
                                scriptMenu.popup()
                                return
                            }
                            win.vm.run("select", [row.index, (mouse.modifiers & Qt.ShiftModifier) ? "range"
                                : (mouse.modifiers & Qt.ControlModifier) ? "toggle" : "only"])
                        }
                        onDoubleClicked: (mouse) => { if (mouse.button === Qt.LeftButton) win.vm.run("toggleRow", [row.index]) }
                    }
                }
                // a click below the rows selects none; a right click there opens the menu
                MouseArea {
                    z: -1
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onPressed: (mouse) => {
                        scriptList.forceActiveFocus()
                        win.vm.run("select", [-1, "none"])
                        if (mouse.button === Qt.RightButton) scriptMenu.popup()
                    }
                }
            }
        }

        GroupBox {
            title: qsTr("Output")
            SplitView.fillWidth: true
            ColumnLayout {
                anchors.fill: parent
                spacing: 4
                ListView {
                    id: output
                    objectName: "luaOutput"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: win.vm.rows("output")
                    ScrollBar.vertical: ScrollBar {}
                    onCountChanged: positionViewAtEnd()
                    delegate: Rectangle {
                        id: outLine
                        required property int index
                        required property var model
                        width: output.width
                        height: lineText.implicitHeight
                        color: outLine.model.selected ? palette.highlight : "transparent"
                        Label {
                            id: lineText
                            width: parent.width
                            text: outLine.model.line
                            font.family: "monospace"
                            wrapMode: Text.Wrap
                            color: outLine.model.selected ? palette.highlightedText : palette.text
                        }
                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            onPressed: (mouse) => {
                                output.forceActiveFocus()
                                if (mouse.button === Qt.RightButton) { outputMenu.popup(); return }
                                win.vm.run("selectOutput", [outLine.index, (mouse.modifiers & Qt.ShiftModifier) ? "range" : "only"])
                            }
                        }
                    }
                    MouseArea {
                        z: -1
                        anchors.fill: parent
                        acceptedButtons: Qt.RightButton
                        onPressed: outputMenu.popup()
                    }
                    Keys.onPressed: (event) => {
                        if (event.matches(StandardKey.Copy) && win.clipboard) { win.clipboard.setText(win.vm.run("copyOutput")); event.accepted = true }
                        else if (event.matches(StandardKey.SelectAll)) { win.vm.run("selectAllOutput"); event.accepted = true }
                    }
                }
                // a line of Lua: Enter runs it, Up and Down go through the lines typed before
                TextField {
                    id: input
                    objectName: "luaInput"
                    Layout.fillWidth: true
                    font.family: "monospace"
                    function runLine() { const c = text; clear(); win.vm.run("execute", [c]) }
                    function recall(step) {
                        const t = win.vm.run("recall", [step])
                        if (t !== undefined && t !== null) { text = t; cursorPosition = text.length }
                    }
                    Keys.onReturnPressed: runLine()
                    Keys.onEnterPressed: runLine()
                    Keys.onUpPressed: recall(1)
                    Keys.onDownPressed: recall(-1)
                }
            }
        }
    }

    // Active Registered Functions, as Chimera's: by event and name, each to
    // call or remove
    ApplicationWindow {
        id: functionsWindow
        objectName: "registeredFunctionsWindow"
        title: qsTr("Active Registered Functions")
        width: 560
        height: 300
        property int current: -1
        property string currentGuid: ""
        function pick(i, guid) { functionsWindow.current = i; functionsWindow.currentGuid = guid }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            RowLayout {
                spacing: 0
                Label { text: qsTr("Event"); Layout.preferredWidth: 150; font.bold: true }
                Label { text: qsTr("Name"); Layout.preferredWidth: 130; font.bold: true }
                Label { text: qsTr("Guid"); Layout.fillWidth: true; font.bold: true }
            }
            ListView {
                id: functionList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: win.vm.rows("functions")
                ScrollBar.vertical: ScrollBar {}
                focus: true
                onCountChanged: functionsWindow.pick(-1, "")
                Keys.onPressed: (event) => {
                    if (functionsWindow.current < 0) return
                    if (event.key === Qt.Key_Delete) { removeButton.clicked(); event.accepted = true }
                    else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) { callButton.clicked(); event.accepted = true }
                }
                delegate: Rectangle {
                    id: fn
                    required property int index
                    required property var model
                    readonly property bool picked: fn.index === functionsWindow.current
                    width: functionList.width
                    height: 22
                    color: fn.picked ? palette.highlight : "transparent"
                    RowLayout {
                        anchors.fill: parent
                        spacing: 0
                        Label { text: fn.model.event; Layout.preferredWidth: 150; elide: Text.ElideRight; color: fn.picked ? palette.highlightedText : palette.text }
                        Label { text: fn.model.name; Layout.preferredWidth: 130; elide: Text.ElideRight; color: fn.picked ? palette.highlightedText : palette.text }
                        Label { text: fn.model.guid; Layout.fillWidth: true; elide: Text.ElideRight; font.family: "monospace"; color: fn.picked ? palette.highlightedText : palette.text }
                    }
                    MouseArea {
                        anchors.fill: parent
                        onPressed: { functionsWindow.pick(fn.index, fn.model.guid); functionList.forceActiveFocus() }
                        onDoubleClicked: callButton.clicked()
                    }
                }
            }
            RowLayout {
                Button {
                    id: callButton
                    text: qsTr("&Call")
                    enabled: functionsWindow.current >= 0
                    onClicked: win.vm.run("callFunction", [functionsWindow.currentGuid])
                }
                Button {
                    id: removeButton
                    text: qsTr("&Remove")
                    enabled: functionsWindow.current >= 0
                    onClicked: win.vm.run("removeFunction", [functionsWindow.currentGuid])
                }
                Button { text: qsTr("Remove &All"); enabled: win.vm.functionCount > 0; onClicked: win.vm.run("removeAllFunctions") }
                Item { Layout.fillWidth: true }
                Button { text: qsTr("&OK"); onClicked: functionsWindow.close() }
            }
        }
    }

    // Chimera's Lua Functions: every function Chimera documents, in its words
    // (Return, Library, Name, Parameters, Description), those the prototype
    // lacks marked [Not available] and deprecated ones [Deprecated]; a column's
    // name sorts by it (again: the other way), Filter finds a name or a word of
    // the description, Copy (the right button or Ctrl+C) puts the call and its
    // example on the clipboard, OK closes
    ApplicationWindow {
        id: functionsListWindow
        objectName: "luaFunctionsListWindow"
        title: qsTr("Lua Functions")
        width: 760
        height: 360
        minimumWidth: 200
        minimumHeight: 80
        property var entries: []
        // Chimera's sort: by library, then name, at first; a column's name sorts by it
        property int sortColumn: 1
        property bool descending: false
        property int picked: -1
        readonly property var keys: ["returns", "library", "name", "params", "description"]
        readonly property var widths: [64, 72, 160, 200, 300]
        onVisibleChanged: if (visible) {
            const all = JSON.parse(win.vm.run("functionsList") || "[]")
            all.sort((a, b) => a.library.localeCompare(b.library) || a.name.localeCompare(b.name))
            functionsListWindow.entries = all
            functionsListWindow.sortColumn = 1
            functionsListWindow.descending = false
            functionsListWindow.picked = -1
            filter.text = ""
            filter.forceActiveFocus()
        }
        function sortBy(column) {
            if (column === functionsListWindow.sortColumn) functionsListWindow.descending = !functionsListWindow.descending
            functionsListWindow.sortColumn = column
            const key = functionsListWindow.keys[column], down = functionsListWindow.descending
            const all = functionsListWindow.entries.slice()
            all.sort((a, b) => down ? b[key].localeCompare(a[key]) : a[key].localeCompare(b[key]))
            functionsListWindow.entries = all
        }
        readonly property var shown: functionsListWindow.entries.filter((e) => filter.text === ""
            || (e.library + "." + e.name).toLowerCase().indexOf(filter.text.toLowerCase()) >= 0
            || e.description.toLowerCase().indexOf(filter.text.toLowerCase()) >= 0)
        // Chimera's copy: "--library.name(parameters)", then the example
        function copyPicked() {
            const e = functionsListWindow.shown[functionsListWindow.picked]
            if (!e || !win.clipboard) return
            win.clipboard.setText("--" + e.library + "." + e.name + e.params + "\n" + (e.example !== "" ? e.example + "\n" : ""))
        }
        Shortcut { sequences: [StandardKey.Copy]; enabled: functionsListWindow.active && !filter.activeFocus; onActivated: functionsListWindow.copyPicked() }
        Menu {
            id: copyMenu
            MenuItem { text: qsTr("&Copy"); onTriggered: functionsListWindow.copyPicked() }
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: palette.base
                border.color: palette.mid
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 1
                    spacing: 0
                    Row {
                        Repeater {
                            model: [qsTr("Return"), qsTr("Library"), qsTr("Name"), qsTr("Parameters"), qsTr("Description")]
                            delegate: Label {
                                required property int index
                                required property string modelData
                                text: modelData
                                width: functionsListWindow.widths[index]
                                leftPadding: 4; topPadding: 2; bottomPadding: 2
                                elide: Text.ElideRight
                                background: Rectangle { color: palette.button; border.color: palette.mid }
                                TapHandler { onTapped: functionsListWindow.sortBy(parent.index) }
                            }
                        }
                    }
                    ListView {
                        id: docList
                        objectName: "luaFunctionsList"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        ScrollBar.vertical: ScrollBar {}
                        model: functionsListWindow.shown
                        delegate: Rectangle {
                            id: doc
                            required property var modelData
                            required property int index
                            width: docList.width
                            height: 20
                            readonly property bool isPicked: doc.index === functionsListWindow.picked
                            color: doc.isPicked ? palette.highlight : "transparent"
                            readonly property color ink: doc.isPicked ? palette.highlightedText : palette.text
                            ToolTip.visible: docHover.hovered && doc.modelData.description !== ""
                            ToolTip.delay: 800
                            ToolTip.text: doc.modelData.description
                            HoverHandler { id: docHover }
                            Row {
                                anchors.verticalCenter: parent.verticalCenter
                                Label { text: doc.modelData.returns; width: functionsListWindow.widths[0]; leftPadding: 4; elide: Text.ElideRight; color: doc.ink }
                                Label { text: doc.modelData.library; width: functionsListWindow.widths[1]; leftPadding: 4; elide: Text.ElideRight; color: doc.ink }
                                Label {
                                    text: (doc.modelData.unavailable ? qsTr("[Not available] ") : "") + (doc.modelData.deprecated ? qsTr("[Deprecated] ") : "") + doc.modelData.name
                                    width: functionsListWindow.widths[2]; leftPadding: 4; elide: Text.ElideRight; color: doc.ink
                                }
                                Label { text: doc.modelData.params; width: functionsListWindow.widths[3]; leftPadding: 4; elide: Text.ElideRight; color: doc.ink }
                                Label { text: doc.modelData.description; width: functionsListWindow.widths[4]; leftPadding: 4; elide: Text.ElideRight; color: doc.ink }
                            }
                            TapHandler {
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                onTapped: (point, button) => {
                                    functionsListWindow.picked = doc.index
                                    if (button === Qt.RightButton) copyMenu.popup()
                                }
                            }
                        }
                    }
                }
            }
            RowLayout {
                TextField { id: filter; Layout.preferredWidth: 180; onTextChanged: functionsListWindow.picked = -1 }
                Label { text: qsTr("Filter") }
                Item { Layout.fillWidth: true }
                Button { text: qsTr("&OK"); Layout.preferredWidth: 80; onClicked: functionsListWindow.close() }
            }
        }
    }
}
