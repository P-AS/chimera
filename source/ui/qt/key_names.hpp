#pragma once

#include <QString>

namespace chimera::qt {

/* Chimera's name for a Qt key, or "" for a key Chimera has no name for */
QString keyName(int key, Qt::KeyboardModifiers modifiers);

} // namespace chimera::qt
