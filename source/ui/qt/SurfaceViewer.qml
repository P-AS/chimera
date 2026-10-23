// Chimera's Surface Viewer: layout and bindings only, over the
// surfaceViewer view model.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Chimera

ApplicationWindow {
    id: win
    objectName: "surfaceViewerWindow"
    required property QtObject vm
    required property QtObject session
    title: qsTr("Surface Viewer")
    width: 560
    height: 480
    onVisibleChanged: win.vm.watching = visible
    readonly property var list: JSON.parse(win.session.surfaces)
    readonly property var shown: win.list[win.vm.picked] || null

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 6
        RowLayout {
            Label { text: qsTr("Surface") }
            ComboBox {
                model: win.list.map(s => s.name)
                currentIndex: win.vm.picked
                onActivated: (i) => win.vm.picked = i
                Layout.preferredWidth: 220
            }
            Label { text: qsTr("Zoom") }
            ComboBox {
                model: ["1x", "2x", "3x", "4x", "6x", "8x"]
                readonly property var factors: [1, 2, 3, 4, 6, 8]
                currentIndex: Math.max(0, factors.indexOf(win.vm.zoom))
                onActivated: (i) => win.vm.zoom = factors[i]
            }
            Item { Layout.fillWidth: true }
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            PictureItem {
                width: win.shown ? win.shown.width * win.vm.zoom : 0
                height: win.shown ? win.shown.height * win.vm.zoom : 0
                source: win.vm.picture("surface")
                fit: ({ "aspectMode": 0 })
            }
        }
        Label { text: win.vm.status; elide: Text.ElideRight; Layout.fillWidth: true }
    }
}
