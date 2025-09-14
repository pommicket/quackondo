#!/bin/sh

if [ '!' -d macondo ]; then
	echo 'Please copy/symlink macondo to this directory.'
	exit 1
fi

START_PWD=$(pwd)

mkdir -p quacker/build-release
cd quacker/build-release
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo .. || exit 1
make -j16 || exit 1
DIST=Quackondo
rm -r "$DIST" || exit 1
mkdir -p "$DIST/data" || exit 1
cp Quackle "$DIST/" || exit 1
for item in alphabets lexica strategy themes; do
	cp -r "${START_PWD}/data/$item" "$DIST/data" || exit 1
done
cp -Lr "${START_PWD}/macondo" "$DIST/" || exit 1
tar --xz -cvf 'Quackondo.tar.xz' "$DIST" || exit 1
mv Quackondo.tar.xz "$START_PWD" || exit 1
