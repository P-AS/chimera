// Chimera's Pre-Compiled Modules window: layout and bindings only, over
// the precompiled view model.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "precompiledWindow"
    required property QtObject vm
    title: qsTr("Pre-Compiled Modules")
    width: 760
    height: 440
    property var ticked: ({})
    onVisibleChanged: if (visible) { win.vm.run("refresh"); win.ticked = ({}) }
    function tickedList() { return Object.keys(win.ticked).filter(k => win.ticked[k]).join(",") }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        Label {
            text: qsTr("The code a core translated for each game, kept so the next boot does not translate it again. Removing a game's costs the time to translate it again, never work.")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Label { text: win.vm.total; font.bold: true }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: win.vm.rows("games")
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                id: row
                required property int index
                required property string name
                required property string core
                required property string version
                required property string compiled
                required property int modules
                required property string sizeText
                required property string note
                width: list.width - 16
                contentItem: RowLayout {
                    CheckBox {
                        checked: win.ticked[row.index] === true
                        onToggled: { const t = Object.assign({}, win.ticked); t[row.index] = checked; win.ticked = t }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Label { text: row.name; font.bold: true; elide: Text.ElideMiddle; Layout.fillWidth: true }
                        Label {
                            text: [row.core !== "" ? row.core + (row.version !== "" ? " " + row.version.substring(0, 12) : "") : qsTr("not recorded"),
                                   row.compiled, row.modules > 0 ? qsTr("%1 modules").arg(row.modules) : ""].filter(t => t !== "").join("  -  ")
                            opacity: 0.75
                        }
                        Label { text: row.note; visible: text !== ""; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    }
                    Label { text: row.sizeText }
                }
            }
        }
        RowLayout {
            Button { text: qsTr("Remove Ticked"); enabled: win.tickedList() !== ""; onClicked: { win.vm.run("remove", [win.tickedList()]); win.ticked = ({}) } }
            Label { text: win.vm.error !== "" ? win.vm.error : win.vm.status; color: win.vm.error !== "" ? "#b00020" : palette.text; Layout.fillWidth: true; elide: Text.ElideRight }
            Button { text: qsTr("Refresh"); onClicked: { win.vm.run("refresh"); win.ticked = ({}) } }
            Button { text: qsTr("Close"); onClicked: win.close() }
        }
    }
}
