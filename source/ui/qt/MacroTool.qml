// Chimera's Macro Tool (Macro Input): layout and bindings only, over the
// macroTool view model.
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
    title: qsTr("Macro Input")
    width: 640
    height: 520
    readonly property var zoneList: win.vm.rows("zones")
    property bool leaving: false
    onClosing: (close) => {
        if (win.vm.unsavedCount > 0 && !win.leaving) { close.accepted = false; unsavedDialog.open() }
        win.leaving = false
    }
    readonly property var movieColumns: JSON.parse(win.vm.movieColumns)
    property var pickedRow: null

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            ColumnLayout {
                Layout.preferredWidth: 280
                Layout.fillHeight: true
                ListView {
                    id: zones
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: win.zoneList
                    currentIndex: win.vm.picked
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
                        highlighted: ListView.isCurrentItem
                        text: qsTr("%1 - length: %2%3").arg(z.name).arg(z.length).arg(z.saved ? "" : qsTr(" (not saved)"))
                        onClicked: win.vm.picked = z.index
                        Component.onCompleted: if (z.index === win.vm.picked) win.pickedRow = z
                        ListView.onIsCurrentItemChanged: if (ListView.isCurrentItem) win.pickedRow = z
                    }
                }
                RowLayout {
                    Label { text: qsTr("Macro start") }
                    SpinBox { id: zoneStart; from: 0; to: Math.max(0, win.pianoRoll.frameCount - 1); editable: true; value: win.pianoRoll.selectFirst >= 0 ? win.pianoRoll.selectFirst : 0 }
                }
                RowLayout {
                    Label { text: qsTr("Macro end") }
                    SpinBox { id: zoneEnd; from: 0; to: Math.max(0, win.pianoRoll.frameCount - 1); editable: true; value: win.pianoRoll.selectLast >= 0 ? win.pianoRoll.selectLast : 0 }
                    Button { text: qsTr("Set Macro"); onClicked: win.vm.run("setZone", [zoneStart.value, zoneEnd.value]) }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                enabled: win.pickedRow !== null
                RowLayout {
                    Label { text: qsTr("Macro Name:") }
                    TextField {
                        Layout.fillWidth: true
                        text: win.pickedRow ? win.pickedRow.name : ""
                        onEditingFinished: if (win.pickedRow && text !== win.pickedRow.name) win.vm.run("rename", [win.vm.picked, text])
                    }
                }
                RowLayout {
                    CheckBox { text: qsTr("Overlay"); checked: win.pickedRow ? win.pickedRow.overlay : false; onToggled: win.vm.run("setOverlay", [win.vm.picked, checked]) }
                    CheckBox { text: qsTr("Replace"); checked: win.pickedRow ? win.pickedRow.replace : true; onToggled: win.vm.run("setReplace", [win.vm.picked, checked]) }
                }
                Label { text: qsTr("Its columns:") }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    Flow {
                        width: parent.width
                        spacing: 4
                        Repeater {
                            model: win.movieColumns
                            delegate: CheckBox {
                                required property string modelData
                                text: modelData
                                readonly property var carried: win.pickedRow && win.pickedRow.columns !== "" ? win.pickedRow.columns.split("|") : win.movieColumns
                                checked: carried.indexOf(modelData) >= 0
                                onToggled: {
                                    const now = win.movieColumns.filter(c => c === modelData ? checked : carried.indexOf(c) >= 0)
                                    win.vm.run("setColumns", [win.vm.picked, now.length === win.movieColumns.length ? "" : now.join("|")])
                                }
                            }
                        }
                    }
                }
                RowLayout {
                    Label { text: qsTr("Frame:") }
                    SpinBox { id: placeAt; from: 0; to: Math.max(0, win.pianoRoll.frameCount); editable: true }
                    Button { text: qsTr("Current"); onClicked: placeAt.value = win.pianoRoll.currentFrame }
                    Button { text: qsTr("Place Zone"); onClicked: win.vm.run("place", [win.vm.picked, placeAt.value]) }
                }
            }
        }
        RowLayout {
            Button { text: qsTr("Save Selected As..."); enabled: win.pickedRow !== null; onClicked: saveDialog.open() }
            Button { text: qsTr("Load Macro..."); onClicked: loadDialog.open() }
            Button { text: qsTr("Remove"); enabled: win.vm.picked > 0; onClicked: win.vm.run("remove", [win.vm.picked]) }
            Label { text: win.vm.error !== "" ? win.vm.error : win.vm.status; color: win.vm.error !== "" ? "#b00020" : palette.text; Layout.fillWidth: true; elide: Text.ElideRight }
        }
    }
    FileDialog {
        id: saveDialog
        title: qsTr("Save the macro as")
        fileMode: FileDialog.SaveFile
        currentFolder: win.folderOf ? win.folderOf("Macros") : ""
        nameFilters: [qsTr("Movie Macros (*.chimeraMacro)")]
        onAccepted: win.vm.run("saveAs", [win.vm.picked, selectedFile])
    }
    FileDialog {
        id: loadDialog
        title: qsTr("Load a macro")
        currentFolder: win.folderOf ? win.folderOf("Macros") : ""
        nameFilters: [qsTr("Movie Macros (*.chimeraMacro)"), qsTr("All files (*)")]
        onAccepted: win.vm.run("load", [selectedFile])
    }
    Dialog {
        id: unsavedDialog
        title: qsTr("Save?")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        Label { text: qsTr("You have unsaved macro(s). Close and let them go?") }
        onAccepted: { win.leaving = true; win.close() }
    }
}
