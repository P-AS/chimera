// Chimera's Greenzone budgets (from the Cache Manager), laid out as its
// GreenzoneBudgetsForm: what a history of states may weigh for every
// project (memory, the stride behind the playhead, the kept pictures after
// a load) and, when the Cache Manager's row picked is a project's cache, a
// budget of that project's own, kept beside its cache; OK keeps them,
// Cancel lets them go. Layout and bindings only, over the session (every
// project's) and the cacheManager view model (a project's own).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "greenzoneBudgetsWindow"
    required property QtObject session
    required property QtObject caches
    property Window mainWindow: null
    // the theme's colour for a role (Main hands it its lookup)
    property var themeColor: (role, fallback) => fallback
    // the project the Cache Manager's row picked is ("" for none), and its name
    property string projectId: ""
    property string projectLabelAsked: ""
    // Chimera shortens a long name to 40 characters
    readonly property string projectLabel: win.projectLabelAsked.length <= 40 ? win.projectLabelAsked : win.projectLabelAsked.substring(0, 37) + "..."
    readonly property bool hasProject: win.projectId !== ""
    title: win.hasProject && win.projectLabel !== "" ? qsTr("Greenzone budgets: %1").arg(win.projectLabel) : qsTr("Greenzone budgets")
    width: 470
    height: win.hasProject ? 470 : 300
    modality: Qt.ApplicationModal
    transientParent: win.mainWindow

    // the boxes, read when the window opens
    onVisibleChanged: if (visible) {
        memory.value = win.session.greenzoneBudgetMb
        stride.value = win.session.greenzoneNearStride
        settle.value = win.session.pictureSettleFrames
        const known = win.hasProject ? JSON.parse(win.caches.run("budgetsOf", [win.projectId]) || "{}") : {}
        own.checked = (known.memoryMb || 0) !== 0 || (known.maxNearStride || 0) !== 0
        ownMemory.value = known.memoryMb ? known.memoryMb : win.session.greenzoneBudgetMb
        ownStride.value = known.maxNearStride ? known.maxNearStride : win.session.greenzoneNearStride
    }
    function keep() {
        win.session.greenzoneBudgetMb = memory.value
        win.session.greenzoneNearStride = stride.value
        win.session.pictureSettleFrames = settle.value
        if (!win.hasProject) return
        const asked = own.checked ? [ownMemory.value, ownStride.value] : [0, 0]
        // the project open's through the session, which shows them; another's beside its cache
        if (win.projectId === win.session.projectId) win.session.run("setProjectBudgets", asked)
        else win.caches.run("setBudgetsOf", [win.projectId].concat(asked))
    }

    component Note: Label { wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.columnSpan: 3 }
    component Name: Label { Layout.leftMargin: 12; Layout.preferredWidth: 218 }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        Note { text: qsTr("A greenzone is kept in memory. Past this it is thinned; nothing is written to disk until the project is saved.") }
        Label { text: qsTr("Every project, unless it says otherwise"); font.bold: true; Layout.topMargin: 4 }
        GridLayout {
            columns: 3
            Name { text: qsTr("Keep in memory") }
            SpinBox { id: memory; from: 64; to: 1048576; stepSize: 256; editable: true; Layout.preferredWidth: 110 }
            Label { text: qsTr("MB") }
            Name { text: qsTr("Behind the playhead, 1 frame in") }
            SpinBox { id: stride; from: 1; to: 32; editable: true; Layout.preferredWidth: 110 }
            Item { width: 1 }
            Note { text: qsTr("1 keeps every frame and plays slowest. Higher plays faster on a heavy core.") }
            Name { text: qsTr("After a load, show kept pictures for") }
            SpinBox { id: settle; from: 0; to: 10; editable: true; Layout.preferredWidth: 110 }
            Label { text: qsTr("frames") }
            Note { text: qsTr("On a GPU core the picture can be wrong right after a load. For this many frames a frame seen before shows the picture it had. 0 always shows what the core draws.") }
        }
        Label { text: qsTr("This project"); font.bold: true; visible: win.hasProject; Layout.topMargin: 10 }
        CheckBox {
            id: own
            visible: win.hasProject
            text: qsTr("A different budget for %1").arg(win.projectLabel)
            padding: 2
        }
        GridLayout {
            columns: 3
            visible: win.hasProject
            Name { text: qsTr("Keep in memory") }
            SpinBox { id: ownMemory; enabled: own.checked; from: 64; to: 1048576; stepSize: 256; editable: true; Layout.preferredWidth: 110 }
            Label { text: qsTr("MB") }
            Name { text: qsTr("Behind the playhead, 1 frame in") }
            SpinBox { id: ownStride; enabled: own.checked; from: 1; to: 32; editable: true; Layout.preferredWidth: 110 }
            Item { width: 1 }
            Note { text: qsTr("Takes effect when the project is next opened: a history already holding states is not thrown away to obey a number that has just changed.") }
        }
        Item { Layout.fillHeight: true }
        RowLayout {
            Label { text: win.session.error; color: win.themeColor("AccentError", "#b00020"); Layout.fillWidth: true; elide: Text.ElideRight }
            Button { text: qsTr("OK"); Layout.preferredWidth: 90; onClicked: { win.keep(); win.close() } }
            Button { text: qsTr("Cancel"); Layout.preferredWidth: 90; onClicked: win.close() }
        }
    }
}
