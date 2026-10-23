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
