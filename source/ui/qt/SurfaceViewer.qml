// The Surface Viewer, as Chimera's: a File menu (Copy to Clipboard, Save as
// PNG...), a toolbar with the surface and the zoom (1x to 4x), the surface
// itself on the viewport's colour, and the status line with its name and
// size. Layout and bindings only, over the surfaceViewer view model.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Chimera

ApplicationWindow {
    id: win
    objectName: "surfaceViewerWindow"
    required property QtObject vm
    required property QtObject session
    property QtObject clipboard: null
    property var localPath: (url) => url.toString()
    property var themeColor: (role, fallback) => fallback
    property QtObject customize: null
    property QtObject toolWindows: null
    property Window mainWindow: null
    title: qsTr("Surface Viewer")
    width: 560
    height: 480
    onVisibleChanged: {
        win.vm.watching = visible
        if (visible) windowOptions.placeBack(false)
    }
    readonly property var list: JSON.parse(win.session.surfaces)
    readonly property var shown: win.list[win.vm.picked] || null

    WindowOptions { id: windowOptions; window: win; tool: "surfaceViewer"; vm: win.toolWindows; mainWindow: win.mainWindow }

    // the surface at its own size, for the clipboard or a file
    function grabbed(then) {
        if (!win.shown) return
        surface.grabToImage((result) => then(result), Qt.size(win.shown.width, win.shown.height))
    }
    FileDialog {
        id: pngDialog
        title: qsTr("Save as PNG")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "png"
        nameFilters: [qsTr("PNG files (*.png)")]
        onAccepted: { const path = win.localPath(selectedFile); win.grabbed((r) => r.saveToFile(path)) }
    }

    menuBar: MenuBar {
        Menu {
            delegate: MenuEntry {}
            title: qsTr("&File")
            MenuItem { text: qsTr("&Copy to Clipboard"); enabled: win.shown !== null; onTriggered: win.grabbed((r) => { if (win.clipboard) win.clipboard.setImage(r.image) }) }
            MenuItem { text: qsTr("&Save as PNG..."); enabled: win.shown !== null; onTriggered: pngDialog.open() }
            MenuDivider {}
            MenuItem { text: qsTr("&Close"); onTriggered: win.close() }
        }
        // Chimera adds a Settings menu for the window's items to a tool that has none
        Menu {
            delegate: MenuEntry {}
            title: qsTr("&Settings")
            MenuItem { text: qsTr("Save Window &Position"); checkable: true; checked: windowOptions.savePosition; onTriggered: windowOptions.set("save", checked) }
            MenuItem {
                text: qsTr("Stay on &Top"); checkable: true; checked: windowOptions.onTop
                visible: windowOptions.canStayOnTop; height: visible ? implicitHeight : 0
                onTriggered: windowOptions.set("top", checked)
            }
            MenuItem { text: qsTr("&Float From Main Window"); checkable: true; checked: windowOptions.floating; onTriggered: windowOptions.set("float", checked) }
            MenuItem { text: qsTr("Restore &Defaults"); onTriggered: { win.vm.zoom = 1; windowOptions.restore() } }
        }
    }

    header: ToolBar {
        RowLayout {
            Label { text: qsTr("Surface:") }
            ComboBox {
                model: win.list.map(s => s.name)
                currentIndex: win.vm.picked
                onActivated: (i) => win.vm.picked = i
                Layout.preferredWidth: 200
            }
            Label { text: qsTr("Zoom:") }
            ComboBox {
                model: ["1x", "2x", "3x", "4x"]
                currentIndex: Math.max(0, win.vm.zoom - 1)
                onActivated: (i) => win.vm.zoom = i + 1
                Layout.preferredWidth: 70
            }
        }
    }

    footer: Label { text: win.vm.status; padding: 4; elide: Text.ElideRight }

    Rectangle {
        anchors.fill: parent
        color: win.themeColor("EmulatorViewport", "black")
        ScrollView {
            anchors.fill: parent
            clip: true
            PictureItem {
                id: surface
                width: win.shown ? win.shown.width * win.vm.zoom : 0
                height: win.shown ? win.shown.height * win.vm.zoom : 0
                source: win.vm.picture("surface")
                fit: ({ "aspectMode": 0 })
            }
        }
    }
}
