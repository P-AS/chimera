// Chimera's Sound Configuration (Config > Sound), laid out as Chimera's
// SoundConfig: the Volume group down the left (Normal and RW & FF, each
// on or off, a slider and its number), Sound Master Enable and Mute Frame
// Advance, Output Method, the sound device, the buffer size, OK and
// Cancel. The window works on a copy that OK keeps; a new device or buffer
// opens the sound again. Layout and bindings only, over the session's
// sound settings.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "soundWindow"
    required property QtObject session
    property Window mainWindow: null
    title: qsTr("Sound Configuration")
    width: 500
    height: 320
    modality: Qt.ApplicationModal
    transientParent: win.mainWindow
    readonly property var devices: JSON.parse(win.session.soundDevices)

    readonly property var names: ["soundOn", "muteFrameAdvance", "soundNormalOn", "soundFastOn", "volume", "fastVolume", "soundBufferMs", "soundDevice"]
    property var picked: ({})
    onVisibleChanged: if (visible) {
        win.session.run("findSoundDevices")
        const p = {}
        for (const n of win.names) p[n] = win.session[n]
        win.picked = p
    }
    function pick(name, value) {
        const now = Object.assign({}, win.picked)
        now[name] = value
        win.picked = now
    }
    function keep() { for (const n of win.names) win.session[n] = win.picked[n] }

    // one volume: on or off, a slider from 100 at the top to 0, and its number
    component Volume: ColumnLayout {
        id: vol
        required property string label
        required property string onSetting
        required property string volumeSetting
        property bool autoOn: false
        spacing: 2
        CheckBox {
            text: qsTr("Ena.")
            checked: win.picked[vol.onSetting] === true
            onToggled: win.pick(vol.onSetting, checked)
            padding: 0
        }
        Label { text: vol.label }
        Slider {
            orientation: Qt.Vertical
            from: 0; to: 100; stepSize: 1
            value: win.picked[vol.volumeSetting] || 0
            onMoved: vol.set(Math.round(value))
            Layout.preferredHeight: 164
            Layout.alignment: Qt.AlignHCenter
        }
        SpinBox {
            from: 0; to: 100; editable: true
            value: win.picked[vol.volumeSetting] || 0
            onValueModified: vol.set(value)
            Layout.preferredWidth: 70
        }
        // Normal's volume moved by hand turns its sound on, or off at 0, as Chimera's
        function set(v) {
            win.pick(vol.volumeSetting, v)
            if (vol.autoOn) win.pick(vol.onSetting, v !== 0)
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 9
        GroupBox {
            title: qsTr("Volume")
            Layout.fillHeight: true
            enabled: win.picked.soundOn === true
            RowLayout {
                Volume { label: qsTr("Normal"); onSetting: "soundNormalOn"; volumeSetting: "volume"; autoOn: true }
                // RW & FF only while Normal is on
                Volume { label: qsTr("RW & FF"); onSetting: "soundFastOn"; volumeSetting: "fastVolume"; enabled: win.picked.soundNormalOn === true }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            RowLayout {
                ColumnLayout {
                    Layout.alignment: Qt.AlignTop
                    spacing: 0
                    CheckBox {
                        text: qsTr("Sound Master Enable")
                        checked: win.picked.soundOn === true
                        onToggled: win.pick("soundOn", checked)
                        padding: 2
                    }
                    Label { text: qsTr("Controls whether cores\neven generate audio."); leftPadding: 22 }
                    CheckBox {
                        text: qsTr("Mute Frame Advance")
                        checked: win.picked.muteFrameAdvance === true
                        onToggled: win.pick("muteFrameAdvance", checked)
                        padding: 2
                        Layout.topMargin: 4
                    }
                }
                Item { Layout.fillWidth: true }
                GroupBox {
                    title: qsTr("Output Method")
                    Layout.alignment: Qt.AlignTop
                    RadioButton { text: qsTr("OpenAL"); checked: true; padding: 2 }
                }
            }
            Label { text: qsTr("Sound Device:") }
            ListView {
                id: deviceList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: [qsTr("<default>")].concat(win.devices)
                ScrollBar.vertical: ScrollBar {}
                delegate: ItemDelegate {
                    required property int index
                    required property string modelData
                    width: deviceList.width
                    padding: 2
                    text: modelData
                    highlighted: index === 0 ? (win.picked.soundDevice || "") === "" : win.picked.soundDevice === modelData
                    onClicked: win.pick("soundDevice", index === 0 ? "" : modelData)
                }
                Rectangle { anchors.fill: parent; color: "transparent"; border.color: palette.mid; z: -1 }
            }
            RowLayout {
                Label { text: qsTr("Buffer Size:") }
                SpinBox {
                    from: 30; to: 250; editable: true
                    value: win.picked.soundBufferMs || 100
                    onValueModified: win.pick("soundBufferMs", value)
                    Layout.preferredWidth: 90
                }
                Label { text: qsTr("milliseconds") }
            }
            RowLayout {
                Label { text: win.session.sound; Layout.fillWidth: true; elide: Text.ElideRight; opacity: 0.8 }
                Button { text: qsTr("&OK"); onClicked: { win.keep(); win.close() } }
                Button { text: qsTr("&Cancel"); onClicked: win.close() }
            }
        }
    }
}
