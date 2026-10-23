// Chimera's Cache Manager: layout and bindings only, over the cacheManager
// view model. What is kept here can all be made again: removing a project's
// cache costs waiting (the run replays), never work.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "cacheManagerWindow"
    required property QtObject vm
    title: qsTr("Cache Manager")
    width: 760
    height: 460
    property var ticked: ({})
    property int picked: -1
    onVisibleChanged: if (visible) { win.vm.run("refresh"); win.ticked = ({}) }

    function tickedList() { return Object.keys(win.ticked).filter(k => win.ticked[k]).join(",") }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        Label {
            text: qsTr("Everything here can be made again. Removing a project's cache costs waiting - its run replays instead of resuming - never work.")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Label { text: win.vm.total; font.bold: true }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: win.vm.rows("caches")
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                id: row
                required property int index
                required property string title
                required property string project
                required property string core
                required property string system
                required property string sizeText
                required property bool orphan
                required property bool open
                required property string note
                required property string folder
                width: list.width - 16
                highlighted: row.index === win.picked
                onClicked: win.picked = row.index
                contentItem: RowLayout {
                    CheckBox {
                        checked: win.ticked[row.index] === true
                        onToggled: { const t = Object.assign({}, win.ticked); t[row.index] = checked; win.ticked = t }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Label { text: row.title + (row.core !== "" ? "  -  " + row.core : "") + (row.system !== "" ? " (" + row.system + ")" : ""); font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                        Label { text: row.project; visible: text !== ""; elide: Text.ElideMiddle; Layout.fillWidth: true; opacity: 0.75 }
                        Label { text: row.note; visible: text !== ""; color: row.orphan ? "#b00020" : palette.text; Layout.fillWidth: true }
                    }
                    Label { text: row.sizeText }
                }
            }
        }
        RowLayout {
            Button { text: qsTr("Select Orphans"); onClicked: { const t = {}; for (const i of win.vm.run("orphans").split(",").filter(s => s !== "")) t[i] = true; win.ticked = t } }
            Button { text: qsTr("Remove"); enabled: win.tickedList() !== ""; onClicked: { win.vm.run("remove", [win.tickedList()]); win.ticked = ({}); win.picked = -1 } }
            Button {
                text: qsTr("Open Folder")
                enabled: win.picked >= 0
                onClicked: Qt.openUrlExternally("file:///" + list.itemAtIndex(win.picked).folder)
            }
            Item { Layout.fillWidth: true }
            Label { text: win.vm.error; color: "#b00020"; visible: text !== "" }
            Button { text: qsTr("Refresh"); onClicked: { win.vm.run("refresh"); win.ticked = ({}) } }
            Button { text: qsTr("Close"); onClicked: win.close() }
        }
    }
}
