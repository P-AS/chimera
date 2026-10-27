// Chimera's Core Manager (Tools > Core Manager...), laid out as its
// CoreManagerForm: the folder and how a core arrives at the top, Select all
// and Show over the list, a row a core with a box to tick (Core, Type,
// Systems, Version, Size), the versions of the core picked on the right
// (Remove version, where it is, when it was built, its terms), the line
// under the list, and Open cores folder, Change folder..., Refresh List,
// Remove, Show Systems... and Close. Chimera downloads nothing: a core is a
// .chimeraCore file someone put in the folder. Layout and bindings only,
// over the cores view model.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "coresWindow"
    required property QtObject vm
    title: qsTr("Core Manager")
    width: 1100
    height: 500
    minimumWidth: 900
    minimumHeight: 420

    readonly property var widths: [150, 70, 200, 250, 70]
    // the cores ticked, by name, and the one picked
    property var ticked: ({})
    readonly property int tickedCount: Object.keys(win.ticked).length
    property string picked: ""
    readonly property var versions: win.picked === "" ? [] : JSON.parse(win.vm.run("versionsOf", [win.picked]) || "[]")
    onVisibleChanged: if (visible) { win.vm.run("refresh"); win.ticked = {}; win.picked = ""; Qt.callLater(win.pickFirst) }
    // the first core listed is picked when none is
    function pickFirst() {
        if (win.picked === "" && list.count > 0 && list.itemAtIndex(0)) win.picked = list.itemAtIndex(0).core
    }
    function tick(name, on) {
        const now = Object.assign({}, win.ticked)
        if (on) now[name] = true; else delete now[name]
        win.ticked = now
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("The cores folder")
        currentFolder: localFiles.folderUrl(win.vm.coresFolder)
        onAccepted: {
            win.vm.coresFolder = localFiles.localPath(selectedFolder)
            win.vm.managerStatus = qsTr("The cores folder is now %1: %2 core(s). A core already loaded stays loaded until Chimera is restarted.").arg(win.vm.coresFolder).arg(list.count)
            win.picked = ""
            Qt.callLater(win.pickFirst)
        }
    }
    MessageDialog {
        id: removeCores
        title: qsTr("Remove cores")
        property int versions: 0
        text: qsTr("Delete %1 package file(s) of %2 core(s) from the cores folder?\n\nA movie recorded on one of these exact builds needs it to replay, and Chimera cannot fetch it again.").arg(removeCores.versions).arg(win.tickedCount)
        buttons: MessageDialog.Ok | MessageDialog.Cancel
        onAccepted: { win.vm.run("removeCores", [JSON.stringify(Object.keys(win.ticked))]); win.ticked = {}; win.picked = ""; Qt.callLater(win.pickFirst) }
    }
    MessageDialog {
        id: removeVersion
        title: qsTr("Remove core")
        property string path: ""
        text: qsTr("Delete %1?\n\nA movie recorded on this exact build needs it to replay, and Chimera cannot fetch it again.").arg(removeVersion.path.replace(/^.*[\\/]/, ""))
        buttons: MessageDialog.Ok | MessageDialog.Cancel
        onAccepted: win.vm.run("removeVersion", [removeVersion.path])
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6
        Label { text: win.vm.managerHeader; Layout.fillWidth: true; elide: Text.ElideRight }
        RowLayout {
            Layout.fillWidth: true
            CheckBox {
                text: win.tickedCount > 0 ? qsTr("Select all (%1 ticked)").arg(win.tickedCount) : qsTr("Select all")
                checked: list.count > 0 && win.tickedCount === list.count
                onClicked: {
                    const now = {}
                    if (checked) for (let i = 0; i < list.count; i++) { const it = list.itemAtIndex(i); if (it) now[it.core] = true }
                    win.ticked = now
                }
                padding: 2
            }
            Item { Layout.fillWidth: true }
            Label { text: qsTr("Show:") }
            RadioButton { text: qsTr("All"); checked: win.vm.shows === 0; padding: 2; onClicked: { win.vm.shows = 0; win.ticked = {} } }
            RadioButton { text: qsTr("Emulators"); checked: win.vm.shows === 1; padding: 2; onClicked: { win.vm.shows = 1; win.ticked = {} } }
            RadioButton { text: qsTr("Games"); checked: win.vm.shows === 2; padding: 2; onClicked: { win.vm.shows = 2; win.ticked = {} } }
            Item { Layout.preferredWidth: 328 }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: palette.base
                border.color: palette.mid
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 1
                    spacing: 0
                    Row {
                        Repeater {
                            model: [qsTr("Core"), qsTr("Type"), qsTr("Systems"), qsTr("Version"), qsTr("Size")]
                            delegate: Label {
                                required property int index
                                required property string modelData
                                text: modelData
                                width: win.widths[index] + (index === 0 ? 22 : 0)
                                leftPadding: 4; topPadding: 2; bottomPadding: 2
                                horizontalAlignment: index === 4 ? Text.AlignRight : Text.AlignLeft
                                rightPadding: index === 4 ? 4 : 0
                                background: Rectangle { color: palette.button; border.color: palette.mid }
                            }
                        }
                    }
                    ListView {
                        id: list
                        objectName: "coreList"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: win.vm.rows("manager")
                        ScrollBar.vertical: ScrollBar {}
                        onCountChanged: Qt.callLater(win.pickFirst)
                        delegate: Rectangle {
                            id: row
                            required property int index
                            required property string core
                            required property string type
                            required property string systems
                            required property string version
                            required property string size
                            width: list.width
                            height: 22
                            readonly property bool isPicked: row.core === win.picked
                            color: isPicked ? palette.highlight : "transparent"
                            readonly property color ink: isPicked ? palette.highlightedText : palette.text
                            Row {
                                anchors.verticalCenter: parent.verticalCenter
                                CheckBox {
                                    checked: win.ticked[row.core] === true
                                    onClicked: win.tick(row.core, checked)
                                    padding: 0
                                    width: 22
                                }
                                Label { text: row.core; color: row.ink; width: win.widths[0]; elide: Text.ElideRight; leftPadding: 2 }
                                Label { text: row.type; color: row.ink; width: win.widths[1]; leftPadding: 4 }
                                Label { text: row.systems; color: row.ink; width: win.widths[2]; elide: Text.ElideRight; leftPadding: 4 }
                                Label { text: row.version; color: row.ink; width: win.widths[3]; elide: Text.ElideRight; leftPadding: 4 }
                                Label { text: row.size; color: row.ink; width: win.widths[4]; horizontalAlignment: Text.AlignRight; rightPadding: 4 }
                            }
                            TapHandler { onTapped: win.picked = row.core }
                        }
                    }
                }
            }
            // the versions of the core picked
            ColumnLayout {
                Layout.preferredWidth: 320
                Layout.maximumWidth: 320
                Layout.fillHeight: true
                Label { text: qsTr("Version") }
                ComboBox {
                    id: versionBox
                    Layout.fillWidth: true
                    model: win.versions.map(v => v.label)
                    onModelChanged: currentIndex = 0
                }
                Button {
                    text: qsTr("Remove version")
                    enabled: versionBox.currentIndex >= 0 && versionBox.currentIndex < win.versions.length
                    Layout.preferredWidth: 155
                    onClicked: { removeVersion.path = win.versions[versionBox.currentIndex].path; removeVersion.open() }
                }
                Label {
                    text: versionBox.currentIndex >= 0 && versionBox.currentIndex < win.versions.length
                        ? (win.vm.run("versionDetail", [win.versions[versionBox.currentIndex].path]) || "") : ""
                    wrapMode: Text.Wrap
                    clip: true
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredHeight: 10
                    Layout.topMargin: 8
                    verticalAlignment: Text.AlignTop
                }
            }
        }
        Label { text: win.vm.managerStatus; wrapMode: Text.Wrap; Layout.fillWidth: true; Layout.preferredHeight: 32 }
        RowLayout {
            Layout.fillWidth: true
            Button {
                text: qsTr("Open cores folder"); Layout.preferredWidth: 140
                onClicked: if (!Qt.openUrlExternally(localFiles.folderUrl(win.vm.coresFolder))) win.vm.managerStatus = qsTr("%1 could not be opened").arg(win.vm.coresFolder)
            }
            Button { text: qsTr("Change folder..."); Layout.preferredWidth: 140; onClicked: folderDialog.open() }
            Button { text: qsTr("Refresh List"); Layout.preferredWidth: 140; onClicked: win.vm.run("refreshList") }
            Button {
                text: qsTr("Remove"); Layout.preferredWidth: 140
                enabled: win.tickedCount > 0
                onClicked: {
                    let n = 0
                    for (const name of Object.keys(win.ticked)) n += JSON.parse(win.vm.run("versionsOf", [name]) || "[]").length
                    removeCores.versions = n
                    removeCores.open()
                }
            }
            Button { text: qsTr("Show Systems..."); Layout.preferredWidth: 140; onClicked: { systemsWindow.all = JSON.parse(win.vm.run("supportedSystems") || "[]"); systemsWindow.show() } }
            Item { Layout.fillWidth: true }
            Button { text: qsTr("Close"); Layout.preferredWidth: 90; onClicked: win.close() }
        }
    }

    // Chimera's Supported Systems: which machines the cores here add up to, and which core runs each
    Window {
        id: systemsWindow
        objectName: "supportedSystemsWindow"
        title: qsTr("Supported Systems")
        width: 720
        height: 520
        minimumWidth: 480
        minimumHeight: 300
        transientParent: win
        color: palette.window
        property var all: []
        // a system answers the filter by its name, its id or one of its cores
        readonly property var shown: {
            const t = filterBox.text.trim().toLowerCase()
            return t === "" ? systemsWindow.all
                : systemsWindow.all.filter(e => e.name.toLowerCase().includes(t) || e.id.toLowerCase().includes(t) || e.cores.toLowerCase().includes(t))
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            RowLayout {
                Label { text: qsTr("Filter:") }
                TextField { id: filterBox; Layout.fillWidth: true }
                Label {
                    text: systemsWindow.shown.length === systemsWindow.all.length ? qsTr("%1 systems").arg(systemsWindow.all.length)
                        : qsTr("%1 of %2").arg(systemsWindow.shown.length).arg(systemsWindow.all.length)
                    Layout.preferredWidth: 100
                    horizontalAlignment: Text.AlignRight
                }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: palette.base
                border.color: palette.mid
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 1
                    spacing: 0
                    Row {
                        Repeater {
                            model: [[qsTr("System"), 260], [qsTr("Id"), 110], [qsTr("Cores"), 300]]
                            delegate: Label {
                                required property var modelData
                                text: modelData[0]; width: modelData[1]
                                leftPadding: 4; topPadding: 2; bottomPadding: 2
                                background: Rectangle { color: palette.button; border.color: palette.mid }
                            }
                        }
                    }
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: systemsWindow.shown
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Row {
                            required property var modelData
                            height: 20
                            Label { text: modelData.name; width: 260; leftPadding: 4; elide: Text.ElideRight }
                            Label { text: modelData.id; width: 110; leftPadding: 4 }
                            Label { text: modelData.cores; width: 300; leftPadding: 4; elide: Text.ElideRight }
                        }
                    }
                }
            }
            RowLayout {
                Item { Layout.fillWidth: true }
                Button { text: qsTr("Close"); Layout.preferredWidth: 90; onClicked: systemsWindow.close() }
            }
        }
    }
}
