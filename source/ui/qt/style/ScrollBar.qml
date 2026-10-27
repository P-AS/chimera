// Fusion's scroll bar, seen all the time there is more to scroll to, as a
// desktop's is (owner, 2026-10-27): Fusion's own shows only while it is
// used and fades away after. Its groove is drawn, so a list that scrolls
// says so before it is touched.
import QtQuick
import QtQuick.Templates as T
import QtQuick.Controls.impl
import QtQuick.Controls.Fusion
import QtQuick.Controls.Fusion.impl

T.ScrollBar {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: 2
    // as needed: there while the content is larger than the view
    visible: control.policy === T.ScrollBar.AlwaysOn || (control.policy === T.ScrollBar.AsNeeded && control.size < 1.0)
    minimumSize: orientation === Qt.Horizontal ? height / width : width / height

    contentItem: Rectangle {
        implicitWidth: control.interactive ? 6 : 2
        implicitHeight: control.interactive ? 6 : 2
        radius: width / 2
        color: {
            if (Fusion.highContrast)
                return control.pressed ? Fusion.highlightedOutline(control.palette) : Fusion.outline(control.palette)
            return control.pressed || control.hovered ? control.palette.dark : control.palette.mid
        }
    }

    background: Rectangle {
        radius: (control.horizontal ? height : width) / 2
        color: control.palette.mid
        opacity: 0.25
    }
}
