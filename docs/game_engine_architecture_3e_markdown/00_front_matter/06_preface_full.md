# Preface

> Source PDF pages: 14-19
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

New to the Third Edition

The computing hardware that lies at the heart of today’s game consoles, mo-
bile devices and personal computers makes heavy use of parallelism. Deep
within the CPUs and GPUs in these devices, multiple functional units operate
simultaneously, employing a “divide and conquer” approach to high-speed
computation. While parallel computing hardware can make traditional single-
threaded programs run faster, programmers need to write concurrent software
to truly take advantage of the hardware parallelism that has become ubiqui-
tous in modern computing platforms.
In prior editions of Game Engine Architecture, the topics of parallelism and
concurrency were touched on in the context of game engine design. However,
they weren’t given the in-depth treatment they deserved. In this, the third
edition of the book, this problem has been remedied via the addition of a brand
new chapter on concurrency and parallelism. Chapters 8 and 16 have also been
augmented to include detailed discussions of how concurrent programming
techniques are typically applied to game engine subsystem and game object
model updates, and how a general-purpose job system can be used to unlock
the power of concurrency within a game engine.
I’ve already mentioned that every good game programmer must have a
strong working knowledge of C++ (in addition to the wide variety of other
useful languages used regularly in the game industry). In my view, a pro-
grammer’s knowledge of high-level languages should rest upon a solid under-
standing of the software and hardware systems that underlie them. As such,
in this edition I’ve expanded Chapter 3 to include a treatment of the funda-
mentals of computer hardware, assembly language, and the operating system
kernel.
This third edition of Game Engine Architecture also improves upon the treat-
ment of various topics covered in prior editions. A discussion of local and
global compiler optimizations has been added. Fuller coverage of the vari-
ous C++ language standards is included. The section on memory caching and
cache coherency has been expanded. The animation chapter has been stream-
lined. And, as with the second edition, various errata have been repaired that
were brought to my attention by you, my devoted readers. Thank you! I hope
you’ll find that the mistakes you found have all been fixed. (Although no
doubt they have been replaced by a slew of new mistakes, about which you
can feel free to inform me, so that I may correct them in the fourth edition of
the book!)


<!-- source-pdf-page: 17 -->

xvi
Preface

Of course, as I’ve said before, the field of game engine programming is al-
most unimaginably broad and deep. There’s no way to cover every topic in
one book. As such, the primary purpose of this book remains to serve as an
awareness-building tool and a jumping-off point for further learning. I hope
you find this edition helpful on your journey through the fascinating and mul-
tifaceted landscape of game engine architecture.

Acknowledgments

No book is created in a vacuum, and this one is certainly no exception. This
book—and its third edition, which you now hold in your hands—would not
have been possible without the help of my family, friends and colleagues in the
game industry, and I’d like to extend warm thanks to everyone who helped me
to bring this project to fruition.
Of course, the ones most impacted by a project like this are invariably the
author’s family. So I’d like to start by offering, for a third time, a special thank-
you to my wife Trina. She was a pillar of strength during the writing of the
original book, and she was as supportive and invaluably helpful as ever dur-
ing my work on the second and third editions. While I was busy tapping away
on my keyboard, Trina was always there to take care of our two boys, Evan
(now age 15) and Quinn (age 12), day after day and night after night, often
forgoing her own plans, doing my chores as well as her own (more often than
I’d like to admit), and always giving me kind words of encouragement when
I needed them the most.
I would also like to extend special thanks to my editors for the first edition,
Matt Whiting and Jeff Lander. Their insightful, targeted and timely feedback
was always right on the money, and their vast experience in the game industry
helped to give me confidence that the information presented in these pages
is as accurate and up-to-date as humanly possible. Matt and Jeff were both
a pleasure to work with, and I am honored to have had the opportunity to
collaborate with such consummate professionals on this project. I’d like to
extend a special thank-you to Jeff for putting me in touch with Alice Peters and
helping me to get this project off the ground in the first place. Matt, thank you
also for stepping up to the plate once again and providing me with valuable
feedback on the new concurrency chapter in the third edition.
A number of my colleagues at Naughty Dog also contributed to this
book, either by providing feedback or by helping me with the structure and
topic content of one of the chapters. I’d like to thank Marshall Robin and
Carlos Gonzalez-Ochoa for their guidance and tutelage as I wrote the render-
ing chapter, and Pål-Kristian Engstad for his excellent and insightful feedback


<!-- source-pdf-page: 18 -->

Preface
xvii

on the content of that chapter. My thanks go to Christian Gyrling for his feed-
back on various sections of the book, including the chapter on animation, and
the new chapter on parallelism and concurrency. I want to extend a special
thank-you to Jonathan Lanier, Naughty Dog’s resident senior audio program-
mer extraordinaire, for providing me with a great deal of the raw informa-
tion you’ll find in the audio chapter, for always being available to chat when I
had questions, and for providing laser-focused and invaluable feedback after
reading the initial draft. I’d also like to thank one of the newest members of
our Naughty Dog programming team, Kareem Omar, for his valuable insights
and feedback on the new concurrency chapter. My thanks also go to the entire
Naughty Dog engineering team for creating all of the incredible game engine
systems that I highlight in this book.
Additional thanks go to Keith Schaeffer of Electronic Arts for providing
me with much of the raw content regarding the impact of physics on a game,
found in Section 13.1. I’d also like to extend a warm thank-you to Christophe
Balestra (who was co-president of Naughty Dog during my first ten years
there), Paul Keet (who was a lead engineer on the Medal of Honor franchise
during my time at Electronic Arts), and Steve Ranck (the lead engineer on the
Hydro Thunder project at Midway San Diego), for their mentorship and guid-
ance over the years. While they did not contribute to the book directly, they
did help to make me the engineer that I am today, and their influences are
echoed on virtually every page in one way or another.
This book arose out of the notes I developed for a course entitled ITP-485:
Programming Game Engines, which I taught under the auspices of the Infor-
mation Technology Program at the University of Southern California for ap-
proximately four years. I would like to thank Dr. Anthony Borquez, the di-
rector of the ITP department at the time, for hiring me to develop the ITP-485
course curriculum in the first place.
My extended family and friends also deserve thanks, in part for their unwa-
vering encouragement, and in part for entertaining my wife and our two boys
on so many occasions while I was working. I’d like to thank my sister- and
brother-in-law, Tracy Lee and Doug Provins, my cousin-in-law Matt Glenn,
and all of our incredible friends, including Kim and Drew Clark, Sherilyn and
Jim Kritzer, Anne and Michael Scherer, Kim and Mike Warner, and Kendra
and Andy Walther.
When I was a teenager, my father Kenneth Gregory
wrote Extraordinary Stock Profits—a book on investing in the stock market—
and in doing so, he inspired me to write this book. For this and so much
more, I am eternally grateful to him. I’d also like to thank my mother Er-
ica Gregory, in part for her insistence that I embark on this project, and in
part for spending countless hours with me when I was a child, beating the art


<!-- source-pdf-page: 19 -->

xviii
Preface

of writing into my cranium. I owe my writing skills, my work ethic, and my
rather twisted sense of humor entirely to her!
I’d like to thank Alice Peters and Kevin Jackson-Mead, as well as the entire
A K Peters staff, for their Herculean efforts in publishing the first edition of
this book. Since that time, A K Peters has been acquired by the CRC Press,
the principal science and technology book division of the Taylor & Francis
Group. I’d like to wish Alice and Klaus Peters all the best in their future en-
deavors. I’d also like to thank Rick Adams, Jennifer Ahringer, Jessica Vega
and Cynthia Klivecka of Taylor & Francis for their patient support and help
throughout the process of creating the second and third editions of Game En-
gine Architecture, Jonathan Pennell for his work on the cover for the second
edition, Scott Shamblin for his work on the third edition’s cover art, and Brian
Haeger (http://www.bionic3d.com) for graciously permitting me to use his
beautiful 3D model of the Space X Merlin rocket engine on the cover of the
third edition.
I am thrilled to be able to say that both the first and second editions of
Game Engine Architecture have been or are being translated into Japanese, Chi-
nese and Korean! I would like to extend my sincere thanks to Kazuhisa Minato
and his team at Namco Bandai Games for taking on the incredibly daunting
task of the Japanese translation, and for doing such a great job with both edi-
tions. I’d also like to thank the folks at Softbank Creative, Inc. for publishing
the Japanese version of the book. I would also like to extend my warmest
thanks to Milo Yip for his hard work and dedication to the Chinese translation
project. My sincere appreciation goes to the Publishing House of the Electron-
ics Industry for publishing the Chinese translation of the book, and to both
the Acorn Publishing Company and Hongreung Science Publishing Co. for
their publication of the Korean translations of the first and second editions,
respectively.
Many of my readers took the time to send me feedback and alert me to er-
rors in the first and second editions, and for that I’d like to extend my sincere
thanks to all of you who contributed. I’d like to give a special thank-you to
Milo Yip, Joe Conley and Zachary Turner for going above and beyond the call
of duty in this regard. All three of you provided me with many-page docu-
ments, chock full of errata and incredibly valuable and insightful suggestions.
I’ve tried my best to incorporate all of this feedback into the third edition—
please keep it coming!

Jason Gregory
April 2018
