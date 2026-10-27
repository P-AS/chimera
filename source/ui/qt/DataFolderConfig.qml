// Chimera's Data Directory (Config > Data Directory), laid out as Chimera's
// The Data Directory window: what the directory holds, where it is (in bold), how
// much is in it and free on its drive, the change waiting for the next
// start, what was last said, and Change..., Use Default, Keep Current, Open
// Folder and Close. Picking a folder asks Chimera's questions: whether to
// use one that already holds data as it is, or whether to move what is
// here. Layout and bindings only, over the dataFolder view model.
import QtQuick
import QtQuick.Controls
import ChimeraDialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "dataFolderWindow"
    required property QtObject vm
    property var themeColor: (role, fallback) => fallback
    property Window mainWindow: null
    title: qsTr("Data Directory")
    width: 640
    height: 300
    modality: Qt.ApplicationModal
    transientParent: win.mainWindow
    onVisibleChanged: if (visible) { win.said = ""; win.vm.run("refresh") }

    // the last thing the window said (Chimera's status line)
    property string said: ""
    // the folder being asked about ("" for the default), and what it is
    property string asking: ""
    property var found: ({})
    function request(folder) {
        win.asking = folder
        const what = JSON.parse(win.vm.run("inspect", [folder]) || "{}")
        win.found = what
        if (what.choice === "same" || what.choice === "nested" || what.choice === "unwritable") {
            win.vm.run("request", [folder, false])
            win.said = win.vm.error !== "" ? win.vm.error : win.vm.status
            return
        }
        if (what.choice === "holds data") useAsIs.open()
        else moveAsk.open()
    }
    function recorded(move) {
        win.vm.run("request", [win.asking, move])
        win.said = win.vm.error !== "" ? win.vm.error : win.vm.status
        win.vm.run("refresh")
    }

    MessageDialog {
        id: useAsIs
        title: qsTr("Data Directory")
        text: qsTr("%1 already holds Chimera data.\n\nUse it as it is? What is in the current directory stays where it is, untouched.").arg(win.found.target || "")
        buttons: MessageDialog.Ok | MessageDialog.Cancel
        onAccepted: win.recorded(false)
    }
    MessageDialog {
        id: moveAsk
        title: qsTr("Data Directory")
        text: qsTr("Move the %1 already here to %2?").arg(win.found.size || "").arg(win.found.target || "")
            + (win.found.tight ? qsTr("\n\nThere may not be room: %1 to move, %2 free there. A move that does not fit is undone and nothing is lost.").arg(win.found.size).arg(win.found.free) : "")
            + qsTr("\n\nYes: it is moved when Chimera next starts.\nNo: start empty there. Greenzones and compiled code are made again as they are needed. What is here stays here.")
        buttons: MessageDialog.Yes | MessageDialog.No | MessageDialog.Cancel
        onButtonClicked: (button) => {
            if (button === MessageDialog.Yes) win.recorded(true)
            else if (button === MessageDialog.No) win.recorded(false)
        }
    }
    FolderDialog {
        id: folderDialog
        title: qsTr("Where Chimera should keep its data. A folder that already has other things in it gets a Chimera folder of its own inside.")
        onAccepted: win.request(localFiles.localPath(selectedFolder))
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 6
        Label {
            text: qsTr("Everything Chimera keeps for you is in one directory: each project's greenzone, unpacked cores, precompiled game code, recovery journals and crash notes. It can run to many gigabytes, and it does not have to be on the system drive.")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Label { text: (win.vm.current, win.vm.standard, win.vm.environment, win.vm.run("inUse") || ""); font.bold: true; elide: Text.ElideMiddle; Layout.fillWidth: true; Layout.topMargin: 6 }
        Label { text: win.vm.figures; Layout.fillWidth: true }
        Label {
            text: win.vm.environment !== "" ? qsTr("CHIMERA_DATA_HOME is set, and it decides: this is a portable install, or somebody meant it. Unset it to choose here.")
                : win.vm.pending === "" ? ""
                : qsTr("At the next start this becomes %1").arg(win.vm.pending) + (win.vm.pendingMove ? qsTr(", and everything here is moved there.") : qsTr(". What is here stays here."))
            color: win.themeColor("AccentWarning", "#b26a00")
            wrapMode: Text.WrapAnywhere
            Layout.fillWidth: true
            Layout.preferredHeight: 40
        }
        Label { text: win.said; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.preferredHeight: 56 }
        Item { Layout.fillHeight: true }
        RowLayout {
            Button { text: qsTr("Change..."); Layout.preferredWidth: 100; enabled: win.vm.environment === ""; onClicked: folderDialog.open() }
            Button {
                text: qsTr("Use Default"); Layout.preferredWidth: 100
                // only when the directory is, or is to become, one of the person's, and the environment does not decide
                enabled: win.vm.environment === "" && (win.vm.pending !== "" ? win.vm.pending : win.vm.current) !== ""
                onClicked: win.request("")
            }
            Button {
                text: qsTr("Keep Current"); Layout.preferredWidth: 110
                visible: win.vm.pending !== ""
                onClicked: { win.vm.run("withdraw"); win.said = win.vm.status }
            }
            Button {
                text: qsTr("Open Folder"); Layout.preferredWidth: 100
                onClicked: {
                    const at = win.vm.run("inUse")
                    if (!Qt.openUrlExternally(localFiles.folderUrl(at))) win.said = qsTr("Could not open %1").arg(at)
                }
            }
            Item { Layout.fillWidth: true }
            Button { text: qsTr("Close"); Layout.preferredWidth: 100; onClicked: win.close() }
        }
    }
}
