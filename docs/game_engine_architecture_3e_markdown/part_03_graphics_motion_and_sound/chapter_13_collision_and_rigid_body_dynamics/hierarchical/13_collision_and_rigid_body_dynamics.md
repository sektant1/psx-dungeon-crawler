# 13 Collision and Rigid Body Dynamics

> Source PDF pages: 836-929
> Extraction mode: PyMuPDF text blocks; line breaks and printed hyphenation are preserved.

<!-- source-pdf-page: 836 -->

I
n the real world, solid objects are inherently, well…solid. They generally
avoid doing impossible things, like passing through one another, all by
themselves. But in a virtual game world, objects don’t do anything unless
we tell them to, and game programmers must make an explicit effort to en-
sure that objects do not pass through one another. This is the role of one of the
central components of any game engine—the collision detection system.
A game engine’s collision system is often closely integrated with a physics
engine. Of course, the field of physics is vast, and what most of today’s game
engines call “physics” is more accurately described as a rigid body dynamics
simulation. A rigid body is an idealized, infinitely hard, non-deformable solid
object. The term dynamics refers to the process of determining how these rigid
bodies move and interact over time under the influence of forces. A rigid body
dynamics simulation allows motion to be imparted to objects in the game in a
highly interactive and naturally chaotic manner—an effect that is much more
difficult to achieve when using canned animation clips to move things about.
A dynamics simulation makes heavy use of the collision detection system
in order to properly simulate various physical behaviors of the objects in the
simulation, including bouncing off one another, sliding under friction, rolling
and coming to rest. Of course, a collision detection system can be used stand-
alone, without a dynamics simulation—many games do not have a “physics”


<!-- source-pdf-page: 837 -->

system at all. But all games that involve objects moving about in two- or three-
dimensional space have some form of collision detection.
In this chapter, we’ll investigate the architecture of both a typical collision
detection system and a typical physics (rigid body dynamics) system. As we
investigate the components of these two closely interrelated systems, we’ll
take a look at the mathematics and the theory that underlie them.

## 13.1 Do You Want Physics in Your Game?

Nowadays, most game engines have some kind of physical simulation capa-
bilities. Some physical effects, like rag doll deaths, are simply expected by
gamers. Other effects, like ropes, cloth, hair or complex physically driven ma-
chinery can add that je ne sais quoi that sets a game apart from its competitors.
In recent years, some game studios have started experimenting with advanced
physical simulations, including approximate real-time fluid mechanics effects
and simulations of deformable bodies. But adding physics to a game is not
without costs, and before we commit ourselves to implementing an exhaus-
tive list of physics-driven features in our game, we should (at the very least)
understand the trade-offs involved.

### 13.1.1 Things You Can Do with a Physics System

Here are just a few of the things you can do or have with a game physics sys-
tem.

•
Detect collisions between dynamic objects and static world geometry.

•
Simulate free rigid bodies under the influence of gravity and other forces.

•
Spring-mass systems.

•
Destructible buildings and structures.

•
Ray and shape casts (to determine line of sight, bullet impacts, etc.).

•
Trigger volumes (determine when objects enter, leave or are inside pre-
defined regions in the game world).

•
Complex machines (cranes, moving platform puzzles and so on).

•
Traps (such as an avalanche of boulders).

•
Drivable vehicles with realistic suspensions.

•
Rag doll character deaths.

•
Powered rag doll: a realistic blend between traditional animation and
rag doll physics.


<!-- source-pdf-page: 838 -->

•
Dangling props (canteens, necklaces, swords), semi-realistic hair, cloth-
ing movements.

•
Cloth simulations.

•
Water surface simulations and buoyancy.

•
Audio propagation.

And the list goes on.
We should note here that in addition to running a physics simulation at
runtime in our game, we can also run a simulation as part of an offline pre-
processing step in order to generate an animation clip. A number of physics
plug-ins are available for animation tools like Maya.
This is also the ap-
proach taken by the Endorphin1 package by NaturalMotion, Inc. (http://www.
naturalmotion.com/endorphin.htm). In this chapter, we’ll restrict our discus-
sion to runtime rigid body dynamics simulations, but offline tools are a pow-
erful option, of which we should always remain aware as we plan our game
projects.

### 13.1.2 Is Physics Fun?

The presence of a rigid body dynamics system in a game does not necessarily
make the game fun. More often than not, the inherently chaotic behavior of a
physics sim can actually detract from the gameplay experience rather than en-
hancing it. The fun derived from physics depends on many factors, including
the quality of the simulation itself, the care with which it has been integrated
with other engine systems, the selection of physics-driven gameplay elements
versus elements that are controlled in a more direct manner, how the physical
elements interact with the goals of the player and the abilities of the player
character, and the genre of game being made.
Let’s take a look at a few broad game genres and how a rigid body dynam-
ics system might fit into each one.

13.1.2.1
Simulations (Sims)

The primary goal of a sim is to accurately reproduce a real-life experience.
Examples include the Flight Simulator, Gran Turismo and NASCAR Racing series
of games. Clearly, the realism provided by a rigid body dynamics system fits
extremely well into these kinds of games.

1NaturalMotion also offers a runtime version of Endorphin called Euphoria.


<!-- source-pdf-page: 839 -->

13.1.2.2
Physics Puzzle Games

The whole idea of a physics puzzle is to let the user play around with dy-
namically simulated toys. So obviously this kind of game relies almost en-
tirely on physics for its core mechanic. Examples of this genre include Bridge
Builder, The Incredible Machine, the online game Fantastic Contraption, and
Crayon Physics for the iPhone.

13.1.2.3
Sandbox Games

In a sandbox game, there may be no objectives at all, or there may be a large
number of optional objectives. The player’s primary objective is usually to
“mess around” and explore what the objects in the game world can be made
to do. Examples of sandbox games include Besiege, Spore, the LittleBigPlanet
series, and of course Minecraft.
Sandbox games can put a realistic dynamics simulation to good use, es-
pecially if much of the fun is derived from playing with realistic (or semi-
realistic) interactions between objects in the game world. So in these contexts,
physics can be fun in and of itself. However, many games trade realism for an
increased fun factor (e.g., larger-than-life explosions, gravity that is stronger
or weaker than normal, etc.). So the dynamics simulation may need to be
tweaked in various ways to achieve the right “feel.”

13.1.2.4
Goal-Based and Story-Driven Games

A goal-based game has rules and specific objectives that the player must
accomplish in order to progress; in a story-driven game, telling a story is
of paramount importance. Integrating a physics system into these kinds of
games can be tricky. We generally give away control in exchange for a realistic
simulation, and this loss of control can inhibit the player’s ability to accomplish
goals or the game’s ability to tell the story.
For example, in a character-based platformer game, we want the player
character to move in ways that are fun and easy to control but not necessarily
physically realistic. In a war game, we might want a bridge to explode in a
realistic way, but we also may want to ensure that the debris doesn’t end up
blocking the player’s only path forward. In these kinds of games, physics is
often not necessarily fun, and in fact it can often get in the way of fun when
the player’s goals are at odds with the physically simulated behaviors of the
objects in the game world. Therefore, developers must be careful to apply
physics judiciously and take steps to control the behavior of the simulation
in various ways to ensure it doesn’t get in the way of gameplay. It’s usually
a good idea to provide the player with a way out of difficult situations, too.


<!-- source-pdf-page: 840 -->

A good example of this can be found in the Halo series of games, where the
player can press X to flip over a vehicle that has landed upside-down.

### 13.1.3 Impact of Physics on a Game

Adding a physics simulation to a game can have all sorts of impacts on the
project and the gameplay. Here are a few examples across various game de-
velopment disciplines.

13.1.3.1
Design Impacts

•
Predictability. The inherent chaos and variability that sets a physically
simulated behavior apart from an animated one is also a source of un-
predictability. If something absolutely must happen a certain way every
time, it’s usually better to animate it than to try to coerce your dynamics
simulation into producing the motion reliably.

•
Tuning and control. The laws of physics (when modeled accurately) are
fixed. In a game, we can tweak the value of gravity or the coefficient
of restitution of a rigid body, which gives back some degree of control.
However, the results of tweaking physics parameters are often indirect
and difficult to visualize. It’s much harder to tweak a force in order to
get a character to move in the desired direction than it is to tweak an
animation of a character walking.

•
Emergent behaviors. Sometimes physics introduces unexpected features
into a game—for example, the rocket-launcher jump trick in Team Fortress
Classic, the high-flying exploding Warthog in Halo and the flying “surf-
boards” in PsyOps.

In general, the game design should usually drive the physics requirements
of a game engine—not the other way around.

13.1.3.2
Engineering Impacts

•
Tools pipeline. A good collision/physics pipeline takes time to build and
maintain.

•
User interface. How does the player control the physics objects in the
world? Does he or she shoot them? Walk into them? Pick them up?
Does he or she hold them using virtual arms, as in Trespasser? Or using
a “gravity gun,” as in Half-Life 2?

•
Collision detection. Collision models intended for use within a dynamics
simulation may need to be more detailed and more carefully constructed
than their non-physics-driven counterparts.


<!-- source-pdf-page: 841 -->

•
AI. Pathing may not be predictable in the presence of physically simu-
lated objects. The engine may need to handle dynamic cover points that
can move or blow up. Can the AI use the physics to its advantage?

•
Misbehaved objects. Animation-driven objects can clip slightly through
one another with few or no ill effects. But when driven by a dynamics
simulation, objects may bounce off one another in unexpected ways or
jitter badly. Collision filtering may need to be applied to permit objects
to interpenetrate slightly. Mechanisms may need to be put in place to
ensure that objects settle and go to sleep properly.

•
Rag doll physics. Rag dolls require a lot of fine-tuning and often suffer
from instability in the simulation. An animation may drive parts of a
character’s body into penetration with other collision volumes—when
the character turns into a rag doll, these interpenetrations can cause enor-
mous instability. Steps must be taken to avoid this.

•
Graphics. Physics-driven motion can have an effect on renderable ob-
jects’ bounding volumes (where they would otherwise be static or more
predictable). The presence of destructible buildings and objects can in-
validate some kinds of precomputed lighting and shadow methods.

•
Networking and multiplayer. Physics effects that do not affect gameplay
may be simulated exclusively (and independently) on each client ma-
chine. However, physics that has an effect on gameplay (such as the
trajectory that a grenade follows) must be simulated on the server and
accurately replicated on all clients.

•
Record and playback. The ability to record gameplay and play it back at a
later time is very useful as a debugging/testing aid, and it can also serve
as a fun game feature. This feature is difficult to implement because it
requires every engine system to behave in a deterministic manner, so
that everything will play out exactly in the same way during playback
as it did when the recording was made. If your physics simulation isn’t
deterministic, this can become a major fly in the ointment.

13.1.3.3
Art Impacts

•
Additional tool and workflow complexity. The need to rig up objects with
mass, friction, constraints and other attributes for consumption by the
dynamics simulation makes the art department’s job more difficult as
well.

•
More complex content. We may need multiple visually identical versions
of an object with different collision and dynamics configurations for dif-


<!-- source-pdf-page: 842 -->

ferent purposes—for example, a pristine version and a destructible ver-
sion.

•
Loss of control. The unpredictability of physics-driven objects can make
it difficult to control the artistic composition of a scene.

13.1.3.4
Other Impacts

•
Interdisciplinary impacts. The introduction of a dynamics simulation into
your game requires close cooperation between engineering, art, audio
and design.

•
Production impacts. Physics can add to a project’s development costs,
technical and organizational complexity and risk.

Having explored the impacts, most teams today do choose to integrate a
rigid body dynamics system into their games. With some careful planning and
wise choices along the way, adding physics to your game can be rewarding and
fruitful. And as we’ll see below, third-party middleware is making physics
more accessible than ever.

## 13.2 Collision/Physics Middleware

Writing a collision system and rigid body dynamics simulation is challenging
and time-consuming work. The collision/physics system of a game engine
can account for a significant percentage of the source code in a typical game
engine. That’s a lot of code to write and maintain!
Thankfully, a number of robust, high-quality collision/physics engines are
now available, either as commercial products or in open source form. Some
of these are listed below. For a discussion of the pros and cons of various
physics SDKs, check out the online game development forums (e.g., http://
www.gamedev.net/community/forums/topic.asp?topic_id=463024).

### 13.2.1 ODE

ODE stands for “Open Dynamics Engine” (http://www.ode.org). As its name
implies, ODE is an open source collision and rigid body dynamics SDK. Its fea-
ture set is similar to a commercial product like Havok. Its benefits include be-
ing free (a big plus for small game studios and school projects!) and the avail-
ability of full source code (which makes debugging much easier and opens up
the possibility of modifying the physics engine to meet the specific needs of a
particular game).


<!-- source-pdf-page: 843 -->

### 13.2.2 Bullet

Bullet is an open source collision detection and physics library used by both
the game and film industries. Its collision engine is integrated with its dy-
namics simulation, but hooks are provided so that the collision system can
be used stand-alone or integrated with other physics engines. It supports
continuous collision detection (CCD)—also known as time of impact (TOI) col-
lision detection—which, as we’ll see below, can be extremely helpful when
a simulation includes small, fast-moving objects. The Bullet SDK is avail-
able for download at http://code.google.com/p/bullet/, and the Bullet wiki is
located at http://www.bulletphysics.com/mediawiki-1.5.8/index.php?title=
Main_Page.

### 13.2.3 TrueAxis

TrueAxis is another collision/physics SDK. It is free for non-commercial use.
You can learn more about TrueAxis at http://trueaxis.com.

### 13.2.4 PhysX

PhysX started out as a library called Novodex, produced and distributed by
Ageia as part of their strategy to market their dedicated physics coprocessor. It
was bought by NVIDIA and retooled so that it can run using NVIDIA’s GPUs
as a coprocessor. (It can also run entirely on a CPU, without GPU support.)
It is available at http://www.nvidia.com/object/nvidia_physx.html. Part of
Ageia’s and NVIDIA’s marketing strategy has been to provide the CPU ver-
sion of the SDK entirely for free, in order to drive the physics coprocessor
market forward. Developers can also pay a fee to obtain full source code and
the ability to customize the library as needed. PhysX is now combined with
APEX, NVIDIA’s scalable multiplatform dynamics framework. PhysX/APEX
is available for Windows, Linux, Mac, Android, Xbox 360, PlayStation 3, Xbox
One, PlayStation 4 and Wii.

### 13.2.5 Havok

Havok is the gold standard in commercial physics SDKs, providing one of the
richest feature sets available and boasting excellent performance characteris-
tics on all supported platforms. (It’s also the most expensive solution.) Havok
is comprised of a core collision/physics engine, plus a number of optional
add-on products including a vehicle physics system, a system for modeling
destructible environments and a fully featured animation SDK with direct in-
tegration into Havok’s rag doll physics system. It is available on Xbox 360,


<!-- source-pdf-page: 844 -->

PlayStation 3, Xbox One, PlayStation 4, PlayStation Vita, Wii, Wii U, Win-
dows 8, Android, Apple Mac and iOS. You can learn more about Havok at
http://www.havok.com.

### 13.2.6 Physics Abstraction Layer (PAL)

The Physics Abstraction Layer (PAL) is an open source library that allows
developers to work with more than one physics SDK on a single project.
It provides hooks for PhysX (Novodex), Newton, ODE, OpenTissue, Toka-
mak, TrueAxis and a few other SDKs.
You can read more about PAL at
http://www.adrianboeing.com/pal/index.html.

### 13.2.7 Digital Molecular Matter (DMM)

Pixelux Entertainment S.A., located in Geneva, Switzerland, has produced a
unique physics engine that uses finite element methods to simulate the dy-
namics of deformable bodies and breakable objects, called Digital Molecu-
lar Matter (DMM). The engine has both an offline and a runtime compo-
nent. It was released in 2008 and can be seen in action in LucasArts’ Star
Wars: The Force Unleashed. A discussion of deformable body mechanics is
beyond our scope here, but you can read more about DMM at http://www.
pixeluxentertainment.com.

## 13.3 The Collision Detection System

The primary purpose of a game engine’s collision detection system is to de-
termine whether any of the objects in the game world have come into contact.
To answer this question, each logical object is represented by one or more ge-
ometric shapes. These shapes are usually quite simple, such as spheres, boxes
and capsules. However, more complex shapes can also be used. The colli-
sion system determines whether or not any of the shapes are intersecting (i.e.,
overlapping) at any given moment in time. So a collision detection system is
essentially a glorified geometric intersection tester.
Of course, the collision system does more than answer yes/no questions
about shape intersection. It also provides relevant information about the na-
ture of each contact. Contact information can be used to prevent unrealistic
visual anomalies on-screen, such as objects interpenetrating one another. This
is generally accomplished by moving all interpenetrating objects apart prior
to rendering the next frame. Collisions can provide support for an object—one
or more contacts that together allow the object to come to rest, in equilibrium
with gravity and/or any other forces acting on it. Collisions can also be used


<!-- source-pdf-page: 845 -->
> Visual fallback for diagrams/images: [PDF page 845](../../../visual_pages/page_0845.jpg)

for other purposes, such as to cause a missile to explode when it strikes its
target or to give the player character a health boost when he passes through
a floating health pack. A rigid body dynamics simulation is often the most
demanding client of the collision system, using it to mimic physically realis-
tic behaviors like bouncing, rolling, sliding and coming to rest. But, of course,
even games that have no physics system can still make heavy use of a collision
detection engine.
In this chapter, we’ll go on a brief high-level tour of how collision detec-
tion engines work. For an in-depth treatment of this topic, a number of ex-
cellent books on real-time collision detection are available, including [14], [48]
and [11].

### 13.3.1 Collidable Entities

If we want a particular logical object in our game to be capable of colliding with
other objects, we need to provide it with a collision representation, describing
the object’s shape and its position and orientation in the game world. This
is a distinct data structure, separate from the object’s gameplay representation
(the code and data that define its role and behavior in the game) and separate
from its visual representation (which might be an instance of a triangle mesh, a
subdivision surface, a particle effect or some other visual representation).
From the point of view of detecting intersections, we generally favor
shapes that are geometrically and mathematically simple. For example, a rock
might be modeled as a sphere for collision purposes; the hood of a car might
be represented by a rectangular box; a human body might be approximated
by a collection of interconnected capsules (pill-shaped volumes). Ideally, we
should resort to a more complex shape only when a simpler representation
proves inadequate to achieve the desired behavior in the game. Figure 13.1
shows a few examples of using simple shapes to approximate object volumes
for collision detection purposes.
Havok uses the term collidable to describe a distinct, rigid object that can
take part in collision detection. It represents each collidable with an instance
of the C++ class hkpCollidable. PhysX calls its rigid objects actors and rep-
resents them as instances of the class NxActor. In both of these libraries, a
collidable entity contains two basic pieces of information—a shape and a trans-
form. The shape describes the collidable’s geometric form, and the transform
describes the shape’s position and orientation in the game world. Collidables
need transforms for three reasons:

1.
Technically speaking, a shape only describes the form of an object (i.e.,
whether it is a sphere, a box, a capsule or some other kind of volume).


<!-- source-pdf-page: 846 -->
> Visual fallback for diagrams/images: [PDF page 846](../../../visual_pages/page_0846.jpg)

Figure 13.1. Simple geometric shapes are often used to approximate the collision volumes of the
objects in a game.

It may also describe the object’s size (e.g., the radius of a sphere or the
dimensions of a box). But a shape is usually defined with its center at the
origin and in some sort of canonical orientation relative to the coordinate
axes. To be useful, a shape must therefore be transformed in order to
position and orient it appropriately in world space.

2.
Many of the objects in a game are dynamic. Moving an arbitrarily com-
plex shape through space could be expensive if we had to move the fea-
tures of the shape (vertices, planes, etc.) individually. But with a trans-
form, any shape can be moved in space inexpensively, no matter how
simple or complex the shape’s features may be.

3.
The information describing some of the more complex kinds of shapes
can take up a nontrivial amount of memory. So, it can be beneficial to
permit more than one collidable to share a single shape description. For
example, in a racing game, the shape information for many of the cars
might be identical. In that case, all of the car collidables in the game can
share a single car shape.

Any particular object in the game may have no collidable at all (if it doesn’t
require collision detection services), a single collidable (if the object is a simple
rigid body) or multiple collidables (each representing one rigid component of
an articulated robot arm, for example).


<!-- source-pdf-page: 847 -->

### 13.3.2 The Collision/Physics World

A collision system typically keeps track of all of its collidable entities via a
singleton data structure known as the collision world. The collision world is a
complete representation of the game world designed explicitly for use by the
collision detection system. Havok’s collision world is an instance of the class
hkpWorld. Likewise, the PhysX world is an instance of NxScene. ODE uses
an instance of class dSpace to represent the collision world; it is actually the
root of a hierarchy of geometric volumes representing all the collidable shapes
in the game.
Maintaining all collision information in a private data structure has a num-
ber of advantages over attempting to store collision information with the game
objects themselves. For one thing, the collision world need only contain col-
lidables for those game objects that can potentially collide with one another.
This eliminates the need for the collision system to iterate over any irrelevant
data structures. This design also permits collision data to be organized in the
most efficient manner possible. The collision system can take advantage of
cache coherency to maximize performance, for example. The collision world
is also an effective encapsulation mechanism, which is generally a plus from
the perspectives of understandability, maintainability, testability and the po-
tential for software reuse.

13.3.2.1
The Physics World

If a game has a rigid body dynamics system, it is usually tightly integrated
with the collision system. It typically shares its “world” data structure with the
collision system, and each rigid body in the simulation is usually associated
with a single collidable in the collision system. This design is commonplace
among physics engines because of the frequent and detailed collision queries
required by the physics system. It’s typical for the physics system to actually
drive the operation of the collision system, instructing it to run collision tests
at least once, and sometimes multiple times, per simulation time step. For this
reason, the collision world is often called the collision/physics world or some-
times just the physics world.
Each dynamic rigid body in the physics simulation is usually associated
with a single collidable object in the collision system (although not all collid-
ables need be dynamic rigid bodies). For example, in Havok, a rigid body is
represented by an instance of the class hkpRigidBody, and each rigid body
has a pointer to exactly one hkpCollidable. In PhysX, the concepts of collid-
able and rigid body are comingled—the NxActor class serves both purposes
(although the physical properties of the rigid body are stored separately, in


<!-- source-pdf-page: 848 -->

an instance of NxBodyDesc). In both SDKs, it is possible to tell a rigid body
that its location and orientation are to be fixed in space, meaning that it will
be omitted from the dynamics simulation and will serve as a collidable only.
Despite this tight integration, most physics SDKs do make at least some
attempt to separate the collision library from the rigid body dynamics simu-
lation. This permits the collision system to be used as a stand-alone library
(which is important for games that don’t need physics but do need to detect
collisions). It also means that a game studio could theoretically replace a physics
SDK’s collision system entirely, without having to rewrite the dynamics sim-
ulation. (Practically speaking, this may be a bit harder than it sounds!)

### 13.3.3 Shape Concepts

A rich body of mathematical theory underlies the everyday concept of shape
(see http://en.wikipedia.org/wiki/Shape). For our purposes, we can think of
a shape simply as a region of space described by a boundary, with a definite
inside and outside. In two dimensions, a shape has area, and its boundary is
defined either by a curved line or by three or more straight edges (in which
case it’s a polygon). In three dimensions, a shape has volume, and its boundary
is either a curved surface or is composed of polygons (in which case is it called
a polyhedron).
It’s important to note that some kinds of game objects, like terrain, rivers
or thin walls, might be best represented by surfaces. In three-space, a surface
is a two-dimensional geometric entity with a front and a back but no inside or
outside. Examples include planes, triangles, subdivision surfaces and surfaces
constructed from a group of connected triangles or other polygons. Most col-
lision SDKs provide support for surface primitives and extend the term shape
to encompass both closed volumes and open surfaces.
It’s commonplace for collision libraries to allow surfaces to be given vol-
ume via an optional extrusion parameter. Such a parameter specifies how
“thick” a surface should be. Doing this helps reduce the occurrence of missed
collisions between small, fast-moving objects and infinitesimally thin surfaces
(the so-called “bullet through paper” problem—see Section 13.3.5.7).

13.3.3.1
Intersection

We all have an intuitive notion of what an intersection is. Technically speaking,
the term comes from set theory (http://en.wikipedia.org/wiki/Intersection_
(set_theory)). The intersection of two sets is comprised of the subset of mem-
bers that are common to both sets. In geometrical terms, the intersection be-
tween two shapes is just the (infinitely large!) set of all points that lie inside
both shapes.


<!-- source-pdf-page: 849 -->

13.3.3.2
Contact

In games, we’re not usually interested in finding the intersection in the strictest
sense, as a set of points. Instead, we want to know simply whether or not
two objects are intersecting. In the event of a collision, the collision system
will usually provide additional information about the nature of the contact.
This information allows us to separate the objects in a physically plausible
and efficient way, for example.
Collision systems usually package contact information into a convenient
data structure that can be instanced for each contact detected. For example,
Havok returns contacts as instances of the class hkContactPoint. Contact
information often includes a separating vector—a vector along which we can
slide the objects in order to efficiently move them out of collision. It also typ-
ically contains information about which two collidables were in contact, in-
cluding which individual shapes were intersecting and possibly even which
individual features of those shapes were in contact. The system may also re-
turn additional information, such as the velocity of the bodies projected onto
the separating normal.

13.3.3.3
Convexity

One of the most important concepts in the field of collision detection is the
distinction between convex and non-convex (i.e., concave) shapes. Technically, a
convex shape is defined as one for which no ray originating inside the shape
will pass through its surface more than once. A simple way to determine if
a shape is convex is to imagine shrink-wrapping it with plastic film—if it’s
convex, no air pockets will be left under the film. So in two dimensions, cir-
cles, rectangles and triangles are all convex, but Pac Man is not. The concept
extends equally well to three dimensions.
The property of convexity is important because, as we’ll see, it’s generally
simpler and less computationally intensive to detect intersections between
convex shapes than concave ones. See http://en.wikipedia.org/wiki/Convex
for more information about convex shapes.

### 13.3.4 Collision Primitives

Collision detection systems can usually work with a relatively limited set of
shape types. Some collision systems refer to these shapes as collision primitives
because they are the fundamental building blocks out of which more complex
shapes can be constructed. In this section, we’ll take a brief look at some of the
most common types of collision primitives.


<!-- source-pdf-page: 850 -->
> Visual fallback for diagrams/images: [PDF page 850](../../../visual_pages/page_0850.jpg)

13.3.4.1
Spheres

The simplest three-dimensional volume is a sphere. And as you might expect,
spheres are the most efficient kind of collision primitive. A sphere is repre-
sented by a center point and a radius. This information can be conveniently
packed into a four-element floating-point vector—a format that works partic-
ularly well with SIMD math libraries.

13.3.4.2
Capsules

A capsule is a pill-shaped volume, composed of a cylinder and two hemispher-
ical end caps. It can be thought of as a swept sphere—the shape that is traced
out as a sphere moves from point A to point B. (There are, however, some im-
portant differences between a static capsule and a sphere that sweeps out a
capsule-shaped volume over time, so the two are not identical.) Capsules are
often represented by two points and a radius (Figure 13.2). Capsules are more
efficient to intersect than cylinders or boxes, so they are often used to model
objects that are roughly cylindrical, such as the limbs of a human body.

2
1

r
r

Figure 13.2. A capsule can be represented by two points and a radius.

13.3.4.3
Axis-Aligned Bounding Boxes

An axis-aligned bounding box (AABB) is a rectangular volume (technically
known as a cuboid) whose faces are parallel to the axes of the coordinate sys-
tem. Of course, a box that is axis-aligned in one coordinate system will not
necessarily be axis-aligned in another. So we can only speak about an AABB
in the context of the particular coordinate frame(s) with which it aligns.
An AABB can be conveniently defined by two points: one containing the
minimum coordinates of the box along each of the three principal axes and the
other containing its maximum coordinates. This is depicted in Figure 13.3.
The primary benefit of axis-aligned boxes is that they can be tested for
interpenetration with other axis-aligned boxes in a highly efficient manner.
The big limitation of using AABBs is that they must remain axis-aligned at
all times if their computational advantages are to be maintained. This means
that if an AABB is used to approximate the shape of an object in the game,


<!-- source-pdf-page: 851 -->
> Visual fallback for diagrams/images: [PDF page 851](../../../visual_pages/page_0851.jpg)

y

ymax

ymin

x
xmin
xmax

Figure 13.3. An axis-aligned box.

the AABB will have to be recalculated whenever that object rotates. Even if
an object is roughly box-shaped, its AABB may degenerate into a very poor
approximation to its shape when the object rotates off-axis. This is shown in
Figure 13.4.

y

y

x

x

Figure 13.4. An AABB is only a good approximation to a box-shaped object when the object’s prin-
cipal axes are roughly aligned with the coordinated system’s axes.

13.3.4.4
Oriented Bounding Boxes

If we permit an axis-aligned box to rotate relative to its coordinate system, we
have what is known as an oriented bounding box (OBB). It is often represented
by three half-dimensions (half-width, half-depth and half-height) and a trans-
formation, which positions the center of the box and defines its orientation
relative to the coordinate axes. Oriented boxes are a commonly used collision
primitive because they do a better job at fitting arbitrarily oriented objects, yet
their representation is still quite simple.

13.3.4.5
Discrete Oriented Polytopes (DOP)

A discrete oriented polytope (DOP) is a more-general case of the AABB and
OBB. It is a convex polytope that approximates the shape of an object. A DOP
can be constructed by taking a number of planes at infinity and sliding them
along their normal vectors until they come into contact with the object whose


<!-- source-pdf-page: 852 -->
> Visual fallback for diagrams/images: [PDF page 852](../../../visual_pages/page_0852.jpg)

shape is to be approximated. An AABB is a 6-DOP in which the plane normals
are taken parallel to the coordinate axes. An OBB is also a 6-DOP in which the
plane normals are parallel to the object’s natural principal axes. A k-DOP is
constructed from an arbitrary number of planes k. A common method of con-
structing a DOP is to start with an OBB for the object in question and then bevel
the edges and/or corners at 45 degrees with additional planes in an attempt
to yield a tighter fit. An example of a k-DOP is shown in Figure 13.5.

Figure 13.5. An OBB that has been beveled on all eight corners is known as a 14-DOP.

13.3.4.6
Arbitrary Convex Volumes

Most collision engines permit arbitrary convex volume to be constructed by a
3D artist in a package like Maya. The artist builds the shape out of polygons
(triangles or quads). An offline tool analyzes the triangles to ensure that they
actually do form a convex polyhedron. If the shape passes the convexity test,
its triangles are converted into a collection of planes (essentially a k-DOP), rep-
resented by k plane equations, or k points and k normal vectors. (If it is found
to be non-convex, it can still be represented by a polygon soup—described in
the next section.) This approach is depicted in Figure 13.6.
Convex volumes are more expensive to intersection-test than the simpler
geometric primitives we’ve discussed thus far. However, as we’ll see in Section

Figure 13.6. An arbitrary convex volume can be represented by a collection of intersecting planes.


<!-- source-pdf-page: 853 -->
> Visual fallback for diagrams/images: [PDF page 853](../../../visual_pages/page_0853.jpg)

13.3.5.5, certain highly efficient intersection-finding algorithms such as GJK
are applicable to these shapes because they are convex.

13.3.4.7
Poly Soup

Some collision systems also support totally arbitrary, non-convex shapes.
These are usually constructed out of triangles or other simple polygons. For
this reason, this type of shape is often called a polygon soup, or poly soup for
short. Poly soups are often used to model complex static geometry, such as
terrain and buildings (Figure 13.7).
As you might imagine, detecting collisions with a poly soup is the most
expensive kind of collision test. In effect, the collision engine must test every
individual triangle, and it must also properly handle spurious intersections
with triangle edges that are shared between adjacent triangles. As a result,
most games try to limit the use of poly soup shapes to objects that will not
take part in the dynamics simulation.

Does a Poly Soup Have an Inside?

Unlike convex and simple shapes, a poly soup does not necessarily represent
a volume—it can represent an open surface as well. Poly soup shapes often
don’t include enough information to allow the collision system to differentiate
between a closed volume and an open surface. This can make it difficult to
know in which direction to push an object that is interpenetrating a poly soup
in order to bring the two objects out of collision.
Thankfully, this is by no means an intractable problem. Each triangle in

Figure 13.7. A poly soup is often used to model complex static surfaces such as terrain or build-
ings.


<!-- source-pdf-page: 854 -->
> Visual fallback for diagrams/images: [PDF page 854](../../../visual_pages/page_0854.jpg)

a poly soup has a front and a back, as defined by the winding order of its
vertices. Therefore, it is possible to carefully construct a poly soup shape so
that all of the polygons’ vertex winding orders are consistent (i.e., adjacent
triangles always “face” in the same direction). This gives the entire poly soup
a notion of “front” and “back.” If we also store information about whether
a given poly soup shape is open or closed (presuming that this fact can be
ascertained by offline tools), then for closed shapes, we can interpret “front”
and “back” to mean “outside” and “inside” (or vice versa, depending on the
conventions used when constructing the poly soup).
We can also “fake” an inside and outside for certain kinds of open poly soup
shapes (i.e., surfaces). For example, if the terrain in our game is represented by
an open poly soup, then we can decide arbitrarily that the front of the surface
always points away from the Earth. This implies that “front” should always
correspond to “outside.” Practically speaking, to make this work, we would
probably need to customize the collision engine in some way in order to make
it aware of our particular choice of conventions.

13.3.4.8
Compound Shapes

Some objects that cannot be adequately approximated by a single shape can
be approximated well by a collection of shapes. For example, a chair might be
modeled out of two boxes—one for the back of the chair and one enclosing the
seat and all four legs. This is shown in Figure 13.8.
A compound shape can often be a more-efficient alternative to a poly soup
for modeling non-convex objects; two or more convex volumes can often out-
perform a single poly soup shape. What’s more, some collision systems can
take advantage of the convex bounding volume of the compound shape as a
whole when testing for collisions. In Havok, this is called midphase collision
detection. As the example in Figure 13.9 shows, the collision system first tests
the convex bounding volumes of the two compound shapes. If they do not
intersect, the system needn’t test the subshapes for collisions at all.

Figure 13.8. A chair can be modeled using a pair of interconnected box shapes.


<!-- source-pdf-page: 855 -->
> Visual fallback for diagrams/images: [PDF page 855](../../../visual_pages/page_0855.jpg)

Bounding Volume
Hierarchies:

Sphere A

Sphere B

Sphere A

A1

A1

B2
B3

A2

A2

Sphere B

B1

B1

B2

B4

B3

B4

Figure 13.9. A collision system need only test the subshapes of a pair of compound shapes when
their convex bounding volumes (in this case, Sphere A and Sphere B) are found to be intersecting.

### 13.3.5 Collision Testing and Analytical Geometry

A collision system makes use of analytical geometry—mathematical descrip-
tions of three-dimensional volumes and surfaces—in order to detect intersec-
tions between shapes computationally.
See http://en.wikipedia.org/wiki/
Analytic_geometry for more details on this profound and broad area of re-
search. In this section, we’ll briefly introduce the concepts behind analytical
geometry, show a few common examples and then discuss the generalized
GJK intersection testing algorithm for arbitrary convex polyhedra.

13.3.5.1
Point versus Sphere

We can determine whether a point p lies within a sphere by simply forming
the separation vector s between the point and the sphere’s center c and then
checking its length. If it is greater than the radius of the sphere r, then the point
lies outside the sphere; otherwise, it lies inside:

s = c −p;
if |s| ≤r, then p is inside.

13.3.5.2
Sphere versus Sphere

Determining if two spheres intersect is almost as simple as testing a point
against a sphere. Again, we form a vector s connecting the center points of
the two spheres. We take its length and compare it with the sum of the radii
of the two spheres. If the length of the separating vector is less than or equal
to the sum of the radii, the spheres intersect; otherwise, they do not:

s = c1 −c2;
(13.1)
if |s| ≤(r1 + r2), then spheres intersect.


<!-- source-pdf-page: 856 -->
> Visual fallback for diagrams/images: [PDF page 856](../../../visual_pages/page_0856.jpg)

To avoid the square root operation inherent in calculating the length of
vector s, we can simply square the entire equation. So Equation (13.1) becomes

s = c1 −c2;
|s|2 = s · s;
if |s|2 ≤(r1 + r2)2, then spheres intersect.

13.3.5.3
The Separating Axis Theorem

Most collision detection systems make heavy use of a theorem known as
the separating axis theorem (http://en.wikipedia.org/wiki/Separating_axis
_theorem). It states that if an axis can be found along which the projections of
two convex shapes do not overlap, then we can be certain that the two shapes
do not intersect at all. If such an axis does not exist and the shapes are convex,
then we know for certain that they do intersect. (If the shapes are concave,
then they may not be interpenetrating despite the lack of a separating axis.
This is one reason why we tend to favor convex shapes in collision detection.)
This theorem is easiest to visualize in two dimensions. Intuitively, it says
that if a line can be found, such that object A is entirely on one side of the
line and object B is entirely on the other side, then objects A and B do not
overlap. Such a line is called a separating line, and it is always perpendicular to
the separating axis. So once we’ve found a separating line, it’s a lot easier to
convince ourselves that the theory is in fact correct by looking at the projections
of our shapes onto the axis that is perpendicular to the separating line.
The projection of a two-dimensional convex shape onto an axis acts like the
shadow that the object would leave on a thin wire. It is always a line seg-
ment, lying on the axis, that represents the maximum extents of the object in
the direction of the axis. We can also think of a projection as a minimum and
maximum coordinate along the axis, which we can write as the fully closed
interval [cmin, cmax]. As you can see in Figure 13.10, when a separating line ex-
ists between two shapes, their projections do not overlap along the separating
axis. However, the projections may overlap along other, non-separating axes.
In three dimensions, the separating line becomes a separating plane, but
the separating axis is still an axis (i.e., an infinite line). Again, the projection
of a three-dimensional convex shape onto an axis is a line segment, which we
can represent by the fully closed interval [cmin, cmax].
Some types of shapes have properties that make the potential separating
axes obvious. To detect intersections between two such shapes A and B, we
can project the shapes onto each potential separating axis in turn and then
check whether or not the two projection intervals, [cA
min, cA
max] and [cB
min, cB
max],


<!-- source-pdf-page: 857 -->
> Visual fallback for diagrams/images: [PDF page 857](../../../visual_pages/page_0857.jpg)

Separating
Line/Plane

Non-Separating Axis

B

B

A

Separating Axis

A

Projection of A
Projection of B

Figure 13.10. The projections of two shapes onto a separating axis are always two disjoint line
segments. The projections of these same shapes onto a non-separating axis are not necessarily
disjoint. If no separating axis exists, the shapes intersect.

are disjoint (i.e., do not overlap). In math terms, the intervals are disjoint if
cA
max < cB
min or if cB
max < cA
min. If the projection intervals along one of the
potential separating axes are disjoint, then we’ve found a separating axis, and
we know the two shapes do not intersect.
One example of this principle in action is the sphere-versus-sphere test. If
two spheres do not intersect, then the axis parallel to the line segment join-
ing the spheres’ center points will always be a valid separating axis (although
other separating axes may exist, depending on how far apart the two spheres
are). To visualize this, consider the limit when the two spheres are just about to
touch but have not yet come into contact. In that case, the only separating axis
is the one parallel to the center-to-center line segment. As the spheres move
apart, we can rotate the separating axis more and more in either direction. This
is shown in Figure 13.11.

13.3.5.4
AABB versus AABB

To determine whether two AABBs are intersecting, we can again apply the
separating axis theorem. The fact that the faces of both AABBs are guaranteed
to lie parallel to a common set of coordinate axes tells us that if a separating
axis exists, it will be one of these three coordinate axes.
So, to test for intersections between two AABBs, which we’ll call A and B,
we merely inspect the minimum and maximum coordinates of the two boxes
along each axis independently. Along the x-axis, we have the two intervals


<!-- source-pdf-page: 858 -->
> Visual fallback for diagrams/images: [PDF page 858](../../../visual_pages/page_0858.jpg)

Many
Separating
Lines/Planes

Separating
Line/Plane

Many
Separating Axes

Separating Axis

Figure 13.11. When two spheres are an inﬁnitesimal distance apart, the only separating axis lies
parallel to the line segment formed by the two spheres’ center points.

[xA
min, xA
max] and [xB
min, xB
max], and we have corresponding intervals for the y-
and z-axes. If the intervals overlap along all three axes, then the two AABBs are
intersecting—in all other cases, they are not. Examples of intersecting and non-
intersecting AABBs are shown in Figure 13.12 (simplified to two dimensions
for the purposes of illustration). For an in-depth discussion of AABB collision,
see http://www.gamasutra.com/features/20000203/lander_01.htm.

13.3.5.5
Detecting Convex Collisions: The GJK Algorithm

A very efficient algorithm exists for detecting intersections between arbitrary
convex polytopes (i.e., convex polygons in two dimensions, or convex poly-
hedra in three dimensions). It is known as the GJK algorithm, named after
its inventors, E. G. Gilbert, D. W. Johnson and S. S. Keerthi of the University
of Michigan. Many papers have been written on the algorithm and its vari-

y

y

x

x

Figure 13.12. A two-dimensional example of intersecting and non-intersecting AABBs. Notice that
even though the second pair of AABBs are intersecting along the x-axis, they are not intersecting
along the y-axis.


<!-- source-pdf-page: 859 -->
> Visual fallback for diagrams/images: [PDF page 859](../../../visual_pages/page_0859.jpg)

ants, including the original paper (http://ieeexplore.ieee.org/xpl/freeabs_all.
jsp?&arnumber=2083), an excellent SIGGRAPH PowerPoint presentation by
Christer Ericson (http://realtimecollisiondetection.net/pubs/SIGGRAPH04_
Ericson_the_GJK_algorithm.ppt) and another great PowerPoint presenta-
tion by Gino van den Bergen (www.laas.fr/~nic/MOVIE/Workshop/Slides/
Gino.vander.Bergen.ppt). However, the easiest-to-understand (and most en-
tertaining) description of the algorithm is probably Casey Muratori’s in-
structional video entitled “Implementing GJK,” available online at http://
mollyrocket.com/849. Because these descriptions are so good, I’ll just give
you a feel for the essence of the algorithm here and then direct you to the Molly
Rocket website and the other references cited above for additional details.
The GJK algorithm relies on a geometric operation known as the Minkowski
difference. This fancy-sounding operation is really quite simple: We take every
point that lies within shape B and subtract it pairwise from every point inside
shape A. The resulting set of points
{(Ai −Bj)
}
is the Minkowski difference.
The useful thing about the Minkowski difference is that, when applied to
two convex shapes, it will contain the origin if and only if those two shapes
intersect. Proof of this statement is a bit beyond our scope, but we can intuit
why it is true by remembering that when we say two shapes A and B intersect,
we really mean that there are points within A that are also within B. During
the process of subtracting every point in B from every point in A, we would
expect to eventually hit one of those shared points that lies within both shapes.
A point minus itself is all zeros, so the Minkowski difference will contain the
origin if (and only if) sphere A and sphere B have points in common. This is
illustrated in Figure 13.13.
The Minkowski difference of two convex shapes is itself a convex shape.
All we care about is the convex hull of the Minkowski difference, not all of the
interior points. The basic procedure of GJK is to try to find a tetrahedron (i.e.,
a four-sided shape made out of triangles) that lies on the convex hull of the
Minkwoski difference and that encloses the origin. If one can be found, then
the shapes intersect; if one cannot be found, then they don’t.
A tetrahedron is just one case of a geometrical object known as a simplex.
But don’t let that name scare you—a simplex is just a collection of points. A
single-point simplex is a point, a two-point simplex is a line segment, a three-
point simplex is a triangle and a four-point simplex is a tetrahedron (see Fig-
ure 13.14).
GJK is an iterative algorithm that starts with a one-point simplex lying any-
where within the Minkowski difference hull. It then attempts to build higher-
order simplexes that might potentially contain the origin. During each itera-
tion of the loop, we take a look at the simplex we currently have and determine


<!-- source-pdf-page: 860 -->
> Visual fallback for diagrams/images: [PDF page 860](../../../visual_pages/page_0860.jpg)

B

B

A

A

y

y

x
A – B

x

A – B

Does not Contain
the Origin

Contains the Origin

Figure 13.13. The Minkowski difference of two intersecting convex shapes contains the origin, but
the Minkowski difference of two non-intersecting shapes does not.

in which direction the origin lies relative to it. We then find a supporting vertex
of the Minkowski difference in that direction—i.e., the vertex of the convex
hull that is closest to the origin in the direction we’re currently going. We add
that new point to the simplex, creating a higher-order simplex (i.e., a point
becomes a line segment, a line segment becomes a triangle and a triangle be-
comes a tetrahedron). If the addition of this new point causes the simplex to
surround the origin, then we’re done—we know the two shapes intersect. On
the other hand, if we are unable to find a supporting vertex that is closer to
the origin than the current simplex, then we know that we can never get there,
which implies that the two shapes do not intersect. This idea is illustrated in
Figure 13.15.

To truly understand the GJK algorithm, you’ll need to check out the papers
and video I referenced previously. But hopefully this description will whet
your appetite for deeper investigation. Or, at the very least, you can impress

Line Segment
Point
Triangle
Tetrahedron

Figure 13.14. Simplexes containing one, two, three and four points.


<!-- source-pdf-page: 861 -->
> Visual fallback for diagrams/images: [PDF page 861](../../../visual_pages/page_0861.jpg)

New Point

New Point

y

y

x

x

Search
Direction

Search
Direction

Figure 13.15. In the GJK algorithm, if adding a point to the current simplex creates a shape that
contains the origin, we know the shapes intersect; if there is no supporting vertex that will bring
the simplex any closer to the origin, then the shapes do not intersect.

your friends by dropping the name “GJK” at parties. (Just don’t try this at job
interviews unless you really do understand the algorithm!)

13.3.5.6
Other Shape-Shape Combinations

We won’t cover any of the other shape-shape intersection combinations here,
as they are covered well in other texts such as [14], [48] and [11]. The key point
to recognize here, however, is that the number of shape-shape combinations is
very large. In fact, for N shape types, the number of pairwise tests required
is O(N2). Much of the complexity of a collision engine arises because of the
sheer number of intersection cases it must handle. This is one reason why
the authors of collision engines usually try to limit the number of primitive
types—doing so drastically reduces the number of cases the collision detector
must handle. (This is also why GJK is popular—it handles collision detection
between all convex shape types in one fell swoop. The only thing that differs
from shape type to shape type is the support function used in the algorithm.)
There’s also the practical matter of how to implement the code that se-
lects the appropriate collision-testing function given two arbitrary shapes
that are to be tested. Many collision engines use a double dispatch method
(http://en.wikipedia.org/wiki/Double_dispatch). In single dispatch (i.e., vir-
tual functions), the type of a single object is used to determine which concrete
implementation of a particular abstract function should be called at runtime.
Double dispatch extends the virtual function concept to two object types. It
can be implemented via a two-dimensional function look-up table keyed by
the types of the two objects being tested. It can also be implemented by ar-
ranging for a virtual function based on the type of object A to call a second
virtual function based on the type of object B.
Let’s take a look at a real-world example. Havok uses objects known as
collision agents (classes derived from hkpCollisionAgent) to handle specific


<!-- source-pdf-page: 862 -->
> Visual fallback for diagrams/images: [PDF page 862](../../../visual_pages/page_0862.jpg)

Figure 13.16. A small, fast-moving object can leave gaps in its motion path between consecutive
snapshots of the collision world, meaning that collisions might be missed entirely.

intersection test cases. Concrete agent classes include hkpSphereSphere-
Agent, hkpSphereCapsuleAgent, hkpGskConvexConvexAgent and so
on. The agent types are referenced by what amounts to a two-dimensional
dispatch table, managed by the class hkpCollisionDispatcher. As you’d
expect, the dispatcher’s job is to efficiently look up the appropriate agent given
a pair of collidables that are to be collision-tested and then call it, passing the
two collidables as arguments.

13.3.5.7
Detecting Collisions between Moving Bodies

Thus far, we’ve considered only static intersection tests between stationary ob-
jects. When objects move, this introduces some additional complexity. Motion
in games is usually simulated in discrete time steps. So one simple approach is
to treat the positions and orientations of each rigid body as stationary at each
time step and use static intersection tests on each “snapshot” of the collision
world. This technique works as long as objects aren’t moving too fast relative
to their sizes. In fact, it works so well that many collision/physics engines,
including Havok, use this approach by default.
However, this technique breaks down for small, fast-moving objects. Imag-
ine an object that is moving so fast that it covers a distance larger than its own size
(measured in the direction of travel) between time steps. If we were to over-
lay two consecutive snapshots of the collision world, we’d notice that there
is now a gap between the fast-moving object’s images in the two snapshots.
If another object happens to lie within this gap, we’ll miss the collision with
it entirely. This problem, illustrated in Figure 13.16, is known as the “bullet
through paper” problem, also known as “tunneling.” The following sections
describe a number of common ways to overcome this problem.

Swept Shapes

One way to avoid tunneling is to make use of swept shapes. A swept shape


<!-- source-pdf-page: 863 -->
> Visual fallback for diagrams/images: [PDF page 863](../../../visual_pages/page_0863.jpg)

is a new shape formed by the motion of a shape from one point to another
over time. For example, a swept sphere is a capsule, and a swept triangle is a
triangular prism (see Figure 13.17).
Rather than testing static snapshots of the collision world for intersections,
we can test the swept shapes formed by moving the shapes from their positions
and orientations in the previous snapshot to their positions and orientations
in the current snapshot. This approach amounts to linearly interpolating the
motion of the collidables between snapshots, because we generally sweep the
shapes along line segments from snapshot to snapshot.
Of course, linear interpolation may not be a good approximation of the mo-
tion of a fast-moving collidable. If the collidable is following a curved path,
then theoretically we should sweep its shape along that curved path. Unfortu-
nately, a convex shape that has been swept along a curve is not itself convex,
so this can make our collision tests much more complex and computationally
intensive.
In addition, if the convex shape we are sweeping is rotating, the resulting
swept shape is not necessarily convex, even when it is swept along a line seg-
ment. As Figure 13.18 shows, we can always form a convex shape by linearly
extrapolating the extreme features of the shapes from the previous and current
snapshots—but the resulting convex shape is not necessarily an accurate rep-
resentation of what the shape really would have done over the time step. Put
another way, a linear interpolation is not appropriate in general for rotating
shapes. So unless our shapes are not permitted to rotate, intersection testing
of swept shapes becomes much more complex and computationally intensive
than its static snapshot-based counterpart.
Swept shapes can be a useful technique for ensuring that collisions are not
missed between static snapshots of the collision world state. However, the
results are generally inaccurate when linearly interpolating curved paths or

Figure 13.17. A swept sphere is a capsule; a swept triangle is a triangular prism.


<!-- source-pdf-page: 864 -->
> Visual fallback for diagrams/images: [PDF page 864](../../../visual_pages/page_0864.jpg)

Figure 13.18. A rotating object swept along a line segment does not necessarily generate a convex shape (left). A linear in-
terpolation of the motion does form a convex shape (right), but it can be a fairly inaccurate approximation of what actually
happened during the time step.

rotating collidables, so more-detailed techniques may be required depending
on the needs of the game.

Continuous Collision Detection (CCD)

Another way to deal with the tunneling problem is to employ a technique
known as continuous collision detection (CCD). The goal of CCD is to find the
earliest time of impact (TOI) between two moving objects over a given time in-
terval.
CCD algorithms are generally iterative in nature. For each collidable, we
maintain both its position and orientation at the previous time step and its po-
sition and orientation at the current time. This information can be used to lin-
early interpolate the position and rotation independently, yielding an approx-
imation of the collidable’s transform at any time between the previous and
current time steps. The algorithm then searches for the earliest TOI along the
motion path. A number of search algorithms are commonly used, including
Brian Mirtich’s conservative advancement method, performing a ray cast on the
Minkowski sum, or considering the minimum TOI of individual feature pairs.
Erwin Coumans of Sony Interactive Entertainment describes some of these
algorithms in http://gamedevs.org/uploads/continuous-collision-detection
-and-physics.pdf along with his own novel variation on the conservative ad-
vancement approach.

### 13.3.6 Performance Optimizations

Collision detection is a CPU-intensive task for two reasons:

1.
The calculations required to determine whether two shapes intersect are
themselves nontrivial.


<!-- source-pdf-page: 865 -->

2.
Most game worlds contain a large number of objects, and the number
of intersection tests required grows rapidly as the number of objects in-
creases.

To detect intersections between n objects, the brute-force technique would be
to test every possible pair of objects, yielding an O(n2) algorithm. However,
much more efficient algorithms are used in practice. Collision engines typ-
ically employ some form of spatial hashing (http://bit.ly/1fLtX1D), spatial
subdivision or hierarchical bounding volumes in order to reduce the number
of intersection tests that must be performed.

13.3.6.1
Temporal Coherency

One common optimization technique is to take advantage of temporal coher-
ency, also known as frame-to-frame coherency. When collidables are moving at
reasonable speeds, their positions and orientations are usually quite similar
from time step to time step. We can often avoid recalculating certain kinds
of information every frame by caching the results across multiple time steps.
For example, in Havok, collision agents (hkpCollisionAgent) are usually
persistent between frames, allowing them to reuse calculations from previous
time steps as long as the motion of the collidables in question hasn’t invali-
dated those calculations.

13.3.6.2
Spatial Partitioning

The basic idea of spatial partitioning is to greatly reduce the number of collid-
ables that need to be checked for intersection by dividing space into a number
of smaller regions. If we can determine (in an inexpensive manner) that a pair
of collidables do not occupy the same region, then we needn’t perform more-
detailed intersection tests on them.
Various hierarchical partitioning schemes, such as octrees, binary space
partitioning trees (BSPs), kd-trees or sphere trees, can be used to subdivide
space for the purposes of collision detection optimization. These trees sub-
divide space in different ways, but they all do so in a hierarchical fashion,
starting with a gross subdivision at the root of the tree and further subdivid-
ing each region until sufficiently fine-grained regions have been obtained. The
tree can then be walked in order to find and test groups of potentially collid-
ing objects for actual intersections. Because the tree partitions space, we know
that when we traverse down one branch of the tree, the objects in that branch
cannot be colliding with objects in other sibling branches.


<!-- source-pdf-page: 866 -->

13.3.6.3
Broad Phase, Midphase and Narrow Phase

Havok uses a three-tiered approach to prune the set of collidables that need to
be tested for collisions during each time step.

•
First, gross AABB tests are used to determine which collidables are po-
tentially intersecting. This is known as broad phase collision detection.

•
Second, the coarse bounding volumes of compound shapes are tested.
This is known as midphase collision detection. For example, in a com-
pound shape composed of three spheres, the bounding volume might
be a fourth, larger sphere that encloses the other spheres. A compound
shape may contain other compound shapes, so in general a compound
collidable has a bounding volume hierarchy. The midphase traverses
this hierarchy in search of subshapes that are potentially intersecting.

•
Finally, the collidables’ individual primitives are tested for intersection.
This is known as narrow phase collision detection.

The Sweep and Prune Algorithm

In all of the major collision/physics engines (e.g., Havok, ODE, PhysX), broad
phase collision detection employs an algorithm known as sweep and prune
(http://en.wikipedia.org/wiki/Sweep_and_prune). The basic idea is to sort
the minimum and maximum dimensions of the collidables’ AABBs along the
three principal axes, and then check for overlapping AABBs by traversing the
sorted lists. Sweep and prune algorithms can make use of frame-to-frame co-
herency (see Section 13.3.6.1) to reduce an O(n log n) sort operation to an ex-
pected O(n) running time. Frame coherency can also aid in the updating of
AABBs when objects rotate.

### 13.3.7 Collision Queries

Another responsibility of the collision detection system is to answer hypotheti-
cal questions about the collision volumes in the game world. Examples include
the following:

•
If a bullet travels from the player’s weapon in a given direction, what is
the first target it will hit, if any?

•
Can a vehicle move from point A to point B without striking anything
along the way?

•
Find all enemy objects within a given radius of a character.

In general, such operations are known as collision queries.


<!-- source-pdf-page: 867 -->

The most common kind of query is a collision cast, sometimes just called a
cast. (The terms trace and probe are other common synonyms for “cast.”) A cast
determines what, if anything, a hypothetical object would hit if it were to be
placed into the collision world and moved along a ray or line segment. Casts
are different from regular collision detection operations because the entity be-
ing cast is not really in the collision world—it cannot affect the other objects in
the world in any way. This is why we say that a collision cast answers hypo-
thetical questions about the collidables in the world.

13.3.7.1
Ray Casting

The simplest type of collision cast is a ray cast, although this name is actually a
bit of a misnomer. What we’re really casting is a directed line segment—in other
words, our casts always have a start point (p0) and an end point (p1). The cast
line segment is tested against the collidable objects in the collision world. If it
intersects any of them, the contact point or points are returned.
Ray casting systems typically describe the line segment via its start point
p0 and a delta vector d that, when added to p0, yields the end point p1. Any
point on this line segment can be found via the following parametric equation,
where the parameter t is permitted to vary between zero and one:

p(t) = p0 + td,
t ∈[0, 1].

Clearly, p0 = p(0) and p1 = p(1). In addition, any contact point along the
segment can be uniquely described by specifying the value of the parame-
ter t corresponding to the contact. Most ray casting APIs return their contact
points as “t values,” or they permit a contact point to be converted into its
corresponding t by making an additional function call.
Most collision detection systems are capable of returning the earliest con-
tact—i.e., the contact point that lies closest to p0 and corresponds to the small-
est value of t. Some systems are also capable of returning a complete list of all
collidables that were intersected by the ray or line segment. The information
returned for each contact typically includes the t value, some kind of unique
identifier for the collidable entity that was hit, and possibly other information
such as the surface normal at the point of contact or other relevant proper-
ties of the shape or surface that was struck. One possible contact point data
structure is shown below.

struct RayCastContact
{
F32
m_t;
// the t value for this
// contact


<!-- source-pdf-page: 868 -->
> Visual fallback for diagrams/images: [PDF page 868](../../../visual_pages/page_0868.jpg)

U32
m_collidableId; // which collidable did we
// hit?

Vector m_normal;
// surface normal at
// contact pt.

// other information...
};

Applications of Ray Casts

Ray casts are used heavily in games. For example, we might want to ask the
collision system whether character A has a direct line of sight to character B.
To determine this, we simply cast a directed line segment from the eyes of
character A to the chest of character B. If the ray hits character B, we know that
A can “see” B. But if the ray strikes some other object before reaching character
B, we know that the line of sight is being blocked by that object. Ray casts
are used by weapon systems (e.g., to determine bullet hits), player mechanics
(e.g., to determine whether or not there is solid ground beneath the character’s
feet), AI systems (e.g., line of sight checks, targeting, movement queries, etc.),
vehicle systems (e.g., to locate and snap the vehicle’s tires to the terrain) and
so on.

13.3.7.2
Shape Casting

Another common query involves asking the collision system how far an imagi-
nary convex shape would be able to travel along a directed line segment before
it hits something solid. This is known as a sphere cast when the volume being
cast is a sphere, or a shape cast in general. (Havok calls them linear casts.) As
with ray casts, a shape cast is usually described by specifying the start point
p0, the distance to travel d and of course the type, dimensions and orientation
of the shape we wish to cast.
There are two cases to consider when casting a convex shape.

1.
The cast shape is already interpenetrating or contacting at least one other
collidable, preventing it from moving away from its starting location.

2.
The cast shape is not intersecting with anything else at its starting loca-
tion, so it is free to move a nonzero distance along its path.

In the first scenario, the collision system typically reports the contact(s)
between the cast shape and all of the collidables with which it is initially in-
terpenetrating. These contacts might be inside the cast shape or on its surface,
as shown in Figure 13.19.


<!-- source-pdf-page: 869 -->
> Visual fallback for diagrams/images: [PDF page 869](../../../visual_pages/page_0869.jpg)

In the second case, the shape can move a nonzero distance along the line
segment before striking something. Presuming that it hits something, it will
usually hit only a single collidable. However, it is possible for a cast shape
to strike more than one collidable simultaneously if its trajectory is just right.
And of course, if the impacted collidable is a non-convex poly soup, the cast
shape may end up touching more than one part of the poly soup simultane-
ously. We can safely say that no matter what kind of convex shape is cast, it
is possible for the cast to generate multiple contact points. The contacts will al-
ways be on the surface of the cast shape in this case, never inside it (because we
know that the cast shape was not interpenetrating anything when it started its
journey). This case is illustrated in Figure 13.20.
As with ray casts, some shape casting APIs report only the earliest contact
experienced by the cast shape, while others allow the shape to continue along
its hypothetical path, returning all the contacts it experiences on its journey.
This is illustrated in Figure 13.21.
The contact information returned by a shape cast is necessarily a bit more
complex than it is for a ray cast. We cannot simply return one or more t val-
ues, because a t value only describes the location of the center point of the
shape along its path. It tells us nothing of where, on the surface or interior
of the shape, it came into contact with the impacted collidable. As a result,
most shape casting APIs return both a t value and the actual contact point,
along with other relevant information (such as which collidable was struck,
the surface normal at the contact point, etc.).
Unlike ray casting APIs, a shape casting system must always be capable of
reporting multiple contacts. This is because even if we only report the contact
with the earliest t value, the shape may have touched multiple distinct collid-
ables in the game world, or it may be touching a single non-convex collidable
at more than one point. As a result, collision systems usually return an array

Figure 13.19.
A cast
sphere that starts in
penetration will be un-
able to move, and possi-
bly many contact points
will lie inside the cast
shape in general.

d

d

Contact

Contacts

Figure 13.20. If the starting location of a cast shape is not interpenetrating anything, then the
shape will move a nonzero distance along its line segment, and its contacts (if any) will always be
on its surface.


<!-- source-pdf-page: 870 -->
> Visual fallback for diagrams/images: [PDF page 870](../../../visual_pages/page_0870.jpg)

Contact 2
Contact 3

d

Contact 1

Figure 13.21. A shape casting API might return all contacts instead of only the earliest contact.

or list of contact point data structures, each of which might look something
like this:

struct ShapeCastContact
{
F32
m_t;
// the t value for this
// contact

U32
m_collidableId; // which collidable did we
// hit?

Point
m_contactPoint; // location of actual
// contact

Vector m_normal;
// surface normal at
// contact pt.

// other information...
};

Given a list of contact points, we often want to distinguish between the
groups of contact points for each distinct t value. For example, the earliest
contact is actually described by the group of contact points that all share the
minimum t in the list. It’s important to realize that collision systems may or
may not return their contact points sorted by t. If it does not, it’s almost always
a good idea to sort the results by t manually. This ensures that if one looks at
the first contact point in the list, it will be guaranteed to be among the earliest
contact points along the shape’s path.


<!-- source-pdf-page: 871 -->

Applications of Shape Casts

Shape casts are extremely useful in games. Sphere casts can be used to deter-
mine whether the virtual camera is in collision with objects in the game world.
Sphere or capsule casts are also commonly used to implement character move-
ment. For example, in order to slide the character forward on uneven terrain,
we can cast a sphere or capsule that lies between the character’s feet in the di-
rection of motion. We can adjust it up or down via a second cast, to ensure that
it remains in contact with the ground. If the sphere hits a very short vertical
obstruction, such as a street curb, it can “pop up” over the curb. If the vertical
obstruction is too tall, like a wall, the cast sphere can be slid horizontally along
the wall. The final resting place of the cast sphere becomes the character’s new
location next frame.

13.3.7.3
Phantoms

Sometimes, games need to determine which collidable objects lie within some
specific volume in the game world. For example, we might want the list of all
enemies that are within a certain radius of the player character. Havok sup-
ports a special kind of collidable object known as a phantom for this purpose.
A phantom acts much like a shape cast whose distance vector d is zero.
At any moment, we can ask the phantom for a list of its contacts with other
collidables in the world. It returns this data in essentially the same format that
would be returned by a zero-distance shape cast.
However, unlike a shape cast, a phantom is persistent in the collision
world. This means that it can take full advantage of the temporal coherency
optimizations used by the collision engine when detecting collisions between
“real” collidables. In fact, the only difference between a phantom and a reg-
ular collidable is that it is “invisible” to all other collidables in the collision
world (and it does not take part in the dynamics simulation). This allows it to
answer hypothetical questions about what objects it would collide with were
it a “real” collidable, but it is guaranteed not to have any effect on the other
collidables—including other phantoms—in the collision world.

13.3.7.4
Other Types of Queries

Some collision engines support other kinds of queries in addition to casts. For
example, Havok supports closest point queries, which are used to find the set of
points on other collidables that are closest to a given collidable in the collision
world.

### 13.3.8 Collision Filtering

It is quite common for game developers to want to enable or disable collisions
between certain kinds of objects. For example, most objects are permitted to


<!-- source-pdf-page: 872 -->

pass through the surface of a body of water—we might employ a buoyancy
simulation to make them float, or they might just sink to the bottom, but in
either case we do not want the water’s surface to appear solid. Most collision
engines allow contacts between collidables to be accepted or rejected based on
game-specific critiera. This is known as collision filtering.

13.3.8.1
Collision Masking and Layers

One common filtering approach is to categorize the objects in the world and
then use a look-up table to determine whether certain categories are permitted
to collide with one another or not. For example, in Havok, a collidable can be
a member of one (and only one) collision layer. The default collision filter in
Havok, represented by an instance of the class hkpGroupFilter, maintains
a 32-bit mask for each layer, each bit of which tells the system whether or not
that particular layer can collide with one of the other layers.

13.3.8.2
Collision Callbacks

Another filtering technique is to arrange for the collision library to invoke
a callback function whenever a collision is detected.
The callback can in-
spect the specifics of the collision and make the decision to either allow
or reject the collision based on suitable criteria.
Havok also supports this
kind of filtering.
When contact points are first added to the world, the
contactPointAdded() callback is invoked. If the contact point is later de-
termined to be valid (it may not be if an earlier TOI contact was found), the
contactPointConfirmed() callback is invoked. The application may re-
ject contact points in these callbacks if desired.

13.3.8.3
Game-Speciﬁc Collision Materials

Game developers often need to categorize the collidable objects in the game
world, in part to control how they collide (as with collision filtering) and in part
to control other secondary effects, such as the sound that is made or the particle
effect that is generated when one type of object hits another. For example,
we might want to differentiate between wood, stone, metal, mud, water and
human flesh.
To accomplish this, many games implement a collision shape categoriza-
tion mechanism similar in many respects to the material system used in the
rendering engine. In fact, some game teams use the term collision material to
describe this categorization. The basic idea is to associate with each collidable
surface a set of properties that defines how that particular surface should be-
have from a physical and collision standpoint. Collision properties can include
sound and particle effects, physical properties like coefficient of restitution or


<!-- source-pdf-page: 873 -->

friction coefficients, collision filtering information and whatever other infor-
mation the game might require.
For simple convex primitives, the collision properties are usually associ-
ated with the shape as a whole. For polygon soup shapes, the properties might
be specified on a per-triangle basis. Because of this latter usage, we usually try
to keep the binding between the collision primitive and its collision material
as compact as possible. A typical approach is to bind collision primitives to
collision materials via an 8-, 16- or 32-bit integer, or a pointer to the material
data. This integer indexes into a global array of data structures containing the
detailed collision properties themselves.

## 13.4 Rigid Body Dynamics

In a game engine, we are particularly concerned with the kinematics of obj-
ects—how they move over time. Many game engines include a physics sys-
tem for the purposes of simulating the motion of the objects in the virtual
game world in a somewhat physically realistic way. Technically speaking,
game physics engines are typically concerned with a particular field of physics
known as dynamics. This is the study of how forces affect the movement of ob-
jects. Until very recently, game physics systems have been focused almost
exclusively on a specific subdiscipline known as classical rigid body dynamics.
This name implies that in a game’s physics simulation, two important simpli-
fying assumptions are made:

•
Classical (Newtonian) mechanics.
The objects in the simulation are as-
sumed to obey Newton’s laws of motion. The objects are large enough
that there are no quantum effects, and their speeds are low enough that
there are no relativistic effects.

•
Rigid bodies. All objects in the simulation are perfectly solid and cannot
be deformed. In other words, their shape is constant. This idea meshes
well with the assumptions made by the collision detection system. Fur-
thermore, the assumption of rigidity greatly simplifies the mathematics
required to simulate the dynamics of solid objects.

Game physics engines are also capable of ensuring that the motions of the
rigid bodies in the game world conform to various constraints. The most com-
mon constraint is that of non-penetration—in other words, objects aren’t al-
lowed to pass through one another. Hence the physics system attempts to
provide realistic collision responses whenever bodies are found to be interpen-


<!-- source-pdf-page: 874 -->

etrating.2 This is one of the primary reasons for the tight interconnection be-
tween the physics engine and the collision detection system.
Most physics systems also allow game developers to set up other kinds of
constraints in order to define realistic interactions between physically simu-
lated rigid bodies. These may include hinges, prismatic joints (sliders), ball
joints, wheels, “rag dolls” to emulate unconscious or dead characters and so
on.
The physics system usually shares the collision world data structure, and
in fact it usually drives the execution of the collision detection algorithm as
part of its time step update routine. There is typically a one-to-one mapping
between the rigid bodies in the dynamics simulation and the collidables man-
aged by the collision engine. For example, in Havok, an hkpRigidBody ob-
ject maintains a reference to one and only one hkpCollidable (although it
is possible to create a collidable that has no rigid body). In PhysX, the two
concepts are a bit more tightly integrated—an NxActor serves both as a coll-
idable object and as a rigid body for the purposes of the dynamics simulation.
These rigid bodies and their corresponding collidables are usually maintained
in a singleton data structure known as the collision/physics world, or sometimes
just the physics world.
The rigid bodies in the physics engine are typically distinct from the logi-
cal objects that make up the virtual world from a gameplay perspective. The
positions and orientations of game objects can be driven by the physics sim-
ulation. To accomplish this, we query the physics engine every frame for the
transform of each rigid body, and apply it in some way to the transform of
the corresponding game object. It’s also possible for a game object’s motion,
as determined by some other engine system (such as the animation system or
the character control system) to drive the position and rotation of a rigid body
in the physics world. As mentioned in Section 13.3.1, a single logical game ob-
ject may be represented by one rigid body in the physics world, or by many.
A simple object like a rock, weapon or barrel, might correspond to one rigid
body. But an articulated character or a complex machine might be composed
of many interconnected rigid pieces.
The remainder of this chapter will be devoted to investigating how game
physics engines work. We’ll briefly introduce the theory that underlies rigid
body dynamics simulations. Then we’ll investigate some of the most common
features of a game physics system and have a look at how a physics engine
might be integrated into a game.

2Or in the case of continuous collision detection, the collision response actually prevents the
penetration from occurring.


<!-- source-pdf-page: 875 -->

### 13.4.1 Some Foundations

A great many excellent books, articles and slide presentations have been writ-
ten on the topic of classical rigid body dynamics. A solid foundation in ana-
lytical mechanics theory can be obtained from [17]. Even more relevant to our
discussion are texts like [39], [13] and [29], which have been written specifically
about the kind of physics simulations done by games. Other texts, like [2], [11]
and [32], include chapters on rigid body dynamics for games. Chris Hecker
wrote a series of helpful articles on the topic of game physics for Game Devel-
oper Magazine; Chris has posted these and a variety of other useful resources at
http://chrishecker.com/Rigid_Body_Dynamics. An informative slide presen-
tation on dynamics simulation for games was produced by Russell Smith, the
primary author of ODE; it is available at http://www.ode.org/slides/parc/
dynamics.pdf.
In this section, I’ll summarize the fundamental theoretical concepts that
underlie the majority of game physics engines. This will be a whirlwind tour
only, and by necessity I’ll have to omit some details. Once you’ve read this
chapter, I strongly encourage you to read at least a few of the additional re-
sources cited previously.

13.4.1.1
Units

Most rigid body dynamics simulations operate in the MKS system of units. In
this system, distance is measured in meters (abbreviated “m”), mass is mea-
sured in kilograms (abbreviated “kg”) and time is measured in seconds (ab-
breviated “s”). Hence the name MKS.
You could configure your physics system to use other units if you wanted
to, but if you do this, you need to make sure everything in the simulation
is consistent. For example, constants like the acceleration due to gravity g,
which is measured in m/s2 in the MKS system, would have to be re-expressed
in whatever unit system you select. Most game teams just stick with MKS to
keep life simple.

13.4.1.2
Separability of Linear and Angular Dynamics

An unconstrained rigid body is one that can translate freely along all three
Cartesian axes and that can rotate freely about these three axes as well. We
say that such a body has six degrees of freedom (DOF).
It is perhaps somewhat surprising that the motion of an unconstrained
rigid body can be separated into two independent components:

•
Linear dynamics. This is a description of the motion of the body when
we ignore all rotational effects. (We can use linear dynamics alone to


<!-- source-pdf-page: 876 -->

describe the motion of an idealized point mass—i.e., a mass that is in-
finitesimally small and cannot rotate.)

•
Angular dynamics. This is a description of the rotational motion of the
body.

As you can well imagine, this ability to separate the linear and angular
components of a rigid body’s motion is extremely helpful when analyzing or
simulating its behavior. It means that we can calculate a body’s linear motion
without regard to rotation—as if it were an idealized point mass—and then
layer its angular motion on top in order to arrive at a complete description of
the body’s motion.

13.4.1.3
Center of Mass

For the purposes of linear dynamics, an unconstrained rigid body acts as
though all of its mass were concentrated at a single point known as the center
of mass (abbreviated CM, or sometimes COM). The center of mass is essentially
the balancing point of the body for all possible orientations. In other words,
the mass of a rigid body is distributed evenly around its center of mass in all
directions.
For a body with uniform density, the center of mass lies at the centroid of the
body. That is, if we were to divide the body up into N very small pieces, add up
the positions of all these pieces as a vector sum and then divide by the number
of pieces, we’d end up with a pretty good approximation to the location of the
center of mass. If the body’s density is not uniform, the position of each little
piece would need to be weighted by that piece’s mass, meaning that in general
the center of mass is really a weighted average of the pieces’ positions. So we
have

∑
∀i
mi
=
∑
∀i
miri

rCM =
∑
∀i
miri

m
,

where the symbol m represents the total mass of the body, and the symbol r
represents a radius vector or position vector—i.e., a vector extending from the
world-space origin to the point in question. (These sums become integrals in
the limit as the sizes and masses of the little pieces approach zero.)
The center of mass always lies inside a convex body, although it may actu-
ally lie outside the body if it is concave. (For example, where would the center
of mass of the letter “C” lie?)


<!-- source-pdf-page: 877 -->
> Visual fallback for diagrams/images: [PDF page 877](../../../visual_pages/page_0877.jpg)

### 13.4.2 Linear Dynamics

For the purposes of linear dynamics, the position of a rigid body can be fully
described by a position vector rCM that extends from the world-space origin
to the center of mass of the body, as shown in Figure 13.22. Since we’re using
the MKS system, position is measured in meters (m). For the remainder of
this discussion, we’ll drop the CM subscripts, as it is understood that we are
describing the motion of the body’s center of mass.

y

CM

x

Figure 13.22. For the purposes of linear dynamics, the position of a rigid body can be fully described
by the position of its center of mass.

13.4.2.1
Linear Velocity and Acceleration

The linear velocity of a rigid body defines the speed and direction in which the
body’s CM is moving. It is a vector quantity, typically measured in meters per
second (m/s). Velocity is the first time derivative of position, so we can write

v(t) = dr(t)

dt
= ˙r(t),

where the dot over the vector r denotes taking the derivative with respect to
time. Differentiating a vector is the same as differentiating each component
independently, so

vx(t) = drx(t)

dt
= ˙rx(t),

and so on for the y- and z-components.
Linear acceleration is the first derivative of linear velocity with respect to
time, or the second derivative of the position of a body’s CM versus time. Ac-
celeration is a vector quantity, usually denoted by the symbol a. So we can


<!-- source-pdf-page: 878 -->

write

a(t) = dv(t)

dt
= ˙v(t)

= d2r(t)

dt2
= ¨r(t).

13.4.2.2
Force and Momentum

A force is defined as anything that causes an object with mass to accelerate or
decelerate. A force has both a magnitude and a direction in space, so all forces
are represented by vectors. A force is often denoted by the symbol F. When N
forces are applied to a rigid body, their net effect on the body’s linear motion
is found by simply adding up the force vectors:

Fnet =
N
∑
i=1
Fi.

Newton’s famous Second Law states that force is proportional to accelera-
tion and mass:

F(t) = ma(t) = m¨r(t).
(13.2)

As Newton’s law implies, force is measured in units of kilogram-meters per
second squared (kg-m/s2). This unit is also called the Newton.
When we multiply a body’s linear velocity by its mass, the result is a quan-
tity known as linear momentum. It is customary to denote linear momentum
with the symbol p:

p(t) = mv(t).

When mass is constant, Equation (13.2) holds true. But if mass is not con-
stant, as would be the case for a rocket whose fuel is being gradually used up
and converted into energy, Equation (13.2) is not exactly correct. The proper
formulation is actually as follows:

dt
= d(m(t)v(t))

F(t) = dp(t)

dt
.

which of course reduces to the more familiar F = ma when the mass is constant
and can be brought outside the derivative. Linear momentum is not of much
concern to us. However, the concept of momentum will become relevant when
we discuss angular dynamics.


<!-- source-pdf-page: 879 -->

### 13.4.3 Solving the Equations of Motion

The central problem in rigid body dynamics is to solve for the motion of the
body, given a set of known forces acting on it. For linear dynamics, this means
finding v(t) and r(t) given knowledge of the net force Fnet(t) and possibly
other information, such as the position and velocity at some previous time.
As we’ll see below, this amounts to solving a pair of ordinary differential
equations—one to find v(t) given a(t) and the other to find r(t) given v(t).

13.4.3.1
Force as a Function

A force can be constant, or it can be a function of time as shown above. A force
can also be a function of the position of the body, its velocity, or any number
of other quantities. So in general, the expression for force should really be
written as follows:
F(t, r(t), v(t), . . . ) = ma(t).
(13.3)

This can be rewritten in terms of the position vector and its first and second
derivatives as follows:

F(t, r(t), ˙r(t), . . . ) = m¨r(t).

For example, the force exerted by a spring is proportional to how far it has
been stretched away from its natural resting position. In one dimension, with
the spring’s resting position at x = 0, we can write

F(t, x(t)) = −kx(t),

where k is the spring constant, a measure of the spring’s stiffness.
As another example, the damping force exerted by a mechanical viscous
damper (a so-called dashpot) is proportional to the velocity of the damper’s
piston. So in one dimension, we can write

F(t, v(t)) = −bv(t),

where b is a viscous damping coefficient.

13.4.3.2
Ordinary Differential Equations

In general, an ordinary differential equation (ODE) is an equation involving a
function of one independent variable and various derivatives of that function.
If our independent variable is time and our function is x(t), then an ODE is a
relation of the form

dtn = f
(
t, x(t), dx(t)

)
.

dt2
, . . . , dn−1x(t)

dnx

dt
, d2x(t)

dtn−1


<!-- source-pdf-page: 880 -->

Put another way, the nth derivative of x(t) is expressed as a function f
whose arguments can be time (t), position (x(t)), and any number of deriva-
tives of x(t) as long as those derivatives are of lower order than n.
As we saw in Equation (13.3), force is a function of time, position and ve-
locity in general:

¨r(t) = 1

mF(t, r(t), ˙r(t)).

This clearly qualifies as an ODE. We wish to solve this ODE in order to find
v(t) and r(t).

13.4.3.3
Analytical Solutions

In some rare situations, the differential equations of motion can be solved an-
alytically, meaning that a simple, closed-form function can be found that de-
scribes the body’s position for all possible values of time t. A common example
is the vertical motion of a projectile under the influence of a constant acceler-
ation due to gravity, a(t) = [0, g, 0], where g = −9.8m/s2. In this case, the
ODE of motion boils down to
¨y(t) = g.

Integrating once yields
˙y(t) = gt + v0,

where v0 is the vertical velocity at time t = 0. Integrating a second time yields
the familiar solution
y(t) = 1

2 gt2 + v0t + y0,

where y0 is the initial vertical position of the object.
However, analytical solutions are almost never possible in game physics.
This is due in part to the fact that closed-form solutions to some differential
equations are simply not known. Moreover, a game is an interactive simula-
tion, so we usually cannot predict how the forces in a game will behave over
time. This makes it impossible to find simple, closed-form expressions for the
positions and velocities of the objects in the game as functions of time.
There are of course exceptions to this rule of thumb. For example, it’s pretty
common to solve for a closed-form expression in order to determine with what
velocity a projectile must be launched in order to hit a predefined target.

### 13.4.4 Numerical Integration

For the reasons cited above, game physics engines turn to a technique known
as numerical integration. With this technique, we solve our differential equa-
tions in a time-stepped manner—using the solution from a previous time step


<!-- source-pdf-page: 881 -->
> Visual fallback for diagrams/images: [PDF page 881](../../../visual_pages/page_0881.jpg)

to arrive at the solution for the next time step. The duration of the time step is
usually taken to be (roughly) constant and is denoted by the symbol ∆t. Given
that we know the body’s position and velocity at the current time t1 and that
the force is known as a function of time, position and/or velocity, we wish
to find the position and velocity at the next time step t2 = t1 + ∆t. In other
words, given r(t1), v(t1) and F(t, r, v), the problem is to find r(t2) and v(t2).

13.4.4.1
Explicit Euler

One of the simplest numerical solutions to an ODE is known as the explicit Eu-
ler method. This is the intuitive approach often taken by new game program-
mers. Let’s assume for the moment that we already know the current velocity
and that we wish to solve the following ODE to find the body’s position on
the next frame:
v(t) = ˙r(t).
(13.4)

Using the explicit Euler method, we simply convert the velocity from meters
per second into meters per frame by multiplying by the time delta, and then
we add “one frame’s worth” of velocity onto the current position in order to
find the new position on the next frame. This yields the following approximate
solution to the ODE given by Equation (13.4):

r(t2) = r(t1) + v(t1)∆t.
(13.5)

We can take an analogous approach to find the body’s velocity next frame
given the net force acting this frame. Hence, the approximate explicit Euler
solution to the ODE

a(t) = Fnet(t)

m
= ˙v(t)

is as follows:

v(t2) = v(t1) + Fnet(t)

m
∆t.
(13.6)

Interpretations of Explicit Euler

What we’re really doing in Equation (13.5) is assuming that the velocity of the
body is constant during the time step. Therefore, we can use the current ve-
locity to predict the body’s position on the next frame. The change in position
∆r between times t1 and t2 is hence ∆r = v(t1)∆t. Graphically, if we imagine
a plot of the position of the body versus time, we are taking the slope of the
function at time t1 (which is just v(t1)) and extrapolating it linearly to the next
time step t2. As we can see in Figure 13.23, linear extrapolation does not nec-
essarily provide us with a particularly good estimate of the true position at the


<!-- source-pdf-page: 882 -->
> Visual fallback for diagrams/images: [PDF page 882](../../../visual_pages/page_0882.jpg)

next time step r(t2), but it does work reasonably well as long as the velocity is
roughly constant.
Figure 13.23 suggests another way to interpret the explicit Euler method—
as an approximation of a derivative. By definition, any derivative is the quo-
tient of two infinitesimally small differences (in our case, dr/dt). The explicit
Euler method approximates this using the quotient of two finite differences. In
other words, dr becomes ∆r and dt becomes ∆t. This yields

dr
dt ≈∆r

∆t ;

v(t1) ≈r(t2) −r(t1)

t2 −t1
,

which again simplifies to Equation (13.5). This approximation is really only
valid when the velocity is constant over the time step. It is also valid in the limit
as ∆t tends toward zero (at which point it becomes exactly right). Obviously,
this same analysis can be applied to Equation (13.6) as well.

13.4.4.2
Properties of Numerical Methods

We’ve implied that the explicit Euler method is not particularly accurate. Let’s
pin this idea down more concretely. A numerical solution to an ordinary dif-
ferential equation actually has three important and interrelated properties:

•
Convergence. As the time step ∆t tends toward zero, does the approxi-
mate solution get closer and closer to the real solution?

•
Order. Given a particular numerical approximation to the solution of
an ODE, how “bad” is the error? Errors in numerical ODE solutions
are typically proportional to some power of the time step duration ∆t,
so they are often written using “big O” notation (e.g., O(∆t2)). We say

r(t)

r(t2)

rapprox(t2)

Δr

r(t1)

= v(t1)
Δr
Δt

Δt

t

t1
t2

Figure 13.23. In the explicit Euler method, the slope of r(t) at time t1 is used to linearly extrapolate
from r(t1) to an estimate of the true value of r(t2).


<!-- source-pdf-page: 883 -->

that a particular numerical method is of “order n” when its error term is
O(∆t(n+1)).
•
Stability. Does the numerical solution tend to “settle down” over time?
If a numerical method adds energy into the system, object velocities will
eventually “explode,” and the system will become unstable. On the other
hand, if a numerical method tends to remove energy from the system, it
will have an overall damping effect, and the system will be stable.

The concept of order warrants a little more explanation. We usually mea-
sure the error of a numerical method by comparing its approximate equation
with the infinite Taylor series expansion of the exact solution to the ODE. We
then cancel terms by subtracting the two equations. The remaining Taylor
terms represent the error inherent in the method. For example, the explicit
Euler equation is
r(t2) = r(t1) + ˙r(t1)∆t.

The infinite Taylor series expansion of the exact solution is

2 ¨r(t1)∆t2 + 1

6r(3)(t1)∆t3 + . . . ,

r(t2) = r(t1) + ˙r(t1)∆t + 1

where r(3) represents the third derivative with respect to time. Therefore, the
error is represented by all of the terms after the v∆t term, which is of order
O(∆t2) (because this term dwarfs the other higher-order terms):

2 ¨r(t1)∆t2 + 1

6r(3)(t1)∆t3 + . . .

E = 1

= O
(
∆t2)
.

To make the error of a method explicit, we’ll often write its equation with
the error term added in “big O” notation at the end. For example, the explicit
Euler method’s equation is most accurately written as follows:

r(t2) = r(t1) + ˙r(t1)∆t + O
(
∆t2)
.

We say that the explicit Euler method is a “first-order” method because it is
accurate up to and including the Taylor series term involving ∆t to the first
power. In general, if a method’s error term is O(∆t(n+1)), then it is said to be
an “order n” method.

13.4.4.3
Alternatives to Explicit Euler

The explicit Euler method sees quite a lot of use for simple integration tasks in
games, producing the best results when the velocity is nearly constant. How-
ever, it is not used in general-purpose dynamics simulations because of its


<!-- source-pdf-page: 884 -->

high error and poor stability. There are all sorts of other numerical meth-
ods for solving ODEs, including backward Euler (another first-order method),
midpoint Euler (a second-order method) and the family of Runge-Kutta
methods. (The fourth-order Runge-Kutta, often abbreviated “RK4,” is par-
ticularly popular.) We won’t describe these in any detail here, as you can
find voluminous amounts of information about them online and in the litera-
ture. The Wikipedia page http://en.wikipedia.org/wiki/Numerical_ordinary
_differential_equations serves as an excellent jumping-off point for learning
these methods.

13.4.4.4
Verlet Integration

The numerical ODE method most often used in interactive games these days is
probably the Verlet method, so I’ll take a moment to describe it in some detail.
There are actually two variants of this method: regular Verlet and the so-called
velocity Verlet. I’ll present both methods here, but I’ll leave the theory and deep
explanations to the myriad papers and Web pages available on the topic. (For
a start, check out http://en.wikipedia.org/wiki/Verlet_integration.)
The regular Verlet method is attractive because it achieves a high order
(low error), is relatively simple and inexpensive to evaluate, and produces a
solution for position directly in terms of acceleration in one step (as opposed
to the two steps normally required to go from acceleration to velocity and then
from velocity to position). The formula is derived by adding two Taylor series
expansions, one going forward in time and one going backward in time:

2 ¨r(t1)∆t2 + 1

6r(3)(t1)∆t3 + O(∆t4);

r(t1 + ∆t) = r(t1) + ˙r(t1)∆t + 1

2 ¨r(t1)∆t2 −1

6r(3)(t1)∆t3 + O(∆t4).

r(t1 −∆t) = r(t1) −˙r(t1)∆t + 1

Adding these expressions causes the negative terms to cancel with the corre-
sponding positive ones. The result gives us the position at the next time step
in terms of the acceleration and the two (known) positions at the current and
previous time steps. This is the regular Verlet method:

r(t1 + ∆t) = 2r(t1) −r(t1 −∆t) + a(t1)∆t2 + O(∆t4).

In terms of net force, the Verlet method becomes

r(t1 + ∆t) = 2r(t1) −r(t1 −∆t) + Fnet(t1)

m
∆t2 + O(∆t4).

The velocity is conspicuously absent from this expression. However, it
can be found using the following somewhat inaccurate approximation (among
other alternatives):

v(t1 + ∆t) = r(t1 + ∆t) −r(t1)

∆t
+ O(∆t).


<!-- source-pdf-page: 885 -->

13.4.4.5
Velocity Verlet

The more commonly used velocity Verlet method is a four-step process in which
the time step is divided into two parts to facilitate the solution. Given that
a(t1) = 1

mF
(
t1, r(t1), v(t1)
)
is known, we do the following:

1.
Calculate r(t1 + ∆t) = r(t1) + v(t1)∆t + 1

2a(t1)∆t2.

2.
Calculate v(t1 + 1

2∆t) = v(t1) + 1

2a(t1)∆t.

mF
(
t2, r(t2), v(t2)
)
.

3.
Determine a(t1 + ∆t) = a(t2) = 1

4.
Calculate v(t1 + ∆t) = v(t1 + 1

2∆t) + 1

2a(t1 + ∆t)∆t.

Notice in the third step that the force function depends on the position and
velocity on the next time step, r(t2) and v(t2). We already calculated r(t2)
in step 1, so we have all the information we need as long as the force is not
velocity-dependent. If it is velocity-dependent, then we must approximate
the next frame’s velocity, perhaps using the explicit Euler method.

### 13.4.5 Angular Dynamics in Two Dimensions

Up until now, we’ve focused on analyzing the linear motion of a body’s cen-
ter of mass (which acts as if it were a point mass). As I said earlier, an uncon-
strained rigid body will rotate about its center of mass. This means that we can
layer the angular motion of a body on top of the linear motion of its center of
mass in order to arrive at a complete description of the body’s overall motion.
The study of a body’s rotational motion in response to applied forces is called
angular dynamics.
In two dimensions, angular dynamics works almost identically to linear
dynamics. For each linear quantity, there’s an angular analog, and the math-
ematics works out quite neatly. So let’s investigate two-dimensional angular
dynamics first. As we’ll see, when we extend the discussion into three di-
mensions, things get a bit messier, but we’ll burn that bridge when we get to
it!

13.4.5.1
Orientation and Angular Speed

In two dimensions, every rigid body can be treated as a thin sheet of material.
(Some physics texts refer to such a body as a plane lamina.) All linear mo-
tion occurs in the xy-plane, and all rotations occur about the z-axis. (Visualize
wooden puzzle pieces sliding about on an air hockey table.)
The orientation of a rigid body in 2D is fully described by an angle θ, mea-
sured in radians relative to some agreed-upon zero rotation. For example, we


<!-- source-pdf-page: 886 -->

might specify that θ = 0 when a race car is facing directly down the positive
x-axis in world space. This angle is of course a time-varying function, so we
denote it θ(t).

13.4.5.2
Angular Speed and Acceleration

Angular velocity measures the rate at which a body’s rotation angle changes
over time. In two dimensions, angular velocity is a scalar, more correctly called
angular speed, since the term “velocity” really only applies to vectors. It is de-
noted by the scalar function ω(t) and measured in radians per second (rad/s).
Angular speed is the derivative of the orientation angle θ(t) with respect to
time:
Angular:
Linear:

ω(t) = dθ(t)

dt
= ˙θ(t)
v(t) = dr(t)

dt
= ˙r(t).

And as we’d expect, angular acceleration, denoted α(t) and measured in
radians per second squared (rad/s2), is the rate of change of angular speed:

Angular:
Linear:

α(t) = dω(t)

dt
= ˙ω(t) = ¨θ(t)
a(t) = dv(t)

dt
= ˙v(t) = ¨r(t).

13.4.5.3
Moment of Inertia

The rotational equivalent of mass is a quantity known as the moment of inertia.
Just as mass describes how easy or difficult it is to change the linear velocity
of a point mass, the moment of inertia measures how easy or difficult it is to
change the angular speed of a rigid body about a particular axis. If a body’s
mass is concentrated near an axis of rotation, it will be relatively easier to rotate
about that axis, and it will hence have a smaller moment of inertia than a body
whose mass is spread out away from that axis.
Since we’re focusing on two-dimensional angular dynamics right now, the
axis of rotation is always z, and a body’s moment of inertia is a simple scalar
value. Moment of inertia is usually denoted by the symbol I. We won’t get into
the details of how to calculate the moment of inertia here. For a full derivation,
see [17].

13.4.5.4
Torque

Until now, we’ve assumed that all forces are applied to the center of mass of a
rigid body. However, in general, forces can be applied at arbitrary points on a


<!-- source-pdf-page: 887 -->
> Visual fallback for diagrams/images: [PDF page 887](../../../visual_pages/page_0887.jpg)

body. If the line of action of a force passes through the body’s center of mass,
then the force will produce linear motion only, as we’ve already seen. Other-
wise, the force will introduce a rotational force known as a torque in addition
to the linear motion it normally causes. This is illustrated in Figure 13.24.

We can calculate torque using a cross product. First, we express the location
at which the force is applied as a vector r extending from the body’s center of
mass to the point of application of the force. (In other words, the vector r is in
body space, where the origin of body space is defined to be the center of mass.)
This is illustrated in Figure 13.25. The torque N caused by a force F applied at
a location r is

N = r × F.
(13.7)

Equation (13.7) implies that torque increases as the force is applied farther
from the center of mass. This explains why a lever can help us to move a heavy
object. It also explains why a force applied directly through the center of mass
produces no torque and no rotation—the magnitude of the vector r is zero in
this case.

When two or more forces are applied to a rigid body, the torque vectors
produced by each one can be summed, just as we can sum forces. So in general
we are interested in the net torque, Nnet.

In two dimensions, the vectors r and F must both lie in the xy-plane, so N
will always be directed along the positive or negative z-axis. As such, we’ll de-
note a two-dimensional torque via the scalar Nz, which is just the z-component
of the vector N.

Torque is related to angular acceleration and moment of inertia in much

F2

F1

Figure 13.24. On the left, a force applied to a body’s CM produces purely linear motion. On the
right, a force applied off-center will give rise to a torque, producing rotational motion as well as
linear motion.


<!-- source-pdf-page: 888 -->
> Visual fallback for diagrams/images: [PDF page 888](../../../visual_pages/page_0888.jpg)

Figure 13.25. Torque is calculated by taking the cross product between a force’s point of application
in body space (i.e., relative to the center of mass) and the force vector. The vectors are shown
here in two dimensions for ease of illustration; if it could be drawn, the torque vector would be
directed into the page.

the same way that force is related to linear acceleration and mass:

Angular:
Linear:

Nz(t) = Iα(t) = I ˙ω(t) = I ¨θ(t)
F(t) = ma(t) = m ˙v(t) = m¨r(t).
(13.8)

13.4.5.5
Solving the Angular Equations of Motion in Two Dimensions

For the two-dimensional case, we can solve the angular equations of motion
using exactly the same numerical integration techniques we applied to the lin-
ear dynamics problem. The pair of ODEs that we wish to solve is as follows:

Angular:
Linear:

Nnet(t) = I ˙ω(t)
Fnet(t) = m ˙v(t)

ω(t) = ˙θ(t)
v(t) = ˙r(t),

and their approximate explicit Euler solutions are

Angular:
Linear:

ω(t2) = ω(t1) + I−1Nnet(t1)∆t
v(t2) = v(t1) + m−1Fnet(t1)∆t

θ(t2) = θ(t1) + ω(t1)∆t
r(t2) = r(t1) + v(t1)∆t.

Of course, we could apply any of the other more-accurate numerical meth-
ods as well, such as the velocity Verlet method (I’ve omitted the linear case
here for compactness, but compare this to the steps given in Section 13.4.4.5):

1.
Calculate θ(t1 + ∆t) = θ(t1) + ω(t1)∆t + 1

2α(t1)∆t2.

2.
Calculate ω(t1 + 1

2∆t) = ω(t1) + 1

2α(t1)∆t.


<!-- source-pdf-page: 889 -->

3.
Calculate α(t1 + ∆t) = α(t2) = I−1Nnet
(
t2, θ(t2), ω(t2)
)
.

4.
Calculate ω(t1 + ∆t) = ω(t1 + 1

2∆t) + 1

2α(t1 + ∆t)∆t.

### 13.4.6 Angular Dynamics in Three Dimensions

Angular dynamics in three dimensions is a somewhat more complex topic
than its two-dimensional counterpart, although the basic concepts are of
course very similar. In the following section, I’ll give a very brief overview
of how angular dynamics works in 3D, focusing primarily on the things that
are typically confusing to someone who is new to the topic. For further in-
formation, check out Glenn Fiedler’s series of articles on the topic, available
at http://gafferongames.com/game-physics/physics-in-3d/. Another help-
ful resource is the paper entitled “An Introduction to Physically Based Model-
ing” by David Baraff of the Robotics Institute at Carnegie Mellon University,
available at http://www-2.cs.cmu.edu/~baraff/sigcourse/notesd1.pdf.

13.4.6.1
The Inertia Tensor

A rigid body may have a very different distribution of mass about the three co-
ordinate axes. As such, we should expect a body to have different moments of
inertia about different axes. For example, a long thin rod should be relatively
easy to make rotate about its long axis because all the mass is concentrated
very close to the axis of rotation. Likewise, the rod should be relatively more
difficult to make rotate about its short axis because its mass is spread out far-
ther from the axis. This is indeed the case, and it is why a figure skater spins
faster when she tucks her limbs in close to her body.
In three dimensions, the rotational mass of a rigid body is represented by a
3 × 3 matrix known as its inertia tensor. It is usually represented by the symbol
I (as before, we won’t describe how to calculate the inertia tensor here; see [17]
for details):






Ixx
Ixy
Ixz
Iyx
Iyy
Iyz
Izx
Izy
Izz

.

I =

The elements lying along the diagonal of this matrix are the moments of
inertia of the body about its three principal axes, Ixx, Iyy and Izz. The off-
diagonal elements are called products of inertia. They are zero when the body
is symmetrical about all three principal axes (as would be the case for a rect-
angular box). When they are nonzero, they tend to produce physically real-
istic yet somewhat unintuitive motions that the average game player would
probably think were “wrong” anyway. Therefore, the inertia tensor is often


<!-- source-pdf-page: 890 -->

simplified down to the three-element vector
[Ixx
Iyy
Izz
]
in game physics
engines.

13.4.6.2
Orientation in Three Dimensions

In two dimensions, we know that the orientation of a rigid body can be de-
scribed by a single angle θ, which measures rotation about the z-axis (assum-
ing the motion is taking place in the xy-plane). In three dimensions, a body’s
orientation could be represented using three Euler angles
[θx
θy
θz
]
, each
representing the body’s rotation about one of the three Cartesian axes. How-
ever, as we saw in Chapter 5, Euler angles suffer from gimbal lock problems
and can be difficult to work with mathematically. Therefore, the orientation
of a body is more often represented using either a 3 × 3 matrix R or a unit
quaternion q. We’ll use the quaternion form exclusively in this chapter.
Recall that a quaternion is a four-element vector whose x-, y- and z-com-
ponents can be interpreted as a unit vector u lying along the axis of rotation,
scaled by the sine of the half-angle and whose w component is the cosine of
the half-angle:

q =
[qx
qy
qz
qw
]

=
[q
qw
]

=
[
u sin θ

2
]
.

2
cos θ

A body’s orientation is of course a function of time, so we should write it q(t).
Again, we need to select an arbitrary direction to be our zero rotation. For
example, we might say that by default, the front of every object will lie along
the positive z-axis in world space, with y up and x to the left. Any non-identity
quaternion will serve to rotate the object away from this canonical world-space
orientation. The choice of the canonical orientation is arbitrary, but of course
it’s important to be consistent across all assets in the game.

13.4.6.3
Angular Velocity and Momentum in Three Dimensions

In three dimensions, angular velocity is a vector quantity, denoted by ω(t).
The angular velocity vector can be visualized as a unit-length vector u that
defines the axis of rotation, scaled by the two-dimensional angular velocity
ωu = ˙θu of the body about the u-axis. Hence,

ω(t) = ωu(t)u = ˙θu(t)u.

In linear dynamics, we saw that if there are no forces acting on a body,
then the linear acceleration is zero, and linear velocity is constant. In two-
dimensional angular dynamics, this again holds true: If there are no torques


<!-- source-pdf-page: 891 -->
> Visual fallback for diagrams/images: [PDF page 891](../../../visual_pages/page_0891.jpg)

Figure 13.26. A rectangular object that is spun about its shortest or longest axis has a constant angular velocity vector.
However, when spun about its medium-sized axis, the direction of the angular velocity vector changes wildly.

acting on a body in two dimensions, then the angular acceleration α is zero,
and the angular speed ω about the z-axis is constant.

Unfortunately, this is not the case in three dimensions. It turns out that even
when a rigid body is rotating in the absence of all forces, its angular velocity
vector ω(t) may not be constant because the axis of rotation can continually
change direction. You can see this effect in action when you try to spin a rect-
angular object, like a block of wood, in mid-air in front of you. If you throw
the block so that it is rotating about its shortest axis, it will spin in a stable way.
The orientation of the axis stays roughly constant. The same thing happens if
you try to spin the block about its longest axis. But if you try to spin the block
around the remaining axis (the one that’s neither the shortest nor the longest),
the rotation will be utterly unstable. (Try it! Go steal a wooden block from
a baby and spin it in various ways. On second thought, make sure to give it
back when you’re done.) The axis of rotation itself changes direction wildly as
the object spins. This is illustrated in Figure 13.26.

The fact that the angular velocity vector can change in the absence of
torques is another way of saying that angular velocity is not conserved. How-
ever, a related quantity called the angular momentum does remain constant in
the absence of forces and hence is conserved. Angular momentum is the rota-
tional equivalent of linear momentum:

Angular:
Linear:

L(t) = Iω(t)
p(t) = mv(t).

Like the linear case, angular momentum L(t) is a three-element vector.
However, unlike the linear case, rotational mass (the inertia tensor) is not a
scalar but rather a 3 × 3 matrix. As such, the expression Iω is computed via a


<!-- source-pdf-page: 892 -->

matrix multiplication:













ωx(t)
ωy(t)
ωz(t)


Lx(t)
Ly(t)
Lz(t)


Ixx
Ixy
Ixz
Iyx
Iyy
Iyz
Izx
Izy
Izz



.

=

Because the angular velocity ω is not conserved, we do not treat it as a pri-
mary quantity in our dynamics simulations the way we do the linear velocity
v. Instead, we treat angular momentum L as the primary quantity. The angu-
lar velocity is a secondary quantity, determined only after we have determined
the value of L at each time step of the simulation.

13.4.6.4
Torque in Three Dimensions

In three dimensions, we still calculate torque as the cross product between the
radial position vector of the point of force application and the force vector itself
(N = r × F). Equation (13.8) still holds, but we always write it in terms of the
angular momentum because angular velocity is not a conserved quantity:

N = Iα(t)

= Idω(t)

dt

dt
(
Iω(t)
)

= d

= dL(t)

dt
.

13.4.6.5
Solving the Equations of Angular Motion in Three Dimensions

When solving the equations of angular motion in three dimensions, we might
be tempted to take exactly the same approach we used for linear motion and
two-dimensional angular motion. We might guess that the differential equa-
tions of motion should be written

Angular 3D?
Linear:

Nnet(t) = I ˙ω(t)
Fnet(t) = m ˙v(t)

ω(t) = ˙θ(t)
v(t) = ˙r(t),

and using the explicit Euler method, we might guess that the approximate
solutions to these ODEs would look something like this:

Angular 3D?
Linear:

ω(t2) = ω(t1) + I−1Nnet(t1)∆t
v(t2) = v(t1) + m−1Fnet(t)∆t

θ(t2) = θ(t1) + ω(t1)∆t
r(t2) = r(t1) + v(t1)∆t.


<!-- source-pdf-page: 893 -->

However, this is not actually correct. The differential equations of three-dimen-
sional angular motion differ from their linear and two-dimensional angular
counterparts in two important ways:

1.
Instead of solving for the angular velocity ω, we solve for the angular
momentum L directly. We then calculate the angular velocity vector as a
secondary quantity using I and L. We do this because angular momen-
tum is conserved, while angular velocity is not.

2.
When solving for the orientation given the angular velocity, we have a
problem: The angular velocity is a three-element vector, while the orien-
tation is a four-element quaternion. How can we write an ODE relating
a quaternion to a vector? The answer is that we cannot, at least not di-
rectly. But what we can do is convert the angular velocity vector into
quaternion form and then apply a slightly odd-looking equation that re-
lates the orientation quaternion to the angular velocity quaternion.

It turns out that when we express a rigid body’s orientation as a quaternion,
the derivative of this quaternion is related to the body’s angular velocity vector
in the following way. First, we construct an angular velocity quaternion. This
quaternion contains the three components of the angular velocity vector in x,
y and z, with its w-component set to zero:

ω =
[
ωx
ωy
ωz
0
]
.

Now the differential equation relating the orientation quaternion to the angu-
lar velocity quaternion is (for reasons we won’t get into here) as follows:

dω(t)

dt
= ˙q(t) = 1

2ω(t)q(t).

It’s important to remember here that ω(t) is the angular velocity quaternion
as described above and that the product ω(t)q(t) is a quaternion product (see
Section 5.4.2.1 for details).
So, we actually need to write the ODEs of motion as follows (note that I’ve
recast the linear ODEs in terms of linear momentum as well, to underscore the
similarities between the two cases):

Angular 3D:
Linear:

Nnet(t) = ˙L(t)
Fnet(t) = ˙p(t)

ω(t) = I−1L(t)
v(t) = m−1p(t)

ω(t) =
[
ω(t)
0
]

v(t) = ˙r(t).

1
2ω(t)q(t) = ˙q(t)


<!-- source-pdf-page: 894 -->

Using the explicit Euler method, the final approximate solution to the angular
ODEs in three dimensions is actually as follows:

L(t2) = L(t1) + Nnet(t1)∆t
(vectors)

(
ri × Fi(t1)
)
;
(vectors)

= L(t1) + ∆t∑
∀i

ω(t2) =
[
I−1L(t2)
0
]
;
(quaternions)

q(t2) = q(t1) + 1

2ω(t1)q(t1)∆t.
(quaternions)

The orientation quaternion q(t) should be renormalized periodically to reverse
the effects of the inevitable accumulation of floating-point error.
As always, the explicit Euler method is being used here just as an example.
In a real engine, we would employ velocity Verlet, RK4 or some other more-
stable and more-accurate numerical method.

### 13.4.7 Collision Response

Everything we’ve discussed so far assumes that our rigid bodies are neither
colliding with anything, nor is their motion constrained in any other way.
When bodies collide with one another, the dynamics simulation must take
steps to ensure that they respond realistically to the collision and that they
are never left in a state of interpenetration after the simulation step has been
completed. This is known as collision response.

13.4.7.1
Energy

Before we discuss collision response, we must understand the concept of en-
ergy. When a force moves a body over a distance, we say that the force does
work. Work represents a change in energy—that is, a force either adds energy
to a system of rigid bodies (e.g., an explosion) or it removes energy from the
system (e.g., friction). Energy comes in two forms. The potential energy V
of a body is the energy it has simply because of where it is relative to a force
field such as a gravitational or a magnetic field. (For example, the higher up
a body is above the surface of the Earth, the more gravitational potential en-
ergy it has.) The kinetic energy of a body T represents the energy arising from
the fact that it is moving relative to other bodies in a system. The total energy
E = V + T of an isolated system of bodies is a conserved quantity, meaning that
it remains constant unless energy is being drained from the system or added
from outside the system.
The kinetic energy arising from linear motion can be written

2mv2,

Tlinear = 1


<!-- source-pdf-page: 895 -->

or in terms of the linear momentum and velocity vectors:

Tlinear = 1

2p · v.

Analogously, the kinetic energy arising from a body’s rotational motion is as
follows:
Tangular = 1

2L · ω.

Energy and its conservation can be extremely useful concepts when solving
all sorts of physics problems. We’ll see the role that energy plays in the deter-
mination of collision responses in the following section.

13.4.7.2
Impulsive Collision Response

When two bodies collide in the real world, a complex set of events takes place.
The bodies compress slightly and then rebound, changing their velocities and
losing energy to sound and heat in the process. Most real-time rigid body
dynamics simulations approximate all of these details with a simple model
based on an analysis of the momenta and kinetic energies of the colliding ob-
jects, called Newton’s law of restitution for instantaneous collisions with no friction.
It makes the following simplifying assumptions about the collision:

•
The collision force acts over an infinitesimally short period of time, turn-
ing it into what we call an idealized impulse. This causes the velocities of
the bodies to change instantaneously as a result of the collision.

•
There is no friction at the point of contact between the objects’ surfaces.
This is another way of saying that the impulse acting to separate the bod-
ies during the collision is normal to both surfaces—there is no tangen-
tial component to the collision impulse. (This is just an idealization of
course; we’ll get to friction in Section 13.4.7.5.)

•
The nature of the complex submolecular interactions between the bod-
ies during the collision can be approximated by a single quantity known
as the coefficient of restitution, customarily denoted by the symbol ε. This
coefficient describes how much energy is lost during the collision. When
ε = 1, the collision is perfectly elastic, and no energy is lost. (Picture two
billiard balls colliding in mid-air.) When ε = 0, the collision is perfectly
inelastic, also known as perfectly plastic and the kinetic energy of both
bodies is lost. The bodies will stick together after the collision, continu-
ing to move in the direction that their mutual center of mass had been
moving before the collision. (Picture pieces of putty being slammed to-
gether.)


<!-- source-pdf-page: 896 -->
> Visual fallback for diagrams/images: [PDF page 896](../../../visual_pages/page_0896.jpg)

All collision analysis is based around the idea that linear momentum is
conserved. So for two bodies 1 and 2, we can write

p1 + p2 = p′
1 + p′
2,
or
m1v1 + m2v2 = m′
1v′
1 + m′
2v′
2

where the primed symbols represent the momenta and velocities after the col-
lision. The kinetic energy of the system is conserved as well, but we must ac-
count for the energy lost due to heat and sound by introducing an additional
energy loss term Tlost:

2m′
1v′
1
2 + 1

2m′
2v′
2
2 + Tlost.

1
2m1v2
1 + 1

2m2v2
2 = 1

If the collision is perfectly elastic, the energy loss Tlost is zero. If it is perfectly
plastic, the energy loss is equal to the original kinetic energy of the system, the
primed kinetic energy sum becomes zero and the bodies stick together after
the collision.
To resolve a collision using Newton’s law of restitution, we apply an ideal-
ized impulse to the two bodies. An impulse is like a force that acts over an in-
finitesimally short period of time and thereby causes an instantaneous change
in the velocity of the body to which it is applied. We could denote an impulse
with the symbol ∆p, since it is a change in momentum (∆p = m∆v). How-
ever, most physics texts use the symbol ˆp (pronounced “p-hat”) instead, so
we’ll do the same.
Because we assume that there is no friction involved in the collision, the
impulse vector must be normal to both surfaces at the point of contact. In
other words, ˆp = ˆpn, where n is the unit vector normal to both surfaces. This is
illustrated in Figure 13.27. If we assume that the surface normal points toward
body 1, then body 1 experiences an impulse of ˆp, and body 2 experiences an
equal but opposite impulse −ˆp. Hence, the momenta of the two bodies after
the collision can be written in terms of their momenta prior to the collision and
the impulse ˆp as follows:

p′
1 = p1 + ˆp
p′
2 = p2 −ˆp

m1v′
1 = m1v1 + ˆp
m2v′
2 = m2v2 −ˆp
(13.9)

m1
n
v′
2 = v2 + ˆp

v′
1 = v1 + ˆp

m2
n.

The coefficient of restitution provides the key relationship between the rela-
tive velocities of the bodies before and after the collision. Given that the cen-
ters of mass of the bodies have velocities before the collision and afterward,
the coefficient of restitution ε is defined as follows:

(v′
2 −v′
1) = ε(v2 −v1).
(13.10)


<!-- source-pdf-page: 897 -->
> Visual fallback for diagrams/images: [PDF page 897](../../../visual_pages/page_0897.jpg)

n

Body 1
Body 2
p^

Figure 13.27. In a frictionless collision, the impulse acts along a line normal to both surfaces at the
point of contact. This line is deﬁned by the unit normal vector n.

Solving Equations (13.9) and (13.10) under the temporary assumption that
the bodies cannot rotate yields

ˆp = ˆpn = (ε + 1)(v2 · n −v1 · n)

n.

1
m1
+ 1

m2

Notice that if the coefficient of restitution is one (perfectly elastic collision)
and if the mass of body 2 is effectively infinite (as it would be for, say, a concrete
driveway), then (1/m2) = 0, v2 = 0, and this expression reduces to a reflection
of the other body’s velocity vector about the contact normal, as we’d expect:

ˆp = −2m1(v1 · n)n;

v′
1 = p1 + p2

m1

= m1v1 −2m1(v1 · n)n

m1
= v1 −2m1(v1 · n)n.

The solution gets a bit hairier when we take the rotations of the bodies
into account. In this case, we need to look at the velocities of the points of
contact on the two bodies rather than the velocities of their centers of mass,
and we need to calculate the impulse in such a way as to impart a realistic
rotational effect as a result of the collision. We won’t get into the details here,
but Chris Hecker’s article, available at http://chrishecker.com/images/e/e7/
Gdmphys3.pdf, does an excellent job of describing both the linear and the ro-
tational aspects of collision response. The theory behind collision response is
explained more fully in [17].


<!-- source-pdf-page: 898 -->

13.4.7.3
Penalty Forces

Another approach to collision response is to introduce imaginary forces called
penalty forces into the simulation. A penalty force acts like a stiff damped spring
attached to the contact points between two bodies that have just interpene-
trated. Such a force induces the desired collision response over a short but
finite period of time. Using this approach, the spring constant k effectively
controls the duration of the interpenetration, and the damping coefficient b
acts a bit like the restitution coefficient. When b = 0, there is no damping—no
energy is lost, and the collision is perfectly elastic. As b increases, the collision
becomes more plastic.
Let’s take a brief look at some of the pros and cons of the penalty force
approach to resolving collisions. On the positive side, penalty forces are easy
to implement and understand. They also work well when three or more bodies
are interpenetrating each other. This problem is very difficult to solve when
resolving collisions one pair at a time. A good example is the Sony PS3 demo
in which a huge number of rubber duckies are poured into a bathtub—the
simulation was nice and stable despite the very large number of collisions.
The penalty force method is a great way to achieve this.
Unfortunately, because penalty forces respond to penetration (i.e., relative
position) rather than to relative velocity, the forces may not align with the di-
rection we would intuitively expect, especially during a high-speed collision.
A classic example is a car driving head-on into a truck. The car is low while
the truck is tall. Using only the penalty force method, it is easy to arrive at a
situation in which the penalty force is vertical, rather than horizontal as we
would expect given the velocities of the two vehicles. This can cause the truck
to pop its nose up into the air while the car drives under it.
In general, the penalty force technique works well for low-speed impacts,
but it does not work well at all when objects are moving quickly. It is pos-
sible to combine the penalty force method with other collision resolution ap-
proaches in order to strike a balance between stability in the presence of large
numbers of interpenetrations and responsiveness and more-intuitive behavior
at high velocities.

13.4.7.4
Using Constraints to Resolve Collisions

As we’ll investigate in Section 13.4.8, most physics systems permit various
kinds of constraints to be imposed on the motion of the bodies in the simula-
tion. If collisions are treated as constraints that disallow object interpenetra-
tion, then they can be resolved by simply running the simulation’s general-
purpose constraint solver. If the constraint solver is fast and produces high-


<!-- source-pdf-page: 899 -->
> Visual fallback for diagrams/images: [PDF page 899](../../../visual_pages/page_0899.jpg)

quality visual results, this can be an effective way to resolve collisions.

13.4.7.5
Friction

Friction is a force that arises between two bodies that are in continuous contact,
resisting their movement relative to one another. There are a number of types
of friction. Static friction is the resistance one feels when trying to start a sta-
tionary object sliding along a surface. Dynamic friction is a resisting force that
arises when objects are actually moving relative to one another. Sliding fric-
tion is a type of dynamic friction that resists movement when an object slides
along a surface. Rolling friction is a type of static or dynamic friction that acts
at the point of contact between a wheel or other round object and the surface
it is rolling on. When the surface is very rough, the rolling friction is exactly
strong enough to cause the wheel to roll without sliding, and it acts as a form
of static friction. If the surface is somewhat smooth, the wheel may slip, and a
dynamic form of rolling friction comes into play. Collision friction is the friction
that acts instantaneously at the point of contact when two bodies collide while
moving. (This is the friction force that we ignored when discussing Newton’s
law of restitution in Section 13.4.7.1.) Various kinds of constraints can have fric-
tion as well. For example, a rusted hinge or axle might resist being turned by
introducing a friction torque.
Let’s look at an example to understand the essence of how friction works.
Linear sliding friction is proportional to the component of an object’s weight
that is acting normal to the surface on which it is sliding. The weight of an
object is just the force due to gravity, G = mg, which is always directed down-
ward. The component of this force normal to an inclined surface that makes
an angle θ with the horizontal is just GN = mg cos θ. The friction force f is
then
f = µmg cos θ,

where the constant of proportionality µ is called the coefficient of friction. This
force acts tangentially to the surface, in a direction opposite to the attempted
or actual motion of the object. This is illustrated in Figure 13.28.
Figure 13.28 also shows the component of the gravitational force acting tan-
gent to the surface, GT = mg sin θ. This force tends to make the object accel-
erate down the plane, but in the presence of sliding friction, it is counteracted
by f. Hence, the net force tangent to the surface is

Fnet = GT −f = mg(sin θ −µ cos θ).

If the angle of inclination is such that the expression in parentheses is zero, the
object will slide at a constant speed (if already moving) or be at rest. If the


<!-- source-pdf-page: 900 -->
> Visual fallback for diagrams/images: [PDF page 900](../../../visual_pages/page_0900.jpg)

expression is greater than zero, the object will accelerate down the surface. If
it is less than zero, the object will decelerate and eventually come to rest.

13.4.7.6
Welding

An additional problem arises when an object is sliding across a polygon soup.
Recall that a polygon soup is just what its name implies—a soup of essentially
unrelated polygons (usually triangles). As an object slides from one triangle
of this soup to the next, the collision detection system will generate additional
spurious contacts because it will think that the object is about to hit the edge
of the next triangle. This is illustrated in Figure 13.29.
There are a number of solutions to this problem. One is to analyze the
set of contacts and discard ones that appear to be spurious, based on various
heuristics and possibly some knowledge of the object’s contacts on a previous
frame (e.g., if we know the object was sliding along a surface and a contact
normal arises that is due to the object being near the edge of its current triangle,
then discard that contact normal). Versions of Havok prior to 4.5 employed
this approach.
Starting with Havok 4.5, a new technique was implemented that essentially
annotates the mesh with triangle adjacency information. The collision detec-
tion system therefore “knows” which edges are interior edges and can discard
spurious collisions reliably and quickly. Havok describes this solution as weld-
ing, because in effect the edges of the triangles in the poly soup are welded to
one another.

13.4.7.7
Coming to Rest, Islands and Sleeping

When energy is removed from a simulated system via friction, damping or
other means, moving objects will eventually come to rest. This seems like a
natural consequence of the simulation—something that would just “fall out”
of the differential equations of motion. Unfortunately, in a real computerized

| | =

mg cos

|
T| =
mg sin

|
N| =
mg cos

= m

Figure 13.28. The force of friction f is proportional to the normal component of the object’s
weight. The proportionality constant µ is called the coefﬁcient of friction.


<!-- source-pdf-page: 901 -->
> Visual fallback for diagrams/images: [PDF page 901](../../../visual_pages/page_0901.jpg)

Spurious Contacts

with Triangle Edge

Figure 13.29. When an object slides between two adjacent triangles, spurious contacts with the
new triangle’s edge can be generated.

simulation, coming to rest is never quite that simple. Various factors such
as floating-point error, inaccuracies in the calculation of restitution forces and
numerical instability can cause objects to jitter forever rather than coming to
rest as they should. For this reason, most physics engines use various heuristic
methods to detect when objects are oscillating instead of coming to rest as they
should. Additional energy can be removed from the system to ensure that such
objects eventually settle down, or they can simply be stopped abruptly once
their average velocity drops below a threshold.
When an object really does stop moving (finds itself in a state of equilib-
rium), there is no reason to continue integrating its equations of motion every
frame. To optimize performance, most physics engines allow dynamic objects
in the simulation to be put to sleep. This excludes them from the simulation tem-
porarily, although sleeping objects are still active from a collision standpoint.
If any force or impulse begins acting on a sleeping object, or if the object loses
one of the contacts that was holding it in equilibrium, it will be awoken so that
its dynamic simulation can be resumed.

Sleep Criteria

Various criteria can be used to determine whether or not a body qualifies for
sleep. It’s not always easy to make this determination in a robust manner for
all situations. For example, a long pendulum might have very low angular
momentum and yet still be moving visibly on-screen.
The most commonly used criteria for equilibrium detection include:

•
The body is supported. This means it has three or more contact points
(or one or more planar contacts) that allow it to attain equilibrium with
gravity and any other forces that might be affecting it.

•
The body’s linear and angular momentum are below a predefined thresh-
old.


<!-- source-pdf-page: 902 -->

•
A running average of the linear and angular momentum are below a pre-
defined threshold.

•
The total kinetic energy of the body (T =
1
2p · v + 1

2L · ω) is below a
predefined threshold. The kinetic energy is usually mass-normalized
so that a single threshold can be used for all bodies regardless of their
masses.

The motion of a body that is about to go to sleep might be progressively damped
so that it comes to a smooth stop rather than stopping abruptly.

Simulation Islands

Both Havok and PhysX further optimize their performance by automatically
grouping objects that either are interacting or have the potential to interact in
the near future into sets called simulation islands. Each simulation island can be
simulated independently of all the other islands—an approach that is highly
conducive to cache coherency optimizations and parallel processing.
Havok and PhysX both put entire islands to sleep rather than individual
rigid bodies. This approach has its pros and cons. The performance boost
is obviously larger when a whole group of interacting objects can be put to
sleep. On the other hand, if even one object in an island is awake, the entire
island is awake. Overall, it seems that the pros tend to outweigh the cons, so
the simulation island design is one we’re likely to continue to see in future
versions of these SDKs.

### 13.4.8 Constraints

An unconstrained rigid body has six degrees of freedom (DOF): It can trans-
late in three dimensions, and it can rotate about the three Cartesian axes. Con-
straints restrict an object’s motion, reducing its degrees of freedom either par-
tially or completely. Constraints can be used to model all sorts of interesting
behaviors in a game. Here are a few examples:

•
a swinging chandelier (point-to-point constraint);

•
a door that can be kicked, slammed, blown of its hinges (hinge con-
straint);

•
a vehicle’s wheel assembly (axle constraint with damped springs for sus-
pension);

•
a train or a car pulling a trailer (stiff spring/rod constraint);

•
a rope or chain (chain of stiff springs or rods); and


<!-- source-pdf-page: 903 -->
> Visual fallback for diagrams/images: [PDF page 903](../../../visual_pages/page_0903.jpg)

Figure 13.31. A stiff spring constraint requires that a point on body A be separated from a point on
body B by a user-speciﬁed distance.

•
a rag doll (specialized constraints that mimic the behavior of various
joints in the human skeleton).

In the sections that follow, we’ll briefly investigate these and some of the
other most common kinds of constraints typically provided by a physics SDK.

13.4.8.1
Point-to-Point Constraints

Figure
13.30.
A
point-to-point
constraint requires
that a point on body
A aligns with a point
on body B.

A point-to-point constraint is the simplest type of constraint. It acts like a ball-
and-socket joint—bodies can move in any way they like, as long as a specified
point on one body lines up with a specified point on the other body. This is
illustrated in Figure 13.30.

13.4.8.2
Stiff Springs

A stiff spring constraint is a lot like a point-to-point constraint except that it
keeps the two points separated by a specified distance. This kind of constraint
acts like an invisible rod between the two constrained points. Figure 13.31
illustrates this constraint.

13.4.8.3
Hinge Constraints

A hinge constraint limits rotational motion to only a single degree of freedom,
about the hinge’s axis. An unlimited hinge acts like an axle, allowing the con-
strained object to complete an unlimited number of full rotations. It’s common
to define limited hinges that can only move through a predefined range of an-
gles about the one allowed axis. For example, a one-way door can only move
through a 180 degree arc, because otherwise it would pass through the adja-
cent wall. Likewise, a two-way door is constrained to move through a ±180
degree arc. Hinge constraints may also be given a degree of friction in the
form of a torque that resists rotation about the hinge’s axis. A limited hinge
constraint is shown in Figure 13.32.


<!-- source-pdf-page: 904 -->
> Visual fallback for diagrams/images: [PDF page 904](../../../visual_pages/page_0904.jpg)

13.4.8.4
Prismatic Constraints

Prismatic constraints act like a piston: A constrained body’s motion is re-
stricted to a single translational degree of freedom. A prismatic constraint
may or may not permit rotation about the translation axis of the piston. Pris-
matic constraints can of course be limited or unlimited and may or may not
include friction. A prismatic constraint is illustrated in Figure 13.33.

13.4.8.5
Other Common Constraint Types

Many other types of constraints are possible, of course. Here are just a few
examples:

•
Planar. Objects are constrained to move in a two-dimensional plane.

•
Wheel.
This is typically a hinge constraint with unlimited rotation,
coupled with some form of vertical suspension simulated via a spring-
damper assembly.

•
Pulley. In this specialized constraint, an imaginary rope passes through
a pulley and is attached to two bodies. The bodies move along the line
of the rope via a leverage ratio.

Constraints may be breakable, meaning that after enough force is applied,
they automatically come apart. Alternatively, the game can turn the constraint
on and off at will, using its own criteria for when the constraint should break.

13.4.8.6
Constraint Chains

Long chains of linked bodies are sometimes difficult to simulate in a stable
manner because of the iterative nature of the constraint solver. A constraint
chain is a specialized group of constraints with information that tells the con-
straint solver how the objects are connected. This allows the solver to deal
with the chain in a more stable manner than would otherwise be possible.

Figure 13.32. A limited hinge constraint mimics the behavior of a door.


<!-- source-pdf-page: 905 -->
> Visual fallback for diagrams/images: [PDF page 905](../../../visual_pages/page_0905.jpg)

Figure 13.33. A prismatic constraint acts like a piston.

13.4.8.7
Rag Dolls

A rag doll is a physical simulation of the way a human body might move when
it is dead or unconscious and hence entirely limp. Rag dolls are created by
linking together a collection of rigid bodies, one for each semi-rigid part of the
body. For example, we might have capsules for the feet, calves, thighs, hands,
upper and lower arms and head and possibly a few for the torso to simulate
the flexibility of the spine.

The rigid bodies in a rag doll are connected to one another via constraints.
Rag doll constraints are specialized to mimic the kinds of motions the joints in
a real human body can perform. We usually make use of constraint chains to
improve the stability of the simulation.

A rag doll simulation is always tightly integrated with the animation sys-
tem. As the rag doll moves in the physics world, we extract the positions and
rotations of the rigid bodies, and we use this information to drive the positions
and orientations of certain joints in the animated skeleton. So in effect, a rag
doll is really just a form of procedural animation that happens to be driven by
the physics system. (See Chapter 12 for more details on skeletal animation.)

Of course, implementing a rag doll is not quite as simple as I’ve made it
sound here. For one thing, there’s usually not a one-to-one mapping between
the rigid bodies in the rag doll and the joints in the animated skeleton—the
skeleton usually has more joints than the rag doll has bodies. Therefore, we
need a system that can map rigid bodies to joints (i.e., one that “knows” to
which joint each rigid body in the rag doll corresponds). There may be addi-
tional joints between those that are being driven by the rag doll bodies, so the
mapping system must also be capable of determining the correct pose trans-
forms for these intervening joints. This is not an exact science. We must apply
artistic judgment and/or some knowledge of human biomechanics in order to
achieve a natural-looking rag doll.


<!-- source-pdf-page: 906 -->
> Visual fallback for diagrams/images: [PDF page 906](../../../visual_pages/page_0906.jpg)

Capsule strikes

Bone continues

an obstacle

to move

Bone
Collision
Capsule

Figure 13.34. With a powered rag doll constraint, and in the absence of any additional forces or
torques, a rigid body representing the lower arm can be made to exactly track the movements of
an animated elbow joint (left). If an obstacle blocks the motion of the body, it will diverge from
that of the animated elbow joint in a realistic way (right).

13.4.8.8
Powered Constraints

Constraints can also be “powered,” meaning that an external engine system
such as the animation system can indirectly control the translations and orien-
tations of the rigid bodies in the rag doll.

Let’s take an elbow joint as an example. An elbow acts pretty much like a
limited hinge, with a little less than 180 degrees of free rotation. (Actually, an
elbow can also rotate axially, but we’ll ignore that for the purposes of this dis-
cussion.) To power this constraint, we model the elbow as a rotational spring.
Such a spring exerts a torque proportional to the spring’s angle of deflection
away from some predefined rest angle, N = −k(θ −θrest). Now imagine
changing the rest angle externally, say by ensuring that it always matches the
angle of the elbow joint in an animated skeleton. As the rest angle changes, the
spring will find itself out of equilibrium, and it will exert a torque that tends
to rotate the elbow back into alignment with θrest. In the absence of any other
forces or torques, the rigid bodies will exactly track the motion of the elbow
joint in the animated skeleton. But if other forces are introduced (for example,
the lower arm comes in contact with an immovable object), then these forces
will play into the overall motion of the elbow joint, allowing it to diverge from
the animated motion in a somewhat realistic manner. As illustrated in Fig-
ure 13.34, this provides the illusion of a human who is trying her best to move
in a certain way (i.e., the “ideal” motion provided by the animation) but who
is sometimes unable to do so due to the limitations of the physical world (e.g.,
her arm gets caught on something as she tries to swing it forward).


<!-- source-pdf-page: 907 -->

### 13.4.9 Controlling the Motions of Rigid Bodies

Most game designs call for a degree of control over the way rigid bodies move
over and above the way they would move naturally under the influence of
gravity and in response to collisions with other objects in the scene. For exam-
ple:

•
An air vent applies an upward force to any object that enters its shaft of
influence.

•
A car is coupled to a trailer and exerts a pulling force on it as it moves.

•
A tractor beam exerts a force on an unwitting spacecraft.

•
An anti-gravity device causes objects to hover.

•
The flow of a river creates a force field that causes objects floating in the
river to move downstream.

And the list goes on. Most physics engines typically provide their users with
a number of ways to exert control over the bodies in the simulation. We’ll
outline the most common of these mechanisms in the following sections.

13.4.9.1
Gravity

Gravity is ubiquitous in most games that take place on the surface of the Earth
or some other planet (or on a spacecraft with simulated gravity). Gravity is
technically not a force but rather a (roughly) constant acceleration, so it af-
fects all bodies equally regardless of their mass. Because of its ubiquitous and
special nature, the magnitude and direction of the gravitational acceleration is
specified via a global setting in most SDKs. (If you’re writing a space game,
you can always set gravity to zero to eliminate it from the simulation.)

13.4.9.2
Applying Forces

Any number of forces can be applied to the bodies in a game physics simula-
tion. A force always acts over a finite time interval. (If it acted instantaneously,
it would be called an impulse—more on that in Section 13.4.9.4 below.) The
forces in a game are often dynamic in nature—they often change their direc-
tions and/or their magnitudes every frame. So the force-application function
in most physics SDKs is designed to be called once per frame for the duration
of the force’s influence. The signature of such a function usually looks some-
thing like this: applyForce(const Vector& forceInNewtons), where
the duration of the force is assumed to be ∆t.


<!-- source-pdf-page: 908 -->

13.4.9.3
Applying Torques

When a force is applied such that its line of action passes through the center of
mass of a body, no torque is generated, and only the body’s linear acceleration
is affected. If it is applied off-center, it will induce both a linear and a rotational
acceleration. A pure torque can be applied to a body as well by applying two
equal and opposite forces to points equidistant from the center of mass. The
linear motions induced by such a pair of forces will cancel each other out (since
for the purposes of linear dynamics, the forces both act at the center of mass).
This leaves only their rotational effects. A pair of torque-inducing forces like
this is known as a couple (http://en.wikipedia.org/wiki/Couple_(mechanics).
A special function such as applyTorque(const Vector& torque) may
be provided for this purpose. However, if your physics SDK provides no
applyTorque() function, you can always write one and have it generate a
suitable couple instead.

13.4.9.4
Applying Impulses

As we saw in Section 13.4.7.2, an impulse is an instantaneous change in veloc-
ity (or actually, a change in momentum). Technically speaking, an impulse
is a force that acts for an infinitesimal amount of time. However, the short-
est possible duration of force application in a time-stepped dynamics simu-
lation is ∆t, which is not short enough to simulate an impulse adequately.
As such, most physics SDKs provide a function with a signature such as
applyImpulse(const Vector& impulse) for the purposes of applying
impulses to bodies.
Of course, impulses come in two flavors—linear and
angular—and a good SDK should provide functions for applying both types.

### 13.4.10 The Collision/Physics Step

Now that we’ve covered the theory and some of the technical details behind
implementing a collision and physics system, let’s take a brief look at how
these systems actually perform their updates every frame.
Every collision/physics engine performs the following basic tasks during
its update step. Different physics SDKs may perform these phases in different
orders. That said, the technique I’ve seen used most often goes something like
this:

1.
The forces and torques acting on the bodies in the physics world are inte-
grated forward by ∆t in order to determine their tentative positions and
orientations next frame.


<!-- source-pdf-page: 909 -->

2.
The collision detection library is called to determine if any new contacts
have been generated between any of the objects as a result of their ten-
tative movement. (The bodies normally keep track of their contacts in
order to take advantage of temporal coherency. Hence, at each step of
the simulation, the collision engine need only determine whether any
previous contacts have been lost and whether any new contacts have
been added.)

3.
Collisions are resolved, often by applying impulses or penalty forces or
as part of the constraint-solving step below. Depending on the SDK, this
phase may or may not include continuous collision detection (CCD, oth-
erwise known as time of impact detection or TOI).

4.
Constraints are satisfied by the constraint solver.

At the conclusion of step 4, some of the bodies may have moved away from
their tentative positions as determined in step 1. This movement may cause
additional interpenetrations between objects or cause other previously satis-
fied constraints to be broken. Therefore, steps 1 through 4 (or sometimes only
2 through 4, depending on how collisions and constraints are resolved) are
repeated until either (a) all collisions have been successfully resolved and all
constraints are satisfied, or (b) a predefined maximum number of iterations
has been exceeded. In the latter case, the solver effectively “gives up,” with the
hope that things will resolve themselves naturally during subsequent frames
of the simulation. This helps to avoid performance spikes by amortizing the
cost of collision and constraint resolution over multiple frames. However, it
can lead to incorrect-looking behavior if the errors are too large or if the time
step is too long or is inconsistent. Penalty forces can be blended into the sim-
ulation in order to gradually resolve these problems over time.

13.4.10.1
The Constraint Solver

A constraint solver is essentially an iterative algorithm that attempts to satisfy
a large number of constraints simultaneously by minimizing the error between
the actual positions and rotations of the bodies in the physics world and their
ideal positions and rotations as defined by the constraints. As such, constraint
solvers are essentially iterative error-minimization algorithms.
Let’s take a look first at how a constraint solver works in the trivial case
of a single pair of bodies connected by a single hinge constraint. During each
step of the physics simulation, the numerical integrator will find new tenta-
tive transforms for the two bodies. The constraint solver then evaluates their


<!-- source-pdf-page: 910 -->

relative positions and calculates the error between the positions and orienta-
tions of their shared axis of rotation. If any error is detected, the solver moves
the bodies in such a way as to minimize or eliminate it. Since there are no
other bodies in the system, the second iteration of the step should discover no
new contacts, and the constraint solver will find that the one hinge constraint
is now satisfied. Hence the loop can exit without further iterations.

When more than one constraint must be satisfied simultaneously, more
iterations may be required. During each iteration, the numerical integrator
will sometimes tend to move the bodies out of alignment with their con-
straints, while the constraint solver tends to put them back into alignment.
With luck, and a carefully designed approach to minimizing error in the con-
straint solver, this feedback loop should eventually settle into a valid solu-
tion. However, the solution may not always be exact. This is why, in games
with physics engines, you sometimes witness seemingly impossible behaviors,
like chains that stretch (opening up little gaps between the links), objects that
interpenetrate briefly or hinges that momentarily move beyond their allow-
able ranges. The goal of the constraint solver is to minimize error—it’s not
always possible to eliminate it completely.

13.4.10.2
Variations between Engines

The description given above is of course an over-simplification of what re-
ally goes on in a physics/collision engine every frame. The way in which
the various phases of computation are performed, and their order relative
to one another, may vary from physics SDK to physics SDK. For example,
some kinds of constraints are modeled as forces and torques that are taken
care of by the numerical integration step rather than being resolved by the
constraint solver.
Collision may be run before the integration step rather
than after. Collisions may be resolved in any number of different ways. Our
goal here is merely to give you a taste of how these systems work. For a
detailed understanding of how any one SDK operates, you’ll want to read
its documentation and probably also inspect its source code (presuming the
relevant bits are available for you to read).
The curious and industrious
reader can get a good start by downloading and experimenting with Open
Dynamics Engine (ODE) and/or PhysX, as these two SDKs are available for
free. You can also learn a great deal from ODE’s wiki, which is available at
http://opende.sourceforge.net/wiki/index.php/Main_Page.


<!-- source-pdf-page: 911 -->
> Visual fallback for diagrams/images: [PDF page 911](../../../visual_pages/page_0911.jpg)

Drive
Update

Submit

Debug Draw

Figure 13.35. Rigid bodies are linked to their visual representations by way of game objects. An
optional direct rendering path is usually provided so that the locations of the rigid bodies can be
visualized for debugging purposes.

## 13.5 Integrating a Physics Engine into Your Game

Obviously, a collision/physics engine is of little use by itself—it must be inte-
grated into your game engine. In this section, we’ll discuss the most common
interface points between the collision/physics engine and the rest of the game
code.

### 13.5.1 Linking Game Objects and Rigid Bodies

The rigid bodies and collidables in the collision/physics world are nothing
more than abstract mathematical descriptions. In order for them to be useful
in the context of a game, we need to link them in some way to their visual
representations on-screen. Usually, we don’t draw the rigid bodies directly
(except for debugging purposes). Instead, the rigid bodies are used to describe
the shape, size, and physical behavior of the logical objects that make up the
virtual game world. We’ll discuss game objects in depth in Chapter 16, but for
the time being, we’ll rely on our intuitive notion of what a game object is—
a logical entity in the game world, such as a character, a vehicle, a weapon,
a floating power-up and so on. So the linkage between a rigid body in the
physics world and its visual representation on-screen is usually indirect, with
the logical game object serving as the hub that links the two together. This is
illustrated in Figure 13.35.
In general, a game object is represented in the collision/physics world by
zero or more rigid bodies. The following list describes three possible scenarios:

•
Zero rigid bodies. Game objects without any rigid bodies in the physics


<!-- source-pdf-page: 912 -->

world act as though they are not solid, because they have no collision
representation at all. Decorative objects with which the player or non-
player characters cannot interact, such as birds flying overhead or por-
tions of the game world that can be seen but never reached, might have
no collision. This scenario can also apply to objects whose collision de-
tection is handled manually (without the help of the collision/physics
engine) for some reason.

•
One rigid body. Most simple game objects need only be represented by a
single rigid body. In this case, the shape of the rigid body’s collidable is
chosen to closely approximate the shape of the game object’s visual rep-
resentation, and the rigid body’s position and orientation exactly match
the position and orientation of the game object itself.

•
Multiple rigid bodies. Some complex game objects are represented by mul-
tiple rigid bodies in the collision/physics world. Examples include char-
acters, machinery, vehicles or any object that is composed of multiple
solid pieces that can move relative to one another. Such game objects
usually make use of a skeleton (i.e., a hierarchy of affine transforms) to
track the locations of their component pieces (although other means are
certainly possible as well). The rigid bodies are usually linked to the
joints of the skeleton in such a way that the position and orientation
of each rigid body corresponds to the position and orientation of one
of the joints. The joints in the skeleton might be driven by an anima-
tion, in which case the associated rigid bodies simply come along for the
ride. Alternatively, the physics system might drive the locations of rigid
bodies and hence indirectly control the locations of the joints. The map-
ping from joints to rigid bodies may or may not be one-to-one—some
joints might be controlled entirely by animation, while others are linked
to rigid bodies.

The linkage between game objects and rigid bodies must be managed by
the engine, of course. Typically, each game object will manage its own rigid
bodies, creating and destroying them when necessary, adding and removing
them from the physics world as needed, and maintaining the connection be-
tween each rigid body’s location and the location of the game object and/or
one of its joints. For complex game objects consisting of multiple rigid bodies,
a wrapper class of some kind may be used to manage them. This insulates
the game objects from the nitty-gritty details of managing a collection of rigid
bodies and allows different kinds of game objects to manage their rigid bodies
in a consistent way.


<!-- source-pdf-page: 913 -->

13.5.1.1
Physics-Driven Bodies

If our game has a rigid body dynamics system, then presumably we want the
motions of at least some of the objects in the game to be driven entirely by the
simulation. Such game objects are called physics-driven objects. Bits of debris,
exploding buildings, rocks rolling down a hillside, empty magazines and shell
casings—these are all examples of physics-driven objects.

A physics-driven rigid body is linked to its game object by stepping the
simulation and then querying the physics system for the body’s position and
orientation. This transform is then applied either to the game object as a whole
or to a joint or some other data structure within the game object.

Example: Building a Safe with a Detachable Door

When physics-driven rigid bodies are linked to the joints of a skeleton, the bod-
ies are often constrained to produce a desired kind of motion. As an example,
let’s look at how a safe with a detachable door might be modeled.

Visually, let’s assume that the safe consists of a single triangle mesh with
two submeshes, one for the housing and one for the door. A two-joint skeleton
is used to control the motions of these two pieces. The root joint is bound to
the housing of the safe, while the child joint is bound to the door in such a way
that rotating the door joint causes the door submesh to swing open and shut
in a suitable way.

The collision geometry for the safe is broken into two independent pieces
as well, one for the housing and one for the door. These two pieces are used
to create two totally separate rigid bodies in the collision/physics world. The
rigid body for the safe’s housing is attached to the root joint in the skeleton,
and the door’s rigid body is linked to the door joint. A hinge constraint is
then added to the physics world to ensure that the door body swings properly
relative to the housing when the dynamics of the two rigid bodies are simu-
lated. The motions of the two rigid bodies representing the housing and the
door are used to update the transforms of the two joints in the skeleton. Once
the skeleton’s matrix palette has been generated by the animation system, the
rendering engine will end up drawing the housing and door submeshes in the
locations of the rigid bodies within the physics world.

If the door needs to be blown off at some point, the constraint can be bro-
ken, and impulses can be applied to the rigid bodies to send them flying. Vis-
ibly, it will appear to the human player that the door and the housing have
become separate objects. But in reality, it’s still a single game object and a
single triangle mesh with two joints and two rigid bodies.


<!-- source-pdf-page: 914 -->

13.5.1.2
Game-Driven Bodies

In most games, certain objects in the game world need to be moved about in
a non-physical way. The motions of such objects might be determined by an
animation or by following a spline path, or they might be under the control
of the human player. We often want these objects to participate in collision
detection—to be capable of pushing the physics-driven objects out of their
way, for example—but we do not want the physics system to interfere with
their motion in any way. To accommodate such objects, most physics SDKs
provide a special type of rigid body known as a game-driven body. (Havok
calls these “key framed” bodies.)
Game-driven bodies do not experience the effects of gravity. They are also
considered to be infinitely massive by the physics system (usually denoted by
a mass of zero, since this is an invalid mass for a physics-driven body). The
assumption of infinite mass ensures that forces and collision impulses within
the simulation can never change the velocity of a game-driven body.
To move a game-driven body around in the physics world, we cannot sim-
ply set its position and orientation every frame to match the location of the cor-
responding game object. Doing so would introduce discontinuities that would
be very difficult for the physical simulation to resolve. (For example, a physics-
driven body might find itself suddenly interpenetrating a game-driven body,
but it would have no information about the game-driven body’s momentum
with which to resolve the collision.) As such, game-driven bodies are usually
moved using impulses—instantaneous changes in velocity that, when inte-
grated forward in time, will position the bodies in the desired places at the
end of the time step. Most physics SDKs provide a convenience function that
will calculate the linear and angular impulses required in order to achieve a
desired position and orientation on the next frame. When moving a game-
driven body, we do have to be careful to zero out its velocity when it is sup-
posed to stop. Otherwise, the body will continue forever along its last nonzero
trajectory.

Example: Animated Safe Door

Let’s continue our example of the safe with a detachable door. Imagine that
we want a character to walk up to the safe, dial the combination, open the
door, deposit some money and close and lock the door again. Later, we want
a different character to get the money in a rather less-civilized manner—by
blowing the door off the safe. To do this, the safe would be modeled with an
additional submesh for the dial and an additional joint that allows the dial to
be rotated. No rigid body is required for the dial, however, unless of course


<!-- source-pdf-page: 915 -->

we want it to fly off when the door explodes.
During the animated sequence of the person opening and closing the safe,
its rigid bodies can be put into game-driven mode. The animation now drives
the joints, which in turn drive the rigid bodies. Later, when the door is to be
blown off, we can switch the rigid bodies into physics-driven mode, break the
hinge constraint, apply the impulse and watch the door fly.
As you’ve probably already noticed, the hinge constraint is not actually
needed in this particular example. It would only be required if the door is to
be left open at some point and we want to see the door swinging naturally in
response to the safe being moved or the door being bumped.

13.5.1.3
Fixed Bodies

Most game worlds are composed of both static geometry and dynamic objects.
To model the static components of the game world, most physics SDKs provide
a special kind of rigid body known as a fixed body. Fixed bodies act a bit like
game-driven bodies, but they do not take part in the dynamics simulation at
all. They are, in effect, collision-only bodies. This optimization can give a big
performance boost to most games, especially those whose worlds contain only
a small number of dynamic objects moving around within a large static world.

13.5.1.4
Havok’s Motion Type

In Havok, all types of rigid body are represented by instances of the class hkp-
RigidBody. Each instance contains a field that specifies its motion type. The
motion type tells the system whether the body is fixed, game-driven (what
Havok calls “key framed”) or physics-driven (what Havok calls “dynamic”).
If a rigid body is created with the fixed motion type, its type can never be
changed. Otherwise, the motion type of a body can be changed dynamically at
runtime. This feature can be incredibly useful. For example, an object that is in
a character’s hand would be game-driven. But as soon as the character drops
or throws the object, it would be changed to physics-driven so the dynamics
simulation can take over its motion. This is easily accomplished in Havok by
simply changing the motion type at the moment of release.
The motion type also doubles as a way to give Havok some hints about the
inertia tensor of a dynamic body. As such, the “dynamic” motion type is bro-
ken into subcategories such as “dynamic with sphere inertia,” “dynamic with
box inertia” and so on. Using the body’s motion type, Havok can decide to ap-
ply various optimizations based on assumptions about the internal structure
of the inertia tensor.


<!-- source-pdf-page: 916 -->

### 13.5.2 Updating the Simulation

The physics simulation must of course be updated periodically, usually once
per frame. This does not merely involve stepping the simulation (numerically
integrating, resolving collisions and applying constraints). The linkages be-
tween the game objects and their rigid bodies must be maintained as well. If
the game needs to apply any forces or impulses to any of the rigid bodies, this
must also be done every frame. The following steps are required to completely
update the physics simulation:

•
Update game-driven rigid bodies. The transforms of all game-driven rigid
bodies in the physics world are updated so that they match the trans-
forms of their counterparts (game objects or joints) in the game world.

•
Update phantoms. A phantom shape acts like a game-driven collidable
with no corresponding rigid body. It is used to perform certain kinds
of collision queries. The locations of all phantoms are updated prior to
the physics step, so that they will be in the right places when collision
detection is run.

•
Update forces, apply impulses and adjust constraints. Any forces being ap-
plied by the game are updated. Any impulses caused by game events
that occurred this frame are applied. Constraints are adjusted if neces-
sary. (For example, a breakable hinge might be checked to determine if
it has been broken; if so, the physics engine is instructed to remove the
constraint.)

•
Step the simulation.
We saw in Section 13.4.10 that the collision and
physics engines must both be updated periodically. This involves nu-
merically integrating the equations of motion to find the physical state of
all bodies on the next frame, running the collision detection algorithm to
add and remove contacts from all rigid bodies in the physics world, re-
solving collisions and applying constraints. Depending on the SDK, these
update phases may be hidden behind a single atomic step() function,
or it may be possible to run them individually.

•
Update physics-driven game objects. The transforms of all physics-driven
objects are extracted from the physics world, and the transforms of the
corresponding game objects or joints are updated to match.

•
Query phantoms. The contacts of each phantom shape are read after the
physics step and used to make decisions.

•
Perform collision cast queries. Ray casts and shape casts are kicked off, ei-
ther synchronously or asynchronously. When the results of these queries


<!-- source-pdf-page: 917 -->

become available, they are used by various engine systems to make de-
cisions.

These tasks are usually performed in the order shown above, with the ex-
ception of ray and shape casts, which can theoretically be done at any time
during the game loop. Clearly it makes sense to update game-driven bod-
ies and apply forces and impulses prior to the step, so that the effects will
be “seen” by the simulation. Likewise, physics-driven game objects should
always be updated after the step, to ensure that we’re using the most up-to-
date body transforms. Rendering typically happens after everything else in
the game loop. This ensures that we are rendering a consistent view of the
game world at a particular instant in time.

13.5.2.1
Timing Collision Queries

In order to query the collision system for up-to-date information, we need to
run our collision queries (ray and shape casts) after the physics step has run
during the frame. However, the physics step is usually run toward the end
of the frame, after the game logic has made most of its decisions and the new
locations of any game-driven physics bodies have been determined. When,
then, should collision queries be run?
This question does not have an easy answer. We have a number of options,
and most games end up using some or all of them:

•
Base decisions on last frame’s state. In many cases, decisions can be made
correctly based on last frame’s collision information. For example, we
might want to know whether or not the player was standing on some-
thing last frame, in order to decide whether or not he should start falling
this frame. In this case, we can safely run our collision queries prior to
the physics step.
•
Accept a one-frame lag. Even if we really want to know what is happen-
ing this frame, we may be able to tolerate a one-frame lag in our collision
query results. This is usually only true if the objects in question aren’t
moving too fast. For example, we might move one object forward in time
and then want to know whether or not that object is now in the player’s
line of sight. A one-frame-off error in this kind of query may not be no-
ticeable to the player. If this is the case, we can run the collision query
prior to the physics step (returning collision information from the previ-
ous frame) and then use these results as if they were an approximation to
the collision state at the end of the current frame.
•
Run the query after the physics step. Another approach is to run certain
queries after the physics step. This is feasible when the decisions being


<!-- source-pdf-page: 918 -->

made based on the results of the query can be deferred until late in the
frame. For example, a rendering effect that depends on the results of a
collision query could be implemented this way.

13.5.2.2
Single-Threaded Updating

A very simple single-threaded game loop might look something like this:

F32 dt = 1.0f/30.0f;

for (;;) // main game loop
{
g_hidManager->poll();

g_gameObjectManager->preAnimationUpdate(dt);
g_animationEngine->updateAnimations(dt);
g_gameObjectManager->postAnimationUpdate(dt);

g_physicsWorld->step(dt);
g_animationEngine->updateRagDolls(dt);

g_gameObjectManager->postPhysicsUpdate(dt);
g_animationEngine->finalize();

g_effectManager->update(dt);

g_audioEngine->udate(dt);

// etc.

g_renderManager->render();

dt = calcDeltaTime();
}

In this example, our game objects are updated in three phases: once before an-
imation runs (during which they can queue up new animations, for example),
once after the animation system has calculated final local poses and a tenta-
tive global pose (but before the final global pose and matrix palette has been
generated) and once after the physics system has been stepped.

•
The locations of all game-driven rigid bodies are generally updated in
preAnimationUpdate() or postAnimationUpdate(). Each game-
driven body’s transform is set to match the location of either the game
object that owns it or a joint in the owner’s skeleton.


<!-- source-pdf-page: 919 -->

•
The location of each physics-driven rigid body is generally read in post-
PhysicsUpdate() and used to update the location of either the game
object or one of the joints in its skeleton.

One important concern is the frequency with which you are stepping the
physics simulation. Most numerical integrators, collision detection algorithms
and constraint solvers operate best when the time between steps (∆t) is con-
stant. It’s usually a good idea to step your physics/collision SDK with an ideal
1/30 second or 1/60 second time delta and then govern the frame rate of your
overall game loop. If your game drops below its target frame rate, it’s better
to let the physics slow down visually than to try to adjust the simulation time
step to match the actual frame rate.

### 13.5.3 Example Uses of Collision and Physics in a Game

To make our discussion of collision and physics more concrete, let’s take a
high-level look at a few common examples of how collision and/or physics
simulations are commonly used in real games.

13.5.3.1
Simple Rigid Body Game Objects

Many games include simple physically simulated objects like weapons, rocks
that can be picked up and thrown, empty magazines, furniture, objects on
shelves that can be shot and so on. Such objects might be implemented by
creating a custom game object class and giving it a reference to a rigid body in
the physics world (e.g., hkpRigidBody if we’re using Havok). Or we might
create an add-on component class that handles simple rigid body collision and
physics, allowing this feature to be added to virtually any type of game object
in the engine.
Simple physics objects usually change their motion type at runtime. They
are game-driven when being held in a character’s hand and physics-driven
when in free fall after having been dropped.

13.5.3.2
Bullet Traces

Whether or not you approve of game violence, the fact remains that laser guns
and projectile weapons of one form or another are a big part of most games.
Let’s look at how these are typically implemented.
Sometimes projectiles are implemented using ray casts. On the frame that
the weapon is fired, we shoot off a ray cast, determine what object was hit and
immediately impart the impact to the affected object.
Unfortunately, the ray cast approach does not account for the travel time
of the projectile. It also does not account for the slight downward trajectory


<!-- source-pdf-page: 920 -->

caused by the influence of gravity. If these details are important to the game,
we can model our projectiles using real rigid bodies that move through the
collision/physics world over time. This is especially useful for slower-moving
projectiles, like thrown objects or rockets. The thrown bricks in Naughty Dog’s
The Last of Us used such an approach.

There are plenty of issues to consider and deal with when implementing
laser beams and projectiles. A few of the most common ones are discussed
below.

Bullet Ray Casting

When using ray casting to check for bullet hits, the question arises: Does the
ray come from the camera focal point or from the tip of the gun in the player
character’s hands? This is especially problematic in a third-person shooter,
where the ray coming out of the player’s gun usually does not align with the
ray coming from the camera focal point through the reticle in the center of the
screen. This can lead to situations in which the reticle appears to be on top of a
target, yet the third-person character is clearly behind an obstacle and would
not be able to shoot that target from his point of view. Various “tricks” must
usually be employed to ensure that the player feels like he or she is shooting
what he or she is aiming at while maintaining plausible visuals on the screen.

Mismatches between Collision and Visible Geometry

Mismatches between collision geometry and visible geometry can lead to situ-
ations in which the player can see the target through a small crack or just over
the edge of some other object, and yet the collision geometry is solid, so the
bullet cannot reach the target. (This is usually only a problem for the player
character.) One solution to this problem is to use a render query instead of
a collision query to determine if the ray actually hit the target. For example,
during one of the rendering passes, we could generate a texture in which each
pixel stores the unique identifier of the game object to which it corresponds.
We can then query this texture to determine whether or not an enemy char-
acter or other suitable target occupies the pixel(s) underneath the weapon’s
reticle.

Aiming in a Dynamic Environment

AI-controlled characters may need to “lead” their shots if projectiles take a
finite amount of time to reach their targets.


<!-- source-pdf-page: 921 -->

Impact Effects

When bullets hit their targets, we may want to trigger a sound or a particle
effect, lay down a decal or perform other tasks.
In the Unreal engine, this is accomplished via a system of physical materi-
als. Visible geometry can be tagged not only with visual materials, but with
physical materials as well. The former defines how the surface looks, the latter
defines how it reacts to physical interactions, including impact sounds, bullet
“squib” particle effects, decals and so on. (See http://udn.epicgames.com/
Three/PhysicalMaterialSystem.html for more details.)
At Naughty Dog, we use a very similar system: Collision geometry can
be tagged with polygon attributes (PATs for short), which define certain physi-
cal behaviors like footstep sounds. But bullet impacts are treated in a special
way, because we need them to interact directly with visible geometry rather
than the crude collision geo. As such, visible materials can be tagged with an
optional bullet effect that defines which bullet squib, impact sound and decal
should be laid down for each possible type of projectile that might impact that
surface.

13.5.3.3
Grenades

Grenades in games are sometimes implemented as free-moving physics ob-
jects. However, this leads to a significant loss of control. Some control can be
regained by imposing various artificial forces or impulses on the grenade. For
example, we could apply an extreme air drag once the grenade bounces for
the first time, in an attempt to limit the distance it can bounce away from its
target.
Some game teams actually go so far as to manage the grenade’s motion en-
tirely manually. The arc of a grenade’s trajectory can be calculated beforehand,
using a series of ray casts to determine what target it would hit if released. The
trajectory can even be shown to the player via some kind of on-screen display.
When the grenade is thrown, the game moves it along its arc and can then
carefully control the bounce so that it never goes too far away from its target,
while still looking natural.

13.5.3.4
Explosions

In a game, an explosion typically has a few components: some kind of visual
effect like a fireball and smoke, audio effects to mimic the sound of the explo-
sion and its impacts with objects in the world, and a growing damage radius
that affects any objects in its wake.


<!-- source-pdf-page: 922 -->

When an object finds itself in the radius of an explosion, its health is typ-
ically reduced, and we often also want to impart some motion to mimic the
effect of the shock wave. This might be done via an animation. (For exam-
ple, the reaction of a character to an explosion might best be done this way.)
We might also wish to allow the impact reaction to be driven entirely by the
dynamics simulation. We can accomplish this by having the explosion apply
impulses to any suitable objects within its radius. It’s pretty easy to calculate
direction of these impulses—they are typically radial, calculated by normaliz-
ing the vector from the center of the explosion to the center of the impacted
object and then scaling this vector by the magnitude of the explosion (and per-
haps falling off as the distance from the epicenter increases).
Explosions may interact with other engine systems as well. For example,
we might want to impart a “force” to the animated foliage system, causing
grass, plants and trees to momentarily bend as a result of the explosion’s shock
wave.

13.5.3.5
Destructible Objects

Destructible objects are commonplace in many games. These objects are pecu-
liar because they start out in an undamaged state in which they must appear
to be a single cohesive object, and yet they must be capable of breaking into
many separate pieces. We may want the pieces to break off one by one, al-
lowing the object to be “whittled down” gradually, or we may only require a
single catastrophic explosion.
Deformable body simulations like DMM can handle destruction naturally.
However, we can also implement breakable objects using rigid body dynam-
ics. This is typically done by dividing a model into a number of breakable
pieces and assigning a separate rigid body to each one. This is the approach
taken by Havok Destruction, for example.
For reasons of performance optimization and/or visual quality, we might
decide to use special “undamaged” versions of the visual and collision geom-
etry, each of which is constructed as a single solid piece. This model can be
swapped out for the damaged version when the object needs to start breaking
apart. In other cases, we may want to model the object as separate pieces at
all times. This might be appropriate if the object is a stack of bricks or a pile of
pots and pans, for example.
To model a multi-piece object, we could simply stack a bunch of rigid bod-
ies and let the physics simulation take care of it. This can be made to work
in good-quality physics engines (although it’s not always trivial to get right).
However, we may want some Hollywood-style effects that cannot be achieved
with a simple stack of rigid bodies.


<!-- source-pdf-page: 923 -->

For example, we may want to define the structure of the object. Some pieces
might be indestructible, like the base of a wall or the chassis of a car. Others
might be non-structural—they just fall off when hit by bullets or other objects.
Still other pieces might be structural—if they are hit, not only do they fall, but
they also impart forces to other pieces lying on top of them. Some pieces could
be explosive—when they are hit, they create secondary explosions or propagate
damage throughout the structure. We may want some pieces to act as valid
cover points for some characters, but not others. This implies that our breakable
object system may have some connections to the cover system.
We might also want our breakable objects to have a notion of health. Dam-
age might build up until eventually the whole thing collapses, or each piece
might have a health, requiring multiples shots or impacts before it is allowed
to break. Constraints might also be employed to allow broken pieces to hang
off the object rather than coming away from it completely.
We may also want our structures to take time to collapse completely. For
example, if a long bridge is hit by an explosion at one end, the collapse should
slowly propagate from one end to the other so that the bridge looks massive.
This is another example of a feature the physics system won’t give you for
free—it would just wake up all rigid bodies in the simulation island simulta-
neously. These kinds of effects can be implemented through judicious use of
the game-driven motion type.

13.5.3.6
Character Mechanics

For a game like bowling, pinball or Marble Madness, the “main character” is
a ball that rolls around in an imaginary game world. For this kind of game,
we could very well model the ball as a free-moving rigid body in the physics
simulation and control its movements by applying forces and impulses to it
during gameplay.
In character-based games, however, we usually don’t take this kind of ap-
proach. The locomotion of a humanoid or animal character is usually far too
complex to be controlled adequately with forces and impulses. Instead, we
usually model characters as a set of game-driven capsule-shaped rigid bodies,
each one linked to a joint in the character’s animated skeleton. These bodies
are primarily used for bullet hit detection or to generate secondary effects such
as when a character’s arm bumps an object off a table. Because these bodies
are game-driven, they won’t avoid interpenetrations with immovable objects
in the physics world, so it is up to the animator to ensure that the character’s
movements appear believable.
To move the character around in the game world, most games use sphere
or capsule casts to probe in the direction of desired motion. Collisions are


<!-- source-pdf-page: 924 -->
> Visual fallback for diagrams/images: [PDF page 924](../../../visual_pages/page_0924.jpg)

resolved manually. This allows us to do cool stuff like:

•
having the character slide along walls when he runs into them at an
oblique angle;
•
allowing the character to “pop up” over low curbs rather than getting
stuck;
•
preventing the character from entering a “falling” state when he walks
off a low curb;
•
preventing the character from walking up slopes that are too steep (most
games have a cutoff angle after which the character will slide back rather
than being able to walk up the slope); and
•
adjusting animations to accommodate collisions.

As an example of this last point, if the character is running directly into a
wall at a roughly 90 degree angle, we can let the character “moonwalk” into
the wall forever, or we can slow down his animation. We can also do some-
thing even more slick, like playing an animation in which the character sticks
out his hand and touches the wall and then idles sensibly until the movement
direction changes.
Havok provides a character controller system that handles many of these
things. In Havok’s system, illustrated in Figure 13.36, a character is modeled
as a capsule phantom that is moved each frame to find a potential new loca-
tion. A collision contact manifold (i.e., a collection of contact planes, cleaned
up to eliminate noise) is maintained for the character. This manifold can be
analyzed each frame in order to determine how best to move the character,
adjust animations and so on.

13.5.3.7
Camera Collision

In many games, the camera follows the player’s character or vehicle around
in the game world, and it can often be rotated or controlled in limited ways
by the person playing the game. It’s important in such games to never permit
the camera to interpenetrate geometry in the scene, as this would break the
illusion of realism. The camera system is therefore another important client of
the collision engine in many games.
The basic idea behind most camera collision systems is to surround the
virtual camera with one or more sphere phantoms or sphere cast queries that
can detect when it is getting close to colliding with something. The system
can respond by adjusting the camera’s position and/or orientation in some
way to avoid the potential collision before the camera actually passes through
the object in question.


<!-- source-pdf-page: 925 -->
> Visual fallback for diagrams/images: [PDF page 925](../../../visual_pages/page_0925.jpg)

Figure 13.36. Havok’s character controller models a character as a capsule-shaped phantom. The
phantom maintains a noise-reduced collision manifold (a collection of contact planes) that can be
used by the game to make movement decisions.

This sounds simple enough, but it is actually an incredibly tricky problem
requiring a great deal of trial and error to get right. To give you a feel for how
much effort can be involved, many game teams have a dedicated engineer
working on the camera system for the entire duration of the project. We can’t
possibly cover camera collision detection and resolution in any depth here, but
the following list should give you a sense of some of the most pertinent issues
to be aware of:

•
Zooming the camera in to avoid collisions works well in a wide variety
of situations. In a third-person game, you can zoom all the way in to
a first-person view without causing too much trouble (other than mak-
ing sure the camera doesn’t interpenetrate the character’s head in the
process).
•
It’s usually a bad idea to drastically change the horizontal angle of the
camera in response to collisions, as this tends to mess with camera-
relative player controls. However, some degree of horizontal adjustment
can work well, depending on what the player is expected to be doing at
the time. If she is aiming at a target, she’ll be angry with you if you
throw off her aim to bring the camera out of collision. But if she’s just lo-
comoting through the world, the change in camera orientation may feel
entirely natural. Because of this, you might want to allow adjustments
to the horizontal angle of the camera only when the main character is not


<!-- source-pdf-page: 926 -->

in the heat of a battle.

•
You can adjust the vertical angle of the camera to some degree, but it’s
important not to do too much of this, or the player will lose track of the
horizon and end up looking down onto the top of the player character’s
head!

•
Some games allow the camera to move along an arc lying in a vertical
plane, perhaps described by a spline. This permits a single HID control
such as the vertical deflection of the left thumb stick to control both the
zoom and the vertical angle of the camera in an intuitive way. (The cam-
era in the Naughty Dog engine works this way.) When the camera comes
into collision with objects in the world, it can be automatically moved
along this same arc to avoid the collision, the arc might be compressed
horizontally, or any number of other approaches might be taken.

•
It’s important to consider not only what’s behind and beside the camera
but what is in front of it as well. For example, what should happen if
a pillar or another character comes between the camera and the player
character? In some games, the offending object becomes translucent; in
others, the camera zooms in or swings around to avoid the collision. This
may or may not feel good to the person playing the game! How you
handle these kinds of situations can make or break the perceived quality
of your game.

Even after taking account of these and many other problematic situations,
your camera may not look or feel right! Always budget plenty of time for trial
and error when implementing a camera collision system.

13.5.3.8
Rag Doll Integration

In Section 13.4.8.7, we learned how special types of constraints can be used to
link a collection of rigid bodies together to mimic the behavior of a limp (dead
or unconscious) human body. In this section, we’ll investigate a few of the
issues that arise when integrating rag doll physics into your game.
As we saw in Section 13.5.3.6, the gross movements of a conscious character
are usually determined by performing shape casts or moving a phantom shape
around in the game world. The detailed movements of the character’s body
are typically driven by animations. Game-driven rigid bodies are sometimes
attached to the limbs for the purposes of weapons targeting or to allow the
character to knock over other objects in the world.
When a character becomes unconscious, the rag doll system kicks in. The
character’s limbs are modeled as capsule-shaped rigid bodies connected via
constraints and linked to joints in the character’s animated skeleton.
The


<!-- source-pdf-page: 927 -->

physics system simulates the motions of these bodies, and we update the
skeletal joints to match, thereby allowing physics to move the character’s
body.
The set of rigid bodies used for rag doll physics might not be the same ones
affixed to the character’s limbs when it was alive. This is because the two colli-
sion models have very different requirements. When the character is alive, its
rigid bodies are game-driven, so we don’t care if they interpenetrate. And in
fact, we usually want them to overlap, so there aren’t any holes through which
an enemy character might shoot. But when the character turns into a rag doll,
it’s important that the rigid bodies do not interpenetrate, as this would cause
the collision resolution system to impart large impulses that would tend to
make the limbs explode outward! For these reasons, it’s actually quite com-
mon for characters to have entirely different collision/physics representations
depending on whether they’re conscious or unconscious.
Another issue is how to transition from the conscious state to the uncon-
scious state. A simple LERP blend between animation-generated and physics-
generated poses usually doesn’t work very well, because the physics pose very
quickly diverges from the animation pose. (A blend between two totally unre-
lated poses usually doesn’t look natural.) As such, we may want to use pow-
ered constraints during the transition (see Section 13.4.8.8).
Characters often interpenetrate background geometry when they are con-
scious (i.e., when their rigid bodies are game-driven). This means that the rigid
bodies might be inside another solid object when the character transitions to
rag doll (physics-driven) mode. This can give rise to huge impulses that cause
rather wild-looking rag doll behavior in-game. To avoid these problems, it is
best to author death animations carefully, so that the character’s limbs are kept
out of collision as best as possible. It’s also important to detect collisions via
phantoms or collision callbacks during the game-driven mode so that you can
drop the character into rag doll mode the moment any part of his body touches
something solid.
Even when these steps are taken, rag dolls have a tendency to get stuck
inside other objects. Single-sided collision can be an incredibly important fea-
ture when trying to make rag dolls look good. If a limb is partly embedded
in a wall, it will tend to be pushed out of the wall rather than staying stuck
inside it. However, even single-sided collision doesn’t solve all problems. For
example, when the character is moving quickly or if the transition to rag doll
isn’t executed properly, one rigid body in the rag doll can end up on the far
side of a thin wall. This causes the character to hang in mid-air rather than
falling properly to the ground.
Another rag doll feature that can be useful is the ability for unconscious


<!-- source-pdf-page: 928 -->

characters to regain consciousness and get back up. To implement this, we
need a way to search for a suitable “stand up” animation. We want to find
an animation whose pose on frame zero most closely matches the rag doll’s
pose after it has come to rest (which is totally unpredictable in general). This
can be done by matching the poses of only a few key joints, like the upper
thighs and the upper arms. Another approach is to manually guide the rag doll
into a pose suitable for getting up by the time it comes to rest, using powered
constraints.
As a final note, we should mention that setting up a rag doll’s constraints
can be a tricky business. We generally want the limbs to move freely but with-
out doing anything biomechanically impossible. This is one reason specialized
types of constraints are often used when constructing rag dolls. Nonetheless,
you shouldn’t assume that your rag dolls will look great without some effort.
High-quality physics engines like Havok provide a rich set of content creation
tools that allow an artist to set up constraints within a DCC package like Maya
and then test them in real time to see how they might look in-game.
All in all, getting rag doll physics to work in your game isn’t particularly
difficult, but getting it to look good can take a lot of work! As with many things
in game programming, it’s a good idea to budget plenty of time for trial and
error, especially when it’s your first time working with rag dolls.

## 13.6 Advanced Physics Features

A rigid body dynamics simulation with constraints can cover an amazing
range of physics-driven effects in a game. However, such a system clearly has
its limitations. Recent research and development is seeking to expand physics
engines beyond constrained rigid bodies. Here are just a few examples:

•
Deformable bodies. As hardware capabilities improve and more-efficient
algorithms are developed, physics engines are beginning to provide sup-
port for deformable bodies. DMM is an excellent example of such an
engine.

•
Cloth. Cloth can be modeled as a sheet of point masses, connected by
stiff springs. Cloth is notoriously difficult to get right, as many difficul-
ties arise with respect to collision between cloth and other objects, nu-
merical stability of the simulation, etc. That being said, many games
and third-party physics SDKs like Havok now provide powerful and
well-behaved cloth simulations for use in games and other real-time
applications.


<!-- source-pdf-page: 929 -->

•
Hair. Hair can be modeled by a large number of small physically simu-
lated filments, or a simpler approach can be used in which sheets of cloth
are texture-mapped to look like hair, and the cloth simulation is tuned to
make the character’s hair move in a believable way. This is how Chloe’s
hair in Uncharted: The Lost Legacy works. Hair simulation and rendering
remains an active area of research, and the quality of hair in games will
certainly continue to improve.
•
Water surface simulations and buoyancy. Games have been doing water
surface simulations and buoyancy for some time now. Buoyancy can be
implemented via a special-case system (not part of the physics engine
per se), or it can be modeled via forces within the physics simulation.
Organic movement of the water surface is often a rendering effect only
and does not affect the physics simulation at all. From the point of view
of physics, the water surface is often modeled as a plane. For large dis-
placements in the water surface, the entire plane might be moved. How-
ever, some game teams and researchers are pushing the limits of these
simulations, allowing for dynamic water surfaces, waves that crest, re-
alistic current simulations and more. One good example is the water in
the god game From Dust.
•
General fluid dynamics simulations. Right now, fluid dynamics falls pri-
marily into the realm of specialized simulation libraries. However, this
is an active area of research and development, and some games already
make use of fluid simulations to produce some astounding visual effects.
For example, the LittleBigPlanet series makes use of a 2D fluid simulation
for its smoke and fire effects; and the PhysX SDK offers a 3D position based
fluid simulation that produces stunningly realistic results.
•
Physically based audio synthesis. When physically simulated objects col-
lide, bounce, roll and slide, it’s important to be able to generate appropri-
ate audio to reinforce the believability of the simulation. These sounds
can be created in games via controlled playback of pre-recorded audio
clips. But dynamic synthesis of such sounds is becoming a viable alter-
native, and is currently an active area of research.

•
GPGPU. As GPUs become more and more powerful, there has been a
shift toward harnessing their awesome parallel processing power for
tasks other than graphics. One obvious application of general-purpose
GPU (GPGPU) computing is for collision and physics simulation. For
example, Naughty Dog’s cloth simulation engine was ported to run en-
tirely on the GPU for PlayStation 4.
