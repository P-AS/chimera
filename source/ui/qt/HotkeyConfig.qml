// Chimera's Configure Hotkeys: Find at the top, a tab a group of actions
// (General, Movie, Tools, RAM Search, TAStudio, in Chimera's order), each
// action's name and its box down columns, and along the bottom the tips,
// Auto Tab, Misc... (Restore Defaults, Restore Current Tab Defaults, Clear
// All, Clear Current Tab), Save and Cancel. The window works on a copy:
// Save keeps it; Cancel, or closing the window, lets it go. Layout and
// bindings only, over the hotkeys view model.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "hotkeyConfigWindow"
    required property QtObject hotkeys
    required property QtObject keyNamer
    required property QtObject session
    property Window mainWindow: null
    property var themeColor: (role, fallback) => fallback
    title: qsTr("Configure Hotkeys")
    width: 783
    height: 463
    modality: Qt.ApplicationModal
    transientParent: win.mainWindow

    readonly property var groups: JSON.parse(win.hotkeys.groups)

    onVisibleChanged: {
        if (visible) {
            win.hotkeys.run("edit")
            tabs.currentIndex = 0
            search.text = ""
        } else {
            win.session.run("stopCatchPad")
            if (win.hotkeys.editing) win.hotkeys.run("cancel")
        }
    }

    // Find: the action named goes to its tab, its box taking the next key
    function findAction(text) {
        const found = win.hotkeys.run("find", [text])
        if (!found) return false
        const hit = JSON.parse(found)
        const tab = win.groups.indexOf(hit.group)
        if (tab < 0) return false
        tabs.currentIndex = tab
        const page = pages.itemAt(tab)
        if (page) page.focusAction(hit.action)
        return true
    }

    // a tab's actions: a name and its box, down columns, more columns as needed
    component HotkeyPage: ScrollView {
        id: page
        required property string group
        clip: true
        contentWidth: flow.width
        contentHeight: availableHeight
        function focusAfter(at) {
            const n = entries.count
            for (let k = 1; k <= n; k++) {
                const item = entries.itemAt((at + k) % n)
                if (item && item.visible) { item.focusBox(); return }
            }
        }
        function focusAction(name) {
            for (let i = 0; i < entries.count; i++) {
                const item = entries.itemAt(i)
                if (item && item.visible && item.action === name) { item.focusBox(); return }
            }
        }
        Flow {
            id: flow
            flow: Flow.TopToBottom
            topPadding: 10
            leftPadding: 6
            height: Math.max(24, page.availableHeight - 21)
            spacing: 1
            Repeater {
                id: entries
                model: win.hotkeys.rows("hotkeys")
                delegate: Row {
                    id: entry
                    required property int index
                    required property string group
                    required property string action
                    required property string keys
                    visible: entry.group === page.group
                    width: visible ? 250 : 0
                    height: visible ? 23 : 0
                    function focusBox() { box.focusField() }
                    Label { text: entry.action; width: 120; topPadding: 4; elide: Text.ElideRight }
                    BindingBox {
                        id: box
                        width: 120
                        keys: entry.keys
                        session: win.session
                        keyNamer: win.keyNamer
                        themeColor: win.themeColor
                        combination: true
                        press: (name) => win.hotkeys.run("press", [entry.action, name])
                        special: (name) => win.hotkeys.run("special", [entry.action, name])
                        next: () => page.focusAfter(entry.index)
                    }
                }
            }
            // what Chimera says at the end of the TAStudio tab
            Label {
                visible: page.group === "TAStudio"
                width: 240
                wrapMode: Text.WordWrap
                topPadding: 4
                text: qsTr("Save States hotkeys operate with branches when TAStudio is engaged.")
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        anchors.topMargin: 6
        spacing: 4
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Label { text: qsTr("Find:") }
            TextField {
                id: search
                objectName: "hotkeySearch"
                Layout.preferredWidth: 149
                // Chimera's suggestions: the names that start with what is typed
                readonly property var suggestions: JSON.parse(win.hotkeys.run("suggest", [search.text]) || "[]")
                onTextEdited: if (search.suggestions.length > 0 && search.activeFocus) suggestionsPopup.open(); else suggestionsPopup.close()
                Keys.onPressed: (event) => {
                    if (event.key !== Qt.Key_Return && event.key !== Qt.Key_Enter && event.key !== Qt.Key_Tab) return
                    event.accepted = true
                    suggestionsPopup.close()
                    if (!win.findAction(search.text) && search.suggestions.length > 0) {
                        search.text = search.suggestions[0]
                        win.findAction(search.text)
                    }
                }
                Popup {
                    id: suggestionsPopup
                    y: search.height
                    width: Math.max(search.width, 200)
                    padding: 1
                    contentItem: ListView {
                        implicitHeight: Math.min(contentHeight, 200)
                        clip: true
                        model: search.suggestions
                        delegate: ItemDelegate {
                            required property string modelData
                            width: ListView.view.width
                            text: modelData
                            padding: 3
                            onClicked: { suggestionsPopup.close(); search.text = modelData; win.findAction(modelData) }
                        }
                    }
                }
            }
        }
        TabBar {
            id: tabs
            Layout.fillWidth: true
            Repeater {
                model: win.groups
                delegate: TabButton { required property string modelData; text: modelData; width: implicitWidth }
            }
        }
        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: 0
            StackLayout {
                anchors.fill: parent
                currentIndex: tabs.currentIndex
                Repeater {
                    id: pages
                    model: win.groups
                    delegate: HotkeyPage { required property string modelData; group: modelData }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Label { text: qsTr("Tips:") }
            Label { text: qsTr("* Escape clears a key mapping") }
            Label { text: qsTr("* Disable Auto Tab to multiply bind") }
            Item { Layout.fillWidth: true }
            CheckBox {
                text: qsTr("Auto Tab")
                checked: win.hotkeys.autoTab
                onToggled: win.hotkeys.autoTab = checked
            }
            Button {
                text: qsTr("Misc...")
                Layout.preferredWidth: 60
                onClicked: miscMenu.popup()
                Menu {
                    id: miscMenu
                    MenuItem { text: qsTr("Restore Defaults"); onTriggered: win.hotkeys.run("defaults") }
                    MenuItem { text: qsTr("Restore Current Tab Defaults"); onTriggered: win.hotkeys.run("defaults", [win.groups[tabs.currentIndex]]) }
                    MenuItem { text: qsTr("Clear All"); onTriggered: win.hotkeys.run("clear") }
                    MenuItem { text: qsTr("Clear Current Tab"); onTriggered: win.hotkeys.run("clear", [win.groups[tabs.currentIndex]]) }
                }
            }
            Button { text: qsTr("&Save"); Layout.preferredWidth: 60; onClicked: { win.hotkeys.run("save"); win.close() } }
            Button { text: qsTr("&Cancel"); Layout.preferredWidth: 60; onClicked: win.close() }
        }
    }
}
