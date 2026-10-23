// Chimera's Display Configuration (Config > Display), laid out as Chimera's
// DisplayConfig: Scaling & Filtering (the final filter, the aspect ratio,
// cropping, Defaults), Misc (Display Features, static window titles) and
// Window (the chrome windowed and full screen, double-click full screen).
// Its boxes are a copy: OK keeps them, Cancel lets them go. Layout and
// bindings only, over the display view model and the session's switches.
//
// Not here: prescaling and Auto Prescale, Scale OSD text with system scale
// (Qt scales the whole window with the screen), the snowy null core (the
// prototype shows Chimera's mark instead), Windows fullscreen hacks, and
// Mouse Capture Forces Stay on Top (the prototype does not capture the
// pointer).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "displayWindow"
    required property QtObject vm
    required property QtObject session
    property Window mainWindow: null
    title: qsTr("Display Configuration")
    width: 690
    height: 440
    modality: Qt.ApplicationModal
    transientParent: win.mainWindow

    // the display view model's settings the window shows, and the session's two
    readonly property var vmNames: ["finalFilter", "letterbox", "integerScale", "aspectMode", "customWidth", "customHeight", "ratioX",
        "ratioY", "cropLeft", "cropTop", "cropRight", "cropBottom", "minimalDisplay", "staticWindowTitles", "titleSuffix", "captionWindowed",
        "menuWindowed", "frameWindowed", "saveWindowPosition", "stayOnTop", "menuFullscreen", "statusBarFullscreen",
        "autohideMouseFullscreen", "allowDoubleClickFullscreen"]
    property var picked: ({})
    onVisibleChanged: if (visible) {
        tabs.currentIndex = 0
        const p = {}
        for (const n of win.vmNames) p[n] = win.vm[n]
        p.displayNothing = win.session.displayNothing
        p.showStatusBar = win.session.showStatusBar
        win.picked = p
    }
    function pick(name, value) {
        const now = Object.assign({}, win.picked)
        now[name] = value
        win.picked = now
    }
    function keep() {
        for (const n of win.vmNames) win.vm[n] = win.picked[n]
        win.session.displayNothing = win.picked.displayNothing
        win.session.showStatusBar = win.picked.showStatusBar
    }

    component Box: CheckBox {
        required property string setting
        checked: win.picked[setting] === true
        onToggled: win.pick(setting, checked)
        padding: 2
    }
    component Number: SpinBox {
        required property string setting
        from: 0
        to: 10000
        editable: true
        value: win.picked[setting] || 0
        onValueModified: win.pick(setting, value)
        Layout.preferredWidth: 90
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 9
        spacing: 6
        TabBar {
            id: tabs
            Layout.fillWidth: true
            TabButton { text: qsTr("Scaling && Filtering"); width: implicitWidth }
            TabButton { text: qsTr("Misc"); width: implicitWidth }
            TabButton { text: qsTr("Window"); width: implicitWidth }
        }
        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            StackLayout {
                anchors.fill: parent
                currentIndex: tabs.currentIndex
                // Scaling & Filtering
                RowLayout {
                    spacing: 8
                    ColumnLayout {
                        Layout.alignment: Qt.AlignTop
                        GroupBox {
                            title: qsTr("Final Filter")
                            ColumnLayout {
                                spacing: 0
                                RadioButton { text: qsTr("None"); checked: win.picked.finalFilter === 0; onClicked: win.pick("finalFilter", 0); padding: 2 }
                                RadioButton { text: qsTr("Bilinear"); checked: win.picked.finalFilter === 1; onClicked: win.pick("finalFilter", 1); padding: 2 }
                            }
                        }
                        GroupBox {
                            title: qsTr("Cropping")
                            GridLayout {
                                columns: 2
                                Label { text: qsTr("Left:") }
                                Number { setting: "cropLeft"; to: 4000 }
                                Label { text: qsTr("Top:") }
                                Number { setting: "cropTop"; to: 4000 }
                                Label { text: qsTr("Right:") }
                                Number { setting: "cropRight"; to: 4000 }
                                Label { text: qsTr("Bottom:") }
                                Number { setting: "cropBottom"; to: 4000 }
                            }
                        }
                        // Chimera's Defaults: bilinear, letterboxed, the system's ratio, nothing cropped
                        Button {
                            text: qsTr("Defaults")
                            onClicked: {
                                const now = Object.assign({}, win.picked)
                                Object.assign(now, { finalFilter: 1, letterbox: true, aspectMode: 1, cropLeft: 0, cropTop: 0, cropRight: 0, cropBottom: 0 })
                                win.picked = now
                            }
                        }
                    }
                    ColumnLayout {
                        Layout.alignment: Qt.AlignTop
                        Layout.fillWidth: true
                        Box { setting: "letterbox"; text: qsTr("Maintain aspect ratio (letterbox)") }
                        // as Chimera's: the choices below only matter with the ratio kept
                        Box { setting: "integerScale"; text: qsTr("Expand pixels by integers only (e.g. no 1.3333x)"); enabled: win.picked.letterbox === true }
                        GroupBox {
                            title: qsTr("Aspect Ratio Selection")
                            Layout.fillWidth: true
                            enabled: win.picked.letterbox === true
                            ColumnLayout {
                                spacing: 0
                                RadioButton { text: qsTr("Use 1:1 pixel size (for crispness or debugging)"); checked: win.picked.aspectMode === 0; onClicked: win.pick("aspectMode", 0); padding: 2 }
                                RadioButton { text: qsTr("Use system's recommendation"); checked: win.picked.aspectMode === 1; onClicked: win.pick("aspectMode", 1); padding: 2 }
                                Label { text: qsTr("Allow pixel distortion (e.g. 2x1 pixels, for better AR fit):"); topPadding: 6 }
                                RowLayout {
                                    RadioButton { text: qsTr("Use custom size:"); checked: win.picked.aspectMode === 2; onClicked: win.pick("aspectMode", 2); padding: 2 }
                                    Number { setting: "customWidth" }
                                    Label { text: "x" }
                                    Number { setting: "customHeight" }
                                }
                                RowLayout {
                                    RadioButton { text: qsTr("Use custom AR:"); checked: win.picked.aspectMode === 3; onClicked: win.pick("aspectMode", 3); padding: 2 }
                                    TextField {
                                        text: win.picked.ratioX !== undefined ? String(win.picked.ratioX) : ""
                                        validator: DoubleValidator { bottom: 0.01 }
                                        onEditingFinished: win.pick("ratioX", Number(text))
                                        Layout.preferredWidth: 64
                                    }
                                    Label { text: ":" }
                                    TextField {
                                        text: win.picked.ratioY !== undefined ? String(win.picked.ratioY) : ""
                                        validator: DoubleValidator { bottom: 0.01 }
                                        onEditingFinished: win.pick("ratioY", Number(text))
                                        Layout.preferredWidth: 64
                                    }
                                }
                            }
                        }
                    }
                }
                // Misc
                ColumnLayout {
                    GroupBox {
                        title: qsTr("Display Features (for speeding up replays)")
                        ColumnLayout {
                            spacing: 0
                            RadioButton {
                                text: qsTr("Full - Display Everything"); padding: 2
                                checked: !win.picked.displayNothing && !win.picked.minimalDisplay
                                onClicked: { win.pick("displayNothing", false); win.pick("minimalDisplay", false) }
                            }
                            RadioButton {
                                text: qsTr("Minimal - Display HUD Only (TBD)"); padding: 2
                                checked: !win.picked.displayNothing && win.picked.minimalDisplay === true
                                onClicked: { win.pick("displayNothing", false); win.pick("minimalDisplay", true) }
                            }
                            RadioButton {
                                text: qsTr("Absolute Zero - Display Nothing"); padding: 2
                                checked: win.picked.displayNothing === true
                                onClicked: { win.pick("displayNothing", true); win.pick("minimalDisplay", false) }
                            }
                        }
                    }
                    RowLayout {
                        spacing: 16
                        Box { setting: "staticWindowTitles"; text: qsTr("Keep window titles static") }
                        Label { text: qsTr("Window title suffix (static and not):") }
                        TextField {
                            text: win.picked.titleSuffix || ""
                            placeholderText: qsTr("Chimera")
                            onTextEdited: win.pick("titleSuffix", text)
                            Layout.preferredWidth: 80
                        }
                    }
                    Label {
                        text: qsTr("Some tools put filenames, status, etc. in their window titles.\nChecking this disables those features.")
                        Layout.leftMargin: 20
                    }
                    Item { Layout.fillHeight: true }
                }
                // Window
                ColumnLayout {
                    RowLayout {
                        Layout.alignment: Qt.AlignTop
                        spacing: 8
                        GroupBox {
                            title: qsTr("Windowed")
                            Layout.alignment: Qt.AlignTop
                            ColumnLayout {
                                spacing: 0
                                Box { setting: "captionWindowed"; text: qsTr("Caption") }
                                Box { setting: "menuWindowed"; text: qsTr("Menu") }
                                Box { setting: "showStatusBar"; text: qsTr("Status Bar") }
                                RowLayout {
                                    Label { text: qsTr("Frame:") }
                                    Slider {
                                        from: 0; to: 2; stepSize: 1; snapMode: Slider.SnapAlways
                                        value: win.picked.frameWindowed !== undefined ? win.picked.frameWindowed : 2
                                        onMoved: win.pick("frameWindowed", value)
                                        Layout.preferredWidth: 100
                                    }
                                    Label { text: [qsTr("None"), qsTr("Thin"), qsTr("Thick")][win.picked.frameWindowed !== undefined ? win.picked.frameWindowed : 2] }
                                }
                                Box { setting: "saveWindowPosition"; text: qsTr("Save Window Position") }
                                // Chimera hides it where it does not work: everywhere but Windows
                                Box {
                                    setting: "stayOnTop"; text: qsTr("Stay on Top")
                                    visible: Qt.platform.os === "windows"; height: visible ? implicitHeight : 0
                                }
                            }
                        }
                        GroupBox {
                            title: qsTr("Fullscreen")
                            Layout.alignment: Qt.AlignTop
                            ColumnLayout {
                                spacing: 0
                                Box { setting: "menuFullscreen"; text: qsTr("Menu") }
                                Box { setting: "statusBarFullscreen"; text: qsTr("Status Bar") }
                                Box { setting: "autohideMouseFullscreen"; text: qsTr("Auto-Hide Mouse Cursor") }
                            }
                        }
                    }
                    Box { setting: "allowDoubleClickFullscreen"; text: qsTr("Allow Double-Click Fullscreen (hold shift to force fullscreen to toggle in case using zapper, etc.)") }
                    Item { Layout.fillHeight: true }
                }
            }
        }
        RowLayout {
            Item { Layout.fillWidth: true }
            Button { text: qsTr("OK"); onClicked: { win.keep(); win.close() } }
            Button { text: qsTr("Cancel"); onClicked: win.close() }
        }
    }
}
