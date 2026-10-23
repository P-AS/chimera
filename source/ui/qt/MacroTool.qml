// Macro Input, as Chimera's: the File menu (Save Selected As..., Load
// Macro..., Recent); the zone's start and end with Set Macro; the zones, the
// entire movie first; the picked zone's name, the frame it goes to with
// Current, Place Zone, Replace and Overlay; and the movie's controls along
// the bottom, each ticked when the picked zone carries it. Layout and
// bindings only, over the macroTool view model.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "macroToolWindow"
    required property QtObject vm
    required property QtObject pianoRoll
    property var folderOf: null
    // the theme's colour for a role (Main hands it its lookup)
    property var themeColor: (role, fallback) => fallback
    property QtObject customize: null
    property QtObject toolWindows: null
    property Window mainWindow: null
    title: qsTr("Macro Input")
    width: 420
    height: 460

    WindowOptions { id: windowOptions; window: win; tool: "macroInput"; vm: win.toolWindows; mainWindow: win.mainWindow }
    onVisibleChanged: if (visible) windowOptions.placeBack(false)
    function menuOpened() { if (win.customize) win.customize.run("menuOpened") }
    function menuClosed() { if (win.customize) win.customize.run("menuClosed") }

    readonly property var zoneList: win.vm.rows("zones")
    readonly property var movieColumns: JSON.parse(win.vm.movieColumns)
    property var pickedRow: null

    // Chimera asks before macros not saved are let go: Yes saves each, No lets them go
    property bool leaving: false
    property var toSave: []
    onClosing: (close) => {
        if (win.vm.unsavedCount > 0 && !win.leaving) { close.accepted = false; unsavedDialog.open() }
        win.leaving = false
    }
    function saveNext() {
        if (win.toSave.length === 0) { win.leaving = true; win.close(); return }
        const z = win.toSave[0]
        win.toSave = win.toSave.slice(1)
        saveDialog.zone = z
        saveDialog.open()
    }
    MessageDialog {
        id: unsavedDialog
        title: qsTr("Save?")
        text: qsTr("You have unsaved macro(s). Do you wish to save them?")
        buttons: MessageDialog.Yes | MessageDialog.No | MessageDialog.Cancel
        onButtonClicked: (button) => {
            if (button === MessageDialog.Cancel) return
            if (button === MessageDialog.No) { win.leaving = true; win.close(); return }
            const list = []
            for (let i = 0; i < zones.count; i++) if (!zones.itemAtIndex(i) || zones.itemAtIndex(i).saved === false) list.push(i)
            win.toSave = list
            win.saveNext()
        }
    }
    MessageDialog { id: notice; buttons: MessageDialog.Ok }

    FileDialog {
        id: saveDialog
        title: qsTr("Save Selected As")
        property int zone: -1
        fileMode: FileDialog.SaveFile
        defaultSuffix: "chimeraMacro"
        currentFolder: win.folderOf ? win.folderOf("Macros") : ""
        nameFilters: [qsTr("Movie Macros (*.chimeraMacro)")]
        onAccepted: {
            win.vm.run("saveAs", [saveDialog.zone, selectedFile])
            if (win.toSave.length > 0 || win.vm.unsavedCount === 0) win.saveNext()
        }
    }
    FileDialog {
        id: loadDialog
        title: qsTr("Load Macro")
        currentFolder: win.folderOf ? win.folderOf("Macros") : ""
        nameFilters: [qsTr("Movie Macros (*.chimeraMacro)"), qsTr("All files (*)")]
        onAccepted: win.vm.run("load", [selectedFile])
    }

    menuBar: MenuBar {
        Menu {
            title: qsTr("&File")
            onAboutToShow: win.menuOpened()
            onAboutToHide: win.menuClosed()
            MenuItem {
                text: qsTr("Save Selected As...")
                onTriggered: {
                    if (win.pickedRow === null) { notice.text = qsTr("Please select a zone first."); notice.open(); return }
                    saveDialog.zone = win.vm.picked
                    saveDialog.open()
                }
            }
            MenuItem { text: qsTr("Load Macro..."); onTriggered: loadDialog.open() }
            Menu {
                id: recentMenu
                title: qsTr("Recent")
                MenuItem { text: qsTr("None"); enabled: false; visible: win.pianoRoll.recentMacros === ""; height: visible ? implicitHeight : 0 }
                Instantiator {
                    model: win.pianoRoll.recentMacros === "" ? [] : win.pianoRoll.recentMacros.split("\n")
                    delegate: MenuItem {
                        required property string modelData
                        text: modelData
                        onTriggered: win.vm.run("load", [modelData])
                    }
                    onObjectAdded: (index, object) => recentMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => recentMenu.removeItem(object)
                }
            }
            MenuSeparator {}
            MenuItem { text: qsTr("&Close"); onTriggered: win.close() }
        }
        // Chimera adds a Settings menu for the window's items to a tool that has none
        Menu {
            title: qsTr("&Settings")
            onAboutToShow: win.menuOpened()
            onAboutToHide: win.menuClosed()
            MenuItem { text: qsTr("Save Window &Position"); checkable: true; checked: windowOptions.savePosition; onTriggered: windowOptions.set("save", checked) }
            MenuItem {
                text: qsTr("Stay on &Top"); checkable: true; checked: windowOptions.onTop
                visible: windowOptions.canStayOnTop; height: visible ? implicitHeight : 0
                onTriggered: windowOptions.set("top", checked)
            }
            MenuItem { text: qsTr("&Float From Main Window"); checkable: true; checked: windowOptions.floating; onTriggered: windowOptions.set("float", checked) }
            MenuItem { text: qsTr("Restore &Defaults"); onTriggered: windowOptions.restore() }
        }
    }

    footer: Label {
        text: win.vm.error !== "" ? win.vm.error : win.vm.status
        color: win.vm.error !== "" ? win.themeColor("AccentError", "#b00020") : palette.text
        padding: 4
        elide: Text.ElideRight
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 7
        spacing: 6
        Label { text: qsTr("macro start      macro end") }
        RowLayout {
            SpinBox {
                id: zoneStart; from: 0; to: Math.max(0, win.pianoRoll.frameCount - 1); editable: true
                value: win.pianoRoll.selectFirst >= 0 ? win.pianoRoll.selectFirst : 0
                Layout.preferredWidth: 100
            }
            SpinBox {
                id: zoneEnd; from: 0; to: Math.max(0, win.pianoRoll.frameCount - 1); editable: true
                value: win.pianoRoll.selectLast >= 0 ? win.pianoRoll.selectLast : 0
                Layout.preferredWidth: 100
            }
            Button { text: qsTr("Set Macro"); onClicked: win.vm.run("setZone", [zoneStart.value, zoneEnd.value]) }
        }
        ListView {
            id: zones
            objectName: "macroZones"
            Layout.fillWidth: true
            Layout.preferredHeight: 140
            clip: true
            model: win.zoneList
            currentIndex: win.vm.picked
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                id: z
                required property int index
                required property string name
                required property int length
                required property bool overlay
                required property bool replace
                required property string columns
                required property bool saved
                width: zones.width
                padding: 2
                highlighted: ListView.isCurrentItem
                // the entire movie's zone, as Chimera says, does not change
                text: qsTr("%1 - length: %2").arg(z.name).arg(z.length) + (z.index === 0 ? qsTr(" [Zones don't change!]") : "")
                onClicked: win.vm.picked = z.index
                Component.onCompleted: if (z.index === win.vm.picked) win.pickedRow = z
                ListView.onIsCurrentItemChanged: if (ListView.isCurrentItem) win.pickedRow = z
            }
        }
        GridLayout {
            columns: 4
            enabled: win.pickedRow !== null
            Label { text: qsTr("Macro Name:") }
            TextField {
                Layout.preferredWidth: 110
                text: win.pickedRow ? win.pickedRow.name : ""
                onTextEdited: if (win.pickedRow) win.vm.run("rename", [win.vm.picked, text])
            }
            Label { text: qsTr("Frame:") }
            SpinBox { id: placeAt; from: 0; to: Math.max(0, win.pianoRoll.frameCount); editable: true; Layout.preferredWidth: 90 }
            Button { text: qsTr("Place Zone"); onClicked: win.vm.run("place", [win.vm.picked, placeAt.value]) }
            CheckBox { text: qsTr("Replace"); checked: win.pickedRow ? win.pickedRow.replace : true; onToggled: win.vm.run("setReplace", [win.vm.picked, checked]) }
            CheckBox { text: qsTr("Overlay"); checked: win.pickedRow ? win.pickedRow.overlay : false; onToggled: win.vm.run("setOverlay", [win.vm.picked, checked]) }
            Button { text: qsTr("Current"); onClicked: placeAt.value = win.pianoRoll.currentFrame }
        }
        Item { Layout.fillHeight: true }
        // the movie's controls, each ticked when the picked zone carries it: as Chimera places them, the last
        // in the bottom row and the rows right-aligned (laid out from the end and turned over)
        Flow {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            rotation: 180
            spacing: 4
            enabled: win.pickedRow !== null
            Repeater {
                model: win.movieColumns.slice().reverse()
                delegate: CheckBox {
                    required property string modelData
                    text: modelData
                    padding: 1
                    rotation: 180
                    readonly property var carried: win.pickedRow && win.pickedRow.columns !== "" ? win.pickedRow.columns.split("|") : win.movieColumns
                    checked: carried.indexOf(modelData) >= 0
                    opacity: checked ? 1 : 0.6
                    onToggled: {
                        const now = win.movieColumns.filter(c => c === modelData ? checked : carried.indexOf(c) >= 0)
                        win.vm.run("setColumns", [win.vm.picked, now.length === win.movieColumns.length ? "" : now.join("|")])
                    }
                }
            }
        }
    }
}
