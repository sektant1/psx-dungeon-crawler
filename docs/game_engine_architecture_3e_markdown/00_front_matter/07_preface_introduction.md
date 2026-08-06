# Preface - Introduction

> Source PDF pages: 14-16
> Extraction mode: PyMuPDF text blocks; line breaks and printed hyphenation are preserved.

<!-- source-pdf-page: 14 -->
> Visual fallback for diagrams/images: [PDF page 14](../visual_pages/page_0014.jpg)

Preface

W
elcome to Game Engine Architecture. This book aims to present a com-
plete discussion of the major components that make up a typical com-
mercial game engine. Game programming is an immense topic, so we have
a lot of ground to cover. Nevertheless, I trust you’ll find that the depth of
our discussions is sufficient to give you a solid understanding of both the the-
ory and the common practices employed within each of the engineering disci-
plines we’ll cover. That said, this book is really just the beginning of a fascinat-
ing and potentially lifelong journey. A wealth of information is available on all
aspects of game technology, and this text serves both as a foundation-laying
device and as a jumping-off point for further learning.
Our focus in this book will be on game engine technologies and architec-
ture. This means we’ll cover the theory underlying the various subsystems
that comprise a commercial game engine, the data structures, algorithms and
software interfaces that are typically used to implement them, and how these
subsystems function together within a game engine as a whole. The line be-
tween the game engine and the game is rather blurry. We’ll focus primarily
on the engine itself, including a host of low-level foundation systems, the ren-
dering engine, the collision system, the physics simulation, character anima-
tion, audio, and an in-depth discussion of what I call the gameplay foundation
layer. This layer includes the game’s object model, world editor, event system
and scripting system. We’ll also touch on some aspects of gameplay program-


<!-- source-pdf-page: 15 -->

xiv
Preface

ming, including player mechanics, cameras and AI. However, by necessity,
the scope of these discussions will be limited mainly to the ways in which
gameplay systems interface with the engine.
This book is intended to be used as a course text for a two- or three-course
college-level series in intermediate game programming. It can also be used
by amateur software engineers, hobbyists, self-taught game programmers and
existing members of the game industry alike. Junior engineers can use this text
to solidify their understanding of game mathematics, engine architecture and
game technology. And some senior engineers who have devoted their careers
to one particular specialty may benefit from the bigger picture presented in
these pages as well.
To get the most out of this book, you should have a working knowledge
of basic object-oriented programming concepts and at least some experience
programming in C++. The game industry routinely makes use of a wide range
of programming languages, but industrial-strength 3D game engines are still
written primarily in C++. As such, any serious game programmer needs to be
able to code in C++. We’ll review the basic tenets of object-oriented program-
ming in Chapter 3, and you will no doubt pick up a few new C++ tricks as you
read this book, but a solid foundation in the C++ language is best obtained
from [46], [36] and [37]. If your C++ is a bit rusty, I recommend you refer to
these or similar books to refresh your knowledge as you read this text. If you
have no prior C++ experience, you may want to consider reading at least the
first few chapters of [46] and/or working through a few C++ tutorials online,
before diving into this book.
The best way to learn computer programming of any kind is to actually
write some code. As you read through this book, I strongly encourage you
to select a few topic areas that are of particular interest to you and come up
with some projects for yourself in those areas. For example, if you find char-
acter animation interesting, you could start by installing OGRE and explor-
ing its skinned animation demo. Then you could try to implement some of
the animation blending techniques described in this book, using OGRE. Next
you might decide to implement a simple joypad-controlled animated charac-
ter that can run around on a flat plane. Once you have something relatively
simple working, expand upon it! Then move on to another area of game tech-
nology. Rinse and repeat. It doesn’t particularly matter what the projects are,
as long as you’re practicing the art of game programming, not just reading
about it.
Game technology is a living, breathing thing that can never be entirely
captured within the pages of a book. As such, additional resources, errata,
updates, sample code and project ideas will be posted from time to time on


<!-- source-pdf-page: 16 -->

Preface
xv

this book’s website at http://www.gameenginebook.com. You can also follow
me on Twitter @jqgregory.
