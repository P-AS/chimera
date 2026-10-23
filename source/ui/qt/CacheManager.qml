// Chimera's Cache Manager: layout and bindings only, over the cacheManager
// view model. A project's cache can be made again: removing it costs
// waiting (the run replays), never work. Unsaved work a crash left is work.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "cacheManagerWindow"
    required property QtObject vm
    title: qsTr("Cache Manager")
    width: 820
    height: 520
    // Chimera's Greenzone budgets button: the window is the main window's
    signal budgetsAsked()
    property var ticked: ({})
    property int picked: -1
    onVisibleChanged: if (visible) { win.vm.run("refresh"); win.ticked = ({}) }

    function tickedList() { return Object.keys(win.ticked).filter(k => win.ticked[k]).join(",") }
    function rowTitles(list) { return list === "" ? "" : win.vm.run("titles", [list]) }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        Label {
            text: qsTr("A project's cache can be made again: removing it costs waiting - its run replays instead of resuming - never work. Unsaved work a crash left is work, and starts locked.")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Label { text: win.vm.total; font.bold: true }
        Label { text: win.vm.overLimit; visible: text !== ""; wrapMode: Text.WordWrap; Layout.fillWidth: true; color: "#b00020" }
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
                required property string kind
                required property bool locked
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
                        Label { text: row.note; visible: text !== ""; color: row.orphan ? "#b00020" : palette.text; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    }
                    Label { text: row.kind; opacity: 0.75 }
                    Label { text: row.locked ? qsTr("Locked") : ""; Layout.preferredWidth: 56 }
                    Label { text: row.sizeText; Layout.preferredWidth: 72; horizontalAlignment: Text.AlignRight }
                }
            }
        }
        // Chimera's: the limit, kept at once; nothing is removed while it is typed
        RowLayout {
            CheckBox { text: qsTr("Keep the cache under"); checked: win.vm.autoClean; onToggled: win.vm.autoClean = checked }
            SpinBox { from: 1; to: 100000; editable: true; value: win.vm.limitGb; onValueModified: win.vm.limitGb = value }
            Label { text: qsTr("GB, and leave the disk at least") }
            SpinBox { from: 0; to: 100000; editable: true; value: win.vm.floorGb; onValueModified: win.vm.floorGb = value }
            Label { text: qsTr("GB free") }
        }
        RowLayout {
            Button { text: qsTr("Select Orphans"); onClicked: { const t = {}; for (const i of win.vm.run("orphans").split(",").filter(s => s !== "")) t[i] = true; win.ticked = t } }
            Button { text: qsTr("Remove Ticked"); enabled: win.tickedList() !== ""; onClicked: { removeDialog.rows = win.tickedList(); removeDialog.open() } }
            Button { text: qsTr("Lock / Unlock"); enabled: win.tickedList() !== ""; onClicked: { win.vm.run("toggleLock", [win.tickedList()]); win.ticked = ({}) } }
            Button { text: qsTr("Greenzone budgets..."); onClicked: win.budgetsAsked() }
            Button {
                text: qsTr("Clean Now")
                onClicked: {
                    const going = win.vm.run("wouldGo")
                    if (going === "") { win.vm.run("cleanNow"); return }
                    cleanDialog.rows = going
                    cleanDialog.open()
                }
            }
            Button {
                text: qsTr("Open Folder")
                enabled: win.picked >= 0
                onClicked: Qt.openUrlExternally("file:///" + list.itemAtIndex(win.picked).folder)
            }
            Item { Layout.fillWidth: true }
            Button { text: qsTr("Refresh"); onClicked: { win.vm.run("refresh"); win.ticked = ({}) } }
            Button { text: qsTr("Close"); onClicked: win.close() }
        }
        Label {
            text: win.vm.error !== "" ? win.vm.error : win.vm.status
            color: win.vm.error !== "" ? "#b00020" : palette.text
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    // a press names things: it asks first
    Dialog {
        id: removeDialog
        property string rows: ""
        title: qsTr("Remove these?")
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 560)
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        Label {
            width: removeDialog.availableWidth
            wrapMode: Text.Wrap
            text: qsTr("These go, locked or not:\n%1\n\nA project's cache is made again as its run replays; unsaved work is gone for good.").arg(win.rowTitles(removeDialog.rows))
        }
        onAccepted: { win.vm.run("remove", [removeDialog.rows]); win.ticked = ({}); win.picked = -1 }
    }
    Dialog {
        id: cleanDialog
        property string rows: ""
        title: qsTr("Clean Now?")
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 560)
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        Label {
            width: cleanDialog.availableWidth
            wrapMode: Text.Wrap
            text: qsTr("To come under the limit, the oldest unlocked items go:\n%1").arg(win.rowTitles(cleanDialog.rows))
        }
        onAccepted: { win.vm.run("cleanNow"); win.ticked = ({}); win.picked = -1 }
    }
}
