# 14 Audio

> Source PDF pages: 930-1033
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

## 14.1 The Physics of Sound

Sound is a compression wave that travels through the air (or some other com-
pressible medium). A sound wave gives rise to alternating regions of air com-
pression and decompression (also known as rarefaction) relative to the average
atmospheric pressure. As such, we measure the amplitude of a sound wave in
units of pressure. In SI units, pressure is measured in Pascals, abbreviated Pa.
One Pascal is the force of one Newton applied over an area of one square meter
(1 Pa = 1 N/m2 = 1 kg/(m · s2)).
The instantaneous acoustic pressure is the ambient atmospheric pressure
(considered a constant for our purposes) plus the perturbation caused by the
sound wave at one specific instant in time:

pinst = patmos + psound.

Of course, sound is a dynamic phenomenon—the sound pressure varies over
time. We can plot the instantaneous sound pressure as a function of time,
pinst(t). In signal processing theory—the area of mathematics that underlies vir-
tually every aspect of digital audio technology—such a time-varying function
is called a signal. Figure 14.1 illustrates a typical sound wave signal p(t), os-
cillating about the average atmospheric pressure.

p(t)

+

t
patmos

–

Figure 14.1. A signal p(t) can be used to model the time-varying acoustic pressure of a sound.


<!-- source-pdf-page: 932 -->
> Visual fallback for diagrams/images: [PDF page 932](../../../visual_pages/page_0932.jpg)

p(t)

T

t

Figure 14.2. The period T of an arbitrary periodic signal is the minimum time between repeated
patterns in the waveform.

### 14.1.1 Properties of Sound Waves

When a musical instrument plays a long steady note, the resulting sound pres-
sure signal is periodic, meaning the waveform consists of a repeating pattern
characteristic to that particular kind of instrument. The period T of any re-
peating pattern describes the minimum amount of time that passes between
successive instances of the pattern. For example, for a sinusoidal sound wave
the period measures the time between successive peaks or troughs. In SI units,
period is typically measured in seconds (s). This is illustrated in Figure 14.2.
The frequency of a wave is just the inverse of its period (f = 1/T). Fre-
quency is measured in Hertz (Hz), which means “cycles per second.” A “cy-
cle” is technically a dimensionless quantity, so the Hertz is the inverse of the
second (Hz = 1/s).
Many scientists and mathematicians make use of a quantity known as the
angular frequency, typically denoted by the symbol ω. The angular frequency is
just the rate of oscillation measured in radians per second instead of cycles per
second. Since one complete circular rotation is 2π radians, ω = 2π f = 2π/T.
Angular frequency is very useful when analyzing sinusoidal waves, because
a circular motion in two dimensions gives rise to a sinusoidal motion when
projected onto a single dimensional axis.
The amount by which a periodic signal such as a sine wave is shifted left or
right along the time axis is known as its phase. Phase is a relative term. For ex-
ample, sin(t) is really just a version of cos(t) that has been phase-shifted by + π

2
along the t axis (i.e., sin(t) = cos(t −π

2 )). Likewise cos(t) is just sin(t) phase-
shifted by −π

2 (i.e., cos(t) = sin(t + π

2 )). Phase is illustrated in Figure 14.3.
The speed v at which a sound wave propagates through its medium de-
pends upon the material and physical properties of the medium, including
phase (solid, gas or liquid), temperature, pressure and density. In 20°C dry


<!-- source-pdf-page: 933 -->
> Visual fallback for diagrams/images: [PDF page 933](../../../visual_pages/page_0933.jpg)

sin(t)
cos(t)

/2
–

/2

/2

t

Figure 14.3. The sine and cosine functions are just phase-shifted versions of one another.

air, the speed of sound is approximately 343.2 m/s, which is 767.7 mph or
1235.6 km/h.
The wavelength λ of a sinusoidal wave measures the spatial distance be-
tween successive peaks or troughs. It depends in part on the frequency of the
wave, but because it is a spatial measurement it also depends on the speed of
the wave. Specifically, λ = v/ f where v is the speed of the wave (measured in
m/s) and f is the frequency (measured in Hz or 1/s). The seconds in the nu-
merator and denominator cancel one another, and we are left with wavelength
measured in units of meters.

### 14.1.2 Perceived Loudness and the Decibel

In order to judge the “loudness” of the sounds we hear, our ears continuously
average the amplitude of the incoming sound signal over a short, sliding time
window. This averaging effect is modeled well by a quantity known as the
effective sound pressure. This is defined as the root mean square (RMS) of the
instantaneous sound pressure measured over a specific interval of time.
If we were to take a series of n discrete sound pressure measurements pi,
equally spaced in time, the RMS sound pressure prms would be

√

n
∑
i=1
p2
i .
(14.1)

1
n

prms =

However, our ears take pressure measurements continuously, rather than at
discrete points in time. If we imagine measuring the instantaneous sound
pressure continuously, starting at time T1 and lasting until time T2, the sum-
mation in Equation (14.1) would become an integral as follows:

√

∫T2

1
T2 −T1

T1
(p(t))2 dt.
(14.2)

prms =


<!-- source-pdf-page: 934 -->

However, the story doesn’t end here. Perceived loudness is actually pro-
portional to the acoustic intensity I, which is itself proportional to the square of
the RMS sound pressure:

I ∝p2
rms.

Humans can perceive a very wide range of sound pressure variations—
from the flutter of a piece of paper falling to the ground to the boom of an
aircraft breaking mach one. In order to manage such a wide dynamic range,
we normally measure sound intensity in units of decibels (dB). The decibel is
a logarithmic unit that expresses the ratio between two values. By employing
a logarithmic scale, the decibel allows a wide range of measurements to be
represented by a relatively narrow range of values. A decibel is actually one-
tenth of a bel, a unit named in honor of Alexander Graham Bell.
When sound intensity is measured in decibels, it is called sound pressure
level (SPL) and represented by the symbol Lp. Sound pressure level is defined
as the ratio of the acoustic intensity (i.e., the squared pressure) of a sound rela-
tive to a reference intensity pref that represents the lower limit of human hear-
ing. So we have:

)

(
p2
rms
p2
ref

Lp = 10 log10

dB

)
dB,

( prms

= 20 log10

pref

where the 20 arises because when we take the square outside the logarithm, it
becomes a multiplication by two. The commonly used reference sound pres-
sure in air is pref = 20 µPa (RMS). For more information on sound pressure,
the physics of sound and human aural perception, see [6].
By the way, if you’re feeling a bit rusty, the following identities may help
to refresh your memory on logarithms. In Equations (14.3), b, x and y are
positive real numbers with b ̸= 1, c and d are any real numbers, c = logb x,
and d = logb y (or written another way, bc = x and bd = y).

logb x = c
when
bc = x
(definition);

logb 1 = 0
because
b0 = 1;

logb b = 1
because
b1 = b;

logb(x · y) = logb x + logb y
because
bc · bd = bc+d;
(14.3)

logb(x/y) = logb x −logb y
because
bc/bd = bc−d;

logb xd = d logb x
because
(bc)d = bcd.


<!-- source-pdf-page: 935 -->
> Visual fallback for diagrams/images: [PDF page 935](../../../visual_pages/page_0935.jpg)

Figure 14.4. The human ear is most sensitive in the frequency range between 2 and 5 kHz. As the
frequency decreases or increases beyond this range, more and more acoustic pressure is required
to produce the same perception of “loudness.”

14.1.2.1
Equal-Loudness Contours

The human ear does not have the same response to sound waves of different
frequencies. The human ear is most sensitive in the frequency range between
2 and 5 kHz. As the frequency decreases or increases beyond this range, more
and more acoustic intensity (i.e., pressure) is required to produce the same
perception of “loudness.”

Figure 14.4 shows a number of equal-loudness countours, each corre-
sponding to a different perceived loudness level. These curves show that more
pressure is required at low and high frequencies to achieve the same perceived
loudness than is needed at mid-range frequencies. Or put another way, if we
were to keep the amplitude of an acoustic pressure wave the same while vary-
ing the frequency, the human ear would actually perceive the lower and higher
frequencies as “less loud” than the mid-range frequencies. The lowest equal-
loudness contour represents the quietest audible tone and is also known as
the absolute threshold of hearing. The highest contour passes through the
human threshold of pain, which lies roughly at the 120 dB level for audible
sounds.

For more information on equal-loudness contours, and the Fletcher-Mun-
son curves on which they are based, see https://bit.ly/2HfCjCs.


<!-- source-pdf-page: 936 -->

14.1.2.2
The Audible Frequency Band

A typical adult can hear sounds with frequencies as low as 20 Hz and as high
as 20,000 Hz (20 kHz) (although the upper limit generally decreases with age).
The equal-loudness contours help to explain why the human ear can perceive
sounds only within this limited “band” of frequencies. As the frequency be-
comes lower or higher, more and more acoustic pressure is required to pro-
duce the same perceived loudness. As the frequency approaches the lower or
upper limits of human hearing, the countours become asympotically vertical,
meaning we’d need an effectively infinite acoustic pressure to produce any
perception of loudness at all. Or put another way, human audio perception
drops off to effectively zero outside the audible frequency band.

### 14.1.3 Sound Wave Propagation

Like any kind of wave, an acoustic pressure wave propagates through space
and can be absorbed or reflected by surfaces, diffracted around corners and
through narrow “slits,” and refracted as it passes across the boundary between
different transmission media. Sound waves exhibit no polarization1 because
the acoustic pressure oscillation occurs in the direction of wave travel (this is
known as a longitudinal wave), rather than perpendicular to it as with light
waves (a transverse wave). In games, we typically model the absorption, re-
flection and sometimes the diffraction (e.g., bending slightly around corners)
of our virtual sound waves, but we generally ignore refraction effects because
these effects are not easily noticed by a human listener.

14.1.3.1
Fall-Off with Distance

In an open space with otherwise perfectly still air, and assuming a sound
source that radiates equally in all directions, the intensity of the sound pressure
wave it produces falls off with distance, following a 1/r2 law, while pressure
follows a 1/r law.

p(r) ∝1

r ;

I(r) ∝1

r2 .

Here, r measures the radial distance of the listener or microphone from the
sound source, and both pressure and intensity are expressed as functions of r.

1Sound waves in solids can be transverse and therefore can exhibit polarization.


<!-- source-pdf-page: 937 -->
> Visual fallback for diagrams/images: [PDF page 937](../../../visual_pages/page_0937.jpg)

Figure 14.5. Three types of sound sources and their sound radiation patterns (in two dimensions
for ease of illustration). From left to right: omnidirectional, conical and directional.

More precisely, the sound pressure level for a spherically radiating (omni-
directional) sound wave in open space can be written as follows:

)
dB

(
1
4πr2

Lp(r) = Lp(0) + 10 log10

= Lp(0) −10 log10
(
4πr2)
dB,

where Lp(r) is the SPL at the listener as a function of its radial distance from
the sound source, and Lp(0) represents the unattenuated or “natural” sound
intensity of the source.
Sound sources are not always omnidirectional. For example, when a large,
flat wall reflects sound waves, it acts like a purely directional sound source—the
reflected waves propagate in a single direction, and the pressure wavefronts
are essentially parallel.
A bullhorn projects sound in a particular direction but with a conical fall-off,
meaning that the intensity of the sound waves is maximum along the center-
line of the projection “cone,” but falls off as the angle between the listener and
this centerline increases.
Various sound radiation patterns are illustrated in Figure 14.5.

14.1.3.2
Atmospheric Absorption

The 1/r fall-off of sound pressure with distance arises because energy is dis-
sipated as the waveform expands geometrically. This fall-off affects sounds
of all frequencies equally. Sound intensity also falls off with distance due to
energy absorption by the atmosphere. Atmospheric absorption effects are not
uniform across the entire frequency spectrum. In general, the absorption effect
becomes greater as the frequency of the sound increases.
I’m reminded of a story I heard when I was in high school: A woman was
walking down a quiet village street at night. She heard a sporadic sequence of


<!-- source-pdf-page: 938 -->

low tones with long, silent gaps between them. Curious what might be mak-
ing these strange tones, she walked toward them. As she walked, the tones
became louder and the spaces between the tones seemed to get shorter. After
a few minutes’ walk, the tones had resolved into a beautiful piece of music.
The woman arrived at an open window to discover a viola player practicing
within. The musician stopped playing to say “hello,” and the woman asked
him why he had been playing random notes a few minutes before. He replied,
“I haven’t been playing random notes—I’ve been playing this piece the whole
time.” The explanation for what the woman heard, of course, is that lower-
frequency sounds can be heard over longer distances than higher-frequency
sounds due to atmospheric absorption.
You can learn more about sound
wave propagation at http://www.sfu.ca/sonic-studio/handbook/Sound_
Propagation.html.
Other factors also affect the intensity of sound waves as they propagate
through their medium. In general, fall-off depends on distance, frequency,
temperature and humidity. See http://sengpielaudio.com/calculator-air.htm
for an online calculator that lets you experiment with the effects of these
factors.

14.1.3.3
Phase Shift and Interference

When multiple sound waves overlap in space, their amplitudes add toge-
ther—this is called superposition. Consider two periodic sound waves with the
same frequency. (The simplest example would be two sinusoids.) If the waves
are in phase—that is, their peaks and troughs line up—then the waves will pos-
itively reinforce each other, and the result is a wave with larger amplitude than
either of the original waves. Likewise, if the waves are out of phase, the peaks
of one wave can tend to cancel the troughs of the other and vice versa, and the
result is a wave with lower (or even zero) amplitude.
When multiple waves interact, we call this interference. Constructive inter-
ference describes the case in which the waves reinforce one another and the am-
plitude increases. Destructive interference occurs when the waves cancel each
other out, resulting in lower amplitude.
The frequency of the waves has an important effect on this phenomenon:
If the frequencies of the two waves match closely, the interference simply in-
creases or decreases the overall amplitude. If the frequencies differ signifi-
cantly, we can get an effect called beating, wherein the frequency difference
causes alternating periods of the waves being in and out of phase, resulting in
alternating periods of higher and lower amplitude.
Interference can occur between two totally unrelated sound signals, or it
can occur if a single sound signal takes multiple paths from the source to the


<!-- source-pdf-page: 939 -->

listener. In the latter case, the difference in path lengths introduces a phase
shift that can cause either constructive or destructive interference, depending
on the amount of the phase shift.

Comb Filtering

Interference can lead to an effect known as comb filtering. This is caused when
sound waves reflect off surfaces in such a way as to either almost completely
cancel or completely reinforce certain frequencies. The result is a frequency
response (see Section 14.2.5.7) with lots of narrow peaks and troughs, which
when plotted look a bit like a comb (hence the name). This effect can have a big
impact on audio reproduction and recording—sometimes it is an undesirable
artifact, and sometimes it is used as a tool. The existence of comb filtering
is also one of the key reasons why it is generally better to spend money on
acoustic room treatment than to spend money on high-end audio equipment:
If the room exhibits comb effects, you’re wasting your time trying to get a flat
response from your gear. See http://www.realtraps.com/video_comb.htm for
a great video by Ethan Winer on the subject.

14.1.3.4
Reverb and Echo

In any environment containing sound-reflective surfaces, a listener generally
receives three kinds of sound waves from a sound source:

•
Direct (dry). Sound waves that arrive at the listener via a direct, un-
obstructed path from the source are collectively known as direct or dry
sound.

•
Early reflections (echo). Sound waves that arrive at the listener via an indi-
rect path, after being reflected from and partially absorbed by surround-
ing surfaces, take a longer time to reach the listener because their path
is longer. As such, there will be a delay between the arrival of the di-
rect sound waves and the arrival of the reflected waves. The first group
of reflected sound waves arriving at the ear have only interacted with
one or two surfaces. As such, they are relatively “clean” signals, and we
perceive them as distinct new “copies” of the sound or echos.

•
Late reverberations (tail). Once the sound waves have bounced around the
listening space more than a few times, they superimpose and interfere
with one another so much that the brain can no longer detect distinct
echos. These are known as late reverberations or the diffuse tail. The prop-
erties of the reflective surfaces cause the amplitudes of the waves to be
attenuated by varying amounts. And because the reflected sound waves


<!-- source-pdf-page: 940 -->
> Visual fallback for diagrams/images: [PDF page 940](../../../visual_pages/page_0940.jpg)

p(t)

Dry
Wet

Early
Reflections

Late Reverberations
(Diffuse Tail)

t

1600 ms
100 ms
50 ms
0 ms

Figure 14.6. Direct sound waves, early reﬂections and late reverberations.

are delayed, phase shifts occur causing the waves to interfere with one
another. This causes certain frequencies to be attenuated relative to the
others. When we speak of the acoustics of a space, we are largely speak-
ing about the effects of late reverberations on the perceived “quality” or
“timbre” of the sound.

Collectively, the echos and the tail combine with the dry sound to create what
is known as wet sound. Figure 14.6 illustrates the wet and dry components of
a single abrupt clap.
The early reflections and late reverberations provide the brain with a
wealth of cues that tell us quite a lot about the type of space we’re in. The
pre-delay is the time interval between the arrival of the direct sound waves and
the arrival of the very first reflected waves. From the pre-delay, the brain can
determine the approximate size of the room or space in which we are listen-
ing. The decay is the time it takes for the reflected sound waves to die away.
This tells our brains how much of the sound has been absorbed by the sur-
roundings, and so indirectly tells us something about the materials that make
up the space we’re in. For example, a small tiled bathroom would produce
late reverberations with a very short pre-delay (due to its small size) and a
long decay (due to the tile’s ability to reflect sound waves efficiently, with
little absorption). A large granite-walled room like Grand Central Terminal
(a.k.a. Grand Central Station) in New York City will have a much longer pre-
delay and a lot more echos, but the decay will be similar to that of the tiled
bathroom.
If we were to hang curtains in that bathroom, or if the walls were covered
with wood panels instead of tile, the pre-delay would remain the same, but


<!-- source-pdf-page: 941 -->

the decay, along with other factors such as density (how closely spaced in time
the individual reflections are) and diffusion (the rate at which the reflections
increase in density over time), would change. This explains how a person
can guess where they are even when blindfolded, or how the blind can learn
to navigate with only a cane to aid them. Sound provides us with a lot of
information about our surroundings!
The term reverb is used to describe the quality of a sound in terms of
its wet components. In the early days of audio recording, sound engineers
had little control over reverb, relying entirely on the shape and construction
of the room in which the recording was made.
Later, simple artificial re-
verb devices were created, from the use of a speaker and microphone in a
bathroom by Bill Putman Sr. (founder of Universal Audio), to the use of a
long metal plate or spring to introduce a delay in a sound signal, to modern
digital techniques. Today, digital signal processor (DSP) chips and/or soft-
ware are used not only to recreate natural reverb effects in recorded sound
effects and music, but also to augment recordings with all sorts of interest-
ing effects that are not normally heard in nature.
We’ll learn more about
digital signal processing in Section 14.2. You can read more about reverb at
http://www.uaudio.com/blog/the-basics-of-reverb.
An anechoic chamber is a room especially designed to entirely eliminate re-
flected sound waves. This is accomplished by lining the walls, floor and ceiling
of the room with thick corrugated foam padding that absorbs essentially all of
the reflected sound waves. As a result, only the direct (dry) sound reaches
the listener or microphone. Sound in an anechoic chamber has a completely
“dead” timbre. Anechoic chambers are useful for recording “pure” sounds
that contain no reverb. Such pure sounds are often perfect candidates for in-
put into a digital signal processing pipeline, giving a sound designer maxi-
mum flexibility to control the timbre of the sound.

14.1.3.5
Sound in Motion: The Doppler Effect

If you’ve ever stood at a railway crossing when a train goes by, you’ve heard
the Doppler effect in action. The sound of the train seems higher pitched when it
is approaching you, and becomes lower pitched as it races off into the distance.
Sound waves travel at a roughly constant speed through the air, but the sound
source (in this case, the train) is also moving. The sound waves that are moving
in the same direction as the train become “squashed together,” and the waves
that are moving opposite to the motion of the train become “spread out,” each
by an amount proportional to the difference between the speed of sound in
air and the speed of the train through the air. The frequency of the squashed
waves is therefore increased, because the space between the peaks and troughs


<!-- source-pdf-page: 942 -->

of the sound waves has been effectively reduced, resulting in a higher-pitched
sound. Likewise, the frequency of the spread-out waves is decreased, resulting
in a lower-pitched sound. The Doppler effect was named after the Austrian
physicist Christian Doppler, who identified it in 1842.
The Doppler effect also occurs when the listener is moving but the sound
source is stationary. In general, the Doppler shift is dependent upon the rel-
ative velocity (as a vector) between the listener and the sound source. In one
dimension, the Doppler shift amounts to a change in frequency, and can be
quantified as follows:

)
f,

f ′ =
( c + vl

c + vs

where f is the original frequency, f ′ is the Doppler-shifted (observed) fre-
quency at the listener, c is the speed of sound in air and vl and vs are the
speeds of the listener and sound source, respectively. If the speeds of the
sound sources are very small relative to the speed of sound, we can approxi-
mate this relationship as follows:

f ′ =
(1 + (vl −vs)

)
f

c

=
(1 + ∆v

)
f.

c

This expression makes the relative velocity, ∆v, apparent. The Doppler effect
can be easily visualized by looking at the following animated GIF: http://en.
wikipedia.org/wiki/File:Dopplereffectsourcemovingrightatmach0.7.gif.

### 14.1.4 Perception of Position

The human auditory system has evolved to allow a reasonably accurate per-
ception of the position of sounds in the space around us. A number of factors
contribute to our perception of sound position:

•
Fall-off with distance provides us with a rough idea of how far away the
source of a sound is. In order for this to work, we must have some idea
of the loudness of the sound when heard at close range to serve as a
“baseline.”

•
Atmospheric absorption causes the higher frequencies in a sound to drop
out as the source moves farther away from the listener. This can serve
as an important cue in perceiving the difference between, for example, a
person speaking at normal volume but far away and a person speaking
at reduced volume up close.


<!-- source-pdf-page: 943 -->

•
Having two ears, one on the left and one on the right, gives us a great
deal of positional information. A sound that is to our right will sound
louder in the right ear than in the left. An interaural time difference (ITD) of
approximately one millisecond also arises, because a sound to one side
of your head will take just a little bit longer to reach the opposite ear.
Finally, the head itself obstructs sounds, so the ear opposite to the sound
source will perceive a slightly muffled version of the sound reaching the
near ear. This is known as interaural intensity difference (IID).

•
Ear shape has an effect as well. Our ears are cupped slightly forward, so
sounds coming from behind us are very slightly muffled relative to those
coming from in front of us.

•
The head-related transfer function (HRTF) is a mathematical model of the
minute effects that the folds of our ears (the pinnae) have on sounds com-
ing from different directions.

## 14.2 The Mathematics of Sound

Signal processing and systems theory is the area of mathematics that lies at the
heart of virtually all modern audio technology. It is also extensively used in a
wide variety of other technological and engineering endeavors, including im-
age processing and machine vision, aeronautics, electronics, fluid dynamics,
and the list goes on. In this section, we’ll embark on a whirlwind tour of the
key concepts in signals and systems theory, because it will help us to under-
stand some of the more advanced topics in game audio later in the chapter.
(It’s also an important area of mathematical theory that can benefit any game
programmer—so what the heck!) An in-depth treatment of the topic can be
obtained from [41].

### 14.2.1 Signals

A signal is any function of one or more independent variables, typically de-
scribing the behavior of some kind of physical phenomenon. In Section 14.1,
we used the signal p(t) to represent the time-varying acoustic pressure of an
audio compression wave. Of course, many other kinds of signals are possi-
ble. A signal v(t) might represent the voltage produced by a microphone over
time, while w(t) might model the time-varying water pressure in a system of
pipes, or we could use f (t) to represent the varying population of foxes in an
ecosystem.
In studying signal theory, we often refer to the independent variable as
“time” and represent it with the symbol t—but of course the independent vari-


<!-- source-pdf-page: 944 -->
> Visual fallback for diagrams/images: [PDF page 944](../../../visual_pages/page_0944.jpg)

able might represent some other quantity, and there may be more than one in-
dependent variable. For example, one can think of a 2D greyscale image as a
signal i(x, y), where the two independent variables, x and y, represent the or-
thogonal coordinate axes, and the signal value i represents the intensity of the
greyscale image at each pixel. A color image could be similarly represented
by three signals, r(x, y) for the red channel, g(x, y) for the green channel and
b(x, y) for the blue channel.

14.2.1.1
Be Discrete, Continuously

The 2D image examples above bring to light an important distinction between
two fundamental kinds of signal: continuous and discrete.

•
If the independent variable is a real number (t ∈R), we call the signal a
continuous-time signal. In this chapter, we’ll use the symbol t to represent
continuous “time,” and we’ll use round parentheses for our functional
notation (e.g., x(t)) to remind us that we’re working with a continuous-
time signal.

•
If the independent variable(s) is an integer (n ∈I), we call the signal a
discrete-time signal. We’ll use the symbol n to represent discrete “time,”
and we’ll use square brackets for our functional notation (e.g., x[n]) to
remind us that we’re working with a discrete-time signal. Note that the
value of a discrete-time signal might still be a real number (x[n] ∈R)—
the only thing the term “discrete-time signal” prescribes is that the inde-
pendent variable is an integer (n ∈I).

In Figure 14.1, we saw that we can visualize a continuous-time signal as an or-
dinary function plot, with time t on the horizontal axis and the signal value
p(t) on the vertical axis. We can plot a discrete-time signal x[n] in similar
fashion, although the function’s values are only defined for integer values of
the independent variable n (see Figure 14.7). One common way to think of a
discrete-time signal is as a sampled version of a continuous-time signal. The
sampling process (also known as digitization or analog-to-digital conversion) lies
at the heart of digital audio recording and playback. See Section 14.3.2.1 for
more information on sampling.

### 14.2.2 Manipulating Signals

It will be important in the following discussions for us to understand some
basic ways to manipulate a signal by making changes to its independent vari-
able. For example, to reflect a signal about t = 0, we simply replace t with −t
in the signal’s equation. To time-shift the entire signal to the right (i.e., in the


<!-- source-pdf-page: 945 -->
> Visual fallback for diagrams/images: [PDF page 945](../../../visual_pages/page_0945.jpg)

positive direction) by a distance s, we replace t with t −s in the signal’s equa-
tion. (Time shifting to the left/negative direction is accomplished by replacing
t with t + s.) We can also expand or compress the domain of the signal by scal-
ing the independent variable. These simple transformations are illustrated in
Figure 14.8.

### 14.2.3 Linear Time-Invariant (LTI) Systems

In the context of signal processing theory, a system is defined as any device or
process that transforms an input signal into a new output signal. The math-
ematical concept of a system can be used to describe, analyze and manipu-
late many real-world systems that arise in audio processing, including micro-
phones, speakers, analog-to-digital converters, reverb units, equalizers and
filters and even the acoustics of a room.
As a simple example, an amplifier is a system that increases the amplitude
of its input signal by a factor A known as the gain of the amp. Given an in-
put signal x(t), such an amplification system would produce an output signal
y(t) = Ax(t).
A time-invariant system is one for which a time shift in the input signal
causes an equal time shift in the output signal. In other words, the behavior
of the system does not change over time.
A linear system is one that possesses the property of superposition. This
means that if an input signal consists of a weighted sum of other signals, then
the output is a weighted sum of the individual outputs that would have been
produced, had each of the other signals been fed through the system indepen-
dently.
Linear time-invariant (LTI) systems are extremely useful for two reasons.

x[n]

n

Figure 14.7. The value of a discrete-time signal x[n] is deﬁned only for integer values of n.


<!-- source-pdf-page: 946 -->
> Visual fallback for diagrams/images: [PDF page 946](../../../visual_pages/page_0946.jpg)

x(t)

x(–t)

t

t

x(2t)

x(t – s)

s

t

t

Figure 14.8. Simple manipulations of a signal’s independent variable.

First, their behaviors are well understood and relatively easy to work with
mathematically. Second, many real physical systems in the fields of audio
propagation theory, electronics, mechanics, fluid flow, etc. can be modeled ac-
curately using LTI systems. As such, we will restrict ourselves to a discussion
of LTI systems for our purposes of understanding audio technology.

We can visualize any system as a black box with an input signal and an
output signal, as shown in Figure 14.9.
Using this black-box notation, simple systems can be conveniently inter-
connected to construct more complex systems. For example:

Figure 14.9. A system as
a black box.

•
The output of system A could be connected to the input of system B,
yielding a composite system that performs operation A followed by op-
eration B. This is called a serial connection.

•
The outputs of two systems could be added together.

•
The output of a system could be fed back into an earlier input, yielding
what is known as a feedback loop.

See Figure 14.10 for examples of all of these kinds of connections.
One very important property of all LTI systems is that their interconnec-
tions are order-independent. So if we have a serial connection of system A fol-
lowed by system B, we can reverse the order of the two systems and the output
will remain unchanged.


<!-- source-pdf-page: 947 -->
> Visual fallback for diagrams/images: [PDF page 947](../../../visual_pages/page_0947.jpg)

### 14.2.4 Impulse Response of an LTI System

It’s all fine and dandy to talk about systems that convert an input signal into
an output signal, and it’s even pretty intuitive to draw diagrams of system
interconnections. But how can we describe the operation of a system mathe-
matically?
Recall from Section 14.2.3 that for a linear system, if the input consists of a
linear combination (weighted sum) of input signals, the output will be a linear
combination (weighted sum) of the individual outputs (had each of the input
signals been fed into the system independently). So, if we can figure out a way
to represent an arbitrary input signal as a weighted sum of very simple signals,
we should be able to describe the behavior of the system by describing only its
response to those very simple signals.

14.2.4.1
The Unit Impulse

If we are going to describe an input signal as a linear combination of simple
signals, the question arises: Which simple signal shall we use? For reasons
that will become clear in a moment, our signal of choice is going to be the unit
impulse. This signal is one of a family of related functions known as singularity
functions because they all contain at least one discontinuity or “singularity.”
In discrete time, the unit impulse δ[n] is as simple as it gets: It is a signal
whose value is zero everywhere except at n = 0, where its value is one:

δ[n] =
{
1
if n = 0,
0
otherwise.

The discrete-time unit impulse is illustrated in Figure 14.11.

Serial

B
y(t)

A
x(t)

Parallel

Feedback Loop

y(t)
x(t)

a

+

A

+
x(t)

y(t)

–a
d
dt
y(t)

b

B

Figure 14.10. Various ways to interconnect systems. In the serial connection, y(t) = B(A(x(t))). In
the parallel connection, y(t) = aA(x(t)) + bB(x(t)). In the feedback loop, y(t) = x(t) −a ˙y(t).


<!-- source-pdf-page: 948 -->
> Visual fallback for diagrams/images: [PDF page 948](../../../visual_pages/page_0948.jpg)

In continuous time, the unit impulse δ(t) is a bit trickier to define. It is a
function whose value is zero everywhere except at t = 0, where its value is
infinite—but the area under the curve is equal to one.
To see how such a strange beast of a function might be formally defined,
imagine a “box” function b(t), whose value is zero everwhere except in the
interval [0, T), where its value is 1/T. The area under this curve is just the
area of the box, width times height, or T × 1

T = 1. Now imagine the limit
as T →0. As this happens, the width of the box approaches zero and its
height approaches infinity, but its area remains equal to 1. This is shown in
Figure 14.12.
The unit impulse function is typically denoted by the symbol δ(t). It can
be formally defined as follows:

δ(t) = lim
T→0 b(t),

where

b(t) =
{ 1/T
if t ≥0 and t < T,
0
otherwise.

As shown in Figure 14.13, we typically plot the unit impulse by drawing
an arrow whose height represents the area under the curve (since the actual
“height” of the function at t = 0 is infinite).

14.2.4.2
Using an Impulse Train to Represent a Signal

Now that we know what the unit impulse signal is, let’s see if we can describe
an arbitrary signal x[n] as a linear combination of unit impulses. (Spoiler alert:
It turns out we can.)
The function δ[n −k] is a time-shifted discrete unit impulse, whose value is
zero everywhere except at time n = k, where it is equal to one. In other words,
the unit impulse δ[n −k] is “positioned” at time k. Consider an impulse at one
particular value of k (say, k = 3). Let’s make sure that the “height” of that
impulse matches the value of the original function at k = 3 by “scaling” the
impulse by x[3], yielding x[3]δ[n −3]. If we rinse and repeat for all possible

Figure 14.11. The unit impulse in discrete time.


<!-- source-pdf-page: 949 -->
> Visual fallback for diagrams/images: [PDF page 949](../../../visual_pages/page_0949.jpg)

b t

1/T

t
b t
T

t
T

Figure 14.13. The value of the unit impulse function δ(t) is
zero everywhere except at t = 0, where it is inﬁnite. It is
drawn as an arrow of unit height to indicate that the area
under the curve is 1.

Figure 14.12. The unit impulse can be deﬁned as the limit of a
box function b(t) whose width approaches zero.

values of k, we get a train of impulses of the form x[k]δ[n −k]. Adding all these
scaled, time-shifted impulse functions together is just another way of writing
the original signal x[n]:

x[n] =
+∞
∑
k=−∞
x[k]δ[n −k].
(14.4)

We won’t give a rigorous proof here, but it’s probably not too hard to be-
lieve that doing this in continuous time works in pretty much the same way.
The only difficulty is that for continuous time, the sum in Equation (14.4) be-
comes an integral. Let’s imagine an infinite sequence of time-shifted unit im-
pulses δ(t −τ), each one located at a different time τ. We can build up an arbi-
trary signal x(t) in an analogous fashion to the discrete-time case, as follows:

x(t) =
∫+∞

τ=−∞x(τ)δ(t −τ) dτ.
(14.5)

14.2.4.3
Convolution

Equation (14.4) tells us how to represent a signal x[n] as a linear combination
of simple, time-shifted unit impulse signals δ[n −k]. Let’s imagine putting
just one of these weighted impulse inputs (x[k]δ[n −k]) through the system. It
doesn’t matter which one we choose, so let’s select the one at k = 0. This gives
us the input signal x[0]δ[n].
We’ll use the notation x[n] =⇒y[n] to indicate that an input signal x[n]
is being transformed by an LTI system into an output signal y[n]. So we can
write:

x[0]δ[n] =⇒y[n].


<!-- source-pdf-page: 950 -->
> Visual fallback for diagrams/images: [PDF page 950](../../../visual_pages/page_0950.jpg)

h[n]

[n]

n
n

h(t)

(t)

t
t

Figure 14.14. Examples of the impulse response of a system in discrete and continuous time.

The value of x[0] is just a constant, so because we’re dealing with a linear sys-
tem, the output y[n] will just be that same constant times the system’s response
to the unit impulse δ[n]. Let’s use the signal h[n] to represent the system’s re-
sponse to a “bare” unit impulse: δ[n] =⇒h[n]. The signal h[n] is called the
impulse response of the system. So we can write the system’s response to our
simple input signal as follows:

x[0]δ[n] =⇒x[0]h[n].

The concept of impulse response is illustrated in Figure 14.14.
The response of an LTI system to a time-shifted unit impulse is just a time-
shifted impulse response (δ[n −k] =⇒h[n −k]). So for values of k other than
zero, everything works out exactly the same except that now the input and
output signals are both time-shifted by k:

x[k]δ[n −k] =⇒x[k]h[n −k].

To find the system’s response to the entire input signal x[n], we just sum
up the responses to each individual time-shifted component, like this:

+∞
∑
k=−∞
x[k] δ[n −k] =⇒
+∞
∑
k=−∞
x[k] h[n −k].

In other words, the output of our system can be written as follows:

y[n] =
+∞
∑
k=−∞
x[k] h[n −k].
(14.6)

This very important equation is known as the convolution sum. It’s conve-
nient to introduce a new mathematical operator ∗to represent the operation


<!-- source-pdf-page: 951 -->
> Visual fallback for diagrams/images: [PDF page 951](../../../visual_pages/page_0951.jpg)

of convolution:

x[n] ∗h[n] =
+∞
∑
k=−∞
x[k] h[n −k].
(14.7)

Equations (14.6) and (14.7) give us a way to calculate an LTI system’s re-
sponse y[n] to any arbitrary input signal x[n], given only the impulse response of
the system, h[n]. In other words, for LTI systems, the impulse response signal
h[n] completely describes the system. Pretty cool stuff.

Convolution in Continuous Time

In our discussions above, we worked in discrete time to keep things simple. In
continuous time, everything works out in pretty much the same way. The only
difference is that summations become integrals, and we need to remember to
include the differential dτ in our equations.
When we apply an arbitrary signal x(t) to the input of a continuous-time
LTI system, the output signal can be written as follows:

y(t) =
∫+∞

τ=−∞x(τ) h(t −τ) dτ.
(14.8)

As before, we’ll use the operator ∗as a shorthand for convolution:

x(t) ∗h(t) =
∫+∞

τ=−∞x(τ) h(t −τ) dτ.
(14.9)

x(

)

Analogous to the convolution sum, the integral in Equations (14.8) and (14.9)
is known as the convolution integral.

14.2.4.4
Visualizing Convolution

Let’s try to visualize the convolution operation in the continuous time case. To
evaluate y(t) = x(t) ∗h(t) for one specific value of t (say, t = 4), we perform
the following steps as illustrated in Figure 14.15:

h(

h(t –

)

)

t

1.
Plot x(τ), using τ as the time variable because t is fixed (at t = 4 in this
example).

) h(t –

x(

)

2.
Plot h(t −τ). We can rewrite this as h(−τ + t). Because τ is negated,
we know that the impulse response has been flipped about τ = 0. And
because we’ve added t to the independent variable, we know the signal
has been shifted to the left by t = 4 units.

Integrate to find area

under this curve.

Figure 14.15. Visualization
of the convolution oper-
ation in continuous time.

3.
Multiply the two signals together across the entire τ axis.


<!-- source-pdf-page: 952 -->

4.
Integrate from −∞to +∞along the τ axis to find the area under the
resulting curve. This is the value of y(t) at this one specific value of t (in
this example, t = 4).

Remember that we must repeat this procedure for every possible value of t in
order to determine the complete output signal y(t).

14.2.4.5
Some Properties of Convolution

The properties of the convolution operation are surprisingly analogous to
those of ordinary multiplication. Convolution is:

•
commutative: x(t) ∗h(t) = h(t) ∗x(t);

•
associative: x(t) ∗
(
h1(t) ∗h2(t)
) =
(
x(t) ∗h1(t)
) ∗h2(t); and

•
distributive: x(t) ∗
(
h1(t) + h2(t)
) =
(
x(t) ∗h1(t)
) +
(
x(t) ∗h2(t)
)
.

### 14.2.5 The Frequency Domain and the Fourier Transform

In order to arrive at the concepts of impulse response and convolution, we de-
scribed a signal as a weighted sum of unit impulses. We can also represent
a signal as a weighted sum of sinusoids. Representing a signal in this man-
ner essentially breaks it up into its frequency components. This will allow us to
derive another incredibly powerful mathematical tool—the Fourier transform.

14.2.5.1
The Sinusoidal Signal

A sinusoidal signal is produced when a circular motion in two dimensions is
projected onto a single axis. An audio signal in the form of a sinusoid produces
a “pure” tone at one specific frequency.
The most basic sinusoidal signal is the sine (or cosine) function. The signal
x(t) = sin t takes on the value 0 at t = 0, π and 2π, has a value of 1 at t = π

2
and has a value of −1 at t = 3π

2 .
The most general form of a real-valued sinusoidal signal is

x(t) = A cos(ω0t + ϕ).
(14.10)

Here, A represents the amplitude of the sine wave (i.e., the peaks and troughs
of the cosine wave hit maximum and minimum values of A and −A, respec-
tively). The angular frequency is ω0, measured in radians/second (see Section
14.1.1 for a discussion of frequency and angular frequency). ϕ represents a
phase offset (also measured in radians) that shifts the cosine wave to the left or
right along the time axis.


<!-- source-pdf-page: 953 -->
> Visual fallback for diagrams/images: [PDF page 953](../../../visual_pages/page_0953.jpg)

When A = 1, ω0 = 1 and ϕ = 0, Equation (14.10) reduces to x(t) = cos t.
When ϕ = π

2 , the expression becomes x(t) = sin t. The cos function represents
the projection of a circular motion onto the horizontal axis, while sin represents
its projection onto the vertical axis.

14.2.5.2
The Complex Exponential Signal

The cosine function isn’t actually the best tool for representing a signal as a
sum of sinusoids. The math is much simpler and more elegant if we make use
of complex numbers instead. In order to understand how this works, we need
to review complex math, and take a look at how multiplication of complex
numbers works. So bear with me here—all will become clear by the time we’re
done.

A Brief Review of Complex Numbers

You’ll probably remember from high-school math class that a complex number
is a kind of two-dimensional quantity consisting of a real part and an imagi-
nary part. Any complex number can be written as follows: c = a + jb, where
a and b are real numbers and j = √−1 is the imaginary unit. The real part of c
is a = Re(c), and its imaginary part is b = Im(c).
You can visualize a complex number as a kind of “vector” [a, b] in a two-
dimensional space known as the Argand plane. It’s important to remember,
however, that complex numbers and vectors are not interchangeable—their
mathematical behaviors are quite different.
We define the magnitude of a complex number as the length of its 2D “vec-
tor” representation in the complex plane: |c| =
√

a2 + b2. The angle the vector
makes with the real axis is known as its argument: arg c = tan−1(b/a). (The
argument of a complex number is sometimes called its phase. As we’ll see, the
term “phase” is closely related to the phase offset ϕ in Equation (14.10).) The
magnitude and argument of a complex number are depicted in Figure 14.16.

Figure 14.16. The magnitude |c| =
√

a2 + b2 of a complex number is its length in the complex plane,
and its argument arg c = tan−1(b/a) is the angle it makes with the Re axis.


<!-- source-pdf-page: 954 -->

Complex Multiplication and Rotation

We won’t get into all of the properties of complex numbers here. Check out
http://www.math.wisc.edu/∼angenent/Free-Lecture-Notes/freecomplexnu
mbers.pdf for an in-depth discussion of complex number theory. However,
there is one mathematical operation that does concern us here: the operation
of complex multiplication.
Complex numbers are multiplied algebraically (no dot or cross products
here):

c1c2 = (a1 + jb1)(a2 + jb2)
= (a1a2) + j(a1b2 + a2b1) + j2b1b2
= (a1a2 −b1b2) + j(a1b2 + a2b1).
(14.11)

If you work out2 the magnitude and argument (angle) of the product c1c2,
you’ll find that the magnitude is equal to the product of the two input magni-
tudes, and the argument is the sum of the input arguments:

|c1c2| = |c1||c2|;

arg(c1c2) = arg c1 + arg c2.
(14.12)

The fact that multiplication of complex numbers causes their angles (ar-
guments) to add means that complex multiplication produces a rotation in the
complex plane. If the magnitude of c1 is unity (|c1| = 1), then the magnitude
of the product will be equal to the magnitude of c2 (|c1c2| = |c2|). In this case,
the product represents a pure rotation of c2 by an angle equal to arg c1 (see Fig-
ure 14.17). If |c1| ̸= 1, then the product’s magnitude will be scaled by |c1|, and
the result is that c2 undergoes a spiral motion in the complex plane.
This explains why unit-length quaternions operate as rotations in 3D space!
A quaternion is essentially a four-dimensional complex number, with one real
part and three imaginary parts. So a quaternion follows the same basic rules in
three dimensions that a regular complex number follows in two dimensions.
The fact that complex multiplication produces a rotation makes sense when
we consider what happens when we multiply j by itself many times:

1 × j = j,

j × j =
√

−1
√

−1 = −1,
−1 × j = −j,
−j × j = 1,
. . .

2Yikes—this sounds an awful lot like an exercise for the reader…


<!-- source-pdf-page: 955 -->
> Visual fallback for diagrams/images: [PDF page 955](../../../visual_pages/page_0955.jpg)

Im

c2

c c
1   2

+j

2
c1

arg c = 80

arg c c = 110

arg c = 30

1   2

1

= 30

 + 80

+1
–1

Re

|c | = |c | = 1
1                2

–j

Figure 14.17. Multiplying two complex numbers together that both have magnitudes of 1 produces
a pure rotation in the complex plane.

1

0
2

3

Figure 14.18. Multiplying the imaginary number j = √−1 by itself acts like rotating a unit vector by
90 degrees in the complex plane.

So multiplying j by itself is like rotating a unit vector by 90 degrees in the com-
plex plane. In fact, multiplying any complex number by j has the effect of
rotating it by 90 degrees. This is illustrated in Figure 14.18.

The Complex Exponential and Euler’s Formula

For any complex number c with |c| = 1, the function f (n) = cn, with n taking
on a sequence of increasing positive real values, will trace out a circular path in


<!-- source-pdf-page: 956 -->
> Visual fallback for diagrams/images: [PDF page 956](../../../visual_pages/page_0956.jpg)

Im

Im(c ) = sin(n arg c)
n

+j

c5

c4

c3

c2

c

Re
+1
–1

n

|c| = 1

–j

Figure 14.19. Multiplying a complex number by itself repeatedly traces out a circular path in the
Argand plane, producing a sinusoid when projected onto any axis through the origin.

the complex plane. Any circular path in two dimensions traces out a sine curve
along the vertical axis, and a corresponding cosine curve along the horizontal
axis. This is illustrated in Figure 14.19.
Raising a complex number to a real power (cn) produces rotation in the com-
plex plane, and therefore yields a sinusoid when projected onto any axis in
the plane. As it turns out, we can also get this rotational effect by raising a
real number to a complex power (nc). This means that we can write Equation
(14.10) in terms of complex numbers as follows:

ejω0t = cos ω0t + j sin ω0t, t ∈R;
(14.13)

Re
[
ejω0t]
= cos ω0t;

Im
[
ejω0t]
= sin ω0t,

where e ≈2.71828 is the real transcendental number that defines the base of
the natural logarithm function.
Equation (14.13) is one of the most important equations in all of mathemat-
ics. It is known as Euler’s formula. Why it works is a bit of a mystery (even to
some seasoned mathematicians). The theorem can be explained by looking at
the Taylor series expansion of ejt, or by considering the derivative of ex and
then allowing x to become a complex number. But for our purposes, it should
suffice to rely on the intuitions we gained from looking at how complex mul-
tiplication results in rotation in the complex plane.


<!-- source-pdf-page: 957 -->

14.2.5.3
The Fourier Series

Now that we have the mathematical tools we need to represent sinusoids as
complex numbers, let’s turn our attention again to the task of representing a
signal as a sum of sinusoids.
Doing this is easiest when the signal is periodic. In this case, we can write
the signal as a sum of harmonically related sinusoids:

x(t) =
+∞
∑
k=−∞
akej(kω0)t.
(14.14)

We call this the Fourier series representation of the signal. Here, the complex
exponential functions ej(kω0)t are the sinusoidal components from which we
are building up the signal. These components are harmonically related, in that
each one has a frequency that is an integer multiple k of the so-called fundamen-
tal frequency ω0. The coefficients ak represent the “amount” of each harmonic
present in the signal x(t).

14.2.5.4
The Fourier Transform

A full explanation of this topic is beyond the scope of this book, but for our
purposes it will suffice to state (without any proof whatsoever!) that any rea-
sonably well-behaved signal,3 even signals that are non-periodic, can be repre-
sented as a linear combination of sinusoids. In general, an arbitrary signal may
contain components at any frequency, not just frequencies that are harmoni-
cally related. As such, the discrete set of harmonic coefficients ak in Equation
(14.14) becomes a continuum of values representing “how much” of each fre-
quency the signal contains.
We can envision a new function X(ω) whose independent variable is the
frequency ω rather than time t, and whose value represents the amount of each
frequency present in the original signal x(t). We say that x(t) is the time domain
representation of the signal, while X(ω) is its frequency domain representation.
Mathematically, we can find the frequency domain representation of a sig-
nal from its time domain representation, and vice versa, by using the Fourier
transform:

X(ω) =
∫+∞

−∞x(t)e−jωtdt;
(14.15)

∫+∞

x(t) = 1

−∞X(ω)ejωtdω.
(14.16)

2π

3All signals that meet the so-called Dirichlet conditions have Fourier transforms and are there-
fore “reasonably well-behaved” for our purposes.


<!-- source-pdf-page: 958 -->
> Visual fallback for diagrams/images: [PDF page 958](../../../visual_pages/page_0958.jpg)

x(t) = e –at

(Bode Plot)
Time Domain

2
1
t

|X(

)| =

a2 +

Frequency Domain

a

arg X(

) = tan–1

Figure 14.20. The Fourier transform yields a complex-valued frequency domain signal. A Bode
plot is used to visualize this complex-valued signal in terms of its magnitude and its phase (or
argument).

If you compare Equation (14.16) to the Fourier series from Equation (14.14),
you can see the similarity. Rather than describing the “amounts” of the fre-
quency components via a discrete series of coefficients ak, we’re now describ-
ing them using the continuous function X(ω). But in both cases we’re repre-
senting x(t) as a “sum” of sinusoids.

14.2.5.5
Bode Plots

In general the Fourier transform of a real-valued signal is a complex-valued sig-
nal (X(ω) ∈C). When visualizing the Fourier transform, we often draw it
using two plots. For example, we might plot its real and imaginary compo-
nents. Or we might plot its magnitude and its argument (angle) on two dif-
ferent plots—a visualization known as a Bode plot (pronounced “Boh-dee”).
Figure 14.20 shows an example of a signal and its Bode plot.

14.2.5.6
The Fast Fourier Transform (FFT)

A collection of fast algorithms exist for calculating the Fourier transform in
discrete time. This family of algorithms is called, aptly enough, the fast Fourier
transform or FFT. You can read more about the FFT at http://en.wikipedia.org/
wiki/Fast_Fourier_transform.


<!-- source-pdf-page: 959 -->

14.2.5.7
Fourier Transforms and Convolution

It is interesting to note that convolution in the time domain corresponds to
multiplication in the frequency domain and vice versa. Given a system whose
impulse response is h(t), we know that we can find the output of the system
y(t) in response to an input x(t) as follows:

y(t) = x(t) ∗h(t).

In the frequency domain, given the Fourier transforms of the impulse response
H(ω) and the input X(ω), we can find the Fourier transform of the output as
follows:
Y(ω) = X(ω)H(ω).

This result is pretty incredible, and it’s also very handy. Sometimes it is more
convenient to perform a convolution on the time axis using a system’s im-
pulse response h(t), while at other times it’s more convenient to perform a
multiplication in the frequency domain using the system’s frequency response
H(ω).
As it turns out, LTI systems exhibit a property called duality, which says
that you can reverse the roles of time and frequency and virtually the same
mathematical rules continue to apply. So, for example, we can understand
how signal modulation (the multiplication of one signal by another) works in
the time domain by looking at what happens when we convolve the Fourier
transforms of the two signals on the frequency axis. Having two ways of tack-
ling a problem is always better than one!

14.2.5.8
Filtering

The Fourier transform allows us to visualize the set of frequencies that make
up virtually any audio signal. A filter is an LTI system that attenuates a se-
lected range of input frequencies while leaving all other frequencies unaltered.
A low-pass filter retains low frequencies while attenuating high frequencies. A
high-pass filter does the opposite, retaining high frequencies and attenuating
lower frequencies. A band-pass filter attenuates both low and high frequencies
but retains frequencies within a limited passband. A notch filter does the oppo-
site, retaining low and high frequencies but attenuating frequencies within a
limited stopband.
Filters are used in a stereo system’s equalizer, by attenuating or boosting
specific frequencies based on user inputs. Filters can also be used to attenuate
noise, if the spectra of the noise signal and the desirable signal occupy different
regions of the frequency axis. For example, if a high-frequency noise signal is


<!-- source-pdf-page: 960 -->
> Visual fallback for diagrams/images: [PDF page 960](../../../visual_pages/page_0960.jpg)

c

c

Figure 14.21. The frequency response H(ω) for an ideal ﬁlter has a value of one in the passband
and zero in the stopband.

adversely affecting a lower-frequency voice or music signal, a low-pass filter
could be used to eliminate the noise.

The frequency response H(ω) of an ideal filter looks like a rectangular box,
with a value of one in the passband and zero in the stopband. When we
multiply this by the Fourier transform of our input signal X(ω), the output
Y(ω) = X(ω)H(ω) will have its passband frequencies preserved exactly, and
its stopband frequencies all set to zero. The frequency response for an ideal
filter is shown in Figure 14.21.

Of course, an ideal filter that completely passes certain frequencies and
completely suppresses others may not be desirable. The frequency responses
of most real-world filters have a gradual fall-off between the passband and
stopband. This aids filtering in situations where there is no single, clear-cut
line between the desirable frequencies and the unwanted frequencies. The
frequency response of a low-pass filter with a gradual fall-off is shown in Fig-
ure 14.22.

The equalizer (EQ) found on most high-fidelity audio equipment permits
the user to adjust the amount of bass, mid-range and treble that is output. An
EQ is really just a collection of filters tuned to different frequency ranges and
applied in series to an audio signal.

Filtering theory is an immense field of study, so we can’t possibly do it
justice here. For a great deal more information, see [41, Chapter 6].

## 14.3 The Technology of Sound

Before we can fully understand the software that comprises a game’s audio
engine, we need a firm grasp of audio hardware and technology and of the
terminology used by industry professionals to describe it.


<!-- source-pdf-page: 961 -->
> Visual fallback for diagrams/images: [PDF page 961](../../../visual_pages/page_0961.jpg)

### 14.3.1 Analog Audio Technology

The earliest audio hardware was based on analog electronics. This was the
easiest way to record, manipulate and play back audio compression waves,
because sound is itself an analog physical phenomenon. In this section, we’ll
briefly explore some key analog audio technologies.

14.3.1.1
Microphones

A microphone (also known as a “mic” or “mike”) is a transducer that converts
an audio compression wave into an electronic signal. Microphones make use
of various technologies in order to convert the mechanical pressure variations
of a sound wave into an equivalent signal based on variations in electric volt-
age. A dynamic microphone uses electromagnetic induction, while a condenser
microphone utilizes changes in capacitance. Other types of mics use piezoelec-
tric generation or light modulation to produce a voltage signal.
Different microphones have different sensitivity patterns, known as polar
patterns. These patterns describe how sensitive the mic is to sound at various
angles about its central axis. An omnidirectional mic is equally sensitive in all
directions. A bidirectional mic has two sensitivity “lobes” in the shape of a
figure eight. A cardioid mic has essentially a unidirectional sensitivity profile,
so named because of its somewhat heart-shaped polar pattern. Some common

)|
10

20 log  |H(

0 dB

0.1/RC
1/RC
10/RC
100/RC

–20 dB

–40 dB

arg H(

)

0

0.1/RC
1/RC
10/RC
100/RC

/4

–

–

/2

Figure 14.22. The frequency response H(ω) for an RC (resistor-capacitor) low-pass ﬁlter with a
gradual fall-off. Both the horizontal and vertical axes of both plots are drawn using a logarithmic
scale.


<!-- source-pdf-page: 962 -->
> Visual fallback for diagrams/images: [PDF page 962](../../../visual_pages/page_0962.jpg)

0

0

–5 dB

–5 dB

–10 dB

–10 dB

–15 dB

–15 dB

–20 dB

–20 dB

–25 dB

–25 dB

270

90

90

270

0

–5 dB

–10 dB

180

180

–15 dB

–20 dB

–25 dB

270

90

180

Figure 14.23. Three typical microphone polar patterns, clockwise from upper left: omnidirectional, cardioid and bidirectional.

microphone polar patterns are illustrated in Figure 14.23.

14.3.1.2
Speakers

A speaker is basically a microphone operated in reverse—it is a transducer that
converts a varying input voltage signal into vibrations in a membrane, which
in turn gives rise to air pressure variations that result in a sound pressure wave.


<!-- source-pdf-page: 963 -->

14.3.1.3
Speaker Layouts: Stereo

Sound systems usually support multiple speaker output channels. A stereo
device such as an iPod, the sound system in your car or your grandpa’s port-
able “boom box” supports at least two speakers for the left and right stereo
channels. Some high-fidelity stereo systems also boast two additional “tweet-
ers”—tiny speakers that are capable of reproducing the highest-frequency
sounds within the left and right channels. This allows the two main speak-
ers to be larger, and therefore better at covering the bass. Some stereo systems
also support a subwoofer or LFE (low-frequency effects) speaker. Such sys-
tems are sometimes called 2.1 systems—two for the left and right, and “dot
one” for the LFE speaker.

Headphones versus Speakers

It’s important to distinguish between stereo speakers in an open room and
stereo headphones. Stereo speakers in a room will typically be positioned in
front of the listener and offset to either side. This means that the sound waves
coming from the left speaker are actually received by the right ear as well, and
vice versa. The waves from the more-distant speaker will arrive at the ear with
a slight time delay (phase shift) and a slight attenuation. The phase-shifted
sound waves from the more-distant speaker will tend to interfere with those
coming from the closer speaker. The sound system should take this interfer-
ence into account in order to produce the highest quality sound.
Headphones, on the other hand, come in direct contact with the ears, so
the left and right channels are perfectly isolated and do not interfere with one
another. Also, because headphones deliver sound almost directly to the ear
canal, the head-related transfer effects (HRTF) of the shape of the ears them-
selves do not come into play (see Section 14.1.4), meaning that somewhat less
spatial information is received by the listener.

14.3.1.4
Speaker Layouts: Surround Sound

Home theater surround sound systems typically come in two flavors: 5.1 and
7.1. As you undoubtedly guessed, these numbers refer to the five or seven
“main” speakers, plus the one subwoofer. The goal of a surround sound sys-
tem is to immerse the listener in a realistic soundscape, by providing positional
information as well as high-fidelity sound reproduction (see Section 14.1.4).
The main speaker channels in a 5.1 system are: center, front left, front right,
rear left and rear right. A 7.1 system adds two additional speakers, surround
left and surround right, which are intended to be placed directly to either side
of the listener. Dolby Digital AC-3 and DTS are two popular surround sound


<!-- source-pdf-page: 964 -->
> Visual fallback for diagrams/images: [PDF page 964](../../../visual_pages/page_0964.jpg)

Figure 14.24. Speaker arrangement for a 7.1 surround sound home theater system.

technologies. The speaker layout of a typical 7.1 home theater is shown in
Figure 14.24.
Dolby Surround, Dolby Pro Logic and Dolby Pro Logic II are technologies
for expanding a stereo source signal into 5.1 surround sound. A stereo signal
lacks the positional information necessary to drive the 5.1 speaker configu-
ration directly. But using these Dolby technologies, an approximation of the
missing positional information can be generated heuristically using various
cues found within the original stereo source signal.

14.3.1.5
Analog Signal Levels

Audio voltage signals may be transmitted at various voltage levels. A micro-
phone usually produces a low-amplitude voltage signal—these are called mic-
level signals. For connections between components, higher-voltage line-level
signals are used. There’s a big difference between professional audio equip-
ment and consumer electronics when it comes to line-level voltages. Profes-
sional gear is usually designed to work with line levels ranging from 2.191 V
(volts) peak-to-peak for a nominal signal up to a maximum voltage of 3.472 V
peak-to-peak. The peak-to-peak voltage of a “line level” signal on consumer
equipment varies quite a bit, but most consumer devices output up to 1.0 V
peak-to-peak, and have inputs that can handle up to 2.0 V signals. It’s impor-


<!-- source-pdf-page: 965 -->

tant to match the levels of input and output signals when connecting audio
equipment. Passing a voltage that is too high for the device to handle will
cause clipping of the signal. And passing a voltage that is too low will result
in audio that sounds quieter than it should.

14.3.1.6
Ampliﬁers

The small voltages produced by a microphone must be amplified in order to
drive speakers with enough force to produce audible sound waves. An am-
plifier is an analog electronic circuit that produces at its output a nearly exact
replica of its input signal, but with the amplitude of the signal increased sig-
nificantly. An amp essentially increases the power content of a signal. It does
this by drawing from some kind of power source, and driving the increased
voltage produced by this power source in such a way as to mimic the behavior
of the input signal over time. In other words, an amp modulates the output of
its power source to match its much lower-voltage input signal.
The core technology behind an amplifier is the transistor—that well-known
and utterly ingenious device that sits at the heart of many modern electronic
devices, including its crowning achievement—the computer.
A transistor
makes use of a semiconducting material in order to link the voltages between
two otherwise isolated, independent circuits. As such, a low-voltage signal
can be used to drive a higher-voltage circuit. This is exactly what is required
of an amplifier. We won’t get into the details of how transistors and ampli-
fiers work under the hood here. But if you’re curious, you can whet your
whistle with this great YouTube video on how the very first transistor worked:
https://www.youtube.com/watch?v=RdYHljZi7ys. And you can read more
about amplifier circuits here: http://en.wikipedia.org/wiki/Amplifier.
The gain A of an amplification system is defined as the ratio of output
power Pout to input power Pin. Like sound pressure level, gain is typically
measured in decibels:

)
dB.

( Pout

A = 10 log10

Pin

14.3.1.7
Volume/Gain Controls

A volume control is basically an inverse amplifier, also known as an attenua-
tor. Rather than increasing the amplitude of an electrical signal, it decreases the
amplitude, while keeping all other aspects of the waveform intact. In a home
theater system, the D/A converter produces a voltage signal with a very small
amplitude. The power amp boosts this signal up to the maximum “safe” out-
put power, beyond which the sound produced by your speakers would begin
to clip and distort (or even damage your hardware). The volume control then


<!-- source-pdf-page: 966 -->

attenuates this maximum output power to produce sound at the desired lis-
tening volume.
A volume control is much simpler to make than an amplifier. One can be
constructed by introducing a variable resistor, also known as a potentiometer,
into the circuit somewhere between the amplifier’s output and the speakers.
When the resistance is at its minimum (at or very close to zero), the ampli-
tude of the input signal isn’t changed, and a sound of maximum volume is
produced. When the resistance is at its maximum setting, the input signal’s
amplitude is maximally attenuated, and a sound of minimum volume is pro-
duced.
If your stereo system at home reports the volume in decibels, you’ve prob-
ably noticed that the values are always negative. This is because the volume
control is attenuating the output of the power amp. The volume meter is still
measured like a gain, but the “input” power is the maximum power of the
amp, and the “output” power is the volume selected by the user:

)
dB,

( Pvolume

A = 10 log10

Pmax

which will be negative as long as Pvolume < Pmax.

14.3.1.8
Analog Wiring and Connectors

An analog monophonic audio voltage signal can be carried by a pair of wires;
a stereo signal requires three wires (two channels plus a common ground).
The wiring can be internal to a device, in which case it is usually called a bus.
Wiring can also be external, for use in connecting different devices to one an-
other.
External wiring is typically connected to audio hardware either via a di-
rect “clip” or screw-post connector, of the kind found on high-end speakers,
or via various standardized connectors. Examples include RCA jacks, large
TRS (tip/ring/sleeve) jacks (the kind used by telephone operators in the early
1900s), TRS mini-jacks (found on your iPod, mobile phone and most PC sound
cards), keyed jacks (found most often on high-quality microphones and power
amps), and the list goes on.
Audio wiring is available in a wide range of quality levels. Thicker-gauge
wiring offers less resistance and therefore can transmit signals over farther
distances without unacceptable levels of attenuation. Optional shielding can
help reduce noise. And of course the choice of which metal to use in the con-
struction of the wires and connectors can make a difference in the quality of
the wiring as well.


<!-- source-pdf-page: 967 -->
> Visual fallback for diagrams/images: [PDF page 967](../../../visual_pages/page_0967.jpg)

### 14.3.2 Digital Audio Technology

The introduction of the compact disc (CD) marked a turning point in the au-
dio industry toward digital audio storage and processing. Digital technology
opens up a great many new possibilities, from reducing the size and increas-
ing the capacity of storage media, to using powerful computer hardware and
software to synthesize and manipulate audio in previously unimagined ways.
Today, analog audio storage devices are a thing of the past, and analog audio
signals are typically employed only where necessary—at the microphone and
the speaker.
As we saw in Section 14.2.1.1, the distinction between analog and digital
audio technologies corresponds exactly to the distinction between continuous-
time and discrete-time signals in the study of signal processing theory.

14.3.2.1
Analog-to-Digital Conversion: Pulse-Code Modulation

To record audio for use in a digitial system, such as a computer or game con-
sole, the time-varying voltage of an analog audio signal must first be converted
into digital form. Pulse-code modulation (PCM) is the standard method for en-
coding a sampled analog sound signal so that it can be stored in a computer’s
memory, transmitted over a digital telephony network or burned onto a com-
pact disc.
In pulse-code modulation, voltage measurements are taken at regular time
intervals. The voltage measurements may be stored in floating-point format,
or they may be quantized so that each measurement can be stored in an integer
with a fixed number of bits (typically 8, 16, 24 or 32). The sequence of measured
voltage values is then stored into an array in memory, or written out to a long-
term storage medium. The process of measuring a single analog voltage and
converting it to quantized numeric form is called analog-to-digital conversion
or A/D conversion. Specialized hardware is typically used to perform A/D
conversions. When we repeat this process at regular time intervals, it is called
sampling. A hardware or software component that performs A/D conversion
and/or sampling is referred to as an A/D converter or ADC.
In math terms, given the continuous-time audio signal p(t), we construct
the sampled version p[n] such that for each sample, p[n] = p(nTs), where n is a
non-negative integer used to index the samples, and Ts is the amount of time
between each sample, known as the sampling period. The basics of sampling
are illustrated in Figure 14.25.
The digital signal that results from PCM sampling has two important prop-
erties:

•
Sampling rate. This is the frequency at which the voltage measurements


<!-- source-pdf-page: 968 -->
> Visual fallback for diagrams/images: [PDF page 968](../../../visual_pages/page_0968.jpg)

s

s

Figure 14.25. A discrete-time signal can be thought of as a sampled version of a continuous-time
signal.

(samples) are taken. In principle an analog signal can be recorded dig-
itally without any loss of fidelity, provided that it is sampled at a fre-
quency twice that of the highest-frequency component present in the
original signal. This somewhat astounding and incredibly useful fact
is known as the Shannon-Nyquist sampling theorem. As we saw in Section
14.1.2.2, humans can only hear sounds within a limited band of frequen-
cies (from 20 Hz to 20 kHz). So all audio signals of interest to human
beings are band-limited, and can be faithfully recorded using a sampling
rate of a little over 40 kHz. (Voice signals occupy a narrower band of fre-
quencies, from 300 Hz to 3.4 kHz, so digital telephony systems can get
away with a sampling frequency of only 8 kHz.)

•
Bit depth. This describes the number of bits used to represent each quan-
tized voltage measurement. Quantization error is the error introduced by
rounding the measured voltage values to the nearest quantized value.
All other things being equal, a greater bit depth results in lower quan-
tization error, and therefore yields a higher-quality audio recording. A
bit depth of 16 is typical among uncompressed audio data formats. Bit
depth is sometimes referred to as resolution.

The Shannon-Nyquist Sampling Theorem

The Shannon-Nyquist sampling theorem states that if a band-limited continuous-
time signal (i.e., a signal whose Fourier transform is zero everywhere outside
a limited band of frequencies) is sampled to produce its discrete-time coun-
terpart, the original continuous-time signal can be recovered exactly from the
discrete signal, provided that the sampling rate is high enough. The mini-
mum sampling frequency for which this relation holds is called the Nyquist


<!-- source-pdf-page: 969 -->
> Visual fallback for diagrams/images: [PDF page 969](../../../visual_pages/page_0969.jpg)

frequency.

ωs > 2 ωmax,

where

ωs = 2π

Ts
.

Clearly, it is the existence of this theorem that allows digital technology to be
used in audio processing. Without it, digital audio would be doomed never
to sound as good as analog audio, and computers would not be playing the
significant role in the production of high-fidelity audio that they do today.
We won’t get into all of the gory details of why the sampling theorem
works. But we can gain some insight by realizing that the act of sampling
a signal at regularly spaced intervals in time causes its frequency spectrum
(Fourier transform) to be duplicated over and over along the frequency axis.
The higher the sampling frequency, the more “spaced out” these copies of the
signal’s frequency spectrum will be. So if the original signal is band-limited,

X(

)

max

max

X (

)
s

s
max

...
...

s

s

s

s

X (

)
s

s
max

...
...

Aliasing

Figure 14.26. The frequency spectrum of a band-limited signal is zero everywhere except within
a limited frequency band (top). If the sampling frequency exceeds the Nyquist frequency, the
spectrum copies do not overlap and the original signal can be recovered exactly (middle). If the
sampling frequency is too low, the spectrum copies overlap and aliasing results (bottom).


<!-- source-pdf-page: 970 -->

and if the sampling frequency is high enough, we can guarantee that the copies
of the frequency spectrum will be spaced far enough apart so as not to overlap
with one another. When this happens, we can recover the original frequency
spectrum exactly via a low-pass filter that filters out all of the copies of the
spectrum except the original. However, if the sampling frequency is too low,
the spectrum copies will overlap with one another. This is called aliasing, and
it prevents us from exactly recovering the original signal’s spectrum. See Fig-
ure 14.26 for an illustration of aliased and unaliased sampling.

14.3.2.2
Digital-to-Analog Conversion: Demodulation

When a digital sound signal is to be played back, a process opposite to that of
analog-to-digital conversion is required. We call this, sensibly enough, digital-
to-analog conversion or D/A conversion for short. It is also termed demodulation
because it undoes the effects of pulse-code modulation. A digital-to-analog
conversion circuit is called a DAC.
D/A conversion hardware generates an analog voltage corresponding to
each sampled voltage value in a digital signal, as represented by an array of
quantized PCM values in memory. If we drive this hardware with new values
periodically, at the rate at which the samples were measured during PCM, and
presuming that the sample rate was high enough as per the Shannon-Nyquist
sampling theorem, the analog voltage signal produced should exactly match
the original voltage signal.
Practically speaking, when we drive an analog voltage circuit with a se-
quence of discrete voltage levels, unwanted high-frequency oscillations are
often introduced as the hardware tries to rapidly change from one voltage
level to another. D/A hardware typically includes a low-pass or band-pass
filter to remove these unwanted oscillations, thereby ensuring an accurate re-
production of the original analog signal. For more information on filtering,
see Section 14.2.5.8.

14.3.2.3
Digital Audio Formats and Codecs

Various data formats exist for storing PCM audio data on disc or transmitting
it over the Internet. Each format has its history, and its pros and cons. Some
formats such as AVI are actually “container” formats, which can encapsulate
digital audio signals in more than one data format.
Some audio formats store the PCM data in an uncompressed form. Oth-
ers utilize various forms of data compression to reduce the required file size
or transmission bandwidth. Some compression schemes are lossy, meaning
that some of the fidelity of the original signal is lost in the compression/de-
compression process. Other compression schemes are lossless, meaning that


<!-- source-pdf-page: 971 -->

the original PCM data can be recovered exactly after a round-trip compres-
sion/decompression cycle.
Let’s take a look at a few of the most common audio data formats.

•
Raw header-less PCM data is sometimes used in situations where the
meta-information about the signal, such as the sample rate and bit depth,
is known a priori.

•
Linear PCM (LPCM) is an uncompressed audio format that can support
up to eight channels of audio at a 48 kHz or 96 kHz sampling frequency,
and 16, 20 or 24 bits per sample. The “linear” in LPCM refers to the fact
that the amplitude measurements are taken on a linear scale (as opposed
to, say, a logarithmic scale).

•
WAV is an uncompressed file format created by Microsoft and IBM. Its
use is commonplace on the Windows operating system. Its correct name
is “waveform audio file format” although it is also rarely referred to as
“audio for windows.” The WAV file format is actually one of a family
of formats known as resource interchange file format (RIFF). The contents
of a RIFF file are arranged in chunks, each with a four-character code
(FOURCC) that defines the contents of the chunk and a chunk size field.
The bitstream in a WAV file conforms to the linear pulse-code modula-
tion (LPCM) format. WAV files can also contain compressed audio, but
they are most commonly used for storing uncompressed audio data.

•
WMA (Windows Media Audio) is proprietary audio compression tech-
nology designed by Microsoft as an alternative to MP3. See http://en.
wikipedia.org/wiki/Windows_Media_Audio for details.

•
AIFF (audio interchange file format) is a format developed by Apple
Computer, Inc. and used widely on Macintosh computers. Like a WAV/
RIFF file, an AIFF file typically contains uncompressed PCM data, and is
comprised of chunks, each prefaced by a four-character code and a size
field. AIFF-C is a compressed variant of the AIFF format.

•
MP3 is a lossy compressed audio file format that has become the de
facto standard on most digital audio players, and is also widely used
by games and multimedia systems and services. The full name of this
format is actually MPEG-1 or MPEG-2 audio layer III. MP3 compression
can result in files that are one-tenth the size, but with very little per-
ceptual difference from the original uncompressed audio. These results
are achieved by making use of perceptual coding—a technique that elimi-
nates portions of the audio signal that are beyond the perception of most
people anyway.


<!-- source-pdf-page: 972 -->

•
ATRAC stands for Adaptive Transform Acoustic Coding—a family of
proprietary audio compression techniques developed by Sony. The for-
mat was originally developed to allow Sony’s MiniDisc media to con-
tain audio with the same running time as a CD while occupying signifi-
cantly less space and undergoing an imperceptable degradation in qual-
ity. See http://en.wikipedia.org/wiki/Adaptive_Transform_Acoustic_
Coding for more details.
•
Ogg Vorbis is an open source file format that offers lossy compression.
Ogg refers to a “container” format that is commonly used in conjunction
with the Vorbis data format.
•
Dolby Digital (AC-3) is a lossy compression format supporting channel
formats from mono to 5.1 surround sound.
•
DTS is a collection of theater audio technologies developed by DTS,
Inc.
DTS Coherent Acoustics is a digital audio format transportable
through S/PDIF interfaces (see Section 14.3.2.5) and used on DVDs and
Laserdiscs.
•
VAG is a proprietary audio file format available for use by all PlaySta-
tion 3 developers. It makes use of adaptive differential PCM (ADPCM), an
analog-to-digital conversion scheme based on PCM. Differential PCM
(DPCM) stores the deltas between samples rather than the absolute val-
ues of the samples themselves, in order to allow the signal to be com-
pressed more effectively. Adaptive DPCM varies the sample rate dy-
namically in order to further improve the achievable compression ratio.
•
MPEG-4 SLS, MPEG-4 ALS and MPEG-4 DST are formats that offer loss-
less compression.

This list is by no means comprehensive. In fact, there are a dizzying num-
ber of audio file formats, and an even longer list of compression/decompres-
sion algorithms. For an introduction to the fascinating world of audio data
formats, check out our old friend Wikipedia: http://en.wikipedia.org/wiki/
Digital_audio_format. The “PlayStation 3 Secrets” website also provides some
excellent information on audio formats: https://bit.ly/2HOVtvR.

14.3.2.4
Parallel and Interleaved Audio Data

One way to organize multi-channel audio data is to store the samples for each
monophonic channel into a separate buffer. In this case, you’d need six par-
allel buffers to describe a 5.1 audio signal. This arrangement is shown in Fig-
ure 14.27.
Multi-channel audio data can also be interleaved within a single buffer. In
this case, all of the samples for each time index are grouped together in a pre-


<!-- source-pdf-page: 973 -->
> Visual fallback for diagrams/images: [PDF page 973](../../../visual_pages/page_0973.jpg)

defined order. Figure 14.28 depicts an interleaved PCM buffer containing a
six-channel (5.1) audio signal.

C[n]

14.3.2.5
Digital Wiring and Connectors

FL[n]

S/PDIF (Sony/Philips Digital Interconnect Format) is an interconnect technol-
ogy that transmits audio signals digitally, thereby eliminating the possibility of
noise being introduced by analog wiring. The S/PDIF standard is physically
realized either via a coaxial cable connection (also called S/PDIF) or a fiber-
optic connection (known as TOSLINK).
Regardless of the physical interface (S/PDIF coaxial or TOSLINK optical),
the S/PDIF transport protocol is limited to 2-channel 24-bit LPCM uncom-
pressed audio at standard sampling rates ranging from 32 kHz to 192 kHz.
However, not all equipment works at all sample rates. The same physical in-
terfaces can be also be used to transport bitstream-encoded audio (e.g., Dolby
Digital or DTS lossy compressed data) at bitrates ranging from 32 kpbs to
640 kbps for Dolby Digital and 768 kbps to 1536 kbps for DTS, respectively.
Uncompressed multi-channel LPCM (i.e., greater than two stereo channels)
can only be sent over an HDMI (high-definition multimedia interface) connec-
tion on consumer audio equipment. HDMI connectors are used for transmis-
sion of both uncompressed digital video and either compressed or uncom-
pressed digital audio signals. HDMI supports up to a 36.86 Mbps bitrate for
multi-channel or bitstream audio. However, HDMI bitrates for audio vary de-
pending on the video mode—only 720 p/50 Hz modes or higher are capable of
utilizing the full audio bandwidth. See the HDMI specification under the sec-
tion “video dependency” for more information on this. Apple’s DisplayPort
and Thunderbolt connectors are other high-bandwidth alternatives similar in
many respects to HDMI.
USB connections are sometimes used to send audio signals. On most game
consoles, the USB output is intended only to drive headphones.
Wireless audio connections are also possible. The Bluetooth standard is the

FR[n]

RL[n]

RR[n]

LFE[n]

C[n+1]

FL[n+1]

FR[n+1]

Figure 14.28.
Six-
channel (5.1) PCM bus
data
in
interleaved
format.

Parallel

FR[n]
FL[n]
C[n]
RR[n]
RL[n]
LFE[n]

RR[n+1]
RL[n+1]
LFE[n+1]

FR[n+1]
FL[n+1]
C[n+1]

FR[n+2]
FL[n+2]
C[n+2]

RR[n+2]
RL[n+2]
LFE[n+2]

...
...
...

...
...
...

Figure 14.27. Six-channel (5.1) PCM bus data in parallel format.


<!-- source-pdf-page: 974 -->

most commonly used method of transmitting audio signals wirelessly.

## 14.4 Rendering Audio in 3D

Thus far, we’ve learned about the physics of sound, the mathematics of signal
processing and the various technologies that are used to record and play back
sounds. In this section, we’ll explore how all of this theory and technology
can be put to use in a game engine, in order to produce realistic, immersive
soundscapes for our games.
Any game that takes place in a virtual 3D world requires some sort of 3D
audio rendering engine. A high-quality 3D audio system should provide the
player with a rich, immersive and believable soundscape that matches what’s
going on in this 3D world, while supporting the story and remaining true to
the tonal design of the game.

•
The inputs to this system are the myriad 3D sounds that emanate from all
over the game world: footsteps, speech, the sound of objects bumping
into one another, gunfire, ambient sounds like wind or rainfall and so
on.

•
Its output is a handful of sound channels that, when played in the speak-
ers, reproduce as believably as possible what the player would actually
hear if he or she were really there in the virtual game world.

Ideally we’d like our audio engine to produce its output in full 7.1 or 5.1 sur-
round sound, because this gives the ears the richest possible set of positional
cues. However, audio engines must also support stereo output for players
who don’t have fancy home theater systems—or who just want to play their
game using headphones so they don’t wake their neighbors.
A game’s audio engine is also responsible for playing sounds that do not
originate in the virtual world. Examples include the music track, sounds made
by the in-game menu system, a narrator’s voice-over, the voice of the player
character (especially in first-person shooters) and possibly certain ambient
sounds. We call these 2D sounds. Such sounds are designed to be played “di-
rectly” in the speakers, after having been mixed with the outputs of the 3D
spatialization engine.

### 14.4.1 Overview of 3D Sound Rendering

The primary tasks performed by the 3D audio engine are as follows:


<!-- source-pdf-page: 975 -->

•
Sound synthesis is the process of generating the sound signals that corre-
spond to the events occurring in the game world. These might be pro-
duced by playing back pre-recorded sound clips, or they might be proce-
durally generated at runtime.

•
Spatialization produces the illusion that each 3D sound is coming from the
proper location in the game world, from the point of view of the listener.
Spatialization is accomplished by controlling the amplitude of each sound
wave (i.e., its gain or volume) in two ways:

◦
Distance-based attenuation controls the overall volume of a sound in
order to provide an indication of its radial distance from the listener.
◦
Pan controls a sound’s relative volume in each of the available
speakers in order to provide an indication of direction from which
the sound is arriving.

•
Acoustical modeling heightens the realism of the rendered soundscape by
mimicking the early reflections and late reverberations that character-
ize the listening space, and by accounting for the presence of obstacles
that partially or completely block the path between the sound source and
the listener. Some sound engines also model the frequency-dependent
effects of atmospheric absorption (Section 14.1.3.2) and/or HRTF effects
(Section 14.1.4).

•
Doppler shifting may also be applied to account for any relative move-
ment between a sound source and the listener.

•
Mixing is the process of controlling the relative volumes of all the 2D and
3D sounds in our game. The mix is driven in part by physics and in part
by aesthetic choices made by the game’s sound designers.

### 14.4.2 Modeling the Audio World

In order to render the soundscape of a virtual world, we must first describe
that world to the engine. The “audio world model” consists of the following
elements:

•
3D sound sources. Each 3D sound in the game world consists of a mono-
phonic audio signal, emanating from a specific position. We must also
provide the engine with its velocity, radiation pattern (omnidirectional,
conical, planar) and range (beyond which the sound is inaudible).

•
Listener.
The listener is a “virtual microphone” located in the game
world. It is defined by its position, velocity and orientation.


<!-- source-pdf-page: 976 -->

•
Environmental model. This model either describes the geometry and prop-
erties of the surfaces and objects present in the virtual world, and/or it
describes the acoustic properties of the listening spaces in which gameplay
takes place.

The positions of the source and listener are used for distance-based attenu-
ation; the radiation pattern of the sound source also factors into the distance-
based attenuation calculation. The orientation of the listener defines a refer-
ence frame in which the angular position of the sound is calculated. This angle
in turn determines the pan—the relative volumes of the sound in the five or
seven main speakers of 5.1 or 7.1 surround sound, respectively. The relative
velocity of source and listener is used when applying a Doppler shift. And last
but not least, the environmental model is used for modeling the acoustics of the
listening space and to account for partial or complete blockage of the sound
path.

### 14.4.3 Distance-Based Attenuation

Distance-based attenuation reduces the volume of a 3D sound as the radial
distance between it and the listener increases.

14.4.3.1
Fall-Off Min and Max

The number of sound sources in a typical game world is very large. Due to
hardware and CPU bandwidth limitations, we couldn’t possibly render them
all. And we wouldn’t want to, because thanks to distance-based fall-off all
sounds beyond a certain distance from the listener can’t be heard anyway. For
this reason, each sound source is usually annoted with fall-off (FO) parame-
ters.
The fall-off min (“FO min” for short) is a minimum radius, which we’ll de-
note rmin, within which the sound doesn’t fall off at all and is heard at full
volume. The fall-off max or “FO max” is a maximum radius, denoted rmax, be-
yond which the sound source is considered to be silent and can therefore be
ignored. Between the FO min and FO max, we need to blend smoothly from
full volume down to zero.

14.4.3.2
Blending to Zero

One way to blend from maximum volume down to zero is to use a linear ramp
between FO min and FO max. Depending on the type of sound, a linear fall-off
might sound just fine.
In Section 14.1.3.1, we learned that sound intensity, which is closely related
to our perception of “loudness,” falls off with radial distance according to a


<!-- source-pdf-page: 977 -->

1/r2 rule. Gain, which is proportional to the amplitude of the sound pressure,
falls of as 1/r. So really the right thing to do is to use a 1/r curve to blend the
gain of a sound from full volume down to zero.
One problem with the function 1/r is that it is asymptotic—it never quite
reaches zero, no matter how large r gets. We can fix this by shifting the curve
slightly downward so that it crosses the r axis at rmax. Or we can simply clamp
the sound intensity to zero for all r > rmax.

14.4.3.3
Bending the Rules

When making The Last of Us, Naughty Dog’s sound department discovered
that attenuating character dialog using the 1/r2 rule caused speech to become
unintelligible too quickly for characters that were only a modest distance away.
This was a serious problem, especially during the stealth sections of gameplay,
where hearing the enemies’ ambient conversations was important both as a
tactical tool and as a means of advancing the storyline.
To solve this problem, the sound department at Naughty Dog utilized a
sophisticated fall-off curve that causes dialog to roll off more slowly near the
listener, more quickly in the mid-range, and then more slowly again as the
distance to the listener grows very large. This allows speech to be audible
over longer distances, while still retaining a natural-sounding fall-off.
The dialog fall-off curves were also adjusted dynamically at runtime, based
on the current “tension level” of the game (i.e., whether the enemies are un-
aware of the player, are searching for him or are engaged in direct combat with
him). This is what allows the voices in The Last of Us to project over longer dis-
tances during stealth gameplay, while not rising to overpowering levels when
combat breaks out.
Finally, a “sweetener” reverb could be optionally enabled to allow char-
acter voices to bleed around corners, even when the direct path is 100% ob-
structed. This tool is incredibly helpful in situations where modeling realistic
fall-off is less important than ensuring that the player can hear a conversation
clearly.
There are all sorts of ways to “cheat” when designing your 3D audio model.
But no matter what you do, always remember this simple lesson: Never be
afraid to do whatever it takes to satisfy the needs of your game. Don’t worry—
the laws of physics won’t be offended!

14.4.3.4
Atmospheric Attenuation

As we saw in Section 14.1.3.2, low-pitched sounds are attenuated by the atmo-
sphere less than high-pitched sounds. Some games, including Naughty Dog’s
The Last of Us, model this phenomenon by applying a low-pass filter to each


<!-- source-pdf-page: 978 -->
> Visual fallback for diagrams/images: [PDF page 978](../../../visual_pages/page_0978.jpg)

3D sound whose passband slides toward lower and lower frequencies as the
distance between the sound source and the listener increases.

### 14.4.4 Pan

Panning is a technique used to provide the illusion that a 3D sound is coming
from a particular direction. By controlling the volume (i.e., gain) of the sound
in each of the available speakers, we can induce the perception of a phantom
image of the sound in three-dimensional space. This method of panning is
called amplitude panning because we are providing angular information to the
listener by adjusting only the amplitudes of the sound waves produced at each
speaker (as opposed to using phase offsets, reverb or filtering to provide posi-
tional cues). It is sometimes referred to as IID panning because it relies on the
perceptual effects of interaural intensity difference (IID) to produce a sound’s
phantom image.
The term “pan” comes from early technology that used a “panoramic po-
tentiometer” (variable resistor) or “pan pot” to control the relative volumes
of the left and right speakers of a stereo system. Dialing the pan pot to one
extreme would produce sound only in the left speaker; dialing it to the other
extreme would drive the right speaker exclusively; and centering the pan pot
dial would distribute the sound equally to both speakers.
To understand how pan works, we envision our listener located at the cen-
ter of a circle. The speakers are positioned at various points on the circumfer-
ence of this circle, so we’ll call it the speaker circle in this book. The radius of
the circle approximates the average distance between the listener and any one
speaker.
For a stereo sound system, the front and right speakers are located roughly
at ±45 degrees to the left and right of center. For stereo headphones, they
are positioned at ±90 degrees (and the radius is much smaller). For 7.1 sur-
round sound, we consider only the seven “main” speakers, as the LFE channel
provides no positional cues. These speakers are located roughly as shown in
Figure 14.29. When panning to a 5.1 system, we simply omit the surround left
and surround right speakers.
For the time being, let’s treat each 3D sound as a point source. To pan
a sound, we first determine its azimuthal (horizontal) angle. The azimuthal
angle must be measured in the local space of the listener, so that an angle of
zero corresponds to the position directly in front of the listener. Next, we figure
out which two speakers around the circumference of our circle are adjacent to
this azimuthal angle. We convert the angle into a percentage of the arc between
the two speakers. Finally, we use this percentage to determine the gains of the


<!-- source-pdf-page: 979 -->
> Visual fallback for diagrams/images: [PDF page 979](../../../visual_pages/page_0979.jpg)

Figure 14.29. Speaker layout for 7.1 pan.

1

s

2

s

1

2

Figure 14.30. Treating the sound as a point source, the pan blend percentage β is calculated be-
tween the two speakers immediately adjacent to the source.

sound in each speaker.

To formulate this mathematically, let’s use the symbol θs for the azimuthal
angle of the sound. We’ll call the angles of the two adjacent speakers θ1 and
θ2. The percentage β is then calculated as follows:

β = θs −θ1

θ2 −θ1
.

This calculation is illustrated in Figure 14.30.


<!-- source-pdf-page: 980 -->

14.4.4.1
Constant Gain Panning

Our first instinct might be to use the percentage β to perform a simple linear
interpolation between the gains of the two speakers. Given the gain A of the
unpanned sound, the gains of that sound as played in each speaker would be
calculated as follows:

A1 = (1 −β)A;

A2 = βA.

This is known as constant gain panning, because the net gain A = A1 + A2 is
constant, independent of the values of θs and β.
The main problem with constant gain panning is that it does not produce
the perception of constant loudness as the sound moves around the acoustic
field. Gain controls the amplitude of the sound pressure wave, and therefore
controls the sound pressure level (SPL). However, as we learned in Section 14.1.2,
human perception of loudness is actually proportional to the intensity or power
of a sound wave, both of which vary as the square of the SPL.
As an illustration of the problem, imagine that our sound is panned to the
halfway point between our two speakers. Constant gain panning would have
us set the gains A1 and A2 to 1

2 A each. But this yields a total power of A2
1 +
A2
2 = ( 1

2 A)2 + ( 1

2 A)2 = 1

2 A2. In other words, the loudness of the sound will
be one-half of what it would have been, had the sound been panned to only
the left or the right speaker.

14.4.4.2
The Constant Power Pan Law

Clearly in order to keep the perception of loudness constant as a sound’s image
moves about the listener, we need to keep the power constant. This rule is
known as the constant power pan law, or just the pan law for short.
There’s a very easy way to implement the constant power pan law. Instead
of linearly interpolating the gains, we use the sine and cosine of the blend
percentage β to calculate them:

A1 = sin( π

2 β)A;

A2 = cos( π

2 β)A.

Consider again a sound image that is panned to halfway between the two
speakers (β =
1
2). With constant power panning, the two speakers’ gains
will be set to A1 = A2 =
1
√

2 A. This yields a total power of A2
1 + A2
2 =

2 A)2 = A2. This works for any value of β, so the power A2 is
constant no matter where our sound image is placed around the circle.

2 A)2 + ( 1
√

( 1
√


<!-- source-pdf-page: 981 -->

Sound designers often apply a “3 dB rule” to account for the pan law: If a
sound is to be mixed equally to two speakers, the gain in each speaker should
be reduced by 3 dB relative to the gain that would be used if the sound were to

)
≈
−0.15. Voltage gain (or amplitude gain) is defined as 20 log10(Aout/Ain), and
20 × −0.15 = −3 dB. (The 20 in front of the logarithm arises because a decibel
is one-tenth of a bel, multiplied by two to account for the fact that we’re dealing
with A2 and not A.)

be played in only one speaker. The value −3 dB arises because log10
(
1
√

2

14.4.4.3
Headroom

Panning causes sounds to be rendered entirely by one speaker in some situa-
tions, and by two (or more, as we’ll see) speakers in others. Let’s say a sound is
being played equally by two adjacent speakers, and its volume is so loud that
each speaker is outputting its maximum power. What happens when that
sound pans around to only one speaker? The answer is that we’d probably
blow out the speaker, because our constant power pan law requires us to use
more gain for one speaker than for two.
To prevent this problem, we need to artificially lower the maximum gains
of our sounds across the board, such that the worst-case scenario of playing the
sound in one speaker won’t overdrive that speaker. The practice of artificially
reducing the maximum range of volume is known as “leaving oneself some
headroom.”
The concept of headroom also applies to mixing. When two or more sounds
are mixed, their amplitudes add up. By leaving some headroom in our mix,
we can accomodate worst-case scenarios where a large number of high-vol-
ume sounds play simultaneously.

14.4.4.4
To Center or Not to Center?

In cinema, the center channel was historically used for speech; only the sound
effects would be panned to the other speakers around the room. The idea
behind this practice was that the characters in the movie are usually on-screen
when they speak, so the audience expects to hear their voices front-and-center.
This approach has the nice side-effect of separating out the speech from the rest
of the sounds in the film, meaning that loud sound effects won’t use up all the
available headroom and drown out the dialog.
In 3D games, the situation is quite different. The player generally wants to
hear dialog coming from the “correct” location around him or her. If the player
swings the camera by 180 degrees, the dialog should likewise swing about the


<!-- source-pdf-page: 982 -->
> Visual fallback for diagrams/images: [PDF page 982](../../../visual_pages/page_0982.jpg)

sound field by 180 degrees. As such, games usually do not assign all dialog to
the center speaker; instead, it is included in the pan for both sound effects and
dialog.
Of course, this brings us back to the headroom problem—loud gunfire can
now completely drown out the speech. At Naughty Dog, we overcame this
problem by “splitting the difference” and always playing some of the dialog
in the center channel, as well as panning some of it to the rest of the speakers
along with the sound effects.

14.4.4.5
Focus

When the source of a sound is far away from the listener, we can treat it as a
point source. We simply calculate a single azimuthal angle and feed it into our
constant power panning system. However, when a sound source approaches
or actually enters into the circle that defines the radial distance of the speakers
from the listener, it can no longer be accurately modeled as a point source
represented by a single angle.
Consider the case of moving toward and past a sound source. At first, the
sound source appears entirely in the front speakers. As it passes the listener,
we somehow need to transfer the sound to the rear speakers. If we model the
sound as a point source, our only option is to “pop” the sound from the fronts
to the rears.
Ideally we’d like the sound’s image to gradually “spread out” around the
speaker circle as it approaches. That way, as it nears the listener, we can start
playing more of it in the side speakers. When the sound source is coincident
with the listener, it can be played in all seven (or five) speakers. And once it
passes, we can smoothly transition the sound to the rears, dropping the front
gains to zero as it recedes behind the listener.
We can do this kind of thing and more if we model a sound source not
as a point on the speaker circle but as an arc. Looking at it another way, we
can think of each sound source as having an arbitrary shape in 3D space, and
its projection onto the speaker circle subtends a certain angle, defining a “pie
wedge” shape within the circle. This is analogous to the concept of solid angle
often used in the calculation of ambient occlusion in 3D graphics—see http://
en.wikipedia.org/wiki/Solid_angle for details.
We’ll call the angle subtended by an extended sound source the focus angle,
and we’ll denote it α. A point source can be thought of as an “edge case” in
which α = 0. The focus angle is depicted in Figure 14.31.
To render a sound with a nonzero focus angle, we must first determine
the subset of speakers that either intersect its projected arc on the speaker cir-
cle, or are immediately adjacent to the arc. Then we must divide the sound’s


<!-- source-pdf-page: 983 -->
> Visual fallback for diagrams/images: [PDF page 983](../../../visual_pages/page_0983.jpg)

Figure 14.31. The focus angle α deﬁnes the projection of an extended sound source on the speaker
circle.

intensity/power among these speakers in order to induce the perception of a
phantom image that extends across the projected arc.
We can divide the sound amongst the relevant speakers in various ways.
For example, we could arrange for all the speakers that lie within the focus “pie
slice” to receive equal maximum power, and then apportion less of the sound
to the two speakers immediately adjacent the arc to create a fall-off. But no
matter how we do it, we must remember to always obey the constant power
pan law. So, we must set the gains in such a way that the sum of their squares
(i.e., the sum of their powers) equals the squared gain of the original unpanned
sound source.

14.4.4.6
Dealing with Verticality

In both stereo and surround sound set-ups, the speakers all lie roughly in a
horizontal plane. This arrangement makes it tricky to position sounds above
or below the plane of the listener’s ears.
The ideal of course would be to model a true “periphonic” sound field
by using a spherical speaker arrangement. A technology known as Ambison-
ics (http://en.wikipedia.org/wiki/Ambisonics) is capable of accommodating
both planar and spherical speaker arrangements. However, it is not supported
by any game console—at least not yet. Sony now offers a 3D audio technol-
ogy in their Platinum Wireless Headset for PS4, and games are beginning to
support it. But even in the presence of 3D audio technology, games still need
to support a planar speaker arrangement for 5.1 and 7.1 sound systems.
It turns out that the concept of focus can be leveraged to simulate some de-


<!-- source-pdf-page: 984 -->

gree of verticality in our sound imagery. We simply project all sounds onto the
horizontal plane, and then use a nonzero focus angle for any sounds whose
projections fall too close to or within the speaker circle. An elevated sound
that is far away will be rendered in virtually the same way as one that is not
elevated. But as the elevated sound passes overhead, we blend it across multi-
ple speakers, thereby producing a phantom image within the speaker circle. If
we combine this with distance-based attenuation and frequency-dependent at-
mospheric absorptions, we can provide the listener with enough cues to make
the sound seem to be located above or below the listener.

14.4.4.7
Further Reading on Pan

The basics of the constant power pan law can be found here: http://www.
rs-met.com/documents/tutorials/PanRules.pdf. The following site is also a
great resource on the topic: http://www.music.miami.edu/programs/mue/
Research/jwest/Chap_3/Chap_3_IID_Based_Panning_Methods.html.
The paper entitled “Spatial Sound Generation and Perception by Ampli-
tude Panning Techniques” by Ville Pukki of the Helsinki University of Tech-
nology, available at https://aaltodoc.aalto.fi/bitstream/handle/123456789/
2345/isbn9512255324.pdf?sequence=1, provides a clear description of the spa-
tialization problem and outlines the vector based amplitude panning (VBAP)
method, as well as providing an extensive bibliography for further reading.
David Griesinger’s paper, “Stereo and Surround Panning in Practice,”
also makes for a very interesting read; it is available at http://www.
davidgriesinger.com/pan_laws.pdf. David’s website is chock full of research
on sound perception and audio reproduction technologies.

### 14.4.5 Propagation, Reverb and Acoustics

Even if we were to implement distance-based attenuation, pan and Doppler,
our 3D sound engine still wouldn’t be able to generate a realistic soundscape.
This is because a lot of the auditory cues we humans use to sort out what
kind of space we’re in come from the early reflections, late reverberations and
head-related transfer function (HRTF) effects caused by sound waves taking
multiple paths to reach our ears. The term “sound propagation modeling” can
be applied to any technique that is designed to take into account the ways in
which sound waves propagate through a space.
Many different approaches are used, both in research and in interactive
media and games. These technologies fall into three basic categories:

•
geometric analysis attempts to model the actual pathways taken by sound
waves,


<!-- source-pdf-page: 985 -->

•
perceptually based models focus on reproducing what the ear perceives us-
ing an LTI system model of the acoustics of a listening space, and

•
ad hoc methods employ various kinds of approximations to produce rea-
sonably accurate acoustics with minimal data and/or processing band-
width.

The following paper does a good job of surveying many of the techniques
that fall into the first two categories: http://www-sop.inria.fr/reves/Nicolas.
Tsingos/publis/presence03.pdf. In this section, we’ll briefly discuss LTI sys-
tems modeling, and then turn our attention to a few ad hoc methods, because
they tend to be more practical for use in real games.

14.4.5.1
Modeling Propagation Effects with an LTI System

Imagine that I am standing in a room containing various objects made of var-
ious materials. A sound is made in the room. It reflects and diffracts and
bounces around the room, and eventually reaches my ears. If you think about
it, it doesn’t really matter which specific paths those sound waves took. The
only thing that affects my perception is the specific superposition of the dry
direct sound waves and the various time-shifted and possibly muffled or oth-
erwise altered wet indirect waves.
It turns out that all of these effects can be modeled with a linear time-
invariant (LTI) system. Theoretically, if we could measure the impulse response
of the room for a given pair of points that represent the source of the sound
and the listener, we can determine exactly how any sound we might play at
that source location should sound if heard at the listener position. All we need
to do is convolve the dry sound with the impulse response!

pwet(t) = pdry(t) ∗h(t).

This technique seems like a silver bullet at first blush. However, it is ac-
tually more difficult and less practical than it may at first seem. It’s pretty
easy to determine the impulse response of a space in real life—you can record
the sound of a short “click” that approximates the unit impulse δ(t), and the
recorded signal will approximate h(t). But in a virtual space, we’d need to
perform a complex and expensive simulation of each play space in order to
determine h(t). Also, to model the room’s acoustics accurately, we’d need
to perform this calculation for a large number of source-listener point pairs
throughout the game world, and once calculated the size of this data would
be immense. Finally, the operation of convolution is itself not inexpensive,
and game consoles and sound cards have in the past lacked the horsepower
to do this for every sound in the game in real time.


<!-- source-pdf-page: 986 -->
> Visual fallback for diagrams/images: [PDF page 986](../../../visual_pages/page_0986.jpg)

Figure 14.32. It’s a good idea to cross-blend between reverb settings based on the position of the
listener.

Modern gaming hardware is getting more powerful all the time, and a
convolution-based approach to propagation modeling is becoming more fea-
sible. For example, Micah Taylor et al. created a real-time demo of convolution
reverb that produced promising results—see https://intel.ly/2J8Gpsu. That
said, most games still don’t use this approach, but instead they rely on various
ad hoc methods and approximations to model environmental reverb.

14.4.5.2
Reverb Regions

One common approach to modeling the wet characteristics of a play space is
to annotate the game world with manually placed regions, each of which is
tagged with appropriate reverb settings such as pre-delay, decay, density and
diffusion. See Section 14.1.3.4 for a discussion of these parameters. As the
virtual listener moves through these regions, we can light up the appropriate
reverb mode: If the player enters a large tiled room, we can bump up the echos;
when the player enters a small closet, we can virtually eliminate the reverb to
produce a very dry sound.
It’s a good idea to smoothly cross-blend between reverb settings as the lis-
tener moves through the play space. We can use simple linear interpolation to
perform this cross-blend for each parameter. The blend percentage is best cal-
culated using a measure of how far “into” the region the listener is. For exam-
ple, imagine moving between an outdoor space and an indoor space through
a doorway. We could define a region around the doorway within which the
blend occurs. If the listener is entirely outside the blend region, the blend per-
centage should yield 100% of the outdoor reverb settings and 0% of the indoor
settings. If the listener is standing at the halfway point within the blend re-
gion, we’d want a 50/50 mix of the reverb settings. Once the listener passes
out of the blend region inside the building, we’ll have reached a 0% outdoor
/ 100% indoor blend. This idea is illustrated in Figure 14.32.


<!-- source-pdf-page: 987 -->
> Visual fallback for diagrams/images: [PDF page 987](../../../visual_pages/page_0987.jpg)

14.4.5.3
Obstruction, Occlusion and Exclusion

When using regions to define the acoustics of our play spaces, we typically as-
sign a single impulse response function or a single collection of reverb settings
to each region. This captures the essence of each play space (e.g., large tiled
hall, small closet lined with coats, flat outdoor plain, etc.). But it results in a
less-than-perfect reproduction of the acoustics that arise due to obstacles. For
example, imagine a square room with a large pillar in the center. If a sound
source is located in the corner of the room, a listener will perceive a very dif-
ferent timbre as he or she moves about the room, depending on whether the
direct path is obstructed by the pillar or not. If we use a single set of reverb
parameters for this room, we cannot capture these subtleties.
To address this problem, we can attempt to model the geometry and mate-
rial properties of the environment in some way, determine how sound waves
are affected by the obstacles in their path, and then use the results of this anal-
ysis to alter the “base” reverb settings associated with the room.
Figure 14.33 shows the three ways in which the objects and surfaces in the
game world can affect the transmission of sound waves:

•
Occlusion. This describes a situation in which there exists no unfettered
path from the sound source to the listener. A listener might still be able
to hear a fully occluded sound, if for example there is only a thin wall
or door between it and the source of the sound. Either the dry and wet
components of an occluded sound are both attenuated and/or muffled,
or the sound is entirely silent from the point of view of the listener.

•
Obstruction. This describes a case in which the direct path between the
sound and the listener is blocked, but an indirect path is available. Ob-
struction can occur for example when a sound source passes behind a
car, pillar or other obstacle. The dry component of an obstructed sound
is either entirely absent or greatly muffled, and the wet component may
be altered as well to account for the sound waves having to take a longer,
more reflected path to the listener.

•
Exclusion. This describes a case in which there is a free direct path be-
tween source and listener, but the indirect path is compromised in some
way. This can happen if a sound is produced in one room and passes
through a narrow opening such as a door or window to reach the listener.
In an exclusion situation, the dry component of the sound remains unal-
tered but the wet component is attenuated, muffled or, for very narrow
openings, entirely absent.


<!-- source-pdf-page: 988 -->
> Visual fallback for diagrams/images: [PDF page 988](../../../visual_pages/page_0988.jpg)

indirect

direct

sound

indirect

direct

sound

indirect

direct

sound

Figure 14.33. From top to bottom: occlusion, obstruction and exclusion.

Analyzing the Direct Path

Determining whether the direct path is blocked or not is not difficult. We sim-
ply cast a ray (see Section 13.3.7.1) from the listener to each sound source. If it
is blocked, the direct path is occluded. If not, it is free.
If we wish to model sound transmission through walls and other obstacles,
ray casting can still be used. We cast a ray from source to listener, and for each
contact we query the material properties of the impacted surface to determine
how much of the sound’s energy it absorbs. If it allows some energy to pass
through, we can cast another ray starting on the other side of the obstacle and
continue tracing the path to the listener. Once all of the sound’s energy has
been absorbed, we can conclude that the sound cannot be heard. But if the
ray makes it all the way to the listener without losing all sound energy, we can
attenuate the gain of the dry sound component by the corresponding amount
to simulate transmission of the sound.


<!-- source-pdf-page: 989 -->
> Visual fallback for diagrams/images: [PDF page 989](../../../visual_pages/page_0989.jpg)

Analyzing the Indirect Path

Determining whether the indirect path is occluded is a much more difficult
problem. Ideally, we’d perform some kind of search (A* perhaps) to deter-
mine whether or not a path exists from the source to the listener, and also
how much attenuation and reflection is introduced by each viable path. In
practice, this path tracing method is rarely used because it is processor- and
memory-intensive. And at the end of the day, we game programmers aren’t
really interested in creating physically accurate simulations that will win us
Nobel prizes in physics. We merely want to produce a soundscape that is im-
mersive and believable.
Never fear, all is not lost. There are all sorts of ways in which we can ob-
tain an approximate model of the indirect path of a sound. For example, if we
are using reverb regions to model the overall acoustics of the various spaces in
our game (see Section 14.4.5.2), we could leverage these regions to determine
whether an indirect path exists. For example, we could use some simple rules
of thumb:

1.
If the source and listener are in the same region, assume an indirect path
exists.

2.
If the source and listener are in different regions, assume the indirect
path is occluded.

Using these assumptions combined with the results of our direct path ray cast,
we can differentiate between the four cases: free, occluded, obstructed or ex-
cluded.

Accounting for Diffraction

When any wave passes through a narrow opening or interacts with a corner,
it spreads out as shown in Figure 14.34. We call this phenomenon diffraction.
Because of diffraction, sounds can be heard around corners as if a direct path
existed, as long as the angular difference between the direct path and curved
path is not too great.
One way to determine whether sound can diffract in order to reach the lis-
tener is to cast a few “curved” rays around the central “direct” ray. Most colli-
sion engines don’t support curved path tracing, but we can emulate a curved
path by using multiple straight-line ray casts. Figure 14.35 shows a simple ex-
ample, in which five rays are cast from the sound source to the listener—one
direct ray, plus two “curved” traces comprised of two straight-line ray casts
each. Technically speaking we’re employing a piecewise-linear approximation to
each curved path we wish to trace.


<!-- source-pdf-page: 990 -->
> Visual fallback for diagrams/images: [PDF page 990](../../../visual_pages/page_0990.jpg)

If the direct ray is occluded but the curved traces can “see” the listener,
this tells us that the listener is within the “diffraction region” around a nearby
corner, and should hear the sound as if it is not occluded.

Applying the Model Using Reverb and Gain

Thus far, we’ve discussed how to determine whether the direct and indirect
paths are blocked or not. This analysis can also tell us something about the
acoustic impact of an occlusion or obstruction. (For example, sound passing
through a wall can be muffled; sound taking a long “bouncy” path might intro-
duce a lot of reverb.) The question now is: How do we apply this knowledge
when rendering the sound?

One simple approach is to simply attenuate the dry and wet components
of the sound individually, based on whether the direct or indirect paths are
totally or partially blocked, respectively. To finesse the results, we can also
apply more or less reverb to each component of the sound, based on whatever
heuristic information we gathered when determining the path(s) taken by the
sound. The needs of every game are different, so this is one of those times
when trial and error is your best and only option!

Blending Obstructed Sounds

If you were to go off and implement everything we talked about in the sec-
tions above, you’d notice a glaring problem. As a sound source moves be-
tween the four states described above—for example, from being free to being
obstructed—the timbre and loudness of the sound will seem to “pop.” There
are a number of ways to smooth out such transitions. You could apply a little
hysteresis, meaning that you delay the response of the sound system to changes
in the obstructed state of each sound, and then use this short delay window
to smoothly cross-blend between the two sets of reverb settings. But the delay

Figure 14.34. Diffraction causes the dry component of a sound to be clearly audible even when the
direct path is blocked.


<!-- source-pdf-page: 991 -->
> Visual fallback for diagrams/images: [PDF page 991](../../../visual_pages/page_0991.jpg)

Figure 14.35. Curved ray casts can be approximated using multiple straight-line rays.

might be noticeable, so this isn’t an ideal solution.

For the Uncharted and The Last of Us series, Naughty Dog’s senior sound
programmer Jonathan Lanier invented a proprietary system that he called sto-
castic propagation modeling. Without giving away any trade secrets, I can tell
you that this system involves casting a bunch of rays to each sound source,
some direct and some indirect, and accumulating these hit/miss results over
many frames. From this data, we generate a probabalistic model of the de-
gree of occlusion experienced by both the dry and wet components of each
sound source. This allows us to smoothly transition a sound from being fully
obstructed to fully free without noticeable “pops.”

14.4.5.4
Sound Portals in The Last of Us

For The Last of Us, Naughty Dog needed a way to model the actual pathways
that sounds take through the environment. If an enemy NPC is speaking
while standing in a long hallway that connects to the room the player is in,
we wanted to be able to hear the sound of his voice coming from the doorway,
not “through the wall” along a straight-line path.

To do this, we used a network of interconnected regions. There were two
kinds of regions: rooms and portals. For each sound source, we found a path
from the listener to the sound by using connectivity information provided by
the sound designer when laying out the regions. If both the sound source and
listener were in the same room, we’d use the tried and true method of perform-
ing obstruction/occlusion/exclusion analysis that we used on the Uncharted
series. But if the sound source was in a room directly connected to the lis-
tener’s room via a portal, we would play the sound as if it were located in the
portal region. We found that we only needed to go “one hop” in the room con-
nectivity graph to make this work for all real situations that arose in the game.
Obviously I’m leaving out a lot of important details here, but Figure 14.36 il-
lustrates the basics of how this system worked.


<!-- source-pdf-page: 992 -->
> Visual fallback for diagrams/images: [PDF page 992](../../../visual_pages/page_0992.jpg)

Fake
Sound
Source

Portal
Region

Figure 14.36. The portal-based audio propagation model used in The Last of Us by Naughty Dog,
Inc.

14.4.5.5
Further Reading on Environmental Acoustics

Audio propagation modeling and acoustics analysis are areas of active re-
search, and more and more advanced techniques are being applied in the game
industry as hardware capabilities continue to improve. A few links are listed
below to whet your appetite, but a Google search for “sound propagation” or
“acoustics modeling” will provide many more hours of enjoyment!

•
“Real-Time Sound Propagation in Video Games” by Jean-François Guay
of Ubisoft Montreal (https://bit.ly/2HdBiLc);

•
“Modern Audio Technologies in Games” presented at GDC 2003 by A.
Menshikov (https://bit.ly/2J7FYyD);

•
“3D Sound in Games” by Jake Simpson (https://bit.ly/2HfVFTU).

### 14.4.6 Doppler Shift

As we saw in Section 14.1.3.5, the Doppler effect is a change in frequency
that’s dependent upon the relative velocity between source and listener: vrel =
vsource −vlistener. This frequency change can be approximated by simply time-
scaling the sound signal. This results in the “chipmunk effect” with which
Alvin and the Chipmunks have made us all so familiar—by speeding up a
sound, the pitch also rises. Because our sound signals are digital (i.e., sam-
pled discrete-time signals), this kind of time scaling can be accomplished via
sample rate conversion (see Section 14.5.4.4). However, this is not strictly the


<!-- source-pdf-page: 993 -->
> Visual fallback for diagrams/images: [PDF page 993](../../../visual_pages/page_0993.jpg)

correct thing to do, because the speeding up or slowing down of the sound
can become noticable.
The ideal solution is to apply a pitch shift without affecting the time
axis.
This can be done in a number of ways, including the phase vocoder
and time domain harmonic scaling approaches. A complete description of these
techniques is beyond our scope here, but you can read more about them at
http://www.dspdimension.com/admin/time-pitch-overview.
Time-independent pitch shifting technology is an extremely powerful
thing to have in your audio engine, in part because it also allows you to per-
form frequency-independent time scaling. So not only can you alter the pitch
of sounds without changing timing for Doppler, you can also speed up or slow
down sounds without altering their pitch for all sorts of other cool effects.

## 14.5 Audio Engine Architecture

To this point, we’ve discussed the concepts and methodologies behind 3D
sound rendering, and the theory and technologies that underlie them. In this
section, we’ll turn our attention to the architecture of the software and hard-
ware components used to implement a 3D audio rendering engine.
As with most computer systems, a game engine’s audio rendering software
is typically arranged into a “stack” of layered hardware and software compo-
nents (see Figure 14.37).

•
Hardware inevitably serves as the foundation of this structure, providing
at minimum the necessary circuitry to drive the digital or analog speaker
outputs that connect our PC or game console to a pair of headphones, a
TV or a surround sound home theater system. Audio hardware may
also provide “acceleration” to the software above it in the stack by sup-
plying codecs, mixers, reverb tanks, effects units, waveform synthesizers
and/or DSP chips in silicon. This hardware is often called the sound card

Figure 14.37. The audio hardware/software “stack.”


<!-- source-pdf-page: 994 -->
> Visual fallback for diagrams/images: [PDF page 994](../../../visual_pages/page_0994.jpg)

because PCs sometimes provide their audio capabilities via a plug-in pe-
ripheral card.

•
On a personal computer, the hardware is typically encapsulated in a
driver layer, allowing the OS to support sound cards from a wide range
of vendors.

•
On both PCs and game consoles, the hardware and drivers are usually
wrapped in a low-level application programming interface (API) designed
to free the programmer from having to deal with the minutia of control-
ling the hardware and drivers directly.

•
The 3D audio engine itself is built on top of these foundations.

The feature set presented to the programmer by the audio hardware/soft-
ware stack is usually modeled after the feature set of a multi-channel mixer con-
sole (http://en.wikipedia.org/wiki/Mixing_console) of the sort used in rec-
ording studios and at live concerts (see Figure 14.38). A mixer board can accept
a relatively large number of audio inputs obtained from microphones and/or
electronic instruments. The input sounds can be filtered and equalized, and re-
verb and other effects can be applied to them. The console is then used to mix
all of the signals together, setting the relative volumes of the sounds as desired
by the sound designer. The final mixed output is routed to the speakers (for a
live performance) or to the individual channels of a multi-track recording.
In the same sense, the audio HW/SW stack must accept a large number of
inputs (2D and 3D sounds), process them in various ways, mix them together
so that their relative gains are set appropriately and finally pan these signals
to the speaker output channels to produce the illusion of a three-dimensional
soundscape for the human player.

### 14.5.1 The Audio Processing Pipeline

As we learned in Section 14.4.1, the process of rendering a 3D sound involves
a number of discrete steps:

•
For each 3D sound, a “dry” digital (PCM) signal must be synthesized.

•
Distance-based attenuation is applied to provide a sense of distance from
the listener, and reverb is applied to the signal to model the acoustics
of the virtual listening space and to provide spatialization cues to the
listener. This produces a new “wet” signal.

•
The wet and dry signals are panned (independently) to one or more
speakers in order to produce the final “image” of each signal in three-
dimensional space.


<!-- source-pdf-page: 995 -->
> Visual fallback for diagrams/images: [PDF page 995](../../../visual_pages/page_0995.jpg)

Figure 14.38.
A multi-channel mixer console by Focusrite with support for 72 inputs and 48
outputs.

•
The panned multi-channel signals of all the 3D sounds are mixed together
into a single multi-channel signal, which is either sent through a paral-
lel bank of DACs and amps to drive the analog speaker outputs or sent
directly to a digital output such as HDMI or S/PDIF.

Clearly, we think of the process of rendering 3D audio as a pipeline. And
because a game world typically contains a large number of sound sources,
multiple instances of this pipeline are in flight simultaneously. For this reason,

6-Channel

dry
Synth
Distance
Attenuation

Pan

Reverb

Pan

wet

7.1 Out

dry
Synth
Distance
Attenuation

Mixer

Pan

LFE
Gen

Reverb

Pan

wet

Figure 14.39. The audio processing graph (pipeline).


<!-- source-pdf-page: 996 -->
> Visual fallback for diagrams/images: [PDF page 996](../../../visual_pages/page_0996.jpg)

the audio processing pipeline is sometimes called the audio processing graph. It
truly is a graph of interconnected components, ultimately culminating in the
handful of speaker channels that comprise the final mixed, panned output.
Figure 14.39 presents a high-level view of the audio graph.

### 14.5.2 Concepts and Terminology

Before we can explore the audio processing pipeline in any depth, we need
to become familiar with a few concepts and the terminology used to describe
them.

14.5.2.1
Voices

Each 2D or 3D sound passing through the audio rendering graph is called a
voice. This term comes from the early days of electronic music: A synthesizer
would produce musical notes via a set of waveform generators called “voices.”
A synthesizer contains a limited number of waveform generator circuits,
so electronic musicians speak of how many simultaneous voices their synth
can produce. In the same sense, a game’s audio rendering engine typically
has a limited number of codecs, reverb units and so on. The maximum num-
ber of voices supported by a particular audio HW/SW stack is dictated by
the number of independent parallel pathways through the audio graph. This
number is generally bounded by limited memory resources, limited hardware
resources and/or processing power limitations. This number is sometimes re-
ferred to as the degree of polyphony supported by the system.

2D Voices

A game’s audio rendering pipeline must also be capable of handling 2D
sounds, such as music, menu sound effects, narrator voice-overs and so on.
2D voices are also processed by the audio rendering pipeline. The main things
that differentiate 2D sound processing from 3D processing are:

•
2D sounds originate as multi-channel signals, one for each available
speaker, whereas 3D sounds originate as dry monophonic signals. As
such, 2D sounds do not pass through a pan pot.

•
A 2D sound may contain “baked” reverb or other effects. If so, the sound
may not make use of the reverb capabilities of the rendering engine.

As such, 2D sounds typically enter the pipeline just prior to the master mixer,
where they are combined with the 3D sounds to produce the final “mix.”


<!-- source-pdf-page: 997 -->
> Visual fallback for diagrams/images: [PDF page 997](../../../visual_pages/page_0997.jpg)

Codec

Gain

Distortion

7-Channel

Post-Send
Filter

Pre-Send
Filter
Pan

dry

wet
Reverb
Pan

wet
Reverb
Pan

wet
Reverb
Pan

Figure 14.40. The pipeline through which an individual 3D voice passes on its way through the
audio graph.

14.5.2.2
Buses

The interconnections between the components that make up the audio graph
are called buses. In electronics, a bus is a circuit whose primary purpose is to
connect other circuits to one another. In software, a bus is nothing more than
a logical construct that describes the presence of an interconnection between
components.

### 14.5.3 The Voice Bus

Figure 14.40 presents a more-detailed view of the pipeline of components
through which a single 3D voice passes as it is rendered by the audio engine.
In the following sections, we’ll explore each of these components in detail and
learn why they are interconnected in the way that they are.

14.5.3.1
Sound Synthesis: Codecs

An audio signal passes through the rendering graph in digital form.
The
term synthesis describes the process of generating these digital signals. Au-
dio signals may be synthesized by simply “playing back” a pre-recorded au-
dio clip. They might also be procedurally generated, perhaps by combining
one or more fundamental waveforms (sinusoid, square wave, sawtooth, etc.),
and/or by applying various filters to a harmonically rich noise signal. Since


<!-- source-pdf-page: 998 -->

most games use pre-recorded audio clips almost exclusively, we’ll restrict our
discussion to them here.
Pre-recorded audio clips can be provided to the game engine in any one
of the myriad compressed and uncompressed audio file formats in use today
(see Section 14.3.2.3). Raw PCM data is the “canonical” format accepted by
the various components in the audio processing graph. Therefore, a device
or software component known as a codec is used to convert each source audio
clip into a raw PCM data stream. The codec interprets the source data format,
decompresses the data if necessary, and then transmits it onto the voice bus
for its journey through the audio processing graph.

14.5.3.2
Gain Control

The loudness of each source sound in the 3D world can be controlled in a num-
ber of ways: When recording the audio clip, we can set the recording levels to
produce a sound at the desired loudness. We can process the clip in an offline
tool to adjust its gain. At runtime, we can also dynamically adjust the volume
of the clip using a gain control component within the audio graph. See Section
14.3.1.7 for a detailed discussion of gain control.

14.5.3.3
Aux Sends

When a sound engineer at a recording studio or live concert wants to apply
effects to a sound, he or she can route the sound out of the multi-channel mix-
ing console, through an effects “pedal,” and then back into the mixing board
for further processing. These outputs are known as auxiliary send outputs, or
aux sends for short.
Within the audio processing graph, the term “aux send” is used in an anal-
ogous manner: It describes a bifurcation point in the pipeline, splitting the
signal into two parallel signals. One of these signals is for the dry component
of the sound. The other is piped through a reverb/effects component to create
the wet component of the sound.

14.5.3.4
Reverb

The wet signal path is typically routed through a component that adds early
reflections and late reverberations. Reverb might be implemented using a con-
volution, as described in Section 14.4.5.1. If convolution is not practical in
real time, either because the console or PC lacks DSP hardware or because the
game’s CPU and/or memory budgets are insufficient, reverb can be imple-
mented using a reverb tank. This is essentially a buffering system that caches
time-delayed copies of a sound that are then mixed with the original to mimic
early reflections and/or late reverberations, combined with a filter to mimic the


<!-- source-pdf-page: 999 -->

interference effects and general attenuation of high-frequency components in
the reflected sound waves.

14.5.3.5
Pre-Send Filter

The voice pipeline typically includes a filter that is applied before the aux send
bifurcation, and therefore applies to both the dry and wet components of the
sound. This is called a pre-send filter. It is generally used to model phenomena
that occur at the source of the sound. For example, we could mimic the sound
of someone wearing a gas mask with a pre-send filter.

14.5.3.6
Post-Send Filter

Another filter is typically provided after the aux send bifurcation. As such,
this filter only applies to the dry component of the sound. This filter can be
useful for modeling the muffling effect of an obstruction/occlusion on the di-
rect sound path. At Naughty Dog, we also use a post-send filter to implement
the frequency-specific fall-off that occurs due to atmospheric absorption (see
Section 14.1.3.2).

14.5.3.7
Pan Pots

The dry and wet components of a 3D sound are monophonic signals through-
out their journey along the voice bus. At the very end of the pipeline, each one
of these two mono signals must be panned to the two stereo speakers/head-
phones or the five or seven surround sound speakers. For this reason, every
3D voice bus terminates in two or more pan pots, one for the dry signal and
one or more for the wet. The components may be panned differently. The dry
signal is panned according to the actual location of the source. The wet sig-
nal, however, may be panned with a wider focus to simulate the way in which
reflected sound waves tend to impinge on the listener’s head from various di-
rections. If the sound is coming from a narrow doorway, the focus of the wet
signal might be only a few degrees. But if the listener is standing in the cen-
ter of a cavernous hall, the wet signal should probably be given a 360-degree
focus (i.e., it should be rendered in all speakers equally).

### 14.5.4 Master Mixer

Each pan pot’s output is a multi-channel bus, containing signals for each of
the desired output channels (stereo or surround sound). A game typically has
a large number of 3D sounds playing simultaneously. The master mixer takes
all of these multi-channel inputs and mixes them together into a single multi-
channel signal for output to the speakers.


<!-- source-pdf-page: 1000 -->

Depending on the specifics of the implementation, the master mixer might
be implemented in hardware, or it might live entirely in software. If the master
mix is performed in hardware, the sound card designer has the option of per-
forming an analog mix or a digital mix. (Software can only do digital mixing,
for obvious reasons.)

14.5.4.1
Analog Mixing

An analog mixer is essentially just an summation circuit—the amplitudes of
the individual input signals are added together, and the resultant wave’s am-
plitude is then attenuated to fall back within the desired signal voltage range.

14.5.4.2
Digital Mixing

Mixing can also be performed digitally by software running on a dedicated
DSP chip or a general-purpose CPU. A digital mixer takes multiple PCM data
streams as its inputs, and produces a single PCM data stream at its output.
A digital mixer’s job is a little more complicated than that of an analog
mixer, because the collection of PCM channels it is combining may have been
recorded at different sample rates and/or different bit depths. Two processes
known as sample depth conversion and sample rate conversion must be executed
on all of the mixer’s input signals to bring them into a common format. Once
this has been done, mixing again becomes trivial. At each time index, the val-
ues of all the input samples are simply added together, and the final output
amplitude is adjusted if necessary to bring the combined signal into the de-
sired volume range.

14.5.4.3
Sample Depth Conversion

If the bit depths of the mixer’s input signals differ, sample depth conversion can
be used to convert them to a common format. This operation is trivial. We
simply de-quantize the input sample values into floating-point format, and
then re-quantize each one at the desired output bit depth. See Section 12.8.2
for all the gory details on quantization.

14.5.4.4
Sample Rate Conversion

If the sample rates of the input signals differ, sample rate conversion must be used
to convert them all into the desired output sample rate prior to mixing. In
principle, this involves converting the signal into analog form, and then re-
sampling it at the desired rate (which could be done using D/A and A/D
hardware). In practice, analog sample rate conversion tends to introduce un-
wanted noise, so the conversion is almost always accomplished by running a
direct digital-to-digital algorithm directly on the PCM data stream.


<!-- source-pdf-page: 1001 -->
> Visual fallback for diagrams/images: [PDF page 1001](../../../visual_pages/page_1001.jpg)

7.1-Channel
7-Channel

Pre-
Amp

EQ

LFE
Gen

7.1 Out

Compressor

Vol.

Figure 14.41. A typical master output bus.

An understanding of signal processing theory (see Section 14.2) is neces-
sary to fully understand how these algorithms function, and a full discussion is
beyond our scope here. But in certain simple cases, the concept is easy enough
to grasp. For example, if we are doubling the sample rate, we can interpo-
late adjacent samples and insert these values as new samples, thereby dou-
bling the number of samples. It’s not quite as simple as this—one must take
care to avoid introducing aliasing into the resulting signal, for example. See
http://en.wikipedia.org/wiki/Sample_rate_conversion for a detailed discus-
sion of sample rate conversion.

### 14.5.5 The Master Output Bus

Once the voices have been mixed, they are processed by the master output bus.
This is a collection of components that process the output prior to sending it to
the speakers. A typical master output bus is depicted in Figure 14.41, and its
components are described briefly below. Every audio engine does things a bit
differently, and not all engines support all of the components described below.
Some engines may also introduce additional components not shown here.

•
Pre-amp. The pre-amp allows the master signal’s gain to be trimmed
prior to passing through the remainder of the output bus.

•
LFE generator. As we mentioned in Section 14.4.4, a pan pot only drives
the two, five or seven “main” speakers of a stereo or surround sound sys-
tem. The LFE (subwoofer) channel does not contribute to the positioning
of a sound’s 3D image. An LFE generator is a component that extracts the
very lowest frequencies of the final mixed signal and uses this to drive
the LFE channel.


<!-- source-pdf-page: 1002 -->

•
Equalizer. Most audio engines provide some kind of equalizer (EQ). As
described in Section 14.2.5.8, an EQ allows specific frequency bands in
the signal to be boosted or attenuated individually. A typical EQ divides
the spectrum up into anywhere from four to tens of individually tunable
bands.
•
Compressor. A compressor performs dynamic range compression (DRC)
on the audio signal. A compressor reduces the volume of the loudest
portions of the signal and/or increases the volume of the quietest mo-
ments. It does this automatically by analyzing the input signal’s volume
characteristics and adjusting the compression dynamically. See http://
en.wikipedia.org/wiki/Dynamic_range_compression for a detailed dis-
cussion of DRC.
•
Master gain control. This component allows the overall volume of the
entire game to be controlled.
•
Outputs. The output of the master bus is a collection of line-level analog
signals corresponding to the speaker channels and/or a digital HDMI
or S/PDIF multi-channel signal, suitable for transmission to a TV or a
home theater system.

### 14.5.6 Implementing a Bus

14.5.6.1
Analog Buses

An analog bus is implemented via a number of parallel electronic connections.
To carry a monophonic audio signal, we need two parallel wires or “lines” on
the circuit board: one to carry the voltage signal itself, and one to serve as
ground.
An analog bus operates pretty much instantaneously. The output signal
from an upstream component is immediately consumed by the next down-
stream component, because the signal is a continuous physical phenomenon.
Such circuits are quite simple. The only real complication is ensuring that the
voltage levels and impedances of the input and output signals match.

14.5.6.2
Digital Buses

One could imagine using simple digital circuitry to build instantaneous con-
nections between our digital components. However, this would require the
connected components to run in perfect lock-step: At the exact moment that
the sender produces a byte of data, the receiver would have to consume it.
Otherwise, the byte would be lost.
To overcome the synchronization problems inherent in connecting two dig-
ital components, ring buffers are typically used at the input and/or output of


<!-- source-pdf-page: 1003 -->

each component. A ring buffer is a buffer that can be shared by two clients—
one reader and one writer. To make this work, we maintain two pointers or
indices within the buffer, called the read head and the write head. The reader
consumes data at the read head, advancing it forward in the buffer as data is
consumed, and wrapping when the end of the buffer is reached. The writer
stores data into the buffer at the write head, advancing and wrapping as well.
Neither head is permitted to “pass” the other, which guarantees that the two
clients cannot conflict with one other (i.e., reading data that hasn’t been written
yet, or writing over top of data that is currently being read).
The simplest way to connect, say, the digital output of a codec to the digital
input of a DAC is to use a shared ring buffer. The codec writes to the exact same
buffer read by the DAC.
While simple, the shared buffer approach only works when the two compo-
nents have access to the same physical memory. This is trivial to do when the
components are running in threads on a single CPU. To make a shared mem-
ory approach work between two separate operating system processes, each of
which has its own private virtual memory space, the OS needs to provide a
mechanism for mapping the same physical memory into each process’s vir-
tual address space.
This is usually only possible when the two processes
are running on the same core, or on different cores within a multicore com-
puter.
If the two components are running on different cores that cannot share
memory (as would be the case if one were running on the PC and the other on
a plug-in sound card, for example), then each component needs its own input
or output buffer. Data must be copied from the output buffer of one component
to the input buffer of the other. This might be accomplished via a direct mem-
ory access controller (DMAC), as is the case when transferring data between the
PPU and the SPUs on the PS3. It might also be accomplished via a specialized
bus, such as the ubiquitous PCI Express (PCIe) bus that is used to connect the
main CPU core(s) to plug-in peripheral cards on a PC.

14.5.6.3
Bus Latency

In order to play sound, the game or application must feed audio data period-
ically into the codecs that ultimately drive the speaker outputs. We call this
servicing the audio. The rate at which the game or app services its audio is
crucial to proper sound production: If packets are sent too infrequently, the
buffers will underflow, meaning that the device consumes all of the data before
a new packet arrives. This causes the audio to drop out briefly while the soft-
ware catches up. If packets are sent too often, the PCM buffers can overflow,
causing packets to be lost. This causes the audio to seem to “skip.”


<!-- source-pdf-page: 1004 -->

The size of the input and output buffers that comprise a digital bus dictates
the latency of the sound system—in other words, how much delay is intro-
duced by the bus. If the buffers are very small, latency is minimized, but this
places a greater burden on the CPU because it must feed the buffers more fre-
quently. Likewise, larger buffers translate into less CPU load but also higher
latency. We usually measure the latency of a piece of audio hardware in mil-
liseconds, rather than measuring the size of the buffers in bytes. This is done
because buffer size depends upon the data format and the degree of compres-
sion supported by the codec, but the latency is really what we care about when
trying to produce high-fidelity sound.
How much latency is acceptable? This depends on the application. Profes-
sional audio systems require very short latencies—on the order of 0.5 ms. This
is because audio signals are often fed through a network of audio hardware
before being synchronized with each other and often to a video signal as well.
Every time latency is introduced by the hardware, accurate synchronization
becomes more difficult.
Game consoles, on the other hand, can tolerate a longer latency. In a game,
all we care about is synchronizing the audio and the graphics. If the game is
rendering at 60 FPS, this translates into 1/60 = 16.6 ms per frame. As long
as the audio isn’t delayed by more than 16 ms, we know it will be in sync
with graphics rendered for that same frame. (In fact, many games use double
or triple buffering for their rendering engines, which introduces one or two
frames of delay between the time the game requests that a frame be drawn
and when that frame will actually appear on the TV screen. The television
may also introduce a delay. As such, a triple-buffered 60 Hz game can actually
tolerate an audio latency of 3 × 16 = 48 ms or more.) The PlayStation 3’s DMA
controller runs every 5.5 ms, so PS3 audio systems are typically configured
such that the audio buffers can hold an integer multiple of 5.5 ms worth of
audio.

### 14.5.7 Asset Management

14.5.7.1
Audio Clips

The most atomic audio asset is a clip—a single digital sound asset with its own
local timeline (analogous to an animation clip). A clip is sometimes called
a sound buffer, because the digital sample data is stored in a buffer. A clip
might encapsulate monophonic audio data (typical for 3D sound assets), or
it might contain multi-channel audio (typically used for 2D assets or stereo
sound sources in 3D). A clip may be stored in any of the audio file formats
supported by your engine.


<!-- source-pdf-page: 1005 -->

14.5.7.2
Sound Cues

A sound cue is a collection of audio clips plus metadata that describes how
they should be processed and played. Cues are usually the primary means
by which the game can request sounds to be played. (Playing individual clips
may or may not be supported by the engine.) Cues also serve as a convenient
division-of-labor mechanism: The sound designers can craft the cues using an
offline tool, without having to worry about how or when they’ll be played in-
game. And the game programmers can play the cues conveniently in response
to relevant events in the game, without having to worry about micromanaging
the details of playback.
There are many ways in which the collection of clips in a cue could be
interpreted and played back. A cue might contain clips representing the six
channels of a pre-mixed 5.1 music recording. A cue might also collect up a set
of raw sounds, from which a random selection can be made, for the sake of
variety. A cue might also be set up to play its collection of raw sounds in a
predefined sequence. A cue typically specifies whether the sound(s) it encap-
sulates represent a one-shot sound or a looping sound.
Some audio engines permit a cue to provide one or more optional sound
clips that only play if the main sound is interrupted midway through playing.
For example, a vocal cue might include a “glottal stop” sound that is played
only if the person’s line of dialog is interrupted. This feature can also be used
to provide a distinct “tail” sound that is played when a looping cue is stopped.
For example, a looping machine gun sound cue might use this “tail” clip fea-
ture to produce a suitable echoing fall-off sound when the firing ceases.
A cue’s metadata might include whether it is intended to be played in 3D or
2D; the FO min, FO max and fall-off curve of the sound source; group member-
ship (see Section 14.5.8.1); and possibly any special effects, filtering or equal-
ization that should be applied when the sound is played. In Sony’s Scream
engine—the sound engine used by Naughty Dog in its Uncharted and The Last
of Us series—a cue can contain arbitrary script code that allows a sound de-
signer to completely control how the encapsulated sound asset(s) are played
when the cue plays.

Playing a Cue

Every audio engine that supports the concept of cues provides an API for
playing them. This API is usually the primary way—and sometimes the only
way—for the game code to request that a sound be played.
The cue playing API generally allows the programmer to specify whether
the cue should be played as a 2D or 3D sound, to provide 3D position and


<!-- source-pdf-page: 1006 -->

velocity parameters, to specify whether the sound should loop or play only
once, and to specify whether the source buffer is in-memory or streamed. The
API usually also allows us to control the volume of the sound and possibly
other aspects of playback.

Most APIs return a sound handle to the caller. This handle allows the pro-
gram to keep track of a sound as it is playing, so that it can be modified or
canceled before the sound ends. A sound handle is usually implemented un-
der the hood as an index into a global handle table, rather than as a raw pointer
to the data that descibes the sound instance. That way, if the sound ends natu-
rally, the handle can be “nulled out” automatically. A handle mechanism can
also be used to make the system thread-safe—if one thread kills the sound,
other threads that have handles to the sound will automatically see their han-
dles become invalid.

14.5.7.3
Sound Banks

A 3D audio engine manages a lot of assets. The game world contains a large
number of objects. Each object can generate a variety of sounds. And in ad-
dition to 3D sound effects, we have music, speech, menu sound effects and so
on.

All of this audio data takes up an immense amount of space, so we can’t
keep it all in memory at once. On the other hand, the individual audio clips are
too fine-grained and too numerous for them to be managed on an individual
basis. As such, most game engines package their sound clips and cues into
coarse units called sound banks.

Some sound banks are loaded when the game starts up and left in memory
forever. For example, the collection of sounds made by the player character are
always needed, so we could keep them in memory indefinitely. Other banks
might be loaded and unloaded dynamically as the needs of the game change.
For example, the sounds in level A might not be used in level B, so we can load
the “A” bank only when level A is being played. For example, in Naughty
Dog’s The Last of Us, the sounds of rain, flowing water and the creaking of
beams on the verge of collapse were only loaded when the player was in the
tilted building in Boston.

Some sound engines permit banks to be relocated in memory. This feature
can entirely eliminate the memory fragmentation problems that would other-
wise arise as lots of differently sized banks are loaded and unloaded during
gameplay. See Section 6.2.2.2 for more information on memory relocation.


<!-- source-pdf-page: 1007 -->

14.5.7.4
Streaming Sounds

Some sounds are so long in duration that they cannot be conveniently stored
in memory all at once. Music and speech are common examples. For these
kinds of sounds, many game engines support streaming audio.
Streaming audio is possible because when playing a sound, the only in-
formation we actually need is the signal data at and around the current time
index. To implement streaming, we maintain a relatively small ring buffer for
each streaming sound. Prior to playing the sound, we pre-load a small chunk
of it into the buffer, then play the sound normally. The audio pipeline con-
sumes the data from the ring buffer as it plays, making room for us to load
more data. As long as we keep filling the buffer with data before it is all con-
sumed, our sound will play seamlessly.

### 14.5.8 Mixing Your Game

If we were to play every sound coming from every game object, properly atten-
uated and spatialized and acoustically modeled, using all the techniques and
technology we’ve discussed thus far, what would be the result? We might ex-
pect the answer to this question to be “an incredibly immersive and believable
soundscape that wins awards and makes us rich!” But what we’d actually get
is cacophony.
What separates a good game from a great game is the mix—what you hear,
how much of it you hear, and just as importantly what you don’t hear. The
goal of a game’s sound designer is to produce a final mix that:

•
sounds realistic and emersive;

•
isn’t too distracting, annoying or difficult to listen to;

•
conveys all information relevant to gameplay and/or story effectively;
and

•
maintains a mood and tonal color that is always appropriate, given the
events taking place in the game and the overall design of the game.

All sorts of different kinds of sounds must come together in the game’s
mix. These include music, speech, ambient sounds like rain, wind, insects or
the creak of an old building, sound effects such as weapons fire, explosions
and vehicles, and the bumps, slides and rolling sounds made by physically
simulated objects.
Various techniques are employed to ensure the mix of the game meets these
goals. We’ll explore a few of them in the following sections.


<!-- source-pdf-page: 1008 -->

14.5.8.1
Groups

The most obvious thing we can do to improve the mix of our game is to set
the levels of all the source sounds in the 3D world appropriately. The impor-
tant thing here is to ensure that each sound’s gain is appropriate relative to
the other sounds in the game. For example, footsteps should be quieter than
gunfire.
In some games, the loudness of certain sounds needs to change dynami-
cally. Often we want to control an entire category of sounds at once. For ex-
ample, during a frenetic fight sequence, we might want to bring up the levels
of the music and the weapons, and drop the volume of ancillary sound effects.
Or during a quiet moment with characters talking to one another, we might
want to boost the speech a little and tone down ambient sounds to ensure the
dialog can be heard.
For this reason, many audio engines support the concept of groups—a con-
cept “stolen” from our old friend the multi-channel mixing console. On a mix-
ing board, a collection of sound inputs can be routed to an intermediate mixer
circuit, combining them into a single “group signal.” The gain of this signal
can then be controlled with a single knob on the board, thereby allowing the
sound engineer to control the loudness of all input signals at once.
In the software world, groups are implemented by simply categorizing
sound cues, rather than physically mixing their signals together. For exam-
ple, we can classify a cue as being music, a sound effect, a weapon, a line of
speech and so on. Then, the engine can provide a means of controlling the
gains of all sounds in each category with a single control value. Groups usu-
ally also allow entire categories of sound to be paused, restarted and muted
conveniently with a simple API call.
Some sound engines do also provide a mechanism for physically mixing
groups of audio signals into a single signal, just as is done when working with
groups on a mixing console. In Sony’s Scream engine, this is called generating
a pre-master submix. After the relative gains of the signals in the group have
been locked down by the submix, the resulting signal can be routed through
additional filters or other processing stages. This gives the sound designer
even more control over the mix of the game.

14.5.8.2
Ducking

Ducking is a technique in which the volume/gain of certain sounds are tem-
porarily reduced in order to make other sounds more audible. For example,
when a character is speaking, the level of background noise could be reduced
automatically to make the dialog more audible.


<!-- source-pdf-page: 1009 -->

Ducking can be triggered in numerous ways. The presence of one particu-
lar type of sound might be used to duck another category of sounds. A game
event might trigger a duck programmatically. Any triggering mechanism that
is deemed appropriate can be used to initiate a duck.
The reduction of volume caused by a duck is typically accomplished via
the group categorization system: When one category of sound is playing, it
can automatically duck one or more other categories by various amounts. Or
the game code can call a function to duck a group of sounds programmatically.
Ducking can also be performed by routing one sound signal into the side-
chain input of the dynamic range compressor (DRC) on a different voice’s bus.
Recall from Section 14.5.5 that a DRC analyzes the volume characteristics of a
signal and automatically compresses the loudness of the signal appropriately.
When a side-chain input is connected to a DRC, it analyzes the side-chain signal
when deciding how to adjust the volume. So, we can arrange for increased
loudness in one signal to cause a decrease in the dynamic range of another
signal.

14.5.8.3
Bus Presets and Mix Snapshots

Many sound engines allow the sound designer to set up configuration param-
eters, save them off and then recall and apply them conveniently at runtime.
In Sony’s Scream engine, these come in two basic flavors: bus presets and mix
snapshots.
A bus preset is a set of configuration parameters that control aspects of the
components on a single bus (voice bus or master output bus). For example,
a bus preset might describe one particular reverb set-up that mimics, say, the
acoustics of a large open hall, or the interior of a car, or a small broom closet. Or
a bus preset might control the DRC settings on the master output bus. Many
such presets can be created by the sound designer, and the appropriate ones
activated at runtime as the game requires.
A mix snapshot is the same kind of idea applied to gain control. The gains
of the various channels within a group can be established a priori and then
applied at runtime as needed.

14.5.8.4
Instance Limiting

Instance limiting is a means of controlling the number of sounds that are permit-
ted to play simultaneously. For example, even though 20 NPCs are all firing
their guns at once, we might only play the three or four gun sounds that are
nearest to the listener. Instance limiting is important for two reasons: First,
it’s a great way to prevent cacophony. Second, a sound engine typically sup-
ports only a fixed number of simultaneous voices, either because of hardware


<!-- source-pdf-page: 1010 -->

limitations (e.g., the sound card only has N codecs) or because of memory or
processor bandwidth limitations in the software, so we must use them wisely.

Per-Group Limits

Instance limiting is sometimes applied differently to different groups of
sounds. For example, we might specify that we should play up to four guns
simultaneously, hear up to three people speaking at a time and allow up to
five other sound effects to play at once, plus up to two overlapping music
tracks.

Prioritization and Voice Stealing

In a 3D game with lots of dynamic elements, there may be more sounds play-
ing at any given time than the system has voices for. Some sound engines
support a large number (or even an infinite number) of virtual voices. Each vir-
tual voice represents a sound that is technically playing, but that can be muted
or stopped temporarily so it ceases to occupy valuable hardware or software
resources. The engine uses various criteria to dynamically determine which
virtual voices should be mapped to “real” voices at any given moment.
One of the simplest ways to limit the number of sounds playing simultane-
ously is to assign a maximum radius to every 3D sound source. As we saw in
Section 14.4.3.1, this is the FO max radius. If the listener is beyond this distance
from the sound, it is considered inaudible and its virtual voice is temporarily
muted or stopped, freeing its resources for use by other voices. The process of
automatically silencing virtual voices is called voice stealing.
Another common approach is to assign each cue or group of cues a priority.
When too many virtual voices are playing at once, those with lower priorities
can be silenced (stolen) in favor of higher priority voices.
Sound engines may also provide various other mechanisms for controlling
the details of the voice stealing algorithm. For example, a cue might be given
a minimum play time, after which its voice is permitted to be stolen. Sounds
might be faded out rather than cut off abruptly when their voice is stolen. And
some cues might be temporarily marked as “unstealable” to ensure that they
play, even despite their priority settings.

14.5.8.5
Mixing In-Game Cinematics

Under normal gameplay conditions, the listener or “virtual microphone” is
typically positioned at or near the location of the camera, and the sound
sources are modeled where they really are in the environment. Distance-based
attenuation, direct and indirect sound path determination, voice limiting—all


<!-- source-pdf-page: 1011 -->

are determined using these realistic positions.
However, during an in-game cinematic—a portion of the game in which
player control is suspended so that a story moment can take place—the cam-
era often pans out away from the player’s head. This kind of thing tends to
wreak havoc with our 3D audio system. We could just keep the listener/mic
locked to the location of the camera; but this is not always appropriate. For
example, if there’s a long shot of two characters speaking, we probably still
want to mix so that the characters’ voices can be heard, even though physi-
cally speaking they are too far away to be heard. In this case, we might want
to detach the listener from the camera, and artificially position it nearer to the
characters.
Mixing in-game cinematics is a lot closer to mixing a film. As such, a sound
engine needs to be capable of “breaking the rules” and doing things that aren’t
necessarily physically realistic.

### 14.5.9 Audio Engine Survey

It should be evident by now that creating a 3D audio engine is a massive un-
dertaking. Luckily for us, lots of people have already put a great deal of effort
into this task, and the result is a wide range of audio software that we can use
pretty much out of the box. This ranges from low-level sound libraries all the
way to fully featured 3D audio rendering engines.
In the following sections, we’ll survey a few of the most common audio
libraries and engines. Some of these are specific to a particular target platform,
while others are cross-platform.

14.5.9.1
Windows: The Universal Audio Architecture

In the early days of PC gaming, the feature set and architecture of PC sound
cards varied a great deal from platform to platform and vendor to vendor.
Microsoft attempted to encapsulate all of this diversity within its DirectSound
API, supported by the Windows Driver Model (WDM) and the Kernel Audio
Mixer (KMixer) driver. However, because vendors could not agree on a com-
mon feature set or set of standard interfaces, the same functionality would of-
ten be realized in very different ways on different sound cards. This required
the operating system to manage a very large number of incompatible driver
interfaces.
For Windows Vista and beyond, Microsoft introduced a new standard
called the Universal Audio Architecture (UAA). Only a limited set of hard-
ware features are supported by the standard UAA driver API—all remain-
ing features are implemented in software (although hardware manufacturers


<!-- source-pdf-page: 1012 -->

are still free to provide additional “hardware acceleration” features, as long
as they provide custom drivers to expose them). Although the introduction
of UAA limited the competitive advantage of prominent sound card vendors
like Creative Labs, it did have the desired effect of creating a solid, feature-rich
standard, which could be used by games and PC applications in a convenient
way.
The UAA standard had another positive effect on the user’s aural experi-
ence. In the old DirectSound days, a game could take complete control of the
sound card, meaning that sounds coming from the OS or other applications
such as an email program would be “locked out” and their sounds would not
play while the game was running. The new UAA architecture allowed the OS
to claim ultimate control over the final mix heard through the PC’s speakers.
Multiple applications could finally share the sound card in a reasonable and
consistent way. Search online for “Universal Audio Architecture” to find more
information on UAA.
The UAA is implemented on Windows by the so-called Windows Audio
Session API, or WASAPI for short. This API is not really intended for use by
games. It supports most advanced audio processing features in software only,
with limited support for hardware acceleration. Instead, games usually make
use of the XAudio2 API, which is described in the next section.

14.5.9.2
XAudio2

XAudio 2 is the high-powered low-level API that provides access to the au-
dio hardware on Xbox 360, Xbox One and Windows. It replaces DirectAudio
and provides access to a wide range of hardware-accelerated features includ-
ing programmable DSP effects, submixing, support for a wide range of com-
pressed and uncompressed audio formats, and multirate processing to lighten
the load on the main CPU(s).
Atop the XAudio2 API sits a 3D audio rendering library called X3DAudio.
These APIs are also available on the Windows platform for use by PC games.
Microsoft used to offer a powerful audio authoring tool called the “cross-
platform audio creation tool” or XACT for short, which was intended for
use with XNA Game Studio, but neither XNA nor XACT are supported any
longer.

14.5.9.3
Scream and BoomRangBuss

On the PS3 and PS4, Naughty Dog uses Sony’s 3D audio engine Scream and
its synth library, BoomRangBuss.
The audio hardware on a PlayStation 3 is very much like a UAA-compliant
audio device, supporting up to eight channels of audio for full 7.1 surround


<!-- source-pdf-page: 1013 -->

sound support, plus a hardware mixer and HDMI, S/PDIF, analog and USB/
Bluetooth outputs. This audio hardware is encapsulated by a collection of OS-
level libraries including libaudio, libsynth and libmixer. On top of these li-
braries, game makers are free to implement their own audio software stacks.
Sony also provides a powerful 3D-capable audio stack of its own called Scream
which game studios can use “out of the box.” Scream is available on the
PS3, PS4 and PSVita platforms. Its architecture mimics a fully featured multi-
channel mixer console.
On top of Scream, Naughty Dog implemented a proprietary 3D envi-
ronmental audio system for use on the Uncharted and The Last of Us series.
This system provides stochastic obstruction/occlusion modeling and a portal-
based audio rendering system that permits rendering a highly realistic sound-
scape.

Advanced Linux Sound Architecture

The Linux equivalent of the UAA driver model is called the Advanced Linux
Sound Architecture (ALSA). This Linux kernel component replaced the orig-
inal Open Sound System (OSSv3) as the standard way to expose audio func-
tionality to applications and games. See http://www.alsa-project.org/main/
index.php/Main_Page for more information on ALSA.

QNX Sound Architecture

QNX Sound Architecture (QSA) is a driver-level audio API for the QNX
Neutrino real-time OS. As a game programmer, you’ll probably never use
QNX. But its documentation does provide an excellent picture of the concepts
and the typical feature set of audio hardware. See http://www.qnx.com/
developers/docs/6.5.0/index.jsp?topic=%2Fcom.qnx.doc.neutrino_audio%2
Fmixer.html for these docs.

14.5.9.4
Multiplatform 3D Audio Engines

A number of powerful, ready-to-use cross-platform 3D audio engines are
available. We’ll outline the most well-known of these below.

•
OpenAL is a cross-platform 3D audio rendering API that has been de-
liberately designed to mimic the design of the OpenGL graphics li-
brary. Early versions of the library were open source, but it is now li-
censed software. A number of vendors provide implementations of the
OpenAL API spec, including OpenAL Soft (http://kcat.strangesoft.net/
openal.html and AeonWave-OpenAL (http://www.adalin.com).


<!-- source-pdf-page: 1014 -->

•
AeonWave 4D is a low-cost audio library for Windows and Linux by
Adalin B.V.
•
FMOD Studio is an audio authoring tool that features a “pro audio” look
and feel (http://www.fmod.org). A full-featured runtime 3D audio API
allows assets created in FMOD Studio to be rendered in real time on the
Windows, Mac, iOS and Android platforms.
•
Miles Sound System is a popular audio middleware solution by Rad Game
Tools (http://www.radgametools.com/miles.htm). It provides a pow-
erful audio processing graph and is available on virtually every gaming
platform imaginable.
•
Wwise is a 3D audio rendering engine by Audiokinetic (https://www.
audiokinetic.com). It is notably not based around the concepts and fea-
tures of a multi-channel mixing console, but rather presents the sound
designer and programmer with a unique interface based on game objects
and events.
•
The Unreal Engine of course provides its own 3D audio engine and pow-
erful integrated tool chain (http://www.unrealengine.com). For an in-
depth look at Unreal’s audio feature set and tools, see [45].

## 14.6 Game-Speciﬁc Audio Features

On top of the 3D audio rendering pipeline, games typically implement all sorts
of game-specific features and systems. Some examples include:

•
Split-screen support. Multiplayer games that support split-screen play
must provide some mechanism that allows multiple listeners in the 3D
game world to share a single set of speakers in the living room.
•
Physics-driven audio. Games that support dynamic, physically simulated
objects like debris, destructible objects and rag dolls require a means of
playing appropriate audio in response to impacts, sliding, rolling and
breaking.
•
Dynamic music system. Many story-driven games require the music to
adapt in real time to the mood and tension of events in the game.
•
Character dialog system. AI-driven characters seem a great deal more re-
alistic when they speak to one another and to the player’s character.
•
Sound synthesis. Some engines continue to provide the ability to synthe-
size sounds “from scratch” by combining various kinds of waveforms
(sinusoid, square, sawtooth, etc.) at various volumes and frequencies.
Advanced synthesis techniques are also becoming practical for use in
real-time games. For example:


<!-- source-pdf-page: 1015 -->

◦
Musical instrument synthesizers reproduce the natural sound of an
analog musical instrument without the use of pre-recorded audio.

◦
Physically based sound synthesis encompasses a broad range of tech-
niques that attempt to accurately reproduce the sound that would
be made by an object as it physically interacts with a virtual en-
vironment.
Such systems make use of the contact, momentum,
force, torque and deformation information available from a mod-
ern physics simulation engine, in concert with the properties of the
material from which the object is made and its geometric shape,
in order to synthesize suitable sounds for impacts, sliding, rolling,
bending and so on. Here are just a few links to research on this fas-
cinating topic: http://gamma.cs.unc.edu/research/sound, http://
gamma.cs.unc.edu/AUDIO_MATERIAL, http://www.cs.cornell.
edu/projects/sound, and https://ccrma.stanford.edu/∼bilbao/
booktop/node14.html.

◦
Vehicle engine synthesizers aim to reproduce the sounds made by
a vehicle, given inputs such as the acceleration, RPM and load
placed on a virtual engine, and the mechanical movements of the
vehicle. (The vehicle chase sequences in Naughty Dog’s three Un-
charted games all used various forms of dynamic engine modeling,
although technically these systems were not synthesizers, because
they produced their output by cross-fading between various pre-
recorded sounds.)

◦
Articulatory speech synthesizers produce human speech “from
scratch” via a 3D model of the human vocal tract. VocalTractLab
(http://www.vocaltractlab.de) is a free tool that allows students to
learn about and experiment with vocal synthesis.

•
Crowd Modeling. Games that feature crowds of people (audiences, city
dwellers, etc.) require some means of rendering the sound of that crowd.
This is not as simple as playing lots and lots of human voices over top of
one another. Instead, it is usually necessary to model the crowd as mul-
tiple layers of sounds, including a background ambience plus individual
vocalizations.

We can’t possibly cover everything from the above list in one chapter. But
let’s spend a few more pages covering some of the most common game-specific
features.


<!-- source-pdf-page: 1016 -->

### 14.6.1 Supporting Split-Screen

Supporting split-screen multiplayer is a tricky problem, because you have mul-
tiple listeners in the virtual game world, but they must share a single set of
speakers in the player’s living room. If you simply pan the sounds multi-
ple times, once for each listener, and then mix the results into the speakers
evenly, the result won’t always sound sensible. There is no perfect solution:
For example, if player A is standing right next to an explosion and player B
is standing far away, the person playing player B will still hear the explosion
loud and clear. The best a game can do is cobble together a hybrid solution, in
which some sounds are handled in a “physically correct” way and others are
“fudged” in order to produce the most sensible-sounding experience for the
players.

### 14.6.2 Character Dialog

Even if we’ve created characters for our game that look like photographs of real
human beings, and even if they move in astoundingly realistic ways, they still
won’t seem real to the player until they can speak realistically. Speech commu-
nicates information crucial to gameplay. It’s a central story-telling tool. And
it cements the emotional bond between the human player and the characters
in the game. Speech can also be the deciding factor in the player’s perception
of intelligence among the AI-controlled characters in a game.
At the Game Developer’s Conference (GDC) in 2002, Chris Butcher and
Jaime Griesemer of Bungie gave a talk entitled, “The Illusion of Intelligence:
The Integration of AI and Level Design in Halo” (http://bit.ly/1g7FbhD). In
their talk, they shared an anecdote about how important speech can be to com-
municating the motivations of an AI-driven character to the player. In Halo,
when the Elite leader of a squad of Covenant was killed, the grunts would all
run away in fear. In playtest after playtest, no one seemed to understand that
it was the killing of the Elite that had triggered the grunts to flee. Finally, the
grunts were given lines of dialog saying something to the effect of, “Leader
dead—run away!” Only then did play testers start to really grok what was
going on!
In this section, we’ll explore some of the fundamental subsystems you’ll
find in the character dialog system of pretty much any character-based game.
We’ll also discuss some of the specific techniques and technologies used by
Naughty Dog to create rich, realistic conversations in The Last of Us. For more
information and in-game videos of Naughty Dog’s character dialog system in
action, check out the talk I gave at GDC 2014 entitled, “Context-Aware Char-
acter Dialog in The Last of Us,” available in PDF and QuickTime formats at


<!-- source-pdf-page: 1017 -->

http://www.gameenginebook.com.

14.6.2.1
Giving a Character a Voice

It’s easy enough to give a game character a voice—simply play an appropriate
pre-recorded sound whenever the character needs to speak. However, things
are never quite that simple. The dialog system in a game engine is typically a
reasonably complex beast. Here are just a few of the reasons why:

•
We need a way to catalog all the possible lines of dialog that each char-
acter might be called upon to say, and give each of these lines some kind
of unique id so that the game can trigger them when needed.
•
We need to make sure that each uniquely identifiable character in the
game speaks with a recognizable and consistent voice. For example, each
of the hunters in the Pittsburgh section of The Last of Us was assigned
to one of eight unique voices, so that no two hunters in a battle would
sound the same.
•
We may not know a priori which character is going to be called upon
to say a specific line, so we often need to record the same line multiple
times, spoken by various voice actors, so that the appropriate voice can
be used to say the line when needed.
•
We also usually want a lot of variety in the things that are said. So most
dialog systems provide a means of selecting specific lines at random from
a pool of possibilities.
•
Speech audio assets tend to be of relatively long duration, which means
they occupy a lot of memory. Many lines of dialog are part of cinematic
sequences, and hence are only spoken once in the entire game. For these
reasons, it’s usually wasteful to store speech assets in memory. Instead,
it’s typical for speech audio assets to be streamed on demand (see Section
14.5.7.4).

Usually other vocal sounds, like the “efforts” made when lifting something
heavy, jumping over an obstacle or getting punched in the gut, are handled by
the same system that handles spoken dialog. This is done largely because a
character’s efforts need to match his or her spoken voice. So we may as well
leverage the dialog system to produce effort sounds as well.

14.6.2.2
Deﬁning a Line of Dialog

Most dialog systems introduce a level of indirection between a request to speak
and the choice of the particular audio clip to play. The game programmer or de-
signer requests logical lines of dialog, each of which is represented by a unique


<!-- source-pdf-page: 1018 -->

identifier such as a string, or better yet a hashed string id (see Section 6.4.3.1).
The sound designers can then “fill out” each logical line with one or more au-
dio clips in order to provide the necessary variety both in voice quality and in
terms of what exactly is said.
For example, let’s imagine a logical line in which the character says some-
thing to the effect of, “I’m out of ammo.” We’ll assign this logical dialog line
the unique id 'line-out-of-ammo, where the leading single quote indi-
cates a hashed string id. Let’s assume also that there are ten different char-
acters that might say this line: the player character (call him “drake”), the
player’s sidekick (call her “elena”) and up to eight enemy characters (call
them “pirate-a” through “pirate-h”). We’ll need some kind of data structure
to define all the physical audio assets that make up this one logical line of
dialog.
At Naughty Dog, sound designers use the Scheme programming language
to define logical dialog lines using a custom syntax. We’ll use a similar syntax
in our example below. However, the specifics of the implementation are not
important here. All we’re interested in is the structure of the data itself:

(define-dialog-line 'line-out-of-ammo
(character 'drake
(lines
drk-out-of-ammo-01
;; "Dammit, I'm out!"
drk-out-of-ammo-02
;; "Crap, need more bullets."
drk-out-of-ammo-03
;; "Oh, now I'm REALLY mad."
)
)
(character 'elena
(lines
eln-out-of-ammo-01
;; "Help, I'm out!"
eln-out-of-ammo-02
;; "Got any more bullets?"
)
)
(character 'pirate-a
(lines
pira-out-of-ammo-01
;; "I'm out!"
pira-out-of-ammo-02
;; "Need more ammo!"
;; ...
)
)
;; ...
(character 'pirate-h
(lines
pirh-out-of-ammo-01
;; "I'm out!"
pirh-out-of-ammo-02
;; "Need more ammo!"


<!-- source-pdf-page: 1019 -->

;; ...
)
)
)

Rather than define your dialog lines in one monolithic data structure like
the one shown above, it’s usually better to break the lines out into separate
files by character. For example, all of Drake’s lines can be managed in one file,
Elena’s in another file and all the pirates’ lines can be stored in a third file.
This helps to prevent the sound designers from stepping on each others’ toes.
It also means that we can manage our memory more efficiently. For example,
if there are no pirates in a given section of the game, there’s no need to keep
the data for the pirates’ dialog lines in memory. It’s also a good idea to split
the dialog data up by level, for the same reason.

14.6.2.3
Playing a Line of Dialog

Given this data, the dialog system can easily convert a request for a logical line
of dialog such as 'line-out-of-ammo into a specific audio clip. It simply
looks up the character’s specific voice id in the table, and then makes a random
choice amongst the various possible lines for that character.
It’s usually a good idea to put in place some kind of mechanism to ensure
that lines aren’t repeated too often. One way to accomplish this is to store the
indices of the various lines in an array and then randomly shuffle its contents.
To select a line, we simply cycle through the shuffled array in order. Once
all possible lines have been exhausted, we reshuffle the array, taking care that
the most recently played line doesn’t end up in the first slot. This prevents all
repetition while keeping the line selections sounding random.
Dialog line requests are typically made by gameplay code in C++, Java,
C# or whatever language your game is written in. Game designers may also
request lines of dialog via script (Lua, Python, etc.). The dialog system’s API
is usually designed with simplicity of use in mind. If an AI programmer or
game designer has to jump through a lot of hoops just to get a line of dialog
to play, you may discover that your characters are uncannily silent! It’s best
to provide a simple, fire-and-forget interface. Leave all the hard work to the
programmer who is crafting the dialog system.
For example, in Uncharted 3: Drake’s Deception, C++ code could request a
character to play a line of dialog by calling a simple PlayDialog() member
function of the Npc class. These calls would be peppered throughout the AI
decision-making code in order to trigger appropriate lines of dialog at key
moments in the game. For example:


<!-- source-pdf-page: 1020 -->

void Skill::OnEvent(const Event& evt)
{
Npc* pNpc = GetSelf(); // grab a pointer to the NPC

switch (evt.GetMessage())
{
case SID("player-seen"):
// play a line of dialog...
pNpc->PlayDialog(SID("line-player-seen"));
// ... and move to closest cover
pNpc->MoveTo(GetClosestCover());
break;
// ...
}

// ...
}

14.6.2.4
Priority and Interruption

What happens if a character is asked to speak while he’s already speaking?
What if he receives more than one speech command on the same frame? In
both cases, a priority system is a good way to resolve ambiguities.
To implement such a system, we simply assign each line of dialog to a pri-
ority level. When a request to say a line of dialog comes in, the system looks
at the priority of the currently playing line if any, and the priorities of the line
or lines that have been requested this frame. It finds the highest priority line
among these. If the currently playing line “wins,” it continues to play and
the requested lines are ignored. If one of the requested lines is higher priority
than the current line, or if the character isn’t speaking yet, the new line plays,
interrupting the current line if necessary.
Implementing the interruption of the speech itself is actually a bit tricky.
We can’t perform a cross-fade (i.e., fade the volume of the playing sound down
and the new sound up) because this sounds strange and wrong when applied
to the speech of a single character. Ideally, we’d want to play at least some
kind of glottal stop sound just prior to starting the new line. It might even be
appropriate to play a short phrase indicating that the character is surprised
and/or annoyed by the interruption, and then play the new line of dialog. The
dialog system in The Last of Us doesn’t do any of these fancy things. It simply
stops the current line and immediately plays the new one. This sounds pretty
good most of the time. Of course, each game has its own unique speech pat-
terns, and what works in one game may not work well in another. So as the
saying goes, “Your mileage may vary.”


<!-- source-pdf-page: 1021 -->

14.6.2.5
Conversations

In The Last of Us, Naughty Dog wanted the enemy NPCs to sound like they’re
having real conversations with one another. This meant that the characters
would need to be capable of saying relatively long chains of lines, with back-
and-forth banter between two or more characters. Likewise, in Uncharted 4: A
Thief’s End and Uncharted: The Lost Legacy, we wanted the characters to have
conversations while driving around Madagascar and India in their jeep. These
conversations could even be interrupted (for example by the player deciding
to exit the jeep to explore the area), and would continue where they left off
when the player returned to the vehicle.
Conversations in The Last of Us, Uncharted 4 and The Lost Legacy are con-
structed from logical segments. Each segment corresponds to one logical line,
spoken by one particular actor in the conversation. Each segment is given a
unique id, and the segments are chained together into a conversation via these
ids. As an example, let’s see how we would define the following conversation:

A:
“Hey, did you find anything?”

B:
“No, I’ve been looking for an hour and I ain’t found nothin’.”

A:
“Well then shut up and keep looking!”

This conversation could be expressed in the Naughty Dog conversation system
as follows:

(define-conversation-segment 'conv-searching-for-stuff-01
:rule []
:line 'line-did-you-find-anything
;; "Hey, did you find anything?"
:next-seg 'conv-searching-for-stuff-02
)
(define-conversation-segment 'conv-searching-for-stuff-02
:rule []
:line 'line-nope-not-yet
;; "I've been looking for an hour..."
:next-seg 'conv-searching-for-stuff-03
)
(define-conversation-segment 'conv-searching-for-stuff-03
:rule []
:line 'line-shut-up-keep-looking
;; "Well then shut up and keep looking!"
)

This syntax might seem a bit verbose at first glace. But as we’ll see in Sec-
tion 14.6.2.8, breaking the conversation out like this gives us a great deal of


<!-- source-pdf-page: 1022 -->

flexibility. For example, it allows branching conversations to be defined in a
natural and reasonably convenient way.

14.6.2.6
Interrupting Conversations

We saw in Section 14.6.2.4 that a simple priority system can be used to han-
dle interruptions and to resolve contention when more than one logical line is
requested simultaneously.
When conversations are in play, a priority system can still be used. But its
implementation is a bit more complex in this case. For example, imagine a
conversation between characters A and B. A says his line, then B says her line
while A waits his turn. During the time that B is speaking, A is asked to play
an entirely different line of dialog. He’s not tehnically speaking, so by the rules
for dialog prioritization, applied to each character individually, there would
be no problem and the line would play. But this could sound very jarring,
depending on what’s being said.

A:
“Hey, did you find anything?”

B:
“No, I’ve been looking for an hour and…”

A:
“Look, a shiny object!”
(interruption by an unrelated line of dialog)

B:
“…I ain’t found nothin’.”

To overcome this issue on The Last of Us, we introduced the concept of con-
versations as “first-class entities.” When a conversation is started, the sys-
tem “knows” that each of the characters is involved in that conversation, even
when he or she isn’t speaking. Each conversation has a priority, and the prior-
itization rules are applied to entire conversations, not the individual lines on a
per-character basis. So for example, when charater A is asked to say, “Look, a
shiny object!” the system knows that he is currently involved in the “Hey, did
you find anything?” conversation. Persumably the line “Look, a shiny object!”
is at the same or lower priority as the current conversation, so the interruption
isn’t allowed.
If the interrupting line is something higher priority like, “Holy cow, he’s
pointing a gun at us!” then the line is allowed to interrupt the existing conver-
sation. In that case, all of the characters in the conversation are interrupted.
The result is an interruption that sounds natural and intelligent.

A:
“Hey, did you find anything?”

B:
“No, I’ve been looking for an hour and…”


<!-- source-pdf-page: 1023 -->

A:
“Holy cow, he’s pointing a gun at us!”
(interruption by a higher-priority conversation)

B:
“Get him!”
(The original conversation is interrupted by the new one, and A and B go into
combat mode.)

14.6.2.7
Exclusivity

On The Last of Us, we also introduced the concept of exclusivity. Any line of
dialog or conversation can be marked as either non-exclusive, faction-exclusive
or globally exclusive. This mark-up controls how interruptions work for the
given line or conversation.

•
A non-exclusive line or conversation is permitted to play over top of other
lines or conversations. For example, during a search for the player, it’s
not a big problem if one hunter is mumbling to himself, “Huh, there’s
nothing over here.” while another hunter is saying, “I’m getting tired
of this.” The two hunters aren’t speaking to each other, so the overlap
sounds perfectly natural.

•
A faction-exclusive line or conversation interrupts all other lines or con-
versations within that character’s faction. For example, if the player
(Joel) is spotted during a search, the hunter that saw him might say,
“He’s over here!” The other hunters should immediately stop speak-
ing, because we want to make it seem as if the hunters can hear one an-
other, and also to communicate to the player that their collective focus
has shifted. However, if Joel’s companion Ellie is whispering a warn-
ing to him at the time, we probably do not want to interrupt her. She is
not part of the hunter gang, and what she has to say to Joel is relevant
whether or not the hunters have spotted him.

•
A globally exclusive line or conversation interrupts all other lines, across
faction boundaries. This is useful in situations in which every character
within earshot should react to hearing whatever is being said.

14.6.2.8
Choices and Branching Conversations

It’s often desirable to allow conversations to play out in different ways de-
pending on what the player does, on what decisions the AI characters make
and/or on other aspects of game world state. When authoring or editing con-
versations, the writers and sound designers would like to have control not
only over which lines are said, but also over the logical conditions that control
which branch of the conversation will be taken at any given moment during


<!-- source-pdf-page: 1024 -->

gameplay. This puts the creative power in the hands of the people who need
it, instead of forcing them to work through a programmer.
Naughty Dog implemented such a system for use on The Last of Us. It was
inspired in part by an earlier system developed by Valve and described by
Elan Ruskin in his talk, “Rule Databased for Contextual Dialog and Game
Logic,” which he delivered at the Game Developer’s Conference in 2012. The
talk is available here: http://www.gdcvault.com/play/1015317/AI-driven
-Dynamic-Dialog-through. Naughty Dog’s conversation system differs from
Valve’s in a number of significant ways, but the core idea behind both systems
is similar. We’ll describe the Naughty Dog system here, since that’s the system
with which the author has the most experience.
In Naughty Dog’s conversation system, each segment of a conversation
can consist of one or more alternative lines of dialog. Each alternative within
the segment carries with it a selection rule. If the rule evaluates to true, that
alternative is selected; if the rule evaluates to false, the alternative is ignored.
A rule is comprised of one or more criteria. Each criterion is a simple logi-
cal expression that evaluates to a Boolean. The expressions ('health > 5)
and ('player-death-count == 1) are examples of criteria. If more than
one criterion is provided within a rule, they are logically combined using the
Boolean AND operator. A rule only evaluates to true when all of its criteria
evaluate to true.
Here’s an example of one segment of a conversation, with three alternatives
that depend upon the health of the character doing the talking:

(define-conversation-segment 'conv-player-hit-by-bullet
(
:rule [ ('health < 25) ]
:line 'line-i-need-a-doctor
;; "I'm bleeding bad... need a doctor!"
)
(
:rule [ ('health < 75) ]
:line 'line-im-in-trouble
;; "Now I'm in real trouble."
)
(
:rule [ ] ;; no criteria acts as an "else" case
:line 'line-that-was-close
;; "Ah! Can't let that happen again!"
)
)


<!-- source-pdf-page: 1025 -->

Branching Dialog

By breaking a conversation into segments, each of which contains one or more
alternative lines, we open up the possibility of crafting branching conversa-
tions. For example, let’s consider a conversation in which Ellie (the player’s
companion in The Last of Us) asks Joel (the player character) if he’s all right
when he’s been shot at. If the player wasn’t actually hit by the bullet, the con-
versation goes like this:

Ellie:
“Are you OK?”

Joel:
“Yeah, I’m fine.”

Ellie:
“Geez. Keep your head down!”

If Joel has been hit, the conversation plays out differently:

Ellie:
“Are you OK?”

Joel:
“(panting) Not exactly.”

Ellie:
“You’re bleeding!”

We can express this branching conversation using the conversation syntax de-
scribed above:

(define-conversation-segment 'conv-shot-at--start
(
:rule [ ]
:line 'line-are-you-ok ;; "Are you OK?"
:next-seg 'conv-shot-at--health-check
:next-speaker 'listener ;; *** see comments below
)
)

(define-conversation-segment 'conv-shot-at--health-check
(
:rule [ (('speaker 'shot-recently) == false) ]
:line 'line-yeah-im-fine ;; "Yeah, I'm fine."
:next-seg 'conv-shot-at--not-hit
:next-speaker 'listener ;; *** see comments below
)
(
:rule [ (('speaker 'shot-recently) == true) ]
:line 'line-not-exactly ;; "(panting) Not exactly."
:next-seg 'conv-shot-at--hit
:next-speaker 'listener ;; *** see comments below
)
)


<!-- source-pdf-page: 1026 -->

(define-conversation-segment 'conv-shot-at--not-hit
(
:rule [ ]
:line 'line-keep-head-down ;; "Geez. Keep your head down!"
)
)

(define-conversation-segment 'conv-shot-at--hit
(
:rule [ ]
:line 'line-youre-bleeding ;; "You're bleeding!"
)
)

Speaker and Listener

There’s a subtle aspect to what’s going on in the branching converstion above.
At any given moment in a two-person conversation, one person is the speaker
and the other is the listener. The roles of speaker and listener ping-pong back
and forth as the conversation progresses. In the first segment of the conver-
sation, 'conv-shot-at--start, Ellie is the speaker and Joel is the listener.
When we chain to the next segment, 'conv-shot-at--health-check, we
specify the value 'listener for the field :next-speaker. This tells the
system to use the current listener (Joel) as the next segment’s speaker, thereby
reversing the roles. In that segment, we check whether the speaker has been
shot recently via the criteria (('speaker 'shot-recently) == false)
and (('speaker 'shot-recently) == true).
But now Joel is the
speaker, so everything works out as we’d expect.
An abstract speaker/listener system doesn’t seem all that useful for a con-
versation between two principal characters like Joel and Ellie. But by keeping
the definition of the conversation abstract, we gain a significant amount of
flexibility. For one thing, we could use the same conversation specification
to define a conversation in which Joel asks Ellie if she’s OK. This works be-
cause the entire conversation is defined in a way that is independent of which
character is saying each line. Moreover, for enemy characters it’s absolutely
essential that conversations be defined in a generic manner, because we don’t
know which specific characters will be doing the speaking a priori. For enemy
battle chatter, we typically select a pair of characters dynamically and fire off
the conversation. It has to work, no matter which two characters are selected.
The speaker/listener system can be extended to two- or three-person con-
versations. The Naughty Dog conversation system supported up to three lis-
teners, although the vast majority of our conversations were between only


<!-- source-pdf-page: 1027 -->
> Visual fallback for diagrams/images: [PDF page 1027](../../../visual_pages/page_1027.jpg)

two characters.

Fact Dictionaries

The criteria within a rule reference symbolic quantities like 'health and
'player-death-count. These symbolic quantities are implemented under
the hood as entries in a dictionary data structure—basically a table containing
key-value pairs. We call these fact dictionaries. An example of a fact dictionary
is shown in Table 14.1.

You may have noticed in Table 14.1 that each value in the dictionary has an
associated data type. In other words, the values in the dictionary are variants.
A variant is a data object that is capable of holding values of various types,
much like a union in C or C++. However, unlike a union, a variant also
stores information about the type of data it currently contains. This allows us
to validate the type of a value prior to using it. It also lets us convert data from
one type to another. For example, if our variant holds the integer value 42,
we could ask the variant to return it to us as the floating-point value 42.0f
instead.

In The Last of Us, each character has its own fact dictionary containing facts
about the character itself like health, weapon type, awareness level and so on.
Each “faction” of characters also has a fact dictionary. This allows us to ex-
press facts about the faction as a whole, like how many characters remain
alive within the group. Finally, there is a singleton “global” fact dictionary
that contains information about the game as a whole, without respect to fac-
tion. Things like the amount of time spent playing, the name of the current
level or how many times the player has retried a particular task are all things
that can go into the global fact dictionary.

Criterion Syntax

When writing a criterion, the syntax allows for facts to be pulled from any
dictionary by name. For example, (('self 'health) > 5) tells the sys-

Key
Value
Data Type
'name
'ellie
StringId
'faction
'buddy
StringId
'health
82
int
'is-joels-friend
true
bool
…
…
…

Table 14.1. An example of a fact dictionary.


<!-- source-pdf-page: 1028 -->
> Visual fallback for diagrams/images: [PDF page 1028](../../../visual_pages/page_1028.jpg)

tem to grab the fact dictionary of the character itself, look up the value of the
'health fact in that dictionary and then check if it is greater than 5. Likewise,
(('global 'seconds-playing) <= 23.5) instructs the system to look
up the 'seconds-playing fact from the global fact dictionary, and check
that it is less than or equal to 23.5 seconds.
If the user doesn’t specify a dictionary explicity, as in ('health > 5), the
system searches for the named fact by following a predefined search order.
Check the character’s fact dictionary first. If that fails, try to find it in the
dictionary that matches the character’s faction. Finally, if all else fails, look
for the fact in the global dictionary. This “search path” feature allows sound
designers to be as brief as possible when writing criteria (albeit with the loss
of some specificity and clarity in the rules).

14.6.2.9
Context-Sensitive Dialog

In The Last of Us, we wanted to have enemy characters call out the location of
the player in an intelligent way. If the player is hiding in a store, the enemies
should shout, “He’s in the store!” If he’s hiding behind a car, we want the bad
guys to say, “He’s behind that car!” This makes the characters sound incredi-
bly intelligent, yet it turns out to be a relatively simple thing to implement.
To make this work, the sound designers mark up our game worlds with
regions. Each region is tagged with one of two kinds of location tags. A specific
tag marks the region with a very specifc location like “behind the counter”
or “by the cash register.” A general tag marks the region with a more general
location like “in the store” or “in the street.”
To determine which line of dialog to play, the system determines within
which region the player is located, and within which region the enemy NPC
is located. If they are both in the same general region, the player’s specific tag is
used to select dialog lines. When the NPC and player reside in different general
regions, we fall back to using the player’s general region tag to select the line.
So if the enemy and the player are both in the store, we might select a line like,
“He’s by the window!” But if the NPC is in the store and the player is out in
the street, we might hear the NPC say, “He’s out in the street! Get him!” See
Figure 14.42 for an illustration of how this system works.
This very simple system proved incredibly powerful. It was difficult to set
up due to the sheer number of combinations of lines that had to be recorded
and configured, but the final result in-game was worth the effort.

14.6.2.10
Dialog Actions

Lines of dialog delivered without body language usually look uncanny and
unrealistic. Some dialog lines are delivered as part of a full-body animation—
an in-game cinematic for example. But some lines must be delivered while the


<!-- source-pdf-page: 1029 -->
> Visual fallback for diagrams/images: [PDF page 1029](../../../visual_pages/page_1029.jpg)

general: street

general: garage

specific: car

specific:
cabinet

“He’s by
that tree!”

NPC1

general: store

specific: counter

specific: tree

“He’s out in
the street!”

Player

specific: soda

machine

NPC2

Figure 14.42. General and speciﬁc regions for context-sensitive dialog line selection.

character is busy doing something else, like walking, running or firing their
weapon. Ideally we’d like to spice up such lines of dialog with some gestures
to breathe life into them.
On The Last of Us, we implemented a gesture system using additive anima-
tion technology (see Section 12.6.5). These gestures could be explicitly called
out by C++ code or script. In addition, each line of dialog could have a script
associated with it whose timeline was synchronized with the audio. This al-
lowed us to trigger gestures at precise moments during key lines of dialog.

### 14.6.3 Music

Music is an incredibly important aspect of pretty much any good game. It
sets the tone, drives the player’s sense of tension, and can make (or break) an
emotional scene. A game engine’s music system is typically charged with the
following duties:

•
Provide the ability to play back music tracks as streaming audio clips
(because music clips are almost always too large to fit in memory).
•
Provide musical variety.
•
Match the music to the events occurring in the game.
•
Seamlessly transition from one piece of music to the next.


<!-- source-pdf-page: 1030 -->

•
Mix the audio with the other sounds in the game in a suitable and pleas-
ing manner.

•
Allow music to be temporarily ducked to enhance the audibility of spe-
cific sounds or conversations in-game.
•
Permit brief pieces of music or sound effects known as stingers to tem-
porarily interrupt the currently playing music track.

•
Allow music to be paused and restarted. (You don’t need a full orchestra
playing a grandiose theme during every single second of gameplay, you
know!)

We generally expect the music to change to match the changing levels of
tension and/or emotional moods of the events happening in the game. One
way to accomplish this is to create multiple playlists, each of which is intended
for a different level of tension or emotional mood in the game. Each playlist
contains one or more pieces of music, from which selections may be made
randomly or sequentially. As the tension and mood change in the game—as
battles begin and end, touching cutscenes come and go and so on—the mu-
sic system detects these changes and selects new music playlists as appropri-
ate. Some games implement a “stack” of music selections at increasing ten-
sion levels—calm music for when no enemies are around, tense music when
the player is approaching an unsuspecting group of enemies, startling music
at first contact and fast-paced music during battle.
Stingers are another way to match the music to the events in the game. A
stinger is a short musical clip or sound effect that can temporarily interrupt
the currently playing music track, or play over top of it while the main track’s
volume is ducked down. For example, the first time the player makes line-
of-sight with a new enemy, we might want to play an ominous “rumbling”
sound to give the player a cue that danger is near. Or when the player dies,
we may want to quickly switch to a snippet of “death music.” Both of these
are situations in which a stinger might be used.
Transitioning smoothly between different music streams is somewhat of a
challenge. We cannot blindly cross-blend between totally unrelated pieces of
music and expect it to always sound good. The tempos of the two pieces may
not match, and the “beat” of one piece of music might not line up with that of
the next. The key is to time each transition properly. A rapid cross-fade can
be useful if the tempos don’t match; a longer cross-fade might work well if the
tempos are nearly identical. This takes some trial and error to get right. Even
getting a piece of music to loop properly requires some tweaking by the sound
engineer.
The topic of game music is a broad one, and we can’t really do it justice
here. If you are interested in learning more, [45] is a great book to start with.


<!-- source-pdf-page: 1031 -->
> Visual fallback for diagrams/images: [PDF page 1031](../../../visual_pages/page_1031.jpg)

Taylor & Francis

Taylor & Francis Group
http://taylorandfrancis.com


<!-- source-pdf-page: 1032 -->
> Visual fallback for diagrams/images: [PDF page 1032](../../../visual_pages/page_1032.jpg)

Part IV
Gameplay


<!-- source-pdf-page: 1033 -->
> Visual fallback for diagrams/images: [PDF page 1033](../../../visual_pages/page_1033.jpg)

Taylor & Francis

Taylor & Francis Group
http://taylorandfrancis.com
