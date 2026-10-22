// Themes: layout and bindings only, over the themes view model. A theme
// picked is used at once.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "themesWindow"
    required property QtObject vm
    required property QtObject colors
    title: qsTr("Themes")
    width: 560
    height: 600

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        Label { text: win.vm.status; wrapMode: Text.Wrap; Layout.fillWidth: true }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: win.vm.rows("themes")
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                id: theme
                required property string name
                required property string author
                required property string description
                required property bool dark
                required property string windowColor
                required property string textColor
                required property string selectionColor
                required property string buttonColor
                width: list.width - 16
                highlighted: theme.name === win.vm.current
                onClicked: win.vm.run("choose", [theme.name])
                contentItem: RowLayout {
                    // the theme's own colours, small
                    Rectangle {
                        width: 44; height: 30
                        color: win.colors.resolve(theme.windowColor)
                        border.color: palette.mid
                        Text { anchors.centerIn: parent; text: "Aa"; color: win.colors.resolve(theme.textColor); font.bold: true }
                    }
                    Rectangle { width: 12; height: 30; color: win.colors.resolve(theme.selectionColor); border.color: palette.mid }
                    Rectangle { width: 12; height: 30; color: win.colors.resolve(theme.buttonColor); border.color: palette.mid }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Label {
                            text: theme.name + (theme.dark ? qsTr(" (dark)") : "")
                            font.bold: true
                            color: theme.highlighted ? palette.highlightedText : palette.windowText
                        }
                        Label {
                            text: (theme.author !== "" ? theme.author + " - " : "") + theme.description
                            elide: Text.ElideRight
                            maximumLineCount: 1
                            opacity: 0.75
                            color: theme.highlighted ? palette.highlightedText : palette.windowText
                            Layout.fillWidth: true
                        }
                    }
                }
            }
        }
        Label { text: win.vm.error; color: "#b00020"; visible: text !== "" }
    }
}
