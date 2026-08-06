# 14 Audio

> Source PDF pages: 930-931
> Extraction mode: PyMuPDF text blocks; line breaks and printed hyphenation are preserved.

<!-- source-pdf-page: 930 -->

I
f you’ve ever watched a horror film with your speakers muted, you know
just how important audio is to immersiveness. (If not, try it! It’s a real ear-
opener.) Be it a film or a video game, sound can quite literally make the dif-
ference between a gripping, emotional, unforgettable multimedia experience
and a lackluster yawnfest.
Modern games immerse the player in a realistic (or a semi-realistic but styl-
ized) virtual environment. The graphics engine is charged with the task of
reproducing as accurately and believably as possible what the player would
actually see, if he or she were present within this virtual environment (while re-
maining true to the art style of the game). In exactly the same sense, the audio
engine is charged with the task of accurately and believably reproducing what
the player would actually hear, if he or she were present in the game world
(while remaining true to the fiction and tonal style of the game). Sound pro-
grammers today use the term audio rendering engine to underscore its many
parallels with the graphics rendering engine.
In this chapter, we’re going to explore both the theory and practice of cre-
ating audio for a triple-A game. We’ll introduce an area of mathematics called
signal processing theory that underlies almost every aspect of digital audio tech-
nology, including digital sound recording and playback, filtering, reverb and
other digital signal processor (DSP) effects. We’ll explore game audio from
a software engineering standpoint, by investigating a number of widely used


<!-- source-pdf-page: 931 -->
> Visual fallback for diagrams/images: [PDF page 931](../../../visual_pages/page_0931.jpg)

audio APIs, breaking down the components that comprise a typical audio ren-
dering engine and learning how the audio system is interconnected with other
game engine systems. We’ll also see how environmental acoustic modeling
and character dialog were handled in Naughty Dog’s popular game The Last
of Us. So hold on tight, keep your hands inside the car at all times and enjoy
the noisy ride!
