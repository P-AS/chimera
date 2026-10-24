// The Debugger, as Chimera's: its menus (File, Debug, Settings); the
// Disassembler box on the left; Registers (values set by hand, flags as
// buttons) and Breakpoints on the right; Seek To, Cancel, Run and the step
// buttons down the right. No core here disassembles, steps or takes
// breakpoints (the engine has no call for them), so those parts are off
// with Chimera's words for why, as Chimera shows them for such a core. The
// registers are read as the machine moves. Layout and bindings only, over
// the debugger view model.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "debuggerWindow"
    required property QtObject vm
    required property QtObject session
    property QtObject customize: null
    // the theme's colour for a role (Main hands it its lookup)
    property var themeColor: (role, fallback) => fallback
    property QtObject toolWindows: null
    property Window mainWindow: null
    title: qsTr("Debugger")
    width: 767
    height: 580
    onVisibleChanged: {
        if (visible) { windowOptions.placeBack(false); win.vm.run("refresh") }
    }

    WindowOptions { id: windowOptions; window: win; tool: "debugger"; vm: win.toolWindows; mainWindow: win.mainWindow }

    // read again as the machine moves, while the window is open, at most ten times a second
    Connections {
        target: win.session
        enabled: win.visible
        function onFrameChanged() { if (!refreshTimer.running) refreshTimer.start() }
        function onRunningChanged() { win.vm.run("refresh") }
    }
    Timer { id: refreshTimer; interval: 100; onTriggered: win.vm.run("refresh") }

    readonly property string notImplemented: qsTr("This core does not currently implement this feature")

    menuBar: MenuBar {
        Menu {
            delegate: MenuEntry {}
            title: qsTr("&File")
            MenuItem { text: qsTr("&Close"); onTriggered: win.close() }
        }
        Menu {
            delegate: MenuEntry {}
            title: qsTr("&Debug")
            MenuItem { text: qsTr("Step &Into"); enabled: false }
            MenuItem { text: qsTr("Step O&ver"); enabled: false }
            MenuItem { text: qsTr("Step Ou&t"); enabled: false }
            MenuDivider {}
            Action { text: qsTr("Refresh"); shortcut: "F5"; onTriggered: win.vm.run("refresh") }
        }
        // Chimera adds a Settings menu for the window's items to a tool that has none
        Menu {
            delegate: MenuEntry {}
            title: qsTr("&Settings")
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
        text: win.vm.error !== "" ? win.vm.error
            : !win.session.hasRegisters ? (win.session.isOpen ? qsTr("This core does not show its registers.") : qsTr("No core is running."))
            : ""
        color: win.vm.error !== "" ? win.themeColor("AccentError", "#b00020") : palette.text
        padding: 4
        visible: text !== ""
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        // the disassembler: off, as for every core here
        GroupBox {
            title: qsTr("Disassembler")
            Layout.preferredWidth: 407
            Layout.fillHeight: true
            enabled: false
            ToolTip.visible: disasmHover.hovered
            ToolTip.text: qsTr("This core does not currently support disassembling")
            HoverHandler { id: disasmHover }
            ColumnLayout {
                anchors.fill: parent
                RowLayout {
                    Label { text: qsTr("Cpu:") }
                    Label { text: qsTr("Unknown") }
                    Item { Layout.fillWidth: true }
                    Button { text: qsTr("To PC") }
                }
                Label { text: qsTr("Use '[' or ']' to change address, hold 'shift' for speed.") }
                Row {
                    Label { text: qsTr("Address"); width: 120; font.bold: true }
                    Label { text: qsTr("Instruction"); font.bold: true }
                }
                Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; color: palette.base; border.color: palette.mid }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            GroupBox {
                title: qsTr("Registers")
                Layout.fillWidth: true
                Layout.preferredHeight: 234
                enabled: win.session.hasRegisters
                ScrollView {
                    anchors.fill: parent
                    clip: true
                    ColumnLayout {
                        // the registers of more than one bit: a name and its value in hex
                        Repeater {
                            model: win.vm.rows("registers")
                            delegate: RowLayout {
                                id: reg
                                required property int index
                                required property string name
                                required property string hex
                                required property int bits
                                visible: reg.bits !== 1
                                Label { text: reg.name; Layout.preferredWidth: 80 }
                                TextField {
                                    text: reg.hex
                                    font.family: "monospace"
                                    font.capitalization: Font.AllUppercase
                                    maximumLength: Math.max(1, reg.bits / 4)
                                    Layout.preferredWidth: 16 + Math.max(1, reg.bits / 4) * 10
                                    onEditingFinished: if (text !== reg.hex && text !== "") win.vm.run("setRegister", [reg.index, "0x" + text])
                                }
                            }
                        }
                        // the flags: a button each, pressed when set, the last first, "Flag" left off, as Chimera's
                        Flow {
                            spacing: 0
                            Repeater {
                                model: win.vm.rows("registers")
                                delegate: Button {
                                    id: flag
                                    required property int index
                                    required property string name
                                    required property var value
                                    required property int bits
                                    visible: flag.bits === 1
                                    width: visible ? implicitWidth : 0
                                    text: flag.name.replace("Flag", "").trim()
                                    checkable: true
                                    checked: Number(flag.value) === 1
                                    padding: 4
                                    onClicked: win.vm.run("setRegister", [flag.index, checked ? 1 : 0])
                                }
                            }
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                // breakpoints: off, as no core here has memory callbacks
                GroupBox {
                    title: qsTr("Breakpoints")
                    Layout.preferredWidth: 239
                    Layout.fillHeight: true
                    enabled: false
                    ToolTip.visible: breakHover.hovered
                    ToolTip.text: qsTr("This core does not currently support breakpoints")
                    HoverHandler { id: breakHover }
                    Rectangle { anchors.fill: parent; color: palette.base; border.color: palette.mid }
                }
                ColumnLayout {
                    Layout.fillHeight: true
                    Button { text: qsTr("Seek To:"); enabled: false; Layout.preferredWidth: 75 }
                    TextField { enabled: false; Layout.preferredWidth: 75 }
                    Button { text: qsTr("Cancel"); enabled: false; Layout.preferredWidth: 75 }
                    Item { Layout.fillHeight: true }
                    Button { text: qsTr("Run"); enabled: win.session.isOpen && !win.session.running; onClicked: win.session.run("play"); Layout.preferredWidth: 75 }
                    Button { text: qsTr("Step Into"); enabled: false; Layout.preferredWidth: 75; ToolTip.visible: hovered; ToolTip.text: win.notImplemented }
                    Button { text: qsTr("Step Over"); enabled: false; Layout.preferredWidth: 75; ToolTip.visible: hovered; ToolTip.text: win.notImplemented }
                    Button { text: qsTr("Step Out"); enabled: false; Layout.preferredWidth: 75; ToolTip.visible: hovered; ToolTip.text: win.notImplemented }
                }
            }
        }
    }
}
