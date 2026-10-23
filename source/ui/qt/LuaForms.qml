// Lua's forms: each form a script makes, a window of its own with its
// controls where the script put them. Layout and bindings only, over the
// forms view model; what the person does goes back to it.
import QtQuick
import QtQuick.Controls
import "Shapes.js" as Shapes

Item {
    id: lua
    required property QtObject vm

    Instantiator {
        model: lua.vm.rows("forms")
        delegate: ApplicationWindow {
            id: form
            // the row's roles by name: "width" and "text" are a window's own
            required property var model
            readonly property int formHandle: form.model.handle
            objectName: "luaForm" + form.formHandle
            title: form.model.text
            // the size inside its frame, as the script asked
            width: form.model.width
            height: form.model.height
            minimumWidth: 40
            minimumHeight: 30
            visible: true
            // closed by the person: the script's close function, then it goes
            onClosing: lua.vm.run("closed", [form.formHandle])

            Repeater {
                model: lua.vm.rows("controls")
                delegate: Item {
                    id: control
                    // the row's roles by name: "x", "text" and the rest are an item's own
                    required property var model
                    readonly property int handle: control.model.handle
                    readonly property string kind: control.model.kind
                    readonly property string label: control.model.text
                    visible: control.model.form === form.formHandle && control.model.visible
                    enabled: control.model.enabled
                    x: control.model.x
                    y: control.model.y
                    width: control.model.width
                    height: control.model.height

                    Button {
                        anchors.fill: parent
                        visible: control.kind === "button"
                        text: control.label
                        onClicked: lua.vm.run("click", [control.handle])
                    }
                    Label {
                        anchors.fill: parent
                        visible: control.kind === "label"
                        text: control.label
                        font.family: control.model.fixedWidth ? "monospace" : Qt.application.font.family
                        wrapMode: Text.Wrap
                        TapHandler { onTapped: lua.vm.run("click", [control.handle]) }
                    }
                    CheckBox {
                        anchors.fill: parent
                        visible: control.kind === "checkbox"
                        text: control.label
                        checked: control.model.checked
                        onToggled: { lua.vm.run("setChecked", [control.handle, checked]); lua.vm.run("click", [control.handle]) }
                    }
                    ComboBox {
                        anchors.fill: parent
                        visible: control.kind === "dropdown"
                        model: control.model.items === "" ? [] : control.model.items.split("\n")
                        currentIndex: control.model.selected
                        displayText: control.label
                        onActivated: (index) => lua.vm.run("choose", [control.handle, index])
                    }
                    // a text box keeps what is typed; the script reads it with gettext
                    TextField {
                        anchors.fill: parent
                        visible: control.kind === "textbox" && !control.model.multiline
                        topPadding: 0
                        bottomPadding: 0
                        text: control.label
                        font.family: control.model.fixedWidth ? "monospace" : Qt.application.font.family
                        onTextEdited: lua.vm.run("setText", [control.handle, text])
                        onAccepted: lua.vm.run("click", [control.handle])
                    }
                    ScrollView {
                        anchors.fill: parent
                        visible: control.kind === "textbox" && control.model.multiline
                        TextArea {
                            text: control.label
                            font.family: control.model.fixedWidth ? "monospace" : Qt.application.font.family
                            wrapMode: control.model.multiline ? TextEdit.Wrap : TextEdit.NoWrap
                            onTextChanged: if (text !== control.label) lua.vm.run("setText", [control.handle, text])
                            Keys.onReturnPressed: (event) => { if (control.model.multiline) event.accepted = false; else lua.vm.run("click", [control.handle]) }
                        }
                    }
                    // a picture box: what the script drew, on its colour, in its own pixels
                    Canvas {
                        id: box
                        anchors.fill: parent
                        visible: control.kind === "picturebox"
                        readonly property var drawn: control.kind === "picturebox" ? JSON.parse(control.model.shapes) : []
                        onDrawnChanged: requestPaint()
                        onImageLoaded: requestPaint()
                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.reset()
                            ctx.fillStyle = control.model.background
                            ctx.fillRect(0, 0, width, height)
                            for (const s of box.drawn) {
                                ctx.save()
                                Shapes.draw(ctx, box, s, 1, 1, Shapes.urlOf)
                                ctx.restore()
                            }
                        }
                        HoverHandler { onPointChanged: lua.vm.run("pointer", [control.handle, Math.floor(point.position.x), Math.floor(point.position.y)]) }
                        TapHandler { onTapped: lua.vm.run("click", [control.handle]) }
                    }
                }
            }
        }
    }
}
