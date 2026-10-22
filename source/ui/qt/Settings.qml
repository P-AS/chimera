// A machine's settings, one row each, as its package declares them: the
// control follows the declared type. Layout and bindings only, over the
// settings view model.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "settingsWindow"
    required property QtObject settings
    title: qsTr("Settings")
    width: 620
    height: 600

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        Label { text: win.settings.status }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 10
            model: win.settings.rows("settings")
            ScrollBar.vertical: ScrollBar {}
            delegate: ColumnLayout {
                id: row
                required property string name
                required property string label
                required property string type
                required property var value
                required property string options
                required property string optionLabels
                required property var min
                required property var max
                required property string description
                required property bool edited
                width: list.width - 16
                spacing: 2

                RowLayout {
                    Label {
                        text: row.label + (row.edited ? " *" : "")
                        font.bold: true
                        Layout.preferredWidth: 220
                        elide: Text.ElideRight
                    }
                    CheckBox {
                        visible: row.type === "bool"
                        checked: row.value === true
                        onToggled: win.settings.run("set", [row.name, checked])
                    }
                    ComboBox {
                        visible: row.type === "enum"
                        model: row.optionLabels.split("\n")
                        currentIndex: row.options.split("\n").indexOf(String(row.value))
                        onActivated: (i) => win.settings.run("set", [row.name, row.options.split("\n")[i]])
                        Layout.preferredWidth: 260
                    }
                    SpinBox {
                        visible: row.type === "int"
                        editable: true
                        from: row.min === undefined ? -2147483647 : row.min
                        to: row.max === undefined ? 2147483647 : row.max
                        value: row.type === "int" ? row.value : 0
                        onValueModified: win.settings.run("set", [row.name, value])
                    }
                    TextField {
                        visible: row.type === "string" || row.type === "float"
                        text: row.value === undefined ? "" : String(row.value)
                        onEditingFinished: win.settings.run("set", [row.name, row.type === "float" ? Number(text) : text])
                        Layout.preferredWidth: 260
                    }
                }
                Label {
                    text: row.description
                    wrapMode: Text.Wrap
                    opacity: 0.75
                    font.pointSize: 9
                    Layout.fillWidth: true
                }
            }
        }

        Label { text: win.settings.error; color: "#b00020"; visible: text !== "" }
        RowLayout {
            Item { Layout.fillWidth: true }
            Button { text: qsTr("Discard"); enabled: win.settings.discardEnabled; onClicked: win.settings.run("discard") }
            Button { text: qsTr("Apply"); enabled: win.settings.applyEnabled; onClicked: win.settings.run("apply") }
        }
    }
}
