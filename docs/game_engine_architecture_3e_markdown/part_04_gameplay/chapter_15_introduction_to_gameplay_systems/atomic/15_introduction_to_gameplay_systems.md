# 15 Introduction to Gameplay Systems

> Source PDF pages: 1034-1035
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
