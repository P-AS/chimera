// RAM Search, as Chimera's: its menus (File, Options, Search, Settings,
// Columns) and toolbar; the candidates on the left with their count and
// domain above them; on the right what a search compares with (Compare To /
// By), how (Comparison Operator), the size and display, Search and
// Auto-Search; the message line at the bottom. RAM Poke and Go to Address
// are windows of their own. Layout and bindings only, over the ramSearch
// view model.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "ramSearchWindow"
    required property QtObject vm
    // the main window's: Paths folders, the clipboard, theme colours, RAM Watch and the hex editor
    property var folderOf: null
    property QtObject clipboard: null
    property var themeColor: (role, fallback) => fallback
    property QtObject customize: null
    property QtObject toolWindows: null
    property Window mainWindow: null
    property var showRamWatch: null
    property var viewInHexEditor: null
    title: qsTr("RAM Search")
    width: 700
    height: 600

    WindowOptions { id: windowOptions; window: win; tool: "ramSearch"; vm: win.toolWindows; mainWindow: win.mainWindow }
    onVisibleChanged: if (visible) windowOptions.placeBack(false)

    MessageDialog { id: notice; buttons: MessageDialog.Ok }
    function tell(title, text) { notice.title = title; notice.text = text; notice.open() }
    // more rows than anyone picks by hand: asked first, as Chimera asks
    MessageDialog {
        id: manyAsk
        title: qsTr("RAM Search")
        buttons: MessageDialog.Ok | MessageDialog.Cancel
        property var then: null
        onButtonClicked: (button) => { const t = manyAsk.then; manyAsk.then = null; if (button === MessageDialog.Ok && t) t() }
    }
    function onPicked(action, then) {
        if (win.vm.selectedCount <= 1000) { then(); return }
        manyAsk.text = qsTr("%1 addresses are selected. %2 all of them?").arg(Number(win.vm.selectedCount).toLocaleString(Qt.locale("en_US"), "f", 0)).arg(action)
        manyAsk.then = then
        manyAsk.open()
    }
    function addToRamWatch() {
        win.onPicked(qsTr("Add to RAM Watch"), () => { if (win.vm.run("addToRamWatch") > 0 && win.showRamWatch) win.showRamWatch() })
    }
    function pokeAddress() {
        if (!win.vm.canPoke) return
        win.onPicked(qsTr("Poke"), () => poke.start(JSON.parse(win.vm.run("pokeInfo"))))
    }
    function copyRows() {
        win.onPicked(qsTr("Copy"), () => { const t = win.vm.run("selectedLines"); if (t && win.clipboard) win.clipboard.setText(t) })
    }

    FileDialog {
        id: openDialog
        title: qsTr("Open a watch list")
        property string how: "open"
        currentFolder: win.folderOf ? win.folderOf("Watch (.wch)") : ""
        nameFilters: [qsTr("Watch Files (*.wch)"), qsTr("All files (*)")]
        onAccepted: win.vm.run("open", [selectedFile, openDialog.how])
    }
    FileDialog {
        id: saveDialog
        title: qsTr("Save the search as")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "wch"
        currentFolder: win.folderOf ? win.folderOf("Watch (.wch)") : ""
        nameFilters: [qsTr("Watch Files (*.wch)")]
        onAccepted: win.vm.run("save", [selectedFile])
    }

    // a menu item showing its own shortcut, as Chimera's menus do
    Component {
        id: shortcutItem
        MenuItem {
            id: item
            readonly property string keys: action && typeof action.shortcut === "string" ? action.shortcut
                : action && action.keysShown ? action.keysShown : ""
            rightPadding: padding + (keys !== "" ? keyLabel.implicitWidth + 24 : 0)
            Label {
                id: keyLabel
                text: item.keys
                anchors.right: parent.right
                anchors.rightMargin: item.padding
                anchors.verticalCenter: parent.verticalCenter
                color: item.highlighted ? item.palette.highlightedText : item.palette.text
            }
        }
    }

    readonly property bool anyRow: win.vm.selectedCount > 0
    Action { id: searchAction; text: qsTr("&Search"); icon.source: "qrc:/chimera/icons/search.png"; enabled: win.vm.searching && !win.vm.autoSearch; onTriggered: win.vm.run("search") }
    Action { id: newSearchAction; text: qsTr("&New Search"); icon.source: "qrc:/chimera/icons/restart.png"; enabled: win.vm.domains !== ""; onTriggered: win.vm.run("newSearch") }
    Action { id: undoAction; text: qsTr("&Undo"); icon.source: "qrc:/chimera/icons/undo.png"; shortcut: "Ctrl+Z"; enabled: win.vm.canUndo; onTriggered: win.vm.run("undo") }
    Action { id: redoAction; text: qsTr("&Redo"); icon.source: "qrc:/chimera/icons/redo.png"; shortcut: "Ctrl+Y"; enabled: win.vm.canRedo; onTriggered: win.vm.run("redo") }
    Action { id: copyPrevAction; text: qsTr("Copy Value to Prev"); icon.source: "qrc:/chimera/icons/Previous.png"; enabled: win.vm.searching; onTriggered: win.vm.run("copyValueToPrev") }
    Action { id: clearCountsAction; text: qsTr("&Clear Change Counts"); enabled: win.vm.searching && !win.vm.fast; onTriggered: win.vm.run("clearChangeCounts") }
    Action { id: removeAction; property string keysShown: "Delete"; text: qsTr("&Remove selected"); icon.source: "qrc:/chimera/icons/Delete.png"; enabled: win.anyRow; onTriggered: win.vm.run("removeSelected") }
    Action { id: goToAction; text: qsTr("&Go to Address..."); shortcut: "Ctrl+G"; enabled: win.vm.searching; onTriggered: goTo.open() }
    Action { id: watchAction; text: qsTr("&Add to RAM Watch"); icon.source: "qrc:/chimera/icons/Find.png"; shortcut: "Ctrl+W"; enabled: win.anyRow; onTriggered: win.addToRamWatch() }
    Action { id: pokeAction; text: qsTr("&Poke Address"); icon.source: "qrc:/chimera/icons/poke.png"; shortcut: "Ctrl+P"; enabled: win.vm.canPoke; onTriggered: win.pokeAddress() }
    // frozen addresses: written before every frame, for this session only
    Action {
        id: freezeAction; text: qsTr("Freeze Address"); icon.source: "qrc:/chimera/icons/Freeze.png"; shortcut: "Ctrl+F"
        enabled: win.vm.canPoke
        onTriggered: win.onPicked(qsTr("Freeze or unfreeze"), () => win.vm.run("toggleFreeze"))
    }
    Action { id: selectAllAction; text: qsTr("Select All"); shortcut: "Ctrl+A"; enabled: win.vm.count > 0; onTriggered: win.vm.run("selectAll") }

    menuBar: MenuBar {
        Menu {
            title: qsTr("&File")
            delegate: shortcutItem
            Action { text: qsTr("&Open..."); shortcut: "Ctrl+O"; enabled: win.vm.searching; onTriggered: { openDialog.how = "open"; openDialog.open() } }
            Action { text: qsTr("&Save"); shortcut: "Ctrl+S"; enabled: win.vm.currentFile !== ""; onTriggered: win.vm.run("save") }
            Action { text: qsTr("Save As..."); shortcut: "Ctrl+Shift+S"; enabled: win.vm.searching; onTriggered: saveDialog.open() }
            Action { text: qsTr("&Append File..."); enabled: win.vm.searching; onTriggered: { openDialog.how = "append"; openDialog.open() } }
            Action { text: qsTr("&Truncate from File..."); enabled: win.vm.searching; onTriggered: { openDialog.how = "truncate"; openDialog.open() } }
            Menu {
                delegate: MenuEntry {}
                id: recentMenu
                title: qsTr("Recent")
                icon.source: "qrc:/chimera/icons/Recent.png"
                MenuItem { text: qsTr("None"); enabled: false; visible: win.vm.recentFiles === ""; height: visible ? implicitHeight : 0 }
                Instantiator {
                    model: win.vm.recentFiles === "" ? [] : win.vm.recentFiles.split("\n")
                    delegate: MenuItem {
                        required property string modelData
                        text: modelData
                        enabled: win.vm.searching
                        onTriggered: win.vm.run("open", [modelData, "open"])
                    }
                    onObjectAdded: (index, object) => recentMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => recentMenu.removeItem(object)
                }
            }
            MenuDivider {}
            Action { property string keysShown: "Alt+F4"; text: qsTr("&Close"); onTriggered: win.close() }
        }
        Menu {
            delegate: MenuEntry {}
            title: qsTr("&Options")
            Menu {
                delegate: MenuEntry {}
                title: qsTr("&Mode")
                MenuItem { text: qsTr("&Detailed"); checkable: true; checked: !win.vm.fast; onTriggered: win.vm.fast = false }
                MenuItem { text: qsTr("&Fast"); checkable: true; checked: win.vm.fast; onTriggered: win.vm.fast = true }
            }
            Menu {
                delegate: MenuEntry {}
                id: domainMenu
                title: qsTr("&Memory Domains")
                Instantiator {
                    model: win.vm.domains === "" ? [] : win.vm.domains.split(",")
                    delegate: MenuItem {
                        required property string modelData
                        text: modelData
                        checkable: true
                        checked: win.vm.domain === modelData
                        onTriggered: win.vm.domain = modelData
                    }
                    onObjectAdded: (index, object) => domainMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => domainMenu.removeItem(object)
                }
            }
            Menu {
                delegate: MenuEntry {}
                title: qsTr("&Size")
                MenuItem { text: qsTr("&1 Byte"); checkable: true; checked: win.vm.size === 1; onTriggered: win.vm.size = 1 }
                MenuItem { text: qsTr("&2 Byte"); checkable: true; checked: win.vm.size === 2; onTriggered: win.vm.size = 2 }
                MenuItem { text: qsTr("&4 Byte"); checkable: true; checked: win.vm.size === 4; onTriggered: win.vm.size = 4 }
            }
            MenuItem { text: qsTr("Check Mis-aligned"); checkable: true; checked: win.vm.checkMisaligned; onTriggered: win.vm.checkMisaligned = checked }
            MenuDivider {}
            MenuItem { text: qsTr("&Big Endian"); checkable: true; checked: win.vm.bigEndian; onTriggered: win.vm.bigEndian = checked }
            Menu {
                delegate: MenuEntry {}
                id: displayMenu
                title: qsTr("&Display Type")
                Instantiator {
                    model: win.vm.displays === "" ? [] : win.vm.displays.split("\n")
                    delegate: MenuItem {
                        required property string modelData
                        required property int index
                        text: modelData
                        checkable: true
                        checked: win.vm.display === index
                        onTriggered: win.vm.display = index
                    }
                    onObjectAdded: (index, object) => displayMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => displayMenu.removeItem(object)
                }
            }
            Menu {
                delegate: MenuEntry {}
                title: qsTr("Define Previous Value")
                MenuItem { text: qsTr("Last &Search"); checkable: true; checked: win.vm.previousType === 1; onTriggered: win.vm.previousType = 1 }
                MenuItem { text: qsTr("&Previous Frame"); checkable: true; checked: win.vm.previousType === 2; enabled: !win.vm.fast; onTriggered: win.vm.previousType = 2 }
                MenuItem { text: qsTr("&Original"); checkable: true; checked: win.vm.previousType === 0; onTriggered: win.vm.previousType = 0 }
                MenuItem { text: qsTr("Last &Change"); checkable: true; checked: win.vm.previousType === 3; enabled: !win.vm.fast; onTriggered: win.vm.previousType = 3 }
            }
        }
        Menu {
            title: qsTr("&Search")
            delegate: shortcutItem
            MenuItem { action: newSearchAction }
            MenuDivider {}
            MenuItem { action: searchAction }
            MenuItem { action: undoAction }
            MenuItem { action: redoAction }
            MenuItem { action: copyPrevAction }
            MenuItem { action: clearCountsAction }
            MenuItem { action: removeAction }
            MenuDivider {}
            MenuItem { action: goToAction }
            MenuItem { action: watchAction }
            MenuItem { action: pokeAction }
            MenuItem { action: freezeAction }
            MenuItem { action: selectAllAction }
            MenuDivider {}
            MenuItem { text: qsTr("Clear Undo History"); enabled: win.vm.canUndo; onTriggered: win.vm.run("clearUndo") }
        }
        Menu {
            delegate: MenuEntry {}
            title: qsTr("&Settings")
            MenuItem { text: qsTr("&Preview Mode"); checkable: true; checked: win.vm.previewMode; onTriggered: win.vm.previewMode = checked }
            MenuItem { text: qsTr("&Auto-Search"); checkable: true; checked: win.vm.autoSearch; onTriggered: win.vm.autoSearch = checked }
            MenuItem { text: qsTr("&Auto-Search Account for Lag"); checkable: true; checked: win.vm.autoSearchAccountForLag; onTriggered: win.vm.autoSearchAccountForLag = checked }
            MenuDivider {}
            MenuItem { text: qsTr("Always E&xclude RAM Watch List"); checkable: true; checked: win.vm.alwaysExcludeRamWatch; onTriggered: win.vm.alwaysExcludeRamWatch = checked }
            MenuItem { text: qsTr("&Use Undo History"); checkable: true; checked: win.vm.useUndoHistory; onTriggered: win.vm.useUndoHistory = checked }
            MenuDivider {}
            MenuItem { text: qsTr("Save Window &Position"); checkable: true; checked: windowOptions.savePosition; onTriggered: windowOptions.set("save", checked) }
            MenuItem {
                text: qsTr("Stay on &Top"); checkable: true; checked: windowOptions.onTop
                visible: windowOptions.canStayOnTop; height: visible ? implicitHeight : 0
                onTriggered: windowOptions.set("top", checked)
            }
            MenuItem { text: qsTr("&Float From Main Window"); checkable: true; checked: windowOptions.floating; onTriggered: windowOptions.set("float", checked) }
            MenuItem { text: qsTr("Restore &Defaults"); onTriggered: { win.vm.run("restoreDefaults"); windowOptions.restore() } }
        }
        Menu {
            delegate: MenuEntry {}
            id: columnsMenu
            title: qsTr("Columns")
            Instantiator {
                model: win.columns
                delegate: MenuItem {
                    required property var modelData
                    text: modelData.text + " (" + modelData.name + "Column)"
                    checkable: true
                    checked: win.shown(modelData.key)
                    onTriggered: win.showColumn(modelData.key, checked)
                }
                onObjectAdded: (index, object) => columnsMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => columnsMenu.removeItem(object)
            }
        }
    }

    // Chimera's columns: its key (the view model's sort name), heading, width and the row model's field
    readonly property var columns: [
        {key: "Address", name: "Address", text: "Address", width: 78, field: "Address"},
        {key: "Value", name: "Value", text: "Value", width: 77, field: "Value"},
        {key: "Prev", name: "Prev", text: "Prev", width: 77, field: "Previous"},
        {key: "Changes", name: "Changes", text: "Changes", width: 78, field: "Changes"},
        {key: "Diff", name: "Diff", text: "Diff", width: 77, field: "Diff"}
    ]
    readonly property var hidden: win.vm.hiddenColumns === "" ? [] : win.vm.hiddenColumns.split(",")
    function shown(key) { return win.hidden.indexOf(key) < 0 }
    function showColumn(key, on) {
        const now = win.hidden.filter((k) => k !== key)
        if (!on) now.push(key)
        win.vm.hiddenColumns = now.join(",")
    }
    readonly property var visibleColumns: win.columns.filter((c) => win.shown(c.key))

    header: ToolBar {
        RowLayout {
            spacing: 0
            component Tool: ToolButton {
                property string tip: action ? action.text.replace(/&/g, "") : ""
                display: AbstractButton.IconOnly
                ToolTip.visible: hovered
                ToolTip.text: tip
                ToolTip.delay: 500
            }
            Tool { action: searchAction; display: AbstractButton.TextBesideIcon; text: qsTr("Search ") }
            ToolSeparator {}
            Tool { action: newSearchAction; display: AbstractButton.TextBesideIcon; text: qsTr("New") }
            ToolSeparator {}
            Tool { action: copyPrevAction; tip: qsTr("Copy Value to Previous") }
            ToolButton {
                text: qsTr("C"); enabled: clearCountsAction.enabled; onClicked: clearCountsAction.trigger()
                ToolTip.visible: hovered; ToolTip.text: qsTr("Clear Change Counts"); ToolTip.delay: 500
            }
            ToolSeparator {}
            Tool { action: removeAction; tip: qsTr("Cut") }
            Tool { action: watchAction; tip: qsTr("Watch") }
            Tool { action: pokeAction; tip: qsTr("Poke") }
            Tool { action: freezeAction; tip: qsTr("Freeze") }
            ToolSeparator {}
            Tool { action: undoAction; tip: qsTr("Undo Search") }
            Tool { action: redoAction; tip: qsTr("Redo") }
            ToolSeparator { visible: win.vm.rebootNeeded }
            ToolButton {
                objectName: "ramSearchReboot"
                visible: win.vm.rebootNeeded
                icon.source: "qrc:/chimera/icons/reboot.png"
                display: AbstractButton.IconOnly
                ToolTip.visible: hovered
                ToolTip.text: qsTr("A new search needs to be started in order for these changes to take effect")
                onClicked: win.vm.run("newSearch")
            }
            ToolButton {
                objectName: "ramSearchOutOfRange"
                visible: win.vm.outOfRange > 0
                icon.source: "qrc:/chimera/icons/ExclamationRed.png"
                display: AbstractButton.IconOnly
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Warning! Out of Range Addresses in list, click to remove them")
                onClicked: win.vm.run("removeOutOfRange")
            }
        }
    }

    footer: Label {
        objectName: "ramSearchMessage"
        text: win.vm.error !== "" ? win.vm.error : win.vm.status
        padding: 4
        elide: Text.ElideRight
    }

    // the list's right-click menu
    Menu {
        id: listMenu
        delegate: shortcutItem
        MenuItem { action: searchAction }
        Action { text: qsTr("&Start New Search"); onTriggered: win.vm.run("newSearch") }
        MenuDivider {}
        Action { property string keysShown: "Del"; text: qsTr("Remove Selected"); enabled: win.anyRow; onTriggered: win.vm.run("removeSelected") }
        Action { property string keysShown: "Ctrl+W"; text: qsTr("Add to RAM Watch"); enabled: win.anyRow; onTriggered: win.addToRamWatch() }
        Action { property string keysShown: "Ctrl+P"; text: qsTr("Poke Address"); enabled: win.vm.canPoke; onTriggered: win.pokeAddress() }
        MenuItem {
            action: Action {
                property string keysShown: "Ctrl+F"
                text: win.vm.selectionFrozen ? qsTr("&Unfreeze Address") : qsTr("&Freeze Address")
                icon.source: win.vm.selectionFrozen ? "qrc:/chimera/icons/Unfreeze.png" : "qrc:/chimera/icons/Freeze.png"
                onTriggered: win.onPicked(qsTr("Freeze or unfreeze"), () => win.vm.run("toggleFreeze"))
            }
            visible: win.vm.canPoke; height: visible ? implicitHeight : 0
        }
        MenuItem {
            text: qsTr("Unfreeze &All"); icon.source: "qrc:/chimera/icons/Unfreeze.png"
            visible: win.vm.anyFrozen; height: visible ? implicitHeight : 0
            onTriggered: win.vm.run("unfreezeAll")
        }
        MenuDivider {}
        MenuItem {
            text: qsTr("View in Hex Editor")
            visible: win.anyRow && win.viewInHexEditor !== null; height: visible ? implicitHeight : 0
            onTriggered: win.onPicked(qsTr("Open in the Hex Editor"), () => {
                const info = JSON.parse(win.vm.run("pokeInfo"))
                win.viewInHexEditor(info.domain, parseInt(info.addresses.split(",")[0], 16))
            })
        }
        MenuDivider {}
        Action { text: qsTr("&Clear Preview"); onTriggered: win.vm.run("clearPreview") }
    }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal

        ColumnLayout {
            SplitView.fillWidth: true
            SplitView.minimumWidth: 200
            spacing: 0
            RowLayout {
                Label { objectName: "ramSearchTotal"; text: win.vm.totalText; padding: 4 }
                Item { Layout.fillWidth: true }
                Label { text: win.vm.searchedDomain; padding: 4 }
            }
            // the headings: a click sorts by the column, again the other way round
            Row {
                Layout.fillWidth: true
                Repeater {
                    model: win.visibleColumns
                    delegate: Button {
                        required property var modelData
                        required property int index
                        text: modelData.text
                        flat: true
                        padding: 2
                        width: index === win.visibleColumns.length - 1 ? Math.max(modelData.width, list.width - x) : modelData.width
                        onClicked: win.vm.run("sort", [modelData.key])
                    }
                }
            }
            ListView {
                id: list
                objectName: "ramSearchList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                focus: true
                model: win.vm.rows("results")
                ScrollBar.vertical: ScrollBar {}
                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_Delete) { win.vm.run("removeSelected"); event.accepted = true }
                    else if (event.matches(StandardKey.Copy)) { win.copyRows(); event.accepted = true }
                }
                delegate: Rectangle {
                    id: row
                    required property int index
                    required property var model
                    width: list.width
                    height: 22
                    color: row.model.selected ? palette.highlight
                         : row.model.invalid ? win.themeColor("RowInvalid", "#FFDAB9")
                         : row.model.frozen ? (row.model.preview ? win.themeColor("RowExcludedActive", "#E6E6FA") : win.themeColor("RowActive", "#E0FFFF"))
                         : row.model.preview ? win.themeColor("RowExcluded", "#FFC0CB")
                         : win.themeColor("RowDefault", palette.base)
                    Row {
                        anchors.fill: parent
                        Repeater {
                            model: win.visibleColumns
                            delegate: Label {
                                required property var modelData
                                required property int index
                                width: index === win.visibleColumns.length - 1 ? Math.max(modelData.width, list.width - x) : modelData.width
                                height: row.height
                                leftPadding: 3
                                verticalAlignment: Text.AlignVCenter
                                text: { const v = row.model[modelData.field]; return v === undefined || v === null ? "" : String(v) }
                                font.family: "monospace"
                                elide: Text.ElideRight
                                color: row.model.selected ? palette.highlightedText : palette.text
                            }
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onPressed: (mouse) => {
                            list.forceActiveFocus()
                            if (mouse.button === Qt.RightButton) {
                                if (!row.model.selected) win.vm.run("select", [row.index, "only"])
                                listMenu.popup()
                                return
                            }
                            win.vm.run("select", [row.index, (mouse.modifiers & Qt.ShiftModifier) ? "range"
                                : (mouse.modifiers & Qt.ControlModifier) ? "toggle" : "only"])
                        }
                    }
                }
                MouseArea {
                    z: -1
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onPressed: (mouse) => {
                        list.forceActiveFocus()
                        win.vm.run("select", [-1, "none"])
                        if (mouse.button === Qt.RightButton) listMenu.popup()
                    }
                }
            }
        }

        ColumnLayout {
            SplitView.preferredWidth: 280
            SplitView.minimumWidth: 250
            GroupBox {
                title: qsTr("Compare To / By")
                Layout.fillWidth: true
                GridLayout {
                    columns: 2
                    rowSpacing: 0
                    RadioButton { text: qsTr("Previous Value"); checked: win.vm.compare === 0; onClicked: win.vm.compare = 0; Layout.columnSpan: 2 }
                    RadioButton { text: qsTr("Specific Value:"); checked: win.vm.compare === 1; onClicked: win.vm.compare = 1 }
                    TextField {
                        objectName: "specificValueBox"; Layout.preferredWidth: 70; enabled: win.vm.compare === 1; font.family: "monospace"
                        text: win.vm.specificValue; onTextEdited: win.vm.specificValue = text
                    }
                    RadioButton { text: qsTr("Specific Address:"); checked: win.vm.compare === 2; onClicked: win.vm.compare = 2 }
                    TextField {
                        Layout.preferredWidth: 70; enabled: win.vm.compare === 2; font.family: "monospace"
                        text: win.vm.specificAddress; onTextEdited: win.vm.specificAddress = text
                    }
                    RadioButton { text: qsTr("Specific Changes:"); checked: win.vm.compare === 3; enabled: !win.vm.fast; onClicked: win.vm.compare = 3 }
                    TextField {
                        Layout.preferredWidth: 70; enabled: win.vm.compare === 3 && !win.vm.fast; font.family: "monospace"
                        text: win.vm.specificChanges; onTextEdited: win.vm.specificChanges = text
                    }
                    RadioButton { text: qsTr("Difference of:"); checked: win.vm.compare === 4; enabled: !win.vm.fast; onClicked: win.vm.compare = 4 }
                    TextField {
                        Layout.preferredWidth: 70; enabled: win.vm.compare === 4; font.family: "monospace"
                        text: win.vm.difference; onTextEdited: win.vm.difference = text
                    }
                }
            }
            GroupBox {
                title: qsTr("Comparison Operator")
                Layout.fillWidth: true
                GridLayout {
                    columns: 2
                    rowSpacing: 0
                    // Chimera's order, top to bottom
                    RadioButton { text: qsTr("Equal To"); checked: win.vm.op === 0; onClicked: win.vm.op = 0; Layout.columnSpan: 2 }
                    RadioButton { text: qsTr("Not Equal To"); checked: win.vm.op === 5; onClicked: win.vm.op = 5; Layout.columnSpan: 2 }
                    RadioButton { text: qsTr("Less Than"); checked: win.vm.op === 3; onClicked: win.vm.op = 3; Layout.columnSpan: 2 }
                    RadioButton { text: qsTr("Greater Than"); checked: win.vm.op === 1; onClicked: win.vm.op = 1; Layout.columnSpan: 2 }
                    RadioButton { text: qsTr("Less Than or Equal To"); checked: win.vm.op === 4; onClicked: win.vm.op = 4; Layout.columnSpan: 2 }
                    RadioButton { text: qsTr("Greater Than or Equal To"); checked: win.vm.op === 2; onClicked: win.vm.op = 2; Layout.columnSpan: 2 }
                    RadioButton { text: qsTr("Different by:"); checked: win.vm.op === 6; onClicked: win.vm.op = 6 }
                    TextField {
                        Layout.preferredWidth: 70; enabled: win.vm.op === 6; font.family: "monospace"
                        text: win.vm.differentBy; onTextEdited: win.vm.differentBy = text
                    }
                }
            }
            GridLayout {
                columns: 2
                Label { text: qsTr("Size") }
                Label { text: qsTr("Display") }
                ComboBox {
                    Layout.fillWidth: true
                    model: [qsTr("1 Byte"), qsTr("2 Byte"), qsTr("4 Byte")]
                    currentIndex: win.vm.size === 4 ? 2 : win.vm.size - 1
                    onActivated: (i) => win.vm.size = [1, 2, 4][i]
                }
                ComboBox {
                    Layout.fillWidth: true
                    model: win.vm.displays === "" ? [] : win.vm.displays.split("\n")
                    currentIndex: win.vm.display
                    onActivated: (i) => win.vm.display = i
                }
            }
            RowLayout {
                Button { text: qsTr("&Search"); highlighted: true; enabled: searchAction.enabled; onClicked: win.vm.run("search") }
                CheckBox { text: qsTr("Auto Search"); checked: win.vm.autoSearch; onToggled: win.vm.autoSearch = checked }
            }
            Item { Layout.fillHeight: true }
        }
    }

    RamPoke { id: poke; vm: win.vm; tell: win.tell }

    // Go to Address: a candidate's row picked, asked again until one is found, as Chimera's
    ApplicationWindow {
        id: goTo
        title: qsTr("Go to Address")
        width: 280
        height: 120
        modality: Qt.ApplicationModal
        function open() { goToText.text = ""; goTo.show(); goTo.raise(); goToText.forceActiveFocus() }
        function accept() {
            const row = win.vm.run("goTo", [goToText.text])
            if (row >= 0) { list.positionViewAtIndex(row, ListView.Center); goTo.close() }
            else goToText.selectAll()
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            Label { text: qsTr("Enter a hexadecimal value") }
            TextField { id: goToText; Layout.fillWidth: true; font.family: "monospace"; onAccepted: goTo.accept() }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Button { text: qsTr("OK"); onClicked: goTo.accept() }
                Button { text: qsTr("Cancel"); onClicked: goTo.close() }
            }
        }
    }
}
