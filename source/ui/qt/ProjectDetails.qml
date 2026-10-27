// The open project's details, as Chimera's TAStudio edits them from its
// Metadata menu, each in a window of its own: Header Info (part "header"),
// Edit Comments ("comments") and Edit Subtitles ("subtitles"), from one
// file. Layout and bindings only, over the projectDetails view model.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: win.part === "header" ? "headerInfoWindow" : win.part === "comments" ? "editCommentsWindow" : "editSubtitlesWindow"
    property string part: "header"
    required property QtObject vm
    title: win.part === "header" ? qsTr("Header Info") : win.part === "comments" ? qsTr("Edit Comments")
         : qsTr("Edit Subtitles (%1)").arg(win.vm.count)
    width: 640
    height: 480
    onVisibleChanged: if (visible) win.vm.run("reload")

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8

        StackLayout {
            currentIndex: win.part === "header" ? 0 : win.part === "comments" ? 1 : 2
            Layout.fillWidth: true
            Layout.fillHeight: true

            GridLayout {
                columns: 2
                Label { text: qsTr("Author") }
                RowLayout {
                    TextField { id: author; text: win.vm.author; Layout.fillWidth: true }
                    Button { text: qsTr("Default"); onClicked: author.text = win.vm.defaultAuthor }
                }
                Label { text: qsTr("Emulator version") }
                TextField { id: emulatorVersion; text: win.vm.emulatorVersion; Layout.fillWidth: true }
                Label { text: qsTr("Core") }
                Label { text: win.vm.core; Layout.fillWidth: true }
                Label { text: qsTr("Board name") }
                TextField { id: boardName; text: win.vm.boardName; Layout.fillWidth: true }
                Label { text: qsTr("Game name") }
                TextField { id: gameName; text: win.vm.gameName; Layout.fillWidth: true }
                Item { width: 1 }
                CheckBox { id: makeDefault; text: qsTr("Make this the author of new projects") }
                Item { Layout.fillHeight: true; width: 1 }
                Button {
                    text: qsTr("Keep")
                    Layout.alignment: Qt.AlignRight | Qt.AlignBottom
                    onClicked: win.vm.run("setHeader", [author.text, emulatorVersion.text, boardName.text, gameName.text, makeDefault.checked])
                }
            }

            ColumnLayout {
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    TextArea { id: comments; text: win.vm.comments; wrapMode: TextEdit.NoWrap }
                }
                Button { text: qsTr("Keep"); Layout.alignment: Qt.AlignRight; onClicked: win.vm.run("setComments", [comments.text]) }
            }

            ColumnLayout {
                ListView {
                    id: subtitleList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: win.vm.rows("subtitles")
                    currentIndex: -1
                    ScrollBar.vertical: ScrollBar {}
                    delegate: ItemDelegate {
                        id: srow
                        required property int index
                        required property int frame
                        required property int fromLeft
                        required property int fromTop
                        required property int duration
                        required property string color
                        required property string message
                        width: subtitleList.width - 16
                        highlighted: ListView.isCurrentItem
                        text: qsTr("%1 for %2 at %3,%4  %5  %6").arg(srow.frame).arg(srow.duration).arg(srow.fromLeft).arg(srow.fromTop).arg(srow.color).arg(srow.message)
                        onClicked: {
                            subtitleList.currentIndex = srow.index
                            subFrame.value = srow.frame
                            subX.value = srow.fromLeft
                            subY.value = srow.fromTop
                            subDuration.value = srow.duration
                            subColor.text = srow.color
                            subMessage.text = srow.message
                        }
                    }
                }
                GridLayout {
                    columns: 6
                    Label { text: qsTr("Frame") }
                    SpinBox { id: subFrame; from: 0; to: 99999999; editable: true }
                    Label { text: qsTr("For") }
                    SpinBox { id: subDuration; from: 1; to: 99999999; value: 120; editable: true }
                    Label { text: qsTr("Colour") }
                    TextField { id: subColor; text: "#FFFFFFFF" }
                    Label { text: qsTr("X") }
                    SpinBox { id: subX; from: 0; to: 9999; editable: true }
                    Label { text: qsTr("Y") }
                    SpinBox { id: subY; from: 0; to: 9999; editable: true }
                    Label { text: qsTr("Text") }
                    TextField { id: subMessage; Layout.fillWidth: true }
                }
                RowLayout {
                    Label { text: win.vm.error !== "" ? win.vm.error : win.vm.status; Layout.fillWidth: true; elide: Text.ElideRight }
                    Button {
                        text: qsTr("Add")
                        onClicked: win.vm.run("addSubtitle", [subFrame.value, subX.value, subY.value, subDuration.value, subColor.text, subMessage.text])
                    }
                    Button {
                        text: qsTr("Change")
                        enabled: subtitleList.currentIndex >= 0
                        onClicked: win.vm.run("editSubtitle", [subtitleList.currentIndex, subFrame.value, subX.value, subY.value, subDuration.value, subColor.text, subMessage.text])
                    }
                    Button {
                        text: qsTr("Remove")
                        enabled: subtitleList.currentIndex >= 0
                        onClicked: { win.vm.run("removeSubtitle", [subtitleList.currentIndex]); subtitleList.currentIndex = -1 }
                    }
                }
            }
        }
    }
}
