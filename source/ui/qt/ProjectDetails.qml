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
    title: win.part === "header" ? qsTrId("project_details.header_info") : win.part === "comments" ? qsTrId("project_details.edit_comments")
         : qsTrId("project_details.edit_subtitles").arg(win.vm.count)
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
                Label { text: qsTrId("project_details.author") }
                RowLayout {
                    TextField { id: author; text: win.vm.author; Layout.fillWidth: true }
                    Button { text: qsTrId("project_details.default"); onClicked: author.text = win.vm.defaultAuthor }
                }
                Label { text: qsTrId("project_details.emulator_version") }
                TextField { id: emulatorVersion; text: win.vm.emulatorVersion; Layout.fillWidth: true }
                Label { text: qsTrId("common.core") }
                Label { text: win.vm.core; Layout.fillWidth: true }
                Label { text: qsTrId("project_details.board_name") }
                TextField { id: boardName; text: win.vm.boardName; Layout.fillWidth: true }
                Label { text: qsTrId("project_details.game_name") }
                TextField { id: gameName; text: win.vm.gameName; Layout.fillWidth: true }
                Item { width: 1 }
                CheckBox { id: makeDefault; text: qsTrId("project_details.make_this_the_author_of_new") }
                Item { Layout.fillHeight: true; width: 1 }
                Button {
                    text: qsTrId("project_details.keep")
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
                Button { text: qsTrId("project_details.keep"); Layout.alignment: Qt.AlignRight; onClicked: win.vm.run("setComments", [comments.text]) }
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
                        text: qsTrId("project_details.for_at").arg(srow.frame).arg(srow.duration).arg(srow.fromLeft).arg(srow.fromTop).arg(srow.color).arg(srow.message)
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
                    Label { text: qsTrId("common.frame") }
                    SpinBox { id: subFrame; from: 0; to: 99999999; editable: true }
                    Label { text: qsTrId("project_details.for") }
                    SpinBox { id: subDuration; from: 1; to: 99999999; value: 120; editable: true }
                    Label { text: qsTrId("project_details.colour") }
                    TextField { id: subColor; text: "#FFFFFFFF" }
                    Label { text: qsTrId("project_details.x") }
                    SpinBox { id: subX; from: 0; to: 9999; editable: true }
                    Label { text: qsTrId("project_details.y") }
                    SpinBox { id: subY; from: 0; to: 9999; editable: true }
                    Label { text: qsTrId("project_details.text") }
                    TextField { id: subMessage; Layout.fillWidth: true }
                }
                RowLayout {
                    Label { text: win.vm.error !== "" ? win.vm.error : win.vm.status; Layout.fillWidth: true; elide: Text.ElideRight }
                    Button {
                        text: qsTrId("project_details.add")
                        onClicked: win.vm.run("addSubtitle", [subFrame.value, subX.value, subY.value, subDuration.value, subColor.text, subMessage.text])
                    }
                    Button {
                        text: qsTrId("project_details.change")
                        enabled: subtitleList.currentIndex >= 0
                        onClicked: win.vm.run("editSubtitle", [subtitleList.currentIndex, subFrame.value, subX.value, subY.value, subDuration.value, subColor.text, subMessage.text])
                    }
                    Button {
                        text: qsTrId("common.remove")
                        enabled: subtitleList.currentIndex >= 0
                        onClicked: { win.vm.run("removeSubtitle", [subtitleList.currentIndex]); subtitleList.currentIndex = -1 }
                    }
                }
            }
        }
    }
}
