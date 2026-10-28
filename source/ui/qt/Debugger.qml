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
    title: qsTrId("debugger.debugger")
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

    readonly property string notImplemented: qsTrId("debugger.this_core_does_not_currently_implement")

    menuBar: MenuBar {
        Menu {
            delegate: MenuEntry {}
            title: qsTrId("common.file")
            MenuItem { text: qsTrId("common.close_mnemonic"); onTriggered: win.close() }
        }
        Menu {
            delegate: MenuEntry {}
            title: qsTrId("debugger.debug")
            MenuItem { text: qsTrId("debugger.step_into_mnemonic"); enabled: false }
            MenuItem { text: qsTrId("debugger.step_over_mnemonic"); enabled: false }
            MenuItem { text: qsTrId("debugger.step_out_mnemonic"); enabled: false }
            MenuDivider {}
            Action { text: qsTrId("debugger.refresh"); shortcut: "F5"; onTriggered: win.vm.run("refresh") }
        }
        // Chimera adds a Settings menu for the window's items to a tool that has none
        Menu {
            delegate: MenuEntry {}
            title: qsTrId("common.settings")
            MenuItem { text: qsTrId("common.save_window_position"); checkable: true; checked: windowOptions.savePosition; onTriggered: windowOptions.set("save", checked) }
            MenuItem {
                text: qsTrId("common.stay_on_top"); checkable: true; checked: windowOptions.onTop
                visible: windowOptions.canStayOnTop; height: visible ? implicitHeight : 0
                onTriggered: windowOptions.set("top", checked)
            }
            MenuItem { text: qsTrId("common.float_from_main_window"); checkable: true; checked: windowOptions.floating; onTriggered: windowOptions.set("float", checked) }
            MenuItem { text: qsTrId("common.restore_defaults_mnemonic"); onTriggered: windowOptions.restore() }
        }
    }

    footer: Label {
        text: win.vm.error !== "" ? win.vm.error
            : !win.session.hasRegisters ? (win.session.isOpen ? qsTrId("debugger.this_core_does_not_show_its") : qsTrId("debugger.no_core_is_running"))
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
            title: qsTrId("debugger.disassembler")
            Layout.preferredWidth: 407
            Layout.fillHeight: true
            enabled: false
            ToolTip.visible: disasmHover.hovered
            ToolTip.text: qsTrId("debugger.this_core_does_not_currently_support")
            HoverHandler { id: disasmHover }
            ColumnLayout {
                anchors.fill: parent
                RowLayout {
                    Label { text: qsTrId("debugger.cpu") }
                    Label { text: qsTrId("debugger.unknown") }
                    Item { Layout.fillWidth: true }
                    Button { text: qsTrId("debugger.to_pc") }
                }
                Label { text: qsTrId("debugger.use_or_to_change_address_hold") }
                Row {
                    Label { text: qsTrId("debugger.address"); width: 120; font.bold: true }
                    Label { text: qsTrId("debugger.instruction"); font.bold: true }
                }
                Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; color: palette.base; border.color: palette.mid }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            GroupBox {
                title: qsTrId("debugger.registers")
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
                                    // as many digits as the view model shows (a 6-bit register is 2)
                                    maximumLength: Math.max(1, Math.ceil(reg.bits / 4))
                                    Layout.preferredWidth: 16 + Math.max(1, Math.ceil(reg.bits / 4)) * 10
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
                    title: qsTrId("debugger.breakpoints")
                    Layout.preferredWidth: 239
                    Layout.fillHeight: true
                    enabled: false
                    ToolTip.visible: breakHover.hovered
                    ToolTip.text: qsTrId("debugger.this_core_does_not_currently_support_2")
                    HoverHandler { id: breakHover }
                    Rectangle { anchors.fill: parent; color: palette.base; border.color: palette.mid }
                }
                ColumnLayout {
                    Layout.fillHeight: true
                    Button { text: qsTrId("debugger.seek_to"); enabled: false; Layout.preferredWidth: 75 }
                    TextField { enabled: false; Layout.preferredWidth: 75 }
                    Button { text: qsTrId("common.cancel"); enabled: false; Layout.preferredWidth: 75 }
                    Item { Layout.fillHeight: true }
                    Button { text: qsTrId("debugger.run"); enabled: win.session.isOpen && !win.session.running; onClicked: win.session.run("play"); Layout.preferredWidth: 75 }
                    Button { text: qsTrId("debugger.step_into"); enabled: false; Layout.preferredWidth: 75; ToolTip.visible: hovered; ToolTip.text: win.notImplemented }
                    Button { text: qsTrId("debugger.step_over"); enabled: false; Layout.preferredWidth: 75; ToolTip.visible: hovered; ToolTip.text: win.notImplemented }
                    Button { text: qsTrId("debugger.step_out"); enabled: false; Layout.preferredWidth: 75; ToolTip.visible: hovered; ToolTip.text: win.notImplemented }
                }
            }
        }
    }
}
