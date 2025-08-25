Quackondo   ![Icon](Quackondo.png)
=======

Crossword game artificial intelligence and analysis tool.

This is a somewhat hacky attempt to integrate the [Macondo](https://github.com/domino14/macondo)
AI into [Quackle](https://github.com/quackle/quackle/). Macondo can solve endgames and 1-in-the-bag pre-endgames,
including very complex ones which Quackle's built-in solver can't handle.

To install Quackondo, go to the [Releases](https://github.com/pommicket/Quackondo/releases) page.

Video guide: https://www.youtube.com/watch?v=lNZnWYDTMXU

To use Macondo inside of Quackle, click on the 'Macondo' tab (next to 'Settings' on the left). You can then check the
"Use Macondo for 'Simulate'" button, and click simulate (after generating choices) to see which move Macondo prefers.
You can also solve (pre-)endgames, provided that the tile bag is sufficiently empty by clicking the Solve button in the Macondo tab.

**There may be bugs!** Please report them here: <https://github.com/pommicket/Quackondo/issues>. Ideally include
an explanation of what you were doing that caused the bug, a GCG file of the game, and the output from Macondo
(available at the bottom of the 'Macondo' tab).

License
-----------------
See LICENSE in this directory.

Building Quackle:
-----------------
Quackle runs automated GitHub CI builds on Qt 5.12 and 5.15, so it should work with any Qt version in that range.
See README.MacOS and README.Windows for platform-specific instructions.  Generally:

Clone the repo or download the tarball and untar.  Use cmake to build quacker, which will automatically build quackle and quackleio:

	cd quacker
	mkdir build && cd build
	cmake ..
	cmake --build .

The binary will build as 'Quackle'.

If you're building with Qt provided by cmake, then invoke cmake as...

	cmake -DCMAKE_PREFIX_PATH="<path_to_vcpkg>/installed/<arch>" ..

The Quackle cmake build system uses Qt5 by default.  But you can specify Qt6 by invoking...

	cmake -DQT_VERSION=6 ..

Latest known working version of Macondo: [v0.11.2](https://github.com/domino14/macondo/releases/tag/v0.11.2)

Note for Windows: As of v0.11.2, unmodified Macondo won't work with Quackondo due to
[a bug in a library it uses](https://github.com/chzyer/readline/issues/229).
The installer in the Releases page ships with a
[patched version of Macondo](https://github.com/pommicket/macondo).

File organization:
------------------
* quackle/ - libquackle sources.  libquackle is the engine, and can be linked to any convenient interface.  It does not use Qt.
* quackle/quackleio/ - I/O library for Quackle.  Implements stuff for accessing dictionaries, serializing GCG files, etc.  Also, command-line option handling.  This does have some modest dependencies on Qt.
* quackle/quacker/ - code for full Quackle UI.  Written in Qt, and requires libquackleio and libquackle.
* quackle/makeminidawg/ - standalone console program for building Quackle dictionaries.
* quackle/makegaddag/ - standalone console program for building gaddag files.
* quackle/data/ - lexicons, strategy files, and alphabet resources for Quackle.
In this directory is libquackle. Run qmake and then run make in this directory. Then cd to quackle/quackleio/, run qmake, and then run make.

Original quackle authors:
olaughlin@gmail.com
jasonkatzbrown@gmail.edu
jfultz@wolfram.com
matt.liberty@gmail.com
