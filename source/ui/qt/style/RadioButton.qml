// Fusion's radio button with the circle Chimera's has: the system's, a light
// circle with a dark edge whatever the theme (Fusion's own goes dark with it).
import QtQuick
import QtQuick.Templates as T

T.RadioButton {
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
        radius: width / 2
        color: !control.enabled ? "#F0F0F0" : control.down ? "#CCE4F7" : "#FFFFFF"
        border.color: !control.enabled ? "#BCBCBC" : control.hovered || control.visualFocus ? "#0078D7" : "#333333"
        Rectangle {
            x: (parent.width - width) / 2
            y: (parent.height - height) / 2
            width: 5
            height: 5
            radius: width / 2
            color: control.enabled ? "#202020" : "#A0A0A0"
            visible: control.checked
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
