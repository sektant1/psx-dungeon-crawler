# 1 Introduction

> Source PDF pages: 22-87
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

## 1.1 Structure of a Typical Game Team

Before we delve into the structure of a typical game engine, let’s first take a
brief look at the structure of a typical game development team. Game stu-
dios are usually composed of five basic disciplines: engineers, artists, game
designers, producers and other management and support staff (marketing,
legal, information technology/technical support, administrative, etc.). Each
discipline can be divided into various subdisciplines. We’ll take a brief look at
each below.

### 1.1.1 Engineers

The engineers design and implement the software that makes the game, and
the tools, work. Engineers are often categorized into two basic groups: runtime
programmers (who work on the engine and the game itself) and tools program-
mers (who work on the offline tools that allow the rest of the development
team to work effectively). On both sides of the runtime/tools line, engineers
have various specialties. Some engineers focus their careers on a single en-
gine system, such as rendering, artificial intelligence, audio or collision and
physics. Some focus on gameplay programming and scripting, while others
prefer to work at the systems level and not get too involved in how the game
actually plays. Some engineers are generalists—jacks of all trades who can
jump around and tackle whatever problems might arise during development.
Senior engineers are sometimes asked to take on a technical leadership role.
Lead engineers usually still design and write code, but they also help to man-
age the team’s schedule, make decisions regarding the overall technical direc-
tion of the project, and sometimes also directly manage people from a human
resources perspective.
Some companies also have one or more technical directors (TD), whose job
it is to oversee one or more projects from a high level, ensuring that the teams
are aware of potential technical challenges, upcoming industry developments,
new technologies and so on. The highest engineering-related position at a
game studio is the chief technical officer (CTO), if the studio has one. The


<!-- source-pdf-page: 25 -->

CTO’s job is to serve as a sort of technical director for the entire studio, as well
as serving a key executive role in the company.

### 1.1.2 Artists

As we say in the game industry, “Content is king.” The artists produce all of
the visual and audio content in the game, and the quality of their work can
literally make or break a game. Artists come in all sorts of flavors:

•
Concept artists produce sketches and paintings that provide the team with
a vision of what the final game will look like. They start their work early
in the concept phase of development, but usually continue to provide vi-
sual direction throughout a project’s life cycle. It is common for screen-
shots taken from a shipping game to bear an uncanny resemblance to the
concept art.

•
3D modelers produce the three-dimensional geometry for everything in
the virtual game world. This discipline is typically divided into two sub-
disciplines: foreground modelers and background modelers. The for-
mer create objects, characters, vehicles, weapons and the other objects
that populate the game world, while the latter build the world’s static
background geometry (terrain, buildings, bridges, etc.).

•
Texture artists create the two-dimensional images known as textures,
which are applied to the surfaces of 3D models in order to provide detail
and realism.

•
Lighting artists lay out all of the light sources in the game world, both
static and dynamic, and work with color, intensity and light direction to
maximize the artfulness and emotional impact of each scene.

•
Animators imbue the characters and objects in the game with motion.
The animators serve quite literally as actors in a game production, just as
they do in a CG film production. However, a game animator must have a
unique set of skills in order to produce animations that mesh seamlessly
with the technological underpinnings of the game engine.

•
Motion capture actors are often used to provide a rough set of motion data,
which are then cleaned up and tweaked by the animators before being
integrated into the game.

•
Sound designers work closely with the engineers in order to produce and
mix the sound effects and music in the game.


<!-- source-pdf-page: 26 -->

•
Voice actors provide the voices of the characters in many games.

•
Many games have one or more composers, who compose an original score
for the game.

As with engineers, senior artists are often called upon to be team leaders.
Some game teams have one or more art directors—very senior artists who man-
age the look of the entire game and ensure consistency across the work of all
team members.

### 1.1.3 Game Designers

The game designers’ job is to design the interactive portion of the player’s ex-
perience, typically known as gameplay. Different kinds of designers work at
different levels of detail. Some (usually senior) game designers work at the
macro level, determining the story arc, the overall sequence of chapters or lev-
els, and the high-level goals and objectives of the player. Other designers work
on individual levels or geographical areas within the virtual game world, lay-
ing out the static background geometry, determining where and when ene-
mies will emerge, placing supplies like weapons and health packs, designing
puzzle elements and so on. Still other designers operate at a highly technical
level, working closely with gameplay engineers and/or writing code (often in
a high-level scripting language). Some game designers are ex-engineers, who
decided they wanted to play a more active role in determining how the game
will play.
Some game teams employ one or more writers. A game writer’s job can
range from collaborating with the senior game designers to construct the story
arc of the entire game, to writing individual lines of dialogue.
As with other disciplines, some senior designers play management roles.
Many game teams have a game director, whose job it is to oversee all aspects
of a game’s design, help manage schedules, and ensure that the work of indi-
vidual designers is consistent across the entire product. Senior designers also
sometimes evolve into producers.

### 1.1.4 Producers

The role of producer is defined differently by different studios. In some game
companies, the producer’s job is to manage the schedule and serve as a human
resources manager. In other companies, producers serve in a senior game de-
sign capacity. Still other studios ask their producers to serve as liaisons be-
tween the development team and the business unit of the company (finance,
legal, marketing, etc.). Some smaller studios don’t have producers at all. For


<!-- source-pdf-page: 27 -->

example, at Naughty Dog, literally everyone in the company, including the
two co-presidents, plays a direct role in constructing the game; team man-
agement and business duties are shared between the senior members of the
studio.

### 1.1.5 Other Staff

The team of people who directly construct the game is typically supported by
a crucial team of support staff. This includes the studio’s executive manage-
ment team, the marketing department (or a team that liaises with an external
marketing group), administrative staff and the IT department, whose job is
to purchase, install and configure hardware and software for the team and to
provide technical support.

### 1.1.6 Publishers and Studios

The marketing, manufacture and distribution of a game title are usually han-
dled by a publisher, not by the game studio itself. A publisher is typically a
large corporation, like Electronic Arts, THQ, Vivendi, Sony, Nintendo, etc.
Many game studios are not affiliated with a particular publisher. They sell
each game that they produce to whichever publisher strikes the best deal with
them. Other studios work exclusively with a single publisher, either via a
long-term publishing contract or as a fully owned subsidiary of the publishing
company. For example, THQ’s game studios are independently managed, but
they are owned and ultimately controlled by THQ. Electronic Arts takes this
relationship one step further, by directly managing its studios. First-party de-
velopers are game studios owned directly by the console manufacturers (Sony,
Nintendo and Microsoft). For example, Naughty Dog is a first-party Sony de-
veloper. These studios produce games exclusively for the gaming hardware
manufactured by their parent company.

## 1.2 What Is a Game?

We probably all have a pretty good intuitive notion of what a game is. The
general term “game” encompasses board games like chess and Monopoly, card
games like poker and blackjack, casino games like roulette and slot machines,
military war games, computer games, various kinds of play among children,
and the list goes on.
In academia we sometimes speak of game theory, in
which multiple agents select strategies and tactics in order to maximize their
gains within the framework of a well-defined set of game rules. When used
in the context of console or computer-based entertainment, the word “game”


<!-- source-pdf-page: 28 -->

usually conjures images of a three-dimensional virtual world featuring a hu-
manoid, animal or vehicle as the main character under player control. (Or
for the old geezers among us, perhaps it brings to mind images of two-
dimensional classics like Pong, Pac-Man, or Donkey Kong.) In his excellent book,
A Theory of Fun for Game Design, Raph Koster defines a game to be an interactive
experience that provides the player with an increasingly challenging sequence
of patterns which he or she learns and eventually masters [30]. Koster’s asser-
tion is that the activities of learning and mastering are at the heart of what we
call “fun,” just as a joke becomes funny at the moment we “get it” by recog-
nizing the pattern.

For the purposes of this book, we’ll focus on the subset of games that com-
prise two- and three-dimensional virtual worlds with a small number of play-
ers (between one and 16 or thereabouts). Much of what we’ll learn can also
be applied to HTML5/JavaScript games on the Internet, pure puzzle games
like Tetris, or massively multiplayer online games (MMOG). But our primary
focus will be on game engines capable of producing first-person shooters,
third-person action/platform games, racing games, fighting games and the
like.

### 1.2.1 Video Games as Soft Real-Time Simulations

Most two- and three-dimensional video games are examples of what computer
scientists would call soft real-time interactive agent-based computer simulations.
Let’s break this phrase down in order to better understand what it means.

In most video games, some subset of the real world—or an imaginary
world—is modeled mathematically so that it can be manipulated by a computer.
The model is an approximation to and a simplification of reality (even if it’s
an imaginary reality), because it is clearly impractical to include every detail
down to the level of atoms or quarks. Hence, the mathematical model is a sim-
ulation of the real or imagined game world. Approximation and simplification
are two of the game developer’s most powerful tools. When used skillfully,
even a greatly simplified model can sometimes be almost indistinguishable
from reality—and a lot more fun.

An agent-based simulation is one in which a number of distinct entities
known as “agents” interact. This fits the description of most three-dimensional
computer games very well, where the agents are vehicles, characters, fireballs,
power dots and so on. Given the agent-based nature of most games, it should
come as no surprise that most games nowadays are implemented in an object-
oriented, or at least loosely object-based, programming language.


<!-- source-pdf-page: 29 -->

All interactive video games are temporal simulations, meaning that the vir-
tual game world model is dynamic—the state of the game world changes over
time as the game’s events and story unfold. A video game must also respond to
unpredictable inputs from its human player(s)—thus interactive temporal simu-
lations. Finally, most video games present their stories and respond to player
input in real time, making them interactive real-time simulations. One notable
exception is in the category of turn-based games like computerized chess or
turn-based strategy games. But even these types of games usually provide the
user with some form of real-time graphical user interface. So for the purposes
of this book, we’ll assume that all video games have at least some real-time
constraints.
At the core of every real-time system is the concept of a deadline. An ob-
vious example in video games is the requirement that the screen be updated
at least 24 times per second in order to provide the illusion of motion. (Most
games render the screen at 30 or 60 frames per second because these are multi-
ples of an NTSC monitor’s refresh rate.) Of course, there are many other kinds
of deadlines in video games as well. A physics simulation may need to be up-
dated 120 times per second in order to remain stable. A character’s artificial
intelligence system may need to “think” at least once every second to prevent
the appearance of stupidity. The audio library may need to be called at least
once every 1/60 second in order to keep the audio buffers filled and prevent
audible glitches.
A “soft” real-time system is one in which missed deadlines are not catas-
trophic. Hence, all video games are soft real-time systems—if the frame rate dies,
the human player generally doesn’t! Contrast this with a hard real-time system,
in which a missed deadline could mean severe injury to or even the death of a
human operator. The avionics system in a helicopter or the control-rod system
in a nuclear power plant are examples of hard real-time systems.
Mathematical models can be analytic or numerical. For example, the analytic
(closed-form) mathematical model of a rigid body falling under the influence
of constant acceleration due to gravity is typically written as follows:

y(t) = 1

2 gt2 + v0t + y0.
(1.1)

An analytic model can be evaluated for any value of its independent variables,
such as the time t in the above equation, given only the initial conditions v0
and y0 and the constant g. Such models are very convenient when they can be
found. However, many problems in mathematics have no closed-form solu-
tion. And in video games, where the user’s input is unpredictable, we cannot
hope to model the entire game analytically.
A numerical model of the same rigid body under gravity can be expressed


<!-- source-pdf-page: 30 -->

as follows:

y(t + ∆t) = F(y(t), ˙y(t), ¨y(t), . . .).
(1.2)

That is, the height of the rigid body at some future time (t + ∆t) can be found
as a function of the height and its first, second, and possibly higher-order time
derivatives at the current time t. Numerical simulations are typically imple-
mented by running calculations repeatedly, in order to determine the state of
the system at each discrete time step. Games work in the same way. A main
“game loop” runs repeatedly, and during each iteration of the loop, various
game systems such as artificial intelligence, game logic, physics simulations
and so on are given a chance to calculate or update their state for the next
discrete time step. The results are then “rendered” by displaying graphics,
emitting sound and possibly producing other outputs such as force-feedback
on the joypad.

## 1.3 What Is a Game Engine?

The term “game engine” arose in the mid-1990s in reference to first-person
shooter (FPS) games like the insanely popular Doom by id Software. Doom
was architected with a reasonably well-defined separation between its core
software components (such as the three-dimensional graphics rendering sys-
tem, the collision detection system or the audio system) and the art assets,
game worlds and rules of play that comprised the player’s gaming experience.
The value of this separation became evident as developers began licensing
games and retooling them into new products by creating new art, world lay-
outs, weapons, characters, vehicles and game rules with only minimal changes
to the “engine” software. This marked the birth of the “mod community”—
a group of individual gamers and small independent studios that built new
games by modifying existing games, using free toolkits provided by the orig-
inal developers.
Towards the end of the 1990s, some games like Quake III Arena and Unreal
were designed with reuse and “modding” in mind. Engines were made highly
customizable via scripting languages like id’s Quake C, and engine licensing
began to be a viable secondary revenue stream for the developers who created
them. Today, game developers can license a game engine and reuse significant
portions of its key software components in order to build games. While this
practice still involves considerable investment in custom software engineer-
ing, it can be much more economical than developing all of the core engine
components in-house.


<!-- source-pdf-page: 31 -->
> Visual fallback for diagrams/images: [PDF page 31](../../../visual_pages/page_0031.jpg)

Can be “modded” to

Can be used to build any
game imaginable
Cannot be used to build
more than one game

Can be customized to
make very similar games

build any game in a
specific genre

Quake III
Engine

Unity,
Unreal Engine 4,
Source Engine, ...

PacMan

Hydro Thunder
Engine

Probably
impossible

Figure 1.1. Game engine reusability gamut.

The line between a game and its engine is often blurry. Some engines make
a reasonably clear distinction, while others make almost no attempt to sepa-
rate the two. In one game, the rendering code might “know” specifically how
to draw an orc. In another game, the rendering engine might provide general-
purpose material and shading facilities, and “orc-ness” might be defined en-
tirely in data. No studio makes a perfectly clear separation between the game
and the engine, which is understandable considering that the definitions of
these two components often shift as the game’s design solidifies.
Arguably a data-driven architecture is what differentiates a game engine
from a piece of software that is a game but not an engine. When a game
contains hard-coded logic or game rules, or employs special-case code to ren-
der specific types of game objects, it becomes difficult or impossible to reuse
that software to make a different game. We should probably reserve the term
“game engine” for software that is extensible and can be used as the founda-
tion for many different games without major modification.
Clearly this is not a black-and-white distinction. We can think of a gamut
of reusability onto which every engine falls. Figure 1.1 takes a stab at the lo-
cations of some well-known games/engines along this gamut.
One would think that a game engine could be something akin to Apple
QuickTime or Microsoft Windows Media Player—a general-purpose piece of
software capable of playing virtually any game content imaginable. However,
this ideal has not yet been achieved (and may never be). Most game engines
are carefully crafted and fine-tuned to run a particular game on a particular
hardware platform. And even the most general-purpose multiplatform en-
gines are really only suitable for building games in one particular genre, such
as first-person shooters or racing games. It’s safe to say that the more general-
purpose a game engine or middleware component is, the less optimal it is for
running a particular game on a particular platform.
This phenomenon occurs because designing any efficient piece of soft-
ware invariably entails making trade-offs, and those trade-offs are based on
assumptions about how the software will be used and/or about the target


<!-- source-pdf-page: 32 -->

hardware on which it will run. For example, a rendering engine that was de-
signed to handle intimate indoor environments probably won’t be very good
at rendering vast outdoor environments. The indoor engine might use a bi-
nary space partitioning (BSP) tree or portal system to ensure that no geometry
is drawn that is being occluded by walls or objects that are closer to the camera.
The outdoor engine, on the other hand, might use a less-exact occlusion mech-
anism, or none at all, but it probably makes aggressive use of level-of-detail
(LOD) techniques to ensure that distant objects are rendered with a minimum
number of triangles, while using high-resolution triangle meshes for geometry
that is close to the camera.

The advent of ever-faster computer hardware and specialized graphics
cards, along with ever-more-efficient rendering algorithms and data struc-
tures, is beginning to soften the differences between the graphics engines of
different genres. It is now possible to use a first-person shooter engine to build
a strategy game, for example. However, the trade-off between generality and
optimality still exists. A game can always be made more impressive by fine-
tuning the engine to the specific requirements and constraints of a particular
game and/or hardware platform.

## 1.4 Engine Differences across Genres

Game engines are typically somewhat genre specific. An engine designed for
a two-person fighting game in a boxing ring will be very different from a mas-
sively multiplayer online game (MMOG) engine or a first-person shooter (FPS)
engine or a real-time strategy (RTS) engine. However, there is also a great deal
of overlap—all 3D games, regardless of genre, require some form of low-level
user input from the joypad, keyboard and/or mouse, some form of 3D mesh
rendering, some form of heads-up display (HUD) including text rendering in
a variety of fonts, a powerful audio system, and the list goes on. So while
the Unreal Engine, for example, was designed for first-person shooter games,
it has been used successfully to construct games in a number of other genres
as well, including the wildly popular third-person shooter franchise Gears of
War by Epic Games, the hit action-adventure games in the Batman: Arkham se-
ries by Rocksteady Studios, the well-known fighting game Tekken 7 by Bandai
Namco Studios, and the first three role-playing third-person shooter games in
the Mass Effect series by BioWare.

Let’s take a look at some of the most common game genres and explore
some examples of the technology requirements particular to each.


<!-- source-pdf-page: 33 -->
> Visual fallback for diagrams/images: [PDF page 33](../../../visual_pages/page_0033.jpg)

Figure 1.2. Overwatch by Blizzard Entertainment (Xbox One, PlayStation 4, Windows). (See Color
Plate I.)

### 1.4.1 First-Person Shooters (FPS)

The first-person shooter (FPS) genre is typified by games like Quake, Un-
real Tournament, Half-Life, Battlefield, Destiny, Titanfall and Overwatch (see Fig-
ure 1.2). These games have historically involved relatively slow on-foot roam-
ing of a potentially large but primarily corridor-based world. However, mod-
ern first-person shooters can take place in a wide variety of virtual environ-
ments including vast open outdoor areas and confined indoor areas. Modern
FPS traversal mechanics can include on-foot locomotion, rail-confined or free-
roaming ground vehicles, hovercraft, boats and aircraft. For an overview of
this genre, see http://en.wikipedia.org/wiki/First-person_shooter.
First-person games are typically some of the most technologically challeng-
ing to build, probably rivaled in complexity only by third-person shooters,
action-platformer games, and massively multiplayer games. This is because
first-person shooters aim to provide their players with the illusion of being
immersed in a detailed, hyperrealistic world. It is not surprising that many of
the game industry’s big technological innovations arose out of the games in
this genre.
First-person shooters typically focus on technologies such as:

•
efficient rendering of large 3D virtual worlds;


<!-- source-pdf-page: 34 -->
> Visual fallback for diagrams/images: [PDF page 34](../../../visual_pages/page_0034.jpg)

•
a responsive camera control/aiming mechanic;

•
high-fidelity animations of the player’s virtual arms and weapons;

•
a wide range of powerful handheld weaponry;

•
a forgiving player character motion and collision model, which often
gives these games a “floaty” feel;

•
high-fidelity animations and artificial intelligence for the non-player
characters (NPCs)—the player’s enemies and allies; and

•
small-scale online multiplayer capabilities (typically supporting be-
tween 10 and 100 simultaneous players), and the ubiquitous “death
match” gameplay mode.

The rendering technology employed by first-person shooters is almost al-
ways highly optimized and carefully tuned to the particular type of environ-
ment being rendered. For example, indoor “dungeon crawl” games often em-
ploy binary space partitioning trees or portal-based rendering systems. Out-
door FPS games use other kinds of rendering optimizations such as occlusion
culling, or an offline sectorization of the game world with manual or auto-
mated specification of which target sectors are visible from each source sector.
Of course, immersing a player in a hyperrealistic game world requires
much more than just optimized high-quality graphics technology. The charac-
ter animations, audio and music, rigid body physics, in-game cinematics and
myriad other technologies must all be cutting-edge in a first-person shooter.
So this genre has some of the most stringent and broad technology require-
ments in the industry.

### 1.4.2 Platformers and Other Third-Person Games

“Platformer” is the term applied to third-person character-based action games
where jumping from platform to platform is the primary gameplay mechanic.
Typical games from the 2D era include Space Panic, Donkey Kong, Pitfall! and
Super Mario Brothers. The 3D era includes platformers like Super Mario 64,
Crash Bandicoot, Rayman 2, Sonic the Hedgehog, the Jak and Daxter series (Fig-
ure 1.3), the Ratchet & Clank series and Super Mario Galaxy. See http://en.
wikipedia.org/wiki/Platformer for an in-depth discussion of this genre.
In terms of their technological requirements, platformers can usually be
lumped together with third-person shooters and third-person action/adven-
ture games like Just Cause 2, Gears of War 4 (Figure 1.4), the Uncharted series,
the Resident Evil series, the The Last of Us series, Red Dead Redemption 2, and the
list goes on.


<!-- source-pdf-page: 35 -->
> Visual fallback for diagrams/images: [PDF page 35](../../../visual_pages/page_0035.jpg)

Third-person character-based games have a lot in common with first-per-
son shooters, but a great deal more emphasis is placed on the main character’s
abilities and locomotion modes. In addition, high-fidelity full-body charac-
ter animations are required for the player’s avatar, as opposed to the some-
what less-taxing animation requirements of the “floating arms” in a typical
FPS game. It’s important to note here that almost all first-person shooters have
an online multiplayer component, so a full-body player avatar must be ren-
dered in addition to the first-person arms. However, the fidelity of these FPS
player avatars is usually not comparable to the fidelity of the non-player char-
acters in these same games; nor can it be compared to the fidelity of the player
avatar in a third-person game.

In a platformer, the main character is often cartoon-like and not particularly
realistic or high-resolution. However, third-person shooters often feature a
highly realistic humanoid player character. In both cases, the player character
typically has a very rich set of actions and animations.

Some of the technologies specifically focused on by games in this genre
include:

Figure 1.3. Jak II by Naughty Dog (Jak, Daxter, Jak and Daxter, and Jak II © 2003, 2013/™SIE. Created
and developed by Naughty Dog, PlayStation 2.) (See Color Plate II.)


<!-- source-pdf-page: 36 -->
> Visual fallback for diagrams/images: [PDF page 36](../../../visual_pages/page_0036.jpg)

Figure 1.4. Gears of War 4 by The Coalition (Xbox One). (See Color Plate III.)

•
moving platforms, ladders, ropes, trellises and other interesting locomo-
tion modes;

•
puzzle-like environmental elements;

•
a third-person “follow camera” which stays focused on the player char-
acter and whose rotation is typically controlled by the human player via
the right joypad stick (on a console) or the mouse (on a PC—note that
while there are a number of popular third-person shooters on a PC, the
platformer genre exists almost exclusively on consoles); and

•
a complex camera collision system for ensuring that the view point never
“clips” through background geometry or dynamic foreground objects.

### 1.4.3 Fighting Games

Fighting games are typically two-player games involving humanoid charac-
ters pummeling each other in a ring of some sort. The genre is typified by
games like Soul Calibur and Tekken 3 (see Figure 1.5). The Wikipedia page
http://en.wikipedia.org/wiki/Fighting_game provides an overview of this
genre.
Traditionally games in the fighting genre have focused their technology
efforts on:


<!-- source-pdf-page: 37 -->
> Visual fallback for diagrams/images: [PDF page 37](../../../visual_pages/page_0037.jpg)

•
a rich set of fighting animations;
•
accurate hit detection;
•
a user input system capable of detecting complex button and joystick
combinations; and
•
crowds, but otherwise relatively static backgrounds.

Since the 3D world in these games is small and the camera is centered
on the action at all times, historically these games have had little or no need
for world subdivision or occlusion culling. They would likewise not be ex-
pected to employ advanced three-dimensional audio propagation models, for
example.
Modern fighting games like EA’s Fight Night Round 4 and NetherRealm
Studios’ Injustice 2 (Figure 1.6) have upped the technological ante with features
like:

•
high-definition character graphics;
•
realistic skin shaders with subsurface scattering and sweat effects;
•
photo-realistic lighting and particle effects;
•
high-fidelity character animations; and

Figure 1.5. Tekken 3 by Namco (PlayStation). (See Color Plate IV.)


<!-- source-pdf-page: 38 -->
> Visual fallback for diagrams/images: [PDF page 38](../../../visual_pages/page_0038.jpg)

•
physics-based cloth and hair simulations for the characters.

It’s important to note that some fighting games like Ninja Theory’s Heav-
enly Sword and For Honor by Ubisoft Montreal take place in a large-scale virtual
world, not a confined arena. In fact, many people consider this to be a separate
genre, sometimes called a brawler. This kind of fighting game can have tech-
nical requirements more akin to those of a third-person shooter or a strategy
game.

### 1.4.4 Racing Games

The racing genre encompasses all games whose primary task is driving a
car or other vehicle on some kind of track.
The genre has many subcat-
egories. Simulation-focused racing games (“sims”) aim to provide a driv-
ing experience that is as realistic as possible (e.g., Gran Turismo).
Arcade
racers favor over-the-top fun over realism (e.g., San Francisco Rush, Cruis’n
USA, Hydro Thunder). One subgenre explores the subculture of street rac-
ing with tricked out consumer vehicles (e.g., Need for Speed, Juiced).
Kart
racing is a subcategory in which popular characters from platformer games
or cartoon characters from TV are re-cast as the drivers of whacky vehicles
(e.g., Mario Kart, Jak X, Freaky Flyers).
Racing games need not always in-
volve time-based competition. Some kart racing games, for example, offer

Figure 1.6. Injustice 2 by NetherRealm Studios (PlayStation 4, Xbox One, Android, iOS, Microsoft
Windows). (See Color Plate V.)


<!-- source-pdf-page: 39 -->
> Visual fallback for diagrams/images: [PDF page 39](../../../visual_pages/page_0039.jpg)

Figure 1.7. Gran Turismo Sport by Polyphony Digital (PlayStation 4). (See Color Plate VI.)

modes in which players shoot at one another, collect loot or engage in a va-
riety of other timed and untimed tasks. For a discussion of this genre, see
http://en.wikipedia.org/wiki/Racing_game.
A racing game is often very linear, much like older FPS games. However,
travel speed is generally much faster than in an FPS. Therefore, more focus is
placed on very long corridor-based tracks, or looped tracks, sometimes with
various alternate routes and secret short-cuts. Racing games usually focus all
their graphic detail on the vehicles, track and immediate surroundings. As an
example of this, Figure 1.7 shows a screenshot from the latest installment in the
well-known Gran Turismo racing game series, Gran Turismo Sport, developed
by Polyphony Digital and published by Sony Interactive Entertainment. How-
ever, kart racers also devote significant rendering and animation bandwidth
to the characters driving the vehicles.
Some of the technological properties of a typical racing game include the
following techniques:

•
Various “tricks” are used when rendering distant background elements,
such as employing two-dimensional cards for trees, hills and mountains.

•
The track is often broken down into relatively simple two-dimensional
regions called “sectors.” These data structures are used to optimize ren-
dering and visibility determination, to aid in artificial intelligence and
path finding for non-human-controlled vehicles, and to solve many other
technical problems.

•
The camera typically follows behind the vehicle for a third-person per-


<!-- source-pdf-page: 40 -->
> Visual fallback for diagrams/images: [PDF page 40](../../../visual_pages/page_0040.jpg)

Figure 1.8. Age of Empires by Ensemble Studios (Windows). (See Color Plate VII.)

spective, or is sometimes situated inside the cockpit first-person style.

•
When the track involves tunnels and other “tight” spaces, a good deal
of effort is often put into ensuring that the camera does not collide with
background geometry.

### 1.4.5 Strategy Games

The modern strategy game genre was arguably defined by Dune II: The Building
of a Dynasty (1992). Other games in this genre include Warcraft, Command &
Conquer, Age of Empires and Starcraft. In this genre, the player deploys the battle
units in his or her arsenal strategically across a large playing field in an attempt
to overwhelm his or her opponent. The game world is typically displayed
at an oblique top-down viewing angle. A distinction is often made between
turn-based strategy games and real-time strategy (RTS). For a discussion of
this genre, see https://en.wikipedia.org/wiki/Strategy_video_game.
The strategy game player is usually prevented from significantly changing
the viewing angle in order to see across large distances. This restriction per-
mits developers to employ various optimizations in the rendering engine of a
strategy game.


<!-- source-pdf-page: 41 -->
> Visual fallback for diagrams/images: [PDF page 41](../../../visual_pages/page_0041.jpg)

Figure 1.9. Total War: Warhammer 2 by Creative Assembly (Windows). (See Color Plate VIII.)

Older games in the genre employed a grid-based (cell-based) world con-
struction, and an orthographic projection was used to greatly simplify the ren-
derer. For example, Figure 1.8 shows a screenshot from the classic strategy
game Age of Empires.

Modern strategy games sometimes use perspective projection and a true
3D world, but they may still employ a grid layout system to ensure that units
and background elements, such as buildings, align with one another properly.
A popular example, Total War: Warhammer 2, is shown in Figure 1.9.

Some other common practices in strategy games include the following
techniques:

•
Each unit is relatively low-res, so that the game can support large num-
bers of them on-screen at once.

•
Height-field terrain is usually the canvas upon which the game is de-
signed and played.

•
The player is often allowed to build new structures on the terrain in ad-
dition to deploying his or her forces.

•
User interaction is typically via single-click and area-based selection of
units, plus menus or toolbars containing commands, equipment, unit
types, building types, etc.


<!-- source-pdf-page: 42 -->
> Visual fallback for diagrams/images: [PDF page 42](../../../visual_pages/page_0042.jpg)

Figure 1.10. World of Warcraft by Blizzard Entertainment (Windows, MacOS). (See Color Plate IX.)

### 1.4.6 Massively Multiplayer Online Games (MMOG)

The massively multiplayer online game (MMOG or just MMO) genre is typ-
ified by games like Guild Wars 2 (AreaNet/NCsoft), EverQuest (989 Studios/
SOE), World of Warcraft (Blizzard) and Star Wars Galaxies (SOE/Lucas Arts), to
name a few. An MMO is defined as any game that supports huge numbers
of simultaneous players (from thousands to hundreds of thousands), usually
all playing in one very large, persistent virtual world (i.e., a world whose in-
ternal state persists for very long periods of time, far beyond that of any one
player’s gameplay session). Otherwise, the gameplay experience of an MMO
is often similar to that of their small-scale multiplayer counterparts. Subcate-
gories of this genre include MMO role-playing games (MMORPG), MMO real-
time strategy games (MMORTS) and MMO first-person shooters (MMOFPS).
For a discussion of this genre, see http://en.wikipedia.org/wiki/MMOG. Fig-
ure 1.10 shows a screenshot from the hugely popular MMORPG World of War-
craft.

At the heart of all MMOGs is a very powerful battery of servers. These
servers maintain the authoritative state of the game world, manage users sign-
ing in and out of the game, provide inter-user chat or voice-over-IP (VoIP) ser-
vices and more. Almost all MMOGs require users to pay some kind of regular


<!-- source-pdf-page: 43 -->
> Visual fallback for diagrams/images: [PDF page 43](../../../visual_pages/page_0043.jpg)

subscription fee in order to play, and they may offer micro-transactions within
the game world or out-of-game as well. Hence, perhaps the most important
role of the central server is to handle the billing and micro-transactions which
serve as the game developer’s primary source of revenue.

Graphics fidelity in an MMO is almost always lower than its non-massively
multiplayer counterparts, as a result of the huge world sizes and extremely
large numbers of users supported by these kinds of games.

Figure 1.11 shows a screen from Bungie’s latest FPS game, Destiny 2. This
game has been called an MMOFPS because it incorporates some aspects of
the MMO genre. However, Bungie prefers to call it a “shared world” game
because unlike a traditional MMO, in which a player can see and interact with
literally any other player on a particular server, Destiny provides “on-the-fly
match-making.” This permits the player to interact only with the other players
with whom they have been matched by the server; this matchmaking system
has been significantly improved for Destiny 2. Also unlike a traditional MMO,
the graphics fidelity in Destiny 2 is on par with first- and third-person shooters.

We should note here that the game Player Unknown’s Battlegrounds (PUBG)
has recently popularized a subgenre known as battle royale. This type of game
blurs the line between regular multiplayer shooters and massively multiplayer
online games, because they typically pit on the order of 100 players against
each other in an online world, employing a survival-based “last man standing”
gameplay style.

Figure 1.11. Destiny 2 by Bungie, © 2018 Bungie Inc. (Xbox One, PlayStation 4, PC) (See Color Plate X.)


<!-- source-pdf-page: 44 -->
> Visual fallback for diagrams/images: [PDF page 44](../../../visual_pages/page_0044.jpg)

### 1.4.7 Player-Authored Content

As social media takes off, games are becoming more and more collaborative
in nature. A recent trend in game design is toward player-authored content. For
example, Media Molecule’s LittleBigPlanet,™LittleBigPlanet™2 (Figure 1.12)
and LittleBigPlanet™3: The Journey Home are technically puzzle platformers, but
their most notable and unique feature is that they encourage players to create,
publish and share their own game worlds. Media Molecule’s latest installment
in this engaging genre is Dreams for the PlayStation 4 (Figure 1.13).

Perhaps the most popular game today in the player-created content genre
is Minecraft (Figure 1.14). The brilliance of this game lies in its simplicity:
Minecraft game worlds are constructed from simple cubic voxel-like elements
mapped with low-resolution textures to mimic various materials. Blocks can
be solid, or they can contain items such as torches, anvils, signs, fences and
panes of glass. The game world is populated with one or more player charac-
ters, animals such as chickens and pigs, and various “mobs”—good guys like
villagers and bad guys like zombies and the ubiquitous creepers who sneak up
on unsuspecting players and explode (only scant moments after warning the
player with the “hiss” of a burning fuse).

Players can create a randomized world in Minecraft and then dig into the
generated terrain to create tunnels and caverns. They can also construct their
own structures, ranging from simple terrain and foliage to vast and complex

Figure 1.12. LittleBigPlanet™2 by Media Molecule, © 2014 Sony Interactive Entertainment (PlaySta-
tion 3). (See Color Plate XI.)


<!-- source-pdf-page: 45 -->
> Visual fallback for diagrams/images: [PDF page 45](../../../visual_pages/page_0045.jpg)

Figure 1.13. Dreams by Media Molecule, © 2017 Sony Computer Computer Europe (PlayStation 4).
(See Color Plate XII.)

buildings and machinery. Perhaps the biggest stroke of genius in Minecraft
is redstone. This material serves as “wiring,” allowing players to lay down
circuitry that controls pistons, hoppers, mine carts and other dynamic ele-
ments in the game. As a result, players can create virtually anything they can
imagine, and then share their worlds with their friends by hosting a server and
inviting them to play online.

Figure 1.14. Minecraft by Markus “Notch” Persson / Mojang AB (Windows, MacOS, Xbox 360, PlaySta-
tion 3, PlayStation Vita, iOS). (See Color Plate XIII.)


<!-- source-pdf-page: 46 -->

### 1.4.8 Virtual, Augmented and Mixed Reality

Virtual, augmented and mixed reality are exciting new technologies that aim
to immerse the viewer in a 3D world that is either entirely generated by a com-
puter, or is augmented by computer-generated imagery. These technologies
have many applications outside the game industry, but they have also become
viable platforms for a wide range of gaming content.

1.4.8.1
Virtual Reality

Virtual reality (VR) can be defined as an immersive multimedia or computer-
simulated reality that simulates the user’s presence in an environment that is
either a place in the real world or in an imaginary world. Computer-generated
VR (CG VR) is a subset of this technology in which the virtual world is exclu-
sively generated via computer graphics. The user views this virtual environ-
ment by donning a headset such as HTC Vive, Oculus Rift, Sony PlayStation
VR, Samsung Gear VR or Google Daydream View. The headset displays the
content directly in front of the user’s eyes; the system also tracks the move-
ment of the headset in the real world, so that the virtual camera’s movements
can be perfectly matched to those of the person wearing the headset. The user
typically holds devices in his or her hands which allow the system to track the
movements of each hand. This allows the user to interact in the virtual world:
Objects can be pushed, picked up or thrown, for example.

1.4.8.2
Augmented and Mixed Reality

The terms augmented reality (AR) and mixed reality (MR) are often confused
or used interchangeably. Both technologies present the user with a view of
the real world, but with computer graphics used to enhance the experience.
In both technologies, a viewing device like a smart phone, tablet or tech-
enhanced pair of glasses displays a real-time or static view of a real-world
scene, and computer graphics are overlaid on top of this image. In real-time
AR and MR systems, accelerometers in the viewing device permit the virtual
camera’s movements to track the movements of the device, producing the il-
lusion that the device is simply a window through which we are viewing the
actual world, and hence giving the overlaid computer graphics a strong sense
of realism.
Some people make a distinction between these two technologies by us-
ing the term “augmented reality” to describe technologies in which computer
graphics are overlaid on a live, direct or indirect view of the real world, but are
not anchored to it. The term “mixed reality,” on the other hand, is more often


<!-- source-pdf-page: 47 -->

applied to the use of computer graphics to render imaginary objects which
are anchored to the real world and appear to exist within it. However, this
distinction is by no means universally accepted.
Here are a few examples of AR technology in action:

•
The U.S. Army provides its soldiers with improved tactical awareness
using a system dubbed “tactical augmented reality” (TAR)—it overlays
a video-game-like heads-up display (HUD) complete with a mini-map
and object markers onto the soldier’s view of the real world (https://
youtu.be/x8p19j8C6VI).
•
In 2015, Disney demonstrated some cool AR technology that renders a
3D cartoon character on top of a sheet of real-world paper on which a
2D version of the character is colored with a crayon (https://youtu.be/
SWzurBQ81CM).
•
PepsiCo also pranked commuting Londoners with an AR-enabled bus
stop. People sitting in the bus stop enclosure were treated to AR images
of a prowling tiger, a meteor crashing, and an alien tentacle grabbing
unwitting passers by off the street (https://youtu.be/Go9rf9GmYpM).

And here are a few examples of MR:

•
Starting with Android 8.1, the camera app on the Pixel 1 and Pixel 2
supports AR Stickers, a fun feature that allows users to place animated
3D objects and characters into videos and photos.
•
Microsoft’s HoloLens is another example of mixed reality. It overlays
world-anchored graphics onto a live video image, and can be used for a
wide range of applications including education and training, engineer-
ing, health care, and entertainment.

1.4.8.3
VR/AR/MR Games

The game industry is currently experimenting with VR and AR/MR technolo-
gies, and is trying to find its footing within these new media. Some traditional
3D games have been “ported” to VR, yielding very interesting, if not particu-
larly innovative, experiences. But perhaps more exciting, entirely new game
genres are starting to emerge, offering gameplay experiences that could not be
achieved without VR or AR/MR.
For example, Job Simulator by Owlchemy Labs plunges the user into a vir-
tual job museum run by robots, and asks them to perform tongue-in-cheek
approximations of various real-world jobs, making use of game mechanics


<!-- source-pdf-page: 48 -->
> Visual fallback for diagrams/images: [PDF page 48](../../../visual_pages/page_0048.jpg)

that simply wouldn’t work on a non-VR platform. Owlchemy’s next install-
ment, Vacation Simulator, applies the same whimsical sense of humour and art
style to a world in which the robots of Job Simulator invite the player to relax
and perform various tasks. Figure 1.15 shows a screenshot from another in-
novative (and somewhat disturbing!) game for HTC Vive called Accounting,
from the creators of “Rick & Morty” and The Stanley Parable.

1.4.8.4
VR Game Engines

VR game engines are technologically similar in many respects to first-person
shooter engines, and in fact many FPS-capable engines such as Unity and Un-
real Engine support VR “out of the box.” However, VR games differ from FPS
games in a number of significant ways:

•
Stereoscopic rendering. A VR game needs to render the scene twice, once
for each eye. This doubles the number of graphics primitives that must
be rendered, although other aspects of the graphics pipeline such as vis-
ibility culling can be performed only once per frame, since the eyes are
reasonably close together. As such, a VR game isn’t quite as expensive to
render as the same game would be to render in split-screen multiplayer
mode, but the principle of rendering each frame twice from two (slightly)
different virtual cameras is the same.

•
Very high frame rate. Studies have shown that VR running at below 90

Figure 1.15. Accounting by Squanchtendo and Crows Crows Crows (HTC Vive). (See Color Plate XIV.)


<!-- source-pdf-page: 49 -->

frames per second is likely to induce disorientation, nausea, and other
negative user effects. This means that not only do VR systems need to
render the scene twice per frame, they need to do so at 90+ FPS. This is
why VR games and applications are generally required to run on high-
powered CPU and GPU hardware.
•
Navigation issues. In an FPS game, the player can simply walk around
the game world with the joypad or the WASD keys. In a VR game, a
small amount of movement can be realized by the user physically walk-
ing around in the real world, but the safe physical play area is typically
quite small (the size of a small bathroom or closet). Travelling by “fly-
ing” tends to induce nausea as well, so most games opt for a point-and-
click teleportation mechanism to move the virtual player/camera across
larger distances. Various real-world devices have also been conceived
that allow a VR user to “walk” in place with their feet in order to move
around in a VR world.

Of course, VR makes up for these limitations somewhat by enabling new user
interaction paradigms that aren’t possible in traditional video games. For ex-
ample,

•
users can reach in the real world to touch, pick up and throw objects in
the virtual world;
•
a player can dodge an attack in the virtual world by dodging physically
in the real world;
•
new user interface opportunities are possible, such as having floating
menus attached to one’s virtual hands, or seeing a game’s credits written
on a whiteboard in the virtual world;
•
a player can even pick up a pair of virtual VR goggles and place them
onto his or her head, thereby transporting them into a “nested” VR
world—an effect that might best be called “VR-ception.”

1.4.8.5
Location-Based Entertainment

Games like Pokémon Go neither overlay graphics onto an image of the real
world, nor do they generate a completely immersive virtual world. However,
the user’s view of the computer-generated world of Pokémon Go does react
to movements of the user’s phone or tablet, much like a 360-degree video.
And the game is aware of your actual location in the real world, prompting
you to go searching for Pokémon in nearby parks, malls and restaurants. This
kind of game can’t really be called AR/MR, but neither does it fall into the VR
category. Such a game might be better described as a form of location-based


<!-- source-pdf-page: 50 -->

entertainment, although some people do use the AR moniker for these kinds of
games.

### 1.4.9 Other Genres

There are of course many other game genres which we won’t cover in depth
here. Some examples include:

•
sports, with subgenres for each major sport (football, baseball, soccer,
golf, etc.);

•
role-playing games (RPG);

•
God games, like Populous and Black & White;

•
environmental/social simulation games, like SimCity or The Sims;

•
puzzle games like Tetris;

•
conversions of non-electronic games, like chess, card games, go, etc.;

•
web-based games, such as those offered at Electronic Arts’ Pogo site;

and the list goes on.
We have seen that each game genre has its own particular technological re-
quirements. This explains why game engines have traditionally differed quite
a bit from genre to genre. However, there is also a great deal of technologi-
cal overlap between genres, especially within the context of a single hardware
platform. With the advent of more and more powerful hardware, differences
between genres that arose because of optimization concerns are beginning to
evaporate. It is therefore becoming increasingly possible to reuse the same en-
gine technology across disparate genres, and even across disparate hardware
platforms.

## 1.5 Game Engine Survey

### 1.5.1 The Quake Family of Engines

The first 3D first-person shooter (FPS) game is generally accepted to be Castle
Wolfenstein 3D (1992). Written by id Software of Texas for the PC platform, this
game led the game industry in a new and exciting direction. id Software went
on to create Doom, Quake, Quake II and Quake III. All of these engines are very
similar in architecture, and I will refer to them as the Quake family of engines.
Quake technology has been used to create many other games and even other
engines. For example, the lineage of Medal of Honor for the PC platform goes
something like this:


<!-- source-pdf-page: 51 -->

•
Quake III (id Software);

•
Sin (Ritual);

•
F.A.K.K. 2 (Ritual);

•
Medal of Honor: Allied Assault (2015 & Dreamworks Interactive); and

•
Medal of Honor: Pacific Assault (Electronic Arts, Los Angeles).

Many other games based on Quake technology follow equally circuitous
paths through many different games and studios. In fact, Valve’s Source en-
gine (used to create the Half-Life games) also has distant roots in Quake tech-
nology.
The Quake and Quake II source code is freely available, and the original
Quake engines are reasonably well architected and “clean” (although they are
of course a bit outdated and written entirely in C). These code bases serve
as great examples of how industrial-strength game engines are built. The
full source code to Quake and Quake II is available at https://github.com/
id-Software/Quake-2.
If you own the Quake and/or Quake II games, you can actually build the
code using Microsoft Visual Studio and run the game under the debugger us-
ing the real game assets from the disk. This can be incredibly instructive. You
can set breakpoints, run the game and then analyze how the engine actually
works by stepping through the code. I highly recommend downloading one
or both of these engines and analyzing the source code in this manner.

### 1.5.2 Unreal Engine

Epic Games, Inc. burst onto the FPS scene in 1998 with its legendary game Un-
real. Since then, the Unreal Engine has become a major competitor to Quake
technology in the FPS space. Unreal Engine 2 (UE2) is the basis for Unreal
Tournament 2004 (UT2004) and has been used for countless “mods,” univer-
sity projects and commercial games. Unreal Engine 4 (UE4) is the latest evolu-
tionary step, boasting some of the best tools and richest engine feature sets in
the industry, including a convenient and powerful graphical user interface for
creating shaders and a graphical user interface for game logic programming
called Blueprints (previously known as Kismet).
The Unreal Engine has become known for its extensive feature set and co-
hesive, easy-to-use tools. The Unreal Engine is not perfect, and most devel-
opers modify it in various ways to run their game optimally on a particular
hardware platform. However, Unreal is an incredibly powerful prototyping
tool and commercial game development platform, and it can be used to build
virtually any 3D first-person or third-person game (not to mention games in


<!-- source-pdf-page: 52 -->

other genres as well). Many exciting games in all sorts of genres have been
developed with UE4, including Rime by Tequila Works, Genesis: Alpha One by
Radiation Blue, A Way Out by Hazelight Studios, and Crackdown 3 by Microsoft
Studios.
The Unreal Developer Network (UDN) provides a rich set of documenta-
tion and other information about all released versions of the Unreal Engine
(see http://udn.epicgames.com/Main/WebHome.html). Some documenta-
tion is freely available. However, access to the full documentation for the latest
version of the Unreal Engine is generally restricted to licensees of the engine.
There are plenty of other useful websites and wikis that cover the Unreal En-
gine. One popular one is http://www.beyondunreal.com.
Thankfully, Epic now offers full access to Unreal Engine 4, source code and
all, for a low monthly subscription fee plus a cut of your game’s profits if it
ships. This makes UE4 a viable choice for small independent game studios.

### 1.5.3 The Half-Life Source Engine

Source is the game engine that drives the well-known Half-Life 2 and its sequels
HL2: Episode One and HL2: Episode Two, Team Fortress 2 and Portal (shipped to-
gether under the title The Orange Box). Source is a high-quality engine, rivaling
Unreal Engine 4 in terms of graphics capabilities and tool set.

### 1.5.4 DICE’s Frostbite

The Frostbite engine grew out of DICE’s efforts to create a game engine for Bat-
tlefield Bad Company in 2006. Since then, the Frostbite engine has become the
most widely adopted engine within Electronic Arts (EA); it is used by many of
EA’s key franchises including Mass Effect, Battlefield, Need for Speed, Dragon Age,
and Star Wars Battlefront II. Frostbite boasts a powerful unified asset creation
tool called FrostEd, a powerful tools pipeline known as Backend Services, and
a powerful runtime game engine. It is a proprietary engine, so it’s unfortu-
nately unavailable for use by developers outside EA.

### 1.5.5 Rockstar Advanced Game Engine (RAGE)

RAGE is the engine that drives the insanely popular Grand Theft Auto V. De-
veloped by RAGE Technology Group, a division of Rockstar Games’ Rockstar
San Diego studio, RAGE has been used by Rockstar Games’ internal studios to
develop games for PlayStation 4, Xbox One, PlayStation 3, Xbox 360, Wii, Win-
dows, and MacOS. Other games developed on this proprietary engine include
Grand Theft Auto IV, Red Dead Redemption and Max Payne 3.


<!-- source-pdf-page: 53 -->

### 1.5.6 CRYENGINE

Crytek originally developed their powerful game engine known as CRYEN-
GINE as a tech demo for NVIDIA. When the potential of the technology was
recognized, Crytek turned the demo into a complete game and Far Cry was
born. Since then, many games have been made with CRYENGINE including
Crysis, Codename Kingdoms, Ryse: Son of Rome, and Everyone’s Gone to the Rap-
ture. Over the years the engine has evolved into what is now Crytek’s latest
offering, CRYENGINE V. This powerful game development platform offers a
powerful suite of asset-creation tools and a feature-rich runtime engine featur-
ing high-quality real-time graphics. CRYENGINE can be used to make games
targeting a wide range of platforms including Xbox One, Xbox 360, PlaySta-
tion 4, PlayStation 3, Wii U, Linux, iOS and Android.

### 1.5.7 Sony’s PhyreEngine

In an effort to make developing games for Sony’s PlayStation 3 platform more
accessible, Sony introduced PhyreEngine at the Game Developer’s Conference
(GDC) in 2008. As of 2013, PhyreEngine has evolved into a powerful and full-
featured game engine, supporting an impressive array of features including
advanced lighting and deferred rendering. It has been used by many studios to
build over 90 published titles, including thatgamecompany’s hits flOw, Flower
and Journey, and Coldwood Interactive’s Unravel. PhyreEngine now supports
Sony’s PlayStation 4, PlayStation 3, PlayStation 2, PlayStation Vita and PSP
platforms. PhyreEngine gives developers access to the power of the highly
parallel Cell architecture on PS3 and the advanced compute capabilities of the
PS4, along with a streamlined new world editor and other powerful game de-
velopment tools. It is available free of charge to any licensed Sony developer
as part of the PlayStation SDK.

### 1.5.8 Microsoft’s XNA Game Studio

Microsoft’s XNA Game Studio is an easy-to-use and highly accessible game
development platform based on the C# language and the Common Language
Runtime (CLR), and aimed at encouraging players to create their own games
and share them with the online gaming community, much as YouTube encour-
ages the creation and sharing of home-made videos.
For better or worse, Microsoft officially retired XNA in 2014. However,
developers can port their XNA games to iOS, Android, Mac OS X, Linux
and Windows 8 Metro via an open-source implementation of XNA called
MonoGame. For more details, see https://www.windowscentral.com/xna-
dead-long-live-xna.


<!-- source-pdf-page: 54 -->

### 1.5.9 Unity

Unity is a powerful cross-platform game development environment and run-
time engine supporting a wide range of platforms. Using Unity, developers
can deploy their games on mobile platforms (e.g., Apple iOS, Google An-
droid), consoles (Microsoft Xbox 360 and Xbox One, Sony PlayStation 3 and
PlayStation 4, and Nintendo Wii, Wii U), handheld gaming platforms (e.g.,
Playstation Vita, Nintendo Switch), desktop computers (Microsoft Windows,
Apple Macintosh and Linux), TV boxes (e.g., Android TV and tvOS) and vir-
tual reality (VR) systems (e.g., Oculus Rift, Steam VR, Gear VR).
Unity’s primary design goals are ease of development and cross-platform
game deployment. As such, Unity provides an easy-to-use integrated editor
environment, in which you can create and manipulate the assets and entities
that make up your game world and quickly preview your game in action right
there in the editor, or directly on your target hardware. Unity also provides a
powerful suite of tools for analyzing and optimizing your game on each tar-
get platform, a comprehensive asset conditioning pipeline, and the ability to
manage the performance-quality trade-off uniquely on each deployment plat-
form. Unity supports scripting in JavaScript, C# or Boo; a powerful animation
system supporting animation retargeting (the ability to play an animation au-
thored for one character on a totally different character); and support for net-
worked multiplayer games.
Unity has been used to create a wide variety of published games, including
Deus Ex: The Fall by N-Fusion/Eidos Montreal, Hollow Knight by Team Cherry,
and the subversive retro-style Cuphead by StudioMDHR. The Webby Award
winning short film Adam was rendered in real time using Unity.

### 1.5.10 Other Commercial Game Engines

There are lots of other commercial game engines out there. Although indie de-
velopers may not have the budget to purchase an engine, many of these prod-
ucts have great online documentation and/or wikis that can serve as a great
source of information about game engines and game programming in general.
For example, check out the Tombstone engine (http://tombstoneengine.com/)
by Terathon Software, the LeadWerks engine (https://www.leadwerks.com/),
and HeroEngine by Idea Fabrik, PLC (http://www.heroengine.com/).

### 1.5.11 Proprietary In-House Engines

Many companies build and maintain proprietary in-house game engines.
Electronic Arts built many of its RTS games on a proprietary engine called
Sage, developed at Westwood Studios. Naughty Dog’s Crash Bandicoot and


<!-- source-pdf-page: 55 -->

Jak and Daxter franchises were built on a proprietary engine custom tailored
to the PlayStation and PlayStation 2. For the Uncharted series, Naughty Dog
developed a brand new engine custom tailored to the PlayStation 3 hardware.
This engine evolved and was ultimately used to create Naughty Dog’s The Last
of Us series on the PlayStation 3 and PlayStation 4, as well as its most recent
releases, Uncharted 4: A Thief’s End and Uncharted: The Lost Legacy. And of
course, most commercially licensed game engines like Quake, Source, Unreal
Engine 4 and CRYENGINE all started out as proprietary in-house engines.

### 1.5.12 Open Source Engines

Open source 3D game engines are engines built by amateur and professional
game developers and provided online for free. The term “open source” typi-
cally implies that source code is freely available and that a somewhat open de-
velopment model is employed, meaning almost anyone can contribute code.
Licensing, if it exists at all, is often provided under the Gnu Public License
(GPL) or Lesser Gnu Public License (LGPL). The former permits code to be
freely used by anyone, as long as their code is also freely available; the latter
allows the code to be used even in proprietary for-profit applications. Lots of
other free and semi-free licensing schemes are also available for open source
projects.
There are a staggering number of open source engines available on the
web. Some are quite good, some are mediocre and some are just plain aw-
ful! The list of game engines provided online at http://en.wikipedia.org/
wiki/List_of_game_engines will give you a feel for the sheer number of en-
gines that are out there. (The list at http://www.worldofleveldesign.com/
categories/level_design_tutorials/recommended-game-engines.php is a bit
more digestible.) Both of these lists include both open-source and commer-
cial game engines.
OGRE is a well-architected, easy-to-learn and easy-to-use 3D rendering
engine. It boasts a fully featured 3D renderer including advanced lighting
and shadows, a good skeletal character animation system, a two-dimensional
overlay system for heads-up displays and graphical user interfaces, and a
post-processing system for full-screen effects like bloom.
OGRE is, by its
authors’ own admission, not a full game engine, but it does provide
many of the foundational components required by pretty much any game
engine.
Some other well-known open source engines are listed here:

•
Panda3D is a script-based engine. The engine’s primary interface is the
Python custom scripting language. It is designed to make prototyping


<!-- source-pdf-page: 56 -->

3D games and virtual worlds convenient and fast.

•
Yake is a game engine built on top of OGRE.

•
Crystal Space is a game engine with an extensible modular architecture.

•
Torque and Irrlicht are also well-known open-source game engines.

•
While not technically open-source, the Lumberyard engine does provide
source code to its developers. It is a free cross-platform engine developed
by Amazon, and based on the CRYENGINE architecture.

### 1.5.13 2D Game Engines for Non-programmers

Two-dimensional games have become incredibly popular with the recent ex-
plosion of casual web gaming and mobile gaming on platforms like Apple
iPhone/iPad and Google Android. A number of popular game/multimedia
authoring toolkits have become available, enabling small game studios and
independent developers to create 2D games for these platforms.
These
toolkits emphasize ease of use and allow users to employ a graphical user
interface to create a game rather than requiring the use of a programming lan-
guage. Check out this YouTube video to get a feel for the kinds of games
you can create with these toolkits:
https://www.youtube.com/watch?v=
3Zq1yo0lxOU

•
Multimedia Fusion 2 (http://www.clickteam.com/website/world is a 2D
game/multimedia authoring toolkit developed by Clickteam. Fusion is
used by industry professionals to create games, screen savers and other
multimedia applications. Fusion and its simpler counterpart, The Games
Factory 2, are also used by educational camps like PlanetBravo (http:
//www.planetbravo.com) to teach kids about game development and
programming/logic concepts. Fusion supports the iOS, Android, Flash,
and Java platforms.

•
Game Salad Creator (http://gamesalad.com/creator) is another graphical
game/multimedia authoring toolkit aimed at non-programmers, similar
in many respects to Fusion.

•
Scratch (http://scratch.mit.edu) is an authoring toolkit and graphical
programming language that can be used to create interactive demos and
simple games. It is a great way for young people to learn about pro-
gramming concepts such as conditionals, loops and event-driven pro-
gramming. Scratch was developed in 2003 by the Lifelong Kindergarten
group, led by Mitchel Resnick at the MIT Media Lab.


<!-- source-pdf-page: 57 -->
> Visual fallback for diagrams/images: [PDF page 57](../../../visual_pages/page_0057.jpg)

## 1.6 Runtime Engine Architecture

A game engine generally consists of a tool suite and a runtime component.
We’ll explore the architecture of the runtime piece first and then get into tool
architecture in the following section.

Figure 1.16 shows all of the major runtime components that make up a
typical 3D game engine. Yeah, it’s big! And this diagram doesn’t even account
for all the tools. Game engines are definitely large software systems.

Like all software systems, game engines are built in layers. Normally upper
layers depend on lower layers, but not vice versa. When a lower layer depends
upon a higher layer, we call this a circular dependency. Dependency cycles are to
be avoided in any software system, because they lead to undesirable coupling
between systems, make the software untestable and inhibit code reuse. This
is especially true for a large-scale system like a game engine.

What follows is a brief overview of the components shown in the diagram
in Figure 1.16. The rest of this book will be spent investigating each of these
components in a great deal more depth and learning how these components
are usually integrated into a functional whole.

### 1.6.1 Target Hardware

The target hardware layer represents the computer system or console on which
the game will run. Typical platforms include Microsoft Windows, Linux and
MacOS-based PCs; mobile platforms like the Apple iPhone and iPad, An-
droid smart phones and tablets, Sony’s PlayStation Vita and Amazon’s Kindle
Fire (among others); and game consoles like Microsoft’s Xbox, Xbox 360 and
Xbox One, Sony’s PlayStation, PlayStation 2, PlayStation 3 and PlayStation 4,
and Nintendo’s DS, GameCube, Wii, Wii U and Switch. Most of the topics in
this book are platform-agnostic, but we’ll also touch on some of the design
considerations peculiar to PC or console development, where the distinctions
are relevant.

### 1.6.2 Device Drivers

Device drivers are low-level software components provided by the operating
system or hardware vendor. Drivers manage hardware resources and shield
the operating system and upper engine layers from the details of communi-
cating with the myriad variants of hardware devices available.


<!-- source-pdf-page: 58 -->
> Visual fallback for diagrams/images: [PDF page 58](../../../visual_pages/page_0058.jpg)

Figure 1.16. Runtime game engine architecture.


<!-- source-pdf-page: 59 -->
> Visual fallback for diagrams/images: [PDF page 59](../../../visual_pages/page_0059.jpg)

### 1.6.3 Operating System

On a PC, the operating system (OS) is running all the time. It orchestrates the
execution of multiple programs on a single computer, one of which is your
game. Operating systems like Microsoft Windows employ a time-sliced ap-
proach to sharing the hardware with multiple running programs, known as
preemptive multitasking. This means that a PC game can never assume it has
full control of the hardware—it must “play nice” with other programs in the
system.
On early consoles, the operating system, if one existed at all, was just a thin
library layer that was compiled directly into your game executable. On those
early systems, the game “owned” the entire machine while it was running.
However, on modern consoles this is no longer the case. The operating sys-
tem on the Xbox 360, PlayStation 3, Xbox One and PlayStation 4 can interrupt
the execution of your game, or take over certain system resources, in order to
display online messages, or to allow the player to pause the game and bring
up the PS4’s “XMB” user interface or the Xbox One’s dashboard, for example.
On the PS4 and Xbox One, the OS is continually running background tasks,
such as recording video of your playthrough in case you decide to share it via
the PS4’s Share button, or downloading games, patches and DLC, so you can
have fun playing a game while you wait. So the gap between console and PC
development is gradually closing (for better or for worse).

### 1.6.4 Third-Party SDKs and Middleware

Most game engines leverage a number of third-party software development
kits (SDKs) and middleware, as shown in Figure 1.17. The functional or class-
based interface provided by an SDK is often called an application program-
ming interface (API). We will look at a few examples.

Figure 1.17. Third-party SDK layer.

1.6.4.1
Data Structures and Algorithms

Like any software system, games depend heavily on container data structures
and algorithms to manipulate them. Here are a few examples of third-party
libraries that provide these kinds of services:


<!-- source-pdf-page: 60 -->

•
Boost. Boost is a powerful data structures and algorithms library, de-
signed in the style of the standard C++ library and its predecessor, the
standard template library (STL). (The online documentation for Boost is
also a great place to learn about computer science in general!)

•
Folly. Folly is a library used at Facebook whose goal is to extend the
standard C++ library and Boost with all sorts of useful facilities, with an
emphasis on maximizing code performance.

•
Loki. Loki is a powerful generic programming template library which is
exceedingly good at making your brain hurt!

The C++ Standard Library and STL

The C++ standard library also provides many of the same kinds of facil-
ities found in third-party libraries like Boost. The subset of the standard li-
brary that implements generic container classes such as std::vector and
std::list is often referred to as the standard template library (STL), although
this is technically a bit of a misnomer: The standard template library was writ-
ten by Alexander Stepanov and David Musser in the days before the C++ lan-
guage was standardized. Much of this library’s functionality was absorbed
into what is now the C++ standard library. When we use the term STL in this
book, it’s usually in the context of the subset of the C++ standard library that
provides generic container classes, not the original STL.

1.6.4.2
Graphics

Most game rendering engines are built on top of a hardware interface library,
such as the following:

•
Glide is the 3D graphics SDK for the old Voodoo graphics cards. This
SDK was popular prior to the era of hardware transform and lighting
(hardware T&L) which began with DirectX 7.

•
OpenGL is a widely used portable 3D graphics SDK.

•
DirectX is Microsoft’s 3D graphics SDK and primary rival to OpenGL.

•
libgcm is a low-level direct interface to the PlayStation 3’s RSX graphics
hardware, which was provided by Sony as a more efficient alternative to
OpenGL.

•
Edge is a powerful and highly efficient rendering and animation engine
produced by Naughty Dog and Sony for the PlayStation 3 and used by
a number of first- and third-party game studios.


<!-- source-pdf-page: 61 -->

•
Vulkan is a low-level library created by the Khronos™Group which en-
ables game programmers to submit rendering batches and GPGPU com-
pute jobs directly to the GPU as command lists, and provides them with
fine-grained control over memory and other resources that are shared
between the CPU and GPU. (See Section 4.11 for more on GPGPU pro-
gramming.)

1.6.4.3
Collision and Physics

Collision detection and rigid body dynamics (known simply as “physics”
in the game development community) are provided by the following well-
known SDKs:

•
Havok is a popular industrial-strength physics and collision engine.
•
PhysX is another popular industrial-strength physics and collision en-
gine, available for free download from NVIDIA.
•
Open Dynamics Engine (ODE) is a well-known open source physics/col-
lision package.

1.6.4.4
Character Animation

A number of commercial animation packages exist, including but certainly not
limited to the following:

•
Granny. Rad Game Tools’ popular Granny toolkit includes robust 3D
model and animation exporters for all the major 3D modeling and ani-
mation packages like Maya, 3D Studio MAX, etc., a runtime library for
reading and manipulating the exported model and animation data, and
a powerful runtime animation system. In my opinion, the Granny SDK
has the best-designed and most logical animation API of any I’ve seen,
commercial or proprietary, especially its excellent handling of time.
•
Havok Animation. The line between physics and animation is becoming
increasingly blurred as characters become more and more realistic. The
company that makes the popular Havok physics SDK decided to cre-
ate a complimentary animation SDK, which makes bridging the physics-
animation gap much easier than it ever has been.
•
OrbisAnim. The OrbisAnim library produced for the PS4 by SN Systems
in conjunction with the ICE and game teams at Naughty Dog, the Tools
and Technology group of Sony Interactive Entertainment, and Sony’s
Advanced Technology Group in Europe includes a powerful and effi-
cient animation engine and an efficient geometry-processing engine for
rendering.


<!-- source-pdf-page: 62 -->
> Visual fallback for diagrams/images: [PDF page 62](../../../visual_pages/page_0062.jpg)

1.6.4.5
Biomechanical Character Models

•
Endorphin and Euphoria.
These are animation packages that produce
character motion using advanced biomechanical models of realistic hu-
man movement.

As we mentioned previously, the line between character animation and
physics is beginning to blur. Packages like Havok Animation try to marry
physics and animation in a traditional manner, with a human animator pro-
viding the majority of the motion through a tool like Maya and with physics
augmenting that motion at runtime. But a firm called Natural Motion Ltd. has
produced a product that attempts to redefine how character motion is handled
in games and other forms of digital media.
Its first product, Endorphin, is a Maya plug-in that permits animators to
run full biomechanical simulations on characters and export the resulting an-
imations as if they had been hand animated. The biomechanical model ac-
counts for center of gravity, the character’s weight distribution, and detailed
knowledge of how a real human balances and moves under the influence of
gravity and other forces.
Its second product, Euphoria, is a real-time version of Endorphin intended
to produce physically and biomechanically accurate character motion at run-
time under the influence of unpredictable forces.

### 1.6.5 Platform Independence Layer

Most game engines are required to be capable of running on more than one
hardware platform. Companies like Electronic Arts and ActivisionBlizzard
Inc., for example, always target their games at a wide variety of platforms be-
cause it exposes their games to the largest possible market. Typically, the only
game studios that do not target at least two different platforms per game are
first-party studios, like Sony’s Naughty Dog and Insomniac studios. There-
fore, most game engines are architected with a platform independence layer,
like the one shown in Figure 1.18. This layer sits atop the hardware, drivers,
operating system and other third-party software and shields the rest of the
engine from the majority of knowledge of the underlying platform by “wrap-
ping” certain interface functions in custom functions over which you, the game
developer, will have control on every target platform.
There are two primary reasons to “wrap” functions as part of your game
engine’s platform independence layer like this: First, some application pro-
gramming interfaces (APIs), like those provided by the operating system, or
even some functions in older “standard” libraries like the C standard library,


<!-- source-pdf-page: 63 -->
> Visual fallback for diagrams/images: [PDF page 63](../../../visual_pages/page_0063.jpg)

differ significantly from platform to platform; wrapping these functions pro-
vides the rest of your engine with a consistent API across all of your targeted
platforms. Second, even when using a fully cross-platform library such as Ha-
vok, you might want to insulate yourself from future changes, such as transi-
tioning your engine to a different collision/physics library in the future.

Figure 1.18. Platform independence layer.

### 1.6.6 Core Systems

Every game engine, and really every large, complex C++ software application,
requires a grab bag of useful software utilities. We’ll categorize these under
the label “core systems.” A typical core systems layer is shown in Figure 1.19.
Here are a few examples of the facilities the core layer usually provides:

•
Assertions are lines of error-checking code that are inserted to catch log-
ical mistakes and violations of the programmer’s original assumptions.
Assertion checks are usually stripped out of the final production build
of the game. (Assertions are covered in Section 3.2.3.3.)
•
Memory management. Virtually every game engine implements its own
custom memory allocation system(s) to ensure high-speed allocations
and deallocations and to limit the negative effects of memory fragmen-
tation (see Section 6.2.1).
•
Math library. Games are by their nature highly mathematics-intensive.
As such, every game engine has at least one, if not many, math libraries.
These libraries provide facilities for vector and matrix math, quaternion
rotations, trigonometry, geometric operations with lines, rays, spheres,
frusta, etc., spline manipulation, numerical integration, solving systems
of equations and whatever other facilities the game programmers re-
quire.
•
Custom data structures and algorithms. Unless an engine’s designers de-
cided to rely entirely on third-party packages such as Boost and Folly,
a suite of tools for managing fundamental data structures (linked lists,
dynamic arrays, binary trees, hash maps, etc.) and algorithms (search,
sort, etc.) is usually required. These are often hand coded to minimize
or eliminate dynamic memory allocation and to ensure optimal runtime
performance on the target platform(s).


<!-- source-pdf-page: 64 -->
> Visual fallback for diagrams/images: [PDF page 64](../../../visual_pages/page_0064.jpg)

Figure 1.19. Core engine systems.

A detailed discussion of the most common core engine systems can be
found in Part II.

### 1.6.7 Resource Manager

Present in every game engine in some form, the resource manager provides
a unified interface (or suite of interfaces) for accessing any and all types of
game assets and other engine input data. Some engines do this in a highly cen-
tralized and consistent manner (e.g., Unreal’s packages, OGRE’s Resource-
Manager class). Other engines take an ad hoc approach, often leaving it up
to the game programmer to directly access raw files on disk or within com-
pressed archives such as Quake’s PAK files. A typical resource manager layer
is depicted in Figure 1.20.

Resources (Game Assets)

Material
Resource
3D Model
Resource

Texture
Resource

Font
Resource

Game
World/Map
etc.
Skeleton
Resource

Collision
Resource

Physics
Parameters

Resource Manager

Figure 1.20. Resource manager.

### 1.6.8 Rendering Engine

The rendering engine is one of the largest and most complex components of
any game engine. Renderers can be architected in many different ways. There
is no one accepted way to do it, although as we’ll see, most modern rendering
engines share some fundamental design philosophies, driven in large part by
the design of the 3D graphics hardware upon which they depend.
One common and effective approach to rendering engine design is to em-
ploy a layered architecture as follows.


<!-- source-pdf-page: 65 -->
> Visual fallback for diagrams/images: [PDF page 65](../../../visual_pages/page_0065.jpg)

1.6.8.1
Low-Level Renderer

The low-level renderer, shown in Figure 1.21, encompasses all of the raw ren-
dering facilities of the engine. At this level, the design is focused on rendering
a collection of geometric primitives as quickly and richly as possible, without
much regard for which portions of a scene may be visible. This component is
broken into various subcomponents, which are discussed below.

Skeletal Mesh

Rendering

Low-Level Renderer

Materials &

Static & Dynamic

Lighting
Cameras

Text & Fonts

Shaders

Primitive
Submission

Viewports &
Virtual Screens

Texture and
Surface Mgmt.

Debug Drawing

(Lines etc.)

Graphics Device Interface

Figure 1.21. Low-level rendering engine.

Graphics Device Interface

Graphics SDKs, such as DirectX, OpenGL or Vulkan, require a reasonable
amount of code to be written just to enumerate the available graphics devices,
initialize them, set up render surfaces (back-buffer, stencil buffer, etc.) and so
on. This is typically handled by a component that I’ll call the graphics device
interface (although every engine uses its own terminology).
For a PC game engine, you also need code to integrate your renderer with
the Windows message loop. You typically write a “message pump” that ser-
vices Windows messages when they are pending and otherwise runs your ren-
der loop over and over as fast as it can. This ties the game’s keyboard polling
loop to the renderer’s screen update loop. This coupling is undesirable, but
with some effort it is possible to minimize the dependencies. We’ll explore
this topic in more depth later.

Other Renderer Components

The other components in the low-level renderer cooperate in order to collect
submissions of geometric primitives (sometimes called render packets), such as
meshes, line lists, point lists, particles, terrain patches, text strings and what-
ever else you want to draw, and render them as quickly as possible.


<!-- source-pdf-page: 66 -->
> Visual fallback for diagrams/images: [PDF page 66](../../../visual_pages/page_0066.jpg)

k

Figure 1.22. A typical scene graph/spatial subdivision layer, for culling optimization.

The low-level renderer usually provides a viewport abstraction with an as-
sociated camera-to-world matrix and 3D projection parameters, such as field
of view and the location of the near and far clip planes. The low-level renderer
also manages the state of the graphics hardware and the game’s shaders via
its material system and its dynamic lighting system. Each submitted primitive is
associated with a material and is affected by n dynamic lights. The material
describes the texture(s) used by the primitive, what device state settings need
to be in force, and which vertex and pixel shader to use when rendering the
primitive. The lights determine how dynamic lighting calculations will be ap-
plied to the primitive. Lighting and shading is a complex topic. We’ll discuss
the fundamentals in Chapter 11, but these topics are covered in depth in many
excellent books on computer graphics, including [16], [49] and [2].

1.6.8.2
Scene Graph/Culling Optimizations

The low-level renderer draws all of the geometry submitted to it, without
much regard for whether or not that geometry is actually visible (other than
back-face culling and clipping triangles to the camera frustum). A higher-level
component is usually needed in order to limit the number of primitives sub-
mitted for rendering, based on some form of visibility determination. This
layer is shown in Figure 1.22.
For very small game worlds, a simple frustum cull (i.e., removing objects
that the camera cannot “see”) is probably all that is required. For larger game
worlds, a more advanced spatial subdivision data structure might be used to
improve rendering efficiency by allowing the potentially visible set (PVS) of
objects to be determined very quickly. Spatial subdivisions can take many
forms, including a binary space partitioning tree, a quadtree, an octree, a kd-
tree or a sphere hierarchy. A spatial subdivision is sometimes called a scene
graph, although technically the latter is a particular kind of data structure and
does not subsume the former. Portals or occlusion culling methods might also
be applied in this layer of the rendering engine.
Ideally, the low-level renderer should be completely agnostic to the type
of spatial subdivision or scene graph being used. This permits different game


<!-- source-pdf-page: 67 -->
> Visual fallback for diagrams/images: [PDF page 67](../../../visual_pages/page_0067.jpg)

Figure 1.23. Visual effects.

teams to reuse the primitive submission code but to craft a PVS determination
system that is specific to the needs of each team’s game. The design of the
OGRE open source rendering engine (http://www.ogre3d.org) is a great ex-
ample of this principle in action. OGRE provides a plug-and-play scene graph
architecture. Game developers can either select from a number of preimple-
mented scene graph designs, or they can provide a custom scene graph imple-
mentation.

1.6.8.3
Visual Effects

Modern game engines support a wide range of visual effects, as shown in Fig-
ure 1.23, including:

•
particle systems (for smoke, fire, water splashes, etc.);

•
decal systems (for bullet holes, foot prints, etc.);

•
light mapping and environment mapping;

•
dynamic shadows; and

•
full-screen post effects, applied after the 3D scene has been rendered to
an off-screen buffer.

Some examples of full-screen post effects include:

•
high dynamic range (HDR) tone mapping and bloom;

•
full-screen anti-aliasing (FSAA); and

•
color correction and color-shift effects, including bleach bypass, satura-
tion and desaturation effects, etc.

It is common for a game engine to have an effects system component that
manages the specialized rendering needs of particles, decals and other visual
effects. The particle and decal systems are usually distinct components of the
rendering engine and act as inputs to the low-level renderer. On the other


<!-- source-pdf-page: 68 -->
> Visual fallback for diagrams/images: [PDF page 68](../../../visual_pages/page_0068.jpg)

Front End

Heads-Up Display

Full-Motion Video

In-Game Cinematics

(HUD)

(FMV)

(IGC)

In-Game Menus
In-Game GUI
Wrappers / Attract

Mode

Figure 1.24. Front end graphics.

hand, light mapping, environment mapping and shadows are usually han-
dled internally within the rendering engine proper. Full-screen post effects are
either implemented as an integral part of the renderer or as a separate compo-
nent that operates on the renderer’s output buffers.

1.6.8.4
Front End

Most games employ some kind of 2D graphics overlaid on the 3D scene for
various purposes. These include:

•
the game’s heads-up display (HUD);
•
in-game menus, a console and/or other development tools, which may or
may not be shipped with the final product; and
•
possibly an in-game graphical user interface (GUI), allowing the player to
manipulate his or her character’s inventory, configure units for battle or
perform other complex in-game tasks.

This layer is shown in Figure 1.24. Two-dimensional graphics like these are
usually implemented by drawing textured quads (pairs of triangles) with an
orthographic projection. Or they may be rendered in full 3D, with the quads
bill-boarded so they always face the camera.
We’ve also included the full-motion video (FMV) system in this layer. This
system is responsible for playing full-screen movies that have been recorded
earlier (either rendered with the game’s rendering engine or using another
rendering package).
A related system is the in-game cinematics (IGC) system. This component
typically allows cinematic sequences to be choreographed within the game
itself, in full 3D. For example, as the player walks through a city, a conversation
between two key characters might be implemented as an in-game cinematic.
IGCs may or may not include the player character(s). They may be done as a
deliberate cut-away during which the player has no control, or they may be
subtly integrated into the game without the human player even realizing that


<!-- source-pdf-page: 69 -->
> Visual fallback for diagrams/images: [PDF page 69](../../../visual_pages/page_0069.jpg)

an IGC is taking place. Some games, such as Naughty Dog’s Uncharted 4: A
Thief’s End, have moved away from pre-rendered movies entirely, and display
all cinematic moments in the game as real-time IGCs.

### 1.6.9 Proﬁling and Debugging Tools

Games are real-time systems and, as such, game engineers often need to pro-
file the performance of their games in order to optimize performance. In addi-
tion, memory resources are usually scarce, so developers make heavy use of
memory analysis tools as well. The profiling and debugging layer, shown
in Figure 1.25, encompasses these tools and also includes in-game debug-
ging facilities, such as debug drawing, an in-game menu system or console
and the ability to record and play back gameplay for testing and debugging
purposes.
There are plenty of good general-purpose software profiling tools avail-
able, including:

Figure 1.25.
Proﬁling
and debugging tools.

•
Intel’s VTune,

•
IBM’s Quantify and Purify (part of the PurifyPlus tool suite),

•
Insure++ by Parasoft, and

•
Valgrind by Julian Seward and the Valgrind development team.

However, most game engines also incorporate a suite of custom profiling
and debugging tools. For example, they might include one or more of the
following:

•
a mechanism for manually instrumenting the code, so that specific sec-
tions of code can be timed;

•
a facility for displaying the profiling statistics on-screen while the game
is running;

•
a facility for dumping performance stats to a text file or to an Excel
spreadsheet;

•
a facility for determining how much memory is being used by the engine,
and by each subsystem, including various on-screen displays;

•
the ability to dump memory usage, high water mark and leakage stats
when the game terminates and/or during gameplay;

•
tools that allow debug print statements to be peppered throughout the
code, along with an ability to turn on or off different categories of debug
output and control the level of verbosity of the output; and


<!-- source-pdf-page: 70 -->
> Visual fallback for diagrams/images: [PDF page 70](../../../visual_pages/page_0070.jpg)

Figure 1.26. Collision and physics subsystem.

•
the ability to record game events and then play them back. This is tough
to get right, but when done properly it can be a very valuable tool for
tracking down bugs.

The PlayStation 4 provides a powerful core dump facility to aid program-
mers in debugging crashes. The PlayStation 4 is always recording the last 15
seconds of gameplay video, to allow players to share their experiences via the
Share button on the controller. Because of this, the PS4’s core dump facility
automatically provides programmers not only with a complete call stack of
what the program was doing when it crashed, but also with a screenshot of
the moment of the crash and 15 seconds of video footage showing what was
happening just prior to the crash. Core dumps can be automatically uploaded
to the game developer’s servers whenever the game crashes, even after the
game has shipped. These facilities revolutionize the tasks of crash analysis
and repair.

### 1.6.10 Collision and Physics

Collision detection is important for every game. Without it, objects would
interpenetrate, and it would be impossible to interact with the virtual world
in any reasonable way. Some games also include a realistic or semi-realistic
dynamics simulation. We call this the “physics system” in the game industry,
although the term rigid body dynamics is really more appropriate, because we
are usually only concerned with the motion (kinematics) of rigid bodies and
the forces and torques (dynamics) that cause this motion to occur. This layer
is depicted in Figure 1.26.


<!-- source-pdf-page: 71 -->
> Visual fallback for diagrams/images: [PDF page 71](../../../visual_pages/page_0071.jpg)

Collision and physics are usually quite tightly coupled. This is because
when collisions are detected, they are almost always resolved as part of the
physics integration and constraint satisfaction logic.
Nowadays, very few
game companies write their own collision/physics engine. Instead, a third-
party SDK is typically integrated into the engine.

•
Havok is the gold standard in the industry today. It is feature-rich and
performs well across the boards.

•
PhysX by NVIDIA is another excellent collision and dynamics engine.
It was integrated into Unreal Engine 4 and is also available for free as
a stand-alone product for PC game development. PhysX was originally
designed as the interface to Ageia’s physics accelerator chip. The SDK is
now owned and distributed by NVIDIA, and the company has adapted
PhysX to run on its latest GPUs.

Open source physics and collision engines are also available. Perhaps the
best-known of these is the Open Dynamics Engine (ODE). For more informa-
tion, see http://www.ode.org. I-Collide, V-Collide and RAPID are other pop-
ular non-commercial collision detection engines. All three were developed at
the University of North Carolina (UNC). For more information, see http://
www.cs.unc.edu/~geom/I_COLLIDE/index.html and http://www.cs.unc.
edu/∼geom/V_COLLIDE/index.html.

### 1.6.11 Animation

Any game that has organic or semi-organic characters (humans, animals, car-
toon characters or even robots) needs an animation system. There are five basic
types of animation used in games:

•
sprite/texture animation,

•
rigid body hierarchy animation,

•
skeletal animation,

•
vertex animation, and

•
morph targets.

Skeletal animation permits a detailed 3D character mesh to be posed by an
animator using a relatively simple system of bones. As the bones move, the
vertices of the 3D mesh move with them. Although morph targets and vertex
animation are used in some engines, skeletal animation is the most prevalent
animation method in games today; as such, it will be our primary focus in this
book. A typical skeletal animation system is shown in Figure 1.27.


<!-- source-pdf-page: 72 -->
> Visual fallback for diagrams/images: [PDF page 72](../../../visual_pages/page_0072.jpg)

You’ll notice in Figure 1.16 that the skeletal mesh rendering component
bridges the gap between the renderer and the animation system. There is a
tight cooperation happening here, but the interface is very well defined. The
animation system produces a pose for every bone in the skeleton, and then
these poses are passed to the rendering engine as a palette of matrices. The
renderer transforms each vertex by the matrix or matrices in the palette, in
order to generate a final blended vertex position. This process is known as
skinning.
There is also a tight coupling between the animation and physics systems
when rag dolls are employed. A rag doll is a limp (often dead) animated char-
acter, whose bodily motion is simulated by the physics system. The physics
system determines the positions and orientations of the various parts of the
body by treating them as a constrained system of rigid bodies. The animation
system calculates the palette of matrices required by the rendering engine in
order to draw the character on-screen.

### 1.6.12 Human Interface Devices (HID)

Every game needs to process input from the player, obtained from various
human interface devices (HIDs) including:

•
the keyboard and mouse,

•
a joypad, or

•
other specialized game controllers, like steering wheels, fishing rods,
dance pads, the Wiimote, etc.

We sometimes call this component the player I/O component, because

Figure
1.28.
The
player input/output
system, also known
as the human in-
terface device (HID)
layer.

Skeletal Animation

Animation State
Tree & Layers

Inverse
Kinematics (IK)

Game-Specific
Post-Processing

Sub-skeletal
Animation
LERP and
Additive Blending

Animation
Playback

Animation
Decompression

Figure 1.27. Skeletal animation subsystem.


<!-- source-pdf-page: 73 -->
> Visual fallback for diagrams/images: [PDF page 73](../../../visual_pages/page_0073.jpg)

we may also provide output to the player through the HID, such as force-
feedback/ rumble on a joypad or the audio produced by the Wiimote. A typ-
ical HID layer is shown in Figure 1.28.
The HID engine component is sometimes architected to divorce the low-
level details of the game controller(s) on a particular hardware platform from
the high-level game controls. It massages the raw data coming from the hard-
ware, introducing a dead zone around the center point of each joypad stick, de-
bouncing button-press inputs, detecting button-down and button-up events,
interpreting and smoothing accelerometer inputs (e.g., from the PlayStation
Dualshock controller) and more. It often provides a mechanism allowing the
player to customize the mapping between physical controls and logical game
functions. It sometimes also includes a system for detecting chords (multiple
buttons pressed together), sequences (buttons pressed in sequence within a
certain time limit) and gestures (sequences of inputs from the buttons, sticks,
accelerometers, etc.).

### 1.6.13 Audio

Audio is just as important as graphics in any game engine. Unfortunately, au-
dio often gets less attention than rendering, physics, animation, AI and game-
play. Case in point: Programmers often develop their code with their speak-
ers turned off! (In fact, I’ve known quite a few game programmers who didn’t
even have speakers or headphones.) Nonetheless, no great game is complete
without a stunning audio engine. The audio layer is depicted in Figure 1.29.
Audio engines vary greatly in sophistication.
Quake’s audio engine is
pretty basic, and game teams usually augment it with custom functionality
or replace it with an in-house solution. Unreal Engine 4 provides a reason-
ably robust 3D audio rendering engine (discussed in detail in [45]), although
its feature set is limited and many game teams will probably want to aug-
ment and customize it to provide advanced game-specific features. For Di-
rectX platforms (PC, Xbox 360, Xbox One), Microsoft provides an excellent
runtime audio engine called XAudio2. Electronic Arts has developed an ad-
vanced, high-powered audio engine internally called SoundR!OT. In conjunc-
tion with first-party studios like Naughty Dog, Sony Interactive Entertainment
(SIE) provides a powerful 3D audio engine called Scream, which has been used
on a number of PS3 and PS4 titles including Naughty Dog’s Uncharted 4: A
Thief’s End and The Last of Us: Remastered. However, even if a game team uses a
preexisting audio engine, every game requires a great deal of custom software
development, integration work, fine-tuning and attention to detail in order to
produce high-quality audio in the final product.

Figure 1.29.
Audio
subsystem.


<!-- source-pdf-page: 74 -->
> Visual fallback for diagrams/images: [PDF page 74](../../../visual_pages/page_0074.jpg)

### 1.6.14 Online Multiplayer/Networking

Many games permit multiple human players to play within a single virtual
world. Multiplayer games come in at least four basic flavors:

•
Single-screen multiplayer. Two or more human interface devices (joypads,
keyboards, mice, etc.) are connected to a single arcade machine, PC or
console. Multiple player characters inhabit a single virtual world, and a
single camera keeps all player characters in frame simultaneously. Ex-
amples of this style of multiplayer gaming include Smash Brothers, Lego
Star Wars and Gauntlet.
•
Split-screen multiplayer. Multiple player characters inhabit a single vir-
tual world, with multiple HIDs attached to a single game machine, but
each with its own camera, and the screen is divided into sections so that
each player can view his or her character.
•
Networked multiplayer. Multiple computers or consoles are networked
together, with each machine hosting one of the players.
•
Massively multiplayer online games (MMOG). Literally hundreds of thou-
sands of users can be playing simultaneously within a giant, persistent,
online virtual world hosted by a powerful battery of central servers.

The multiplayer networking layer is shown in Figure 1.30.
Multiplayer games are quite similar in many ways to their single-player
counterparts. However, support for multiple players can have a profound
impact on the design of certain game engine components. The game world
object model, renderer, human input device system, player control system and
animation systems are all affected. Retrofitting multiplayer features into a pre-
existing single-player engine is certainly not impossible, although it can be a
daunting task. Still, many game teams have done it successfully. That said, it
is usually better to design multiplayer features from day one, if you have that
luxury.
It is interesting to note that going the other way—converting a multiplayer
game into a single-player game—is typically trivial. In fact, many game en-
gines treat single-player mode as a special case of a multiplayer game, in which
there happens to be only one player. The Quake engine is well known for its
client-on-top-of-server mode, in which a single executable, running on a single
PC, acts both as the client and the server in single-player campaigns.

Figure 1.30.
Online
multiplayer
net-
working subsystem.

### 1.6.15 Gameplay Foundation Systems

The term gameplay refers to the action that takes place in the game, the rules
that govern the virtual world in which the game takes place, the abilities of the


<!-- source-pdf-page: 75 -->
> Visual fallback for diagrams/images: [PDF page 75](../../../visual_pages/page_0075.jpg)

Gameplay Foundations

High-Level Game Flow System/FSM

Scripting System

Streaming
Static World

System
Dynamic Game

Real-Time Agent-
Based Simulation

Event/Messaging

World Loading /

Elements

Object Model

Hierarchical
Object Attachment

Figure 1.31. Gameplay foundation systems.

player character(s) (known as player mechanics) and of the other characters and
objects in the world, and the goals and objectives of the player(s). Gameplay
is typically implemented either in the native language in which the rest of the
engine is written or in a high-level scripting language—or sometimes both. To
bridge the gap between the gameplay code and the low-level engine systems
that we’ve discussed thus far, most game engines introduce a layer that I’ll
call the gameplay foundations layer (for lack of a standardized name). Shown
in Figure 1.31, this layer provides a suite of core facilities, upon which game-
specific logic can be implemented conveniently.

1.6.15.1
Game Worlds and Object Models

The gameplay foundations layer introduces the notion of a game world, con-
taining both static and dynamic elements. The contents of the world are usu-
ally modeled in an object-oriented manner (often, but not always, using an
object-oriented programming language). In this book, the collection of object
types that make up a game is called the game object model. The game object
model provides a real-time simulation of a heterogeneous collection of objects
in the virtual game world.
Typical types of game objects include:

•
static background geometry, like buildings, roads, terrain (often a special
case), etc.;

•
dynamic rigid bodies, such as rocks, soda cans, chairs, etc.;

•
player characters (PC);

•
non-player characters (NPC);


<!-- source-pdf-page: 76 -->

•
weapons;
•
projectiles;
•
vehicles;
•
lights (which may be present in the dynamic scene at runtime, or only
used for static lighting offline);
•
cameras;

and the list goes on.
The game world model is intimately tied to a software object model, and this
model can end up pervading the entire engine. The term software object model
refers to the set of language features, policies and conventions used to imple-
ment a piece of object-oriented software. In the context of game engines, the
software object model answers questions, such as:

•
Is your game engine designed in an object-oriented manner?
•
What language will you use? C? C++? Java? OCaml?
•
How will the static class hierarchy be organized? One giant monolithic
hierarchy? Lots of loosely coupled components?
•
Will you use templates and policy-based design, or traditional polymor-
phism?
•
How are objects referenced?
Straight old pointers?
Smart pointers?
Handles?
•
How will objects be uniquely identified? By address in memory only?
By name? By a global unique identifier (GUID)?
•
How are the lifetimes of game objects managed?
•
How are the states of the game objects simulated over time?

We’ll explore software object models and game object models in consider-
able depth in Section 16.2.

1.6.15.2
Event System

Game objects invariably need to communicate with one another. This can be
accomplished in all sorts of ways. For example, the object sending the mes-
sage might simply call a member function of the receiver object. An event-
driven architecture, much like what one would find in a typical graphical user
interface, is also a common approach to inter-object communication. In an
event-driven system, the sender creates a little data structure called an event
or message, containing the message’s type and any argument data that are to
be sent. The event is passed to the receiver object by calling its event handler
function. Events can also be stored in a queue for handling at some future time.


<!-- source-pdf-page: 77 -->
> Visual fallback for diagrams/images: [PDF page 77](../../../visual_pages/page_0077.jpg)

1.6.15.3
Scripting System

Many game engines employ a scripting language in order to make devel-
opment of game-specific gameplay rules and content easier and more rapid.
Without a scripting language, you must recompile and relink your game exe-
cutable every time a change is made to the logic or data structures used in the
engine. But when a scripting language is integrated into your engine, changes
to game logic and data can be made by modifying and reloading the script
code. Some engines allow script to be reloaded while the game continues to
run. Other engines require the game to be shut down prior to script recompi-
lation. But either way, the turnaround time is still much faster than it would
be if you had to recompile and relink the game’s executable.

1.6.15.4
Artiﬁcial Intelligence Foundations

Traditionally, artificial intelligence has fallen squarely into the realm of game-
specific software—it was usually not considered part of the game engine per
se. More recently, however, game companies have recognized patterns that
arise in almost every AI system, and these foundations are slowly starting to
fall under the purview of the engine proper.
For example, a company called Kynogon developed a middleware SDK
named Kynapse, which provides much of the low-level technology required
to build commercially viable game AI. This technology was purchased by Au-
todesk and has been superseded by a totally redesigned AI middleware pack-
age called Gameware Navigation, designed by the same engineering team that
invented Kynapse. This SDK provides low-level AI building blocks such as
nav mesh generation, path finding, static and dynamic object avoidance, iden-
tification of vulnerabilities within a play space (e.g., an open window from
which an ambush could come) and a well-defined interface between AI and
animation.

### 1.6.16 Game-Speciﬁc Subsystems

On top of the gameplay foundation layer and the other low-level engine com-
ponents, gameplay programmers and designers cooperate to implement the
features of the game itself. Gameplay systems are usually numerous, highly
varied and specific to the game being developed. As shown in Figure 1.32,
these systems include, but are certainly not limited to the mechanics of the
player character, various in-game camera systems, artificial intelligence for
the control of non-player characters, weapon systems, vehicles and the list
goes on. If a clear line could be drawn between the engine and the game,


<!-- source-pdf-page: 78 -->
> Visual fallback for diagrams/images: [PDF page 78](../../../visual_pages/page_0078.jpg)

it would lie between the game-specific subsystems and the gameplay foun-
dations layer. Practically speaking, this line is never perfectly distinct. At
least some game-specific knowledge invariably seeps down through the game-
play foundations layer and sometimes even extends into the core of the engine
itself.

## 1.7 Tools and the Asset Pipeline

Any game engine must be fed a great deal of data, in the form of game assets,
configuration files, scripts and so on. Figure 1.33 depicts some of the types of
game assets typically found in modern game engines. The thicker dark-grey
arrows show how data flows from the tools used to create the original source
assets all the way through to the game engine itself. The thinner light-grey
arrows show how the various types of assets refer to or use other assets.

### 1.7.1 Digital Content Creation Tools

Games are multimedia applications by nature. A game engine’s input data
comes in a wide variety of forms, from 3D mesh data to texture bitmaps to an-
imation data to audio files. All of this source data must be created and manip-
ulated by artists. The tools that the artists use are called digital content creation
(DCC) applications.
A DCC application is usually targeted at the creation of one particular type
of data—although some tools can produce multiple data types. For example,
Autodesk’s Maya and 3ds Max and Pixologic’s ZBrush are prevalent in the
creation of both 3D meshes and animation data. Adobe’s Photoshop and its
ilk are aimed at creating and editing bitmaps (textures). SoundForge is a pop-
ular tool for creating audio clips. Some types of game data cannot be created
using an off-the-shelf DCC app. For example, most game engines provide a
custom editor for laying out game worlds. Still, some engines do make use of

GAME-SPECIFIC SUBSYSTEMS

Weapons
Power-Ups
etc.
Vehicles
Puzzles

Game-Specific Rendering

Player Mechanics

Game Cameras

AI

State Machine &

Camera-Relative

Fixed Cameras
Scripted/Animated

Goals & Decision-

Actions
(Engine Interface)

etc.

Animation

Controls (HID)

Cameras

Making

Terrain Rendering
Water Simulation

Player-Follow

Debug Fly-
Through Cam

Sight Traces &

Collision Manifold
Movement

Perception
Path Finding (A*)

& Rendering

Camera

Figure 1.32. Game-speciﬁc subsystems.


<!-- source-pdf-page: 79 -->
> Visual fallback for diagrams/images: [PDF page 79](../../../visual_pages/page_0079.jpg)

Digital Content Creation (DCC) Tools

Custom Material

Game Object
Definition Tool

Plug-In

Mesh

Maya, 3DSMAX, etc.
Maya, 3DSMAX, etc.

Mesh Exporter

Game Obj.

Template

Material

Skeletal Hierarchy

Skel.
Hierarchy

World Editor

Exporter

Animation

Animation

Animation

Animation

Exporter

Photoshop
Photoshop

Curves

Set

Tree

Game World

Animation Tree

Editor

Game
Object

Game
Object

TGA
Texture

DXT Compression
DXT
Texture

Game
Object

Game
Object

Houdini/Other Particle Tool
Houdini/Other Particle Tool

Particle
System
Particle Exporter

Audio Manager

Asset
Conditioning

Tool

Pipeline

Sound Forge or Audio Tool
Sound Forge or Audio Tool

Sound

Bank

WAV
sound

GAME

Figure 1.33. Tools and the asset pipeline.

preexisting tools for game world layout. I’ve seen game teams use 3ds Max
or Maya as a world layout tool, with or without custom plug-ins to aid the
user. Ask most game developers, and they’ll tell you they can remember a
time when they laid out terrain height fields using a simple bitmap editor,
or typed world layouts directly into a text file by hand. Tools don’t have to be
pretty—game teams will use whatever tools are available and get the job done.
That said, tools must be relatively easy to use, and they absolutely must be re-
liable, if a game team is going to be able to develop a highly polished product
in a timely manner.


<!-- source-pdf-page: 80 -->

### 1.7.2 The Asset Conditioning Pipeline

The data formats used by digital content creation (DCC) applications are rarely
suitable for direct use in-game. There are two primary reasons for this.

1.
The DCC app’s in-memory model of the data is usually much more com-
plex than what the game engine requires. For example, Maya stores a di-
rected acyclic graph (DAG) of scene nodes, with a complex web of inter-
connections. It stores a history of all the edits that have been performed
on the file. It represents the position, orientation and scale of every ob-
ject in the scene as a full hierarchy of 3D transformations, decomposed
into translation, rotation, scale and shear components. A game engine
typically only needs a tiny fraction of this information in order to render
the model in-game.

2.
The DCC application’s file format is often too slow to read at runtime,
and in some cases it is a closed proprietary format.

Therefore, the data produced by a DCC app is usually exported to a more
accessible standardized format, or a custom file format, for use in-game.
Once data has been exported from the DCC app, it often must be further
processed before being sent to the game engine. And if a game studio is ship-
ping its game on more than one platform, the intermediate files might be pro-
cessed differently for each target platform. For example, 3D mesh data might
be exported to an intermediate format, such as XML, JSON or a simple binary
format. Then it might be processed to combine meshes that use the same ma-
terial, or split up meshes that are too large for the engine to digest. The mesh
data might then be organized and packed into a memory image suitable for
loading on a specific hardware platform.
The pipeline from DCC app to game engine is sometimes called the asset
conditioning pipeline (ACP). Every game engine has this in some form.

1.7.2.1
3D Model/Mesh Data

The visible geometry you see in a game is typically constructed from triangle
meshes. Some older games also make use of volumetric geometry known as
brushes. We’ll discuss each type of geometric data briefly below. For an in-
depth discussion of the techniques used to describe and render 3D geometry,
see Chapter 11.

3D Models (Meshes)

A mesh is a complex shape composed of triangles and vertices. Renderable
geometry can also be constructed from quads or higher-order subdivision


<!-- source-pdf-page: 81 -->

surfaces.
But on today’s graphics hardware, which is almost exclusively
geared toward rendering rasterized triangles, all shapes must eventually be
translated into triangles prior to rendering.
A mesh typically has one or more materials applied to it in order to define
visual surface properties (color, reflectivity, bumpiness, diffuse texture, etc.).
In this book, I will use the term “mesh” to refer to a single renderable shape,
and “model” to refer to a composite object that may contain multiple meshes,
plus animation data and other metadata for use by the game.
Meshes are typically created in a 3D modeling package such as 3ds Max,
Maya or SoftImage. A powerful and popular tool by Pixologic called ZBrush
allows ultra high-resolution meshes to be built in a very intuitive way and then
down-converted into a lower-resolution model with normal maps to approx-
imate the high-frequency detail.
Exporters must be written to extract the data from the digital content cre-
ation (DCC) tool (Maya, Max, etc.) and store it on disk in a form that is di-
gestible by the engine. The DCC apps provide a host of standard or semi-
standard export formats, although none are perfectly suited for game devel-
opment (with the possible exception of COLLADA). Therefore, game teams
often create custom file formats and custom exporters to go with them.

Brush Geometry

Brush geometry is defined as a collection of convex hulls, each of which is
defined by multiple planes. Brushes are typically created and edited directly in
the game world editor. This is essentially an “old school” approach to creating
renderable geometry, but it is still used in some engines.
Pros:

•
fast and easy to create;

•
accessible to game designers—often used to “block out” a game level for
prototyping purposes;

•
can serve both as collision volumes and as renderable geometry.

Cons:

•
low-resolution;

•
difficult to create complex shapes;

•
cannot support articulated objects or animated characters.

1.7.2.2
Skeletal Animation Data

A skeletal mesh is a special kind of mesh that is bound to a skeletal hierarchy
for the purposes of articulated animation. Such a mesh is sometimes called a


<!-- source-pdf-page: 82 -->

skin because it forms the skin that surrounds the invisible underlying skeleton.
Each vertex of a skeletal mesh contains a list of indices indicating to which
joint(s) in the skeleton it is bound. A vertex usually also includes a set of joint
weights, specifying the amount of influence each joint has on the vertex.
In order to render a skeletal mesh, the game engine requires three distinct
kinds of data:

1.
the mesh itself,

2.
the skeletal hierarchy (joint names, parent-child relationships and the
base pose the skeleton was in when it was originally bound to the mesh),
and

3.
one or more animation clips, which specify how the joints should move
over time.

The mesh and skeleton are often exported from the DCC application as a
single data file. However, if multiple meshes are bound to a single skeleton,
then it is better to export the skeleton as a distinct file. The animations are
usually exported individually, allowing only those animations which are in
use to be loaded into memory at any given time. However, some game engines
allow a bank of animations to be exported as a single file, and some even lump
the mesh, skeleton and animations into one monolithic file.
An unoptimized skeletal animation is defined by a stream of 4 × 3 matrix
samples, taken at a frequency of at least 30 frames per second, for each of the
joints in a skeleton (of which there can be 500 or more for a realistic humanoid
character). Thus, animation data is inherently memory-intensive. For this rea-
son, animation data is almost always stored in a highly compressed format.
Compression schemes vary from engine to engine, and some are proprietary.
There is no one standardized format for game-ready animation data.

1.7.2.3
Audio Data

Audio clips are usually exported from Sound Forge or some other audio pro-
duction tool in a variety of formats and at a number of different data sam-
pling rates. Audio files may be in mono, stereo, 5.1, 7.1 or other multi-channel
configurations. Wave files (.wav) are common, but other file formats such as
PlayStation ADPCM files (.vag) are also commonplace. Audio clips are often
organized into banks for the purposes of organization, easy loading into the
engine, and streaming.


<!-- source-pdf-page: 83 -->

1.7.2.4
Particle Systems Data

Modern games make use of complex particle effects. These are authored by
artists who specialize in the creation of visual effects. Third-party tools, such
as Houdini, permit film-quality effects to be authored; however, most game
engines are not capable of rendering the full gamut of effects that can be cre-
ated with Houdini. For this reason, many game companies create a custom
particle effect editing tool, which exposes only the effects that the engine ac-
tually supports. A custom tool might also let the artist see the effect exactly as
it will appear in-game.

### 1.7.3 The World Editor

The game world is where everything in a game engine comes together. To my
knowledge, there are no commercially available game world editors (i.e., the
game world equivalent of Maya or Max). However, a number of commercially
available game engines provide good world editors:

•
Some variant of the Radiant game editor is used by most game engines
based on Quake technology.
•
The Half-Life 2 Source engine provides a world editor called Hammer.
•
UnrealEd is the Unreal Engine’s world editor. This powerful tool also
serves as the asset manager for all data types that the engine can con-
sume.
•
Sandbox is the world editor in CRYENGINE.

Writing a good world editor is difficult, but it is an extremely important
part of any good game engine.

### 1.7.4 The Resource Database

Game engines deal with a wide range of asset types, from renderable geometry
to materials and textures to animation data to audio. These assets are defined
in part by the raw data produced by the artists when they use a tool like Maya,
Photoshop or SoundForge. However, every asset also carries with it a great
deal of metadata. For example, when an animator authors an animation clip in
Maya, the metadata provides the asset conditioning pipeline, and ultimately
the game engine, with the following information:

•
A unique id that identifies the animation clip at runtime.
•
The name and directory path of the source Maya (.ma or .mb) file.
•
The frame range—on which frame the animation begins and ends.
•
Whether or not the animation is intended to loop.


<!-- source-pdf-page: 84 -->
> Visual fallback for diagrams/images: [PDF page 84](../../../visual_pages/page_0084.jpg)

Run-Time Engine

Core Systems

Tools and World Builder

Platform Independence Layer

3rd Party SDKs

OS

Drivers

Hardware (PC, XBOX360, PS3, etc.)

Figure 1.34. Stand-alone tools architecture.

•
The animator’s choice of compression technique and level. (Some assets
can be highly compressed without noticeably degrading their quality,
while others require less or no compression in order to look right in-
game.)

Every game engine requires some kind of database to manage all of the
metadata associated with the game’s assets. This database might be imple-
mented using an honest-to-goodness relational database such as MySQL or
Oracle, or it might be implemented as a collection of text files, managed by
a revision control system such as Subversion, Perforce or Git. We’ll call this
metadata the resource database in this book.
No matter in what format the resource database is stored and managed,
some kind of user interface must be provided to allow users to author and
edit the data. At Naughty Dog, we wrote a custom GUI in C# called Builder
for this purpose. For more information on Builder and a few other resource
database user interfaces, see Section 7.2.1.3.

### 1.7.5 Some Approaches to Tool Architecture

A game engine’s tool suite may be architected in any number of ways. Some
tools might be stand-alone pieces of software, as shown in Figure 1.34. Some
tools may be built on top of some of the lower layers used by the runtime
engine, as Figure 1.35 illustrates. Some tools might be built into the game itself.


<!-- source-pdf-page: 85 -->
> Visual fallback for diagrams/images: [PDF page 85](../../../visual_pages/page_0085.jpg)

Run-Time Engine
Tools and World Builder

Core Systems

Platform Independence Layer

3rd Party SDKs

OS

Drivers

Hardware (PC, XBOX360, PS3, etc.)

Figure 1.35. Tools built on a framework shared with the game.

For example, Quake- and Unreal-based games both boast an in-game console
that permits developers and “modders” to type debugging and configuration
commands while running the game. Finally, web-based user interfaces are
becoming more and more popular for certain kinds of tools.

World Builder

Run-Time Engine

Core Systems

Other Tools

Platform Independence Layer

3rd Party SDKs

OS

Drivers

Hardware (PC, XBOX360, PS3, etc.)

Figure 1.36. Unreal Engine’s tool architecture.


<!-- source-pdf-page: 86 -->
> Visual fallback for diagrams/images: [PDF page 86](../../../visual_pages/page_0086.jpg)

As an interesting and unique example, Unreal’s world editor and asset
manager, UnrealEd, is built right into the runtime game engine. To run the
editor, you run your game with a command-line argument of “editor.” This
unique architectural style is depicted in Figure 1.36. It permits the tools to
have total access to the full range of data structures used by the engine and
avoids a common problem of having to have two representations of every data
structure—one for the runtime engine and one for the tools. It also means that
running the game from within the editor is very fast (because the game is ac-
tually already running). Live in-game editing, a feature that is normally very
tricky to implement, can be developed relatively easily when the editor is a
part of the game. However, an in-engine editor design like this does have its
share of problems. For example, when the engine is crashing, the tools become
unusable as well. Hence a tight coupling between engine and asset creation
tools can tend to slow down production.

1.7.5.1
Web-Based User Interfaces

Web-based user interfaces are quickly becoming the norm for certain kinds of
game development tools. At Naughty Dog, we use a number of web-based
UIs. Naughty Dog’s localization tool serves as the front-end portal into our
localization database. Tasker is the web-based interface used by all Naughty
Dog employees to create, manage, schedule, communicate and collaborate on
game development tasks during production. A web-based interface known
as Connector also serves as our window into the various streams of debugging
information that are emitted by the game engine at runtime. The game spits
out its debug text into various named channels, each associated with a differ-
ent engine system (animation, rendering, AI, sound, etc.) These data streams
are collected by a lightweight Redis database. The browser-based Connec-
tor interface allows users to view and filter this information in a convenient
way.
Web-based UIs offer a number of advantages over stand-alone GUI appli-
cations. For one thing, web apps are typically easier and faster to develop
and maintain than a stand-alone app written in a language like Java, C# or
C++. Web apps require no special installation—all the user needs is a com-
patible web browser. Updates to a web-based interface can be pushed out to
the users without the need for an installation step—they need only refresh or
restart their browser to receive the update. Web interfaces also force us to de-
sign our tools using a client-server architecture. This opens up the possibility
of distributing our tools to a wider audience. For example, Naughty Dog’s
localization tool is available directly to outsourcing partners around the globe


<!-- source-pdf-page: 87 -->

who provide language translation services to us. Stand-alone tools still have
their place of course, especially when specialized GUIs such as 3D visualiza-
tion are required. But if your tool only needs to present the user with editable
forms and tabular data, a web-based tool may be your best bet.
