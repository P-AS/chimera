// Chimera's Customize window (Config > Customize): layout and bindings
// only, over the customize view model and the session's two switches.
// Each box is kept the moment it is ticked.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "customizeWindow"
    required property QtObject vm
    required property QtObject session
    title: qsTr("Customization Options")
    width: 560
    height: 420

    component Note: Label { wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.leftMargin: 28; opacity: 0.75 }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        TabBar {
            id: tabs
            Layout.fillWidth: true
            TabButton { text: qsTr("General") }
            TabButton { text: qsTr("Advanced") }
        }
        StackLayout {
            currentIndex: tabs.currentIndex
            Layout.fillWidth: true
            Layout.fillHeight: true
            ColumnLayout {
                CheckBox { text: qsTr("Start paused"); checked: win.vm.startPaused; onToggled: win.vm.startPaused = checked }
                Note { text: qsTr("The first game or project opened waits paused. After that, one opened runs when the last was running.") }
                CheckBox { text: qsTr("Start in full screen"); checked: win.vm.startFullscreen; onToggled: win.vm.startFullscreen = checked }
                Note { text: qsTr("Takes effect the next time Chimera starts.") }
                CheckBox { text: qsTr("Pause when a menu is open"); checked: win.vm.pauseWhenMenuActivated; onToggled: win.vm.pauseWhenMenuActivated = checked }
                CheckBox { text: qsTr("Run in background"); checked: win.vm.runInBackground; onToggled: win.vm.runInBackground = checked }
                Note { text: qsTr("When this is set, the game keeps running while the window is behind another.") }
                CheckBox { text: qsTr("Accept background input"); checked: win.session.acceptBackgroundInput; onToggled: win.session.acceptBackgroundInput = checked }
                Note { text: qsTr("Gamepads are heard while other programs are in front. The keyboard is only ever heard by the window in front.") }
                CheckBox { text: qsTr("Never ask to save: changes not saved are let go"); checked: win.vm.neverAskSave; onToggled: win.vm.neverAskSave = checked }
                Note { text: qsTr("When a project is closed, another opened or Chimera closed.") }
                Item { Layout.fillHeight: true }
            }
            ColumnLayout {
                CheckBox { text: qsTr("Frame advance button skips non-input frames"); checked: win.session.skipLagFrames; onToggled: win.session.skipLagFrames = checked }
                Note { text: qsTr("When enabled, the frame advance button will skip over frames in which no input was polled (lag frames).") }
                CheckBox { text: qsTr("Run Lua scripts when turboing"); checked: win.vm.luaDuringTurbo; onToggled: win.vm.luaDuringTurbo = checked }
                Item { Layout.fillHeight: true }
            }
        }
        RowLayout {
            Item { Layout.fillWidth: true }
            Button { text: qsTr("Close"); onClicked: win.close() }
        }
    }
}
