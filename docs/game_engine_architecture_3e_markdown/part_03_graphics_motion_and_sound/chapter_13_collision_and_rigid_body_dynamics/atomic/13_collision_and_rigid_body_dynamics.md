# 13 Collision and Rigid Body Dynamics

> Source PDF pages: 836-837
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
