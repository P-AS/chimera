// Chimera's Report an Issue: layout and bindings only, over the help view
// model. The report's first lines are filled in from this session; the
// person copies them into the form in their browser, and saves the files
// worth attaching in one zip. Nothing is sent from here.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: win
    objectName: "reportIssueWindow"
    required property QtObject vm
    required property QtObject clipboard
    required property QtObject pictureFiles
    title: qsTr("Report an Issue")
    width: 640
    height: 560
    onVisibleChanged: if (visible) win.vm.run("makeReport")

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        Label {
            text: qsTr("Copy this into the issue form, then say what happened, what you expected, and how to make it happen again.")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            TextArea { text: win.vm.reportText; readOnly: true; wrapMode: TextEdit.Wrap; font.family: "monospace" }
        }
        RowLayout {
            Button { text: qsTr("&Copy the Text"); onClicked: { win.clipboard.setText(win.vm.reportText); win.vm.status = "" } }
            Button { text: qsTr("&Open the Form in the Browser"); onClicked: Qt.openUrlExternally(win.vm.formUrl) }
        }
        Label { text: qsTr("Files worth attaching:"); visible: win.vm.reportFiles !== "" }
        Label {
            text: win.vm.reportFiles
            visible: text !== ""
            font.family: "monospace"
            elide: Text.ElideMiddle
            Layout.fillWidth: true
        }
        RowLayout {
            Button {
                text: qsTr("&Save Everything as a Zip...")
                onClicked: { zipDialog.selectedFile = "file:///report.zip"; zipDialog.open() }
            }
            Label { text: win.vm.error !== "" ? win.vm.error : win.vm.status; Layout.fillWidth: true; elide: Text.ElideRight }
            Button { text: qsTr("Close"); onClicked: win.close() }
        }
    }
    FileDialog {
        id: zipDialog
        title: qsTr("Save the report and its files")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Zip Archives (*.zip)")]
        onAccepted: {
            // the game as it is now goes in too
            const picture = win.pictureFiles.savePng(win.pictureFiles.temporaryFile("chimera-report-game.png"))
            win.vm.run("saveReport", [selectedFile, picture])
        }
    }
}
