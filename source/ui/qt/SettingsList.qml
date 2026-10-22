// A package's settings, one row each, as it declares them: the control
// follows the declared type. Layout and bindings only, over any view model
// with "settings" rows and a "set" command (Settings, New Project).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ListView {
    id: list
    required property QtObject vm
    clip: true
    spacing: 10
    model: list.vm.rows("settings")
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
                onToggled: list.vm.run("set", [row.name, checked])
            }
            ComboBox {
                visible: row.type === "enum"
                model: row.optionLabels.split("\n")
                currentIndex: row.options.split("\n").indexOf(String(row.value))
                onActivated: (i) => list.vm.run("set", [row.name, row.options.split("\n")[i]])
                Layout.preferredWidth: 260
            }
            SpinBox {
                visible: row.type === "int"
                editable: true
                from: row.min === undefined ? -2147483647 : row.min
                to: row.max === undefined ? 2147483647 : row.max
                value: row.type === "int" ? row.value : 0
                onValueModified: list.vm.run("set", [row.name, value])
            }
            TextField {
                visible: row.type === "string" || row.type === "float"
                text: row.value === undefined ? "" : String(row.value)
                onEditingFinished: list.vm.run("set", [row.name, row.type === "float" ? Number(text) : text])
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
