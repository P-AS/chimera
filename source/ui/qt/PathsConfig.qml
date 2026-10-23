// Chimera's Path Configuration (Config > Paths), laid out as Chimera's
// PathConfig: Always use recent path for ROMs and Special Commands at the
// top; Global and by System (a system picked, its own folders) as tabs, a
// row a folder (its path, a button to browse for it, its name); Save,
// Defaults, OK and Cancel. The rows are a copy: Save or OK keeps them. It
// opens on the system of the game open, as Chimera's does. Layout and
// bindings only, over the paths view model.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "pathsWindow"
    required property QtObject vm
    property QtObject session: null
    property Window mainWindow: null
    title: qsTr("Path Configuration")
    width: 784
    height: 540
    modality: Qt.ApplicationModal
    transientParent: win.mainWindow

    readonly property var systemNames: JSON.parse(win.vm.systems)
    // the rows as typed, "system|name" to the path; what is not here is the view model's
    property var edits: ({})
    property bool recent: true
    function keyOf(system, name) { return system + "|" + name }
    function textOf(system, name, path) { const k = win.keyOf(system, name); return k in win.edits ? win.edits[k] : path }
    function edit(system, name, text) {
        const now = Object.assign({}, win.edits)
        now[win.keyOf(system, name)] = text
        win.edits = now
    }
    function save() {
        win.vm.recentForRoms = win.recent
        for (const k of Object.keys(win.edits)) {
            const at = k.indexOf("|")
            const system = k.substring(0, at), name = k.substring(at + 1)
            if (system === "Global") win.vm.run("setPath", [name, win.edits[k]])
            else win.vm.run("setSystemPath", [system, name, win.edits[k]])
        }
        win.edits = {}
    }
    onVisibleChanged: if (visible) {
        win.edits = {}
        win.recent = win.vm.recentForRoms
        // the system of the game open, when it has folders of its own
        const sys = win.session ? win.session.systemId : ""
        const at = win.systemNames.indexOf(sys)
        systemBox.currentIndex = at >= 0 ? at : (win.systemNames.length > 0 ? 0 : -1)
        tabs.currentIndex = at >= 0 ? 1 : 0
    }

    // Chimera's defaults for every global folder (the by System tab keeps what it has)
    readonly property var chimeraGlobals: ({
        "Base": ".", "ROM": ".", "Firmware": "./Firmware", "Movies": "./Movies", "Movie backups": "./Movies/backup", "A/V Dumps": ".",
        "Tools": "./Tools", "Lua": "./Lua", "Themes": "./Themes", "Watch (.wch)": ".", "Debug Logs": ".", "Bug reports": "./Reports",
        "Macros": "./Movies/Macros", "Temp Files": ""
    })

    // one tab's rows: the path, Browse, the name
    component PathRows: ScrollView {
        id: page
        required property string system
        clip: true
        contentWidth: availableWidth
        ColumnLayout {
            width: page.availableWidth
            spacing: 7
            Item { height: 6 }
            Repeater {
                model: win.vm.rows("paths")
                delegate: RowLayout {
                    id: row
                    required property string system
                    required property string name
                    required property string path
                    visible: row.system === page.system
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? implicitHeight : 0
                    Layout.leftMargin: 6
                    Layout.rightMargin: 6
                    // the ROM folder is not used while the recent one is, as Chimera's
                    enabled: !(row.name === "ROM" && win.recent)
                    TextField {
                        id: box
                        objectName: "pathBox"
                        Layout.fillWidth: true
                        text: win.textOf(row.system, row.name, row.path)
                        onTextEdited: { win.edit(row.system, row.name, text); suggestions.refresh() }
                        // Chimera's suggestions: the special words
                        Popup {
                            id: suggestions
                            y: box.height
                            width: 120
                            padding: 1
                            property var matches: []
                            function refresh() {
                                const t = box.text
                                suggestions.matches = t === "" ? [] : ["%recent%", "%exe%", "%rom%"].filter(w => w.startsWith(t) && w !== t)
                                if (suggestions.matches.length > 0 && box.activeFocus) open(); else close()
                            }
                            contentItem: Column {
                                Repeater {
                                    model: suggestions.matches
                                    delegate: ItemDelegate {
                                        required property string modelData
                                        width: 118
                                        padding: 2
                                        text: modelData
                                        onClicked: { box.text = modelData; win.edit(row.system, row.name, modelData); suggestions.close() }
                                    }
                                }
                            }
                        }
                    }
                    ToolButton {
                        icon.source: "qrc:/chimera/icons/OpenFile.png"
                        Layout.preferredWidth: 26
                        Layout.preferredHeight: 23
                        onClicked: {
                            // a base or a global folder is browsed from the global base, as Chimera's
                            const forSystem = row.name === "Base" || row.system === "Global" ? "" : row.system
                            folderDialog.system = row.system
                            folderDialog.name = row.name
                            folderDialog.forSystem = forSystem
                            folderDialog.title = qsTr("Set the directory for %1").arg(row.name)
                            folderDialog.currentFolder = "file://" + win.vm.run("absolute", [box.text, forSystem])
                            folderDialog.open()
                        }
                    }
                    Label { text: row.name; Layout.preferredWidth: 100; elide: Text.ElideRight }
                }
            }
            Item { Layout.fillHeight: true }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 4
        RowLayout {
            Layout.fillWidth: true
            CheckBox { text: qsTr("Always use recent path for ROMs"); checked: win.recent; onToggled: win.recent = checked; padding: 2 }
            Item { Layout.fillWidth: true }
            ToolButton {
                icon.source: "qrc:/chimera/icons/Help.png"
                Layout.preferredWidth: 26
                Layout.preferredHeight: 23
                onClicked: specialCommands.show()
            }
            Label { text: qsTr("Special Commands") }
        }
        TabBar {
            id: tabs
            Layout.fillWidth: true
            TabButton { text: qsTr("Global"); width: implicitWidth }
            TabButton { text: qsTr("by System"); width: implicitWidth }
        }
        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: 0
            StackLayout {
                anchors.fill: parent
                currentIndex: tabs.currentIndex
                PathRows { system: "Global" }
                ColumnLayout {
                    RowLayout {
                        Layout.margins: 4
                        Label { text: qsTr("System:") }
                        ComboBox { id: systemBox; model: win.systemNames; Layout.preferredWidth: 200 }
                    }
                    PathRows {
                        system: systemBox.currentIndex >= 0 ? win.systemNames[systemBox.currentIndex] : ""
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Button { text: qsTr("&Save"); onClicked: win.save() }
            Button {
                text: qsTr("&Defaults")
                onClicked: {
                    const now = Object.assign({}, win.edits)
                    for (const n of Object.keys(win.chimeraGlobals)) now[win.keyOf("Global", n)] = win.chimeraGlobals[n]
                    win.edits = now
                }
            }
            Label { text: win.vm.error; color: "#b00020"; elide: Text.ElideRight; Layout.fillWidth: true }
            Button { text: qsTr("&OK"); onClicked: { win.save(); win.close() } }
            Button { text: qsTr("&Cancel"); onClicked: win.close() }
        }
    }
    FolderDialog {
        id: folderDialog
        property string system: ""
        property string name: ""
        property string forSystem: ""
        onAccepted: {
            const local = decodeURIComponent(String(selectedFolder).replace(/^file:\/\/(?=\/[A-Za-z]:)/, "").replace(/^file:\/\//, ""))
            win.edit(folderDialog.system, folderDialog.name, win.vm.run("makeRelative", [local, folderDialog.forSystem]))
        }
    }

    // Chimera's Special Commands window: what the special words in a path mean
    Window {
        id: specialCommands
        title: qsTr("Special Commands")
        width: 466
        height: 190
        transientParent: win
        color: palette.window
        GridLayout {
            anchors.fill: parent
            anchors.margins: 13
            columns: 2
            columnSpacing: 20
            rowSpacing: 4
            Label { text: "%recent%" }
            Label { text: qsTr("Sets the path to the Windows Recent Path") }
            Label { text: "%exe%" }
            Label { text: qsTr("Sets the path of the executable (Chimera.exe)") }
            Label { text: ".\\"; Layout.topMargin: 12 }
            Label { text: qsTr("Sets the path to the base path"); Layout.topMargin: 12 }
            Label { text: "..\\" }
            Label { text: qsTr("Sets the path to the folder above the base path") }
            Label {
                Layout.columnSpan: 2
                Layout.leftMargin: 80
                text: qsTr("- Setting the global base path to one of these will set it to the path of the .exe\n- Setting a platform base will set it to the global base\n- Setting a platform folder will set it to the platform base\n- Setting a tools folder will set it to the global base")
            }
            Item { Layout.fillHeight: true; Layout.columnSpan: 2 }
            Item { Layout.fillWidth: true }
            Button { text: qsTr("&OK"); onClicked: specialCommands.close(); Layout.alignment: Qt.AlignRight }
        }
    }
}
