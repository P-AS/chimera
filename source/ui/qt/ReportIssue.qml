// Chimera's Report an Issue (Help), laid out as its form, a dialog of the
// main window: 1. the report form's address with Open in Browser and Copy
// Link; 2. the report's first lines, filled in from this session, with
// Copy Text; 3. the files worth attaching, each with a box ticked (the game
// as it is now first, while a game runs), Save as Zip... and what it last
// said, and Close (Enter or Escape). Nothing is sent from here. Layout and
// bindings only, over the help view model.
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
    // the main window's Paths folders
    property var folderOf: null
    property bool gameRunning: false
    property Window mainWindow: null
    title: qsTr("Report an Issue")
    width: 680
    height: 560
    minimumWidth: 520
    minimumHeight: 460
    modality: Qt.ApplicationModal
    transientParent: win.mainWindow
    flags: Qt.Dialog | Qt.WindowTitleHint | Qt.WindowSystemMenuHint | Qt.WindowMaximizeButtonHint | Qt.WindowCloseButtonHint

    readonly property string pictureRow: qsTr("A picture of the game as it is now (game.png)")
    // the game as it was when the window opened, "" for none
    property string picturePath: ""
    // the rows not ticked, by their text
    property var unticked: ({})
    readonly property var rows: (win.picturePath !== "" ? [win.pictureRow] : []).concat(win.vm.reportFiles.split("\n").filter(s => s !== ""))
    onVisibleChanged: if (visible) {
        win.unticked = {}
        win.picturePath = win.gameRunning ? win.pictureFiles.savePng(win.pictureFiles.temporaryFile("chimera-report-game.png")) : ""
        win.vm.run("makeReport")
    }
    function saveZip(url) {
        const files = win.rows.filter(r => r !== win.pictureRow && !win.unticked[r])
        const picture = win.picturePath !== "" && !win.unticked[win.pictureRow] ? win.picturePath : ""
        win.vm.run("saveReport", [url, picture, files.join("\n")])
    }
    // the name Chimera suggests: chimera-report-yyyy-MM-dd-HHmm.zip
    function zipName() { return "chimera-report-" + Qt.formatDateTime(new Date(), "yyyy-MM-dd-HHmm") + ".zip" }

    Shortcut { sequences: ["Escape", "Return", "Enter"]; onActivated: win.close() }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 4
        Label { text: qsTr("1. Open the report form. It asks you to sign in to GitHub."); Layout.fillWidth: true; elide: Text.ElideRight }
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            TextField { text: win.vm.formUrl; readOnly: true; activeFocusOnTab: false; Layout.fillWidth: true }
            Button { text: qsTr("Open in Browser"); Layout.preferredWidth: 120; onClicked: Qt.openUrlExternally(win.vm.formUrl) }
            Button {
                text: qsTr("Copy Link")
                Layout.preferredWidth: 120
                onClicked: { win.clipboard.setText(win.vm.formUrl); win.vm.status = qsTr("The link is on the clipboard.") }
            }
        }
        Label {
            text: qsTr("2. Choose \"Bug report\" there and paste this over the first lines of the form.")
            Layout.fillWidth: true
            Layout.topMargin: 10
            elide: Text.ElideRight
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            TextArea { objectName: "reportText"; text: win.vm.reportText; readOnly: true; activeFocusOnTab: false; wrapMode: TextEdit.Wrap }
        }
        Button {
            text: qsTr("Copy Text")
            Layout.preferredWidth: 120
            Layout.topMargin: 2
            onClicked: { win.clipboard.setText(win.vm.reportText); win.vm.status = qsTr("The text is on the clipboard.") }
        }
        Label {
            text: qsTr("3. Tick what belongs to the problem. Save it as one zip and attach that, or attach the files one by one.")
            Layout.fillWidth: true
            Layout.topMargin: 4
            elide: Text.ElideRight
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 100
            color: palette.base
            border.color: palette.mid
            ListView {
                id: files
                objectName: "reportFiles"
                anchors.fill: parent
                anchors.margins: 1
                clip: true
                model: win.rows
                ScrollBar.vertical: ScrollBar {}
                delegate: CheckBox {
                    required property string modelData
                    text: modelData
                    width: ListView.view.width
                    padding: 1
                    checked: !win.unticked[modelData]
                    onClicked: { const now = Object.assign({}, win.unticked); if (checked) delete now[modelData]; else now[modelData] = true; win.unticked = now }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 6
            spacing: 10
            Button { text: qsTr("Save as Zip..."); Layout.preferredWidth: 120; onClicked: { zipDialog.selectedFile = zipDialog.currentFolder + "/" + win.zipName(); zipDialog.open() } }
            Label { objectName: "reportStatus"; text: win.vm.status; Layout.fillWidth: true; elide: Text.ElideRight }
            Button { text: qsTr("Close"); Layout.preferredWidth: 90; onClicked: win.close() }
        }
    }
    FileDialog {
        id: zipDialog
        title: qsTr("Save the report's files as one zip")
        currentFolder: win.folderOf ? win.folderOf("Bug reports") : ""
        fileMode: FileDialog.SaveFile
        defaultSuffix: "zip"
        nameFilters: [qsTr("Zip archive (*.zip)")]
        onAccepted: win.saveZip(selectedFile)
    }
}
