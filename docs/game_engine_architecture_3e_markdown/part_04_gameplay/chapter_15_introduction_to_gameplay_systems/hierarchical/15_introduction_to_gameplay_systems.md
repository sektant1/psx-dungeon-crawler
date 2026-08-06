# 15 Introduction to Gameplay Systems

> Source PDF pages: 1034-1057
> Extraction mode: PyMuPDF text blocks; line breaks and printed hyphenation are preserved.

<!-- source-pdf-page: 1034 -->

U
p until now, everything we’ve talked about in this book has focused on
technology. We’ve learned that a game engine is a complex, layered soft-
ware system built on top of the hardware, drivers and operating system of the
target machine. We’ve seen how low-level engine systems provide services
that are required by the rest of the engine; how human interface devices such
as joypads, keyboards, mice and other devices can allow a human player to
provide inputs to the engine; how the rendering engine produces 3D images
on-screen; how the animation system allows characters and objects to move
naturally; how the collision system detects and resolves interpenetrations be-
tween shapes; how the physics simulation causes objects to move in physically
realistic ways; how the 3D audio engine renders a believable and immersive
soundscape for our game world. But despite the wide range of powerful fea-
tures provided by these components, if we were to put them all together, we
still wouldn’t have a game!
A game is defined not by its technology but by its gameplay. Gameplay
can be defined as the overall experience of playing a game. The term game
mechanics pins down this idea a bit more concretely—it is usually defined as
the set of rules that govern the interactions between the various entities in the
game. It also defines the objectives of the player(s), criteria for success and fail-
ure, the player character’s abilities, the number and types of non-player entities


<!-- source-pdf-page: 1035 -->
> Visual fallback for diagrams/images: [PDF page 1035](../../../visual_pages/page_1035.jpg)

that exist within the game’s virtual world and the overall flow of the gaming
experience as a whole. In many games, these elements are intertwined with a
compelling story and a rich cast of characters. However, story and characters
are definitely not a necessary part of every video game, as evidenced by wildly
successful puzzle games like Tetris. In their paper, “A Survey of ‘Game’ Porta-
bility” (http://www.dcs.shef.ac.uk/intranet/research/resmes/CS0705.pdf),
Ahmed BinSubaih, Steve Maddock and Daniela Romano of the University of
Sheffield refer to the collection of software systems used to implement game-
play as a game’s G-factor. In the next three chapters, we’ll explore the crucial
tools and engine systems that define and manage the game mechanics (a.k.a.
gameplay, a.k.a. G-factor) of a game.

## 15.1 Anatomy of a Game World

Gameplay designs vary widely from genre to genre and from game to game.
That said, most 3D games, and a good number of 2D games as well, conform
more or less to a few basic structural patterns. We’ll discuss these patterns
in the following sections, but please keep in mind that there are bound to be
games out there that do not fit neatly into this mold.

### 15.1.1 World Elements

Most video games take place in a two- or three-dimensional virtual game world.
This world is typically comprised of numerous discrete elements. Generally,
these elements fall into two categories: static elements and dynamic elements.
Static elements include terrain, buildings, roads, bridges and pretty much any-
thing that doesn’t move or interact with gameplay in an active way.
Dy-
namic elements include characters, vehicles, weaponry, floating power-ups
and health packs, collectible objects, particle emitters, dynamic lights, invisi-
ble regions used to detect important events in the game, splines that define the
paths of objects and so on. This breakdown of the game world is illustrated in
Figure 15.1.
Gameplay is generally concentrated within the dynamic elements of a
game. Clearly, the layout of the static background plays a crucial role in how
the game plays out. For example, a cover-based shooter wouldn’t be very
much fun if it were played in a big, empty, rectangular room. However, the
software systems that implement gameplay are primarily concerned with up-
dating the locations, orientations and internal states of the dynamic elements,
since they are the elements that change over time. The term game state refers
to the current state of all dynamic game world elements taken as a whole.


<!-- source-pdf-page: 1036 -->
> Visual fallback for diagrams/images: [PDF page 1036](../../../visual_pages/page_1036.jpg)

Figure 15.1. A game world from Uncharted: The Lost Legacy (© 2017/™SIE. Created and developed
by Naughty Dog, PlayStation 4) showing static and dynamic elements.

The ratio of dynamic to static elements also varies from game to game.
Most 3D games consist of a relatively small number of dynamic elements mov-
ing about within a relatively large static background area. Other games, like
the arcade classic Asteroids or the Xbox 360 retro hit Geometry Wars, have no
static elements to speak of (other than a black screen). The dynamic elements
of a game are usually more expensive than the static elements in terms of CPU
resources, so most 3D games are constrained to a limited number of dynamic
elements. However, the higher the ratio of dynamic to static elements, the
more “alive” the game world can seem to the player. As gaming hardware
becomes more and more powerful, games are achieving higher and higher
dynamic-to-static ratios.
It’s important to note that the distinction between the dynamic and static el-
ements in a game world is often a bit blurry. For example, in the arcade game
Hydro Thunder, the waterfalls were dynamic in the sense that their textures
were animated, they had dynamic mist effects at their bases, and they could be
placed into the game world and positioned by a game designer independently
of the terrain and water surface. However, from an engineering standpoint,
waterfalls were treated as static elements because they did not interact with the


<!-- source-pdf-page: 1037 -->

boats in the race in any way (other than to obscure the player’s view of hid-
den boost power-ups and secret passageways). Different game engines draw
different lines between static and dynamic elements, and some don’t draw a
distinction at all (i.e., everything is potentially a dynamic element).
The distinction between static and dynamic serves primarily as an opti-
mization tool—we can do less work when we know that the state of an object
isn’t going to change. For example, when we know a mesh is static and will
never move, its lighting can be precomputed in the form of static vertex light-
ing, light maps, shadow maps, static ambient occlusion information or pre-
computed radiance transfer (PRT) spherical harmonics coefficients. Virtually
any computation that must be done at runtime for a dynamic world element
is a good candidate for precomputation or omission when applied to a static
element.
Games with destructible environments are an example of how the line be-
tween the static and dynamic elements in a game world can blur. For instance,
we might define three versions of every static element—an undamaged ver-
sion, a damaged version, and a fully destroyed version. These background ele-
ments act like static world elements most of the time, but they can be swapped
dynamically during an explosion to produce the illusion of becoming dam-
aged. In reality, static and dynamic world elements are just two extremes
along a gamut of possible optimizations. Where we draw the line between
the two categories (if we draw one at all) shifts as our optimization method-
ologies change and adapt to the needs of the game design.

15.1.1.1
Static Geometry

The geometry of a static world element is often defined in a tool like Maya.
It might be one giant triangle mesh, or it might be broken up into discrete
pieces. The static portions of the scene are sometimes built out of instanced ge-
ometry. Instancing is a memory conservation technique in which a relatively
small number of unique triangle meshes are rendered multiple times through-
out the game world, at different locations and orientations, in order to provide
the illusion of variety. For example, a 3D modeler might create five different
kinds of short wall sections and then piece them together in random combina-
tions in order to construct miles of unique-looking walls.
Static visual elements and collision data might also be constructed from
brush geometry. This kind of geometry originated with the Quake family of
engines. A brush describes a shape as a collection of convex volumes, each
bounded by a set of planes. Brush geometry is fast and easy to create and in-
tegrates well into a BSP-tree-based rendering engine. Brushes can be really
useful for rapidly blocking out the contents of a game world. This allows


<!-- source-pdf-page: 1038 -->
> Visual fallback for diagrams/images: [PDF page 1038](../../../visual_pages/page_1038.jpg)

gameplay to be tested early, when it is cheap to do so. If the layout proves
its worth, the art team can either texture map and fine-tune the brush geome-
try or replace it with more-detailed custom mesh assets. On the other hand, if
the level requires redesign, the brush geometry can be easily revised without
creating a lot of extra work for the art team.

### 15.1.2 World Chunks

When a game takes place in a very large virtual world, it is typically divided
into discrete playable regions, which we’ll call world chunks. Chunks are also
known as levels, maps, stages or areas. The player can usually see only a hand-
ful of chunks at any given moment while playing the game, and he or she
progresses from chunk to chunk as the game unfolds.
Originally, the concept of “levels” was invented as a mechanism to pro-
vide greater variety of gameplay within the memory limitations of early gam-
ing hardware. Only one level could exist in memory at a time, but the player
could progress from level to level for a much richer overall experience. Since
then, game designs have branched out in many directions, and linear level-
based games are much less common today. Some games are essentially still
linear, but the delineations between world chunks are usually not as obvious
to the player as they once were. Other games use a star topology, in which the
player starts in a central hub area and can access other areas at random from
the hub (perhaps only after they have been unlocked). Others use a graph-
like topology, where areas are connected to one another in arbitrary ways.
Still others provide the illusion of a vast, open world, and use level-of-detail
(LOD) techniques to reduce memory overhead and improve performance.
Despite the richness of modern game designs, all but the smallest of game
worlds are still divided into chunks of some kind. This is done for a number of
reasons. First of all, memory limitations are still an important constraint (and
will be until game machines with infinite memory hit the market!). World
chunks are also a convenient mechanism for controlling the overall flow of the
game. Chunks can serve as a division-of-labor mechanism as well; each chunk
can be constructed and managed by a relatively small group of designers and
artists. World chunks are illustrated in Figure 15.2.

### 15.1.3 High-Level Game Flow

A game’s high-level flow defines a sequence, tree or graph of player objectives.
Objectives are sometimes called tasks, stages, levels (a term that can also apply
to world chunks) or waves (if the game is primarily about defeating hordes of
attacking enemies). The high-level flow also provides the definition of success
for each objective (e.g., clear all the enemies and get the key) and the penalty


<!-- source-pdf-page: 1039 -->
> Visual fallback for diagrams/images: [PDF page 1039](../../../visual_pages/page_1039.jpg)

Figure 15.2. Many game worlds are divided into chunks for various reasons, including memory
limitations, the need to control the ﬂow of the game through the world, and as a division-of-
labor mechanism during development.

for failure (e.g., go back to the start of the current area, possibly losing a “life”
in the process). In a story-driven game, this flow might also include various
in-game movies that serve to advance the player’s understanding of the story
as it unfolds. These sequences are sometimes called cut-scenes, in-game cine-
matics (IGC) or noninteractive sequences (NIS). When they are rendered offline
and played back as a full-screen movie, such sequences are usually called full-
motion videos (FMV).
Early games mapped the objectives of the player one-to-one to particular
world chunks (hence the dual meaning of the term “level”). For example, in
Donkey Kong, each new level presents Mario with a new objective (namely, to
reach the top of the structure and progress to the next level). However, this
one-to-one mapping between world chunks and objectives is less popular in
modern game design. Each objective is associated with one or more world
chunks, but the coupling between chunks and objectives remains deliberately
loose. This kind of design offers the flexibility to alter game objectives and
world subdivision independently, which is extremely helpful from a logistic
and practical standpoint when developing a game. Many games group their
objectives into coarser sections of gameplay, often called chapters or acts. A
typical gameplay architecture is shown in Figure 15.3.


<!-- source-pdf-page: 1040 -->
> Visual fallback for diagrams/images: [PDF page 1040](../../../visual_pages/page_1040.jpg)

Chapter 1

Chapter 2

Objective 1A

Objective 2A

Chunk 4

Chunk 1

Objective 2B

Objective 1B

Objective 2C

Chunk 5

Objective 1C

Objective 2D

Optional
Objective 1D

Chunk 2

Optional
Objective 2E

Objective 1E

Optional
Objective 2F

Chunk 6

Objective 2G

Optoinal
Objective 1F

Chunk 3

Optoinal
Objective 2H

Chunk 7

Objective 1G

Objective 2I

Figure 15.3. Gameplay objectives are typically arranged in a sequence, a tree, or a generalized graph, and each one maps to
one or more game world chunks.

## 15.2 Implementing Dynamic Elements: Game Objects

The dynamic elements of a game are usually designed in an object-oriented
fashion. This approach is intuitive and natural and maps well to the game
designer’s notion of how the world is constructed. He or she can visualize
characters, vehicles, floating health packs, exploding barrels and myriad other
dynamic objects moving about in the game. So it is only natural to want to be
able to create and manipulate these elements in the game world editor. Like-
wise, programmers usually find it natural to implement dynamic elements as
largely autonomous agents at runtime. In this book, we’ll use the term game ob-
ject (GO) to refer to virtually any dynamic element within a game world. How-
ever, this terminology is by no means standard within the industry. Game ob-
jects are commonly referred to as entities, actors or agents, and the list of terms
goes on.


<!-- source-pdf-page: 1041 -->

As is customary in object-oriented design, a game object is essentially a col-
lection of attributes (the current state of the object) and behaviors (how the state
changes over time and in response to events). Game objects are usually clas-
sified by type. Different types of objects have different attribute schemas and
different behaviors. All instances of a particular type share the same attribute
schema and the same set of behaviors, but the values of the attributes differ
from instance to instance. (Note that if a game object’s behavior is data-driven,
say through script code or via a set of data-driven rules governing the object’s
responses to events, then behavior too can vary on an instance-by-instance
basis.)
The distinction between a type and an instance of a type is a crucial one. For
example, the game of Pac-Man involves four game object types: ghosts, pellets,
power pills and Pac-Man. However, at any moment in time, there may be up
to four instances of the type “ghost,” 50–100 instances of the type “pellet,” four
“power pill” instances and one instance of the “Pac-Man” type.
Most object-oriented systems provide some mechanism for the inheritance
of attributes, behavior or both. Inheritance encourages code and design reuse.
The specifics of how inheritance works vary widely from game to game, but
most game engines support it in some form.

### 15.2.1 Game Object Models

In computer science, the term object model has two related but distinct mean-
ings. It can refer to the set of features provided by a particular programming
language or formal design language. For example, we might speak of the
C++ object model or the OMT object model. It can also refer to a specific object-
oriented programming interface (i.e., a collection of classes, methods and in-
terrelationships designed to solve a particular problem). One example of this
latter usage is the Microsoft Excel object model, which allows external programs
to control Excel in various ways. (See http://en.wikipedia.org/wiki/Object_
model for further discussion of the term object model.)
In this book, we will use the term game object model to describe the facili-
ties provided by a game engine in order to permit the dynamic entities in the
virtual game world to be modeled and simulated. In this sense, the term game
object model has aspects of both of the definitions given above:

•
A game’s object model is a specific object-oriented programming inter-
face intended to solve the particular problem of simulating the specific
set of entities that make up a particular game.
•
Additionally, a game’s object model often extends the programming lan-
guage in which the engine was written. If the game is implemented


<!-- source-pdf-page: 1042 -->

in a non-object-oriented language like C, object-oriented facilities can
be added by the programmers. And even if the game is written in an
object-oriented language like C++, advanced features like reflection, per-
sistence and network replication are often added. A game object model
sometimes melds the features of multiple languages. For example, a
game engine might combine a compiled programming language such as
C or C++ with a scripting language like Python, Lua or Pawn and pro-
vide a unified object model that can be accessed from either language.

### 15.2.2 Tool-Side Design versus Runtime Design

The object model presented to the designers via the world editor (discussed in
Section 15.4) needn’t be the same object model used to implement the game at
runtime.

•
The tool-side game object model might be implemented at runtime using
a language with no native object-oriented features at all, like C.

•
A single GO type on the tool side might be implemented as a collection
of classes at runtime (rather than as a single class as one might at first
expect).

•
Each tool-side GO might be nothing more than a unique id at runtime,
with all of its state data stored in tables or collections of loosely coupled
objects.

Therefore, a game really has two distinct but closely interrelated object models:

•
The tool-side object model is defined by the set of game object types seen by
the designers within the world editor.

•
The runtime object model is defined by whatever set of language constructs
and software systems the programmers have used to implement the tool-
side object model at runtime. The runtime object model might be iden-
tical to the tool-side model or map directly to it, or it might be entirely
different than the tool-side model under the hood.

In some game engines, the line between the tool-side and runtime designs
is blurred or nonexistent. In others, it is very well delineated. In some engines,
the implementation is actually shared between the tools and the runtime. In
others, the runtime implementation looks almost totally alien relative to the
tool-side view of things. Some aspects of the implementation almost always
creep up into the tool-side design, and game designers must be cognizant of
the performance- and memory-consumption impacts of the game worlds they


<!-- source-pdf-page: 1043 -->

construct and the gameplay rules and object behaviors they design. That said,
virtually all game engines have some form of tool-side object model and a
corresponding runtime implementation of that object model.

## 15.3 Data-Driven Game Engines

In the early days of game development, games were largely hard-coded by
programmers. Tools, if any, were primitive. This worked because the amount
of content in a typical game was miniscule, and the bar wasn’t particularly
high, thanks in part to the primitive graphics and sound of which early game
hardware was capable.
Today, games are orders of magnitude more complex, and the quality bar
is so high that game content is often compared to the computer-generated ef-
fects in Hollywood blockbusters. Game teams have grown much larger, but
the amount of game content is growing faster than team size. In the eighth
generation of consoles, defined by the Xbox One and the PlayStation 4, game
teams routinely speak of the need to produce ten times the content with teams
that are not that much larger than in the previous generation. This trend means
that a game team must be capable of producing very large amounts of content
in an extremely efficient manner.
Engineering resources are often a production bottleneck because high-
quality engineering talent is limited and expensive and because engineers tend
to produce content much more slowly than artists and game designers (due to
the complexities inherent in computer programming). Most teams now be-
lieve that it’s a good idea to put at least some of the power to create content
directly into the hands of the folks responsible for producing that content—
namely the designers and the artists. When the behavior of a game can be
controlled, in whole or in part, by data provided by artists and designers rather
than exclusively by software produced by programmers, we say the engine is
data driven.
Data-driven architectures can improve team efficiency by fully leveraging
all staff members to their fullest potential and by taking some of the heat off
the engineering team. It can also lead to improved iteration times. Whether a
developer wants to make a slight tweak to the game’s content or completely
revise an entire level, a data-driven design allows the developer to see the
effects of the changes quickly, ideally with little or no help from an engineer.
This saves valuable time and can permit the team to polish their game to a
very high level of quality.
That being said, it’s important to realize that data-driven features often
come at a heavy cost. Tools must be provided to allow game designers and


<!-- source-pdf-page: 1044 -->
> Visual fallback for diagrams/images: [PDF page 1044](../../../visual_pages/page_1044.jpg)

artists to define game content in a data-driven manner. The runtime code must
be changed to handle the wide range of possible inputs in a robust way. Tools
must also be provided in-game to allow artists and designers to preview their
work and troubleshoot problems. All of this software requires significant time
and effort to write, test and maintain.
Sadly, many teams make a mad rush into data-driven architectures without
stopping to study the impacts of their efforts on their particular game design
and the specific needs of their team members. In their haste, such teams often
dramatically overshoot the mark, producing overly complex tools and engine
systems that are difficult to use, bug-ridden and virtually impossible to adapt
to the changing requirements of the project. Ironically, in their efforts to realize
the benefits of a data-driven design, a team can easily end up with significantly
lower productivity than the old-fashioned hard-coded methods.
Every game engine should have some data-driven components, but a game
team must exercise extreme care when selecting which aspects of the engine
to data-drive. It’s crucial to weigh the costs of creating a data-driven or rapid-
iteration feature against the amount of time the feature is expected to save
the team over the course of the project. It’s also incredibly important to keep
the KISS mantra (“keep it simple, stupid”) in mind when designing and imple-
menting data-driven tools and engine systems. To paraphrase Albert Einstein,
everything in a game engine should be made as simple as possible, but no sim-
pler.

## 15.4 The Game World Editor

We’ve already discussed data-driven asset-creation tools, such as Maya, Pho-
toshop, Havok content tools and so on. These tools generate individual assets
for consumption by the rendering engine, animation system, audio system,
physics system and so on. The analog to these tools in the gameplay space
is the game world editor—a tool (or a suite of tools) that permits game world
chunks to be defined and populated with static and dynamic elements.
All commercial game engines have some kind of world editor tool.

•
A well-known tool called Radiant is used to create maps for the Quake and
Doom family of engines. A screenshot of Radiant is shown in Figure 15.4.

•
Valve’s Source engine, the engine that drives Half-Life 2, The Orange Box,
Team Fortress 2, the Portal series, the Left 4 Dead series and Titanfall, pro-
vides an editor called Hammer (previously distributed under the names
Worldcraft and The Forge). Figure 15.5 shows a screenshot of Hammer.


<!-- source-pdf-page: 1045 -->
> Visual fallback for diagrams/images: [PDF page 1045](../../../visual_pages/page_1045.jpg)

Figure 15.4. The Radiant world editor for the Quake and Doom family of engines.

•
Crytek’s CRYENGINE provides a powerful suite of world creation and
editing tools. These tools support real-time editing of multiplatform
game environments simultaneously, both in 2D and true stereoscopic
3D. Crytek’s Sandbox editor is depicted in Figure 15.6.

The game world editor generally permits the initial states of game objects
(i.e., the values of their attributes) to be specified. Most game world editors
also give their users some sort of ability to control the behaviors of the dynamic
objects in the game world. This control might be via data-driven configuration
parameters (e.g., object A should start in an invisible state, object B should im-
mediately attack the player when spawned, object C is flammable, etc.), or be-
havioral control might be via a scripting language, thereby shifting the game
designers’ tasks into the realm of programming. Some world editors even
allow entirely new types of game objects to be defined, with little or no pro-
grammer intervention.


<!-- source-pdf-page: 1046 -->
> Visual fallback for diagrams/images: [PDF page 1046](../../../visual_pages/page_1046.jpg)

Figure 15.5. Valve’s Hammer editor for the Source engine.


<!-- source-pdf-page: 1047 -->
> Visual fallback for diagrams/images: [PDF page 1047](../../../visual_pages/page_1047.jpg)

Figure 15.6. The Sandbox editor for CRYENGINE. (See Color Plate XXV.)


<!-- source-pdf-page: 1048 -->

### 15.4.1 Typical Features of a Game World Editor

The design and layout of game world editors vary widely, but most editors
provide a reasonably standard set of features. These include, but are certainly
not limited to, the following.

15.4.1.1
World Chunk Creation and Management

The unit of world creation is usually a chunk (also known as a level or map—
see Section 15.1.2). The game world editor typically allows new chunks to be
created and existing chunks to be renamed, broken up, combined or destroyed.
Each chunk can be linked to one or more static meshes and/or other static
data elements such as AI navigation maps, descriptions of ledges that can be
grabbed by the player, cover point definitions and so on. In some engines, a
chunk is defined by a single background mesh and cannot exist without one.
In other engines, a chunk may have an independent existence, perhaps defined
by a bounding volume (e.g., AABB, OBB or arbitrary polygonal region), and
can be populated by zero or more meshes and/or brush geometry (see Section
1.7.2.1).
Some world editors provide dedicated tools for authoring terrain, water
and other specialized static elements. In other engines, these elements might
be authored using standard DCC applications but tagged in some way to in-
dicate to the asset conditioning pipeline and/or the runtime engine that they
are special. (For example, in the Uncharted and The Last of Us series, water was
authored as a triangle mesh, but it was mapped with a special material that in-
dicated that it was to be treated as water.) Sometimes, special world elements
are created and edited in a separate, stand-alone tool. For example, the height
field terrain in Medal of Honor: Pacific Assault was authored using a customized
version of a tool obtained from another team within Electronic Arts because
this was more expedient than trying to integrate a terrain editor into Radiant,
the world editor being used on the project at the time.

15.4.1.2
Game World Visualization

It’s important for the user of a game world editor to be able to visualize the
contents of the game world. As such, virtually all game world editors provide
a three-dimensional perspective view of the world and/or a two-dimensional
orthographic projection. It’s common to see the view pane divided into four
sections, three for top, side and front orthographic elevations and one for the
3D perspective view.
Some editors provide these world views via a custom rendering engine in-
tegrated directly into the tool. Other editors are themselves integrated into


<!-- source-pdf-page: 1049 -->

a 3D geometry editor like Maya or 3ds Max, so they can simply leverage the
tool’s viewports. Still other editors are designed to communicate with the ac-
tual game engine and use it to render the 3D perspective view. Some editors
are even integrated into the engine itself.

15.4.1.3
Navigation

Clearly, a world editor wouldn’t be of much use if the user weren’t able to
move around within the game world. In an orthographic view, it’s important
to be able to scroll and zoom in and out. In a 3D view, various camera control
schemes are used. It may be possible to focus on an individual object and
rotate around it. It may also be possible to switch into a “fly through” mode
where the camera rotates about its own focal point and can be moved forward,
backward, up and down and panned left and right.
Some editors provide a host of convenience features for navigation. These
include the ability to select an object and focus in on it with a single key press,
the ability to save various relevant camera locations and then jump between
them, various camera movement speed modes for coarse navigation and fine
camera control, a Web-browser-like navigation history that can be used to
jump around the game world and so on.

15.4.1.4
Selection

A game world editor is primarily designed to allow the user to populate a
game world with static and dynamic elements. As such, it’s important for the
user to be able to select individual elements for editing. Some editors only
allow a single object to be selected at a time, while more-advanced editors
permit multiobject selections. Objects might be selected via a rubber-band box
in the orthographic view or by ray cast style picking in the 3D view. Many
editors also display a list of all world elements in a scrolling list or tree view
so that objects can be found and selected by name. Some world editors also
allow selections to be named and saved for later retrieval.
Game worlds are often quite densely populated. As such, it can sometimes
be difficult to select a desired object because other objects are in the way. This
problem can be overcome in a number of ways. When using a ray cast to select
objects in 3D, the editor might allow the user to cycle through all of the objects
that the ray is currently intersecting rather than always selecting the nearest
one. Many editors allow the currently selected object(s) to be temporarily hid-
den from view. That way, if you don’t get the object you want the first time,
you can always hide it and try again. As we’ll see in the next section, layers
can also be an effective way to reduce clutter and improve the user’s ability to
select objects successfully.


<!-- source-pdf-page: 1050 -->
> Visual fallback for diagrams/images: [PDF page 1050](../../../visual_pages/page_1050.jpg)

15.4.1.5
Layers

Some editors also allow objects to be grouped into predefined or user-defined
layers. This can be an incredibly useful feature, allowing the contents of the
game world to be organized sensibly. Entire layers can be hidden or shown to
reduce clutter on-screen. Layers might be color-coded for easy identification.
Layers can be an important part of a division-of-labor strategy, as well. For
example, when the lighting team is working on a world chunk, they can hide
all of the elements in the scene that are not relevant to lighting.
What’s more, if the game world editor is capable of loading and saving
layers individually, conflicts can be avoided when multiple people are work-
ing on a single world chunk at the same time. For example, all of the lights
might be stored in one layer, all of the background geometry in another and all
AI characters in a third. Since each layer is totally independent, the lighting,
background and NPC teams can all work simultaneously on the same world
chunk.

15.4.1.6
Property Grid

The static and dynamic elements that populate a game world chunk typically
have various properties (also known as attributes) that can be edited by the
user. Properties might be simple key-value pairs and be limited to simple
atomic data types like Booleans, integers, floating-point numbers and strings.
In some editors, more complex properties are supported, including arrays of
data and nested compound data structures. More complex data types may be
supported too, such as vectors, RGB colors and references to external assets
(audio files, meshes, animations, etc.)
Most world editors display the attributes of the currently selected object(s)
in a scrollable property grid view. An example of a property grid is shown in
Figure 15.7. The grid allows the user to see the current values of each attribute
and edit the values by typing, using check boxes or drop-down combo boxes,
dragging spinner controls up and down and so on.

Editing Multiobject Selections

In editors that support multiobject selection, the property grid may support
multiobject editing as well. This advanced feature displays an amalgam of the
attributes of all objects in the selection. If a particular attribute has the same
value across all objects in the selection, the value is shown as-is, and editing
the value in the grid causes the property value to be updated in all selected
objects. If the attribute’s value differs from object to object within the selection,
the property grid typically shows no value at all. In this case, if a new value


<!-- source-pdf-page: 1051 -->
> Visual fallback for diagrams/images: [PDF page 1051](../../../visual_pages/page_1051.jpg)

Figure 15.7. A typical property grid.

is typed into the field in the grid, it will overwrite the values in all selected
objects, bringing them all into agreement.

Another problem arises when the selection contains a heterogeneous col-
lection of objects (i.e., objects whose types differ). Each type of object can po-
tentially have a different set of attributes, so the property grid must display
only those attributes that are common to all object types in the selection. This
can still be useful, however, because game object types often inherit from a
common base type. For example, most objects have a position and orientation.
In a heterogeneous selection, the user can still edit these shared attributes even
though more-specific attributes are temporarily hidden from view.

Free-Form Properties

Normally, the set of properties associated with an object, and the data types of
those properties, are defined on a per-object-type basis. For example, a render-
able object has a position, orientation, scale and mesh, while a light has posi-
tion, orientation, color, intensity and light type. Some editors also allow addi-
tional “free-form” properties to be defined by the user on a per-instance basis.


<!-- source-pdf-page: 1052 -->

These properties are usually implemented as a flat list of key-value pairs. The
user is free to choose the name (key) of each free-form property, along with
its data type and its value. This can be incredibly useful for prototyping new
gameplay features or implementing one-off scenarios.

15.4.1.7
Object Placement and Alignment Aids

Some object properties are treated in a special way by the world editor. Typi-
cally the position, orientation and scale of an object can be controlled via spe-
cial handles in the orthographic and perspective viewports, just like in Maya
or Max. In addition, asset linkages often need to be handled in a special way.
For example, if we change the mesh associated with an object in the world, the
editor should display this mesh in the orthographic and 3D perspective view-
ports. As such, the game world editor must have special knowledge of these
properties—it cannot treat them generically, as it can most object properties.
Many world editors provide a host of object placement and alignment aids
in addition to the basic translation, rotation and scale tools. Many of these
features borrow heavily from the feature sets of commercial graphics and 3D
modeling tools like Photoshop, Maya, Visio and others. Examples include
snap to grid, snap to terrain, align to object and many more.

15.4.1.8
Special Object Types

Just as some object properties must be handled in a special way by the world
editor, certain types of objects also require special handling. Examples include:

•
Lights. The world editor usually uses special icons to represent lights,
since they have no mesh. The editor may attempt to display the light’s
approximate effect on the geometry in the scene as well, so that designers
can move lights around in real time and get a reasonably good feel for
how the scene will ultimately look.

•
Particle emitters. Visualization of particle effects can also be problematic
in editors that are built on a stand-alone rendering engine. In this case,
particle emitters might be displayed using icons only, or some attempt
might be made to emulate the particle effect in the editor. Of course, this
is not a problem if the editor is in-game or can communicate with the
running game for live tweaking.

•
Sound sources. As we discussed in Chapter 14, a 3D rendering engine
models sound sources as 3D points or volumes. It may be convenient
to provide specialized editing tools for these in the world editor. For
example, sound designers will find it helpful if they can visualize the


<!-- source-pdf-page: 1053 -->

maximum radius of an omnidirectional sound emitter, or the direction
vector and cone of a directional emitter.

•
Regions. A region is a volume of space that is used by the game to detect
relevant events such as objects entering or leaving the volume or to de-
mark areas for various purposes. Some game engines restrict regions to
being modeled as spheres or oriented boxes, while others may permit ar-
bitrary convex polygonal shapes when viewed from above, with strictly
horizontal sides. Still others might allow regions to be constructed out
of more complex geometry, such as k-DOPs (see Section 13.3.4.5). If re-
gions are always spherical then the designers might be able to make do
with a “Radius” property in the property grid, but to define or modify
the extents of an arbitrarily shaped region, a special-case editing tool is
almost certainly required.

•
Splines. A spline is a three-dimensional curve defined by a set of control
points and possibly tangent vectors at the points, depending on the type
of mathematical curve used. Catmull-Rom splines are commonly used
because they are fully defined by a set of control points (without tan-
gents), and the curve always passes through all of the control points. But
no matter what type of splines are supported, the world editor typically
needs to provide the ability to display the splines in its viewports, and
the user must be able to select and manipulate individual control points.
Some world editors actually support two selection modes—a “coarse”
mode for selecting objects in the scene and a “fine” mode for selecting
the individual components of a selected object, such as the control points
of a spline or the vertices of a region.

•
Nav meshes for AI. In many games, NPCs navigate by running path-
finding algorithms within the navigable regions of the game world.
These navigable regions must be defined, and the world editor usually
plays a central role in allowing AI designers to create, visualize and edit
these regions. For example, a nav mesh is a 2D triangle mesh that pro-
vides a simple description of the boundaries of the navigable region, as
well as providing connectivity information to the path finder.

•
Other custom data. Of course, every game has its own specific data re-
quirements. The world editor may be called upon to provide custom
visualization and editing facilities for these pieces of data. Examples in-
clude a description of the “affordances” (windows, doorways, possible
points of attack or defense) within a play space for use by the AI system,
or geometric features that describe things like cover points or grabbable
ledges for use by the player character and/or NPCs.


<!-- source-pdf-page: 1054 -->

15.4.1.9
Saving and Loading World Chunks

Of course, no world editor would be complete if it were unable to load and
save world chunks. The granularity with which world chunks can be loaded
and saved differs widely from engine to engine. Some engines store each
world chunk in a single file, while others allow individual layers to be loaded
and saved independently. Data formats also vary across engines. Some use
custom binary formats, others text formats like XML or JSON. Each design has
its pros and cons, but every editor provides the ability to load and save world
chunks in some form—and every game engine is capable of loading world
chunks so that they can be played at runtime.

15.4.1.10
Rapid Iteration

A good game world editor usually supports some degree of dynamic tweak-
ing for rapid iteration. Some editors run within the game itself, allowing the
user to see the effects of his or her changes immediately. Others provide a
live connection from the editor to the running game. Still other world editors
operate entirely offline, either as a stand-alone tool or as a plug-in to a DCC
application like Lightwave or Maya. These tools sometimes permit modified
data to be reloaded dynamically into the running game. The specific mech-
anism isn’t important—all that matters is that users have a reasonably short
round-trip iteration time (i.e., the time between making a change to the game
world and seeing the effects of that change in-game). It’s important to real-
ize that iterations don’t have to be instantaneous. Iteration times should be
commensurate with the scope and frequency of the changes being made. For
example, we might expect tweaking a character’s maximum health to be a very
fast operation, but when making major changes to the lighting environment
for an entire world chunk, a much longer iteration time might be acceptable.

### 15.4.2 Integrated Asset Management Tools

In some engines, the game world editor is integrated with other aspects of
game asset database management, such as defining mesh and material prop-
erties, defining animations, blend trees, animation state machines, setting up
collision and physical properties of objects, managing texture resources and
so on. (See Section 7.2.1.2 for a discussion of the game asset database.)
Perhaps the best-known example of this design in action is UnrealEd, the
editor used to create content for games built on the Unreal Engine. UnrealEd
is integrated directly into the game engine, so any changes made in the editor
are made directly to the dynamic elements in the running game. This makes
rapid iteration very easy to achieve. But UnrealEd is much more than a game


<!-- source-pdf-page: 1055 -->
> Visual fallback for diagrams/images: [PDF page 1055](../../../visual_pages/page_1055.jpg)

Figure 15.8. UnrealEd’s Generic Browser provides access to the entire game asset database.

world editor—it is actually a complete content-creation package. It manages
the entire database of game assets, from animations to audio clips to triangle
meshes to textures to materials and shaders and much more. UnrealEd pro-
vides its user with a unified, real-time, WYSIWYG view into the entire asset
database, making it a powerful enabler of any rapid, efficient game develop-
ment process. A few screenshots from UnrealEd are shown in Figures 15.8
and 15.9.

15.4.2.1
Data Processing Costs

In Section 7.2.1, we learned that the asset conditioning pipeline (ACP) con-
verts game assets from their various source formats into the formats required
by the game engine. This is typically a two-step process. First, the asset is
exported from the DCC application to a platform-independent intermediate
format that only contains the data that is relevant to the game. Second, the
asset is processed into a format that is optimized for a specific platform. On
a project targeting multiple gaming platforms, a single platform-independent
asset gives rise to multiple platform-specific assets during this second phase.
One of the key differences between tools pipelines is the point at which this
second platform-specific optimization step is performed. UnrealEd performs
it when assets are first imported into the editor. This approach pays off in


<!-- source-pdf-page: 1056 -->
> Visual fallback for diagrams/images: [PDF page 1056](../../../visual_pages/page_1056.jpg)

Figure 15.9. UnrealEd also provides a world editor.

rapid iteration time when iterating on level design. However, it can make the
cost of changing source assets like meshes, animations, audio assets and so
on more painful. Other engines like the Source engine and the Quake engine
pay the asset optimization cost when baking out the level prior to running
the game. Halo gives the user the option to change raw assets at any time;
they are converted into an optimized form when they are first loaded into the
engine, and the results are cached to prevent the optimization step from being
performed needlessly every time the game is run.


<!-- source-pdf-page: 1057 -->
> Visual fallback for diagrams/images: [PDF page 1057](../../../visual_pages/page_1057.jpg)

Taylor & Francis

Taylor & Francis Group
http://taylorandfrancis.com
