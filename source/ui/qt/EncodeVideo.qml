// Chimera's File > Encode Video: everything asked at once - the file, the
// ffmpeg command or a preset's, the two markers to encode between, the size
// and the sound's sync - then Start Encode winds the run there, encodes it
// and gives the machine back. Closing the window stops it. What it refuses
// is said in the status line in the error colour, as Chimera's. Layout and
// bindings only, over the encoder view model.
//
// Not here: Chimera's Capture OSD and Capture Lua, as the prototype draws
// those over the picture in the window and not into the frames written.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "encodeVideoWindow"
    required property QtObject vm
    required property QtObject session
    required property QtObject pianoRoll
    required property QtObject paths
    required property QtObject files
    // the theme's colour for a role (Main hands it its lookup)
    property var themeColor: (role, fallback) => fallback
    title: qsTr("Encode Video")
    width: 600
    height: 440
    minimumWidth: 600
    minimumHeight: 420
    flags: Qt.Window | Qt.WindowTitleHint | Qt.WindowSystemMenuHint | Qt.WindowMinimizeButtonHint | Qt.WindowCloseButtonHint
    property Window mainWindow: null
    transientParent: win.mainWindow
    readonly property bool running: win.vm.phase === "encoding"
    // what the window itself last refused, said over the encode's status until that changes
    property string said: ""
    Connections {
        target: win.vm
        function onValueChanged(key) { if (key === "status") win.said = "" }
    }
    // a count as Chimera's N0 writes it: 1,234
    function grouped(n) { return Number(n).toLocaleString(Qt.locale("en_US"), "f", 0) }
    function fileName(path) { return path.replace(/^.*[\\/]/, "") }
    readonly property string outputFolder: win.vm.output.trim().replace(/[\\/][^\\/]*$/, "")

    // the window opens on the whole run, a file named for the project in the A/V folder
    onVisibleChanged: {
        if (!visible) return
        fromBox.currentIndex = Math.max(0, fromBox.find(qsTr("Run start")))
        toBox.currentIndex = Math.max(0, toBox.find(qsTr("Run end")))
        // a file named for the project (else the game), until one is chosen
        const named = win.vm.output.replace(/^.*[\\/]/, "").replace(/\.[^.]*$/, "")
        if (named === "") {
            const project = win.session.projectPath.replace(/^.*[\\/]/, "").replace(/\.[^.]*$/, "")
            win.vm.run("suggest", [win.paths.run("resolve", ["A/V Dumps"]) || ".",
                                   project !== "" ? project : win.session.gameName !== "" ? win.session.gameName : "video"])
        }
        // not resizing: the size shown is the picture's own
        if (!win.vm.resize || win.vm.width === 0) useCurrentSize()
    }
    onClosing: if (win.running) win.vm.run("stop")
    function useCurrentSize() {
        win.vm.width = picture.pictureWidth > 0 ? picture.pictureWidth : 640
        win.vm.height = picture.pictureHeight > 0 ? picture.pictureHeight : 480
    }
    // the machine's own size now, from the main window's picture
    property Item picture: null
    Timer { interval: 200; repeat: true; running: win.running; onTriggered: win.vm.run("poll") }

    function start() {
        if (win.files.exists(win.vm.output)) replaceDialog.open()
        else win.begin()
    }
    function begin() {
        win.said = ""
        win.vm.run("start")
        if (win.vm.error !== "") win.said = win.vm.error
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        enabled: true

        GridLayout {
            columns: 3
            Layout.fillWidth: true
            enabled: !win.running
            Label { text: qsTr("Output file:") }
            TextField { text: win.vm.output; Layout.fillWidth: true; onEditingFinished: win.vm.output = text }
            Button { text: qsTr("Browse..."); onClicked: saveDialog.open() }
            Label { text: qsTr("Preset:") }
            ComboBox {
                id: presetBox
                Layout.fillWidth: true
                Layout.columnSpan: 2
                model: win.vm.rows("presets")
                textRole: "name"
                // the preset the command is, or one's own
                currentIndex: find(win.vm.preset)
                displayText: win.vm.preset
                onActivated: (index) => win.vm.run("usePreset", [index])
            }
            Label { text: qsTr("FFmpeg Command:") }
            TextField {
                Layout.fillWidth: true
                Layout.columnSpan: 2
                text: win.vm.command
                onTextEdited: win.vm.command = text
            }
        }
        RowLayout {
            enabled: !win.running
            Label { text: qsTr("From marker:") }
            ComboBox {
                id: fromBox
                Layout.fillWidth: true
                model: win.pianoRoll.rows("markers")
                textRole: "text"
                valueRole: "frame"
                displayText: currentIndex >= 0 ? currentValue + "  " + currentText : ""
                delegate: ItemDelegate { required property var model; width: fromBox.width; text: model.frame + "  " + model.text }
                onCurrentValueChanged: win.vm.fromFrame = currentValue === undefined ? 0 : currentValue
            }
            Label { text: qsTr("To marker:") }
            ComboBox {
                id: toBox
                Layout.fillWidth: true
                model: win.pianoRoll.rows("markers")
                textRole: "text"
                valueRole: "frame"
                displayText: currentIndex >= 0 ? currentValue + "  " + currentText : ""
                delegate: ItemDelegate { required property var model; width: toBox.width; text: model.frame + "  " + model.text }
                onCurrentValueChanged: win.vm.toFrame = currentValue === undefined ? 0 : currentValue
            }
        }
        Label {
            readonly property int frames: win.vm.toFrame - win.vm.fromFrame + 1
            text: frames > 0 ? qsTr("frames %1 to %2 (%3 frames)").arg(win.vm.fromFrame).arg(win.vm.toFrame).arg(win.grouped(frames))
                             : qsTr("the end marker is before the start marker")
            color: frames > 0 ? palette.placeholderText : win.themeColor("AccentError", "#b00020")
        }
        RowLayout {
            enabled: !win.running
            CheckBox { text: qsTr("Resize to"); checked: win.vm.resize; onToggled: win.vm.resize = checked }
            SpinBox { from: 1; to: 8192; editable: true; value: win.vm.width; enabled: win.vm.resize; onValueModified: win.vm.width = value; Layout.preferredWidth: 100 }
            Label { text: "x" }
            SpinBox { from: 1; to: 8192; editable: true; value: win.vm.height; enabled: win.vm.resize; onValueModified: win.vm.height = value; Layout.preferredWidth: 100 }
            Button { text: qsTr("Current size"); enabled: win.vm.resize; onClicked: win.useCurrentSize() }
            Label { text: win.picture && win.picture.pictureWidth > 0 ? qsTr("now %1x%2").arg(win.picture.pictureWidth).arg(win.picture.pictureHeight) : "" }
            CheckBox { text: qsTr("Pad"); checked: win.vm.pad; enabled: win.vm.resize; onToggled: win.vm.pad = checked }
        }
        CheckBox { text: qsTr("Sync to audio"); enabled: !win.running; checked: win.vm.audioSync; onToggled: win.vm.audioSync = checked }

        ProgressBar {
            Layout.fillWidth: true
            from: 0
            to: Math.max(1, win.vm.frameCount)
            value: win.vm.framesDone
        }
        Label {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            wrapMode: Text.Wrap
            elide: Text.ElideRight
            objectName: "encodeStatus"
            text: win.said !== "" ? win.said : win.vm.status
            color: win.said !== "" || win.vm.phase === "failed" ? win.themeColor("AccentError", "#b00020") : palette.windowText
        }
        // the speakers are quiet while a video is written: these say sound is going in
        RowLayout {
            Label { text: qsTr("Audio:") }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                ProgressBar { Layout.fillWidth: true; from: 0; to: 1; value: win.vm.levelLeft }
                ProgressBar { Layout.fillWidth: true; from: 0; to: 1; value: win.vm.levelRight }
            }
        }
        Item { Layout.fillHeight: true }
        RowLayout {
            Button {
                text: qsTr("Open Video")
                // what the last encode wrote, done or stopped; nothing while another is being written
                enabled: !win.running && win.vm.written !== "" && (win.vm.phase === "done" || win.vm.phase === "stopped")
                onClicked: {
                    if (!win.files.exists(win.vm.written)) win.said = qsTr("%1 is no longer there.").arg(win.fileName(win.vm.written))
                    else Qt.openUrlExternally(win.files.folderUrl(win.vm.written))
                }
            }
            Button {
                text: qsTr("Open Folder")
                enabled: win.outputFolder !== "" && win.files.exists(win.outputFolder)
                onClicked: {
                    if (!win.files.exists(win.outputFolder)) win.said = qsTr("The output file's folder does not exist.")
                    else Qt.openUrlExternally(win.files.folderUrl(win.outputFolder))
                }
            }
            Button { text: qsTr("Restore Defaults"); enabled: !win.running; onClicked: win.vm.run("restoreDefaults") }
            Item { Layout.fillWidth: true }
            Button { text: qsTr("Start Encode"); enabled: !win.running && win.vm.toFrame >= win.vm.fromFrame; onClicked: win.start() }
            Button { text: qsTr("Stop"); enabled: win.running; onClicked: win.vm.run("stop") }
            Button { text: qsTr("Close"); onClicked: win.close() }
        }
    }

    FileDialog {
        id: saveDialog
        title: qsTr("Write the video to")
        fileMode: FileDialog.SaveFile
        // where the file is now, and its name, as Chimera's opens
        currentFolder: win.outputFolder !== "" ? win.files.folderUrl(win.outputFolder) : ""
        selectedFile: win.vm.output !== "" ? win.files.folderUrl(win.vm.output) : ""
        nameFilters: [qsTr("MP4 Files (*.mp4)"), qsTr("AVI Files (*.avi)"), qsTr("Matroska Files (*.mkv)"), qsTr("All Files (*)")]
        onAccepted: win.vm.output = win.files.localPath(selectedFile)
    }
    // a video already at that name is not replaced without asking, as Chimera's
    Dialog {
        id: replaceDialog
        title: qsTr("Replace the video?")
        width: 420
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        Label {
            width: parent.width
            wrapMode: Text.Wrap
            text: qsTr("%1 already exists. Replace it with this encode?").arg(win.vm.output.replace(/^.*[\\/]/, ""))
        }
        onAccepted: win.begin()
        onRejected: win.said = qsTr("%1 was kept; pick another name to encode.").arg(win.fileName(win.vm.output))
    }
}
