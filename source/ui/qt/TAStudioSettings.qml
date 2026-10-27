// Chimera's TAStudio Settings (TAStudio's Settings > Open settings...),
// laid out as its form: the tabs Appearance, Autosave, Paint Patterns,
// Input Roll and Misc, in Chimera's words and number ranges, and Apply and
// Cancel under them. Everything is read when the window opens and written
// only by Apply (with Chimera's warning when markers are bound to input
// while hidden columns are not edited); Cancel lets it all go. Set Colors
// is Chimera's Edit TAStudio Colors (its twelve colours, OK, Cancel,
// Defaults), whose answer Apply also writes. Layout and bindings only,
// over the session, pianoRoll and branches view models.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ChimeraDialogs

ApplicationWindow {
    id: win
    objectName: "tastudioSettingsWindow"
    required property QtObject session
    required property QtObject pianoRoll
    required property QtObject branches
    // the roll's colours as the theme has them: {"TasCurrentFrame": "#rrggbb", ...}
    property var themeColorsOf: () => "{}"
    property Window ownerWindow: null
    title: qsTr("TAStudio Settings")
    width: 440
    height: 540
    minimumWidth: 440
    minimumHeight: 520
    modality: Qt.ApplicationModal
    transientParent: win.ownerWindow

    // ---- the copies the window edits, read when it opens ----
    property bool markersColor
    property bool markersIcons
    property bool statesColor
    property bool statesIcons
    property int lagToHide
    property bool hideWasLag
    property bool rotate
    property string rollFont
    property string rollColors
    property int autosaveSeconds
    property bool autosaveAsBackup
    property bool backupOnSave
    property bool followAlways
    property string followScroll
    property int wheelRows
    property bool scrollSync
    property bool editHidden
    property bool bindMarkers
    property bool pauseAtEnd
    property bool branchDoubleClick
    property int branchPreviewSide
    property bool oldBranches
    property bool copyFrameNumbers
    property int maxUndo
    property int rewindStep
    property int rewindStepFast
    property bool markersKeepStates
    property string patternMode
    property string patternKind
    // each control's pattern: {name: {runs: [{v, n}], loop, skipLag, button, min, max, neutral, text}}
    property var patterns: ({})

    onVisibleChanged: if (visible) win.load()
    function load() {
        const r = win.pianoRoll, s = win.session, b = win.branches
        win.markersColor = r.denoteMarkersColor; win.markersIcons = r.denoteMarkersIcons
        win.statesColor = r.denoteStatesColor; win.statesIcons = r.denoteStatesIcons
        win.lagToHide = r.lagFramesToHide; win.hideWasLag = r.hideWasLagFrames; win.rotate = r.horizontal
        win.rollFont = r.rollFont; win.rollColors = r.rollColors
        win.autosaveSeconds = s.autosaveSeconds; win.autosaveAsBackup = s.autosaveAsBackup; win.backupOnSave = s.backupOnSave
        win.followAlways = r.followAlways; win.followScroll = r.followScroll; win.wheelRows = r.wheelRows; win.scrollSync = r.scrollSync; win.editHidden = r.editHiddenColumns
        win.bindMarkers = r.bindMarkers; win.pauseAtEnd = s.pauseAtEnd; win.branchDoubleClick = b.loadOnDoubleClick
        win.branchPreviewSide = b.previewSide; win.oldBranches = b.oldControlScheme; win.copyFrameNumbers = r.copyFrameNumbers
        win.maxUndo = r.maxUndoSteps; win.rewindStep = s.rewindStep; win.rewindStepFast = s.rewindStepFast
        win.markersKeepStates = r.markersKeepStates; win.patternMode = r.patternMode; win.patternKind = r.patternKind
        win.patterns = {}
        win.readPatterns()
        tabs.currentIndex = 0
        patternEditor.choose(0)
    }
    // the project's patterns, those not edited here read again (a machine that opens after the window brings them)
    function readPatterns() {
        const all = Object.assign({}, win.patterns)
        for (let i = 0; i < patternRows.count; i++) {
            const p = patternRows.objectAt(i)
            if (!p || (all[p.name] && all[p.name].edited)) continue
            all[p.name] = { runs: win.parseRuns(p.runs, p.button), loop: p.loop, skipLag: p.skipLag, button: p.button,
                            min: p.min, max: p.max, neutral: p.neutral, edited: false }
        }
        win.patterns = all
        if (patternEditor.control === "" && controlBox.count > 0) patternEditor.choose(0)
    }
    // "On x2, Off x1" or "127 x2, 0 x1" as runs of {v, n}
    function parseRuns(text, button) {
        const out = []
        for (const part of text.split(",")) {
            const t = part.trim()
            if (t === "") continue
            const m = t.match(/^(.*\S)\s+x(\d+)$/)
            const value = m ? m[1] : t, n = m ? parseInt(m[2]) : 1
            out.push({ v: button ? value.toLowerCase() === "on" : parseInt(value), n: n })
        }
        if (out.length === 0) out.push({ v: button ? false : 0, n: 1 })
        return out
    }
    function runsText(p) { return p.runs.map(r => (p.button ? (r.v ? "On" : "Off") : String(r.v)) + " x" + r.n).join(", ") }

    function apply() {
        const r = win.pianoRoll
        // Chimera asks first when markers would move with input the edits cannot reach
        const wasBad = r.bindMarkers && !r.editHiddenColumns
        const isBad = win.bindMarkers && !win.editHidden
        if (isBad && !wasBad) { bindWarning.open(); return }
        win.write()
    }
    function write() {
        const r = win.pianoRoll, s = win.session, b = win.branches
        r.rollColors = win.rollColors; r.rollFont = win.rollFont
        // the patterns changed here, each as the project keeps it
        for (const name in win.patterns) {
            const p = win.patterns[name]
            if (p.edited) r.run("setPattern", [name, win.runsText(p), p.loop, p.skipLag])
        }
        r.denoteMarkersColor = win.markersColor; r.denoteMarkersIcons = win.markersIcons
        r.denoteStatesColor = win.statesColor; r.denoteStatesIcons = win.statesIcons
        r.lagFramesToHide = win.lagToHide; r.hideWasLagFrames = win.hideWasLag; r.horizontal = win.rotate
        s.autosaveSeconds = win.autosaveSeconds; s.autosaveAsBackup = win.autosaveAsBackup; s.backupOnSave = win.backupOnSave
        r.followAlways = win.followAlways; r.wheelRows = win.wheelRows; r.scrollSync = win.scrollSync; r.followScroll = win.followScroll
        r.editHiddenColumns = win.editHidden; r.bindMarkers = win.bindMarkers; s.pauseAtEnd = win.pauseAtEnd
        b.loadOnDoubleClick = win.branchDoubleClick; b.previewSide = win.branchPreviewSide; b.oldControlScheme = win.oldBranches
        r.copyFrameNumbers = win.copyFrameNumbers; r.maxUndoSteps = win.maxUndo
        s.rewindStep = win.rewindStep; s.rewindStepFast = win.rewindStepFast; r.markersKeepStates = win.markersKeepStates
        r.patternMode = win.patternMode; r.patternKind = win.patternKind
        win.close()
    }

    MessageDialog {
        id: bindWarning
        title: qsTr("Warning")
        text: qsTr("You have enabled 'bind markers to input' and disabled 'allow editing hidden columns'. This combination is not recommended, and may result in your markers being moved in unexpected ways. Continue?")
        buttons: MessageDialog.Ok | MessageDialog.Cancel
        onAccepted: win.write()
    }

    // each control's pattern as the project has it
    Instantiator {
        id: patternRows
        model: win.pianoRoll.rows("patterns")
        delegate: QtObject {
            required property string name
            required property string runs
            required property int loop
            required property bool skipLag
            required property bool button
            required property int min
            required property int max
            required property int neutral
        }
        onObjectAdded: if (win.visible) Qt.callLater(win.readPatterns)
    }

    component Check: CheckBox { padding: 2 }
    component Number: SpinBox { editable: true; Layout.preferredWidth: 100 }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        TabBar {
            id: tabs
            Layout.fillWidth: true
            TabButton { text: qsTr("Appearance") }
            TabButton { text: qsTr("Autosave") }
            TabButton { text: qsTr("Paint Patterns") }
            TabButton { text: qsTr("Input Roll") }
            TabButton { text: qsTr("Misc") }
        }
        StackLayout {
            currentIndex: tabs.currentIndex
            Layout.fillWidth: true
            Layout.fillHeight: true

            // ---- Appearance ----
            ColumnLayout {
                spacing: 4
                RowLayout {
                    Button { text: qsTr("Set Font"); Layout.preferredWidth: 90; onClicked: fontDialog.open() }
                    Button { text: qsTr("Set Colors"); Layout.preferredWidth: 90; onClicked: colorsWindow.openWith(win.rollColors) }
                }
                Check { text: qsTr("Denote states with icons"); checked: win.statesIcons; onToggled: win.statesIcons = checked }
                Check { text: qsTr("Denote states with background color"); checked: win.statesColor; onToggled: win.statesColor = checked }
                Check { text: qsTr("Denote markers with icons"); checked: win.markersIcons; onToggled: win.markersIcons = checked }
                Check { text: qsTr("Denote markers with color on frame column"); checked: win.markersColor; onToggled: win.markersColor = checked }
                Label { text: qsTr("Movie settings"); font.bold: true; font.pointSize: Qt.application.font.pointSize * 1.2; Layout.topMargin: 6 }
                Check { text: qsTr("Rotate input roll"); checked: win.rotate; onToggled: win.rotate = checked }
                RowLayout {
                    Label { text: qsTr("Lag frames to hide:") }
                    Number { from: 0; to: 10; value: win.lagToHide; onValueModified: win.lagToHide = value }
                }
                Check { text: qsTr("Hide \"was lag\" frames"); checked: win.hideWasLag; onToggled: win.hideWasLag = checked }
                Item { Layout.fillHeight: true }
            }

            // ---- Autosave ----
            ColumnLayout {
                spacing: 4
                RowLayout {
                    Label { text: qsTr("Autosave frequency:") }
                    Number { from: 0; to: 10000000; value: win.autosaveSeconds; onValueModified: win.autosaveSeconds = value; Layout.preferredWidth: 120 }
                    Label { text: qsTr("seconds") }
                }
                Label { text: qsTr("Set to 0 to disable autosave") }
                Check { text: qsTr("Save as .backup (no greenzone)"); checked: win.autosaveAsBackup; onToggled: win.autosaveAsBackup = checked; Layout.topMargin: 30 }
                Check { text: qsTr("When manually saving, also save .backup"); checked: win.backupOnSave; onToggled: win.backupOnSave = checked }
                Item { Layout.fillHeight: true }
            }

            // ---- Paint Patterns ----
            ColumnLayout {
                spacing: 6
                RowLayout {
                    GroupBox {
                        title: qsTr("Use pattern for painting")
                        Layout.alignment: Qt.AlignTop
                        ColumnLayout {
                            spacing: 0
                            Repeater {
                                model: [["always", qsTr("Always")], ["autofire", qsTr("Auto-Fire columns only")], ["never", qsTr("Never")]]
                                RadioButton {
                                    required property var modelData
                                    padding: 2
                                    text: modelData[1]
                                    checked: win.patternMode === modelData[0]
                                    onToggled: if (checked) win.patternMode = modelData[0]
                                }
                            }
                        }
                    }
                    GroupBox {
                        title: qsTr("Choose pattern")
                        Layout.alignment: Qt.AlignTop
                        ColumnLayout {
                            spacing: 0
                            Repeater {
                                model: [["hold", qsTr("Hold")], ["autofire", qsTr("Auto-Fire")], ["custom", qsTr("Custom")]]
                                RadioButton {
                                    required property var modelData
                                    padding: 2
                                    text: modelData[1]
                                    checked: win.patternKind === modelData[0]
                                    onToggled: if (checked) win.patternKind = modelData[0]
                                    ToolTip.visible: hovered
                                    ToolTip.delay: 600
                                    ToolTip.text: qsTr("You can set a column as auto hold/fire by right-clicking on the column header.")
                                }
                            }
                        }
                    }
                }
                GroupBox {
                    id: patternEditor
                    title: qsTr("Set custom patterns (per movie)")
                    Layout.preferredWidth: 270
                    // the control picked, its runs, and the line picked (the last is "Loop to")
                    property string control: ""
                    property int line: 0
                    readonly property var pattern: win.patterns[patternEditor.control]
                    readonly property bool onLoop: patternEditor.pattern !== undefined && patternEditor.line >= patternEditor.pattern.runs.length
                    function choose(index) {
                        controlBox.currentIndex = index
                        patternEditor.control = index >= 0 && index < controlBox.count ? controlBox.textAt(index) : ""
                        patternEditor.line = 0
                    }
                    // a change to the pattern picked, kept in the window's copy
                    function edit(change) {
                        const all = Object.assign({}, win.patterns)
                        const p = Object.assign({}, all[patternEditor.control])
                        p.edited = true
                        p.runs = p.runs.map(r => ({ v: r.v, n: r.n }))
                        change(p)
                        all[patternEditor.control] = p
                        win.patterns = all
                    }
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 4
                        ComboBox {
                            id: controlBox
                            Layout.fillWidth: true
                            model: patternRows.count > 0 ? Array.from({ length: patternRows.count }, (_, i) => patternRows.objectAt(i) ? patternRows.objectAt(i).name : "") : []
                            onActivated: (i) => patternEditor.choose(i)
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 134
                            color: palette.base
                            border.color: palette.mid
                            ListView {
                                id: runList
                                anchors.fill: parent
                                anchors.margins: 1
                                clip: true
                                ScrollBar.vertical: ScrollBar {}
                                // "0: On  (x2)" a run, from the frame it starts on; "Loop to: n" last
                                model: {
                                    const p = patternEditor.pattern
                                    if (!p) return []
                                    const out = []
                                    let at = 0
                                    for (const r of p.runs) {
                                        out.push(at + ": " + (p.button ? (r.v ? qsTr("On") : qsTr("Off")) : String(r.v)) + "\t(x" + r.n + ")")
                                        at += r.n
                                    }
                                    out.push(qsTr("Loop to: %1").arg(p.loop))
                                    return out
                                }
                                delegate: Rectangle {
                                    required property string modelData
                                    required property int index
                                    width: runList.width
                                    height: runText.implicitHeight + 2
                                    color: index === patternEditor.line ? palette.highlight : "transparent"
                                    Label {
                                        id: runText
                                        leftPadding: 3
                                        text: parent.modelData.replace("\t", "    ")
                                        color: parent.index === patternEditor.line ? palette.highlightedText : palette.text
                                    }
                                    TapHandler { onTapped: patternEditor.line = parent.index }
                                }
                            }
                        }
                        RowLayout {
                            Label { text: qsTr("Value:") }
                            // a button's run is on or off; an analog control's is a value in its range
                            CheckBox {
                                visible: patternEditor.pattern !== undefined && patternEditor.pattern.button
                                enabled: !patternEditor.onLoop
                                padding: 0
                                checked: patternEditor.pattern !== undefined && !patternEditor.onLoop && patternEditor.pattern.runs[patternEditor.line].v === true
                                onToggled: { const on = checked; patternEditor.edit(p => p.runs[patternEditor.line].v = on) }
                            }
                            SpinBox {
                                visible: patternEditor.pattern !== undefined && !patternEditor.pattern.button
                                enabled: !patternEditor.onLoop
                                editable: true
                                Layout.preferredWidth: 90
                                from: patternEditor.pattern !== undefined ? patternEditor.pattern.min : 0
                                to: patternEditor.pattern !== undefined ? patternEditor.pattern.max : 0
                                value: patternEditor.pattern !== undefined && !patternEditor.onLoop ? patternEditor.pattern.runs[patternEditor.line].v : 0
                                onValueModified: { const v = value; patternEditor.edit(p => p.runs[patternEditor.line].v = v) }
                            }
                            Item { Layout.fillWidth: true }
                            Label { text: qsTr("Count:") }
                            // a run's count (never 0), or on the last line the frame it loops to
                            SpinBox {
                                editable: true
                                Layout.preferredWidth: 80
                                from: patternEditor.onLoop ? 0 : 1
                                to: 100
                                value: patternEditor.pattern === undefined ? 1 : patternEditor.onLoop ? patternEditor.pattern.loop : patternEditor.pattern.runs[patternEditor.line].n
                                onValueModified: {
                                    const v = value
                                    if (patternEditor.onLoop) patternEditor.edit(p => p.loop = v)
                                    else patternEditor.edit(p => p.runs[patternEditor.line].n = Math.max(1, v))
                                }
                            }
                        }
                        RowLayout {
                            Button {
                                text: qsTr("Insert")
                                Layout.preferredWidth: 70
                                enabled: patternEditor.pattern !== undefined
                                onClicked: patternEditor.edit(p => p.runs.splice(Math.min(patternEditor.line, p.runs.length), 0, { v: p.button ? false : p.neutral, n: 1 }))
                            }
                            Item { Layout.fillWidth: true }
                            Button {
                                text: qsTr("Delete")
                                Layout.preferredWidth: 70
                                enabled: patternEditor.pattern !== undefined && !patternEditor.onLoop && patternEditor.pattern.runs.length > 1
                                onClicked: patternEditor.edit(p => p.runs.splice(patternEditor.line, 1))
                            }
                        }
                        Check {
                            text: qsTr("Account for lag frames")
                            enabled: patternEditor.pattern !== undefined
                            checked: patternEditor.pattern !== undefined && patternEditor.pattern.skipLag
                            onToggled: { const on = checked; patternEditor.edit(p => p.skipLag = on) }
                        }
                    }
                }
                Item { Layout.fillHeight: true }
            }

            // ---- Input Roll ----
            ColumnLayout {
                spacing: 4
                Check { text: qsTr("When following cursor, scroll at every frame"); checked: win.followAlways; onToggled: win.followAlways = checked }
                GroupBox {
                    title: qsTr("Scroll to cursor method")
                    ColumnLayout {
                        spacing: 0
                        Repeater {
                            model: [["near", qsTr("scroll to view")], ["top", qsTr("scroll to top")],
                                    ["bottom", qsTr("scroll to bottom")], ["center", qsTr("scroll to center")]]
                            RadioButton {
                                required property var modelData
                                padding: 2
                                text: modelData[1]
                                checked: win.followScroll === modelData[0]
                                onToggled: if (checked) win.followScroll = modelData[0]
                            }
                        }
                    }
                }
                RowLayout {
                    Label { text: qsTr("Wheel scroll speed:") }
                    Number { from: 0; to: 100; value: win.wheelRows; onValueModified: win.wheelRows = value }
                    Label { text: qsTr("frames per tick") }
                }
                Check { text: qsTr("Sync scroll across all input rolls"); checked: win.scrollSync; onToggled: win.scrollSync = checked; Layout.topMargin: 6 }
                Check {
                    text: qsTr("Allow editing hidden columns (with clear, record, paste, etc.)")
                    checked: win.editHidden
                    onToggled: win.editHidden = checked
                }
                Item { Layout.fillHeight: true }
            }

            // ---- Misc ----
            ColumnLayout {
                spacing: 4
                GridLayout {
                    columns: 2
                    Label { text: qsTr("Rewind step:"); Layout.preferredWidth: 110 }
                    Number { from: 1; to: 100; value: win.rewindStep; onValueModified: win.rewindStep = value }
                    Label { text: qsTr("Fast rewind step:") }
                    Number { from: 1; to: 100; value: win.rewindStepFast; onValueModified: win.rewindStepFast = value }
                    Label { text: qsTr("Max Undo Count:") }
                    Number { from: 1; to: 10000; value: win.maxUndo; onValueModified: win.maxUndo = value }
                }
                Check { text: qsTr("Bind markers to input"); checked: win.bindMarkers; onToggled: win.bindMarkers = checked }
                Check { text: qsTr("Include frame numbers when copying input."); checked: win.copyFrameNumbers; onToggled: win.copyFrameNumbers = checked }
                Check { text: qsTr("Automatically pause at end of movie."); checked: win.pauseAtEnd; onToggled: win.pauseAtEnd = checked }
                Check { text: qsTr("Load branch by double clicking"); checked: win.branchDoubleClick; onToggled: win.branchDoubleClick = checked }
                Check { text: qsTr("Old control scheme for branches"); checked: win.oldBranches; onToggled: win.oldBranches = checked }
                Check { text: qsTr("Markers keep states by default"); checked: win.markersKeepStates; onToggled: win.markersKeepStates = checked }
                RowLayout {
                    Label { text: qsTr("Branch screenshot size (pixels):") }
                    Number {
                        from: 64
                        to: 4096
                        stepSize: 32
                        value: win.branchPreviewSide
                        onValueModified: win.branchPreviewSide = value
                        ToolTip.visible: hovered
                        ToolTip.delay: 600
                        ToolTip.text: qsTr("The longest side of the picture shown when the mouse is over a branch's number.\nA larger screenshot is shrunk to this size; a smaller one is shown as it is.")
                    }
                }
                Item { Layout.fillHeight: true }
            }
        }
        RowLayout {
            Item { Layout.fillWidth: true }
            Button { text: qsTr("Apply"); Layout.preferredWidth: 80; onClicked: win.apply() }
            Button { text: qsTr("Cancel"); Layout.preferredWidth: 80; onClicked: win.close() }
        }
    }

    FontDialog {
        id: fontDialog
        title: qsTr("The piano roll's font")
        onAccepted: win.rollFont = JSON.stringify({ family: selectedFont.family, pointSize: selectedFont.pointSize,
                                                    bold: selectedFont.bold, italic: selectedFont.italic })
    }

    // Chimera's Edit TAStudio Colors: a colour a line, clicked to pick; OK keeps them for Apply, Defaults is the theme's
    ApplicationWindow {
        id: colorsWindow
        objectName: "tastudioColorsWindow"
        title: qsTr("Edit TAStudio Colors")
        width: 300
        height: 400
        modality: Qt.ApplicationModal
        transientParent: win
        // the colours being chosen ({} is the theme's), and the theme's
        property var own: ({})
        property var theme: ({})
        function openWith(json) {
            try { colorsWindow.own = JSON.parse(json || "{}") } catch (e) { colorsWindow.own = {} }
            try { colorsWindow.theme = JSON.parse(win.themeColorsOf()) } catch (e) { colorsWindow.theme = {} }
            colorsWindow.show()
            colorsWindow.raise()
        }
        function shown(role) { return colorsWindow.own[role] !== undefined ? colorsWindow.own[role] : (colorsWindow.theme[role] || "transparent") }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 2
            Repeater {
                model: [["TasCurrentFrame", qsTr("Emulated Frame Cursor")], ["TasGreenZone", qsTr("Frame# Column")],
                        ["TasGreenZoneInput", qsTr("Input Log")], ["TasGreenZoneInputStated", qsTr("Savestate")],
                        ["TasGreenZoneInputInvalidated", qsTr("Invalidated")], ["TasLagZone", qsTr("Frame# Column (Lag)")],
                        ["TasLagZoneInput", qsTr("Input Log (Lag)")], ["TasLagZoneInputStated", qsTr("Savestate (Lag)")],
                        ["TasLagZoneInputInvalidated", qsTr("Invalidated (Lag)")], ["TasMarker", qsTr("Marker")],
                        ["TasPermanentMarker", qsTr("Marker (run start / last input / end)")], ["TasAnalogEdit", qsTr("Analog Edit Mode")]]
                RowLayout {
                    required property var modelData
                    spacing: 6
                    Rectangle {
                        width: 20; height: 20
                        border.color: palette.text
                        color: colorsWindow.shown(parent.modelData[0])
                        TapHandler {
                            onTapped: {
                                colorDialog.role = parent.parent.modelData[0]
                                colorDialog.selectedColor = colorsWindow.shown(parent.parent.modelData[0])
                                colorDialog.open()
                            }
                        }
                    }
                    Label { text: parent.modelData[1] }
                }
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Button { text: qsTr("Defaults"); Layout.preferredWidth: 75; onClicked: colorsWindow.own = {} }
                Button { text: qsTr("Cancel"); Layout.preferredWidth: 75; onClicked: colorsWindow.close() }
                Button { text: qsTr("OK"); Layout.preferredWidth: 75; onClicked: { win.rollColors = JSON.stringify(colorsWindow.own); colorsWindow.close() } }
            }
        }
        ColorDialog {
            id: colorDialog
            property string role: ""
            onAccepted: {
                const all = Object.assign({}, colorsWindow.own)
                all[colorDialog.role] = selectedColor.toString()
                colorsWindow.own = all
            }
        }
    }
}
