// New Project: layout and bindings only, over the newProject view model.
// The core and its machine, the files in the slots the package declares,
// the settings that apply, the firmware; what is missing; then where the
// project goes.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "newProjectWindow"
    required property QtObject vm
    property string slotFor: ""
    property string firmwareFor: ""
    title: qsTr("New Project")
    width: 760
    height: 760

    FileDialog {
        id: fileDialog
        title: qsTr("Choose a file")
        fileMode: FileDialog.OpenFiles
        onAccepted: { for (const f of selectedFiles) win.vm.run("addFile", [win.slotFor, f]) }
    }
    FileDialog {
        id: firmwareDialog
        title: qsTr("Choose the firmware file")
        onAccepted: win.vm.run("chooseFirmware", [win.firmwareFor, selectedFile])
    }
    FolderDialog {
        id: scanDialog
        title: qsTr("Scan a folder for firmware")
        onAccepted: win.vm.run("scanFolder", [selectedFolder, scanSubfolders.checked])
    }
    FileDialog {
        id: importMovieDialog
        title: win.vm.importMovieLabel
        nameFilters: [win.vm.importMovieLabel + " (" + win.vm.importMovieFilter + ")", qsTr("All files (*)")]
        onAccepted: win.vm.run("importSetMovie", [selectedFile])
    }
    property string importFor: ""
    FileDialog {
        id: importFileDialog
        title: qsTr("Choose a file")
        fileMode: FileDialog.OpenFiles
        onAccepted: { for (const f of selectedFiles) win.vm.run("importAddFile", [win.importFor, f]) }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        // emulators or game cores, then every package of that kind
        RowLayout {
            ComboBox {
                model: [qsTr("Emulators"), qsTr("Games")]
                currentIndex: win.vm.kind === "games" ? 1 : 0
                onActivated: (i) => win.vm.run("chooseKind", [i === 1 ? "games" : "emulators"])
            }
            Label { text: qsTr("Core") }
            ComboBox {
                id: coreBox
                model: win.vm.coreLines === "" ? [] : win.vm.coreLines.split("\n")
                currentIndex: win.vm.coreLine
                displayText: currentIndex < 0 ? qsTr("Choose a core") : currentText
                onActivated: (i) => win.vm.run("chooseCoreLine", [i])
                Layout.fillWidth: true
            }
        }
        RowLayout {
            Label { text: qsTr("Machine"); visible: win.vm.core !== "" }
            ComboBox {
                visible: win.vm.machines !== ""
                model: win.vm.machines === "" ? [] : win.vm.machines.split("\n")
                currentIndex: win.vm.machine
                onActivated: (i) => win.vm.run("chooseMachine", [i])
                Layout.preferredWidth: 220
            }
            // a package of one machine still says which it is
            Label { visible: win.vm.core !== "" && win.vm.machines === ""; text: win.vm.systems }
            Label { text: qsTr("Renderer"); visible: win.vm.renderers !== "" }
            ComboBox {
                visible: win.vm.renderers !== ""
                // one renderer is not a choice
                enabled: win.vm.renderers.indexOf("\n") >= 0
                model: win.vm.renderers === "" ? [] : win.vm.renderers.split("\n")
                currentIndex: win.vm.renderer
                onActivated: (i) => win.vm.run("chooseRenderer", [i])
                Layout.preferredWidth: 180
            }
            Item { Layout.fillWidth: true }
        }
        Label {
            visible: win.vm.rendererCaveat !== ""
            text: win.vm.rendererCaveat
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        // a movie made elsewhere, when the core reads them: the core's answer fills the rest
        Frame {
            Layout.fillWidth: true
            visible: win.vm.importMenu !== ""
            ColumnLayout {
                anchors.fill: parent
                Label { text: win.vm.importMenu.replace("...", ""); font.bold: true }
                RowLayout {
                    Label { text: win.vm.importMovieLabel; Layout.preferredWidth: 160 }
                    Label { text: win.vm.importMovie === "" ? qsTr("none") : win.vm.importMovie.replace(/^.*[\\/]/, ""); Layout.fillWidth: true; elide: Text.ElideMiddle }
                    Button { text: qsTr("Choose..."); onClicked: importMovieDialog.open() }
                }
                Repeater {
                    model: win.vm.rows("importFiles")
                    delegate: RowLayout {
                        id: input
                        required property string option
                        required property string label
                        required property string filter
                        required property bool required
                        required property string files
                        Label { text: input.label + (input.required ? " *" : ""); Layout.preferredWidth: 160 }
                        Label { text: input.files === "" ? qsTr("none") : input.files.split("\n").join(", "); Layout.fillWidth: true; elide: Text.ElideRight }
                        Button {
                            text: qsTr("Add...")
                            onClicked: {
                                win.importFor = input.option
                                importFileDialog.nameFilters = [input.label + " (" + input.filter + ")", qsTr("All files (*)")]
                                importFileDialog.open()
                            }
                        }
                    }
                }
                Repeater {
                    model: win.vm.rows("importOptions")
                    delegate: CheckBox {
                        required property string name
                        required property string label
                        required property bool on
                        text: label
                        checked: on
                        onToggled: win.vm.run("importSetOption", [name, checked])
                    }
                }
                RowLayout {
                    Button { text: qsTr("Import"); onClicked: win.vm.run("importRun") }
                    Label {
                        text: win.vm.imported ? qsTr("The project will take the movie's inputs") : ""
                        Layout.fillWidth: true
                    }
                    Button { text: qsTr("Forget"); visible: win.vm.imported; onClicked: win.vm.run("importForget") }
                }
                Label { text: win.vm.importNotes; visible: text !== ""; wrapMode: Text.Wrap; opacity: 0.75; Layout.fillWidth: true }
            }
        }

        Label { text: qsTr("Files"); font.bold: true }
        ListView {
            id: slotList
            Layout.fillWidth: true
            Layout.preferredHeight: 220
            clip: true
            spacing: 6
            model: win.vm.rows("slots")
            ScrollBar.vertical: ScrollBar {}
            delegate: Frame {
                id: slot
                required property string id
                required property string title
                required property string formats
                required property string help
                required property bool exposed
                required property string files
                required property string filter
                property int picked: -1
                width: slotList.width - 16
                enabled: slot.exposed
                opacity: slot.exposed ? 1 : 0.5
                RowLayout {
                    anchors.fill: parent
                    ColumnLayout {
                        Layout.preferredWidth: 220
                        Label { text: slot.title; font.bold: true }
                        Label { text: slot.formats; opacity: 0.75 }
                        ToolTip.visible: hover.hovered
                        ToolTip.text: slot.help
                        HoverHandler { id: hover }
                    }
                    ListView {
                        id: fileList
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.max(24, Math.min(contentHeight, 96))
                        clip: true
                        model: slot.files === "" ? [] : slot.files.split("\n")
                        delegate: ItemDelegate {
                            required property string modelData
                            required property int index
                            width: fileList.width
                            text: modelData
                            highlighted: index === slot.picked
                            onClicked: slot.picked = index
                        }
                    }
                    ColumnLayout {
                        Button {
                            text: qsTr("Add...")
                            onClicked: {
                                win.slotFor = slot.id
                                fileDialog.nameFilters = [slot.title + " (" + slot.filter + ")", qsTr("All files (*)")]
                                fileDialog.open()
                            }
                        }
                        Button {
                            text: qsTr("Remove")
                            enabled: slot.picked >= 0
                            onClicked: { win.vm.run("removeFile", [slot.id, slot.picked]); slot.picked = -1 }
                        }
                        Button {
                            text: qsTr("Earlier")
                            enabled: slot.picked > 0
                            onClicked: { win.vm.run("moveFile", [slot.id, slot.picked, slot.picked - 1]); slot.picked-- }
                        }
                    }
                }
            }
        }

        Label { text: qsTr("Settings"); font.bold: true }
        Label {
            text: win.vm.suggestion
            visible: text !== ""
            wrapMode: Text.Wrap
            opacity: 0.8
            Layout.fillWidth: true
        }
        SettingsList {
            vm: win.vm
            Layout.fillWidth: true
            Layout.fillHeight: true
        }

        RowLayout {
            visible: firmwareList.count > 0
            Label { text: qsTr("Firmware"); font.bold: true }
            Item { Layout.fillWidth: true }
            // looks through a folder for what is still missing
            CheckBox { id: scanSubfolders; text: qsTr("With subfolders"); checked: true }
            Button { text: qsTr("Scan Folder..."); onClicked: scanDialog.open() }
        }
        ListView {
            id: firmwareList
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 120)
            visible: count > 0
            clip: true
            model: win.vm.rows("firmware")
            delegate: RowLayout {
                id: need
                required property string id
                required property string name
                required property string file
                required property string how
                required property bool missing
                width: firmwareList.width - 16
                Label { text: need.name; font.bold: true; Layout.preferredWidth: 200; elide: Text.ElideRight }
                Label {
                    text: need.missing ? qsTr("not found") : need.file + " (" + need.how + ")"
                    color: need.missing ? "#b00020" : palette.text
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }
                Button { text: qsTr("Choose..."); onClicked: { win.firmwareFor = need.id; firmwareDialog.open() } }
                Button { text: qsTr("Forget"); visible: need.how === "chosen" || need.how === "your own"; onClicked: win.vm.run("forgetFirmware", [need.id]) }
            }
        }

        RowLayout {
            Label { text: qsTr("Title") }
            TextField {
                text: win.vm.title
                placeholderText: qsTr("The first file's name")
                onEditingFinished: win.vm.title = text
                Layout.fillWidth: true
            }
        }
        Label {
            text: win.vm.problems
            color: "#b00020"
            visible: text !== ""
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Label { text: win.vm.error; color: "#b00020"; visible: text !== ""; wrapMode: Text.Wrap; Layout.fillWidth: true }
        RowLayout {
            Label { text: win.vm.status; Layout.fillWidth: true; elide: Text.ElideRight }
            Button { text: qsTr("Cancel"); onClicked: win.close() }
            // made and opened; the first save asks where it goes
            Button { text: qsTr("Make the Project"); enabled: win.vm.ready; onClicked: if (win.vm.run("create") !== undefined) win.close() }
        }
    }
}
