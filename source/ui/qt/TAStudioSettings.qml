// TAStudio's settings, as Chimera's TAStudio Settings dialog has them:
// layout and bindings only, over the session, pianoRoll and branches view
// models. A change takes effect at once and is remembered.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: win
    objectName: "tastudioSettingsWindow"
    required property QtObject session
    required property QtObject pianoRoll
    required property QtObject branches
    title: qsTr("TAStudio Settings")
    width: 620
    height: 440

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8

        TabBar {
            id: tabs
            Layout.fillWidth: true
            TabButton { text: qsTr("Input Roll") }
            TabButton { text: qsTr("Editing") }
            TabButton { text: qsTr("Appearance") }
            TabButton { text: qsTr("Playback") }
            TabButton { text: qsTr("Autosave") }
            TabButton { text: qsTr("Branches") }
            TabButton { text: qsTr("Paint Patterns") }
        }

        StackLayout {
            currentIndex: tabs.currentIndex
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                Label { text: qsTr("Scroll to cursor method:") }
                Repeater {
                    model: [["near", qsTr("scroll to view")], ["top", qsTr("scroll to top")],
                            ["bottom", qsTr("scroll to bottom")], ["center", qsTr("scroll to center")]]
                    RadioButton {
                        required property var modelData
                        text: modelData[1]
                        checked: win.pianoRoll.followScroll === modelData[0]
                        onToggled: if (checked) win.pianoRoll.followScroll = modelData[0]
                    }
                }
                CheckBox {
                    text: qsTr("When following cursor, scroll at every frame")
                    checked: win.pianoRoll.followAlways
                    onToggled: win.pianoRoll.followAlways = checked
                }
                RowLayout {
                    Label { text: qsTr("Wheel scroll speed:") }
                    SpinBox {
                        from: 1; to: 1000; editable: true
                        value: win.pianoRoll.wheelRows
                        onValueModified: win.pianoRoll.wheelRows = value
                    }
                    Label { text: qsTr("frames per tick") }
                }
                Item { Layout.fillHeight: true }
            }

            ColumnLayout {
                CheckBox {
                    text: qsTr("Bind markers to input")
                    checked: win.pianoRoll.bindMarkers
                    onToggled: win.pianoRoll.bindMarkers = checked
                }
                CheckBox {
                    text: qsTr("Markers keep states by default")
                    checked: win.pianoRoll.markersKeepStates
                    onToggled: win.pianoRoll.markersKeepStates = checked
                }
                CheckBox {
                    text: qsTr("Allow editing hidden columns (with clear, record, paste, etc.)")
                    checked: win.pianoRoll.editHiddenColumns
                    onToggled: win.pianoRoll.editHiddenColumns = checked
                }
                // Chimera warns of this pair when it is applied
                Label {
                    visible: win.pianoRoll.bindMarkers && !win.pianoRoll.editHiddenColumns
                    text: qsTr("Binding markers to input while hidden columns are not edited is not recommended: markers may move in unexpected ways.")
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    color: "#b00020"
                }
                CheckBox {
                    text: qsTr("Include frame numbers when copying input")
                    checked: win.pianoRoll.copyFrameNumbers
                    onToggled: win.pianoRoll.copyFrameNumbers = checked
                }
                RowLayout {
                    Label { text: qsTr("Max undo count:") }
                    SpinBox {
                        from: 1; to: 100000; editable: true
                        value: win.pianoRoll.maxUndoSteps
                        onValueModified: win.pianoRoll.maxUndoSteps = value
                    }
                }
                Item { Layout.fillHeight: true }
            }

            ColumnLayout {
                CheckBox {
                    text: qsTr("Denote markers with color on frame column")
                    checked: win.pianoRoll.denoteMarkersColor
                    onToggled: win.pianoRoll.denoteMarkersColor = checked
                }
                CheckBox {
                    text: qsTr("Denote markers with icons")
                    checked: win.pianoRoll.denoteMarkersIcons
                    onToggled: win.pianoRoll.denoteMarkersIcons = checked
                }
                CheckBox {
                    text: qsTr("Denote states with background color")
                    checked: win.pianoRoll.denoteStatesColor
                    onToggled: win.pianoRoll.denoteStatesColor = checked
                }
                CheckBox {
                    text: qsTr("Denote states with icons")
                    checked: win.pianoRoll.denoteStatesIcons
                    onToggled: win.pianoRoll.denoteStatesIcons = checked
                }
                RowLayout {
                    Button { text: qsTr("Set Colors..."); onClicked: colorsWindow.show() }
                    Button { text: qsTr("Set Font..."); onClicked: fontDialog.open() }
                    Button { text: qsTr("Window's Font"); enabled: win.pianoRoll.rollFont !== ""; onClicked: win.pianoRoll.rollFont = "" }
                }
                Item { Layout.fillHeight: true }
            }

            ColumnLayout {
                CheckBox {
                    text: qsTr("Automatically pause at end of movie")
                    checked: win.session.pauseAtEnd
                    onToggled: win.session.pauseAtEnd = checked
                }
                RowLayout {
                    Label { text: qsTr("Rewind step:") }
                    SpinBox {
                        from: 1; to: 10000; editable: true
                        value: win.session.rewindStep
                        onValueModified: win.session.rewindStep = value
                    }
                }
                RowLayout {
                    Label { text: qsTr("Fast rewind step:") }
                    SpinBox {
                        from: 1; to: 10000; editable: true
                        value: win.session.rewindStepFast
                        onValueModified: win.session.rewindStepFast = value
                    }
                }
                Item { Layout.fillHeight: true }
            }

            ColumnLayout {
                RowLayout {
                    Label { text: qsTr("Autosave frequency:") }
                    SpinBox {
                        from: 0; to: 86400; editable: true
                        value: win.session.autosaveSeconds
                        onValueModified: win.session.autosaveSeconds = value
                    }
                    Label { text: qsTr("seconds") }
                }
                Label { text: qsTr("Set to 0 to disable autosave"); opacity: 0.75 }
                CheckBox {
                    text: qsTr("Save as .backup (no greenzone)")
                    checked: win.session.autosaveAsBackup
                    onToggled: win.session.autosaveAsBackup = checked
                }
                CheckBox {
                    text: qsTr("When manually saving, also save .backup")
                    checked: win.session.backupOnSave
                    onToggled: win.session.backupOnSave = checked
                }
                Item { Layout.fillHeight: true }
            }

            ColumnLayout {
                CheckBox {
                    text: qsTr("Load branch by double clicking")
                    checked: win.branches.loadOnDoubleClick
                    onToggled: win.branches.loadOnDoubleClick = checked
                }
                CheckBox {
                    text: qsTr("Old control scheme for branches (loading goes to the branch's frame)")
                    checked: win.branches.oldControlScheme
                    onToggled: win.branches.oldControlScheme = checked
                }
                Item { Layout.fillHeight: true }
            }

            ColumnLayout {
                RowLayout {
                    ColumnLayout {
                        Label { text: qsTr("Choose pattern (for a column's Auto-hold):") }
                        Repeater {
                            model: [["hold", qsTr("Hold")], ["autofire", qsTr("Auto-Fire")], ["custom", qsTr("Custom")]]
                            RadioButton {
                                required property var modelData
                                text: modelData[1]
                                checked: win.pianoRoll.patternKind === modelData[0]
                                onToggled: if (checked) win.pianoRoll.patternKind = modelData[0]
                            }
                        }
                    }
                    ColumnLayout {
                        Label { text: qsTr("Use pattern for painting:") }
                        Repeater {
                            model: [["always", qsTr("Always")], ["autofire", qsTr("Auto-Fire columns only")], ["never", qsTr("Never")]]
                            RadioButton {
                                required property var modelData
                                text: modelData[1]
                                checked: win.pianoRoll.patternMode === modelData[0]
                                onToggled: if (checked) win.pianoRoll.patternMode = modelData[0]
                            }
                        }
                    }
                }
                Label { text: qsTr("Set custom patterns (per movie; Alt held paints a button's):") }
                // a control's own pattern, as runs: "On x2, Off x1", or for an analog control "127 x2, 0 x1"
                ComboBox {
                    id: control
                    Layout.fillWidth: true
                    model: win.pianoRoll.rows("patterns")
                    textRole: "name"
                    // the machine's controls come when it opens: the first is picked then
                    onCountChanged: if (currentIndex < 0 && count > 0) currentIndex = 0
                    onCurrentIndexChanged: Qt.callLater(patternEditor.load)
                }
                Instantiator {
                    id: patternRows
                    model: win.pianoRoll.rows("patterns")
                    delegate: QtObject {
                        required property string name
                        required property string runs
                        required property int loop
                        required property bool skipLag
                    }
                    // a pattern set (or a project's read) is shown as it now is
                    onObjectAdded: Qt.callLater(patternEditor.load)
                }
                GridLayout {
                    id: patternEditor
                    columns: 2
                    function load() {
                        const row = patternRows.objectAt(control.currentIndex)
                        if (!row) return
                        runs.text = row.runs
                        loopTo.value = row.loop
                        lag.checked = row.skipLag
                    }
                    Label { text: qsTr("Pattern:") }
                    TextField { id: runs; Layout.fillWidth: true }
                    Label { text: qsTr("Loop to:") }
                    SpinBox { id: loopTo; from: 0; to: 100000; editable: true }
                    Item { width: 1 }
                    CheckBox { id: lag; text: qsTr("Account for lag frames") }
                }
                RowLayout {
                    Label { text: win.pianoRoll.error; color: "#b00020"; Layout.fillWidth: true; elide: Text.ElideRight }
                    Button {
                        text: qsTr("Set")
                        enabled: control.currentIndex >= 0
                        onClicked: win.pianoRoll.run("setPattern", [control.currentText, runs.text, loopTo.value, lag.checked])
                    }
                }
                Item { Layout.fillHeight: true }
            }
        }
    }

    FontDialog {
        id: fontDialog
        title: qsTr("The piano roll's font")
        onAccepted: win.pianoRoll.rollFont = JSON.stringify({ family: selectedFont.family, pointSize: selectedFont.pointSize,
                                                               bold: selectedFont.bold, italic: selectedFont.italic })
    }

    // TAStudio's Set Colors: the roll's colours, each the theme's until one is picked
    ApplicationWindow {
        id: colorsWindow
        objectName: "tastudioColorsWindow"
        title: qsTr("TAStudio Colors")
        width: 420
        height: 440
        readonly property var own: { try { return JSON.parse(win.pianoRoll.rollColors) } catch (e) { return {} } }
        function setRole(role, value) {
            const all = Object.assign({}, colorsWindow.own)
            if (value === "") delete all[role]
            else all[role] = value
            win.pianoRoll.rollColors = JSON.stringify(all)
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            Repeater {
                model: [["TasCurrentFrame", qsTr("Current frame")], ["TasGreenZoneInput", qsTr("Greenzone")],
                        ["TasGreenZoneInputStated", qsTr("Greenzone, state held")], ["TasGreenZoneInputInvalidated", qsTr("Greenzone, invalidated")],
                        ["TasLagZoneInput", qsTr("Lag")], ["TasLagZoneInputStated", qsTr("Lag, state held")],
                        ["TasLagZoneInputInvalidated", qsTr("Lag, invalidated")], ["TasMarker", qsTr("Marker")],
                        ["TasDefaultRow", qsTr("Other frames")], ["TasAlternatePlayer", qsTr("Every second player")]]
                RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Label { text: modelData[1]; Layout.fillWidth: true }
                    Rectangle {
                        width: 40; height: 18; border.color: palette.mid
                        color: colorsWindow.own[modelData[0]] || "transparent"
                    }
                    Button {
                        text: qsTr("Pick...")
                        onClicked: { colorDialog.role = modelData[0]; colorDialog.selectedColor = colorsWindow.own[modelData[0]] || "white"; colorDialog.open() }
                    }
                    Button {
                        text: qsTr("Theme's")
                        enabled: colorsWindow.own[modelData[0]] !== undefined
                        onClicked: colorsWindow.setRole(modelData[0], "")
                    }
                }
            }
            Item { Layout.fillHeight: true }
            Button { text: qsTr("All the Theme's"); onClicked: win.pianoRoll.rollColors = "{}" }
        }
        ColorDialog {
            id: colorDialog
            property string role: ""
            onAccepted: colorsWindow.setRole(role, selectedColor.toString())
        }
    }
}
