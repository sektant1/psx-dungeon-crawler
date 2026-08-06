# 7 Resources and the File System

> Source PDF pages: 500-501
> Extraction mode: PyMuPDF text blocks; line breaks and printed hyphenation are preserved.

<!-- source-pdf-page: 500 -->

G
ames are by nature multimedia experiences. A game engine therefore
needs to be capable of loading and managing a wide variety of different
kinds of media—texture bitmaps, 3D mesh data, animations, audio clips, col-
lision and physics data, game world layouts, and the list goes on. Moreover,
because memory is usually scarce, a game engine needs to ensure that only
one copy of each media file is loaded into memory at any given time. For ex-
ample, if five meshes share the same texture, then we would like to have only
one copy of that texture in memory, not five. Most game engines employ some
kind of resource manager (a.k.a. asset manager, a.k.a. media manager) to load and
manage the myriad resources that make up a modern 3D game.
Every resource manager makes heavy use of the file system. On a personal
computer, the file system is exposed to the programmer via a library of operat-
ing system calls. However, game engines often “wrap” the native file system
API in an engine-specific API, for two primary reasons. First, the engine might
be cross-platform, in which case the game engine’s file system API can shield
the rest of the software from differences between different target hardware
platforms. Second, the operating system’s file system API might not provide
all the tools needed by a game engine. For example, many engines support
file streaming (i.e., the ability to load data “on the fly” while the game is run-
ning), yet most operating systems don’t provide a streaming file system API


<!-- source-pdf-page: 501 -->

out of the box. Console game engines also need to provide access to a variety
of removable and non-removable media, from memory sticks to optional hard
drives to a DVD-ROM or Blu-ray fixed disk to network file systems (e.g., Xbox
Live or the PlayStation Network, PSN). The differences between various kinds
of media can likewise be “hidden” behind a game engine’s file system API.
In this chapter, we’ll first explore the kinds of file system APIs found in
modern 3D game engines. Then we’ll see how a typical resource manager
works.
