// Chimera's New Chimera Project, laid out as its wizard: a page at a time,
// each with its heading, and < Back, Next > (Create on the last page) and
// Cancel under them beside a line that says what keeps a page from going
// on. 1: the kind of core, the core, its system and its renderer (what a
// hardware renderer costs under it). 2: the game's files, a box a kind of
// file, skipped for a package that takes none. 3: the settings, with the
// machine's presets above them and what the core suggests for the game.
// 4: the firmware those decisions call for, skipped when they call for
// none. A project made from an imported movie asks where it is saved. A
// dialog of the main window, as Chimera's. Layout and bindings only, over
// the newProject view model.
//
// Not here: Chimera's fifth page, the compile step for a core that
// recompiles a game's code, as the prototype runs no compile sessions.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "newProjectWindow"
    required property QtObject vm
    // asked before the project made replaces the one open (the window's question about unsaved changes)
    property var askFirst: (then) => then()
    property Window mainWindow: null
    property var themeColor: (role, fallback) => fallback
    title: qsTr("New Chimera Project")
    width: 620
    height: 480
    minimumWidth: 520
    minimumHeight: 380
    modality: Qt.ApplicationModal
    transientParent: win.mainWindow

    // the page shown: 0 the core, 1 the files, 2 the settings, 3 the firmware
    property int page: 0
    // what the wizard last had to say, in the error colour as Chimera's
    property string said: ""
    property string slotFor: ""
    property int firmwarePicked: -1
    readonly property color errorColor: win.themeColor("AccentError", "#c62828")
    readonly property color goodColor: win.themeColor("AccentGood", "#2e7d32")

    onVisibleChanged: if (visible) {
        // the first core of the kind when none is chosen, as Chimera's opens
        if (win.vm.coreLine < 0 && win.vm.coreLines !== "") win.vm.run("chooseCoreLine", [0])
        win.showPage(0)
    }
    Connections {
        target: win.vm
        function onValueChanged(key, value) { if (key === "status") win.said = value }
    }
    // a path as a file dialog takes it
    function urlOf(path) { return path === "" ? "" : path.startsWith("/") ? "file://" + path : "file:///" + path.replace(/\\/g, "/") }
    // a command whose refusal is said on the wizard's line
    function run(command, args) {
        win.vm.run(command, args || [])
        if (win.vm.error !== "") win.said = win.vm.error
    }

    // the pages asked: the files when the package takes any, the firmware when the choices call for some
    function applies(p) { return p === 1 ? win.vm.filesAsked : p === 3 ? win.vm.firmwareAsked : true }
    function nextFrom(p) {
        for (let i = p + 1; i <= 3; i++) if (win.applies(i)) return i
        return -1
    }
    readonly property bool lastPage: win.nextFrom(win.page) < 0
    function showPage(p) {
        win.page = p
        win.said = ""
        win.firmwarePicked = -1
        // the firmware is looked for again when its page is shown
        if (p === 3) win.vm.run("firmwarePage")
    }
    function back() {
        for (let i = win.page - 1; i >= 0; i--)
            if (win.applies(i)) { win.showPage(i); return }
        win.showPage(0)
    }
    function advance() {
        if (win.page === 0) {
            if (win.vm.core === "") { win.said = qsTr("pick a core (none in the cores folder?)"); return }
            if (win.applies(1)) { win.showPage(1); return }
        }
        if (win.page <= 1) {
            if (win.vm.filesComplaint !== "") { win.said = win.vm.filesComplaint; return }
            win.showPage(2)
            return
        }
        if (win.page === 3 && win.vm.firmwareMissing !== "") {
            win.said = qsTr("%1 is required and not provided").arg(win.vm.firmwareMissing)
            return
        }
        const next = win.nextFrom(win.page)
        if (next < 0) win.create()
        else win.showPage(next)
    }
    function create() {
        // a project made from an imported movie is saved at once, where asked
        if (win.vm.imported) { importSaveDialog.open(); return }
        win.askFirst(() => {
            win.run("create")
            if (win.vm.error === "") win.close()
            else if (win.vm.error.indexOf("clock start") >= 0) win.showPage(2)
        })
    }

    Shortcut { sequences: ["Return", "Enter"]; enabled: win.active && nextButton.enabled; onActivated: win.advance() }
    Shortcut { sequence: "Escape"; enabled: win.active; onActivated: win.close() }

    FileDialog {
        id: fileDialog
        title: qsTr("Choose a file")
        fileMode: FileDialog.OpenFiles
        onAccepted: { for (const f of selectedFiles) win.run("addFile", [win.slotFor, f]) }
    }
    FileDialog {
        id: firmwareDialog
        property string need: ""
        onAccepted: win.run("chooseFirmware", [firmwareDialog.need, selectedFile])
    }
    FolderDialog {
        id: scanDialog
        title: qsTr("Scan a folder for firmware")
        onAccepted: win.run("scanFolder", [selectedFolder, scanSubfolders.checked])
    }
    FileDialog {
        id: importSaveDialog
        title: qsTr("Save the imported project")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "chimeraProject"
        nameFilters: [qsTr("TAS Project Files (*.chimeraProject)")]
        // beside the movie, named after it
        currentFolder: win.urlOf(win.vm.importMovie.replace(/[\\/][^\\/]*$/, ""))
        selectedFile: win.vm.importMovie === "" ? "" : win.urlOf(win.vm.importMovie.replace(/\.[^.\\/]*$/, "") + ".chimeraProject")
        onAccepted: win.askFirst(() => {
            win.run("create", [selectedFile])
            if (win.vm.error === "") win.close()
        })
    }

    component Heading: Label { Layout.fillWidth: true; Layout.bottomMargin: 10; elide: Text.ElideRight }
    component Caption: Label { Layout.preferredWidth: 96 }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        StackLayout {
            id: pages
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: win.page

            // ---- page 1: the machine ----
            ColumnLayout {
                spacing: 8
                Heading { text: qsTr("Please select the core: an emulator, or a game.") }
                GridLayout {
                    columns: 2
                    columnSpacing: 8
                    rowSpacing: 8
                    Layout.fillWidth: true
                    Caption { text: qsTr("Kind:"); Layout.row: 0; Layout.column: 0 }
                    ComboBox {
                        Layout.row: 0
                        Layout.column: 1
                        Layout.preferredWidth: 160
                        model: [qsTr("Emulators"), qsTr("Games")]
                        currentIndex: win.vm.kind === "games" ? 1 : 0
                        onActivated: (i) => {
                            win.run("chooseKind", [i === 1 ? "games" : "emulators"])
                            // another kind: its first core chosen
                            if (win.vm.coreLines !== "") win.run("chooseCoreLine", [0])
                        }
                    }
                    Caption { text: qsTr("Core:"); Layout.row: 1; Layout.column: 0 }
                    ComboBox {
                        objectName: "coreBox"
                        Layout.row: 1
                        Layout.column: 1
                        Layout.fillWidth: true
                        model: win.vm.coreLines === "" ? [] : win.vm.coreLines.split("\n")
                        currentIndex: win.vm.coreLine
                        onActivated: (i) => win.run("chooseCoreLine", [i])
                    }
                    Caption { text: qsTr("System:"); Layout.row: 2; Layout.column: 0 }
                    // a package that is one machine still says which, in a box that is not a choice
                    ComboBox {
                        Layout.row: 2
                        Layout.column: 1
                        Layout.fillWidth: true
                        model: win.vm.machines !== "" ? win.vm.machines.split("\n") : win.vm.systems !== "" ? [win.vm.systems] : []
                        currentIndex: win.vm.machines !== "" ? win.vm.machine : 0
                        enabled: count > 1
                        onActivated: (i) => win.run("chooseMachine", [i])
                    }
                    Caption { text: qsTr("Renderer:"); Layout.row: 3; Layout.column: 0 }
                    // one renderer is not a choice
                    ComboBox {
                        Layout.row: 3
                        Layout.column: 1
                        Layout.fillWidth: true
                        model: win.vm.renderers === "" ? [] : win.vm.renderers.split("\n")
                        currentIndex: win.vm.renderer
                        enabled: count > 1
                        onActivated: (i) => win.run("chooseRenderer", [i])
                    }
                    // what a hardware renderer costs; the room kept whether or not it is said
                    Label {
                        Layout.row: 4
                        Layout.column: 1
                        Layout.fillWidth: true
                        Layout.preferredHeight: 50
                        verticalAlignment: Text.AlignTop
                        text: win.vm.rendererCaveat
                        wrapMode: Text.WordWrap
                        color: palette.placeholderText
                    }
                }
                Item { Layout.fillHeight: true }
                Label {
                    readonly property string url: "https://github.com/ToolAssisted-run/chimera/issues"
                    Layout.fillWidth: true
                    textFormat: Text.StyledText
                    text: qsTr("Please submit any issues and feature requests here: %1").arg("<a href=\"" + url + "\">" + url + "</a>")
                    linkColor: palette.link
                    color: palette.placeholderText
                    elide: Text.ElideRight
                    onLinkActivated: (link) => Qt.openUrlExternally(link)
                    HoverHandler { cursorShape: parent.hoveredLink !== "" ? Qt.PointingHandCursor : Qt.ArrowCursor }
                }
            }

            // ---- page 2: the files ----
            ColumnLayout {
                Heading { text: qsTr("Please provide the game ROMs and other persistent data.") }
                ScrollView {
                    id: slotsView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: slotsView.availableWidth
                        spacing: 6
                        Repeater {
                            model: win.vm.rows("slots")
                            delegate: GroupBox {
                                id: slot
                                required property var model
                                required property string id
                                // the box's own title is the box's: the slot's is read from the row
                                readonly property string slotTitle: slot.model.title
                                required property string formats
                                required property string help
                                required property bool exposed
                                required property string files
                                required property string filter
                                required property int max
                                required property string cardinality
                                required property bool shown
                                property int picked: -1
                                // a slot only settings open, which this machine closes, is not on the form
                                visible: slot.shown
                                enabled: slot.exposed
                                Layout.fillWidth: true
                                Layout.rightMargin: 12
                                title: slot.slotTitle + "  (" + slot.cardinality + ")" + (slot.exposed ? "" : qsTr(" - unavailable with the current files"))
                                ToolTip.visible: slotHover.hovered && ToolTip.text !== ""
                                ToolTip.delay: 600
                                ToolTip.text: slot.help !== "" ? slot.help : qsTr("the core did not explain this slot")
                                HoverHandler { id: slotHover }
                                ColumnLayout {
                                    anchors.fill: parent
                                    spacing: 4
                                    Label {
                                        visible: slot.formats !== ""
                                        text: qsTr("Formats: %1").arg(slot.formats)
                                        wrapMode: Text.WordWrap
                                        Layout.fillWidth: true
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        // the list is where a file dragged onto this slot lands
                                        Rectangle {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 70
                                            color: palette.base
                                            border.color: slotDrop.containsDrag ? palette.highlight : palette.mid
                                            ListView {
                                                id: fileList
                                                anchors.fill: parent
                                                anchors.margins: 1
                                                clip: true
                                                model: slot.files === "" ? [] : slot.files.split("\n")
                                                ScrollBar.vertical: ScrollBar {}
                                                delegate: Rectangle {
                                                    required property string modelData
                                                    required property int index
                                                    width: fileList.width
                                                    height: fileName.implicitHeight + 4
                                                    color: index === slot.picked ? palette.highlight : "transparent"
                                                    Label {
                                                        id: fileName
                                                        anchors.verticalCenter: parent.verticalCenter
                                                        leftPadding: 3
                                                        text: parent.modelData
                                                        color: parent.index === slot.picked ? palette.highlightedText : palette.text
                                                    }
                                                    TapHandler { onTapped: slot.picked = parent.index }
                                                }
                                            }
                                            DropArea {
                                                id: slotDrop
                                                anchors.fill: parent
                                                onDropped: (drop) => {
                                                    for (const u of drop.urls) win.run("addFile", [slot.id, u])
                                                    drop.accept()
                                                }
                                            }
                                        }
                                        // the order is the machine's: the first file is in the drive at boot
                                        ColumnLayout {
                                            visible: slot.max !== 1
                                            Layout.alignment: Qt.AlignTop
                                            Button {
                                                text: "▲"
                                                Layout.preferredWidth: 32
                                                enabled: slot.picked > 0
                                                ToolTip.visible: hovered
                                                ToolTip.text: qsTr("Move up: earlier in the swap order. The first file is the one in the drive at boot.")
                                                onClicked: { win.run("moveFile", [slot.id, slot.picked, slot.picked - 1]); slot.picked-- }
                                            }
                                            Button {
                                                text: "▼"
                                                Layout.preferredWidth: 32
                                                enabled: slot.picked >= 0 && slot.picked < fileList.count - 1
                                                ToolTip.visible: hovered
                                                ToolTip.text: qsTr("Move down: later in the swap order.")
                                                onClicked: { win.run("moveFile", [slot.id, slot.picked, slot.picked + 1]); slot.picked++ }
                                            }
                                        }
                                        ColumnLayout {
                                            Layout.alignment: Qt.AlignTop
                                            Button {
                                                text: qsTr("Add...")
                                                Layout.preferredWidth: 84
                                                onClicked: {
                                                    win.slotFor = slot.id
                                                    fileDialog.nameFilters = slot.filter === "*" ? [qsTr("All files (*)")]
                                                        : [slot.slotTitle + " (" + slot.filter + ")", qsTr("All files (*)")]
                                                    fileDialog.open()
                                                }
                                            }
                                            Button {
                                                text: qsTr("Remove")
                                                Layout.preferredWidth: 84
                                                enabled: slot.picked >= 0
                                                onClicked: { win.run("removeFile", [slot.id, slot.picked]); slot.picked = -1 }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ---- page 3: the settings ----
            ColumnLayout {
                Heading { text: qsTr("Please specify the emulation configuration settings.") }
                // a machine somebody has got working, then adjusted: absent when there are none
                RowLayout {
                    Layout.fillWidth: true
                    visible: presetBox.count > 0
                    Label { text: qsTr("Preset:") }
                    ComboBox {
                        id: presetBox
                        Layout.fillWidth: true
                        model: win.vm.rows("presets")
                        textRole: "label"
                        valueRole: "id"
                        // the list is given again after every edit: the choice stays
                        property string chosen: ""
                        onActivated: chosen = currentValue
                        Connections {
                            target: presetBox.model
                            function onModelReset() { Qt.callLater(presetBox.keep) }
                        }
                        function keep() {
                            const at = chosen === "" ? -1 : indexOfValue(chosen)
                            currentIndex = at >= 0 ? at : (count > 0 ? 0 : -1)
                        }
                        Component.onCompleted: keep()
                    }
                    Button { text: qsTr("Apply"); Layout.preferredWidth: 72; enabled: presetBox.currentIndex >= 0; onClicked: win.run("applyPreset", [presetBox.currentValue]) }
                }
                // what the core suggests for the game, selectable so its source can be copied
                ScrollView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 62
                    visible: win.vm.suggestion !== ""
                    TextArea { text: win.vm.suggestion; readOnly: true; wrapMode: TextEdit.Wrap; activeFocusOnTab: false }
                }
                SettingsList {
                    vm: win.vm
                    presetsHere: false
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }
                // when the machine's clock starts: the machine's for every core, so under the settings, as Chimera's
                RowLayout {
                    Label { text: qsTr("The machine's clock starts at:") }
                    TextField {
                        objectName: "clockStart"
                        text: win.vm.clockStart
                        Layout.preferredWidth: 160
                        onTextEdited: win.vm.clockStart = text
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Every run of this project starts the machine's clock at this moment, so a movie plays back the same.\n"
                            + "A game that takes its luck from the date and time deals other luck from another start.\n"
                            + "Written as year-month-day hours:minutes:seconds, between 1970 and 2200.")
                    }
                    Label { text: qsTr("UTC") }
                }
            }

            // ---- page 4: the firmware ----
            ColumnLayout {
                Heading { text: qsTr("Please indicate where to find the required firmware files.") }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: palette.base
                    border.color: palette.mid
                    readonly property var widths: [190, 130, 110, 180]
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 1
                        spacing: 0
                        Row {
                            Repeater {
                                model: [qsTr("Firmware"), qsTr("Expected file"), qsTr("SHA1"), qsTr("Status")]
                                delegate: Label {
                                    required property int index
                                    required property string modelData
                                    text: modelData
                                    width: parent.parent.parent.widths[index]
                                    leftPadding: 4; topPadding: 2; bottomPadding: 2
                                    elide: Text.ElideRight
                                    background: Rectangle { color: palette.button; border.color: palette.mid }
                                }
                            }
                        }
                        ListView {
                            id: firmwareList
                            objectName: "firmwareList"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: win.vm.rows("firmware")
                            ScrollBar.vertical: ScrollBar {}
                            delegate: Rectangle {
                                id: need
                                required property int index
                                required property string id
                                required property string name
                                required property string title
                                required property string expected
                                required property string sha1Shown
                                required property string state
                                required property bool satisfied
                                required property string description
                                required property string file
                                readonly property bool isPicked: need.index === win.firmwarePicked
                                width: firmwareList.width
                                height: 22
                                color: isPicked ? palette.highlight : "transparent"
                                // green against red says found or not, in line
                                readonly property color ink: isPicked ? palette.highlightedText : need.satisfied ? win.goodColor : win.errorColor
                                readonly property var widths: [190, 130, 110, 180]
                                Row {
                                    anchors.verticalCenter: parent.verticalCenter
                                    Label { text: need.title; color: need.ink; width: need.widths[0]; leftPadding: 4; elide: Text.ElideRight }
                                    Label { text: need.expected; color: need.ink; width: need.widths[1]; leftPadding: 4; elide: Text.ElideRight }
                                    Label { text: need.sha1Shown; color: need.ink; width: need.widths[2]; leftPadding: 4; elide: Text.ElideRight }
                                    Label { text: need.state; color: need.ink; width: need.widths[3]; leftPadding: 4; elide: Text.ElideRight }
                                }
                                ToolTip.visible: needHover.hovered && need.description !== ""
                                ToolTip.delay: 600
                                ToolTip.text: need.description
                                HoverHandler { id: needHover }
                                TapHandler {
                                    onTapped: win.firmwarePicked = need.index
                                    onDoubleTapped: { win.firmwarePicked = need.index; selectFirmware.clicked() }
                                }
                            }
                            Label {
                                visible: firmwareList.count === 0
                                text: qsTr("(these decisions need no firmware)")
                                leftPadding: 4
                            }
                        }
                    }
                }
                RowLayout {
                    Button {
                        id: selectFirmware
                        text: qsTr("Select File...")
                        enabled: win.firmwarePicked >= 0
                        onClicked: {
                            const it = firmwareList.itemAtIndex(win.firmwarePicked)
                            if (!it) return
                            firmwareDialog.need = it.id
                            firmwareDialog.title = qsTr("Locate %1").arg(it.name)
                            firmwareDialog.open()
                        }
                    }
                    Button {
                        text: qsTr("Clear")
                        enabled: win.firmwarePicked >= 0 && firmwareList.itemAtIndex(win.firmwarePicked) !== null && firmwareList.itemAtIndex(win.firmwarePicked).file !== ""
                        onClicked: win.run("clearFirmware", [firmwareList.itemAtIndex(win.firmwarePicked).id])
                    }
                    Button { text: qsTr("Scan Folder..."); onClicked: scanDialog.open() }
                    CheckBox { id: scanSubfolders; text: qsTr("Include sub-folders"); checked: true }
                    Item { Layout.fillWidth: true }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label {
                objectName: "wizardStatus"
                text: win.said
                color: win.errorColor
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Button { text: qsTr("< Back"); Layout.preferredWidth: 84; enabled: win.page > 0; onClicked: win.back() }
            Button {
                id: nextButton
                objectName: "nextButton"
                text: win.lastPage ? qsTr("Create") : qsTr("Next >")
                Layout.preferredWidth: 84
                // the last page's Create waits for the firmware it requires
                enabled: !win.lastPage || win.page !== 3 || win.vm.firmwareMissing === ""
                onClicked: win.advance()
            }
            Button { text: qsTr("Cancel"); Layout.preferredWidth: 84; onClicked: win.close() }
        }
    }
}
