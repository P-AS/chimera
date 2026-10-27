// Chimera's Pre-Compiled Modules (Tools), laid out as its form: how many
// games have compiled code and what removing costs at the top, a row a game
// with a box to tick (Game, Compiled by, When, Modules, Size; a game nothing
// can name greyed, one missing pieces in the error colour), what the row
// picked means under it, and Remove (with what it frees), Open folder and
// Close. Removing costs the minutes to compile a game again, never work.
// Layout and bindings only, over the precompiled view model.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "precompiledWindow"
    required property QtObject vm
    property var themeColor: (role, fallback) => fallback
    title: qsTr("Pre-Compiled Modules")
    width: 900
    height: 460
    minimumWidth: 620
    minimumHeight: 340
    readonly property var widths: [300, 180, 150, 80, 100]
    // the games ticked by their folders, and the one picked
    property var ticked: ({})
    property string picked: ""
    onVisibleChanged: if (visible) { win.vm.run("refresh"); win.ticked = {}; win.picked = "" }
    // the places in the list of the games ticked
    function tickedPlaces() {
        const out = []
        for (let i = 0; i < list.count; i++) { const it = list.itemAtIndex(i); if (it && win.ticked[it.folder]) out.push(i) }
        return out.join(",")
    }
    readonly property string tickedSize: Object.keys(win.ticked).length === 0 ? "" : (win.vm.run("sizeOf", [win.tickedPlaces()]) || "")

    MessageDialog {
        id: removeAsk
        title: qsTr("Pre-Compiled Modules")
        property string places: ""
        property string what: ""
        text: qsTr("Remove %1?\n\n%2 will be freed. Each game is compiled again the next time a project needs it.").arg(removeAsk.what).arg(win.tickedSize)
        buttons: MessageDialog.Yes | MessageDialog.No
        onButtonClicked: (button) => { if (button === MessageDialog.Yes) { win.vm.run("remove", [removeAsk.places]); win.ticked = {} } }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6
        Label { text: win.vm.header; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.preferredHeight: 36 }
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: palette.base
            border.color: palette.mid
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 1
                spacing: 0
                Row {
                    Repeater {
                        model: [qsTr("Game"), qsTr("Compiled by"), qsTr("When"), qsTr("Modules"), qsTr("Size")]
                        delegate: Label {
                            required property int index
                            required property string modelData
                            text: modelData
                            width: win.widths[index] + (index === 0 ? 22 : 0)
                            leftPadding: 4; topPadding: 2; bottomPadding: 2
                            horizontalAlignment: index >= 3 ? Text.AlignRight : Text.AlignLeft
                            rightPadding: index >= 3 ? 4 : 0
                            background: Rectangle { color: palette.button; border.color: palette.mid }
                        }
                    }
                }
                ListView {
                    id: list
                    objectName: "precompiledList"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: win.vm.rows("games")
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        id: row
                        required property int index
                        required property string name
                        required property string by
                        required property string when
                        required property string modulesText
                        required property string sizeShown
                        required property string folder
                        required property string note
                        required property string look
                        width: list.width
                        height: 22
                        readonly property bool isPicked: row.folder === win.picked
                        color: isPicked ? palette.highlight : "transparent"
                        readonly property color ink: isPicked ? palette.highlightedText
                            : row.look === "grey" ? palette.mid
                            : row.look === "error" ? win.themeColor("AccentError", "#c62828") : palette.text
                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            CheckBox {
                                checked: win.ticked[row.folder] === true
                                onClicked: { const now = Object.assign({}, win.ticked); if (checked) now[row.folder] = true; else delete now[row.folder]; win.ticked = now }
                                padding: 0
                                width: 22
                            }
                            Label { text: row.name; color: row.ink; width: win.widths[0]; leftPadding: 2; elide: Text.ElideRight }
                            Label { text: row.by; color: row.ink; width: win.widths[1]; leftPadding: 4; elide: Text.ElideRight }
                            Label { text: row.when; color: row.ink; width: win.widths[2]; leftPadding: 4 }
                            Label { text: row.modulesText; color: row.ink; width: win.widths[3]; horizontalAlignment: Text.AlignRight; rightPadding: 4 }
                            Label { text: row.sizeShown; color: row.ink; width: win.widths[4]; horizontalAlignment: Text.AlignRight; rightPadding: 4 }
                        }
                        TapHandler { onTapped: { win.picked = row.folder; detail.text = row.note } }
                    }
                }
            }
        }
        Label { id: detail; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.preferredHeight: 34 }
        RowLayout {
            Layout.fillWidth: true
            Button {
                text: win.tickedSize === "" ? qsTr("&Remove") : qsTr("&Remove (%1)").arg(win.tickedSize)
                Layout.preferredWidth: 150
                enabled: Object.keys(win.ticked).length > 0
                onClicked: {
                    const places = win.tickedPlaces()
                    const n = places.split(",").filter(s => s !== "").length
                    removeAsk.places = places
                    removeAsk.what = n === 1 ? (list.itemAtIndex(parseInt(places)) ? list.itemAtIndex(parseInt(places)).name : "") : qsTr("%1 games' compiled code").arg(n)
                    removeAsk.open()
                }
            }
            Button {
                text: qsTr("&Open folder"); Layout.preferredWidth: 150
                enabled: win.picked !== ""
                onClicked: Qt.openUrlExternally(localFiles.folderUrl(win.picked))
            }
            // Chimera's: how much memory one compile session is counted at
            Label { text: qsTr("&Memory per compile session (GB):"); Layout.leftMargin: 12 }
            SpinBox {
                from: 1; to: 256
                value: win.vm.sessionGigabytes
                onValueModified: win.vm.sessionGigabytes = value
                Layout.preferredWidth: 90
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("How much memory one compile session is counted at. Chimera runs as many sessions side by side as the computer can give this much each: a smaller figure runs more at once, a larger one fewer. 8 is what one RPCS3 session was measured to need.")
            }
            Label { text: win.vm.error !== "" ? win.vm.error : win.vm.status; color: win.vm.error !== "" ? win.themeColor("AccentError", "#b00020") : palette.text; Layout.fillWidth: true; elide: Text.ElideRight }
            Button { text: qsTr("&Close"); Layout.preferredWidth: 150; onClicked: win.close() }
        }
    }
}
