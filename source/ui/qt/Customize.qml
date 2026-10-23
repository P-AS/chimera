// Chimera's Customize window (Config > Customize), laid out as Chimera's
// GuiOptions: General (the menu and context menu, the save prompts,
// running and listening in the background, Startup Options) and Advanced
// (lag frames, Lua when turboing, Key Priority). The boxes are a copy: OK
// keeps them, Cancel lets them go. Layout and bindings only, over the
// customize view model and the session's and hotkeys' switches.
//
// Not here, as the prototype has no such thing: Chimera's alternate
// keyboard layouts and merged modifier keys (Qt names keys by the layout,
// and Shift, Ctrl and Alt always arrive merged), the GameCube adapter,
// single instance mode, and Skip waterbox integrity checks (a flag no part
// of Chimera reads).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "customizeWindow"
    required property QtObject vm
    required property QtObject session
    required property QtObject hotkeys
    property Window mainWindow: null
    title: qsTr("Customization Options")
    // Chimera's is 418 wide for its 8-point font; wide enough here for the longest line
    width: 520
    height: 424
    modality: Qt.ApplicationModal
    transientParent: win.mainWindow

    // the copy, read when the window opens
    property var picked: ({})
    onVisibleChanged: if (visible) {
        tabs.currentIndex = 0
        win.picked = {
            pauseWhenMenuActivated: win.vm.pauseWhenMenuActivated, showContextMenu: win.vm.showContextMenu,
            neverAskSave: win.vm.neverAskSave, runInBackground: win.vm.runInBackground,
            acceptBackgroundInput: win.session.acceptBackgroundInput, startPaused: win.vm.startPaused,
            startFullscreen: win.vm.startFullscreen, skipLagFrames: win.session.skipLagFrames,
            luaDuringTurbo: win.vm.luaDuringTurbo, noMixedPriority: win.hotkeys.noMixedPriority
        }
    }
    function pick(name, value) {
        const now = Object.assign({}, win.picked)
        now[name] = value
        win.picked = now
    }
    function keep() {
        const p = win.picked
        win.vm.pauseWhenMenuActivated = p.pauseWhenMenuActivated
        win.vm.showContextMenu = p.showContextMenu
        win.vm.neverAskSave = p.neverAskSave
        win.vm.runInBackground = p.runInBackground
        win.session.acceptBackgroundInput = p.acceptBackgroundInput
        win.vm.startPaused = p.startPaused
        win.vm.startFullscreen = p.startFullscreen
        win.session.skipLagFrames = p.skipLagFrames
        win.vm.luaDuringTurbo = p.luaDuringTurbo
        win.hotkeys.noMixedPriority = p.noMixedPriority
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
                    RowLayout {
                        spacing: 16
                        Box { setting: "pauseWhenMenuActivated"; text: qsTr("Pause when menu activated") }
                        Box { setting: "showContextMenu"; text: qsTr("Enable Context Menu") }
                    }
                    Box { setting: "neverAskSave"; text: qsTr("When Chimera is closing, skip \"unsaved changes\" prompts and discard all.") }
                    Box { setting: "runInBackground"; text: qsTr("Run in background") }
                    Note { text: qsTr("When this is set, the client will continue to run when it loses focus.") }
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
            Button { text: qsTr("&OK"); onClicked: { win.keep(); win.close() } }
            Button { text: qsTr("&Cancel"); onClicked: win.close() }
        }
    }
}
