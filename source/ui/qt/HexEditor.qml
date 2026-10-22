// The hex editor: layout and bindings only, over the hexEditor view model.
// Only the lines on screen exist; the scroll bar and the wheel move the
// first line shown.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "hexEditorWindow"
    required property QtObject vm
    readonly property int lineHeight: 18
    title: qsTr("Hex Editor")
    width: 820
    height: 640

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        RowLayout {
            ComboBox {
                model: win.vm.domains === "" ? [] : win.vm.domains.split(",")
                currentIndex: model.indexOf(win.vm.domain)
                onActivated: win.vm.domain = currentText
                Layout.preferredWidth: 130
            }
            ComboBox {
                // the view model's sizes are 1, 2 and 4 bytes, in that order here
                model: [qsTr("1 byte"), qsTr("2 bytes"), qsTr("4 bytes")]
                currentIndex: win.vm.size === 4 ? 2 : win.vm.size - 1
                onActivated: (i) => win.vm.size = [1, 2, 4][i]
            }
            CheckBox { text: qsTr("Big endian"); checked: win.vm.bigEndian; onToggled: win.vm.bigEndian = checked }
            Item { Layout.fillWidth: true }
            TextField { id: goTo; placeholderText: qsTr("Address (hex)"); Layout.preferredWidth: 110; onAccepted: go.clicked() }
            Button { id: go; text: qsTr("Go To"); onClicked: win.vm.run("goTo", [parseInt(goTo.text, 16)]) }
        }

        RowLayout {
            TextField { id: findText; placeholderText: qsTr("Bytes to find (hex)"); Layout.preferredWidth: 180; onAccepted: findNext.clicked() }
            Button { id: findNext; text: qsTr("Find Next"); onClicked: win.vm.run("find", [findText.text]) }
            Item { Layout.fillWidth: true }
            Label {
                text: win.vm.picked < 0 ? qsTr("Nothing picked") : qsTr("Picked %1").arg(win.vm.picked.toString(16).toUpperCase())
            }
            TextField { id: pokeText; placeholderText: qsTr("Value (hex)"); enabled: win.vm.writable && win.vm.picked >= 0; Layout.preferredWidth: 100; onAccepted: write.clicked() }
            Button { id: write; text: qsTr("Write"); enabled: pokeText.enabled; onClicked: win.vm.run("poke", [win.vm.picked, pokeText.text]) }
        }

        Rectangle {
            id: area
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: palette.base
            clip: true
            onHeightChanged: win.vm.visibleLines = Math.max(1, Math.floor(height / win.lineHeight))
            Component.onCompleted: win.vm.visibleLines = Math.max(1, Math.floor(height / win.lineHeight))

            Column {
                x: 4
                Repeater {
                    model: win.vm.rows("lines")
                    delegate: Row {
                        id: line
                        required property var model
                        required property int index
                        height: win.lineHeight
                        spacing: 6
                        Text { text: line.model.address; color: palette.placeholderText; font.family: "monospace"; width: 90 }
                        Repeater {
                            model: 16 / win.vm.size
                            delegate: Text {
                                required property int index
                                readonly property int address: (win.vm.topLine + line.index) * 16 + index * win.vm.size
                                text: line.model["v" + index]
                                color: line.model["c" + index] ? "#d02020" : palette.text
                                font.family: "monospace"
                                font.bold: address === win.vm.picked
                                width: win.vm.size * 2 * 9 + (win.vm.size === 1 ? 2 : 6)
                                TapHandler { onTapped: win.vm.picked = parent.address }
                            }
                        }
                        Text { text: line.model.text; font.family: "monospace"; color: palette.text; leftPadding: 10 }
                    }
                }
            }
            WheelHandler {
                onWheel: (event) => {
                    const step = event.angleDelta.y > 0 ? -3 : 3
                    win.vm.topLine = Math.max(0, Math.min(win.vm.lineCount - win.vm.visibleLines, win.vm.topLine + step))
                }
            }
            ScrollBar {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                policy: ScrollBar.AlwaysOn
                size: win.vm.lineCount > 0 ? Math.min(1, win.vm.visibleLines / win.vm.lineCount) : 1
                position: win.vm.lineCount > 0 ? win.vm.topLine / win.vm.lineCount : 0
                onPositionChanged: if (pressed) win.vm.topLine = Math.round(position * win.vm.lineCount)
            }
        }

        Label { text: win.vm.status }
        Label { text: win.vm.error; color: "#b00020"; visible: text !== "" }
    }
}
