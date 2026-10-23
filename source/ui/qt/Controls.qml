// Configure Hotkeys: layout and bindings only, over the hotkeys view model.
// A row's keys can be typed, or caught: Catch, then press the key or the
// gamepad input. (Controller Config is ControllerConfig.qml.)
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "hotkeyConfigWindow"
    required property QtObject hotkeys
    required property QtObject keyNamer
    required property QtObject session
    // the row waiting for a key ("" for none)
    property string catching: ""
    property string catchAction: ""
    title: qsTr("Configure Hotkeys")
    // the key or pad input caught goes to the row that asked
    function take(name) {
        if (win.catching === "hotkey") win.hotkeys.run("addKey", [win.catchAction, name])
        win.catching = ""
        win.catchAction = ""
    }
    onClosing: if (win.catching !== "") { win.catching = ""; win.session.run("stopCatchPad") }
    width: 640
    height: 600

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8

        ListView {
                id: hotkeyList
                Layout.fillWidth: true
                Layout.fillHeight: true
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
            Label { text: win.hotkeys.error; color: "#b00020"; Layout.fillWidth: true }
            Button { text: qsTr("Reset Every Hotkey"); onClicked: win.hotkeys.run("resetAll") }
        }
    }
}
