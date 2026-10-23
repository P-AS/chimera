// The main window: layout and bindings only. Every decision is the view
// models' (session, pianoRoll); this file says where things go and which
// command a gesture is.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Chimera
import "Shapes.js" as Shapes

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
    required property QtObject luaConsole
    required property QtObject toolWindows
    required property QtObject gameProperties
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
    required property QtObject traceLogger
    // "debugger" is a word of JavaScript's own
    required property QtObject debuggerTool
    required property QtObject surfaceViewer
    required property QtObject guiOverlay
    required property QtObject idleEye
    required property QtObject macroTool
    required property QtObject encoder
    required property QtObject hostInput
    required property QtObject forms
    // a Paths folder for a file dialog to open in (made when it is not there)
    function folderOf(name) {
        root.paths.changed; root.paths.base; root.paths.lastRomFolder; root.paths.recentForRoms
        return root.pictureFiles.folderUrl(root.paths.run("dialogFolder", [name]) || "")
    }

    // the picture alone: TAStudio is a window of its own beside it
    width: 800
    height: 680
    visible: true
    // Chimera's title: the speed while nothing is drawn, the game and its system, the project's file
    // (marked when it has changes), then "Chimera" or the words Display's Misc gives; only those
    // last words when titles are kept static
    readonly property string titleSuffix: root.display.titleSuffix.trim() !== "" ? root.display.titleSuffix : qsTr("Chimera")
    title: root.display.staticWindowTitles ? root.titleSuffix
         : (session.displayNothing ? qsTr("(%1 fps) - ").arg(Math.round(session.fps)) : "")
         + (session.isOpen ? qsTr("%1 [%2] - ").arg(session.gameName).arg(session.systemName) : "")
         + (session.isOpen && session.hasProject
            ? (session.unsaved ? "*" : "") + session.projectPath.replace(/^.*[\\/]/, "") + " - " : "")
         + root.titleSuffix
    // Display's window chrome, as Chimera's SynchChrome: windowed, the caption's buttons, the frame and
    // the menu; full screen, the menu; kept over other windows when asked
    flags: Qt.Window
         | (root.visibility !== Window.FullScreen && root.display.frameWindowed === 0 ? Qt.FramelessWindowHint
            : Qt.WindowTitleHint | Qt.WindowSystemMenuHint | Qt.WindowCloseButtonHint
              | (root.display.captionWindowed ? Qt.WindowMinMaxButtonsHint : 0))
         | (root.display.stayOnTop ? Qt.WindowStaysOnTopHint : 0)
    function synchChrome() { root.menuBar.visible = root.visibility === Window.FullScreen ? root.display.menuFullscreen : root.display.menuWindowed }
    onVisibilityChanged: root.synchChrome()
    Connections {
        target: root.display
        function onMenuWindowedChanged() { root.synchChrome() }
        function onMenuFullscreenChanged() { root.synchChrome() }
    }
    Component.onCompleted: root.synchChrome()

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
    // what a hotkey does when it is the window's: a menu's action, a dialog, a
    // window; TAStudio's editing is TAStudio's
    readonly property var hotkeyActions: ({
        "RAM Search": ramSearchAction, "RAM Watch": ramWatchAction, "Hex Editor": hexEditorAction, "Lua Console": luaConsoleAction,
        "Close ROM": closeAction, "Trace Logger": traceLoggerAction
    })
    Connections {
        target: root.hotkeyRelay
        function onPressed(action) {
            if (action === "Full Screen") root.visibility = root.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen
            else if (action === "Exit Program") root.quitProgram()
            else if (action === "Load Last ROM") {
                const last = root.session.recentProjects.split("\n")[0]
                if (last !== "") root.leaveProject(() => root.session.run("openProject", [last]))
            }
            else if (action === "Seek To...") { if (root.session.seekEnabled) seekDialog.open() }
            else if (action === "Toggle Menu") root.menuBar.visible = !root.menuBar.visible
            else if (action === "TAStudio") { if (root.session.hasProject) tastudioWindow.engage() }
            else if (action === "Toggle Stay on Top") {
                root.display.stayOnTop = !root.display.stayOnTop
                root.note(root.display.stayOnTop ? qsTr("Stay on Top enabled") : qsTr("Stay on Top disabled"))
            }
            else if (action === "Play Movie") root.leaveProject(() => projectDialog.open())
            else if (action === "Record Movie") { root.newProject.run("openOnLastProject"); newProjectWindow.show(); newProjectWindow.raise() }
            else if (action === "Stop Movie") { if (root.session.hasProject) root.leaveProject(() => root.session.run("stopMovie")) }
            else if (action === "Play from beginning") {
                // as TAStudio's: the project as its file has it, from frame 0
                if (root.session.projectPath !== "") root.leaveProject(() => root.session.run("openProject", [root.session.projectPath]))
            }
            else if (action === "Save Movie") {
                if (root.session.hasProject) root.session.projectPath === "" ? saveAsDialog.open() : root.session.run("saveProject")
            }
            else if (action === "Encode Video") root.encodeVideo()
            else if (action === "Larger Window" || action === "Smaller Window") {
                if (picture.pictureWidth <= 0) return
                const n = Math.max(1, Math.min(10, root.windowScale + (action === "Larger Window" ? 1 : -1)))
                root.sizeWindow(n)
                root.note(qsTr("Screensize set to %1x").arg(n))
            }
            else if (root.hotkeyActions[action] && root.hotkeyActions[action].enabled) root.hotkeyActions[action].trigger()
            else if (tastudioWindow.hotkeyActions[action] && tastudioWindow.hotkeyActions[action].enabled) tastudioWindow.hotkeyActions[action].trigger()
        }
    }

    menuBar: RowLayout {
        spacing: 0
        // Chimera's menus, in its order: File, System, View, Config, Tools,
        // Emulator (Game for a game core), Help. System and Emulator are there
        // only while a machine runs, as Chimera's are
        MenuBar {
            Layout.fillWidth: true
            delegate: MenuBarItem {
                id: menuBarItem
                visible: menuBarItem.menu !== null && menuBarItem.menu.shown !== false
                width: visible ? implicitWidth : 0
            }
            Menu {
                // Customize's "pause when a menu is open"
                onAboutToShow: root.customize.run("menuOpened")
                onAboutToHide: root.customize.run("menuClosed")
                delegate: keyedMenuItem
                title: qsTr("&File")
                // opens on the last project's answers, as Chimera's does
                Action { text: qsTr("&New Chimera Project..."); onTriggered: { root.newProject.run("openOnLastProject"); newProjectWindow.show(); newProjectWindow.raise() } }
                Action { text: qsTr("&Open Project..."); onTriggered: root.leaveProject(() => projectDialog.open()) }
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
                Action { text: qsTr("Save Project &As..."); shortcut: "Ctrl+Shift+S"; enabled: root.session.hasProject; onTriggered: saveAsDialog.open() }
                Action { text: qsTr("Save Project &Backup"); enabled: root.session.projectPath !== ""; onTriggered: root.session.run("saveBackup") }
                Action { id: closeAction; property string hotkey: "Close ROM"; text: qsTr("&Close Project"); enabled: root.session.closeEnabled; onTriggered: root.leaveProject(() => root.session.run("close")) }
                MenuSeparator {}
                Action {
                    property string hotkey: "Encode Video"
                    text: qsTr("&Encode Video...")
                    onTriggered: root.encodeVideo()
                }
                Menu {
                    delegate: keyedMenuItem
                    title: qsTr("Scree&nshot")
                    enabled: root.session.isOpen
                    Action { property string hotkey: "Screenshot"; text: qsTr("Screenshot"); onTriggered: root.hotkeys.run("run", ["Screenshot"]) }
                    Action {
                        text: qsTr("Screenshot As...")
                        onTriggered: {
                            const suggested = root.pictureFiles.screenshotSuggestion(root.session.gameName)
                            screenshotAsDialog.currentFolder = suggested.toString().substring(0, suggested.toString().lastIndexOf("/"))
                            screenshotAsDialog.selectedFile = suggested
                            screenshotAsDialog.open()
                        }
                    }
                    Action { property string hotkey: "Screen Raw to Clipboard"; text: qsTr("Screenshot (raw) -> Clipboard"); onTriggered: root.hotkeys.run("run", ["Screen Raw to Clipboard"]) }
                    Action { property string hotkey: "Screen Client to Clipboard"; text: qsTr("Screenshot (client) -> Clipboard"); onTriggered: root.hotkeys.run("run", ["Screen Client to Clipboard"]) }
                    MenuSeparator {}
                    // the counters and messages over the picture in the screenshot too
                    Action { text: qsTr("Capture OSD"); checkable: true; checked: root.session.captureOsd; onTriggered: root.session.captureOsd = checked }
                }
                Action { property string hotkey: "Exit Program"; text: qsTr("E&xit"); shortcut: "Alt+F4"; onTriggered: root.quitProgram() }
            }
            Menu {
                readonly property bool shown: root.session.isOpen
                onAboutToShow: root.customize.run("menuOpened")
                onAboutToHide: root.customize.run("menuClosed")
                delegate: keyedMenuItem
                title: qsTr("&System")
                Action {
                    property string hotkey: "Pause"
                    text: qsTr("&Pause"); checkable: true; checked: !root.session.running
                    onTriggered: root.session.run(root.session.running ? "pause" : "play")
                }
                MenuSeparator {}
                Action { property string hotkey: "Reboot Core"; text: qsTr("&Reboot Core"); enabled: root.session.restartEnabled; onTriggered: root.session.run("restart") }
                MenuSeparator {}
                Action { text: qsTr("Loaded core: %1 (%2)").arg(root.session.coreName).arg(root.session.systemId); enabled: false }
            }
            Menu {
                onAboutToShow: root.customize.run("menuOpened")
                onAboutToHide: root.customize.run("menuClosed")
                delegate: keyedMenuItem
                title: qsTr("&View")
                // the picture N times the machine's own, the window grown or shrunk to it
                Menu {
                    id: windowSizeMenu
                    title: qsTr("&Window Size")
                    enabled: picture.pictureWidth > 0
                    Instantiator {
                        model: 10
                        delegate: MenuItem {
                            required property int index
                            text: (index + 1 >= 10 ? "1" : "") + "&" + ((index + 1) % 10) + "x"
                            checkable: true
                            checked: root.windowScale === index + 1
                            onTriggered: root.sizeWindow(index + 1)
                        }
                        onObjectAdded: (index, object) => windowSizeMenu.insertItem(index, object)
                        onObjectRemoved: (index, object) => windowSizeMenu.removeItem(object)
                    }
                }
                Action { property string hotkey: "Full Screen"; text: qsTr("Switch to Fullscreen"); onTriggered: root.visibility = root.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen }
                Action { property string hotkey: "Display FPS"; text: qsTr("Display FPS"); checkable: true; checked: root.session.showFps; onTriggered: root.session.showFps = checked }
                Action { text: qsTr("Display Average FPS"); checkable: true; checked: root.session.showAverageFps; onTriggered: root.session.showAverageFps = checked }
                Action { text: qsTr("Reset Average FPS"); enabled: root.session.showAverageFps; onTriggered: root.session.run("resetAverageFps") }
                Action { property string hotkey: "Frame Counter"; text: qsTr("Display Frame Count"); checkable: true; checked: root.session.showFrameCounter; onTriggered: root.session.showFrameCounter = checked }
                Action { property string hotkey: "Lag Counter"; text: qsTr("Display Lag Frame Count"); checkable: true; checked: root.session.showLagCounter; onTriggered: root.session.showLagCounter = checked }
                Action { property string hotkey: "Input Display"; text: qsTr("Display Input"); checkable: true; checked: root.session.showInput; onTriggered: root.session.showInput = checked }
                Action { text: qsTr("Display Rerecord Count"); checkable: true; checked: root.session.showRerecords; onTriggered: root.session.showRerecords = checked }
                Action { text: qsTr("Display Subtitles"); checkable: true; checked: root.projectDetails.showSubtitles; onTriggered: root.projectDetails.showSubtitles = checked }
                MenuSeparator {}
                Action { text: qsTr("Display Status Bar"); checkable: true; checked: root.session.showStatusBar; onTriggered: root.session.showStatusBar = checked }
                Action { text: qsTr("Display Messages"); checkable: true; checked: root.session.showMessages; onTriggered: root.session.showMessages = checked }
                Action { text: qsTr("Open &Log Window..."); onTriggered: { logWindow.show(); logWindow.raise() } }
            }
            Menu {
                onAboutToShow: root.customize.run("menuOpened")
                onAboutToHide: root.customize.run("menuClosed")
                delegate: keyedMenuItem
                title: qsTr("&Config")
                Action { text: qsTr("&Controllers..."); onTriggered: { controllerWindow.show(); controllerWindow.raise() } }
                Action { text: qsTr("&Hotkeys..."); onTriggered: { hotkeyWindow.show(); hotkeyWindow.raise() } }
                Action { text: qsTr("Display..."); onTriggered: { displayWindow.show(); displayWindow.raise() } }
                Action { text: qsTr("&Sound..."); onTriggered: { soundWindow.show(); soundWindow.raise() } }
                Action { text: qsTr("Paths..."); onTriggered: { pathsWindow.show(); pathsWindow.raise() } }
                Action { text: qsTr("Firmware..."); onTriggered: { firmwareWindow.show(); firmwareWindow.raise() } }
                Action { text: qsTr("Data Directory..."); onTriggered: { dataFolderWindow.show(); dataFolderWindow.raise() } }
                Action { text: qsTr("&Messages..."); onTriggered: { messagesWindow.show(); messagesWindow.raise() } }
                Action { text: qsTr("&Theme..."); onTriggered: { themesWindow.show(); themesWindow.raise() } }
                Action { text: qsTr("&Autofire..."); onTriggered: autofireDialog.open() }
                Action { text: qsTr("&Customize..."); onTriggered: { customizeWindow.show(); customizeWindow.raise() } }
                Menu {
                    delegate: keyedMenuItem
                    title: qsTr("Speed/Skip")
                    // one of the three, or none: each ticked turns the others off, as Chimera's
                    Action {
                        text: qsTr("Clock Throttle"); checkable: true; checked: root.session.throttle === "clock"
                        onTriggered: root.session.throttle = root.session.throttle === "clock" ? "" : "clock"
                    }
                    Action {
                        text: qsTr("Audio Throttle"); checkable: true; checked: root.session.throttle === "audio"; enabled: root.session.soundOn
                        onTriggered: root.session.throttle = root.session.throttle === "audio" ? "" : "audio"
                    }
                    Action {
                        text: qsTr("VSync Throttle"); checkable: true; checked: root.session.throttle === "vsync"
                        onTriggered: {
                            root.session.throttle = root.session.throttle === "vsync" ? "" : "vsync"
                            // the throttle needs the window at the screen's refresh
                            if (root.session.throttle === "vsync" && !root.session.vsyncEnabled) {
                                root.session.vsyncEnabled = true
                                root.note(qsTr("Display Vsync is set to on (from the next start)"))
                            }
                        }
                    }
                    MenuSeparator {}
                    Action {
                        text: qsTr("VSync Enabled"); checkable: true; checked: root.session.vsyncEnabled
                        onTriggered: {
                            root.session.vsyncEnabled = checked
                            root.note(qsTr("Display Vsync is set to %1 (from the next start)").arg(checked ? qsTr("on") : qsTr("off")))
                        }
                    }
                    MenuSeparator {}
                    Action {
                        property string hotkey: "Toggle Throttle"
                        text: qsTr("Unthrottled"); checkable: true; checked: root.session.unthrottled
                        onTriggered: root.session.unthrottled = checked
                    }
                    Action { text: qsTr("Auto-minimize skipping"); checkable: true; checked: root.session.autoMinimizeSkipping; onTriggered: root.session.autoMinimizeSkipping = checked }
                    Action { text: qsTr("Skip 0 (never)"); checkable: true; checked: root.session.frameSkip === 0; onTriggered: root.session.run("setFrameSkip", [0]) }
                    Menu {
                        id: frameSkipMenu
                        title: qsTr("Skip 1..9")
                        Instantiator {
                            model: 9
                            delegate: MenuItem {
                                required property int index
                                text: String(index + 1)
                                checkable: true
                                checked: root.session.frameSkip === index + 1
                                onTriggered: root.session.run("setFrameSkip", [index + 1])
                            }
                            onObjectAdded: (index, object) => frameSkipMenu.insertItem(index, object)
                            onObjectRemoved: (index, object) => frameSkipMenu.removeItem(object)
                        }
                    }
                    MenuSeparator {}
                    Action { text: qsTr("Speed 50%"); enabled: root.session.throttle === "clock"; checkable: true; checked: root.session.speedPercent === 50; onTriggered: root.session.speedPercent = 50 }
                    Action { text: qsTr("Speed 75%"); enabled: root.session.throttle === "clock"; checkable: true; checked: root.session.speedPercent === 75; onTriggered: root.session.speedPercent = 75 }
                    Action { text: qsTr("Speed 100%"); enabled: root.session.throttle === "clock"; checkable: true; checked: root.session.speedPercent === 100; onTriggered: root.session.speedPercent = 100 }
                    Action { text: qsTr("Speed 150%"); enabled: root.session.throttle === "clock"; checkable: true; checked: root.session.speedPercent === 150; onTriggered: root.session.speedPercent = 150 }
                    Action { text: qsTr("Speed 200%"); enabled: root.session.throttle === "clock"; checkable: true; checked: root.session.speedPercent === 200; onTriggered: root.session.speedPercent = 200 }
                    Action { text: qsTr("Speed 400%"); enabled: root.session.throttle === "clock"; checkable: true; checked: root.session.speedPercent === 400; onTriggered: root.session.speedPercent = 400 }
                }
                // for a key that is a hotkey and a button
                Menu {
                    delegate: keyedMenuItem
                    title: qsTr("Key Priority")
                    Action { text: qsTr("Both Hotkeys and Controllers"); checkable: true; checked: root.hotkeys.keyPriority === 0; onTriggered: root.hotkeys.keyPriority = 0 }
                    Action { text: qsTr("Input overrides Hotkeys"); checkable: true; checked: root.hotkeys.keyPriority === 1; onTriggered: root.hotkeys.keyPriority = 1 }
                    Action { text: qsTr("Hotkeys override Input"); checkable: true; checked: root.hotkeys.keyPriority === 2; onTriggered: root.hotkeys.keyPriority = 2 }
                }
                MenuSeparator {}
                Action { text: qsTr("Save Config"); onTriggered: root.configFiles.run("save") }
                Action { text: qsTr("Save Config As..."); onTriggered: saveConfigDialog.open() }
                Action { text: qsTr("Load Config"); onTriggered: root.configFiles.run("load") }
                Action { text: qsTr("Load Config From..."); onTriggered: loadConfigDialog.open() }
            }
            Menu {
                onAboutToShow: root.customize.run("menuOpened")
                onAboutToHide: root.customize.run("menuClosed")
                delegate: keyedMenuItem
                title: qsTr("&Tools")
                Action { text: qsTr("&Core Manager..."); onTriggered: { coresWindow.show(); coresWindow.raise() } }
                Action { text: qsTr("Cac&he Manager..."); onTriggered: { cacheManagerWindow.show(); cacheManagerWindow.raise() } }
                Action { text: qsTr("Reproducible Media Maker..."); onTriggered: { mediaMakerWindow.show(); mediaMakerWindow.raise() } }
                Action { text: qsTr("Pre-Compiled Modules..."); onTriggered: { precompiledWindow.show(); precompiledWindow.raise() } }
                Action { id: ramWatchAction; property string hotkey: "RAM Watch"; text: qsTr("RAM &Watch"); onTriggered: { ramWatchWindow.show(); ramWatchWindow.raise() } }
                Action { id: ramSearchAction; property string hotkey: "RAM Search"; text: qsTr("RAM &Search"); onTriggered: { ramSearchWindow.show(); ramSearchWindow.raise() } }
                Action { id: luaConsoleAction; property string hotkey: "Lua Console"; text: qsTr("Lua Console"); onTriggered: { luaConsoleWindow.show(); luaConsoleWindow.raise() } }
                Action { id: hexEditorAction; property string hotkey: "Hex Editor"; text: qsTr("&Hex Editor"); onTriggered: { hexEditorWindow.show(); hexEditorWindow.raise() } }
                Action { text: qsTr("&Macro Tool"); enabled: root.session.isOpen; onTriggered: { macroToolWindow.show(); macroToolWindow.raise() } }
                Action { text: qsTr("Batch Runner..."); onTriggered: { batchRunWindow.show(); batchRunWindow.raise() } }
                // ticked while the core log is kept; off until asked, every run
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
            // Chimera's Emulator menu, Game for a game core: only what the core
            // open offers, and gone when it offers nothing
            Menu {
                readonly property bool shown: root.session.isOpen && (root.session.hasGameTimer || root.session.hasSaveData
                    || root.session.hasMovieImport || root.session.hasTrace || root.session.hasRegisters || root.session.surfaces !== "[]")
                onAboutToShow: root.customize.run("menuOpened")
                onAboutToHide: root.customize.run("menuClosed")
                delegate: keyedMenuItem
                title: root.session.isGameCore ? qsTr("&Game") : qsTr("E&mulator")
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
                    // Chimera's: the core's import form first, then the wizard on its answers
                    onTriggered: { root.newProject.run("chooseCore", [root.session.coreName]); movieImportWindow.show(); movieImportWindow.raise() }
                }
                MenuSeparator {}
                // the tools the core offers, by Chimera's names, in its order
                Action { text: qsTr("&Debugger"); enabled: root.session.hasRegisters; onTriggered: { debuggerWindow.show(); debuggerWindow.raise() } }
                Action { text: qsTr("&Surface Viewer"); enabled: root.session.surfaces !== "[]"; onTriggered: { surfaceViewerWindow.show(); surfaceViewerWindow.raise() } }
                Action { id: traceLoggerAction; property string hotkey: "Trace Logger"; text: qsTr("&Trace Logger"); enabled: root.session.hasTrace; onTriggered: { traceLoggerWindow.show(); traceLoggerWindow.raise() } }
            }
            Menu {
                onAboutToShow: root.customize.run("menuOpened")
                onAboutToHide: root.customize.run("menuClosed")
                delegate: keyedMenuItem
                title: qsTr("&Help")
                Action { text: qsTr("Open toolAssisted.run in Browser"); onTriggered: Qt.openUrlExternally(root.help.siteUrl) }
                Action {
                    text: qsTr("&Copy Version Info")
                    onTriggered: { root.clipboard.setText(root.help.run("versionReport")); root.note(qsTr("Version info copied")) }
                }
                Action { text: qsTr("&Report an Issue..."); onTriggered: { reportIssueWindow.show(); reportIssueWindow.raise() } }
                Action { text: qsTr("&About..."); onTriggered: aboutDialog.open() }
            }
        }
        // at the right of the menus while TAStudio is hidden, as Chimera's
        ToolButton {
            objectName: "showTAStudio"
            text: qsTr("Show TAStudio >>")
            flat: true
            visible: tastudioWindow.hiddenByPerson && root.session.hasProject
            onClicked: tastudioWindow.engage()
        }
    }

    // Chimera's context menu on the picture (MainFormContextMenu): the project's items while one is
    // open, Config while full screen, the screenshot and closing while a game runs, and the menu
    // bar back while it is hidden. Its Restart, Stop and Save items do here what their hotkeys do.
    readonly property bool fullScreen: root.visibility === Window.FullScreen
    property bool cursorHidden: false
    property point cursorAt: Qt.point(-100, -100)
    Shortcut {
        // the keyboard's menu key opens it too
        sequence: "Menu"
        enabled: root.customize.showContextMenu
        onActivated: pictureMenu.popup(pictureArea, 0, 0)
    }
    Menu {
        id: pictureMenu
        objectName: "pictureMenu"
        // Config, after the project's items, only while full screen
        property bool configIn: false
        onAboutToShow: {
            root.customize.run("menuOpened")
            if (root.fullScreen !== pictureMenu.configIn) {
                if (root.fullScreen) pictureMenu.insertMenu(10, pictureConfigMenu)
                else pictureMenu.removeMenu(pictureConfigMenu)
                pictureMenu.configIn = root.fullScreen
            }
        }
        onAboutToHide: root.customize.run("menuClosed")
        component Shown: MenuItem { height: visible ? implicitHeight : 0 }
        component ShownSeparator: MenuSeparator { height: visible ? implicitHeight : 0 }
        Shown {
            text: qsTr("Restart Movie"); icon.source: "qrc:/chimera/icons/restart.png"; visible: root.session.hasProject
            onTriggered: root.hotkeys.run("run", ["Play from beginning"])
        }
        Shown {
            text: qsTr("Stop Movie"); icon.source: "qrc:/chimera/icons/Stop.png"; visible: root.session.hasProject
            onTriggered: root.hotkeys.run("run", ["Stop Movie"])
        }
        Shown {
            text: qsTr("Backup Movie"); visible: root.session.hasProject
            onTriggered: { root.session.run("saveBackup"); root.note(qsTr("Backup movie saved.")) }
        }
        Shown {
            text: qsTr("Stop Movie without Saving"); icon.source: "qrc:/chimera/icons/Stop.png"
            visible: root.session.hasProject && root.session.unsaved
            onTriggered: root.session.run("stopMovie")
        }
        // the prototype has no read-only mode: the project is always edited, so Edit, as Chimera's says then
        Shown { text: qsTr("Edit Subtitles"); visible: root.session.hasProject; onTriggered: tastudioWindow.showDetails("subtitles") }
        Shown { text: qsTr("Add Subtitle"); visible: root.session.isOpen && root.session.hasProject; onTriggered: subtitleMaker.openOn(root.session.frame) }
        Shown { text: qsTr("Edit Comments"); visible: root.session.hasProject; onTriggered: tastudioWindow.showDetails("comments") }
        Shown {
            text: qsTr("Save Movie"); icon.source: "qrc:/chimera/icons/SaveAs.png"; visible: root.session.hasProject
            onTriggered: root.hotkeys.run("run", ["Save Movie"])
        }
        Shown {
            text: qsTr("Save Movie As..."); icon.source: "qrc:/chimera/icons/SaveAs.png"; visible: root.session.hasProject
            onTriggered: saveAsDialog.open()
        }
        ShownSeparator { visible: root.session.isOpen }
        Shown { text: qsTr("Screenshot"); visible: root.session.isOpen; onTriggered: root.hotkeys.run("run", ["Screenshot"]) }
        Shown { text: qsTr("Close ROM"); visible: root.session.isOpen; onTriggered: closeAction.trigger() }
        ShownSeparator { visible: root.fullScreen || !root.menuBar.visible }
        Shown {
            text: root.menuBar.visible ? qsTr("Hide Menu") : qsTr("Show Menu")
            visible: root.fullScreen || !root.menuBar.visible
            onTriggered: root.menuBar.visible = !root.menuBar.visible
        }
    }
    Menu {
        id: pictureConfigMenu
        title: qsTr("Config")
        // Chimera shows it only full screen, where the menu bar is gone
        Action { text: qsTr("&Controllers..."); onTriggered: { controllerWindow.show(); controllerWindow.raise() } }
        Action { text: qsTr("&Hotkeys..."); onTriggered: { hotkeyWindow.show(); hotkeyWindow.raise() } }
        Action { text: qsTr("Display..."); onTriggered: { displayWindow.show(); displayWindow.raise() } }
        Action { text: qsTr("&Sound..."); onTriggered: { soundWindow.show(); soundWindow.raise() } }
        Action { text: qsTr("Paths..."); onTriggered: { pathsWindow.show(); pathsWindow.raise() } }
        Action { text: qsTr("&Messages..."); onTriggered: { messagesWindow.show(); messagesWindow.raise() } }
        Action { text: qsTr("&Autofire..."); onTriggered: autofireDialog.open() }
        Action { text: qsTr("Customize..."); onTriggered: { customizeWindow.show(); customizeWindow.raise() } }
        MenuSeparator {}
        Action { text: qsTr("Save Config"); onTriggered: root.configFiles.run("save") }
        Action { text: qsTr("Load Config"); onTriggered: root.configFiles.run("load") }
    }

    // Chimera's Subtitle Maker, for Add Subtitle: the frame the machine is on, or the subtitle already on it
    Dialog {
        id: subtitleMaker
        title: qsTr("Subtitle Maker")
        anchors.centerIn: parent
        modal: true
        width: 435
        property int frame: 0
        property int index: -1
        property string color: "#FFFFFFFF"
        function openOn(f) {
            subtitleMaker.frame = f
            const found = root.projectDetails.run("subtitleOn", [f])
            const sub = found ? JSON.parse(found) : { index: -1, x: 0, y: 0, duration: 120, color: "#FFFFFFFF", message: "" }
            subtitleMaker.index = sub.index
            subX.value = sub.x
            subY.value = sub.y
            subDuration.value = sub.duration
            subtitleMaker.color = sub.color
            subMessage.text = sub.message
            open()
            subMessage.forceActiveFocus()
        }
        onAccepted: {
            // as Chimera's: the one on this frame is replaced
            if (subtitleMaker.index >= 0) root.projectDetails.run("removeSubtitle", [subtitleMaker.index])
            root.projectDetails.run("addSubtitle", [subtitleMaker.frame, subX.value, subY.value, subDuration.value, subtitleMaker.color, subMessage.text])
        }
        ColorDialog {
            id: subColorDialog
            selectedColor: "#" + subtitleMaker.color.substring(3)
            onAccepted: subtitleMaker.color = "#FF" + selectedColor.toString().substring(1).toUpperCase()
        }
        GridLayout {
            anchors.fill: parent
            columns: 4
            Label { text: qsTr("Frame") }
            SpinBox { from: 0; to: 2147483647; value: subtitleMaker.frame; enabled: false; Layout.columnSpan: 3; Layout.preferredWidth: 120 }
            Label { text: qsTr("Message"); Layout.columnSpan: 4 }
            TextField { id: subMessage; Layout.columnSpan: 4; Layout.fillWidth: true }
            SpinBox { id: subX; from: 0; to: 320; editable: true; Layout.preferredWidth: 90 }
            Label { text: qsTr("X position") }
            SpinBox { id: subDuration; from: 0; to: 9999; editable: true; Layout.preferredWidth: 90 }
            Label { text: qsTr("Duration") }
            SpinBox { id: subY; from: 0; to: 240; editable: true; Layout.preferredWidth: 90 }
            Label { text: qsTr("Y position") }
            Rectangle {
                Layout.preferredWidth: 56
                Layout.preferredHeight: 19
                color: "#" + subtitleMaker.color.substring(3)
                border.color: palette.mid
                TapHandler { onDoubleTapped: subColorDialog.open() }
            }
            Label { text: qsTr("Color") }
        }
        footer: DialogButtonBox {
            Button { text: qsTr("&Save"); DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            Button { text: qsTr("&Cancel"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        }
    }

    Dialog {
        id: autofireDialog
        title: qsTr("Autofire Configuration")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAboutToShow: { fireOn.value = root.session.autofireOn; fireOff.value = root.session.autofireOff; fireLag.checked = root.session.autofireSkipsLag }
        // as Chimera's: "Pattern: N on, N off", and whether lag frames are taken into account (they do not move the pattern)
        ColumnLayout {
            RowLayout {
                Label { text: qsTr("Pattern:") }
                SpinBox { id: fireOn; from: 1; to: 512; editable: true; Layout.preferredWidth: 90 }
                Label { text: qsTr("on,") }
                SpinBox { id: fireOff; from: 1; to: 512; editable: true; Layout.preferredWidth: 90 }
                Label { text: qsTr("off") }
            }
            CheckBox { id: fireLag; text: qsTr("Take lag frames into account") }
        }
        onAccepted: { root.session.autofireOn = fireOn.value; root.session.autofireOff = fireOff.value; root.session.autofireSkipsLag = fireLag.checked }
    }
    // Chimera's Go to Frame: the machine paused, a frame index typed
    Dialog {
        id: seekDialog
        title: qsTr("Go to Frame")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAboutToShow: { root.session.run("pause"); seekTo.text = ""; seekTo.forceActiveFocus() }
        ColumnLayout {
            Label { text: qsTr("Jump/Seek to frame index:") }
            TextField {
                id: seekTo
                validator: IntValidator { bottom: 0 }
                Layout.preferredWidth: 200
                onAccepted: seekDialog.accept()
            }
        }
        onAccepted: if (seekTo.text !== "") root.session.run("seek", [parseInt(seekTo.text)])
    }
    RamSearch {
        id: ramSearchWindow; vm: root.ramSearch; folderOf: root.folderOf; visible: false
        clipboard: root.clipboard; customize: root.customize; themeColor: root.osdColor
        toolWindows: root.toolWindows; mainWindow: root
        showRamWatch: () => { ramWatchWindow.show(); ramWatchWindow.raise() }
        viewInHexEditor: (domain, address) => {
            root.hexEditor.domain = domain
            root.hexEditor.run("goTo", [address])
            hexEditorWindow.show()
            hexEditorWindow.raise()
        }
    }
    RamWatch {
        id: ramWatchWindow; vm: root.ramWatch; folderOf: root.folderOf; visible: false
        clipboard: root.clipboard; customize: root.customize; themeColor: root.osdColor
        toolWindows: root.toolWindows; mainWindow: root; gameProperties: root.gameProperties
        viewInHexEditor: (domain, address) => {
            root.hexEditor.domain = domain
            root.hexEditor.run("goTo", [address])
            hexEditorWindow.show()
            hexEditorWindow.raise()
        }
    }
    NewProject { id: newProjectWindow; vm: root.newProject; askFirst: root.leaveProject; mainWindow: root; themeColor: root.osdColor; visible: false }
    MovieImport {
        id: movieImportWindow
        vm: root.newProject
        mainWindow: root
        themeColor: root.osdColor
        visible: false
        // what the core noted, as Chimera says it, before the wizard
        onTaken: {
            if (root.newProject.importSummary !== "") importNotesDialog.open()
            else { newProjectWindow.show(); newProjectWindow.raise() }
        }
    }
    MessageDialog {
        id: importNotesDialog
        title: movieImportWindow.title
        text: root.newProject.importSummary
        buttons: MessageDialog.Ok
        onAccepted: { newProjectWindow.show(); newProjectWindow.raise() }
        onRejected: { newProjectWindow.show(); newProjectWindow.raise() }
    }
    // the project's own window, beside this one: open exactly when a project is
    TAStudio {
        id: tastudioWindow
        session: root.session; pianoRoll: root.pianoRoll; branches: root.branches; projectDetails: root.projectDetails
        clipboard: root.clipboard; customize: root.customize; keyedItem: keyedMenuItem; folderOf: root.folderOf; mainWindow: root
        keyNamer: root.keyNamer; toolWindows: root.toolWindows
        visible: false
    }
    Connections {
        target: root.session
        function onHasProjectChanged() {
            if (root.session.hasProject) tastudioWindow.engage()
            else tastudioWindow.close()
        }
    }
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
    HexEditor {
        id: hexEditorWindow; vm: root.hexEditor; folderOf: root.folderOf; visible: false
        clipboard: root.clipboard; customize: root.customize; themeColor: root.osdColor
        toolWindows: root.toolWindows; mainWindow: root
        showRamWatch: () => { ramWatchWindow.show(); ramWatchWindow.raise() }
    }
    Themes { id: themesWindow; vm: root.themes; colors: root.themeColors; mainWindow: root; visible: false }
    // Chimera's Controller Config and Configure Hotkeys, each a window of its own
    ControllerConfig {
        id: controllerWindow; controls: root.controls; keyNamer: root.keyNamer; session: root.session
        mainWindow: root; themeColor: root.osdColor; visible: false
    }
    HotkeyConfig {
        id: hotkeyWindow; hotkeys: root.hotkeys; keyNamer: root.keyNamer; session: root.session
        mainWindow: root; themeColor: root.osdColor; visible: false
    }
    LuaConsole {
        id: luaConsoleWindow; vm: root.luaConsole; folderOf: root.folderOf; visible: false
        fileUrl: (path) => root.pictureFiles.folderUrl(path); localPath: (url) => root.pictureFiles.localPath(url)
        clipboard: root.clipboard; customize: root.customize; themeColor: root.osdColor
        toolWindows: root.toolWindows; mainWindow: root
    }
    Cores { id: coresWindow; vm: root.cores; visible: false }

    FileDialog {
        id: projectDialog
        title: qsTr("Choose a project")
        currentFolder: root.folderOf("Movies")
        nameFilters: [qsTr("Chimera projects (*.chimeraProject)"), qsTr("All files (*)")]
        // the project's core is found among the installed ones
        onAccepted: root.leaveProject(() => root.session.run("openProject", [selectedFile]))
    }
    // the windows Lua scripts make with the forms library
    LuaForms { vm: root.forms }
    // Chimera's File > Encode Video: a window of its own, modeless
    EncodeVideo {
        id: encodeWindow
        vm: root.encoder; session: root.session; pianoRoll: root.pianoRoll; paths: root.paths; files: root.pictureFiles
        picture: picture
        mainWindow: root
        visible: false
    }
    // Chimera's: a video is an encode of a run, so there is none without a project
    function encodeVideo() {
        if (!root.session.hasProject) { encodeRefused.open(); return }
        encodeWindow.show()
        encodeWindow.raise()
    }
    MessageDialog {
        id: encodeRefused
        text: qsTr("Open a project first: a video is an encode of a run.")
        buttons: MessageDialog.Ok
    }
    Connections {
        target: root.encoder
        function onPhaseChanged() {
            if (root.encoder.phase === "encoding") root.note(qsTr("Encoding video"))
            else if (root.encoder.phase === "done") root.note(qsTr("Video encoded"))
            else if (root.encoder.phase === "stopped") root.note(qsTr("Encoding stopped"))
            else if (root.encoder.phase === "failed") root.note(qsTr("Encoding failed"))
        }
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
        // Capture OSD: the picture area as drawn, with what is over it
        onAccepted: {
            if (root.session.captureOsd) pictureArea.grabToImage(grab => root.pictureFiles.saveGrab(selectedFile, grab.image))
            else root.pictureFiles.save(selectedFile)
        }
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

    Rectangle {
        id: pictureArea
        objectName: "pictureArea"
        anchors.fill: parent
        color: "black"
        // the pointer over the picture, in the machine's pixels, for Lua's input.getmouse:
        // only watched, never taken from what is under it
        PointHandler {
            acceptedButtons: Qt.AllButtons
            grabPermissions: PointerHandler.ApprovesTakeOverByAnything
            onPointChanged: pictureArea.notePointer(point.position, point.pressedButtons)
        }
        HoverHandler {
            // full screen, the pointer hidden after two seconds still, as Chimera's (moving it more than 5 pixels shows it)
            cursorShape: root.cursorHidden ? Qt.BlankCursor : Qt.ArrowCursor
            onPointChanged: {
                pictureArea.notePointer(point.position, -1)
                const p = point.position
                if (Math.abs(p.x - root.cursorAt.x) > 5 || Math.abs(p.y - root.cursorAt.y) > 5) {
                    root.cursorAt = p
                    root.cursorHidden = false
                    mouseIdle.restart()
                }
            }
        }
        Timer {
            id: mouseIdle
            interval: 2000
            running: true
            repeat: true
            onTriggered: if (root.visibility === Window.FullScreen && root.display.autohideMouseFullscreen) root.cursorHidden = true
        }
        WheelHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: (event) => root.hostInput.mouseWheel = root.hostInput.mouseWheel + event.angleDelta.y
        }
        // a right click: the window's menu, as Chimera's, unless the right button holds one of the
        // machine's buttons (Shift held opens it anyway)
        TapHandler {
            acceptedButtons: Qt.RightButton
            grabPermissions: PointerHandler.ApprovesTakeOverByAnything
            onTapped: (eventPoint) => {
                if (!root.customize.showContextMenu) return
                if (!root.keyNamer.shiftHeld() && root.controls.run("hasBinding", ["WMouse R"]) === true) return
                pictureMenu.popup()
            }
        }
        // a double click: full screen and back, as Chimera's, when the display settings allow it and the
        // left button holds none of the machine's buttons (Shift held does it anyway)
        TapHandler {
            acceptedButtons: Qt.LeftButton
            grabPermissions: PointerHandler.ApprovesTakeOverByAnything
            onDoubleTapped: {
                const shift = root.keyNamer.shiftHeld()
                if (!root.display.allowDoubleClickFullscreen && !shift) return
                if (!shift && root.controls.run("hasBinding", ["WMouse L"]) === true) return
                root.visibility = root.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen
            }
        }
        function notePointer(at, buttons) {
            const shown = picture.shownRect, src = picture.sourceRect
            if (shown.width > 0 && shown.height > 0) {
                root.hostInput.mouseX = Math.floor(src.x + (at.x - shown.x) * src.width / shown.width)
                root.hostInput.mouseY = Math.floor(src.y + (at.y - shown.y) * src.height / shown.height)
                // for an analog control bound to the pointer: Chimera's -10000 at the picture's left or top to 10000 at the other side
                root.session.run("pointer", [Math.round((at.x - shown.x) / shown.width * 20000 - 10000),
                                             Math.round((at.y - shown.y) / shown.height * 20000 - 10000)])
            }
            if (buttons < 0) return
            const mask = (buttons & Qt.LeftButton ? 1 : 0) | (buttons & Qt.MiddleButton ? 2 : 0) | (buttons & Qt.RightButton ? 4 : 0)
                       | (buttons & Qt.BackButton ? 8 : 0) | (buttons & Qt.ForwardButton ? 16 : 0)
            // the pointer's buttons over the picture hold what they are bound to, as keys do (WMouse L, M, R, 1, 2)
            const names = ["WMouse L", "WMouse M", "WMouse R", "WMouse 1", "WMouse 2"]
            const was = root.hostInput.mouseButtons
            for (let b = 0; b < 5; b++)
                if (((mask ^ was) >> b) & 1) root.session.run("key", [names[b], ((mask >> b) & 1) === 1])
            root.hostInput.mouseButtons = mask
        }
        // nothing open: Chimera's mark, faint, blinking now and then, its eye
        // following the pointer across the window
        PictureItem {
            id: idleEyePicture
            objectName: "idleEye"
            anchors.fill: parent
            z: 1
            visible: !root.session.isOpen
            source: root.idleEye.picture("eye")
            smooth: true
            HoverHandler { id: idlePointer }
            Timer {
                interval: 16
                repeat: true
                running: idleEyePicture.visible
                property real gaze: 0
                onTriggered: {
                    // where the pointer is across the picture; gone off it, the last place holds
                    if (idlePointer.hovered && idleEyePicture.width > 0) gaze = idlePointer.point.position.x / idleEyePicture.width * 2 - 1
                    root.idleEye.run("tick", [gaze])
                }
            }
        }
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
        // what scripts draw with Chimera's gui library: in the machine's
        // pixels, over the picture where it is drawn; gui.text in the window's
        Canvas {
            id: luaCanvas
            objectName: "luaCanvas"
            anchors.fill: parent
            readonly property var shapes: JSON.parse(root.guiOverlay.shapes)
            onShapesChanged: requestPaint()
            onImageLoaded: requestPaint()
            Connections { target: picture; function onPlacementChanged() { luaCanvas.requestPaint() } }
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.clearRect(0, 0, width, height)
                const shown = picture.shownRect, src = picture.sourceRect
                if (luaCanvas.shapes.length === 0) return
                const sx = src.width > 0 ? shown.width / src.width : 1, sy = src.height > 0 ? shown.height / src.height : 1
                for (const s of luaCanvas.shapes) {
                    if (s.k === "screentext") {
                        ctx.save()
                        ctx.font = "12px monospace"
                        ctx.textBaseline = "top"
                        const w = ctx.measureText(s.text).width
                        const right = s.anchor === "topright" || s.anchor === "bottomright" || s.anchor === "1" || s.anchor === "3"
                        const bottom = s.anchor === "bottomleft" || s.anchor === "bottomright" || s.anchor === "2" || s.anchor === "3"
                        ctx.fillStyle = s.color
                        ctx.fillText(s.text, right ? width - s.x - w : s.x, bottom ? height - s.y - 12 : s.y)
                        ctx.restore()
                        continue
                    }
                    ctx.save()
                    ctx.translate(shown.x - src.x * sx, shown.y - src.y * sy)
                    ctx.scale(sx, sy)
                    Shapes.draw(ctx, luaCanvas, s, sx, sy, Shapes.urlOf)
                    ctx.restore()
                }
            }
        }
        Connections { target: root.guiOverlay; function onMessageChanged() { root.note(root.guiOverlay.message) } }
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
    }

    // every message the window shows, for the log window; and the ones over the picture now
    ListModel { id: messageLog }
    ListModel { id: onScreenMessages }
    // the picture N times the machine's own, the window grown or shrunk to it
    property int windowScale: 1
    function sizeWindow(n) {
        root.windowScale = n
        const w = picture.pictureWidth * n, h = picture.pictureHeight * n
        root.height = Math.round(root.height - pictureArea.height + h)
        root.width = Math.round(root.width - pictureArea.width + w)
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
    LogWindow {
        id: logWindow; log: messageLog; clipboard: root.clipboard; visible: false
        customize: root.customize; toolWindows: root.toolWindows; mainWindow: root
    }
    BatchRun { id: batchRunWindow; vm: root.batchRun; folderOf: root.folderOf; visible: false }
    MediaMaker { id: mediaMakerWindow; vm: root.mediaMaker; mainWindow: root; visible: false }
    CacheManager { id: cacheManagerWindow; vm: root.cacheManager; themeColor: root.osdColor; visible: false; onBudgetsAsked: (projectId, projectLabel) => {
            greenzoneBudgetsWindow.projectId = projectId
            greenzoneBudgetsWindow.projectLabelAsked = projectLabel
            greenzoneBudgetsWindow.show()
            greenzoneBudgetsWindow.raise()
        }
    }
    GreenzoneBudgets { id: greenzoneBudgetsWindow; session: root.session; caches: root.cacheManager; mainWindow: root; visible: false }
    Customize { id: customizeWindow; vm: root.customize; session: root.session; hotkeys: root.hotkeys; mainWindow: root; visible: false }
    MessagesConfig { id: messagesWindow; vm: root.messages; mainWindow: root; visible: false }
    SoundConfig { id: soundWindow; session: root.session; mainWindow: root; visible: false }
    DisplayConfig { id: displayWindow; vm: root.display; session: root.session; mainWindow: root; visible: false }
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
    ReportIssue { id: reportIssueWindow; vm: root.help; clipboard: root.clipboard; pictureFiles: root.pictureFiles; folderOf: root.folderOf; gameRunning: root.session.isOpen; mainWindow: root; visible: false }
    PathsConfig { id: pathsWindow; vm: root.paths; session: root.session; mainWindow: root; visible: false }
    FirmwareConfig { id: firmwareWindow; vm: root.firmware; folderOf: root.folderOf; themeColor: root.osdColor; visible: false }
    DataFolderConfig { id: dataFolderWindow; vm: root.dataFolder; themeColor: root.osdColor; mainWindow: root; visible: false }
    PrecompiledModules { id: precompiledWindow; vm: root.precompiled; themeColor: root.osdColor; visible: false }
    TraceLogger {
        id: traceLoggerWindow; vm: root.traceLogger; session: root.session; visible: false
        folderOf: root.folderOf; customize: root.customize; toolWindows: root.toolWindows; mainWindow: root
    }
    Debugger {
        id: debuggerWindow; vm: root.debuggerTool; session: root.session; visible: false
        customize: root.customize; toolWindows: root.toolWindows; mainWindow: root
    }
    SurfaceViewer {
        id: surfaceViewerWindow; vm: root.surfaceViewer; session: root.session; visible: false
        clipboard: root.clipboard; localPath: (url) => root.pictureFiles.localPath(url); themeColor: root.osdColor
        customize: root.customize; toolWindows: root.toolWindows; mainWindow: root
    }
    MacroTool {
        id: macroToolWindow; vm: root.macroTool; pianoRoll: root.pianoRoll; folderOf: root.folderOf; visible: false
        customize: root.customize; toolWindows: root.toolWindows; mainWindow: root
    }
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

    // only while a machine runs: everything in it is about the machine, as Chimera's
    footer: ToolBar {
        visible: (root.visibility === Window.FullScreen ? root.display.statusBarFullscreen : root.session.showStatusBar) && root.session.isOpen
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            Label { text: root.session.status; elide: Text.ElideRight; Layout.fillWidth: true }
            Label { text: root.session.gamepads; elide: Text.ElideRight; Layout.maximumWidth: 260 }
            Label { text: root.session.sound; elide: Text.ElideRight; Layout.maximumWidth: 200 }
            Label { visible: root.encoder.phase === "encoding"; text: qsTr("Encoding (%1 of %2 frames)").arg(root.encoder.framesDone).arg(root.encoder.frameCount); color: "#b00020" }
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
            Label { text: qsTr("seek %1 frames, %2 ms").arg(root.session.lastSeekFrames).arg(Number(root.session.lastSeekMs).toFixed(0)) }
            Label { text: qsTr("%1 states").arg(root.session.greenzoneStates) }
            Label { text: qsTr("%1 rerecords").arg(root.session.rerecords) }
        }
    }
}
