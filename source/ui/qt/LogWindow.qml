// The log, as Chimera's Log Window: layout and bindings only, over the list
// of every message the window has shown (Chimera's shows its console's).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "logWindow"
    required property ListModel log
    required property QtObject clipboard
    title: qsTr("Log Window")
    width: 640
    height: 400

    function lines(from, to) {
        let text = ""
        for (let i = from; i <= to; i++) text += win.log.get(i).time + " " + win.log.get(i).message + "\n"
        return text
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: win.log
            currentIndex: -1
            ScrollBar.vertical: ScrollBar {}
            onCountChanged: positionViewAtEnd()
            delegate: ItemDelegate {
                id: line
                required property int index
                required property string time
                required property string message
                width: list.width - 16
                highlighted: ListView.isCurrentItem
                contentItem: Label { text: line.time + "  " + line.message; font.family: "monospace"; elide: Text.ElideRight }
                onClicked: list.currentIndex = line.index
            }
        }
        RowLayout {
            Button { text: qsTr("&Clear"); onClicked: { win.log.clear(); list.currentIndex = -1 } }
            Button { text: qsTr("Copy Sel."); enabled: list.currentIndex >= 0; onClicked: win.clipboard.setText(win.lines(list.currentIndex, list.currentIndex)) }
            Button { text: qsTr("Copy All"); enabled: win.log.count > 0; onClicked: win.clipboard.setText(win.lines(0, win.log.count - 1)) }
            Item { Layout.fillWidth: true }
            Button { text: qsTr("Close"); onClicked: win.close() }
        }
    }
}
