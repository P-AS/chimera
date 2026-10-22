#!/bin/sh
# Builds Qt for Windows (x86_64) on Linux with the mingw-w64 compiler Chimera's
# Windows build uses, for the Qt window (source/ui). The Linux Qt of the same
# version runs its tools during the build (moc, qsb, qmlcachegen...), so it is
# given as the host Qt.
#
#   tools/build-qt-windows.sh <host qt> <folder of source archives> <install folder>
#
# The source folder holds qtbase.tar.xz, qtshadertools.tar.xz,
# qtdeclarative.tar.xz and qtsvg.tar.xz (the "submodules" archives of the
# release). Qt is built without its network module: Chimera has no network
# code, and QML without it cannot load anything from the network.
set -eu
host=$1 src=$2 prefix=$3
work=${QT_WORK:-$src/build-mingw}
mkdir -p "$work"

tc="$work/mingw-w64.cmake"
cat > "$tc" <<'EOF'
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
EOF

for m in qtbase qtshadertools qtdeclarative qtsvg; do
	if [ ! -d "$work/$m-src" ]; then
		mkdir -p "$work/$m-src"
		tar -xJf "$src/$m.tar.xz" -C "$work/$m-src" --strip-components=1
	fi
done

step() { echo "=== $1 $(date -u +%H:%M:%S)"; }

step qtbase
cmake -G Ninja -S "$work/qtbase-src" -B "$work/qtbase" \
	-DCMAKE_TOOLCHAIN_FILE="$tc" -DQT_HOST_PATH="$host" \
	-DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_BUILD_TYPE=Release \
	-DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF -DQT_BUILD_BENCHMARKS=OFF \
	-DFEATURE_network=OFF -DFEATURE_dbus=OFF -DFEATURE_sql=OFF \
	-DFEATURE_printsupport=OFF -DFEATURE_widgets=OFF -DFEATURE_testlib=OFF
cmake --build "$work/qtbase" --parallel
cmake --install "$work/qtbase"

for m in qtshadertools qtdeclarative qtsvg; do
	step "$m"
	"$prefix/bin/qt-cmake" -G Ninja -S "$work/$m-src" -B "$work/$m" \
		-DQT_HOST_PATH="$host" -DCMAKE_BUILD_TYPE=Release \
		-DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF
	cmake --build "$work/$m" --parallel
	cmake --install "$work/$m"
done
step done
