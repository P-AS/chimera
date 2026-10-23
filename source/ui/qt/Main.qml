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
    required property QtObject hotkeyRelay
    required property QtObject projectDetails
    required property QtObject themes
    required property QtObject themeColors
    required property QtObject settings
    required property QtObject luaConsole
    required property QtObject branches
    required property QtObject cores
    required property QtObject clipboard
    required property QtObject pictureFiles
    required property QtObject help
    required property QtObject batchRun
    required property QtObject mediaMaker
    required property QtObject cacheManager
    required property QtObject customize
    required property QtObject messages
    required property QtObject display
    required property QtObject paths
    required property QtObject firmware
    required property QtObject configFiles
    required property QtObject dataFolder
    required property QtObject precompiled
    // a Paths folder for a file dialog to open in (made when it is not there)
    function folderOf(name) {
        root.paths.changed; root.paths.base; root.paths.lastRomFolder; root.paths.recentForRoms
        return root.pictureFiles.folderUrl(root.paths.run("dialogFolder", [name]) || "")
    }
    property url chosenPackage

    width: 1280
    height: 760
    visible: true
    // Display Nothing puts the speed in the title, as Chimera's does
    title: (session.isOpen && session.displayNothing ? qsTr("(%1 fps) - ").arg(Math.round(session.fps)) : "")
         + (!session.isOpen ? qsTr("Chimera")
         : session.projectTitle !== "" ? qsTr("Chimera - %1%2 - %3 (%4)").arg(session.unsaved ? "*" : "").arg(session.projectTitle).arg(session.coreName).arg(session.systemName)
         : qsTr("Chimera - %1 (%2)").arg(session.coreName).arg(session.systemName))

    // the theme's colours for what is drawn over the picture, as Chimera's themes give them
    readonly property var themeColorMap: JSON.parse(root.themes.colors)
    function osdColor(role, fallback) { return root.themeColors.resolve(root.themeColorMap[role] || fallback) }
    // Config > Messages: a colour left at Chimera's default is the theme's, one chosen there wins
    function messageColor(chosen, chimeraDefault, role) {
        return chosen.toUpperCase() === chimeraDefault ? root.osdColor(role, "#" + chimeraDefault.substring(3)) : chosen
    }
    readonly property color osdMessageColor: root.messageColor(root.messages.messagesColor, "#FFFFFFFF", "OsdMessage")
    readonly property color osdAlertColor: root.messageColor(root.messages.alertColor, "#FFFF0000", "OsdAlert")
    readonly property color osdMovieInputColor: root.messageColor(root.messages.movieInputColor, "#FF808080", "OsdMovieInput")
    // where each thing over the picture goes: a corner and a distance from it, as Chimera's MessagePosition
    readonly property var osdPositions: JSON.parse(root.messages.positions)
    function osdX(name, w) {
        const p = root.osdPositions[name]
        if (!p) return 0
        return p.anchor === 0 || p.anchor === 2 ? p.x : pictureArea.width - p.x - w
    }
    function osdY(name, h) {
        const p = root.osdPositions[name]
        if (!p) return 0
        return p.anchor < 2 ? p.y : pictureArea.height - p.y - h
    }
    // the keys of every hotkey, for the menus to show beside their actions
    readonly property var hotkeyKeys: JSON.parse(root.hotkeys.bound)
    // a menu item that shows the keys of the hotkey its action names
    Component {
        id: keyedMenuItem
        MenuItem {
            id: keyedItem
            readonly property string keys: action && action.hotkey ? (root.hotkeyKeys[action.hotkey] || "") : ""
            rightPadding: padding + (keys !== "" ? keyLabel.implicitWidth + 24 : 0)
            Label {
                id: keyLabel
                text: keyedItem.keys
                anchors.right: parent.right
                anchors.rightMargin: keyedItem.padding
                anchors.verticalCenter: parent.verticalCenter
                color: keyedItem.highlighted ? keyedItem.palette.highlightedText : keyedItem.palette.text
            }
        }
    }
    // what a hotkey does when it is the window's: a menu's action, a dialog, a window
    function selectBetweenMarkers() {
        const f = root.pianoRoll.currentFrame
        const before = root.pianoRoll.run("previousMarker", [f + 1])
        const after = root.pianoRoll.run("nextMarker", [f])
        root.pianoRoll.run("select", [before >= 0 ? before : 0, after >= 0 ? after - 1 : root.pianoRoll.frameCount - 1])
    }
    // Chimera asks before a project with changes is left - closed, another
    // opened, the program quit - and saves it first when told to
    property var afterAsk: null
    property var afterSave: null
    property bool leaving: false
    function leaveProject(then) {
        // Customize's "never ask to save": the changes are let go
        if (!root.session.unsaved || root.customize.neverAskSave) { then(); return }
        root.afterAsk = then
        askSaveDialog.open()
    }
    function quitProgram() { root.leaveProject(() => { root.leaving = true; Qt.quit() }) }
    onClosing: (close) => {
        if (root.session.unsaved && !root.leaving && !root.customize.neverAskSave) {
            close.accepted = false
            root.quitProgram()
        }
    }
    // frames copied or cut, by a menu, a script or a tool, go on the system's clipboard
    Connections {
        target: root.pianoRoll
        function onClipboardTextChanged() { root.clipboard.setText(root.pianoRoll.clipboardText) }
    }
    Connections {
        target: root.session
        // what waited for the save goes on once it is written
        function onUnsavedChanged() {
            if (!root.session.unsaved && root.afterSave) {
                const then = root.afterSave
                root.afterSave = null
                then()
            }
        }
    }
    readonly property var hotkeyActions: ({
        "RAM Search": ramSearchAction, "RAM Watch": ramWatchAction, "Hex Editor": hexEditorAction, "Lua Console": luaConsoleAction,
        "Undo": undoAction, "Redo": redoAction, "Sel. bet. Markers": betweenMarkersAction, "Select All": selectAllAction,
        "Clear Frames": clearFramesAction, "Delete Frames": deleteFramesAction, "Insert Frame": insertFrameAction,
        "Insert # Frames": insertFramesAction, "Clone Frames": cloneFramesAction, "Paste Insert": pasteInsertAction,
        "Clone # Times": cloneTimesAction, "Close ROM": closeAction
    })
    Connections {
        target: root.hotkeyRelay
        function onPressed(action) {
            if (action === "Full Screen") root.visibility = root.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen
            else if (action === "Open ROM") gameDialog.open()
            else if (action === "Exit Program") root.quitProgram()
            else if (action === "Load Last ROM") {
                const last = root.session.recentProjects.split("\n")[0]
                if (last !== "") root.leaveProject(() => root.session.run("openProject", [last]))
            }
            else if (action === "Seek To...") { if (root.session.seekEnabled) seekDialog.open() }
            else if (action === "Toggle Menu") root.menuBar.visible = !root.menuBar.visible
            else if (action === "Toggle Stay on Top") root.flags = root.flags ^ Qt.WindowStaysOnTopHint
            else if (action === "Larger Window" || action === "Smaller Window") {
                if (picture.pictureWidth <= 0) return
                const n = Math.max(1, Math.min(10, root.windowScale + (action === "Larger Window" ? 1 : -1)))
                root.sizeWindow(n)
                root.note(qsTr("Screensize set to %1x").arg(n))
            }
            else if (root.hotkeyActions[action] && root.hotkeyActions[action].enabled) root.hotkeyActions[action].trigger()
        }
    }

    menuBar: MenuBar {
        Menu {
            // Customize's "pause when a menu is open"
            onAboutToShow: root.customize.run("menuOpened")
            onAboutToHide: root.customize.run("menuClosed")
            delegate: keyedMenuItem
            title: qsTr("&File")
            // opens on the last project's answers, as Chimera's does
            Action { text: qsTr("&New Project..."); shortcut: "Ctrl+N"; onTriggered: { root.newProject.run("openOnLastProject"); newProjectWindow.show(); newProjectWindow.raise() } }
            Action { property string hotkey: "Open ROM"; text: qsTr("&Open Game..."); onTriggered: gameDialog.open() }
            Action { text: qsTr("Open Game &With a Core Package..."); onTriggered: packageDialog.open() }
            Action { text: qsTr("Open &Project..."); shortcut: "Ctrl+Shift+O"; onTriggered: projectDialog.open() }
            Menu {
                delegate: keyedMenuItem
                id: recentMenu
                title: qsTr("&Recent Projects")
                enabled: root.session.recentProjects !== ""
                Instantiator {
                    model: root.session.recentProjects === "" ? [] : root.session.recentProjects.split("\n")
                    delegate: MenuItem {
                        required property string modelData
                        text: modelData
                        onTriggered: root.leaveProject(() => root.session.run("openProject", [modelData]))
                    }
                    onObjectAdded: (index, object) => recentMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => recentMenu.removeItem(object)
                }
            }
            // a project not saved yet asks where it goes
            Action { text: qsTr("&Save Project"); shortcut: "Ctrl+S"; enabled: root.session.hasProject; onTriggered: root.session.projectPath === "" ? saveAsDialog.open() : root.session.run("saveProject") }
            Action { text: qsTr("Save Project &As..."); enabled: root.session.hasProject; onTriggered: saveAsDialog.open() }
            Action { text: qsTr("Save Project &Backup"); enabled: root.session.projectPath !== ""; onTriggered: root.session.run("saveBackup") }
            Action { text: qsTr("Make a &Project of This Game..."); enabled: root.session.isOpen && !root.session.hasProject; onTriggered: newProjectDialog.open() }
            // Chimera's File > Screenshot
            Menu {
                delegate: keyedMenuItem
                title: qsTr("Scree&nshot")
                enabled: root.session.isOpen
                Action { property string hotkey: "Screenshot"; text: qsTr("&Screenshot"); onTriggered: root.hotkeys.run("run", ["Screenshot"]) }
                Action {
                    text: qsTr("Screenshot &As...")
                    onTriggered: {
                        const suggested = root.pictureFiles.screenshotSuggestion(root.session.gameName)
                        screenshotAsDialog.currentFolder = suggested.toString().substring(0, suggested.toString().lastIndexOf("/"))
                        screenshotAsDialog.selectedFile = suggested
                        screenshotAsDialog.open()
                    }
                }
                Action { property string hotkey: "Screen Raw to Clipboard"; text: qsTr("&To Clipboard"); onTriggered: root.hotkeys.run("run", ["Screen Raw to Clipboard"]) }
                Action { property string hotkey: "Screen Client to Clipboard"; text: qsTr("&Client to Clipboard"); onTriggered: root.hotkeys.run("run", ["Screen Client to Clipboard"]) }
            }
            Action { property string hotkey: "Reboot Core"; text: qsTr("&Restart"); enabled: root.session.restartEnabled; onTriggered: root.session.run("restart") }
            Action { id: closeAction; property string hotkey: "Close ROM"; text: qsTr("&Close"); enabled: root.session.closeEnabled; onTriggered: root.leaveProject(() => root.session.run("close")) }
            MenuSeparator {}
            Action { property string hotkey: "Exit Program"; text: qsTr("E&xit"); onTriggered: root.quitProgram() }
        }
        Menu {
            // Customize's "pause when a menu is open"
            onAboutToShow: root.customize.run("menuOpened")
            onAboutToHide: root.customize.run("menuClosed")
            delegate: keyedMenuItem
            title: qsTr("&Emulation")
            Action { text: qsTr("&Play"); shortcut: "Ctrl+P"; enabled: root.session.playEnabled && !root.session.running; onTriggered: root.session.run("play") }
            Action { text: qsTr("P&ause"); shortcut: "Ctrl+Space"; enabled: root.session.running; onTriggered: root.session.run("pause") }
            Action { text: qsTr("&Settings..."); onTriggered: { settingsWindow.show(); settingsWindow.raise() } }
            Action { text: qsTr("&Controls and Hotkeys..."); onTriggered: { controlsWindow.show(); controlsWindow.raise() } }
            Action { text: qsTr("&Themes..."); onTriggered: { themesWindow.show(); themesWindow.raise() } }
            Action { text: qsTr("C&ustomize..."); onTriggered: { customizeWindow.show(); customizeWindow.raise() } }
            Action { text: qsTr("&Messages..."); onTriggered: { messagesWindow.show(); messagesWindow.raise() } }
            Action { text: qsTr("So&und..."); onTriggered: { soundWindow.show(); soundWindow.raise() } }
            Action { text: qsTr("&Display..."); onTriggered: { displayWindow.show(); displayWindow.raise() } }
            Action { text: qsTr("&Paths..."); onTriggered: { pathsWindow.show(); pathsWindow.raise() } }
            Action { text: qsTr("Fi&rmware..."); onTriggered: { firmwareWindow.show(); firmwareWindow.raise() } }
            Action { text: qsTr("Data Director&y..."); onTriggered: { dataFolderWindow.show(); dataFolderWindow.raise() } }
            // Chimera's Config menu's last four
            MenuSeparator {}
            Action { text: qsTr("Save Con&fig"); onTriggered: root.configFiles.run("save") }
            Action { text: qsTr("Save Config &As..."); onTriggered: saveConfigDialog.open() }
            Action { text: qsTr("&Load Config"); onTriggered: root.configFiles.run("load") }
            Action { text: qsTr("Load Config Fro&m..."); onTriggered: loadConfigDialog.open() }
            // Chimera's Config > Key Priority: for a key that is a hotkey and a button
            Menu {
                delegate: keyedMenuItem
                title: qsTr("&Key Priority")
                Action { text: qsTr("&Both Hotkeys and Controllers"); checkable: true; checked: root.hotkeys.keyPriority === 0; onTriggered: root.hotkeys.keyPriority = 0 }
                Action { text: qsTr("&Input overrides Hotkeys"); checkable: true; checked: root.hotkeys.keyPriority === 1; onTriggered: root.hotkeys.keyPriority = 1 }
                Action { text: qsTr("&Hotkeys override Input"); checkable: true; checked: root.hotkeys.keyPriority === 2; onTriggered: root.hotkeys.keyPriority = 2 }
            }
            Action { property string hotkey: "Frame Advance"; text: qsTr("&Frame Advance"); enabled: root.session.frameAdvanceEnabled; onTriggered: root.session.run("frameAdvance") }
            Action { text: qsTr("Go to Frame &0"); shortcut: "Ctrl+Home"; enabled: root.session.seekEnabled; onTriggered: root.session.run("seek", [0]) }
            Action { property string hotkey: "Seek To..."; text: qsTr("&Go to Frame..."); enabled: root.session.seekEnabled; onTriggered: seekDialog.open() }
            Action { property string hotkey: "Seek To Green Arrow"; text: qsTr("&Back to Where the Edits Began"); enabled: root.session.seekEnabled; onTriggered: root.session.run("seekToRestore") }
            Menu {
                delegate: keyedMenuItem
                title: qsTr("&Speed")
                Action { text: "50%"; checkable: true; checked: root.session.speedPercent === 50; onTriggered: root.session.speedPercent = 50 }
                Action { text: "75%"; checkable: true; checked: root.session.speedPercent === 75; onTriggered: root.session.speedPercent = 75 }
                Action { text: "100%"; checkable: true; checked: root.session.speedPercent === 100; onTriggered: root.session.speedPercent = 100 }
                Action { text: "150%"; checkable: true; checked: root.session.speedPercent === 150; onTriggered: root.session.speedPercent = 150 }
                Action { text: "200%"; checkable: true; checked: root.session.speedPercent === 200; onTriggered: root.session.speedPercent = 200 }
                Action { text: "400%"; checkable: true; checked: root.session.speedPercent === 400; onTriggered: root.session.speedPercent = 400 }
                MenuSeparator {}
                Action { property string hotkey: "Increase Speed"; text: qsTr("&Faster"); onTriggered: root.session.run("speedUp") }
                Action { property string hotkey: "Decrease Speed"; text: qsTr("&Slower"); onTriggered: root.session.run("speedDown") }
                Action { property string hotkey: "Reset Speed"; text: qsTr("&Its Own Speed"); onTriggered: root.session.run("speedReset") }
                Action {
                    property string hotkey: "Toggle Throttle"
                    text: qsTr("&Unthrottled"); checkable: true; checked: root.session.unthrottled
                    onTriggered: root.session.unthrottled = checked
                }
                // Chimera's Speed/Skip: frames not drawn while running
                MenuSeparator {}
                Action { text: qsTr("Auto-&minimize skipping"); checkable: true; checked: root.session.autoMinimizeSkipping; onTriggered: root.session.autoMinimizeSkipping = checked }
                Menu {
                    id: frameSkipMenu
                    title: qsTr("Frame Skip")
                    Instantiator {
                        model: 10
                        delegate: MenuItem {
                            required property int index
                            text: index === 0 ? qsTr("Skip &0 (never)") : qsTr("&%1").arg(index)
                            checkable: true
                            checked: root.session.frameSkip === index
                            onTriggered: root.session.run("setFrameSkip", [index])
                        }
                        onObjectAdded: (index, object) => frameSkipMenu.insertItem(index, object)
                        onObjectRemoved: (index, object) => frameSkipMenu.removeItem(object)
                    }
                }
            }
            Action { property string hotkey: "Clear Autohold"; text: qsTr("Clear Auto&hold"); enabled: root.session.autoheld !== "" || root.session.autofired !== ""; onTriggered: root.session.run("clearAutohold") }
            Action { text: qsTr("Auto&fire..."); onTriggered: autofireDialog.open() }
            Action {
                property string hotkey: "Toggle Sound"
                text: qsTr("S&ound"); checkable: true; checked: root.session.soundOn
                onTriggered: root.session.soundOn = checked
            }
            Action {
                property string hotkey: "Toggle Auto-Restore"
                text: qsTr("After an Edit, &Return to Where It Was")
                checkable: true
                checked: root.session.autoRestore
                onToggled: root.session.autoRestore = checked
            }
        }
        Menu {
            // Customize's "pause when a menu is open"
            onAboutToShow: root.customize.run("menuOpened")
            onAboutToHide: root.customize.run("menuClosed")
            delegate: keyedMenuItem
            title: qsTr("&View")
            Action { property string hotkey: "Display FPS"; text: qsTr("Display &FPS"); checkable: true; checked: root.session.showFps; onTriggered: root.session.showFps = checked }
            Action { property string hotkey: "Frame Counter"; text: qsTr("Display Frame &Counter"); checkable: true; checked: root.session.showFrameCounter; onTriggered: root.session.showFrameCounter = checked }
            Action { property string hotkey: "Lag Counter"; text: qsTr("Display &Lag Counter"); checkable: true; checked: root.session.showLagCounter; onTriggered: root.session.showLagCounter = checked }
            Action { property string hotkey: "Input Display"; text: qsTr("Display &Input"); checkable: true; checked: root.session.showInput; onTriggered: root.session.showInput = checked }
            Action { text: qsTr("Display &Subtitles"); checkable: true; checked: root.projectDetails.showSubtitles; onTriggered: root.projectDetails.showSubtitles = checked }
            Action { text: qsTr("Display &Average FPS"); checkable: true; checked: root.session.showAverageFps; onTriggered: root.session.showAverageFps = checked }
            Action { text: qsTr("Reset Average FPS"); enabled: root.session.showAverageFps; onTriggered: root.session.run("resetAverageFps") }
            Action { text: qsTr("Display &Rerecord Count"); checkable: true; checked: root.session.showRerecords; onTriggered: root.session.showRerecords = checked }
            MenuSeparator {}
            Action { text: qsTr("Display Status &Bar"); checkable: true; checked: root.session.showStatusBar; onTriggered: root.session.showStatusBar = checked }
            Action { text: qsTr("Display &Messages"); checkable: true; checked: root.session.showMessages; onTriggered: root.session.showMessages = checked }
            MenuSeparator {}
            Action { text: qsTr("Open &Log Window..."); onTriggered: { logWindow.show(); logWindow.raise() } }
            // the picture N times the machine's own, the window grown or shrunk to it
            Menu {
                id: windowSizeMenu
                title: qsTr("&Window Size")
                enabled: picture.pictureWidth > 0
                Instantiator {
                    model: 10
                    delegate: MenuItem {
                        required property int index
                        text: qsTr("&%1x").arg(index + 1)
                        onTriggered: root.sizeWindow(index + 1)
                    }
                    onObjectAdded: (index, object) => windowSizeMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => windowSizeMenu.removeItem(object)
                }
            }
        }
        Menu {
            // Customize's "pause when a menu is open"
            onAboutToShow: root.customize.run("menuOpened")
            onAboutToHide: root.customize.run("menuClosed")
            delegate: keyedMenuItem
            title: qsTr("&Tools")
            Action { text: qsTr("&Cores..."); onTriggered: { coresWindow.show(); coresWindow.raise() } }
            Action { text: qsTr("&Pre-Compiled Modules..."); onTriggered: { precompiledWindow.show(); precompiledWindow.raise() } }
            Action { id: ramSearchAction; property string hotkey: "RAM Search"; text: qsTr("RAM &Search..."); onTriggered: { ramSearchWindow.show(); ramSearchWindow.raise() } }
            Action { id: ramWatchAction; property string hotkey: "RAM Watch"; text: qsTr("RAM &Watch..."); onTriggered: { ramWatchWindow.show(); ramWatchWindow.raise() } }
            Action { id: hexEditorAction; property string hotkey: "Hex Editor"; text: qsTr("&Hex Editor..."); onTriggered: { hexEditorWindow.show(); hexEditorWindow.raise() } }
            Action { id: luaConsoleAction; property string hotkey: "Lua Console"; text: qsTr("&Lua Console..."); onTriggered: { luaConsoleWindow.show(); luaConsoleWindow.raise() } }
            Action { text: qsTr("C&ache Manager..."); onTriggered: { cacheManagerWindow.show(); cacheManagerWindow.raise() } }
            Action { text: qsTr("Reproducible &Media Maker..."); onTriggered: { mediaMakerWindow.show(); mediaMakerWindow.raise() } }
            Action { text: qsTr("&Batch Runner..."); onTriggered: { batchRunWindow.show(); batchRunWindow.raise() } }
            MenuSeparator {}
            // Chimera's: ticked while the core log is kept; off until asked, every run
            Action {
                text: qsTr("Export Core &Log...")
                checkable: true
                checked: root.session.coreLogPath !== ""
                onTriggered: {
                    checked = Qt.binding(() => root.session.coreLogPath !== "")
                    if (root.session.coreLogPath !== "") {
                        coreLogOffDialog.size = root.session.run("coreLogSize")
                        coreLogOffDialog.open()
                    } else {
                        coreLogFileDialog.selectedFile = "file:///" + root.session.run("coreLogName")
                        coreLogFileDialog.open()
                    }
                }
            }
        }
        Menu {
            // Customize's "pause when a menu is open"
            onAboutToShow: root.customize.run("menuOpened")
            onAboutToHide: root.customize.run("menuClosed")
            delegate: keyedMenuItem
            title: qsTr("&Help")
            Action { text: qsTr("Open toolassisted.run in Browser"); onTriggered: Qt.openUrlExternally(root.help.siteUrl) }
            Action {
                text: qsTr("&Copy Version Info")
                onTriggered: { root.clipboard.setText(root.help.run("versionReport")); root.note(qsTr("Version info copied")) }
            }
            Action { text: qsTr("&Report an Issue..."); onTriggered: { reportIssueWindow.show(); reportIssueWindow.raise() } }
            Action { text: qsTr("&About..."); onTriggered: aboutDialog.open() }
        }
        // Chimera's Emulator menu, Game for a game core: what the core open offers
        Menu {
            // Customize's "pause when a menu is open"
            onAboutToShow: root.customize.run("menuOpened")
            onAboutToHide: root.customize.run("menuClosed")
            delegate: keyedMenuItem
            title: root.session.isGameCore ? qsTr("&Game") : qsTr("E&mulator")
            enabled: root.session.isOpen && (root.session.hasGameTimer || root.session.hasSaveData || root.session.hasMovieImport)
            Action {
                property string hotkey: "Game Time"
                text: qsTr("Display Game &Time"); enabled: root.session.hasGameTimer
                checkable: true; checked: root.session.showGameTime; onTriggered: root.session.showGameTime = checked
            }
            Action {
                text: qsTr("Export Save &Data..."); enabled: root.session.hasSaveData
                onTriggered: {
                    const names = root.session.run("saveDataFiles")
                    if (names === undefined) return
                    if (names === "") { root.note(qsTr("This machine holds no save data yet.")); return }
                    const list = names.split("\n")
                    saveDataDialog.files = list.length
                    // one file under its own name; more in a zip named for the game
                    saveDataDialog.selectedFile = "file:///" + (list.length === 1 ? list[0].replace(/^.*\//, "")
                                                                                   : root.session.gameName + " (save data).zip")
                    saveDataDialog.open()
                }
            }
            Action {
                text: qsTr("&Import Movie..."); enabled: root.session.hasMovieImport
                onTriggered: { root.newProject.run("chooseCore", [root.session.coreName]); newProjectWindow.show(); newProjectWindow.raise() }
            }
        }
        Menu {
            // Customize's "pause when a menu is open"
            onAboutToShow: root.customize.run("menuOpened")
            onAboutToHide: root.customize.run("menuClosed")
            delegate: keyedMenuItem
            // TAStudio's editing, on the frames picked (or the current one), with its keys
            title: qsTr("E&dit")
            readonly property bool picked: root.pianoRoll.selectFirst >= 0
            readonly property int at: picked ? root.pianoRoll.selectFirst : root.pianoRoll.currentFrame
            id: editMenu
            Action {
                id: insertFrameAction; property string hotkey: "Insert Frame"
                text: qsTr("&Insert Frame"); enabled: root.session.isOpen
                onTriggered: root.pianoRoll.run("insertFrames", [Math.min(editMenu.at, root.pianoRoll.frameCount), 1])
            }
            Action { id: insertFramesAction; property string hotkey: "Insert # Frames"; text: qsTr("Insert &Number of Frames..."); enabled: root.session.isOpen; onTriggered: insertDialog.open() }
            Action {
                id: deleteFramesAction; property string hotkey: "Delete Frames"
                text: qsTr("&Delete Frames"); enabled: editMenu.picked
                onTriggered: root.pianoRoll.run("deleteFrames", [root.pianoRoll.selectFirst, root.pianoRoll.selectLast])
            }
            Action {
                id: cloneFramesAction; property string hotkey: "Clone Frames"
                text: qsTr("C&lone Frames"); enabled: editMenu.picked
                onTriggered: root.pianoRoll.run("cloneFrames", [root.pianoRoll.selectFirst, root.pianoRoll.selectLast])
            }
            Action {
                id: cloneTimesAction; property string hotkey: "Clone # Times"
                text: qsTr("Clone a Nu&mber of Times..."); enabled: editMenu.picked
                onTriggered: cloneDialog.open()
            }
            Action {
                id: clearFramesAction; property string hotkey: "Clear Frames"
                text: qsTr("Cl&ear Frames"); enabled: editMenu.picked
                onTriggered: root.pianoRoll.run("clearFrames", [root.pianoRoll.selectFirst, root.pianoRoll.selectLast])
            }
            MenuSeparator {}
            Action {
                text: qsTr("&Copy"); shortcut: StandardKey.Copy; enabled: editMenu.picked
                onTriggered: {
                    root.pianoRoll.run("copyFrames", [root.pianoRoll.selectFirst, root.pianoRoll.selectLast])
                    root.clipboard.setText(root.pianoRoll.clipboardText)
                }
            }
            Action {
                text: qsTr("Cu&t"); shortcut: StandardKey.Cut; enabled: editMenu.picked
                onTriggered: {
                    root.pianoRoll.run("cutFrames", [root.pianoRoll.selectFirst, root.pianoRoll.selectLast])
                    root.clipboard.setText(root.pianoRoll.clipboardText)
                }
            }
            Action {
                // the lines on the system's clipboard, as Chimera's Paste reads them
                text: qsTr("&Paste"); shortcut: StandardKey.Paste; enabled: root.session.isOpen && root.clipboard.hasText
                onTriggered: root.pianoRoll.run("pasteText", [Math.min(editMenu.at, root.pianoRoll.frameCount), root.clipboard.text(), false])
            }
            Action {
                id: pasteInsertAction; property string hotkey: "Paste Insert"
                text: qsTr("Paste In&sert"); enabled: root.session.isOpen && root.clipboard.hasText
                onTriggered: root.pianoRoll.run("pasteText", [Math.min(editMenu.at, root.pianoRoll.frameCount), root.clipboard.text(), true])
            }
            MenuSeparator {}
            Action {
                text: qsTr("T&runcate From Here"); enabled: root.session.isOpen && editMenu.at < root.pianoRoll.frameCount
                onTriggered: root.pianoRoll.run("truncate", [editMenu.at])
            }
            Action {
                id: selectAllAction; property string hotkey: "Select All"
                text: qsTr("Select &All"); enabled: root.pianoRoll.frameCount > 0
                onTriggered: root.pianoRoll.run("select", [0, root.pianoRoll.frameCount - 1])
            }
            Action {
                id: betweenMarkersAction; property string hotkey: "Sel. bet. Markers"
                text: qsTr("Select &Between Markers"); enabled: root.pianoRoll.frameCount > 0
                onTriggered: root.selectBetweenMarkers()
            }
            Action { text: qsTr("Select N&othing"); enabled: editMenu.picked; onTriggered: root.pianoRoll.run("select", [-1, -1]) }
            Action { property string hotkey: "Reselect Clip."; text: qsTr("Pick the Cop&ied Frames Again"); enabled: root.pianoRoll.clipboardFrames > 0; onTriggered: root.pianoRoll.run("reselectClipboard") }
            Action {
                text: qsTr("&Markers Move With the Frames")
                checkable: true
                checked: root.pianoRoll.bindMarkers
                onToggled: root.pianoRoll.bindMarkers = checked
            }
            MenuSeparator {}
            // TAStudio's macros, on the frames picked
            Action { text: qsTr("Save Selection to &Macro..."); enabled: editMenu.picked; onTriggered: saveMacroDialog.open() }
            Action { text: qsTr("Place Macro at Se&lection..."); enabled: editMenu.picked; onTriggered: placeMacroDialog.open() }
            Menu {
                id: recentMacrosMenu
                title: qsTr("Recent Macros")
                enabled: editMenu.picked && root.pianoRoll.recentMacros !== ""
                Instantiator {
                    model: root.pianoRoll.recentMacros === "" ? [] : root.pianoRoll.recentMacros.split("\n")
                    delegate: MenuItem {
                        required property string modelData
                        text: modelData
                        onTriggered: root.pianoRoll.run("placeMacro", [root.pianoRoll.selectFirst, modelData])
                    }
                    onObjectAdded: (index, object) => recentMacrosMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => recentMacrosMenu.removeItem(object)
                }
            }
            MenuSeparator {}
            Action { text: qsTr("TAStudio Se&ttings..."); onTriggered: { tastudioSettingsWindow.show(); tastudioSettingsWindow.raise() } }
        }
        Menu {
            // Customize's "pause when a menu is open"
            onAboutToShow: root.customize.run("menuOpened")
            onAboutToHide: root.customize.run("menuClosed")
            delegate: keyedMenuItem
            title: qsTr("Piano &Roll")
            Action { id: undoAction; property string hotkey: "Undo"; text: qsTr("&Undo"); enabled: root.pianoRoll.undoEnabled; onTriggered: root.pianoRoll.run("undo") }
            Action { id: redoAction; property string hotkey: "Redo"; text: qsTr("&Redo"); enabled: root.pianoRoll.redoEnabled; onTriggered: root.pianoRoll.run("redo") }
            Action { text: qsTr("Undo &History..."); onTriggered: { undoHistoryWindow.show(); undoHistoryWindow.raise() } }
            MenuSeparator {}
            Action { text: qsTr("&Branches..."); onTriggered: { branchesWindow.show(); branchesWindow.raise() } }
            // Chimera's TAStudio Metadata menu: the header, the comments, the subtitles
            Action { text: qsTr("Project &Details..."); enabled: root.session.hasProject; onTriggered: { projectDetailsWindow.show(); projectDetailsWindow.raise() } }
            Action { text: qsTr("Add &Marker Here..."); enabled: root.session.isOpen; onTriggered: markerDialog.open() }
            Action { property string hotkey: "Delete Marker"; text: qsTr("Remove Marker Here"); enabled: root.session.isOpen; onTriggered: root.pianoRoll.run("removeMarker", [root.pianoRoll.currentFrame]) }
            Action {
                property string hotkey: "Seek To Next Marker"
                text: qsTr("&Next Marker"); enabled: root.session.isOpen
                onTriggered: root.session.run("seek", [root.pianoRoll.run("nextMarker", [root.pianoRoll.currentFrame])])
            }
            Action {
                property string hotkey: "Seek To Prev Marker"
                text: qsTr("&Previous Marker"); enabled: root.session.isOpen
                onTriggered: root.session.run("seek", [root.pianoRoll.run("previousMarker", [root.pianoRoll.currentFrame])])
            }
            MenuSeparator {}
            Action { text: qsTr("Make the Inputs 1,000,000 Frames Long"); onTriggered: root.pianoRoll.run("fill", [1000000]) }
            Action { text: qsTr("&Clear Greenzone"); enabled: root.session.isOpen; onTriggered: root.session.run("clearGreenzone") }
            Action { text: qsTr("State &History Check"); enabled: root.session.isOpen; onTriggered: root.session.run("checkStateHistory") }
            Menu {
                delegate: keyedMenuItem
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
            // the columns shown, as Chimera's Columns menu has them
            // the console's columns first, then a menu a player's and the
            // keyboard keys', then a toggle for each group, as Chimera's
            Menu {
                id: columnsMenu
                title: qsTr("&Columns")
                enabled: root.session.isOpen
                Instantiator {
                    model: root.pianoRoll.rows("columns")
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
                        onTriggered: root.pianoRoll.run(shown ? "hideColumn" : "showColumn", [name])
                    }
                    onObjectAdded: (index, object) => columnsMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => columnsMenu.removeItem(object)
                }
                Instantiator {
                    id: groupMenus
                    model: root.pianoRoll.rows("columnGroups")
                    delegate: Menu {
                        id: groupMenu
                        required property string name
                        title: name
                        Instantiator {
                            model: root.pianoRoll.rows("columns")
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
                                onTriggered: root.pianoRoll.run(shown ? "hideColumn" : "showColumn", [name])
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
                Action { text: qsTr("Show &All"); onTriggered: root.pianoRoll.run("showAllColumns") }
                Instantiator {
                    model: root.pianoRoll.rows("columnGroups")
                    delegate: MenuItem {
                        required property string toggle
                        required property bool shown
                        required property bool firstOfToggle
                        text: qsTr("Show %1").arg(toggle)
                        visible: firstOfToggle
                        height: visible ? implicitHeight : 0
                        checkable: true
                        checked: shown
                        onTriggered: root.pianoRoll.run("showGroup", [toggle, !shown])
                    }
                    onObjectAdded: (index, object) => columnsMenu.addItem(object)
                    onObjectRemoved: (index, object) => columnsMenu.removeItem(object)
                }
            }
            Action {
                property string hotkey: "Toggle Follow Cursor"
                text: qsTr("&Follow the Current Frame")
                checkable: true
                checked: root.pianoRoll.followPlayback
                onToggled: root.pianoRoll.followPlayback = checked
            }
        }
    }

    Dialog {
        id: autofireDialog
        title: qsTr("Autofire")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAboutToShow: { fireOn.value = root.session.autofireOn; fireOff.value = root.session.autofireOff; fireLag.checked = root.session.autofireSkipsLag }
        GridLayout {
            columns: 2
            Label { text: qsTr("Pressed for") }
            SpinBox { id: fireOn; from: 1; to: 512; editable: true }
            Label { text: qsTr("Let go for") }
            SpinBox { id: fireOff; from: 1; to: 512; editable: true }
            CheckBox { id: fireLag; text: qsTr("Lag frames do not count"); Layout.columnSpan: 2 }
        }
        onAccepted: { root.session.autofireOn = fireOn.value; root.session.autofireOff = fireOff.value; root.session.autofireSkipsLag = fireLag.checked }
    }
    Dialog {
        id: cloneDialog
        title: qsTr("Clone the frames picked how many times")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        SpinBox { id: cloneTimes; from: 1; to: 100000; value: 1; editable: true }
        onAccepted: root.pianoRoll.run("cloneFrames", [root.pianoRoll.selectFirst, root.pianoRoll.selectLast, cloneTimes.value])
    }
    Dialog {
        id: seekDialog
        title: qsTr("Go to frame")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAboutToShow: seekTo.value = root.pianoRoll.currentFrame
        SpinBox { id: seekTo; from: 0; to: Math.max(0, root.pianoRoll.frameCount); editable: true }
        onAccepted: root.session.run("seek", [seekTo.value])
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
    RamWatch { id: ramWatchWindow; vm: root.ramWatch; folderOf: root.folderOf; visible: false }
    NewProject { id: newProjectWindow; vm: root.newProject; askFirst: root.leaveProject; visible: false }
    ProjectDetails { id: projectDetailsWindow; vm: root.projectDetails; visible: false }
    UndoHistory { id: undoHistoryWindow; vm: root.pianoRoll; visible: false }
    TAStudioSettings { id: tastudioSettingsWindow; session: root.session; pianoRoll: root.pianoRoll; branches: root.branches; visible: false }
    // a file dropped on the window starts a project from it, as in Chimera:
    // a file on its own is not what runs, a project is
    DropArea {
        anchors.fill: parent
        onDropped: (drop) => {
            if (!drop.hasUrls) return
            root.newProject.run("startFrom", [drop.urls[0]])
            newProjectWindow.show()
            newProjectWindow.raise()
            drop.accept()
        }
    }
    HexEditor { id: hexEditorWindow; vm: root.hexEditor; visible: false }
    Themes { id: themesWindow; vm: root.themes; colors: root.themeColors; visible: false }
    Controls { id: controlsWindow; controls: root.controls; hotkeys: root.hotkeys; keyNamer: root.keyNamer; session: root.session; visible: false }
    Settings { id: settingsWindow; settings: root.settings; visible: false }
    LuaConsole { id: luaConsoleWindow; vm: root.luaConsole; folderOf: root.folderOf; visible: false }
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
        currentFolder: root.folderOf("ROM")
        onAccepted: root.leaveProject(() => root.session.run("open", [root.chosenPackage, selectedFile]))
    }
    property url chosenProject
    FileDialog {
        id: projectDialog
        title: qsTr("Choose a project")
        currentFolder: root.folderOf("Movies")
        nameFilters: [qsTr("Chimera projects (*.chimeraProject)"), qsTr("All files (*)")]
        // the project's core is found among the installed ones
        onAccepted: root.leaveProject(() => root.session.run("openProject", [selectedFile]))
    }
    FileDialog {
        id: projectPackageDialog
        title: qsTr("Choose the core package to open it with")
        nameFilters: [qsTr("Core packages (*.chimeraCore)"), qsTr("All files (*)")]
        onAccepted: root.leaveProject(() => root.session.run("openProject", [root.chosenProject, selectedFile]))
    }
    FileDialog {
        id: saveConfigDialog
        title: qsTr("Save the settings as")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Settings (*.json)")]
        onAccepted: root.configFiles.run("saveAs", [selectedFile])
    }
    FileDialog {
        id: loadConfigDialog
        title: qsTr("Load the settings from")
        nameFilters: [qsTr("Settings (*.json)"), qsTr("All files (*)")]
        onAccepted: root.configFiles.run("loadFrom", [selectedFile])
    }
    FileDialog {
        id: screenshotAsDialog
        title: qsTr("Save the picture as")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("PNG pictures (*.png)"), qsTr("JPEG pictures (*.jpg)")]
        onAccepted: root.pictureFiles.save(selectedFile)
    }
    FileDialog {
        id: saveAsDialog
        title: qsTr("Save the project as")
        currentFolder: root.folderOf("Movies")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Chimera projects (*.chimeraProject)")]
        onAccepted: root.session.run("saveProject", [selectedFile])
        // a question that asked for this save is answered Cancel
        onRejected: root.afterSave = null
    }
    FileDialog {
        id: coreLogFileDialog
        title: qsTr("Export Core Log")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Text files (*.txt)")]
        onAccepted: {
            if (root.session.run("startCoreLog", [selectedFile]) === undefined) return
            if (root.session.isOpen) coreLogRebootDialog.open()
            else coreLogInfoDialog.open()
        }
    }
    Dialog {
        id: coreLogRebootDialog
        title: qsTr("Export Core Log")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        Label {
            text: qsTr("The core log is on. Reboot the core now, so the log covers it from the start? Some cores keep their full log only from boot.\n\nThen do what shows the problem, and come back to Tools > Export Core Log... to turn it off.")
            wrapMode: Text.WordWrap
            width: 420
        }
        onAccepted: root.session.run("restart")
    }
    Dialog {
        id: coreLogInfoDialog
        title: qsTr("Export Core Log")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok
        Label {
            text: qsTr("The core log is on. Load the game and do what shows the problem, then come back to Tools > Export Core Log... to turn it off.")
            wrapMode: Text.WordWrap
            width: 420
        }
    }
    Dialog {
        id: coreLogOffDialog
        property var size: 0
        title: qsTr("Export Core Log")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        Label {
            text: qsTr("The core log is on, and is being written to\n\n%1\n\n(%2 KB so far). Turn it off?").arg(root.session.coreLogPath).arg(Math.round(coreLogOffDialog.size / 1024))
            wrapMode: Text.WordWrap
            width: 420
        }
        onAccepted: root.session.run("stopCoreLog")
    }
    FileDialog {
        id: saveDataDialog
        property int files: 1
        title: qsTr("Export Save Data")
        fileMode: FileDialog.SaveFile
        nameFilters: files === 1 ? [qsTr("All files (*)")] : [qsTr("Zip Archives (*.zip)")]
        onAccepted: root.session.run("exportSaveData", [selectedFile])
    }
    FileDialog {
        id: saveMacroDialog
        title: qsTr("Save the frames picked as a macro")
        currentFolder: root.folderOf("Macros")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Movie Macros (*.chimeraMacro)")]
        onAccepted: root.pianoRoll.run("saveMacro", [root.pianoRoll.selectFirst, root.pianoRoll.selectLast, selectedFile])
    }
    FileDialog {
        id: placeMacroDialog
        title: qsTr("Place a macro at the frames picked")
        currentFolder: root.folderOf("Macros")
        nameFilters: [qsTr("Movie Macros (*.chimeraMacro)"), qsTr("All files (*)")]
        onAccepted: root.pianoRoll.run("placeMacro", [root.pianoRoll.selectFirst, selectedFile])
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
        onAccepted: root.pianoRoll.run("toggleAutoHold", [holdValueDialog.name, holdValueBox.value])
    }
    Dialog {
        id: askSaveDialog
        objectName: "askSaveDialog"
        title: qsTr("Closing with Unsaved Changes")
        anchors.centerIn: parent
        modal: true
        Label { text: qsTr("Save Chimera Project?") }
        footer: DialogButtonBox {
            Button {
                text: qsTr("Yes")
                onClicked: {
                    askSaveDialog.close()
                    root.afterSave = root.afterAsk
                    if (root.session.projectPath === "") saveAsDialog.open()
                    else root.session.run("saveProject")
                }
            }
            Button {
                text: qsTr("No")
                onClicked: { askSaveDialog.close(); const then = root.afterAsk; root.afterAsk = null; then() }
            }
            Button {
                text: qsTr("Cancel")
                onClicked: { askSaveDialog.close(); root.afterAsk = null }
            }
        }
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
        currentFolder: root.folderOf("ROM")
        // the installed core that takes this kind of file opens it; where it came from is remembered
        onAccepted: {
            root.paths.lastRomFolder = root.pictureFiles.folderOf(selectedFile)
            root.leaveProject(() => root.session.run("openGame", [selectedFile]))
        }
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
            id: pictureArea
            SplitView.preferredWidth: root.width * 0.58
            SplitView.minimumWidth: 200
            color: "black"
            PictureItem {
                id: picture
                objectName: "picture"
                anchors.fill: parent
                source: root.session.picture("screen")
                // Config > Display: its shape, bars or stretched, whole steps, crop and filter
                smooth: root.display.finalFilter === 1
                fit: ({ "aspectMode": root.display.aspectMode, "customWidth": root.display.customWidth,
                        "customHeight": root.display.customHeight, "ratioX": root.display.ratioX, "ratioY": root.display.ratioY,
                        "letterbox": root.display.letterbox ? 1 : 0, "integerScale": root.display.integerScale ? 1 : 0,
                        "cropLeft": root.display.cropLeft, "cropTop": root.display.cropTop,
                        "cropRight": root.display.cropRight, "cropBottom": root.display.cropBottom })
            }
            // Display Nothing: nothing is drawn here, the counters neither, as Chimera's
            Rectangle { anchors.fill: parent; z: 100; color: "black"; visible: root.session.displayNothing }
            // Chimera's counters over the picture, each where Config > Messages
            // puts it (at first Chimera's places: the speed at the top, the
            // frame counter 14 down, the input 28, the lag counter 42)
            component OsdText: Text {
                property string place: ""
                style: Text.Outline
                styleColor: "black"
                font.family: "monospace"
                font.pixelSize: 12
                x: place === "" ? 0 : root.osdX(place, width)
                y: place === "" ? 0 : root.osdY(place, height)
            }
            // the speed and the average speed share the speed's line, as Chimera's (issue 312)
            OsdText {
                objectName: "osdFps"
                place: "Fps"
                visible: root.session.isOpen && text !== ""
                text: [root.session.showFps ? root.session.fpsText : "",
                       root.session.showAverageFps ? root.session.averageFpsText : ""].filter(t => t !== "").join("   ")
                color: root.osdMessageColor
            }
            // a game's own timer, at the top right 14 down, as Chimera places it
            OsdText {
                objectName: "osdGameTime"
                place: "Game Time"
                visible: root.session.isOpen && root.session.showGameTime && text !== ""
                text: root.session.gameTimeText
                color: root.osdMessageColor
            }
            OsdText {
                objectName: "osdRerecords"
                place: "Rerecords"
                visible: root.session.isOpen && root.session.showRerecords
                text: root.session.rerecords
                color: root.osdMessageColor
            }
            // messages a moment each, the newest at their corner and the older
            // ones away from it, as Chimera stacks them (or only the newest)
            Column {
                objectName: "osdMessages"
                x: root.osdX("Messages", width)
                y: root.osdY("Messages", height)
                visible: root.session.showMessages
                Repeater {
                    model: {
                        const n = onScreenMessages.count
                        const all = []
                        for (let i = 0; i < n; i++) all.push(onScreenMessages.get(i).message)
                        const shown = root.messages.stackMessages ? all : all.slice(-1)
                        const p = root.osdPositions["Messages"]
                        return p && p.anchor < 2 ? shown.reverse() : shown
                    }
                    delegate: OsdText {
                        required property string modelData
                        x: 0
                        y: 0
                        text: modelData
                        color: root.osdMessageColor
                    }
                }
            }
            OsdText {
                objectName: "osdFrameCounter"
                place: "Frame Counter"
                visible: root.session.isOpen && root.session.showFrameCounter
                text: root.session.frameCounterText
                // a lag frame's number is the alert's colour
                color: root.session.lagFrame ? root.osdAlertColor : root.osdMessageColor
            }
            OsdText {
                objectName: "osdInput"
                place: "Input Display"
                visible: root.session.isOpen && root.session.showInput
                text: root.session.inputText
                color: root.osdMovieInputColor
            }
            OsdText {
                objectName: "osdLagCounter"
                place: "Lag Counter"
                visible: root.session.isOpen && root.session.showLagCounter
                text: root.session.lagCount
                color: root.osdAlertColor
            }
            // while autohold or autofire is armed, what they hold, at the top right as Chimera has it
            OsdText {
                objectName: "osdSticky"
                place: "Autohold"
                visible: root.session.isOpen && root.session.stickyArmed
                text: root.session.stickyText
                color: root.osdColor("OsdAutoHold", "#FFFFFF")
            }
            // the project's subtitles on this frame, where and in the colour each says
            Repeater {
                model: root.session.isOpen ? JSON.parse(root.projectDetails.onScreen) : []
                delegate: OsdText {
                    required property var modelData
                    x: modelData.x
                    y: modelData.y
                    text: modelData.text
                    color: modelData.color
                }
            }
            // RAM Watch's on-screen watches, a line every 14, where Config >
            // Messages puts them (Chimera's place: the top left, 70 down)
            Column {
                objectName: "onScreenWatches"
                x: root.osdX("Watches", width)
                y: root.osdY("Watches", height)
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
                        const question = root.pianoRoll.run("autoHoldQuestion", [columnMenu.name])
                        if (question) {
                            holdValueDialog.name = columnMenu.name
                            holdValueDialog.question = question
                            holdValueBox.from = root.pianoRoll.autoHoldMin
                            holdValueBox.to = root.pianoRoll.autoHoldMax
                            holdValueBox.value = root.pianoRoll.autoHoldInitial
                            holdValueDialog.open()
                        } else root.pianoRoll.run("toggleAutoHold", [columnMenu.name])
                    }
                }
                MenuItem { text: qsTr("Hide Column"); onTriggered: root.pianoRoll.run("hideColumn", [columnMenu.name]) }
                MenuItem { text: qsTr("Show All Columns"); onTriggered: root.pianoRoll.run("showAllColumns") }
            }
            PianoRollItem {
                id: roll
                objectName: "pianoRoll"
                anchors { left: parent.left; top: parent.top; bottom: axisBar.top; right: scroll.left }
                rows: root.pianoRoll.rows("frames")
                currentRow: root.pianoRoll.currentFrame
                topRow: root.pianoRoll.topFrame
                onVisibleRowsChanged: root.pianoRoll.visibleFrames = visibleRows
                onTopRowRequested: (row) => root.pianoRoll.topFrame = row
                wheelRows: root.pianoRoll.wheelRows
                denoteStatesColor: root.pianoRoll.denoteStatesColor
                denoteStatesIcons: root.pianoRoll.denoteStatesIcons
                denoteMarkersColor: root.pianoRoll.denoteMarkersColor
                denoteMarkersIcons: root.pianoRoll.denoteMarkersIcons
                colors: root.pianoRoll.rollColors
                rollFont: root.pianoRoll.rollFont
                onCellPressed: (row, column, alt) => root.pianoRoll.run("press", [row, column, alt])
                onColumnMenuRequested: (column, name, autoHeld, x, y) => { columnMenu.name = name; columnMenu.autoHeld = autoHeld; columnMenu.popup() }
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

    // every message the window shows, for the log window; and the ones over the picture now
    ListModel { id: messageLog }
    ListModel { id: onScreenMessages }
    // the picture N times the machine's own, the window grown or shrunk to it
    property int windowScale: 1
    function sizeWindow(n) {
        root.windowScale = n
        const w = picture.pictureWidth * n, h = picture.pictureHeight * n
        const others = root.width - pictureArea.width
        root.height = Math.round(root.height - pictureArea.height + h)
        root.width = Math.round(others + w)
        // the pane its size, not its share of the window
        pictureArea.SplitView.preferredWidth = w
    }
    function note(text) {
        if (text === "") return
        messageLog.append({ "time": Qt.formatTime(new Date(), "hh:mm:ss"), "message": text })
        if (messageLog.count > 5000) messageLog.remove(0)
        if (!root.session.showMessages) return
        onScreenMessages.append({ "message": text, "until": Date.now() + root.messages.messageSeconds * 1000 })
        if (onScreenMessages.count > 5) onScreenMessages.remove(0)
    }
    Connections { target: root.session; function onStatusChanged() { root.note(root.session.status) } }
    Connections { target: root.hotkeys; function onStatusChanged() { root.note(root.hotkeys.status) } }
    Connections { target: root.branches; function onStatusChanged() { root.note(root.branches.status) } }
    Connections { target: root.projectDetails; function onStatusChanged() { root.note(root.projectDetails.status) } }
    Timer {
        interval: 250
        repeat: true
        running: onScreenMessages.count > 0
        onTriggered: { while (onScreenMessages.count > 0 && onScreenMessages.get(0).until < Date.now()) onScreenMessages.remove(0) }
    }
    LogWindow { id: logWindow; log: messageLog; clipboard: root.clipboard; visible: false }
    BatchRun { id: batchRunWindow; vm: root.batchRun; visible: false }
    MediaMaker { id: mediaMakerWindow; vm: root.mediaMaker; visible: false }
    CacheManager { id: cacheManagerWindow; vm: root.cacheManager; visible: false; onBudgetsAsked: { greenzoneBudgetsWindow.show(); greenzoneBudgetsWindow.raise() } }
    GreenzoneBudgets { id: greenzoneBudgetsWindow; session: root.session; visible: false }
    Customize { id: customizeWindow; vm: root.customize; session: root.session; hotkeys: root.hotkeys; visible: false }
    MessagesConfig { id: messagesWindow; vm: root.messages; visible: false }
    SoundConfig { id: soundWindow; session: root.session; visible: false }
    DisplayConfig { id: displayWindow; vm: root.display; session: root.session; visible: false }
    ProjectFiles { session: root.session }
    // Work a crash left behind, offered before the project opens, as
    // Chimera's "Recover unsaved work?" asks
    Dialog {
        id: recoveryDialog
        objectName: "recoveryDialog"
        title: qsTr("Recover unsaved work?")
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 560)
        modal: true
        closePolicy: Popup.NoAutoClose
        standardButtons: Dialog.Yes | Dialog.No
        Label {
            width: recoveryDialog.availableWidth
            wrapMode: Text.Wrap
            text: qsTr("Chimera did not close normally the last time this project was open.\n\n"
                + "The work in progress up to %1 has been kept as:\n%2\n\n"
                + "Open that work now? It opens unsaved, and saving writes it to this project.\n\n"
                + "No opens the project as it was last saved; the recovered copy stays where it is.")
                .arg(root.session.recoveredUpTo).arg(root.session.recoveredCopy)
        }
        onAccepted: root.session.run("recoverWork", [true])
        onRejected: root.session.run("recoverWork", [false])
    }
    Connections {
        target: root.session
        function onRecoveredCopyChanged() { if (root.session.recoveredCopy !== "") recoveryDialog.open() }
    }
    ReportIssue { id: reportIssueWindow; vm: root.help; clipboard: root.clipboard; pictureFiles: root.pictureFiles; folderOf: root.folderOf; visible: false }
    PathsConfig { id: pathsWindow; vm: root.paths; visible: false }
    FirmwareConfig { id: firmwareWindow; vm: root.firmware; folderOf: root.folderOf; visible: false }
    DataFolderConfig { id: dataFolderWindow; vm: root.dataFolder; visible: false }
    PrecompiledModules { id: precompiledWindow; vm: root.precompiled; visible: false }
    Dialog {
        id: aboutDialog
        objectName: "aboutDialog"
        title: qsTr("About Chimera")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok
        ColumnLayout {
            Label { text: qsTr("Chimera"); font.bold: true; font.pointSize: 14 }
            Label { text: qsTr("A frontend for tool-assisted speedruns: the Qt window, a prototype.") }
            Label { text: root.help.buildName }
            Label {
                text: "<a href=\"" + root.help.siteUrl + "\">" + root.help.siteUrl + "</a>"
                onLinkActivated: (link) => Qt.openUrlExternally(link)
            }
        }
    }

    footer: ToolBar {
        visible: root.session.showStatusBar
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            Label { text: root.session.status; elide: Text.ElideRight; Layout.fillWidth: true }
            Label { text: root.session.gamepads; elide: Text.ElideRight; Layout.maximumWidth: 260 }
            Label { text: root.session.sound; elide: Text.ElideRight; Layout.maximumWidth: 200 }
            // Chimera's key priority in the status bar: a click is the next one
            Label {
                text: [qsTr("Keys: both"), qsTr("Keys: buttons first"), qsTr("Keys: hotkeys first")][root.hotkeys.keyPriority] || ""
                ToolTip.visible: keyPriorityArea.containsMouse
                ToolTip.text: [qsTr("Key priority: Allow both hotkeys and controller buttons"),
                               qsTr("Key priority: Controller buttons will override hotkeys"),
                               qsTr("Key priority: Hotkeys will override controller buttons")][root.hotkeys.keyPriority] || ""
                MouseArea { id: keyPriorityArea; anchors.fill: parent; hoverEnabled: true; onClicked: root.hotkeys.run("cycleKeyPriority") }
            }
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
