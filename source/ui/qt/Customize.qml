// Chimera's Customize window (Config > Customize), laid out as Chimera's
// GuiOptions: General (the context menu, the save prompts, listening in
// the background, single instance, how keys are named, the GameCube
// adapter, Startup Options) and Advanced (lag frames, Lua when turboing,
// Key Priority). The boxes are a copy: OK keeps them, Cancel lets them go.
// Layout and bindings only, over the customize view model and the
// session's and hotkeys' switches.
//
// Run in background and Pause when menu activated are gone, as from
// Chimera (owner, 2026-10-24): nothing pauses the machine but the person,
// whatever window is in front or menu open.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "customizeWindow"
    required property QtObject vm
    required property QtObject session
    required property QtObject hotkeys
    required property QtObject controls
    property Window mainWindow: null
    title: qsTr("Customization Options")
    // Chimera's is 418 wide for its 8-point font; wide enough here for the longest line
    width: 520
    height: 520
    modality: Qt.ApplicationModal
    transientParent: win.mainWindow

    // the copy, read when the window opens
    property var picked: ({})
    onVisibleChanged: if (visible) {
        tabs.currentIndex = 0
        win.picked = {
            showContextMenu: win.vm.showContextMenu, neverAskSave: win.vm.neverAskSave,
            acceptBackgroundInput: win.session.acceptBackgroundInput, startPaused: win.vm.startPaused,
            startFullscreen: win.vm.startFullscreen, skipLagFrames: win.session.skipLagFrames,
            luaDuringTurbo: win.vm.luaDuringTurbo, noMixedPriority: win.hotkeys.noMixedPriority,
            alternateLayouts: win.vm.alternateLayouts, mergeModifiers: win.vm.mergeModifiers,
            singleInstance: win.vm.singleInstance, gameCubeAdapter: win.vm.gameCubeAdapter
        }
    }
    function pick(name, value) {
        const now = Object.assign({}, win.picked)
        now[name] = value
        win.picked = now
    }
    function keep() {
        const p = win.picked
        win.vm.showContextMenu = p.showContextMenu
        win.vm.neverAskSave = p.neverAskSave
        win.session.acceptBackgroundInput = p.acceptBackgroundInput
        win.vm.startPaused = p.startPaused
        win.vm.startFullscreen = p.startFullscreen
        win.session.skipLagFrames = p.skipLagFrames
        win.vm.luaDuringTurbo = p.luaDuringTurbo
        win.hotkeys.noMixedPriority = p.noMixedPriority
        win.vm.alternateLayouts = p.alternateLayouts
        win.vm.mergeModifiers = p.mergeModifiers
        win.vm.singleInstance = p.singleInstance
        win.vm.gameCubeAdapter = p.gameCubeAdapter
    }
    // OK, as Chimera's: a change to merging the modifier keys first asks
    // whether the keys bound are rewritten for it (Cancel keeps the window open)
    function ok() {
        if (win.picked.mergeModifiers !== win.vm.mergeModifiers) { rewriteAsk.open(); return }
        win.keep()
        win.close()
    }
    MessageDialog {
        id: rewriteAsk
        title: qsTr("Rewrite keybinds now?")
        text: qsTr("Would you like to replace %1,\nand the same for the other modifier keys,\nin existing keybinds for hotkeys and all systems' gamepads?")
            .arg(win.picked.mergeModifiers ? qsTr("LShift and RShift with Shift") : qsTr("Shift with LShift"))
        buttons: MessageDialog.Yes | MessageDialog.No | MessageDialog.Cancel
        onButtonClicked: (button, role) => {
            if (button === MessageDialog.Cancel) return
            const merging = win.picked.mergeModifiers
            win.keep()
            if (button === MessageDialog.Yes) {
                const names = win.vm.run("modifierKeys", [merging])
                win.hotkeys.run("replaceKeys", [names])
                win.controls.run("replaceKeys", [names])
            }
            win.close()
        }
    }

    component Box: CheckBox {
        required property string setting
        checked: win.picked[setting] === true
        onToggled: win.pick(setting, checked)
        padding: 2
    }
    component Note: Label { Layout.leftMargin: 20; wrapMode: Text.WordWrap; Layout.fillWidth: true }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 9
        spacing: 6
        TabBar {
            id: tabs
            Layout.fillWidth: true
            TabButton { text: qsTr("General"); width: implicitWidth }
            TabButton { text: qsTr("Advanced"); width: implicitWidth }
        }
        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            StackLayout {
                anchors.fill: parent
                currentIndex: tabs.currentIndex
                ColumnLayout {
                    spacing: 2
                    Box { setting: "showContextMenu"; text: qsTr("Enable Context Menu") }
                    Box { setting: "neverAskSave"; text: qsTr("When Chimera is closing, skip \"unsaved changes\" prompts and discard all.") }
                    RowLayout {
                        spacing: 16
                        Box { setting: "acceptBackgroundInput"; text: qsTr("Accept background input") }
                        // the keyboard is only ever heard by the window in front here: background input is the controllers'
                        CheckBox {
                            text: qsTr("From controller only")
                            checked: true
                            enabled: false
                            padding: 2
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("The keyboard is only ever heard by the window in front.")
                        }
                    }
                    Note { text: qsTr("\"Eavesdrops\" on keyboard and gamepad input while other programs are focused.") }
                    // Windows only, as Chimera's: read when the program starts
                    Box { setting: "singleInstance"; text: qsTr("Single instance mode"); enabled: Qt.platform.os === "windows"; Layout.topMargin: 8 }
                    Box { setting: "alternateLayouts"; text: qsTr("Handle alternate keyboard layouts (e.g. Dvorak) [experimental]"); Layout.topMargin: 8 }
                    Box { setting: "mergeModifiers"; text: qsTr("Merge L+R modifier keys e.g. Shift instead of LShift and RShift") }
                    // read when the program starts: SDL is told before it starts
                    Box { setting: "gameCubeAdapter"; text: qsTr("Enable Wii U/Switch GameCube Adapter Support") }
                    GroupBox {
                        title: qsTr("Startup Options")
                        Layout.fillWidth: true
                        Layout.topMargin: 8
                        ColumnLayout {
                            spacing: 2
                            Box { setting: "startPaused"; text: qsTr("Start paused") }
                            Box { setting: "startFullscreen"; text: qsTr("Start in Fullscreen") }
                        }
                    }
                    Item { Layout.fillHeight: true }
                }
                ColumnLayout {
                    spacing: 2
                    Box { setting: "skipLagFrames"; text: qsTr("Frame advance button skips non-input frames") }
                    Note { text: qsTr("When enabled, the frame advance button will skip over\nframes in which no input was polled (lag frames)") }
                    Box { setting: "luaDuringTurbo"; text: qsTr("Run lua scripts when turboing"); Layout.topMargin: 8 }
                    Box { setting: "noMixedPriority"; text: qsTr("Key Priority Toggle - Remove Mixed Key Priority Options"); Layout.topMargin: 8 }
                    Item { Layout.fillHeight: true }
                }
            }
        }
        RowLayout {
            Item { Layout.fillWidth: true }
            Button { text: qsTr("&OK"); onClicked: win.ok() }
            Button { text: qsTr("&Cancel"); onClicked: win.close() }
        }
    }
}
