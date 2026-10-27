// Fusion's check box with the box Chimera's has: Chimera leaves the box to the
// system, so it is a light box with a dark edge whatever the theme, and stays
// seen on a dark window (Fusion's own goes dark with the theme).
import QtQuick
import QtQuick.Templates as T
import QtQuick.Controls.impl

T.CheckBox {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    padding: 6
    spacing: 6

    indicator: Rectangle {
        x: control.text ? (control.mirrored ? control.width - width - control.rightPadding : control.leftPadding) : control.leftPadding + (control.availableWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2
        implicitWidth: 13
        implicitHeight: 13
        color: !control.enabled ? "#F0F0F0" : control.down ? "#CCE4F7" : "#FFFFFF"
        border.color: !control.enabled ? "#BCBCBC" : control.hovered || control.visualFocus ? "#0078D7" : "#333333"
        ColorImage {
            x: (parent.width - width) / 2
            y: (parent.height - height) / 2
            color: control.enabled ? "#202020" : "#A0A0A0"
            source: "qrc:/qt-project.org/imports/QtQuick/Controls/Fusion/images/checkmark.png"
            visible: control.checkState === Qt.Checked
        }
        Rectangle {
            x: 3; y: 3
            width: parent.width - 6
            height: parent.height - 6
            color: control.enabled ? "#202020" : "#A0A0A0"
            visible: control.checkState === Qt.PartiallyChecked
        }
    }

    contentItem: Text {
        leftPadding: control.indicator && !control.mirrored ? control.indicator.width + control.spacing : 0
        rightPadding: control.indicator && control.mirrored ? control.indicator.width + control.spacing : 0
        text: control.text
        font: control.font
        color: control.palette.windowText
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }
}
