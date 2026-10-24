import QtQuick
import QtQuick.Controls

// A menu's divider in the theme's MenuSeparator colour, which the palette
// carries as its midlight. Fusion's own is drawn near black, which cannot be
// seen on a dark menu.
MenuSeparator {
    id: control
    verticalPadding: 3
    contentItem: Rectangle {
        implicitWidth: 188
        implicitHeight: 1
        color: control.palette.midlight
    }
}
