// Chimera's Sound window (Config > Sound): layout and bindings only, over
// the session's sound settings. Kept the moment changed; a new device or
// buffer opens the sound again.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "soundWindow"
    required property QtObject session
    title: qsTr("Sound Configuration")
    width: 520
    height: 420
    onVisibleChanged: if (visible) win.session.run("findSoundDevices")
    readonly property var devices: JSON.parse(win.session.soundDevices)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            ColumnLayout {
                CheckBox { text: qsTr("Sound Master Enable"); checked: win.session.soundOn; onToggled: win.session.soundOn = checked }
                CheckBox { text: qsTr("Mute Frame Advance"); checked: win.session.muteFrameAdvance; onToggled: win.session.muteFrameAdvance = checked }
                GroupBox {
                    title: qsTr("Volume")
                    GridLayout {
                        columns: 3
                        Label { text: qsTr("Normal") }
                        CheckBox { text: qsTr("On"); checked: win.session.soundNormalOn; onToggled: win.session.soundNormalOn = checked }
                        SpinBox { from: 0; to: 100; editable: true; value: win.session.volume; onValueModified: win.session.volume = value }
                        Label { text: qsTr("Fast forward") }
                        CheckBox { text: qsTr("On"); checked: win.session.soundFastOn; onToggled: win.session.soundFastOn = checked }
                        SpinBox { from: 0; to: 100; editable: true; value: win.session.fastVolume; onValueModified: win.session.fastVolume = value }
                    }
                }
                GroupBox {
                    title: qsTr("Output Method")
                    RadioButton { text: qsTr("OpenAL"); checked: true; enabled: false }
                }
                RowLayout {
                    Label { text: qsTr("Buffer Size:") }
                    SpinBox { from: 10; to: 1000; stepSize: 10; editable: true; value: win.session.soundBufferMs; onValueModified: win.session.soundBufferMs = value }
                    Label { text: qsTr("milliseconds") }
                }
                Item { Layout.fillHeight: true }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Label { text: qsTr("Sound Device:") }
                ListView {
                    id: deviceList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: [qsTr("<default>")].concat(win.devices)
                    delegate: ItemDelegate {
                        required property int index
                        required property string modelData
                        width: deviceList.width
                        text: modelData
                        highlighted: index === 0 ? win.session.soundDevice === "" : win.session.soundDevice === modelData
                        onClicked: win.session.soundDevice = index === 0 ? "" : modelData
                    }
                }
            }
        }
        RowLayout {
            Label { text: win.session.sound; Layout.fillWidth: true; elide: Text.ElideRight }
            Button { text: qsTr("Close"); onClicked: win.close() }
        }
    }
}
