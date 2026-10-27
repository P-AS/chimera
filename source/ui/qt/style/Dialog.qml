// Fusion's dialog. In a run nobody watches (--headless) one that opens ends
// the run, said on stderr, as Chimera's --headless does (headless_run.hpp);
// a dialog with an answer for nobody (work a crash left) is answered there.
import QtQuick
import QtQuick.Controls.Fusion as Fusion

Fusion.Dialog {
    id: control
    onOpened: if (typeof headlessRun !== "undefined" && headlessRun !== null && headlessRun.enabled) headlessRun.dialogOpened(control)
}
