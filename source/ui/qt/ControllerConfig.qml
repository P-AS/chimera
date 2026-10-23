// Chimera's Controller Config: Normal Controls, Autofire Controls and Analog
// Controls in tabs (inside each, a tab a player when there is more than
// one), the picture of the controller the package ships beside them, and
// along the bottom the tips, Auto Tab, Misc... (Load Defaults, Clear), Save
// and Cancel. The window works on a copy: Save keeps it; Cancel, or closing
// the window, lets it go. A box takes the next key or gamepad input pressed
// while it has the focus; its arrow gives the bindings a key cannot
// (Escape, the pointer's buttons). Layout and bindings only, over the
// controls view model.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "controllerConfigWindow"
    required property QtObject controls
    required property QtObject keyNamer
    required property QtObject session
    property Window mainWindow: null
    property var themeColor: (role, fallback) => fallback
    title: win.controls.title
    width: 932
    height: 572
    minimumWidth: 932
    minimumHeight: 572
    modality: Qt.ApplicationModal
    transientParent: win.mainWindow

    readonly property var buttonGroups: JSON.parse(win.controls.buttonGroups)
    readonly property var axisGroups: JSON.parse(win.controls.axisGroups)
    // the box with the focus, which a gamepad input pressed goes to; the analog control catching an axis
    property Item focusBox: null
    property int axisCatch: -1
    readonly property string tips: qsTr("* Escape clears a key mapping\n* Disable Auto Tab to multiply bind")

    onVisibleChanged: {
        if (visible) {
            win.controls.run("edit")
            kinds.currentIndex = 0
        } else {
            win.stopCatching()
            if (win.controls.editing) win.controls.run("cancel")
        }
    }
    function stopCatching() {
        win.axisCatch = -1
        win.session.run("stopCatchPad")
    }
    function save() {
        win.controls.run("save")
        win.close()
    }

    // a gamepad input: to the analog control catching an axis, else to the box with the focus
    Connections {
        target: win.session
        function onCaughtPadChanged() {
            const name = win.session.caughtPad
            if (name === "") return
            if (win.axisCatch >= 0) {
                win.controls.run("setAnalog", [win.axisCatch, "value", name])
                win.axisCatch = -1
            } else if (win.focusBox !== null) {
                win.focusBox.take(name)
            }
        }
    }
    // the pointer moved far enough over the window while an axis is caught: the pointer's axis, as Chimera's
    property point lastPointer: Qt.point(-1, -1)
    property real pointerMovedX: 0
    property real pointerMovedY: 0
    HoverHandler {
        onPointChanged: {
            const p = point.position
            if (win.axisCatch >= 0 && win.lastPointer.x >= 0) {
                win.pointerMovedX += 50 * Math.abs(p.x - win.lastPointer.x)
                win.pointerMovedY += 50 * Math.abs(p.y - win.lastPointer.y)
                const caught = win.pointerMovedX >= 20000 ? "WMouse X" : win.pointerMovedY >= 20000 ? "WMouse Y" : ""
                if (caught !== "") {
                    win.controls.run("setAnalog", [win.axisCatch, "value", caught])
                    win.stopCatching()
                }
            }
            win.lastPointer = p
        }
    }
    onAxisCatchChanged: { win.pointerMovedX = 0; win.pointerMovedY = 0 }

    // a box: the keys bound, and an arrow for the bindings a key cannot give
    component InputBox: RowLayout {
        id: box
        required property string table
        required property int row
        required property string keys
        // the next box, for Auto Tab (null: none)
        property var next: null
        spacing: 0
        implicitWidth: 170
        function focusField() { field.forceActiveFocus() }
        function take(name) {
            const moveOn = win.controls.run("press", [box.table, box.row, name])
            if (moveOn === true && box.next) box.next()
            // the box keeps the focus: it waits for the next input
            else if (field.activeFocus) win.session.run("catchPad")
        }
        TextField {
            id: field
            objectName: "bindingBox"
            Layout.fillWidth: true
            Layout.preferredHeight: 23
            readOnly: true
            text: box.keys
            padding: 2
            // the start of the keys shown, as a text box shows them
            onTextChanged: cursorPosition = 0
            Component.onCompleted: cursorPosition = 0
            background: Rectangle {
                color: field.activeFocus ? win.themeColor("InputAwaitingBackground", "#e0ffff") : win.themeColor("InputBackground", palette.base)
                border.color: field.activeFocus ? palette.highlight : palette.mid
            }
            ToolTip.visible: hovered
            ToolTip.delay: 500
            ToolTip.text: qsTr("Current Binding: %1").arg(box.keys) + "\n---\n" + win.tips
            onActiveFocusChanged: {
                if (activeFocus) {
                    win.focusBox = box
                    win.session.run("catchPad")
                } else if (win.focusBox === box) {
                    win.focusBox = null
                    win.session.run("stopCatchPad")
                }
            }
            Keys.priority: Keys.BeforeItem
            Keys.onPressed: (event) => {
                // Alt+F4 still closes the window; nothing else gets past the box
                if (event.key === Qt.Key_F4 && (event.modifiers & Qt.AltModifier)) return
                event.accepted = true
                const name = win.keyNamer.name(event.key, event.modifiers, false)
                if (name === "" || name === "Shift" || name === "Ctrl" || name === "Alt") return
                box.take(name)
            }
            TapHandler { onTapped: field.forceActiveFocus() }
        }
        ToolButton {
            focusPolicy: Qt.NoFocus
            Layout.preferredWidth: 18
            Layout.preferredHeight: 23
            icon.source: "qrc:/chimera/icons/arrow_black_down.png"
            ToolTip.visible: hovered
            ToolTip.delay: 500
            ToolTip.text: qsTr("Click here for special tricky bindings")
            onClicked: specialMenu.popup()
            Menu {
                id: specialMenu
                Repeater {
                    model: [
                        { name: "Escape", tip: qsTr("Binds the Escape key") },
                        { name: "WMouse L", tip: qsTr("Binds the left mouse button") },
                        { name: "WMouse M", tip: qsTr("Binds the middle mouse button") },
                        { name: "WMouse R", tip: qsTr("Binds the right mouse button") },
                        { name: "WMouse 1", tip: qsTr("Binds the mouse auxiliary button 1") },
                        { name: "WMouse 2", tip: qsTr("Binds the mouse auxiliary button 2") }
                    ]
                    delegate: MenuItem {
                        required property var modelData
                        text: modelData.name
                        ToolTip.visible: hovered
                        ToolTip.text: modelData.tip
                        onTriggered: {
                            const moveOn = win.controls.run("special", [box.table, box.row, modelData.name])
                            if (moveOn === true && box.next) box.next()
                        }
                    }
                }
            }
        }
    }

    // one player's buttons (or every button): boxes down a column, a label to the right of each, more columns as needed
    component KeyPanel: ScrollView {
        id: panel
        required property string table
        required property string group
        property bool everyGroup: false
        clip: true
        contentWidth: flow.width
        contentHeight: availableHeight
        readonly property var model: win.controls.rows(panel.table === "autofire" ? "autofire" : "buttons")
        // Chimera's panel: the label as wide as the longest name, 9 pixels a character
        property int labelWidth: 0
        function shownHere(group) { return panel.everyGroup || group === panel.group }
        function focusAfter(at) {
            const n = boxes.count
            for (let k = 1; k <= n; k++) {
                const item = boxes.itemAt((at + k) % n)
                if (item && item.visible) { item.focusField(); return }
            }
        }
        Flow {
            id: flow
            flow: Flow.TopToBottom
            height: Math.max(23, panel.availableHeight - 39)
            spacing: 1
            Repeater {
                id: boxes
                model: panel.model
                delegate: Row {
                    id: entry
                    required property int index
                    required property string shown
                    required property string group
                    required property string keys
                    visible: panel.shownHere(entry.group)
                    spacing: 5
                    function focusField() { entryBox.focusField() }
                    Component.onCompleted: if (visible) panel.labelWidth = Math.max(panel.labelWidth, entry.shown.length * 9)
                    InputBox {
                        id: entryBox
                        width: 170
                        table: panel.table
                        row: entry.index
                        keys: entry.keys
                        next: () => panel.focusAfter(entry.index)
                    }
                    Label {
                        text: entry.shown
                        width: panel.labelWidth + 5
                        topPadding: 3
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }

    // one player's analog controls: Chimera's AnalogBindControl each, one under another
    component AnalogPanel: ScrollView {
        id: apanel
        required property string group
        property bool everyGroup: false
        clip: true
        ColumnLayout {
            spacing: 4
            x: 4
            Repeater {
                model: win.controls.rows("analog")
                delegate: GridLayout {
                    id: bind
                    required property int index
                    required property string axis
                    required property string group
                    required property string value
                    required property double mult
                    required property double deadzone
                    required property string positive
                    required property string negative
                    visible: apanel.everyGroup || bind.group === apanel.group
                    columns: 5
                    columnSpacing: 6
                    rowSpacing: 4
                    Layout.preferredWidth: 474
                    // the axis bound, the control's name, Bind! / Unbind! / Flip Axis
                    TextField {
                        text: bind.value; readOnly: true; padding: 2
                        Layout.preferredWidth: 100; Layout.preferredHeight: 23
                        onTextChanged: cursorPosition = 0
                        Component.onCompleted: cursorPosition = 0
                    }
                    Label { text: bind.axis; Layout.preferredWidth: 92 }
                    Button {
                        text: win.axisCatch === bind.index ? qsTr("Cancel!") : qsTr("Bind!")
                        Layout.preferredWidth: 75
                        onClicked: {
                            if (win.axisCatch === bind.index) { win.stopCatching(); return }
                            win.axisCatch = bind.index
                            win.session.run("catchAxis")
                        }
                    }
                    Button { text: qsTr("Unbind!"); Layout.preferredWidth: 75; onClicked: win.controls.run("unbind", [bind.index]) }
                    Button { text: qsTr("Flip Axis"); Layout.preferredWidth: 75; onClicked: win.controls.run("flip", [bind.index]) }
                    // the key pushing it up, and the sensitivity
                    InputBox { table: "positive"; row: bind.index; keys: bind.positive; Layout.preferredWidth: 100 }
                    Label { text: bind.axis + "+"; Layout.preferredWidth: 92 }
                    Label { text: qsTr("Sensitivity: %1%").arg(Math.round(bind.mult * 100)); Layout.preferredWidth: 110 }
                    Slider {
                        Layout.columnSpan: 2
                        Layout.preferredWidth: 160
                        from: -40; to: 40; stepSize: 1; snapMode: Slider.SnapAlways
                        value: Math.round(bind.mult * 20)
                        onMoved: win.controls.run("setAnalog", [bind.index, "mult", value / 20])
                    }
                    // the key pushing it down, and the dead zone
                    InputBox { table: "negative"; row: bind.index; keys: bind.negative; Layout.preferredWidth: 100 }
                    Label { text: bind.axis + "-"; Layout.preferredWidth: 92 }
                    Label { text: qsTr("Deadzone: %1%").arg(Math.round(bind.deadzone * 100)); Layout.preferredWidth: 110 }
                    Slider {
                        Layout.columnSpan: 2
                        Layout.preferredWidth: 160
                        from: 0; to: 25; stepSize: 1; snapMode: Slider.SnapAlways
                        value: Math.round(bind.deadzone * 50)
                        onMoved: win.controls.run("setAnalog", [bind.index, "deadzone", value / 50])
                    }
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 6
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0
                TabBar {
                    id: kinds
                    Layout.fillWidth: true
                    TabButton { text: qsTr("Normal Controls"); width: implicitWidth }
                    TabButton { text: qsTr("Autofire Controls"); width: implicitWidth }
                    // Chimera leaves the tab out for a controller with no analog controls
                    TabButton { text: qsTr("Analog Controls"); width: visible ? implicitWidth : 0; visible: win.axisGroups.length > 0 }
                }
                StackLayout {
                    currentIndex: kinds.currentIndex
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Repeater {
                        model: ["normal", "autofire", "analog"]
                        delegate: Frame {
                            id: kind
                            required property string modelData
                            readonly property var groups: kind.modelData === "analog" ? win.axisGroups : win.buttonGroups
                            padding: 3
                            ColumnLayout {
                                anchors.fill: parent
                                spacing: 0
                                // a tab a player when there is more than one, as Chimera's
                                TabBar {
                                    id: groupTabs
                                    visible: kind.groups.length > 1
                                    Layout.fillWidth: true
                                    Repeater {
                                        model: kind.groups.length > 1 ? kind.groups : []
                                        delegate: TabButton { required property string modelData; text: modelData; width: implicitWidth }
                                    }
                                }
                                StackLayout {
                                    currentIndex: kind.groups.length > 1 ? groupTabs.currentIndex : 0
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    Repeater {
                                        model: kind.groups.length > 1 ? kind.groups : [""]
                                        delegate: Loader {
                                            id: page
                                            required property string modelData
                                            sourceComponent: kind.modelData === "analog" ? analogPage : keyPage
                                            Component { id: keyPage; KeyPanel { table: kind.modelData; group: page.modelData; everyGroup: kind.groups.length <= 1 } }
                                            Component { id: analogPage; AnalogPanel { group: page.modelData; everyGroup: kind.groups.length <= 1 } }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            // the package's picture of the controller; without one the column is gone
            Image {
                objectName: "controllerPicture"
                visible: win.controls.pictureShown > 0
                source: visible ? "image://controller/" + win.controls.pictureShown : ""
                fillMode: Image.PreserveAspectFit
                Layout.preferredWidth: visible ? 334 : 0
                Layout.fillHeight: true
                Layout.topMargin: 20
                cache: false
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
                checked: win.controls.autoTab
                onToggled: win.controls.autoTab = checked
            }
            Button {
                text: qsTr("Misc...")
                Layout.preferredWidth: 75
                onClicked: miscMenu.popup()
                Menu {
                    id: miscMenu
                    MenuItem { text: qsTr("Load Defaults"); onTriggered: win.controls.run("loadDefaults") }
                    MenuItem { text: qsTr("Clear"); onTriggered: win.controls.run("clear") }
                }
            }
            Button { text: qsTr("&Save"); Layout.preferredWidth: 75; onClicked: win.save() }
            Button { text: qsTr("&Cancel"); Layout.preferredWidth: 75; onClicked: win.close() }
        }
    }
}
