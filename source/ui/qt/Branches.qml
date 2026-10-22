// The movie's branches: layout and bindings only, over the branches view
// model.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "branchesWindow"
    required property QtObject vm
    title: qsTr("Branches")
    width: 560
    height: 420

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8

        RowLayout {
            TextField { id: name; placeholderText: qsTr("Name (Branch %1 if empty)").arg(win.vm.count + 1); Layout.fillWidth: true }
            Button {
                text: qsTr("Make a Branch Here")
                enabled: win.vm.createEnabled
                onClicked: { win.vm.run("create", [name.text]); name.text = "" }
            }
        }
        Label { text: win.vm.status }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: win.vm.rows("branches")
            ScrollBar.vertical: ScrollBar {}
            delegate: Frame {
                id: row
                required property int index
                required property string name
                required property var frame
                required property string time
                required property var frames
                width: list.width - 12
                RowLayout {
                    anchors.fill: parent
                    ColumnLayout {
                        Layout.fillWidth: true
                        Label { text: row.name; font.bold: true }
                        Label {
                            text: qsTr("frame %1 of %2, made %3").arg(row.frame).arg(row.frames).arg(row.time)
                            opacity: 0.75
                        }
                    }
                    Button { text: qsTr("Load"); onClicked: win.vm.run("load", [row.index]) }
                    Button { text: qsTr("Rename"); enabled: name.text !== ""; onClicked: win.vm.run("rename", [row.index, name.text]) }
                    Button { text: qsTr("Remove"); onClicked: win.vm.run("remove", [row.index]) }
                }
            }
        }
        Label { text: win.vm.error; color: "#b00020"; visible: text !== "" }
    }
}
