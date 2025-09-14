#!/bin/sh

# Build Quackle and copy required DLLs, data files
#  Intended for MinGW UCRT shell
if [ "$1" = '' ]; then
	MODE=RelWithDebInfo
else
	MODE=$1
fi
[ "$MACONDO" = '' ] && MACONDO="$(pwd)/macondo"
pacman --needed -S mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-qt5-base mingw-w64-ucrt-x86_64-gcc
mkdir -p quacker/build
cd quacker/build
cmake -DCMAKE_BUILD_TYPE=$MODE ..
ninja
mkdir -p $MODE
cp Quackle.exe $MODE/
for file in Qt5Core.dll Qt5Gui.dll Qt5Widgets.dll libbrotlicommon.dll libbrotlidec.dll libbz2-1.dll libdouble-conversion.dll \
	libfreetype-6.dll libgcc_s_seh-1.dll libglib-2.0-0.dll libgraphite2.dll libharfbuzz-0.dll libiconv-2.dll \
	libicudt77.dll libicuin77.dll libicuuc77.dll libintl-8.dll libmd4c.dll libpcre2-16-0.dll libpcre2-8-0.dll \
	libpng16-16.dll libstdc++-6.dll libwinpthread-1.dll libzstd.dll zlib1.dll; do
	cp /ucrt64/bin/$file $MODE/
done
rm -rf $MODE/data
cp -r ../../data $MODE/
rm -rf $MODE/macondo
cp -r "$MACONDO" $MODE/
for folder in styles imageformats platforms; do
	rm -rf $MODE/$folder
	cp -r /ucrt64/share/qt5/plugins/$folder $MODE/
done
