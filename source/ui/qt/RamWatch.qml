// RAM Watch, as Chimera's: its menus (File, Watches, Settings, Columns), its
// toolbar, the count of watches over the list, the list with its columns,
// and the status line with the warning for addresses past their domain's
// end. New, Edit and Duplicate Watch share one editor window; RAM Poke,
// Edit Separator and Add Game Properties are windows of their own. Layout
// and bindings only, over the ramWatch view model.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "ramWatchWindow"
    required property QtObject vm
    // the main window's: Paths folders, the clipboard, theme colours, a game core's properties, the hex editor
    property var folderOf: null
    property QtObject clipboard: null
    property var themeColor: (role, fallback) => fallback
    property QtObject customize: null
    property QtObject toolWindows: null
    property Window mainWindow: null
    property QtObject gameProperties: null
    property var viewInHexEditor: null
    title: qsTr("RAM Watch")
    width: 560
    height: 480

    WindowOptions { id: windowOptions; window: win; tool: "ramWatch"; vm: win.toolWindows; mainWindow: win.mainWindow }
    onVisibleChanged: if (visible) windowOptions.placeBack(false)
    function menuOpened() { if (win.customize) win.customize.run("menuOpened") }
    function menuClosed() { if (win.customize) win.customize.run("menuClosed") }

    // Chimera asks before a list with changes is let go
    property var afterAsk: null
    property bool closingAsked: false
    function leaveList(then) {
        if (!win.vm.changed) { then(); return }
        win.afterAsk = then
        askSave.open()
    }
    MessageDialog {
        id: askSave
        title: qsTr("Closing with Unsaved Changes")
        text: qsTr("Save watch file?")
        buttons: MessageDialog.Yes | MessageDialog.No | MessageDialog.Cancel
        onButtonClicked: (button) => {
            const then = win.afterAsk
            win.afterAsk = null
            if (button === MessageDialog.Cancel) return
            if (button === MessageDialog.Yes) {
                if (win.vm.path === "") { saveDialog.afterSave = then; saveDialog.open(); return }
                win.vm.run("save")
            }
            if (then) then()
        }
    }
    onClosing: (close) => {
        if (win.closingAsked) { win.closingAsked = false; return }
        if (win.vm.changed) {
            close.accepted = false
            win.leaveList(() => { win.closingAsked = true; win.close() })
        }
    }

    FileDialog {
        id: openDialog
        title: qsTr("Open a watch list")
        property bool append: false
        currentFolder: win.folderOf ? win.folderOf("Watch (.wch)") : ""
        nameFilters: [qsTr("Watch Files (*.wch)"), qsTr("All files (*)")]
        onAccepted: win.vm.run("open", [selectedFile, openDialog.append])
    }
    FileDialog {
        id: saveDialog
        title: qsTr("Save the watch list as")
        property var afterSave: null
        fileMode: FileDialog.SaveFile
        defaultSuffix: "wch"
        currentFolder: win.folderOf ? win.folderOf("Watch (.wch)") : ""
        nameFilters: [qsTr("Watch Files (*.wch)")]
        onAccepted: {
            win.vm.run("save", [selectedFile])
            const then = saveDialog.afterSave
            saveDialog.afterSave = null
            if (then && !win.vm.changed) then()
        }
        onRejected: saveDialog.afterSave = null
    }
    MessageDialog {
        id: notice
        buttons: MessageDialog.Ok
    }
    function tell(title, text) { notice.title = title; notice.text = text; notice.open() }
    MessageDialog {
        id: purgeAsk
        title: qsTr("Purge duplicates?")
        text: qsTr("This will go through each watch starting from the top,\nremoving it if its (domain & addr & size) has been seen before.\nNeither the endianness nor display type are considered.\nOverlapping watches also aren't considered.")
        buttons: MessageDialog.Ok | MessageDialog.Cancel
        onButtonClicked: (button) => { if (button === MessageDialog.Ok) win.vm.run("purgeDuplicates") }
    }

    // what the rows picked allow, as Chimera's menus ask it
    readonly property bool anyRow: win.vm.selectedCount > 0
    readonly property bool anyWatch: win.vm.selectedWatches > 0
    function editWatch(duplicate) {
        const info = JSON.parse(win.vm.run("editInfo", [duplicate ? "duplicate" : "edit"]))
        if (win.vm.selectedWatches === 0) {
            // separators picked: their text, as Chimera's Edit Separator asks it
            if (!duplicate && win.vm.selectedCount > 0) separatorPrompt.open()
            return
        }
        if (info.property) {
            win.tell(qsTr("Game property"), qsTr("A game property's watch reads the property itself, by its name, so there is nothing of it to edit. Poke sets its value; Add Game Properties adds another."))
            return
        }
        editor.start(duplicate ? "duplicate" : "edit", info)
    }
    function pokeWatch() {
        if (!win.anyWatch) return
        const info = JSON.parse(win.vm.run("pokeInfo"))
        if (info.mixed) { win.tell(qsTr("Unsupported Options"), qsTr("RAM Poke does not support mixed types")); return }
        poke.start(info)
    }
    // On Double-Clicking a Watch, and Enter
    function openWatch() { if (win.vm.doubleClickToPoke) win.pokeWatch(); else win.editWatch(false) }
    function copyWatches() { const t = win.vm.run("copySelected"); if (t && win.clipboard) win.clipboard.setText(t) }
    function pasteWatches() { if (win.clipboard) win.vm.run("paste", [win.clipboard.text()]) }

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

    Action { id: newListAction; text: qsTr("&New List"); icon.source: "qrc:/chimera/icons/NewFile.png"; shortcut: "Ctrl+N"; onTriggered: win.leaveList(() => win.vm.run("newList")) }
    Action { id: openAction; text: qsTr("&Open..."); icon.source: "qrc:/chimera/icons/OpenFile.png"; shortcut: "Ctrl+O"; onTriggered: win.leaveList(() => { openDialog.append = false; openDialog.open() }) }
    Action {
        id: saveAction; text: qsTr("&Save"); icon.source: "qrc:/chimera/icons/SaveAs.png"; shortcut: "Ctrl+S"
        onTriggered: win.vm.path === "" ? saveDialog.open() : win.vm.run("save")
    }
    Action { id: newWatchAction; text: qsTr("&New Watch"); icon.source: "qrc:/chimera/icons/addWatch.ico"; shortcut: "Ctrl+W"; onTriggered: editor.start("new", JSON.parse(win.vm.run("editInfo", ["new"]))) }
    Action { id: editAction; text: qsTr("&Edit Watch"); icon.source: "qrc:/chimera/icons/Cut.png"; shortcut: "Ctrl+E"; enabled: win.anyRow; onTriggered: win.editWatch(false) }
    Action { id: removeAction; text: qsTr("&Remove Watch"); icon.source: "qrc:/chimera/icons/Delete.png"; shortcut: "Ctrl+R"; enabled: win.anyRow; onTriggered: win.vm.run("removeSelected") }
    Action { id: duplicateAction; text: qsTr("&Duplicate Watch"); icon.source: "qrc:/chimera/icons/Duplicate.png"; shortcut: "Ctrl+D"; enabled: win.anyWatch; onTriggered: win.editWatch(true) }
    Action { id: splitAction; text: qsTr("Sp&lit Watch"); icon.source: "qrc:/chimera/icons/Split.png"; shortcut: "Ctrl+L"; enabled: win.vm.canSplit; onTriggered: win.vm.run("splitSelected") }
    Action { id: pokeAction; text: qsTr("Poke Address"); icon.source: "qrc:/chimera/icons/poke.png"; shortcut: "Ctrl+P"; enabled: win.vm.canPoke; onTriggered: win.pokeWatch() }
    // frozen addresses are cheats, which wait on the owner (question 5)
    Action { id: freezeAction; text: qsTr("Freeze Address"); icon.source: "qrc:/chimera/icons/Freeze.png"; shortcut: "Ctrl+F"; enabled: false }
    Action { id: separatorAction; text: qsTr("Insert Separator"); icon.source: "qrc:/chimera/icons/InsertSeparator.png"; shortcut: "Ctrl+I"; onTriggered: win.vm.run("insertSeparator") }
    Action { id: clearCountsAction; text: qsTr("&Clear Change Counts"); shortcut: "Alt+C"; onTriggered: win.vm.run("clearChangeCounts") }
    Action { id: upAction; text: qsTr("Move &Up"); icon.source: "qrc:/chimera/icons/MoveUp.png"; shortcut: "Ctrl+Up"; enabled: win.anyRow; onTriggered: win.vm.run("moveSelected", ["up"]) }
    Action { id: downAction; text: qsTr("Move &Down"); icon.source: "qrc:/chimera/icons/MoveDown.png"; shortcut: "Ctrl+Down"; enabled: win.anyRow; onTriggered: win.vm.run("moveSelected", ["down"]) }
    Action { id: topAction; text: qsTr("Move &Top"); icon.source: "qrc:/chimera/icons/MoveTop.png"; shortcut: "Ctrl+Shift+Up"; enabled: win.anyRow; onTriggered: win.vm.run("moveSelected", ["top"]) }
    Action { id: bottomAction; text: qsTr("Move &Bottom"); icon.source: "qrc:/chimera/icons/MoveBottom.png"; shortcut: "Ctrl+Shift+Down"; enabled: win.anyRow; onTriggered: win.vm.run("moveSelected", ["bottom"]) }
    Action { id: selectAllAction; text: qsTr("Select &All"); shortcut: "Ctrl+A"; onTriggered: win.vm.run("selectAll") }

    menuBar: MenuBar {
        Menu {
            title: qsTr("&Files")
            delegate: shortcutItem
            onAboutToShow: win.menuOpened()
            onAboutToHide: win.menuClosed()
            MenuItem { action: newListAction }
            MenuItem { action: openAction }
            MenuItem { action: saveAction; enabled: win.vm.changed }
            Action { text: qsTr("Save &As..."); shortcut: "Ctrl+Shift+S"; onTriggered: saveDialog.open() }
            Action { text: qsTr("A&ppend File..."); onTriggered: { openDialog.append = true; openDialog.open() } }
            Menu {
                id: recentMenu
                title: qsTr("Recent")
                icon.source: "qrc:/chimera/icons/Recent.png"
                MenuItem { text: qsTr("None"); enabled: false; visible: win.vm.recentFiles === ""; height: visible ? implicitHeight : 0 }
                Instantiator {
                    model: win.vm.recentFiles === "" ? [] : win.vm.recentFiles.split("\n")
                    delegate: MenuItem {
                        required property string modelData
                        text: modelData
                        onTriggered: win.leaveList(() => win.vm.run("open", [modelData, false]))
                    }
                    onObjectAdded: (index, object) => recentMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => recentMenu.removeItem(object)
                }
            }
            MenuSeparator {}
            Action { property string keysShown: "Alt+F4"; text: qsTr("&Close"); onTriggered: win.close() }
        }
        Menu {
            title: qsTr("&Watches")
            delegate: shortcutItem
            onAboutToShow: win.menuOpened()
            onAboutToHide: win.menuClosed()
            Menu {
                id: domainMenu
                title: qsTr("Default Domain")
                Instantiator {
                    model: win.vm.domains === "" ? [] : win.vm.domains.split(",")
                    delegate: MenuItem {
                        required property string modelData
                        text: modelData
                        checkable: true
                        checked: win.vm.defaultDomain === modelData
                        onTriggered: win.vm.defaultDomain = modelData
                    }
                    onObjectAdded: (index, object) => domainMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => domainMenu.removeItem(object)
                }
            }
            MenuSeparator {}
            MenuItem { action: newWatchAction }
            // only a game core has properties to offer
            MenuItem {
                text: qsTr("Add Game &Properties...")
                visible: propertyPicker.names.length > 0; height: visible ? implicitHeight : 0
                onTriggered: propertyPicker.open()
            }
            MenuItem { action: editAction }
            MenuItem { action: removeAction }
            MenuItem { action: duplicateAction }
            MenuItem { action: splitAction }
            MenuItem { action: pokeAction }
            MenuItem { action: freezeAction }
            MenuItem { action: separatorAction }
            MenuItem { action: clearCountsAction }
            MenuItem { text: qsTr("Purge Duplicate Watches"); onTriggered: purgeAsk.open() }
            MenuSeparator {}
            MenuItem { action: upAction }
            MenuItem { action: downAction }
            MenuItem { action: topAction }
            MenuItem { action: bottomAction }
            MenuItem { action: selectAllAction }
        }
        Menu {
            title: qsTr("&Settings")
            onAboutToShow: win.menuOpened()
            onAboutToHide: win.menuClosed()
            Menu {
                title: qsTr("Define Previous Value")
                MenuItem { text: qsTr("Previous Frame"); checkable: true; checked: win.vm.previousType === 2; onTriggered: win.vm.previousType = 2 }
                MenuItem { text: qsTr("Last Change"); checkable: true; checked: win.vm.previousType === 3; onTriggered: win.vm.previousType = 3 }
                MenuItem { text: qsTr("&Original"); checkable: true; checked: win.vm.previousType === 0; onTriggered: win.vm.previousType = 0 }
            }
            MenuItem { text: qsTr("Display Watches On Screen"); checkable: true; checked: win.vm.showOnScreen; onTriggered: win.vm.showOnScreen = checked }
            Menu {
                title: qsTr("On Double-Clicking a Watch")
                MenuItem { text: qsTr("Edit Watch"); checkable: true; checked: !win.vm.doubleClickToPoke; onTriggered: win.vm.doubleClickToPoke = false }
                MenuItem { text: qsTr("Poke Address"); checkable: true; checked: win.vm.doubleClickToPoke; onTriggered: win.vm.doubleClickToPoke = true }
            }
            MenuSeparator {}
            MenuItem { text: qsTr("Save Window &Position"); checkable: true; checked: windowOptions.savePosition; onTriggered: windowOptions.set("save", checked) }
            MenuItem {
                text: qsTr("Stay on &Top"); checkable: true; checked: windowOptions.onTop
                visible: windowOptions.canStayOnTop; height: visible ? implicitHeight : 0
                onTriggered: windowOptions.set("top", checked)
            }
            MenuItem { text: qsTr("&Float From Main Window"); checkable: true; checked: windowOptions.floating; onTriggered: windowOptions.set("float", checked) }
            MenuItem {
                text: qsTr("Restore &Defaults")
                // RAM Watch's own: its columns as at first, watches shown on screen, a double click pokes
                onTriggered: {
                    win.vm.hiddenColumns = "Prev,Diff,Type"
                    win.vm.showOnScreen = true
                    win.vm.doubleClickToPoke = true
                    windowOptions.restore()
                }
            }
        }
        Menu {
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
        {key: "OSD", name: "OnScreen", text: "OSD", width: 34, field: "onScreen"},
        {key: "Address", name: "Address", text: "Address", width: 78, field: "address"},
        {key: "Value", name: "Value", text: "Value", width: 77, field: "value"},
        {key: "Prev", name: "Prev", text: "Prev", width: 77, field: "previous"},
        {key: "Changes", name: "Changes", text: "Changes", width: 78, field: "changes"},
        {key: "Diff", name: "Diff", text: "Diff", width: 77, field: "diff"},
        {key: "Type", name: "Type", text: "Type", width: 72, field: "type"},
        {key: "Domain", name: "Domain", text: "Domain", width: 72, field: "domain"},
        {key: "Notes", name: "Notes", text: "Notes", width: 166, field: "note"}
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
                property string tip: action ? action.text.replace("&", "") : ""
                display: AbstractButton.IconOnly
                ToolTip.visible: hovered
                ToolTip.text: tip
                ToolTip.delay: 500
            }
            Tool { action: newListAction; tip: qsTr("New") }
            Tool { action: openAction; tip: qsTr("Open") }
            Tool { action: saveAction; tip: qsTr("Save") }
            ToolSeparator {}
            Tool { action: newWatchAction; tip: qsTr("New Watch") }
            Tool { action: editAction; tip: qsTr("Edit Watch") }
            Tool { action: removeAction; tip: qsTr("Cut") }
            // Chimera's own "C" button
            ToolButton {
                text: qsTr("C"); onClicked: win.vm.run("clearChangeCounts")
                ToolTip.visible: hovered; ToolTip.text: qsTr("Clear Change Counts"); ToolTip.delay: 500
            }
            Tool { action: duplicateAction; tip: qsTr("Duplicate Watch") }
            Tool { action: splitAction; tip: qsTr("Split Watch") }
            Tool { action: pokeAction }
            Tool { action: freezeAction }
            Tool { action: separatorAction; tip: qsTr("Separator") }
            ToolSeparator {}
            Tool { action: upAction; tip: qsTr("Move Up") }
            Tool { action: downAction; tip: qsTr("Move Down") }
        }
    }

    footer: RowLayout {
        spacing: 4
        ToolButton {
            objectName: "ramWatchInvalid"
            visible: win.vm.invalidCount > 0
            icon.source: "qrc:/chimera/icons/ExclamationRed.png"
            display: AbstractButton.IconOnly
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Warning! Out of Range Addresses in list, click to remove them")
            onClicked: win.vm.run("removeInvalid")
        }
        Label {
            Layout.fillWidth: true
            padding: 4
            text: win.vm.error !== "" ? win.vm.error : win.vm.status
            elide: Text.ElideRight
        }
    }

    // the list's right-click menu
    Menu {
        id: listMenu
        delegate: shortcutItem
        onAboutToShow: win.menuOpened()
        onAboutToHide: win.menuClosed()
        MenuItem { action: newWatchAction; visible: !win.anyRow; height: visible ? implicitHeight : 0 }
        Action { property string keysShown: "Ctrl+E"; text: qsTr("&Edit"); icon.source: "qrc:/chimera/icons/Cut.png"; enabled: win.anyRow; onTriggered: win.editWatch(false) }
        Action { property string keysShown: "Ctrl+R"; text: qsTr("&Remove"); icon.source: "qrc:/chimera/icons/Delete.png"; enabled: win.anyRow; onTriggered: win.vm.run("removeSelected") }
        Action { property string keysShown: "Ctrl+D"; text: qsTr("&Duplicate"); icon.source: "qrc:/chimera/icons/Duplicate.png"; enabled: win.anyWatch; onTriggered: win.editWatch(true) }
        Action { property string keysShown: "Ctrl+L"; text: qsTr("Sp&lit"); icon.source: "qrc:/chimera/icons/Split.png"; enabled: win.vm.canSplit; onTriggered: win.vm.run("splitSelected") }
        Action { property string keysShown: "Ctrl+P"; text: qsTr("&Poke"); icon.source: "qrc:/chimera/icons/poke.png"; enabled: win.vm.canPoke; onTriggered: win.pokeWatch() }
        MenuItem {
            text: qsTr("View in Hex Editor")
            visible: win.vm.selectedWatches === 1 && win.viewInHexEditor !== null; height: visible ? implicitHeight : 0
            onTriggered: {
                const info = JSON.parse(win.vm.run("editInfo", ["edit"]))
                win.viewInHexEditor(info.domain, parseInt(info.address, 16))
            }
        }
        MenuSeparator {}
        Action { property string keysShown: "Ctrl+I"; text: qsTr("&Insert Separator"); icon.source: "qrc:/chimera/icons/InsertSeparator.png"; onTriggered: win.vm.run("insertSeparator") }
        Action { property string keysShown: "Ctrl+Up"; text: qsTr("Move &Up"); icon.source: "qrc:/chimera/icons/MoveUp.png"; enabled: win.anyRow; onTriggered: win.vm.run("moveSelected", ["up"]) }
        Action { property string keysShown: "Ctrl+Down"; text: qsTr("Move &Down"); icon.source: "qrc:/chimera/icons/MoveDown.png"; enabled: win.anyRow; onTriggered: win.vm.run("moveSelected", ["down"]) }
        MenuItem { action: topAction }
        MenuItem { action: bottomAction }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Label { objectName: "ramWatchCount"; text: win.vm.countText; padding: 4 }
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
            objectName: "ramWatchList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            focus: true
            model: win.vm.rows("watches")
            ScrollBar.vertical: ScrollBar {}
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Delete) { win.vm.run("removeSelected"); event.accepted = true }
                else if (event.matches(StandardKey.Copy)) { win.copyWatches(); event.accepted = true }
                else if (event.matches(StandardKey.Paste)) { win.pasteWatches(); event.accepted = true }
                else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) { win.openWatch(); event.accepted = true }
            }
            delegate: Rectangle {
                id: row
                required property int index
                required property var model
                readonly property bool separator: row.model.separator === true
                width: list.width
                height: 22
                color: row.model.selected ? palette.highlight
                     : row.separator ? palette.window
                     : row.model.invalid ? win.themeColor("RowInvalid", "#FFDAB9")
                     : win.themeColor("RowDefault", palette.base)
                Row {
                    anchors.fill: parent
                    visible: !row.separator
                    Repeater {
                        model: win.visibleColumns
                        delegate: Item {
                            required property var modelData
                            required property int index
                            width: index === win.visibleColumns.length - 1 ? Math.max(modelData.width, list.width - x) : modelData.width
                            height: row.height
                            CheckBox {
                                visible: modelData.key === "OSD"
                                anchors.centerIn: parent
                                checked: row.model.onScreen === true
                                padding: 0
                                onClicked: win.vm.run("setOnScreen", [row.index, checked])
                            }
                            Label {
                                visible: modelData.key !== "OSD"
                                anchors.fill: parent
                                leftPadding: 3
                                verticalAlignment: Text.AlignVCenter
                                text: { const v = row.model[modelData.field]; return v === undefined || v === null ? "" : String(v) }
                                font.family: modelData.key === "Notes" || modelData.key === "Domain" ? Qt.application.font.family : "monospace"
                                elide: Text.ElideRight
                                color: row.model.selected ? palette.highlightedText : palette.text
                            }
                        }
                    }
                }
                // a separator's text, in the Address column and past it
                Label {
                    visible: row.separator
                    x: 3
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.model.note
                    font.bold: true
                    color: row.model.selected ? palette.highlightedText : palette.text
                }
                MouseArea {
                    anchors.fill: parent
                    // the OSD box takes its own clicks
                    anchors.leftMargin: win.shown("OSD") ? 34 : 0
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
                    onDoubleClicked: (mouse) => { if (mouse.button === Qt.LeftButton) win.openWatch() }
                }
            }
            // a click below the rows lets every row go; a right click there opens the menu
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

    // New Watch, Edit Watch(es), Duplicate Watch: Chimera's one editor
    ApplicationWindow {
        id: editor
        objectName: "watchEditorWindow"
        property string mode: "new"
        property var info: ({})
        property var displays: []
        title: editor.mode === "new" ? qsTr("New Watch") : editor.mode === "duplicate" ? qsTr("Duplicate Watch")
             : editor.info.many ? qsTr("Edit Watches") : qsTr("Edit Watch")
        width: 300
        height: 260
        modality: Qt.ApplicationModal
        function start(mode, info) {
            editor.mode = mode
            editor.info = info
            domainBox.model = win.vm.domains === "" ? [] : win.vm.domains.split(",")
            domainBox.currentIndex = Math.max(0, domainBox.model.indexOf(info.domain))
            addressBox.text = info.address
            sizeBox.currentIndex = info.size === 4 ? 2 : info.size - 1
            editor.fillDisplays(info.display)
            bigEndianBox.checkState = info.bigEndian === 2 ? Qt.PartiallyChecked : info.bigEndian === 1 ? Qt.Checked : Qt.Unchecked
            notesBox.text = info.note
            editor.show()
            editor.raise()
            notesBox.forceActiveFocus()
        }
        function fillDisplays(keep) {
            editor.displays = JSON.parse(win.vm.run("displays", [[1, 2, 4][sizeBox.currentIndex]]))
            displayBox.model = editor.displays.map((d) => d.name)
            const at = editor.displays.findIndex((d) => d.display === keep)
            displayBox.currentIndex = at >= 0 ? at : 0
        }
        function accept() {
            const size = [1, 2, 4][sizeBox.currentIndex]
            const display = editor.displays[displayBox.currentIndex].display
            const endian = bigEndianBox.checkState === Qt.PartiallyChecked ? 2 : bigEndianBox.checked ? 1 : 0
            const ok = editor.mode === "new"
                ? win.vm.run("newWatch", [domainBox.currentText, addressBox.text, size, display, endian === 1, notesBox.text])
                : win.vm.run(editor.mode === "duplicate" ? "duplicateSelected" : "editSelected", [size, display, endian, editor.info.many ? "" : addressBox.text, notesBox.text])
            if (win.vm.error === "") editor.close()
        }
        GridLayout {
            anchors.fill: parent
            anchors.margins: 10
            columns: 2
            Label { text: qsTr("Mem Domain:"); Layout.alignment: Qt.AlignRight }
            ComboBox { id: domainBox; Layout.fillWidth: true; enabled: editor.mode === "new" }
            Label { text: qsTr("Address:"); Layout.alignment: Qt.AlignRight }
            RowLayout {
                Label { text: "0x" }
                TextField { id: addressBox; Layout.fillWidth: true; font.family: "monospace"; enabled: !editor.info.many }
            }
            Label { text: qsTr("Size:"); Layout.alignment: Qt.AlignRight }
            ComboBox {
                id: sizeBox
                Layout.fillWidth: true
                model: [qsTr("1 Byte"), qsTr("2 Byte"), qsTr("4 Byte")]
                onActivated: editor.fillDisplays(editor.displays.length > 0 ? editor.displays[displayBox.currentIndex].display : 0)
            }
            Item { width: 1 }
            CheckBox { id: bigEndianBox; text: qsTr("Big Endian"); tristate: editor.info.many === true }
            Label { text: qsTr("Display Type:"); Layout.alignment: Qt.AlignRight }
            ComboBox { id: displayBox; Layout.fillWidth: true; enabled: !(editor.info.sizesDiffer === true) }
            Label { text: qsTr("Notes:"); Layout.alignment: Qt.AlignRight }
            TextField { id: notesBox; Layout.fillWidth: true; enabled: !editor.info.many; maximumLength: 256; onAccepted: editor.accept() }
            Label { Layout.columnSpan: 2; text: win.vm.error; color: win.themeColor("AccentError", "#b00020"); visible: text !== ""; wrapMode: Text.Wrap; Layout.fillWidth: true }
            RowLayout {
                Layout.columnSpan: 2
                Layout.alignment: Qt.AlignRight
                Button { text: qsTr("OK"); highlighted: true; onClicked: editor.accept() }
                Button { text: qsTr("Cancel"); onClicked: editor.close() }
            }
        }
    }

    // RAM Poke: a value written into the watches picked
    RamPoke { id: poke; vm: win.vm; tell: win.tell }

    // Edit Separator: the text of the separators picked
    ApplicationWindow {
        id: separatorPrompt
        title: qsTr("Edit Separator")
        width: 300
        height: 120
        modality: Qt.ApplicationModal
        function open() { separatorText.text = ""; separatorPrompt.show(); separatorPrompt.raise(); separatorText.forceActiveFocus() }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            Label { text: qsTr("Separator Text:") }
            TextField { id: separatorText; Layout.fillWidth: true; onAccepted: okSeparator.clicked() }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Button { id: okSeparator; text: qsTr("OK"); onClicked: { win.vm.run("editSeparators", [separatorText.text]); separatorPrompt.close() } }
                Button { text: qsTr("Cancel"); onClicked: separatorPrompt.close() }
            }
        }
    }

    // Add Game Properties: a game core's properties, each ticked to be watched
    ApplicationWindow {
        id: propertyPicker
        title: qsTr("Add Game Properties")
        width: 320
        height: 400
        modality: Qt.ApplicationModal
        property var names: []
        property var ticked: ({})
        Connections {
            target: win.vm
            function onDomainsChanged() { propertyPicker.names = win.gameProperties ? (win.gameProperties.run("list") || "").split("\n").filter((n) => n !== "") : [] }
        }
        Component.onCompleted: propertyPicker.names = win.gameProperties ? (win.gameProperties.run("list") || "").split("\n").filter((n) => n !== "") : []
        function open() { propertyPicker.ticked = ({}); propertyPicker.show(); propertyPicker.raise() }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            ListView {
                id: pickList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: propertyPicker.names
                ScrollBar.vertical: ScrollBar {}
                delegate: CheckBox {
                    required property string modelData
                    text: modelData
                    onToggled: { const t = propertyPicker.ticked; t[modelData] = checked; propertyPicker.ticked = t }
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Button {
                    text: qsTr("OK")
                    onClicked: {
                        for (const n of propertyPicker.names) if (propertyPicker.ticked[n]) win.vm.run("addProperty", [n])
                        propertyPicker.close()
                    }
                }
                Button { text: qsTr("Cancel"); onClicked: propertyPicker.close() }
            }
        }
    }
}
