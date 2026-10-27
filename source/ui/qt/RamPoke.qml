// RAM Poke, as Chimera's: a value written into the addresses picked, read
// as they are shown; a game property's watch takes the text the engine
// reads. RAM Watch and RAM Search both open it. Layout and bindings only,
// over the view model given (its pokeInfo and pokeSelected).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: poke
    objectName: "ramPokeWindow"
    // a view model with pokeInfo and pokeSelected; how to tell of a game property refused
    property QtObject vm: null
    property var tell: null
    property var info: ({})
    title: qsTr("RAM Poke - %1").arg(poke.info.domain || "")
    width: 300
    height: 250
    modality: Qt.ApplicationModal
    property string said: qsTr("Enter an address to poke:")
    function start(info) {
        poke.info = info
        poke.said = qsTr("Enter an address to poke:")
        valueBox.text = info.value
        poke.show()
        poke.raise()
        valueBox.forceActiveFocus()
        valueBox.selectAll()
    }
    function accept() {
        const ok = poke.vm.run("pokeSelected", [valueBox.text])
        if (ok === true) { poke.close(); return }
        if (poke.info.property) { if (poke.tell) poke.tell(qsTr("Not set"), poke.vm.error); return }
        poke.said = qsTr("An error occurred when writing Value.")
    }
    GridLayout {
        anchors.fill: parent
        anchors.margins: 10
        columns: 2
        Label { Layout.columnSpan: 2; text: poke.said }
        Label { text: poke.info.property ? qsTr("Property:") : qsTr("Address: 0x"); Layout.alignment: Qt.AlignRight }
        Label { text: (poke.info.property ? poke.info.name : poke.info.addresses) || ""; font.family: "monospace"; elide: Text.ElideRight; Layout.fillWidth: true }
        Label { text: qsTr("Domain:"); Layout.alignment: Qt.AlignRight }
        Label { text: poke.info.domain || "" }
        Label { text: qsTr("Size:"); Layout.alignment: Qt.AlignRight; visible: !poke.info.property }
        Label { text: poke.info.size || ""; visible: !poke.info.property }
        Label { text: qsTr("Display Type:"); Layout.alignment: Qt.AlignRight; visible: !poke.info.property }
        Label { text: poke.info.display || ""; visible: !poke.info.property }
        Label { text: qsTr("Endian:"); Layout.alignment: Qt.AlignRight; visible: !poke.info.property }
        Label { text: poke.info.endian || ""; visible: !poke.info.property }
        Label { text: qsTr("Value:"); Layout.alignment: Qt.AlignRight }
        RowLayout {
            Label { text: "0x"; visible: poke.info.hex === true }
            TextField { id: valueBox; Layout.fillWidth: true; font.family: "monospace"; onAccepted: poke.accept() }
        }
        RowLayout {
            Layout.columnSpan: 2
            Layout.alignment: Qt.AlignRight
            Button { text: qsTr("&Poke"); highlighted: true; onClicked: poke.accept() }
            Button { text: qsTr("&Close"); onClicked: poke.close() }
        }
    }
}
