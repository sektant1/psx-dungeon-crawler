# 1 Introduction

> Source PDF pages: 22-24
> Extraction mode: PyMuPDF text blocks; line breaks and printed hyphenation are preserved.

<!-- source-pdf-page: 22 -->

W
hen I got my first game console in 1979—a way-cool Intellivision sys-
tem by Mattel—the term “game engine” did not exist. Back then, video
and arcade games were considered by most adults to be nothing more than
toys, and the software that made them tick was highly specialized to both
the game in question and the hardware on which it ran. Today, games are a
multi-billion-dollar mainstream industry rivaling Hollywood in size and pop-
ularity. And the software that drives these now-ubiquitous three-dimensional
worlds—game engines like Epic Games’ Unreal Engine 4, Valve’s Source engine
and, Crytek’s CRYENGINE® 3, Electronic Arts DICE’s Frostbite™engine, and
the Unity game engine—have become fully featured reusable software devel-
opment kits that can be licensed and used to build almost any game imagin-
able.
While game engines vary widely in the details of their architecture and im-
plementation, recognizable coarse-grained patterns have emerged across both
publicly licensed game engines and their proprietary in-house counterparts.
Virtually all game engines contain a familiar set of core components, including
the rendering engine, the collision and physics engine, the animation system,
the audio system, the game world object model, the artificial intelligence sys-
tem and so on. Within each of these components, a relatively small number of
semi-standard design alternatives are also beginning to emerge.


<!-- source-pdf-page: 23 -->

There are a great many books that cover individual game engine subsys-
tems, such as three-dimensional graphics, in exhaustive detail. Other books
cobble together valuable tips and tricks across a wide variety of game tech-
nology areas. However, I have been unable to find a book that provides its
reader with a reasonably complete picture of the entire gamut of components
that make up a modern game engine. The goal of this book, then, is to take the
reader on a guided hands-on tour of the vast and complex landscape of game
engine architecture.
In this book you will learn:

•
how real industrial-strength production game engines are architected;

•
how game development teams are organized and work in the real world;

•
which major subsystems and design patterns appear again and again in
virtually every game engine;

•
the typical requirements for each major subsystem;

•
which subsystems are genre- or game-agnostic, and which ones are typ-
ically designed explicitly for a specific genre or game; and

•
where the engine normally ends and the game begins.

We’ll also get a first-hand glimpse into the inner workings of some popular
game engines, such as Quake, Unreal and Unity, and some well-known mid-
dleware packages, such as the Havok Physics library, the OGRE rendering en-
gine and Rad Game Tools’ Granny 3D animation and geometry management
toolkit. And we’ll explore a number of proprietary game engines that I’ve had
the pleasure to work with, including the engine Naughty Dog developed for
its Uncharted and The Last of Us game series.
Before we get started, we’ll review some techniques and tools for large-
scale software engineering in a game engine context, including:

•
the difference between logical and physical software architecture;

•
configuration management, revision control and build systems; and

•
some tips and tricks for dealing with one of the common development
environments for C and C++, Microsoft Visual Studio.

In this book I assume that you have a solid understanding of C++ (the lan-
guage of choice among most modern game developers) and that you under-
stand basic software engineering principles. I also assume you have some


<!-- source-pdf-page: 24 -->

exposure to linear algebra, three-dimensional vector and matrix math and
trigonometry (although we’ll review the core concepts in Chapter 5). Ideally,
you should have some prior exposure to the basic concepts of real time and
event-driven programming. But never fear—I will review these topics briefly,
and I’ll also point you in the right direction if you feel you need to hone your
skills further before we embark.
