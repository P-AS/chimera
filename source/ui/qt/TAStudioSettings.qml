// TAStudio's settings, as Chimera's TAStudio Settings dialog has them:
// layout and bindings only, over the session, pianoRoll and branches view
// models. A change takes effect at once and is remembered.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "tastudioSettingsWindow"
    required property QtObject session
    required property QtObject pianoRoll
    required property QtObject branches
    title: qsTr("TAStudio Settings")
    width: 520
    height: 420

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8

        TabBar {
            id: tabs
            Layout.fillWidth: true
            TabButton { text: qsTr("Input Roll") }
            TabButton { text: qsTr("Editing") }
            TabButton { text: qsTr("Playback") }
            TabButton { text: qsTr("Autosave") }
            TabButton { text: qsTr("Branches") }
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
        }
    }
}
