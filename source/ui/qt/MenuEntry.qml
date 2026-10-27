import QtQuick
import QtQuick.Controls

// A menu's row with Chimera's image margin: a row without an icon or a tick
// keeps its text in line with the rows that have one.
MenuItem {
    leftPadding: padding + (icon.source.toString() === "" && !checkable ? icon.width + spacing : 0)
}
