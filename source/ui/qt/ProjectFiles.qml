// Chimera's project resolution window: layout and bindings only, over the
// session's waiting project. Shown while a project's files were not found
// beside it nor where this machine last had them.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: win
    objectName: "projectFilesWindow"
    required property QtObject session
    title: qsTr("Find the Project's Files")
    width: 600
    height: 380
    visible: win.session.resolvingFiles
    property int picked: -1
    property bool anyMissing: false
    property bool anyMismatch: false
    function recount() {
        let missing = false, mismatch = false
        for (let i = 0; i < files.count; i++) {
            const row = files.itemAtIndex(i)
            if (!row) continue
            if (row.status === 1) missing = true
            if (row.status === 2) mismatch = true
        }
        win.anyMissing = missing
        win.anyMismatch = mismatch
    }
    // closing it is Cancel
    onClosing: if (win.session.resolvingFiles) win.session.run("cancelOpen")

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        Label { text: qsTr("The project names these files. Locate any that were not found beside it:") }
        ListView {
            id: files
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: win.session.rows("projectFiles")
            onCountChanged: Qt.callLater(win.recount)
            delegate: ItemDelegate {
                id: row
                required property int index
                required property string name
                required property string slot
                required property int status
                required property string statusText
                required property string sha1
                required property string actualSha1
                width: files.width
                highlighted: row.index === win.picked
                onClicked: win.picked = row.index
                onDoubleClicked: { win.picked = row.index; locateDialog.open() }
                onStatusChanged: Qt.callLater(win.recount)
                contentItem: RowLayout {
                    Label { text: row.name; Layout.preferredWidth: 220; elide: Text.ElideMiddle }
                    Label { text: row.slot; Layout.preferredWidth: 80 }
                    Label { text: row.statusText; color: row.status === 2 ? "#b00020" : row.status === 1 ? "#a05a00" : palette.text; Layout.fillWidth: true }
                }
            }
        }
        Label {
            Layout.fillWidth: true
            elide: Text.ElideRight
            text: {
                const row = win.picked >= 0 ? files.itemAtIndex(win.picked) : null
                if (!row) return ""
                if (row.status === 2) return qsTr("recorded %1, provided %2").arg(row.sha1).arg(row.actualSha1)
                if (row.status === 0) return qsTr("SHA1 %1").arg(row.sha1)
                return qsTr("double-click, or press Locate, to point at this file")
            }
        }
        RowLayout {
            Button { text: qsTr("Locate..."); enabled: win.picked >= 0; onClicked: locateDialog.open() }
            Button { text: qsTr("Scan Folder..."); onClicked: scanDialog.open() }
            CheckBox { id: subfolders; text: qsTr("Including subfolders"); checked: true }
            Item { Layout.fillWidth: true }
            Label { text: win.session.error; color: "#b00020"; visible: text !== ""; elide: Text.ElideRight }
            // a missing file cannot be passed over; one that is not the recorded bytes can, knowingly
            Button {
                text: win.anyMismatch ? qsTr("Open Anyway") : qsTr("Open")
                enabled: !win.anyMissing
                onClicked: win.session.run("openWithFiles")
            }
            Button { text: qsTr("Cancel"); onClicked: win.session.run("cancelOpen") }
        }
    }
    FileDialog {
        id: locateDialog
        title: win.picked >= 0 && files.itemAtIndex(win.picked) ? qsTr("Locate %1").arg(files.itemAtIndex(win.picked).name) : qsTr("Locate")
        onAccepted: win.session.run("locateProjectFile", [win.picked, selectedFile])
    }
    FolderDialog {
        id: scanDialog
        title: qsTr("Scan a folder for the project's files")
        onAccepted: win.session.run("scanForProjectFiles", [selectedFolder, subfolders.checked])
    }
}
