// Chimera's Messages window (Config > Messages): layout and bindings only,
// over the messages view model. A thing drawn over the picture is picked
// on the left; where it goes is set on the right. Kept the moment changed.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "messagesWindow"
    required property QtObject vm
    title: qsTr("Messages")
    width: 620
    height: 460
    readonly property var names: ["Fps", "Frame Counter", "Lag Counter", "Input Display", "Watches", "Messages", "Rerecords", "Autohold", "Game Time"]
    readonly property var corners: [qsTr("Top left"), qsTr("Top right"), qsTr("Bottom left"), qsTr("Bottom right")]
    readonly property var places: JSON.parse(win.vm.positions)
    property string picked: "Fps"
    readonly property var place: win.places[win.picked] || ({ "x": 0, "y": 0, "anchor": 0 })
    function setPlace(x, y, anchor) { win.vm.run("setPosition", [win.picked, x, y, anchor]) }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            GroupBox {
                title: qsTr("Message type")
                Layout.fillHeight: true
                ColumnLayout {
                    Repeater {
                        model: win.names
                        delegate: RadioButton {
                            required property string modelData
                            readonly property var p: win.places[modelData] || ({ "x": 0, "y": 0, "anchor": 0 })
                            text: qsTr("%1  (%2, %3)").arg(modelData).arg(p.x).arg(p.y)
                            checked: win.picked === modelData
                            onClicked: win.picked = modelData
                        }
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                GroupBox {
                    title: qsTr("%1: where it goes").arg(win.picked)
                    Layout.fillWidth: true
                    GridLayout {
                        columns: 2
                        Label { text: qsTr("Corner") }
                        ComboBox {
                            model: win.corners
                            currentIndex: win.place.anchor
                            onActivated: (i) => win.setPlace(win.place.x, win.place.y, i)
                        }
                        Label { text: qsTr("Across, from that side") }
                        SpinBox { from: 0; to: 4000; editable: true; value: win.place.x; onValueModified: win.setPlace(value, win.place.y, win.place.anchor) }
                        Label { text: qsTr("Down or up, from that edge") }
                        SpinBox { from: 0; to: 4000; editable: true; value: win.place.y; onValueModified: win.setPlace(win.place.x, value, win.place.anchor) }
                    }
                }
                GroupBox {
                    title: qsTr("Colours (left at their first colour, the theme's)")
                    Layout.fillWidth: true
                    GridLayout {
                        columns: 3
                        Repeater {
                            model: [
                                { "label": qsTr("Main Messages"), "which": "messages", "prop": "messagesColor" },
                                { "label": qsTr("Alert Messages"), "which": "alert", "prop": "alertColor" },
                                { "label": qsTr("Previous Frame Input"), "which": "lastInput", "prop": "lastInputColor" },
                                { "label": qsTr("Movie Input"), "which": "movieInput", "prop": "movieInputColor" }
                            ]
                            delegate: RowLayout {
                                required property var modelData
                                Layout.columnSpan: 3
                                Rectangle { width: 18; height: 18; color: win.vm[modelData.prop]; border.color: "gray" }
                                Label { text: modelData.label; Layout.fillWidth: true }
                                Label { text: win.vm[modelData.prop]; font.family: "monospace" }
                                Button {
                                    text: qsTr("Change...")
                                    onClicked: { colorDialog.which = modelData.which; colorDialog.selectedColor = win.vm[modelData.prop]; colorDialog.open() }
                                }
                            }
                        }
                    }
                }
                RowLayout {
                    Label { text: qsTr("Messages fade after") }
                    SpinBox { from: 1; to: 10; editable: true; value: win.vm.messageSeconds; onValueModified: win.vm.messageSeconds = value }
                    Label { text: qsTr("seconds") }
                }
                CheckBox { text: qsTr("Stack messages"); checked: win.vm.stackMessages; onToggled: win.vm.stackMessages = checked }
                Item { Layout.fillHeight: true }
            }
        }
        RowLayout {
            Button { text: qsTr("Default"); onClicked: win.vm.run("resetDefaults") }
            Label { text: win.vm.error; color: "#b00020"; Layout.fillWidth: true; elide: Text.ElideRight }
            Button { text: qsTr("Close"); onClicked: win.close() }
        }
    }
    ColorDialog {
        id: colorDialog
        property string which: ""
        options: ColorDialog.ShowAlphaChannel
        onAccepted: {
            const c = selectedColor
            const hex = (v) => ("0" + Math.round(v * 255).toString(16)).slice(-2)
            win.vm.run("setColor", [colorDialog.which, "#" + hex(c.a) + hex(c.r) + hex(c.g) + hex(c.b)])
        }
    }
}
