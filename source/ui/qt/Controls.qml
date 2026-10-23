// Controls and hotkeys: layout and bindings only, over the controls and
// hotkeys view models. A row's keys can be typed, or caught: Catch, then
// press the key or the gamepad input.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "controlsWindow"
    required property QtObject controls
    required property QtObject hotkeys
    required property QtObject keyNamer
    required property QtObject session
    // the row waiting for a key: which list, and which row (-1: none)
    property string catching: ""
    property int catchRow: -1
    property string catchAction: ""
    title: qsTr("Controls and Hotkeys")
    // the key or pad input caught goes to the row that asked
    // Chimera's Config > Controllers... and Hotkeys...: this window, on that tab
    function showTab(index) {
        tabs.currentIndex = index
        win.show()
        win.raise()
    }
    function take(name) {
        if (win.catching === "button") win.controls.run("addKey", [win.catchRow, name])
        else if (win.catching === "hotkey") win.hotkeys.run("addKey", [win.catchAction, name])
        win.catching = ""
        win.catchRow = -1
        win.catchAction = ""
    }
    onClosing: if (win.catching !== "") { win.catching = ""; win.session.run("stopCatchPad") }
    width: 640
    height: 600

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8

        TabBar {
            id: tabs
            Layout.fillWidth: true
            TabButton { text: qsTr("Buttons (%1)").arg(win.controls.core === "" ? qsTr("no machine") : win.controls.core) }
            TabButton { text: qsTr("Hotkeys") }
        }

        StackLayout {
            currentIndex: tabs.currentIndex
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: buttonList
                clip: true
                model: win.controls.rows("buttons")
                ScrollBar.vertical: ScrollBar {}
                delegate: RowLayout {
                    id: brow
                    required property int index
                    required property string button
                    required property string keys
                    required property bool changed
                    width: buttonList.width - 16
                    Label { text: brow.button + (brow.changed ? " *" : ""); Layout.preferredWidth: 160; elide: Text.ElideRight }
                    TextField {
                        text: brow.keys
                        Layout.fillWidth: true
                        onEditingFinished: if (text !== brow.keys) win.controls.run("bind", [brow.index, text])
                    }
                    Button {
                        text: win.catching === "button" && win.catchRow === brow.index ? qsTr("Press a key or a pad input...") : qsTr("Catch")
                        onClicked: { win.catching = "button"; win.catchRow = brow.index; catcher.forceActiveFocus(); win.session.run("catchPad") }
                    }
                    Button { text: qsTr("Reset"); enabled: brow.changed; onClicked: win.controls.run("reset", [brow.index]) }
                }
            }

            ListView {
                id: hotkeyList
                clip: true
                model: win.hotkeys.rows("hotkeys")
                section.property: "group"
                section.delegate: Label { required property string section; text: section; font.bold: true; topPadding: 6 }
                ScrollBar.vertical: ScrollBar {}
                delegate: RowLayout {
                    id: hrow
                    required property string action
                    required property string keys
                    required property bool changed
                    width: hotkeyList.width - 16
                    Label { text: hrow.action + (hrow.changed ? " *" : ""); Layout.preferredWidth: 180; elide: Text.ElideRight }
                    TextField {
                        text: hrow.keys
                        Layout.fillWidth: true
                        onEditingFinished: if (text !== hrow.keys) win.hotkeys.run("bind", [hrow.action, text])
                    }
                    Button {
                        text: win.catching === "hotkey" && win.catchAction === hrow.action ? qsTr("Press a key or a pad input...") : qsTr("Catch")
                        onClicked: { win.catching = "hotkey"; win.catchAction = hrow.action; catcher.forceActiveFocus(); win.session.run("catchPad") }
                    }
                    Button { text: qsTr("Reset"); enabled: hrow.changed; onClicked: win.hotkeys.run("reset", [hrow.action]) }
                }
            }
        }

        // takes the next key pressed for the row that asked
        Item {
            id: catcher
            focus: false
            Keys.onPressed: (event) => {
                const asHotkey = win.catching === "hotkey"
                const name = win.keyNamer.name(event.key, event.modifiers, asHotkey)
                // a modifier alone is part of a combination still being pressed
                if (name === "" || (asHotkey && (name === "Shift" || name === "Ctrl" || name === "Alt"))) return
                win.take(name)
                win.session.run("stopCatchPad")
                event.accepted = true
            }
        }
        // or the gamepad input pressed first
        Connections {
            target: win.session
            function onCaughtPadChanged() { if (win.session.caughtPad !== "") win.take(win.session.caughtPad) }
        }

        RowLayout {
            Label { text: win.controls.error !== "" ? win.controls.error : win.hotkeys.error; color: "#b00020"; Layout.fillWidth: true }
            Button {
                text: tabs.currentIndex === 0 ? qsTr("Reset Every Button") : qsTr("Reset Every Hotkey")
                onClicked: tabs.currentIndex === 0 ? win.controls.run("resetAll") : win.hotkeys.run("resetAll")
            }
        }
    }
}
