// Chimera's Display window (Config > Display): layout and bindings only,
// over the display view model. Kept the moment changed.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "displayWindow"
    required property QtObject vm
    required property QtObject session
    title: qsTr("Display Configuration")
    width: 560
    height: 500

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        TabBar {
            id: tabs
            Layout.fillWidth: true
            TabButton { text: qsTr("Scaling && Filtering") }
            TabButton { text: qsTr("Window") }
        }
        StackLayout {
            currentIndex: tabs.currentIndex
            Layout.fillWidth: true
            Layout.fillHeight: true
            ColumnLayout {
                GroupBox {
                    title: qsTr("Final Filter")
                    Layout.fillWidth: true
                    RowLayout {
                        RadioButton { text: qsTr("None"); checked: win.vm.finalFilter === 0; onClicked: win.vm.finalFilter = 0 }
                        RadioButton { text: qsTr("Bilinear"); checked: win.vm.finalFilter === 1; onClicked: win.vm.finalFilter = 1 }
                    }
                }
                GroupBox {
                    title: qsTr("Aspect Ratio")
                    Layout.fillWidth: true
                    ColumnLayout {
                        CheckBox { text: qsTr("Maintain aspect ratio (letterbox)"); checked: win.vm.letterbox; onToggled: win.vm.letterbox = checked }
                        CheckBox { text: qsTr("Expand pixels by integers only"); checked: win.vm.integerScale; onToggled: win.vm.integerScale = checked }
                        RadioButton { text: qsTr("Use 1:1 pixel size (for crispness or debugging)"); checked: win.vm.aspectMode === 0; onClicked: win.vm.aspectMode = 0 }
                        RadioButton { text: qsTr("Use system's recommendation"); checked: win.vm.aspectMode === 1; onClicked: win.vm.aspectMode = 1 }
                        RowLayout {
                            RadioButton { text: qsTr("Use custom size:"); checked: win.vm.aspectMode === 2; onClicked: win.vm.aspectMode = 2 }
                            SpinBox { from: 0; to: 10000; editable: true; value: win.vm.customWidth; onValueModified: win.vm.customWidth = value }
                            Label { text: "x" }
                            SpinBox { from: 0; to: 10000; editable: true; value: win.vm.customHeight; onValueModified: win.vm.customHeight = value }
                        }
                        RowLayout {
                            RadioButton { text: qsTr("Use custom ratio:"); checked: win.vm.aspectMode === 3; onClicked: win.vm.aspectMode = 3 }
                            TextField {
                                text: win.vm.ratioX
                                validator: DoubleValidator { bottom: 0.01 }
                                onEditingFinished: win.vm.ratioX = Number(text)
                                Layout.preferredWidth: 64
                            }
                            Label { text: ":" }
                            TextField {
                                text: win.vm.ratioY
                                validator: DoubleValidator { bottom: 0.01 }
                                onEditingFinished: win.vm.ratioY = Number(text)
                                Layout.preferredWidth: 64
                            }
                        }
                    }
                }
                GroupBox {
                    title: qsTr("Crop (pixels)")
                    Layout.fillWidth: true
                    GridLayout {
                        columns: 4
                        Label { text: qsTr("Left") }
                        SpinBox { from: 0; to: 4000; editable: true; value: win.vm.cropLeft; onValueModified: win.vm.cropLeft = value }
                        Label { text: qsTr("Top") }
                        SpinBox { from: 0; to: 4000; editable: true; value: win.vm.cropTop; onValueModified: win.vm.cropTop = value }
                        Label { text: qsTr("Right") }
                        SpinBox { from: 0; to: 4000; editable: true; value: win.vm.cropRight; onValueModified: win.vm.cropRight = value }
                        Label { text: qsTr("Bottom") }
                        SpinBox { from: 0; to: 4000; editable: true; value: win.vm.cropBottom; onValueModified: win.vm.cropBottom = value }
                    }
                }
                Item { Layout.fillHeight: true }
            }
            ColumnLayout {
                GroupBox {
                    title: qsTr("Speed/Display")
                    Layout.fillWidth: true
                    ColumnLayout {
                        RadioButton { text: qsTr("Full - the picture and everything over it"); checked: !win.session.displayNothing; onClicked: win.session.displayNothing = false }
                        RadioButton { text: qsTr("Absolute Zero - nothing drawn, the speed in the title"); checked: win.session.displayNothing; onClicked: win.session.displayNothing = true }
                    }
                }
                CheckBox { text: qsTr("Status bar"); checked: win.session.showStatusBar; onToggled: win.session.showStatusBar = checked }
                CheckBox { text: qsTr("Save window position"); checked: win.vm.saveWindowPosition; onToggled: win.vm.saveWindowPosition = checked }
                Item { Layout.fillHeight: true }
            }
        }
        RowLayout {
            Item { Layout.fillWidth: true }
            Button { text: qsTr("Close"); onClicked: win.close() }
        }
    }
}
