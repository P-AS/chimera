// A package's settings, one row each, as it declares them: the control
// follows the declared type. Layout and bindings only, over any view model
// with "settings" rows and a "set" command (Settings, New Project).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ListView {
    id: list
    required property QtObject vm
    // the presets row at the top; a window that places its own leaves it out
    property bool presetsHere: true
    clip: true
    spacing: 10
    model: list.vm.rows("settings")
    ScrollBar.vertical: ScrollBar {}
    // the presets the machine is offered: Apply writes one's values below
    header: RowLayout {
        width: list.width - 16
        visible: list.presetsHere && presetBox.count > 0
        height: visible ? implicitHeight + 8 : 0
        Label { text: qsTrId("common.preset") }
        ComboBox {
            id: presetBox
            model: list.vm.rows("presets")
            textRole: "label"
            valueRole: "id"
            Layout.fillWidth: true
            // the list is given again after every edit: the choice stays
            property string chosen: ""
            onActivated: chosen = currentValue
            Connections {
                target: presetBox.model
                function onModelReset() { Qt.callLater(presetBox.keep) }
            }
            function keep() {
                const at = chosen === "" ? -1 : indexOfValue(chosen)
                currentIndex = at >= 0 ? at : (count > 0 ? 0 : -1)
            }
            Component.onCompleted: keep()
        }
        Button { text: qsTrId("common.apply"); enabled: presetBox.currentIndex >= 0; onClicked: list.vm.run("applyPreset", [presetBox.currentValue]) }
    }
    // when the machine's clock starts: the project's, for every core, so the last of the
    // settings and scrolled with them like any other (owner, 2026-10-27), where the view
    // model has one (New Project, Project Settings)
    footer: ColumnLayout {
        width: list.width - 16
        visible: list.vm.clockStart !== undefined
        height: visible ? implicitHeight : 0
        spacing: 2
        RowLayout {
            Layout.topMargin: list.count > 0 ? list.spacing : 0
            Label {
                text: qsTrId("settings_list.the_machine_s_clock_starts_at")
                font.bold: true
                Layout.preferredWidth: 220
                elide: Text.ElideRight
            }
            TextField {
                objectName: "clockStart"
                text: list.vm.clockStart === undefined ? "" : list.vm.clockStart
                placeholderText: "2000-01-01 00:00:00"
                onTextEdited: list.vm.clockStart = text
                Layout.preferredWidth: 260
            }
            Label { text: qsTrId("settings_list.utc") }
        }
        Label {
            text: qsTrId("settings_list.every_run_of_this_project_starts")
            wrapMode: Text.Wrap
            opacity: 0.75
            font.pointSize: 9
            Layout.fillWidth: true
        }
    }
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
            // Chimera's uint, long and ulong as their digits (the window's numbers would lose a
            // ulong's last ones), and a date and time as it is shown, in UTC (issue 358)
            TextField {
                visible: row.type === "uint" || row.type === "long" || row.type === "ulong" || row.type === "datetime"
                text: row.value === undefined ? "" : String(row.value)
                placeholderText: row.type === "datetime" ? "2000-01-01 00:00:00" : ""
                validator: RegularExpressionValidator {
                    regularExpression: row.type === "datetime" ? /[0-9: -]*/ : row.type === "long" ? /-?[0-9]*/ : /[0-9]*/
                }
                onEditingFinished: list.vm.run("set", [row.name, text])
                Layout.preferredWidth: 260
            }
            Label { visible: row.type === "datetime"; text: qsTrId("settings_list.utc") }
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
