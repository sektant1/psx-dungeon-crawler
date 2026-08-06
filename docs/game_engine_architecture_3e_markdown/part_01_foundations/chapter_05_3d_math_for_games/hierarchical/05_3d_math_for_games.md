# 5 3D Math for Games

> Source PDF pages: 378-435
> Extraction mode: PyMuPDF text blocks; line breaks and printed hyphenation are preserved.

<!-- source-pdf-page: 378 -->

A
game is a mathematical model of a virtual world simulated in real time
on a computer of some kind. Therefore, mathematics pervades every-
thing we do in the game industry. Game programmers make use of virtu-
ally all branches of mathematics, from trigonometry to algebra to statistics
to calculus. However, by far the most prevalent kind of mathematics you’ll
be doing as a game programmer is 3D vector and matrix math (i.e., 3D linear
algebra).
Even this one branch of mathematics is very broad and very deep, so we
cannot hope to cover it in any great depth in a single chapter. Instead, I will
attempt to provide an overview of the mathematical tools needed by a typi-
cal game programmer. Along the way, I’ll offer some tips and tricks, which
should help you keep all of the rather confusing concepts and rules straight in
your head. For an excellent in-depth coverage of 3D math for games, I highly
recommend Eric Lengyel’s book [32] on the topic. Chapter 3 of Christer Eric-
son’s book [14] on real-time collision detection is also an excellent resource.

## 5.1 Solving 3D Problems in 2D

Many of the mathematical operations we’re going to learn about in the follow-
ing chapter work equally well in 2D and 3D. This is very good news, because


<!-- source-pdf-page: 379 -->
> Visual fallback for diagrams/images: [PDF page 379](../../../visual_pages/page_0379.jpg)

it means you can sometimes solve a 3D vector problem by thinking and draw-
ing pictures in 2D (which is considerably easier to do!) Sadly, this equivalence
between 2D and 3D does not hold all the time. Some operations, like the cross
product, are only defined in 3D, and some problems only make sense when all
three dimensions are considered. Nonetheless, it almost never hurts to start by
thinking about a simplified two-dimensional version of the problem at hand.
Once you understand the solution in 2D, you can think about how the prob-
lem extends into three dimensions. In some cases, you’ll happily discover that
your 2D result works in 3D as well. In others, you’ll be able to find a coor-
dinate system in which the problem really is two-dimensional. In this book,
we’ll employ two-dimensional diagrams wherever the distinction between 2D
and 3D is not relevant.

## 5.2 Points and Vectors

The majority of modern 3D games are made up of three-dimensional objects
in a virtual world. A game engine needs to keep track of the positions, ori-
entations and scales of all these objects, animate them in the game world, and
transform them into screen space so they can be rendered on screen. In games,
3D objects are almost always made up of triangles, the vertices of which are
represented by points. So, before we learn how to represent whole objects in
a game engine, let’s first take a look at the point and its closely related cousin,
the vector.

y

z
x

### 5.2.1 Points and Cartesian Coordinates

Figure 5.1.
A point
represented in Car-
tesian coordinates.

Technically speaking, a point is a location in n-dimensional space. (In games, n
is usually equal to 2 or 3.) The Cartesian coordinate system is by far the most
common coordinate system employed by game programmers. It uses two or
three mutually perpendicular axes to specify a position in 2D or 3D space. So, a
point P is represented by a pair or triple of real numbers, (Px, Py) or (Px, Py, Pz)
(see Figure 5.1).
Of course, the Cartesian coordinate system is not our only choice. Some
other common systems include:

h

r

•
Cylindrical coordinates. This system employs a vertical “height” axis h, a
radial axis r emanating out from the vertical, and a yaw angle theta (θ). In
cylindrical coordinates, a point P is represented by the triple of numbers
(Ph, Pr, Pθ). This is illustrated in Figure 5.2.

Figure 5.2.
A point
represented in cylin-
drical coordinates.


<!-- source-pdf-page: 380 -->
> Visual fallback for diagrams/images: [PDF page 380](../../../visual_pages/page_0380.jpg)

•
Spherical coordinates. This system employs a pitch angle phi (ϕ), a yaw
angle theta (θ) and a radial measurement r. Points are therefore rep-
resented by the triple of numbers (Pr, Pϕ, Pθ).
This is illustrated in
Figure 5.3.

Cartesian coordinates are by far the most widely used coordinate system
in game programming. However, always remember to select the coordinate
system that best maps to the problem at hand. For example, in the game Crank
the Weasel by Midway Home Entertainment, the main character Crank runs
around an art-deco city picking up loot. I wanted to make the items of loot
swirl around Crank’s body in a spiral, getting closer and closer to him until
they disappeared. I represented the position of the loot in cylindrical coor-
dinates relative to the Crank character’s current position. To implement the
spiral animation, I simply gave the loot a constant angular speed in θ, a small
constant linear speed inward along its radial axis r and a very slight constant
linear speed upward along the h-axis so the loot would gradually rise up to
the level of Crank’s pants pockets. This extremely simple animation looked
great, and it was much easier to model using cylindrical coordinates than it
would have been using a Cartesian system.

r

Figure 5.3.
A point
represented
in
spherical
coordi-
nates.

### 5.2.2 Left-Handed versus Right-Handed Coordinate Systems

In three-dimensional Cartesian coordinates, we have two choices when ar-
ranging our three mutually perpendicular axes: right-handed (RH) and left-
handed (LH). In a right-handed coordinate system, when you curl the fingers
of your right hand around the z-axis with the thumb pointing toward posi-
tive z coordinates, your fingers point from the x-axis toward the y-axis. In a
left-handed coordinate system the same thing is true using your left hand.
The only difference between a left-handed coordinate system and a right-

y

y

z

x

x
z

Left-Handed

Right-Handed

Figure 5.4. Left- and right-handed Cartesian coordinate systems.


<!-- source-pdf-page: 381 -->
> Visual fallback for diagrams/images: [PDF page 381](../../../visual_pages/page_0381.jpg)

handed coordinate system is the direction in which one of the three axes is
pointing. For example, if the y-axis points upward and x points to the right,
then z comes toward us (out of the page) in a right-handed system, and away
from us (into the page) in a left-handed system. Left- and right-handed Carte-
sian coordinate systems are depicted in Figure 5.4.
It is easy to convert from left-handed to right-handed coordinates and vice
versa. We simply flip the direction of any one axis, leaving the other two axes
alone. It’s important to remember that the rules of mathematics do not change
between left-handed and right-handed coordinate systems. Only our interpre-
tation of the numbers—our mental image of how the numbers map into 3D
space—changes. Left-handed and right-handed conventions apply to visual-
ization only, not to the underlying mathematics. (Actually, handedness does
matter when dealing with cross products in physical simulations, because a
cross product is not actually a vector—it’s a special mathematical object known
as a pseudovector. We’ll discuss pseudovectors in a little more depth in Section
5.2.4.9.)
The mapping between the numerical representation and the visual repre-
sentation is entirely up to us as mathematicians and programmers. We could
choose to have the y-axis pointing up, with z forward and x to the left (RH)
or right (LH). Or we could choose to have the z-axis point up. Or the x-axis
could point up instead—or down. All that matters is that we decide upon a
mapping, and then stick with it consistently.
That being said, some conventions do tend to work better than others for
certain applications. For example, 3D graphics programmers typically work
with a left-handed coordinate system, with the y-axis pointing up, x to the
right and positive z pointing away from the viewer (i.e., in the direction the
virtual camera is pointing). When 3D graphics are rendered onto a 2D screen
using this particular coordinate system, increasing z-coordinates correspond
to increasing depth into the scene (i.e., increasing distance away from the vir-
tual camera). As we will see in subsequent chapters, this is exactly what is
required when using a z-buffering scheme for depth occlusion.

### 5.2.3 Vectors

A vector is a quantity that has both a magnitude and a direction in n-dimensional
space. A vector can be visualized as a directed line segment extending from a
point called the tail to a point called the head. Contrast this to a scalar (i.e.,
an ordinary real-valued number), which represents a magnitude but has no
direction. Usually scalars are written in italics (e.g., v) while vectors are written
in boldface (e.g., v).
A 3D vector can be represented by a triple of scalars (x, y, z), just as a point


<!-- source-pdf-page: 382 -->

can be. The distinction between points and vectors is actually quite subtle.
Technically, a vector is just an offset relative to some known point. A vector
can be moved anywhere in 3D space—as long as its magnitude and direction
don’t change, it is the same vector.
A vector can be used to represent a point, provided that we fix the tail of
the vector to the origin of our coordinate system. Such a vector is sometimes
called a position vector or radius vector. For our purposes, we can interpret any
triple of scalars as either a point or a vector, provided that we remember that a
position vector is constrained such that its tail remains at the origin of the chosen
coordinate system. This implies that points and vectors are treated in subtly
different ways mathematically. One might say that points are absolute, while
vectors are relative.
The vast majority of game programmers use the term “vector” to refer both
to points (position vectors) and to vectors in the strict linear algebra sense
(purely directional vectors). Most 3D math libraries also use the term “vec-
tor” in this way. In this book, we’ll use the term “direction vector” or just
“direction” when the distinction is important. Be careful to always keep the
difference between points and directions clear in your mind (even if your math
library doesn’t). As we’ll see in Section 5.3.6.1, directions need to be treated
differently from points when converting them into homogeneous coordinates
for manipulation with 4 × 4 matrices, so getting the two types of vector mixed
up can and will lead to bugs in your code.

5.2.3.1
Cartesian Basis Vectors

It is often useful to define three orthogonal unit vectors (i.e., vectors that are
mutually perpendicular and each with a length equal to one), corresponding to
the three principal Cartesian axes. The unit vector along the x-axis is typically
called i, the y-axis unit vector is called j, and the z-axis unit vector is called k.
The vectors i, j and k are sometimes called Cartesian basis vectors.
Any point or vector can be expressed as a sum of scalars (real numbers)
multiplied by these unit basis vectors. For example,

(5, 3, −2) = 5i + 3j −2k.

### 5.2.4 Vector Operations

Most of the mathematical operations that you can perform on scalars can be
applied to vectors as well. There are also some new operations that apply only
to vectors.


<!-- source-pdf-page: 383 -->
> Visual fallback for diagrams/images: [PDF page 383](../../../visual_pages/page_0383.jpg)

v

v
2v

Figure 5.5. Multiplication of a vector by the scalar 2.

5.2.4.1
Multiplication by a Scalar

Multiplication of a vector a by a scalar s is accomplished by multiplying the
individual components of a by s:

sa = (sax, say, saz).

Multiplication by a scalar has the effect of scaling the magnitude of the
vector, while leaving its direction unchanged, as shown in Figure 5.5. Multi-
plication by −1 flips the direction of the vector (the head becomes the tail and
vice versa).
The scale factor can be different along each axis. We call this nonuniform
scale, and it can be represented as the component-wise product of a scaling vector
s and the vector in question, which we’ll denote with the ⊗operator. Techni-
cally speaking, this special kind of product between two vectors is known as
the Hadamard product. It is rarely used in the game industry—in fact, nonuni-
form scaling is one of its only commonplace uses in games:

s ⊗a = (sxax, syay, szaz).
(5.1)

As we’ll see in Section 5.3.7.3, a scaling vector s is really just a compact
way to represent a 3 × 3 diagonal scaling matrix S. So another way to write
Equation (5.1) is as follows:

aS =
[ax
ay
az
]





sx
0
0
0
sy
0
0
0
sz

=
[sxax
syay
szaz
]
.

We’ll explore matrices in more depth in Section 5.3.

5.2.4.2
Addition and Subtraction

The addition of two vectors a and b is defined as the vector whose components
are the sums of the components of a and b. This can be visualized by placing


<!-- source-pdf-page: 384 -->
> Visual fallback for diagrams/images: [PDF page 384](../../../visual_pages/page_0384.jpg)

–b
b

y

a
a – b

a + b

x

Figure 5.6. Vector addition and subtraction.

Figure 5.7. Magnitude of a vector (shown in
2D for ease of illustration).

the head of vector a onto the tail of vector b—the sum is then the vector from
the tail of a to the head of b (see also Figure 5.6):

a + b =
[(ax + bx), (ay + by), (az + bz)]
.

Vector subtraction a −b is nothing more than addition of a and −b (i.e., the
result of scaling b by −1, which flips it around). This corresponds to the vector
whose components are the difference between the components of a and the
components of b:

a −b =
[(ax −bx), (ay −by), (az −bz)]
.

Vector addition and subtraction are depicted in Figure 5.6.

Adding and Subtracting Points and Directions

You can add and subtract direction vectors freely. However, technically speak-
ing, points cannot be added to one another—you can only add a direction vec-
tor to a point, the result of which is another point. Likewise, you can take the
difference between two points, resulting in a direction vector. These opera-
tions are summarized below:

•
direction + direction = direction
•
direction – direction = direction
•
point + direction = point
•
point – point = direction
•
point + point = nonsense

5.2.4.3
Magnitude

The magnitude of a vector is a scalar representing the length of the vector as
it would be measured in 2D or 3D space. It is denoted by placing vertical bars


<!-- source-pdf-page: 385 -->
> Visual fallback for diagrams/images: [PDF page 385](../../../visual_pages/page_0385.jpg)

around the vector’s boldface symbol. We can use the Pythagorean theorem to
calculate a vector’s magnitude, as shown in Figure 5.7:

|a| =
√

a2x + a2y + a2z.

5.2.4.4
Vector Operations in Action

Believe it or not, we can already solve all sorts of real-world game problems
given just the vector operations we’ve learned thus far. When trying to solve a
problem, we can use operations like addition, subtraction, scaling and magni-
tude to generate new data out of the things we already know. For example, if
we have the current position vector of an AI character P1, and a vector v repre-
senting her current velocity, we can find her position on the next frame P2 by
scaling the velocity vector by the frame time interval ∆t, and then adding it to
the current position. As shown in Figure 5.8, the resulting vector equation is
P2 = P1 + v ∆t. (This is known as explicit Euler integration—it’s actually only
valid when the velocity is constant, but you get the idea.)

2

1

Figure 5.8. Simple vec-
tor addition can be
used to ﬁnd a char-
acter’s position in the
next frame, given her
position and velocity
in the current frame.
As another example, let’s say we have two spheres, and we want to know
whether they intersect.
Given that we know the center points of the two
spheres, C1 and C2, we can find a direction vector between them by simply
subtracting the points, d = C2 −C1. The magnitude of this vector d = |d|
determines how far apart the spheres’ centers are. If this distance is less than
the sum of the spheres’ radii, they are intersecting; otherwise they’re not. This
is shown in Figure 5.9.
Square roots are expensive to calculate on most computers, so game pro-
grammers should always use the squared magnitude whenever it is valid to do

1r

2r

d

1

2 –
1

2

y

x

Figure 5.9. A sphere-sphere intersection test involves only vector subtraction, vector magnitude
and ﬂoating-point comparison operations.


<!-- source-pdf-page: 386 -->

so:
|a|2 = (a2
x + a2
y + a2
z).

Using the squared magnitude is valid when comparing the relative lengths
of two vectors (“is vector a longer than vector b?”), or when comparing a vec-
tor’s magnitude to some other (squared) scalar quantity. So in our sphere-
sphere intersection test, we should calculate d2 = |d|2 and compare this to
the squared sum of the radii, (r1 + r2)2 for maximum speed. When writing
high-performance software, never take a square root when you don’t have to!

5.2.4.5
Normalization and Unit Vectors

A unit vector is a vector with a magnitude (length) of one. Unit vectors are
very useful in 3D mathematics and game programming, for reasons we’ll see
below.
Given an arbitrary vector v of length v = |v|, we can convert it to a unit
vector u that points in the same direction as v, but has unit length. To do
this, we simply multiply v by the reciprocal of its magnitude. We call this
normalization:

u = v

|v| = 1

vv.

5.2.4.6
Normal Vectors

A vector is said to be normal to a surface if it is perpendicular to that surface.
Normal vectors are highly useful in games and computer graphics. For exam-
ple, a plane can be defined by a point and a normal vector. And in 3D graphics,
lighting calculations make heavy use of normal vectors to define the direction
of surfaces relative to the direction of the light rays impinging upon them.
Normal vectors are usually of unit length, but they do not need to be. Be
careful not to confuse the term “normalization” with the term “normal vector.”
A normalized vector is any vector of unit length. A normal vector is any vector
that is perpendicular to a surface, whether or not it is of unit length.

5.2.4.7
Dot Product and Projection

Vectors can be multiplied, but unlike scalars there are a number of different
kinds of vector multiplication. In game programming, we most often work
with the following two kinds of multiplication:

•
the dot product (a.k.a. scalar product or inner product), and

•
the cross product (a.k.a. vector product or outer product).


<!-- source-pdf-page: 387 -->
> Visual fallback for diagrams/images: [PDF page 387](../../../visual_pages/page_0387.jpg)

The dot product of two vectors yields a scalar; it is defined by adding the
products of the individual components of the two vectors:

a · b = axbx + ayby + azbz = d
(a scalar).

The dot product can also be written as the product of the magnitudes of the
two vectors and the cosine of the angle between them:

a · b = |a| |b| cos θ.

The dot product is commutative (i.e., the order of the two vectors can be
reversed) and distributive over addition:

a · b = b · a;
a · (b + c) = a · b + a · c.

And the dot product combines with scalar multiplication as follows:

sa · b = a · sb = s(a · b).

Vector Projection

If u is a unit vector (|u| = 1), then the dot product (a · u) represents the length
of the projection of vector a onto the infinite line defined by the direction of u,
as shown in Figure 5.10. This projection concept works equally well in 2D or
3D and is highly useful for solving a wide variety of three-dimensional prob-
lems.

a

u

a

u

Figure 5.10. Vector projection using the dot product.

Magnitude as a Dot Product

The squared magnitude of a vector can be found by taking the dot product of


<!-- source-pdf-page: 388 -->
> Visual fallback for diagrams/images: [PDF page 388](../../../visual_pages/page_0388.jpg)

that vector with itself. Its magnitude is then easily found by taking the square
root:

|a|2 = a · a;
|a| = √a · a.

This works because the cosine of zero degrees is 1, so |a| |a| cos θ = |a| |a| =
|a|2.

Dot Product Tests

Dot products are great for testing if two vectors are collinear or perpendicular,
or whether they point in roughly the same or roughly opposite directions. For
any two arbitrary vectors a and b, game programmers often use the following
tests, as shown in Figure 5.11:

•
Collinear. (a · b) = |a| |b| = ab (i.e., the angle between them is exactly 0
degrees—this dot product equals +1 when a and b are unit vectors).
•
Collinear but opposite. (a · b) = −ab (i.e., the angle between them is 180
degrees—this dot product equals −1 when a and b are unit vectors).
•
Perpendicular. (a · b) = 0 (i.e., the angle between them is 90 degrees).
•
Same direction. (a · b) > 0 (i.e., the angle between them is less than 90
degrees).
•
Opposite directions. (a · b) < 0 (i.e., the angle between them is greater
than 90 degrees).

Some Other Applications of the Dot Product

Dot products can be used for all sorts of things in game programming. For
example, let’s say we want to find out whether an enemy is in front of the
player character or behind him. We can find a vector from the player’s position
P to the enemy’s position E by simple vector subtraction (v = E −P). Let’s
assume we have a vector f pointing in the direction that the player is facing.
(As we’ll see in Section 5.3.10.3, the vector f can be extracted directly from the
player’s model-to-world matrix.) The dot product d = v · f can be used to test
whether the enemy is in front of or behind the player—it will be positive when
the enemy is in front and negative when the enemy is behind.
The dot product can also be used to find the height of a point above or
below a plane (which might be useful when writing a moon-landing game for
example). We can define a plane with two vector quantities: a point Q lying
anywhere on the plane, and a unit vector n that is perpendicular (i.e., normal)


<!-- source-pdf-page: 389 -->
> Visual fallback for diagrams/images: [PDF page 389](../../../visual_pages/page_0389.jpg)

a
b

(a · b) = ab

a

(a · b) = –ab

a

b

(a · b) = 0

a

b

(a · b) < 0

a

(a · b) > 0

b

b

Figure 5.11. Some common dot product tests.

P

n

h = (P – Q)

n

Q

Figure 5.12. The dot product can be used to ﬁnd the height of a point above or below a plane.

to the plane. To find the height h of a point P above the plane, we first calculate
a vector from any point on the plane (Q will do nicely) to the point in question
P. So we have v = P −Q. The dot product of vector v with the unit-length
normal vector n is just the projection of v onto the line defined by n. But that
is exactly the height we’re looking for. Therefore,

h = v · n = (P −Q) · n.
(5.2)

This is illustrated in Figure 5.12.

5.2.4.8
Cross Product

The cross product (also known as the outer product or vector product) of two vec-
tors yields another vector that is perpendicular to the two vectors being multi-
plied, as shown in Figure 5.13. The cross product operation is only defined in


<!-- source-pdf-page: 390 -->
> Visual fallback for diagrams/images: [PDF page 390](../../../visual_pages/page_0390.jpg)

2

3

2
1

3
1

1

Figure 5.14. Area of a parallelogram expressed as the magnitude of a cross product.

three dimensions:

a × b =
[(aybz −azby), (azbx −axbz), (axby −aybx)
]

= (aybz −azby)i + (azbx −axbz)j + (axby −aybx)k.

Figure
5.13.
The
cross
product
of
vectors
a
and
b
(right-handed).
Magnitude of the Cross Product

The magnitude of the cross product vector is the product of the magnitudes of
the two vectors and the sine of the angle between them. (This is similar to the
definition of the dot product, but it replaces the cosine with the sine.)

|a × b| = |a| |b| sin θ.

The magnitude of the cross product |a × b| is equal to the area of the par-
allelogram whose sides are a and b, as shown in Figure 5.14. Since a triangle
is one half of a parallelogram, the area of a triangle whose vertices are speci-
fied by the position vectors V1, V2 and V3 can be calculated as one half of the
magnitude of the cross product of any two of its sides:

2


(V2 −V1) × (V3 −V1)


 .

Atriangle = 1

Direction of the Cross Product

When using a right-handed coordinate system, you can use the right-hand rule
to determine the direction of the cross product. Simply cup your fingers such
that they point in the direction you’d rotate vector a to move it on top of vector
b, and the cross product (a × b) will be in the direction of your thumb.
Note that the cross product is defined by the left-hand rule when using a left-
handed coordinate system. This means that the direction of the cross product
changes depending on the choice of coordinate system. This might seem odd


<!-- source-pdf-page: 391 -->

at first, but remember that the handedness of a coordinate system does not
affect the mathematical calculations we carry out—it only changes our visu-
alization of what the numbers look like in 3D space. When converting from
a right-handed system to a left-handed system or vice versa, the numerical
representations of all the points and vectors stay the same, but one axis flips.
Our visualization of everything is therefore mirrored along that flipped axis.
So if a cross product just happens to align with the axis we’re flipping (e.g.,
the z-axis), it needs to flip when the axis flips. If it didn’t, the mathemati-
cal definition of the cross product itself would have to be changed so that the
z-coordinate of the cross product comes out negative in the new coordinate
system. I wouldn’t lose too much sleep over all of this. Just remember: when
visualizing a cross product, use the right-hand rule in a right-handed coordi-
nate system and the left-hand rule in a left-handed coordinate system.

Properties of the Cross Product

The cross product is not commutative (i.e., order matters):

a × b ̸= b × a.

However, it is anti-commutative:

a × b = −(b × a).

The cross product is distributive over addition:

a × (b + c) = (a × b) + (a × c).

And it combines with scalar multiplication as follows:

(sa) × b = a × (sb) = s(a × b).

The Cartesian basis vectors are related by cross products as follows:

i × j = −(j × i) = k
j × k = −(k × j) = i
k × i = −(i × k) = j

These three cross products define the direction of positive rotations about the
Cartesian axes. The positive rotations go from x to y (about z), from y to z
(about x) and from z to x (about y). Notice how the rotation about the y-axis
“reversed” alphabetically, in that it goes from z to x (not from x to z). As we’ll


<!-- source-pdf-page: 392 -->

see below, this gives us a hint as to why the matrix for rotation about the y-
axis looks inverted when compared to the matrices for rotation about the x- and
z-axes.

The Cross Product in Action

The cross product has a number of applications in games. One of its most
common uses is for finding a vector that is perpendicular to two other vectors.
As we’ll see in Section 5.3.10.2, if we know an object’s local unit basis vectors,
(ilocal, jlocal and klocal), we can easily find a matrix representing the object’s
orientation. Let’s assume that all we know is the object’s klocal vector—i.e., the
direction in which the object is facing. If we assume that the object has no roll
about klocal, then we can find ilocal by taking the cross product between klocal
(which we already know) and the world-space up vector jworld (which equals
[ 0 1 0 ]). We do so as follows: ilocal = normalize(jworld × klocal). We can then
find jlocal by simply crossing ilocal and klocal as follows: jlocal = klocal × ilocal.
A very similar technique can be used to find a unit vector normal to the
surface of a triangle or some other plane. Given three points on the plane, P1,
P2 and P3, the normal vector is just n = normalize
((P2 −P1) × (P3 −P1)
)
.
Cross products are also used in physics simulations. When a force is ap-
plied to an object, it will give rise to rotational motion if and only if it is applied
off-center. This rotational force is known as a torque, and it is calculated as fol-
lows. Given a force F, and a vector r from the center of mass to the point at
which the force is applied, the torque N = r × F.

5.2.4.9
Pseudovectors and Exterior Algebra

We mentioned in Section 5.2.2 that the cross product doesn’t actually produce
a vector—it produces a special kind of mathematical object known as a pseu-
dovector. The difference between a vector and a pseudovector is pretty sub-
tle. In fact, you can’t tell the difference between them at all when performing
the kinds of transformations we normally encounter in game programming—
translation, rotation and scaling. It’s only when you reflect the coordinate sys-
tem (as happens when you move from a left-handed coordinate system to a
right-handed system) that the special nature of pseudovectors becomes ap-
parent. Under reflection, a vector transforms into its mirror image, as you’d
probably expect. But when a pseudovector is reflected, it transforms into its
mirror image and also changes direction.
Positions and all of the derivatives thereof (linear velocity, acceleration,
jerk) are represented by true vectors (also known as polar vectors or contravari-
ant vectors). Angular velocities and magnetic fields are represented by pseu-


<!-- source-pdf-page: 393 -->
> Visual fallback for diagrams/images: [PDF page 393](../../../visual_pages/page_0393.jpg)

v

u

w

v

u

Figure 5.15. In the exterior algebra (Grassman algebra), a single wedge product yields a pseudovec-
tor or bivector, and two wedge products yield a pseudoscalar or trivector.

dovectors (also known as axial vectors, covariant vectors, bivectors or 2-blades).
The surface normal of a triangle (which is calculated using a cross product) is
also a pseudovector.
It’s pretty interesting to note that the cross product (A × B), the scalar triple
product (A · (B × C)) and the determinant of a matrix are all inter-related, and
pseudovectors lie at the heart of it all. Mathematicians have come up with
a set of algebraic rules, called an exterior algebra or Grassman algebra, which
describe how vectors and pseudovectors work and allow us to calculate areas
of parallelograms (in 2D), volumes of parallelepipeds (in 3D), and so on in
higher dimensions.
We won’t get into all the details here, but the basic idea of Grassman alge-
bra is to introduce a special kind of vector product known as the wedge product,
denoted A ∧B. A pairwise wedge product yields a pseudovector and is equiv-
alent to a cross product, which also represents the signed area of the parallelo-
gram formed by the two vectors (where the sign tells us whether we’re rotating
from A to B or vice versa). Doing two wedge products in a row, A ∧B ∧C,


<!-- source-pdf-page: 394 -->
> Visual fallback for diagrams/images: [PDF page 394](../../../visual_pages/page_0394.jpg)

is equivalent to the scalar triple product A · (B × C) and produces another
strange mathematical object known as a pseudoscalar (also known as a trivector
or a 3-blade), which can be interpreted as the signed volume of the parallelepiped
formed by the three vectors (see Figure 5.15). This extends into higher dimen-
sions as well.
What does all this mean for us as game programmers? Not too much. All
we really need to keep in mind is that some vectors in our code are actually
pseudovectors, so that we can transform them properly when changing hand-
edness, for example. Of course if you really want to geek out, you can impress
your friends by talking about exterior algebras and wedge products and explain-
ing how cross products aren’t really vectors. Which might make you look cool
at your next social engagement …or not.
For more information, see http://en.wikipedia.org/wiki/Pseudovector,
http://en.wikipedia.org/wiki/Exterior_algebra, and http://www.terathon
.com/gdc12_lengyel.pdf.

### 5.2.5 Linear Interpolation of Points and Vectors

In games, we often need to find a vector that is midway between two known
vectors. For example, if we want to smoothly animate an object from point A
to point B over the course of two seconds at 30 frames per second, we would
need to find 60 intermediate positions between A and B.
A linear interpolation is a simple mathematical operation that finds an inter-
mediate point between two known points. The name of this operation is often
shortened to LERP. The operation is defined as follows, where β ranges from
0 to 1 inclusive:

L = LERP(A, B, β) = (1 −β)A + βB
=
[(1 −β)Ax + βBx,
(1 −β)Ay + βBy,
(1 −β)Az + βBz
]

Geometrically, L = LERP(A, B, β) is the position vector of a point that lies
β percent of the way along the line segment from point A to point B, as shown
in Figure 5.16. Mathematically, the LERP function is just a weighted average of
the two input vectors, with weights (1 −β) and β, respectively. Notice that
the weights always add to 1, which is a general requirement for any weighted
average.

## 5.3 Matrices

A matrix is a rectangular array of m × n scalars. Matrices are a convenient way
of representing linear transformations such as translation, rotation and scale.


<!-- source-pdf-page: 395 -->
> Visual fallback for diagrams/images: [PDF page 395](../../../visual_pages/page_0395.jpg)

Figure 5.16. Linear interpolation (LERP) between points A and B, with β = 0.4.

A matrix M is usually written as a grid of scalars Mrc enclosed in square
brackets, where the subscripts r and c represent the row and column indices of
the entry, respectively. For example, if M is a 3 × 3 matrix, it could be written
as follows:






M11
M12
M13
M21
M22
M23
M31
M32
M33

.

M =

We can think of the rows and/or columns of a 3 × 3 matrix as 3D vectors.
When all of the row and column vectors of a 3× 3 matrix are of unit magnitude,
we call it a special orthogonal matrix. This is also known as an isotropic matrix,
or an orthonormal matrix. Such matrices represent pure rotations.
Under certain constraints, a 4× 4 matrix can represent arbitrary 3D transfor-
mations, including translations, rotations, and changes in scale. These are called
transformation matrices, and they are the kinds of matrices that will be most use-
ful to us as game engineers. The transformations represented by a matrix are
applied to a point or vector via matrix multiplication. We’ll investigate how
this works below.
An affine matrix is a 4 × 4 transformation matrix that preserves parallelism
of lines and relative distance ratios, but not necessarily absolute lengths and
angles. An affine matrix is any combination of the following operations: rota-
tion, translation, scale and/or shear.

### 5.3.1 Matrix Multiplication

The product P of two matrices A and B is written P = AB. If A and B are
transformation matrices, then the product P is another transformation matrix
that performs both of the original transformations. For example, if A is a scale
matrix and B is a rotation, the matrix P would both scale and rotate the points
or vectors to which it is applied. This is particularly useful in game program-
ming, because we can precalculate a single matrix that performs a whole se-
quence of transformations and then apply all of those transformations to a
large number of vectors efficiently.


<!-- source-pdf-page: 396 -->

To calculate a matrix product, we simply take dot products between the
rows of the nA × mA matrix A and the columns of the nB × mB matrix B. Each
dot product becomes one component of the resulting matrix P. The two matri-
ces can be multiplied as long as the inner dimensions are equal (i.e., mA = nB).
For example, if A and B are 3 × 3 matrices, then P = AB may be expressed as
follows:






P11
P12
P13
P21
P22
P23
P31
P32
P33



P =






Arow1 · Bcol1
Arow1 · Bcol2
Arow1 · Bcol3
Arow2 · Bcol1
Arow2 · Bcol2
Arow2 · Bcol3
Arow3 · Bcol1
Arow3 · Bcol2
Arow3 · Bcol3

.

=

Matrix multiplication is not commutative. In other words, the order in
which matrix multiplication is done matters:

AB ̸= BA

We’ll see exactly why this matters in Section 5.3.2.
Matrix multiplication is often called concatenation, because the product of
n transformation matrices is a matrix that concatenates, or chains together, the
original sequence of transformations in the order the matrices were multiplied.

### 5.3.2 Representing Points and Vectors as Matrices

Points and vectors can be represented as row matrices (1 × n) or column matrices
(n × 1), where n is the dimension of the space we’re working with (usually 2
or 3). For example, the vector v = (3, 4, −1) can be written either as

v1 =
[3
4
−1]

or as






3
4
−1

= vT
1.

v2 =

Here, the superscripted T represents matrix transposition (see Section 5.3.5).
The choice between column and row vectors is a completely arbitrary one,
but it does affect the order in which matrix multiplications are written. This
happens because when multiplying matrices, the inner dimensions of the two
matrices must be equal, so


<!-- source-pdf-page: 397 -->

•
to multiply a 1 × n row vector by an n × n matrix, the vector must appear
to the left of the matrix (v′
1×n = v1×n Mn×n), whereas

•
to multiply an n × n matrix by an n × 1 column vector, the vector must
appear to the right of the matrix (v′
n×1 = Mn×n vn×1).

If multiple transformation matrices A, B and C are applied in order to a
vector v, the transformations “read” from left to right when using row vectors,
but from right to left when using column vectors. The easiest way to remember
this is to realize that the matrix closest to the vector is applied first. This is
illustrated by the parentheses below:

v′ = (((vA)B)C)
Row vectors: read left-to-right;

v′T = (CT(BT(ATvT)))
Column vectors: read right-to-left.

In this book we’ll adopt the row vector convention, because the left-to-right
order of transformations is most intuitive to read for English-speaking people.
That said, be very careful to check which convention is used by your game en-
gine, and by other books, papers or web pages you may read. You can usually
tell by seeing whether vector-matrix multiplications are written with the vec-
tor on the left (for row vectors) or the right (for column vectors) of the matrix.
When using column vectors, you’ll need to transpose all the matrices shown in
this book.

### 5.3.3 The Identity Matrix

The identity matrix is a matrix that, when multiplied by any other matrix, yields
the very same matrix. It is usually represented by the symbol I. The identity
matrix is always a square matrix with 1’s along the diagonal and 0’s every-
where else:






1
0
0
0
1
0
0
0
1

;

I3×3 =

AI = IA ≡A.

### 5.3.4 Matrix Inversion

The inverse of a matrix A is another matrix (denoted A−1) that undoes the ef-
fects of matrix A. So, for example, if A rotates objects by 37 degrees about
the z-axis, then A−1 will rotate by −37 degrees about the z-axis. Likewise, if
A scales objects to be twice their original size, then A−1 scales objects to be
half-sized. When a matrix is multiplied by its own inverse, the result is al-
ways the identity matrix, so A(A−1) ≡(A−1)A ≡I. Not all matrices have


<!-- source-pdf-page: 398 -->

inverses. However, all affine matrices (combinations of pure rotations, trans-
lations, scales and shears) do have inverses. Gaussian elimination or lower-
upper (LU) decomposition can be used to find the inverse, if one exists.
Since we’ll be dealing with matrix multiplication a lot, it’s important to note
here that the inverse of a sequence of concatenated matrices can be written as
the reverse concatenation of the individual matrices’ inverses. For example,

(ABC)−1 = C−1B−1A−1.

### 5.3.5 Transposition

The transpose of a matrix M is denoted MT. It is obtained by reflecting the
entries of the original matrix across its diagonal. In other words, the rows of
the original matrix become the columns of the transposed matrix, and vice
versa:








T


a
b
c
d
e
f
g
h
i


a
d
g
b
e
h
c
f
i



.

=

The transpose is useful for a number of reasons. For one thing, the inverse
of an orthonormal (pure rotation) matrix is exactly equal to its transpose—
which is good news, because it’s much cheaper to transpose a matrix than it is
to find its inverse in general. Transposition can also be important when mov-
ing data from one math library to another, because some libraries use column
vectors while others expect row vectors. The matrices used by a row-vector–
based library will be transposed relative to those used by a library that employs
the column vector convention.
As with the inverse, the transpose of a sequence of concatenated matrices
can be rewritten as the reverse concatenation of the individual matrices’ trans-
poses. For example,
(ABC)T = CTBTAT.

This will prove useful when we consider how to apply transformation matrices
to points and vectors.

### 5.3.6 Homogeneous Coordinates

You may recall from high-school algebra that a 2 × 2 matrix can represent a
rotation in two dimensions. To rotate a vector r through an angle of ϕ degrees
(where positive rotations are counterclockwise), we can write

[r′
x
r′
y
] =
[rx
ry
] [ cos ϕ
sin ϕ
−sin ϕ
cos ϕ

]
.


<!-- source-pdf-page: 399 -->

It’s probably no surprise that rotations in three dimensions can be represented
by a 3 × 3 matrix. The two-dimensional example above is really just a three-
dimensional rotation about the z-axis, so we can write

[r′
x
r′
y
r′
z
] =
[rx
ry
rz
]





cos ϕ
sin ϕ
0
−sin ϕ
cos ϕ
0
0
0
1

.

The question naturally arises: Can a 3 × 3 matrix be used to represent trans-
lations? Sadly, the answer is no. The result of translating a point r by a transla-
tion t requires adding the components of t to the components of r individually:

r + t =
[(rx + tx)
(ry + ty)
(rz + tz)
]
.

Matrix multiplication involves multiplication and addition of matrix elements,
so the idea of using multiplication for translation seems promising. But, unfor-
tunately, there is no way to arrange the components of t within a 3 × 3 matrix
such that the result of multiplying it with the column vector r yields sums like
(rx + tx).
The good news is that we can obtain sums like this if we use a 4 × 4 matrix.
What would such a matrix look like? Well, we know that we don’t want any
rotational effects, so the upper 3 × 3 should contain an identity matrix. If we
arrange the components of t across the bottom-most row of the matrix and set
the fourth element of the r vector (usually called w) equal to 1, then taking the
dot product of the vector r with column 1 of the matrix will yield (1 · rx) + (0 ·
ry) + (0 · rz) + (tx · 1), which is exactly what we want. If the bottom right-hand
corner of the matrix contains a 1 and the rest of the fourth column contains
zeros, then the resulting vector will also have a 1 in its w component. Here’s
what the final 4 × 4 translation matrix looks like:





1
0
0
0
0
1
0
0
0
0
1
0
tx
ty
tz
1





r + t =
[
rx
ry
rz
1
]

=
[(rx + tx)
(ry + ty)
(rz + tz)
1]
.

When a point or vector is extended from three dimensions to four in this
manner, we say that it has been written in homogeneous coordinates. A point
in homogeneous coordinates always has w = 1. Most of the 3D matrix math
done by game engines is performed using 4 × 4 matrices with four-element
points and vectors written in homogeneous coordinates.


<!-- source-pdf-page: 400 -->

5.3.6.1
Transforming Direction Vectors

Mathematically, points (position vectors) and direction vectors are treated in
subtly different ways. When transforming a point by a matrix, the translation,
rotation and scale of the matrix are all applied to the point. But when trans-
forming a direction by a matrix, the translational effects of the matrix are ig-
nored. This is because direction vectors have no translation per se—applying
a translation to a direction would alter its magnitude, which is usually not
what we want.
In homogeneous coordinates, we achieve this by defining points to have
their w components equal to one, while direction vectors have their w compo-
nents equal to zero. In the example below, notice how the w = 0 component
of the vector v multiplies with the t vector in the matrix, thereby eliminating
translation in the final result:

[
v
0
] [
U
0
t
1

]
=
[(vU + 0t)
0
] =
[
vU
0
]
.

Technically, a point in homogeneous (four-dimensional) coordinates can
be converted into non-homogeneous (three-dimensional) coordinates by di-
viding the x, y and z components by the w component:

]
.

[x
y
z
w] ≡
[ x

w
y
w
z
w

This sheds some light on why we set a point’s w component to one and a vec-
tor’s w component to zero. Dividing by w = 1 has no effect on the coordinates
of a point, but dividing a pure direction vector’s components by w = 0 would
yield infinity. A point at infinity in 4D can be rotated but not translated, be-
cause no matter what translation we try to apply, the point will remain at in-
finity. So in effect, a pure direction vector in three-dimensional space acts like
a point at infinity in four-dimensional homogeneous space.

### 5.3.7 Atomic Transformation Matrices

Any affine transformation matrix can be created by simply concatenating a
sequence of 4 × 4 matrices representing pure translations, pure rotations, pure
scale operations and/or pure shears. These atomic transformation building
blocks are presented below. (We’ll omit shear from these discussions, as it
tends to be used only rarely in games.)
Notice that all affine 4 × 4 transformation matrices can be partitioned into
four components:

Maffine =
[U3×3
03×1
t1×3
1

]
.


<!-- source-pdf-page: 401 -->

•
the upper 3 × 3 matrix U, which represents the rotation and/or scale,

•
a 1 × 3 translation vector t,

•
a 3 × 1 vector of zeros 0 =
[0
0
0]T, and

•
a scalar 1 in the bottom-right corner of the matrix.

When a point is multiplied by a matrix that has been partitioned like this, the
result is as follows:

[r′
1×3
1] =
[
r1×3
1
] [U3×3
03×1
t1×3
1

]
=
[(rU + t)
1
]
.

5.3.7.1
Translation

The following matrix translates a point by the vector t:





1
0
0
0
0
1
0
0
0
0
1
0
tx
ty
tz
1




(5.3)

r + t =
[
rx
ry
rz
1
]

=
[(rx + tx)
(ry + ty)
(rz + tz)
1]
,

or in partitioned shorthand:

[r
1] [
I
0
t
1

]
=
[(r + t)
1]
.

To invert a pure translation matrix, simply negate the vector t (i.e., negate tx,
ty and tz).

5.3.7.2
Rotation

All 4 × 4 pure rotation matrices have the form

[r
1] [R
0
0
1

]
=
[rR
1]
.

The t vector is zero, and the upper 3 × 3 matrix R contains cosines and sines
of the rotation angle, measured in radians.
The following matrix represents rotation about the x-axis by an angle ϕ.





1
0
0
0
0
cos ϕ
sin ϕ
0
0
−sin ϕ
cos ϕ
0
0
0
0
1



.
(5.4)

rotatex(r, ϕ) =
[
rx
ry
rz
1
]


<!-- source-pdf-page: 402 -->

The matrix below represents rotation about the y-axis by an angle θ. (Notice
that this one is transposed relative to the other two—the positive and negative
sine terms have been reflected across the diagonal.)





cos θ
0
−sin θ
0
0
1
0
0
sin θ
0
cos θ
0
0
0
0
1



.
(5.5)

rotatey(r, θ) =
[rx
ry
rz
1]

The following matrix represents rotation about the z-axis by an angle γ:





cos γ
sin γ
0
0
−sin γ
cos γ
0
0
0
0
1
0
0
0
0
1



.
(5.6)

rotatez(r, γ) =
[rx
ry
rz
1]

Here are a few observations about these matrices:

•
The 1 within the upper 3 × 3 always appears on the axis we’re rotating
about, while the sine and cosine terms are off-axis.
•
Positive rotations go from x to y (about z), from y to z (about x) and from
z to x (about y). The z to x rotation “wraps around,” which is why the
rotation matrix about the y-axis is transposed relative to the other two.
(Use the right-hand or left-hand rule to remember this.)
•
The inverse of a pure rotation is just its transpose. This works because
inverting a rotation is equivalent to rotating by the negative angle. You
may recall that cos(−θ) = cos(θ) while sin(−θ) = −sin(θ), so negating
the angle causes the two sine terms to effectively switch places, while the
cosine terms stay put.

5.3.7.3
Scale

The following matrix scales the point r by a factor of sx along the x-axis, sy
along the y-axis and sz along the z-axis:





sx
0
0
0
0
sy
0
0
0
0
sz
0
0
0
0
1


(5.7)



rS =
[rx
ry
rz
1]

=
[
sxrx
syry
szrz
1
]
,

or in partitioned shorthand:

[
r
1
] [S3×3
0
0
1

]
=
[
rS3×3
1
]
.

Here are some observations about this kind of matrix:


<!-- source-pdf-page: 403 -->
> Visual fallback for diagrams/images: [PDF page 403](../../../visual_pages/page_0403.jpg)

•
To invert a scaling matrix, simply substitute sx, sy and sz with their re-
ciprocals (i.e., 1/sx, 1/sy and 1/sz).

•
When the scale factor along all three axes is the same (sx = sy = sz),
we call this uniform scale. Spheres remain spheres under uniform scale,
whereas under nonuniform scale they become ellipsoids. To keep the
mathematics of bounding sphere checks simple and fast, many game
engines impose the restriction that only uniform scale may be applied
to renderable geometry or collision primitives.

•
When a uniform scale matrix Su and a rotation matrix R are concate-
nated, the order of multiplication is unimportant (i.e., SuR = RSu). This
only works for uniform scale!

### 5.3.8 4 × 3 Matrices

The rightmost column of an affine 4 × 4 matrix always contains the vector
[0
0
0
1]T. As such, game programmers often omit the fourth column to
save memory. You’ll encounter 4 × 3 affine matrices frequently in game math
libraries.

### 5.3.9 Coordinate Spaces

We’ve seen how to apply transformations to points and direction vectors us-
ing 4 × 4 matrices. We can extend this idea to rigid objects by realizing that
such an object can be thought of as an infinite collection of points. Applying
a transformation to a rigid object is like applying that same transformation to
every point within the object. For example, in computer graphics an object is
usually represented by a mesh of triangles, each of which has three vertices
represented by points. In this case, the object can be transformed by applying
a transformation matrix to all of its vertices in turn.

B

A
B

B

A

A

Figure 5.17. Position vectors for the point P relative to different coordinate axes.


<!-- source-pdf-page: 404 -->
> Visual fallback for diagrams/images: [PDF page 404](../../../visual_pages/page_0404.jpg)

We said above that a point is a vector whose tail is fixed to the origin of
some coordinate system. This is another way of saying that a point (position
vector) is always expressed relative to a set of coordinate axes. The triplet of
numbers representing a point changes numerically whenever we select a new
set of coordinate axes. In Figure 5.17, we see a point P represented by two
different position vectors—the vector PA gives the position of P relative to the
“A” axes, while the vector PB gives the position of that same point relative to
a different set of axes “B.”
In physics, a set of coordinate axes represents a frame of reference, so we
sometimes refer to a set of axes as a coordinate frame (or just a frame). People in
the game industry also use the term coordinate space (or simply space) to refer
to a set of coordinate axes. In the following sections, we’ll look at a few of the
most common coordinate spaces used in games and computer graphics.

5.3.9.1
Model Space

When a triangle mesh is created in a tool such as Maya or 3DStudioMAX, the
positions of the triangles’ vertices are specified relative to a Cartesian coordi-
nate system, which we call model space (also known as object space or local space).
The model-space origin is usually placed at a central location within the object,
such as at its center of mass, or on the ground between the feet of a humanoid
or animal character.
Most game objects have an inherent directionality. For example, an air-
plane has a nose, a tail fin and wings that correspond to the front, up and
left/right directions. The model-space axes are usually aligned to these natu-
ral directions on the model, and they’re given intuitive names to indicate their
directionality as illustrated in Figure 5.18.

•
Front. This name is given to the axis that points in the direction that the
object naturally travels or faces. In this book, we’ll use the symbol F to

up

front

left

Figure 5.18. One possible choice of the model-space front, left and up axis basis vectors for an
airplane.


<!-- source-pdf-page: 405 -->

refer to a unit basis vector along the front axis.

•
Up. This name is given to the axis that points towards the top of the
object. The unit basis vector along this axis will be denoted U.

•
Left or right. The name “left” or “right” is given to the axis that points
toward the left or right side of the object. Which name is chosen de-
pends on whether your game engine uses left-handed or right-handed
coordinates. The unit basis vector along this axis will be denoted L or R,
as appropriate.

The mapping between the ( front, up, le f t) labels and the (x, y, z) axes
is completely arbitrary. A common choice when working with right-handed
axes is to assign the label front to the positive z-axis, the label left to the positive
x-axis and the label up to the positive y-axis (or in terms of unit basis vectors,
F = k, L = i and U = j). However, it’s equally common for +x to be front and
+z to be right (F = i, R = k, U = j). I’ve also worked with engines in which
the z-axis is oriented vertically. The only real requirement is that you stick to
one convention consistently throughout your engine.
As an example of how intuitive axis names can reduce confusion, consider
Euler angles (pitch, yaw, roll), which are often used to describe an aircraft’s
orientation. It’s not possible to define pitch, yaw, and roll angles in terms of
the (i, j, k) basis vectors because their orientation is arbitrary. However, we
can define pitch, yaw and roll in terms of the (L, U, F) basis vectors, because
their orientations are clearly defined. Specifically,

•
pitch is rotation about L or R,

•
yaw is rotation about U, and

•
roll is rotation about F.

5.3.9.2
World Space

World space is a fixed coordinate space, in which the positions, orientations and
scales of all objects in the game world are expressed. This coordinate space ties
all the individual objects together into a cohesive virtual world.
The location of the world-space origin is arbitrary, but it is often placed
near the center of the playable game space to minimize the reduction in
floating-point precision that can occur when (x, y, z) coordinates grow very
large. Likewise, the orientation of the x-, y- and z-axes is arbitrary, although
most of the engines I’ve encountered use either a y-up or a z-up convention.
The y-up convention was probably an extension of the two-dimensional con-
vention found in most mathematics textbooks, where the y-axis is shown go-
ing up and the x-axis going to the right. The z-up convention is also common,


<!-- source-pdf-page: 406 -->
> Visual fallback for diagrams/images: [PDF page 406](../../../visual_pages/page_0406.jpg)

xM

Left
Wingtip:

(–25,50,3)W

xW

(5,0,0)M

zM

zW

Aircraft:

(–25,50,8)W

Figure 5.19. A Lear jet whose left wingtip is at (5, 0, 0) in model space. If the jet is rotated by 90
degrees about the world-space y-axis, and its model-space origin translated to (−25, 50, 8) in
world space, then its left wingtip would end up at (−25, 50, 3) when expressed in world-space
coordinates.

because it allows a top-down orthographic view of the game world to look like
a traditional two-dimensional xy-plot.
As an example, let’s say that our aircraft’s left wingtip is at (5, 0, 0) in model
space. (In our game, front vectors correspond to the positive z-axis in model
space with y up, as shown in Figure 5.18.) Now imagine that the jet is facing
down the positive x-axis in world space, with its model-space origin at some
arbitrary location, such as (−25, 50, 8). Because the F vector of the airplane,
which corresponds to +z in model space, is facing down the +x-axis in world
space, we know that the jet has been rotated by 90 degrees about the world
y-axis. So, if the aircraft were sitting at the world-space origin, its left wingtip
would be at (0, 0, −5) in world space. But because the aircraft’s origin has
been translated to (−25, 50, 8), the final position of the jet’s left wingtip in
world space is (−25, 50, [8 −5]) = (−25, 50, 3). This is illustrated in Fig-
ure 5.19.
We could of course populate our friendly skies with more than one Lear
jet. In that case, all of their left wingtips would have coordinates of (5, 0, 0)
in model space. But in world space, the left wingtips would have all sorts of
interesting coordinates, depending on the orientation and translation of each
aircraft.

5.3.9.3
View Space

View space (also known as camera space) is a coordinate frame fixed to the cam-
era. The view space origin is placed at the focal point of the camera. Again,
any axis orientation scheme is possible. However, a y-up convention with z
increasing in the direction the camera is facing (left-handed) is typical because
it allows z coordinates to represent depths into the screen. Other engines and
APIs, such as OpenGL, define view space to be right-handed, in which case the
camera faces towards negative z, and z coordinates represent negative depths.


<!-- source-pdf-page: 407 -->
> Visual fallback for diagrams/images: [PDF page 407](../../../visual_pages/page_0407.jpg)

y
Virtual
Screen

y

Virtual
Screen

z

x

x

Right-Handed
z

Left-Handed

Figure 5.20. Left- and right-handed examples of view space, also known as camera space.

Two possible definitions of view space are illustrated in Figure 5.20.

### 5.3.10 Change of Basis

In games and computer graphics, it is often quite useful to convert an object’s
position, orientation and scale from one coordinate system into another. We
call this operation a change of basis.

5.3.10.1
Coordinate Space Hierarchies

Coordinate frames are relative. That is, if you want to quantify the position,
orientation and scale of a set of axes in three-dimensional space, you must
specify these quantities relative to some other set of axes (otherwise the num-
bers would have no meaning). This implies that coordinate spaces form a hi-
erarchy—every coordinate space is a child of some other coordinate space, and
the other space acts as its parent. World space has no parent; it is at the root
of the coordinate-space tree, and all other coordinate systems are ultimately
specified relative to it, either as direct children or more-distant relatives.

5.3.10.2
Building a Change of Basis Matrix

The matrix that transforms points and directions from any child coordinate
system C to its parent coordinate system P can be written MC→P (pronounced
“C to P”). The subscript indicates that this matrix transforms points and direc-
tions from child space to parent space. Any child-space position vector PC can


<!-- source-pdf-page: 408 -->
> Visual fallback for diagrams/images: [PDF page 408](../../../visual_pages/page_0408.jpg)

be transformed into a parent-space position vector PP as follows:

PP = PCMC→P;





iC
0
jC
0
kC
0
tC
1





MC→P =





iCx
iCy
iCz
0
jCx
jCy
jCz
0
kCx
kCy
kCz
0
tCx
tCy
tCz
1



.

=

In this equation,

•
iC is the unit basis vector along the child space x-axis, expressed in
parent-space coordinates;

•
jC is the unit basis vector along the child space y-axis, in parent space;

•
kC is the unit basis vector along the child space z-axis, in parent space;
and

•
tC is the translation of the child coordinate system relative to parent
space.

This result should not be too surprising. The tC vector is just the translation
of the child-space axes relative to parent space, so if the rest of the matrix were
identity, the point (0, 0, 0) in child space would become tC in parent space,
just as we’d expect. The iC, jC and kC unit vectors form the upper 3 × 3 of
the matrix, which is a pure rotation matrix because these vectors are of unit
length. We can see this more clearly by considering a simple example, such as
a situation in which child space is rotated by an angle γ about the z-axis, with
no translation. Recall from Equation (5.6) that the matrix for such a rotation is
given by





cos γ
sin γ
0
0
−sin γ
cos γ
0
0
0
0
1
0
0
0
0
1



.

rotatez(r, γ) =
[
rx
ry
rz
1
]

But in Figure 5.21, we can see that the coordinates of the iC and jC vectors, ex-
pressed in parent space, are iC =
[cos γ sin γ 0]
and jC =
[−sin γ cos γ 0]
.
When we plug these vectors into our formula for MC→P, with kC =
[0
0
1]
,
it exactly matches the matrix rotatez(r, γ) from Equation (5.6).


<!-- source-pdf-page: 409 -->
> Visual fallback for diagrams/images: [PDF page 409](../../../visual_pages/page_0409.jpg)

Scaling the Child Axes

Scaling of the child coordinate system is accomplished by simply scaling the
unit basis vectors appropriately. For example, if child space is scaled up by a
factor of two, then the basis vectors iC, jC and kC will be of length 2 instead of
unit length.

5.3.10.3
Extracting Unit Basis Vectors from a Matrix

The fact that we can build a change of basis matrix out of a translation and
three Cartesian basis vectors gives us another powerful tool: Given any affine
4 × 4 transformation matrix, we can go in the other direction and extract the
child-space basis vectors iC, jC and kC from it by simply isolating the ap-
propriate rows of the matrix (or columns if your math library uses column
vectors).
This can be incredibly useful. Let’s say we are given a vehicle’s model-
to-world transform as an affine 4 × 4 matrix (a very common representation).
This is really just a change of basis matrix, transforming points in model space
into their equivalents in world space. Let’s further assume that in our game,
the positive z-axis always points in the direction that an object is facing. So,
to find a unit vector representing the vehicle’s facing direction, we can simply
extract kC directly from the model-to-world matrix (by grabbing its third row).
This vector will already be normalized and ready to go.

5.3.10.4
Transforming Coordinate Systems versus Vectors

We’ve said that the matrix MC→P transforms points and directions from child
space into parent space. Recall that the fourth row of MC→P contains tC, the
translation of the child coordinate axes relative to the world-space axes. There-
fore, another way to visualize the matrix MC→P is to imagine it taking the
parent coordinate axes and transforming them into the child axes. This is the

y

C

γ

γ

C

γ

γ

γ

γ

x

Figure 5.21. Change of basis when child axes are rotated by an angle γ relative to parent.


<!-- source-pdf-page: 410 -->
> Visual fallback for diagrams/images: [PDF page 410](../../../visual_pages/page_0410.jpg)

y

y

y'

P'
P
P

x

x

x'

Figure 5.22. Two ways to interpret a transformation matrix. On the left, the point moves against
a ﬁxed set of axes. On the right, the axes move in the opposite direction while the point remains
ﬁxed.

reverse of what happens to points and direction vectors. In other words, if a
matrix transforms vectors from child space to parent space, then it also trans-
forms coordinate axes from parent space to child space. This makes sense when
you think about it—moving a point 20 units to the right with the coordinate
axes fixed is the same as moving the coordinate axes 20 units to the left with
the point fixed. This concept is illustrated in Figure 5.22.
Of course, this is just another point of potential confusion. If you’re think-
ing in terms of coordinate axes, then transformations go in one direction, but
if you’re thinking in terms of points and vectors, they go in the other direction!
As with many confusing things in life, your best bet is probably to choose a
single “canonical” way of thinking about things and stick with it. For example,
in this book we’ve chosen the following conventions:

•
Transformations apply to vectors (not coordinate axes).
•
Vectors are written as rows (not columns).

Taken together, these two conventions allow us to read sequences of ma-
trix multiplications from left to right and have them make sense (e.g., in the
expression rD = rAMA→BMB→CMC→D, the B’s and C’s in effect “cancel out,”
leaving only rD = rAMA→D). Obviously if you start thinking about the coor-
dinate axes moving around rather than the points and vectors, you either have
to read the transforms from right to left, or flip one of these two conventions
around. It doesn’t really matter what conventions you choose as long as you
find them easy to remember and work with.
That said, it’s important to note that certain problems are easier to think
about in terms of vectors being transformed, while others are easier to work
with when you imagine the coordinate axes moving around. Once you get
good at thinking about 3D vector and matrix math, you’ll find it pretty easy
to flip back and forth between conventions as needed to suit the problem at
hand.


<!-- source-pdf-page: 411 -->

### 5.3.11 Transforming Normal Vectors

A normal vector is a special kind of vector, because in addition to (usually!)
being of unit length, it carries with it the additional requirement that it should
always remain perpendicular to whatever surface or plane it is associated with.
Special care must be taken when transforming a normal vector to ensure that
both its length and perpendicularity properties are maintained.
In general, if a point or (non-normal) vector can be rotated from space A to
space B via the 3 × 3 matrix MA→B, then a normal vector n will be transformed
from space A to space B via the inverse transpose of that matrix, (M−1
A→B)T. We
will not prove or derive this result here (see [32, Section 3.5] for an excellent
derivation). However, we will observe that if the matrix MA→B contains only
uniform scale and no shear, then the angles between all surfaces and vectors in
space B will be the same as they were in space A. In this case, the matrix MA→B
will actually work just fine for any vector, normal or non-normal. However, if
MA→B contains nonuniform scale or shear (i.e., is non-orthogonal), then the an-
gles between surfaces and vectors are not preserved when moving from space
A to space B. A vector that was normal to a surface in space A will not necessar-
ily be perpendicular to that surface in space B. The inverse transpose operation
accounts for this distortion, bringing normal vectors back into perpendicu-
larity with their surfaces even when the transformation involves nonuniform
scale or shear. Another way of looking at this is that the inverse transpose is
required because a surface normal is really a pseudovector rather than a regular
vector (see Section 5.2.4.9).

### 5.3.12 Storing Matrices in Memory

In the C and C++ languages, a two-dimensional array is often used to store a
matrix. Recall that in C/C++ two-dimensional array syntax, the first subscript
is the row and the second is the column, and the column index varies fastest
as you move through memory sequentially.

float m[4][4]; // [row][col], col varies fastest

// "flatten" the array to demonstrate ordering
float* pm = &m[0][0];
ASSERT( &pm[0] == &m[0][0] );
ASSERT( &pm[1] == &m[0][1] );
ASSERT( &pm[2] == &m[0][2] );
// etc.

We have two choices when storing a matrix in a two-dimensional C/C++
array. We can either


<!-- source-pdf-page: 412 -->

1.
store the vectors (iC, jC, kC, tC) contiguously in memory (i.e., each row
contains a single vector), or

2.
store the vectors strided in memory (i.e., each column contains one vector).

The benefit of approach (1) is that we can address any one of the four vec-
tors by simply indexing into the matrix and interpreting the four contiguous
values we find there as a 4-element vector. This layout also has the benefit
of matching up exactly with row vector matrix equations (which is another
reason why I’ve selected row vector notation for this book). Approach (2) is
sometimes necessary when doing fast matrix-vector multiplies using a vector-
enabled (SIMD) microprocessor, as we’ll see later in this chapter. In most game
engines I’ve personally encountered, matrices are stored using approach (1),
with the vectors in the rows of the two-dimensional C/C++ array. This is shown
below:

float M[4][4];

M[0][0]=ix;
M[0][1]=iy;
M[0][2]=iz;
M[0][3]=0.0f;
M[1][0]=jx;
M[1][1]=jy;
M[1][2]=jz;
M[1][3]=0.0f;
M[2][0]=kx;
M[2][1]=ky;
M[2][2]=kz;
M[2][3]=0.0f;
M[3][0]=tx;
M[3][1]=ty;
M[3][2]=tz;
M[3][3]=1.0f;

The matrix looks like this when viewed in a debugger:

M[][]
[0]
[0] ix
[1] iy
[2] iz
[3] 0.0000
[1]
[0] jx
[1] jy
[2] jz
[3] 0.0000
[2]
[0] kx
[1] ky
[2] kz
[3] 0.0000
[3]
[0] tx
[1] ty
[2] tz
[3] 1.0000


<!-- source-pdf-page: 413 -->

One easy way to determine which layout your engine uses is to find a func-
tion that builds a 4 × 4 translation matrix. (Every good 3D math library pro-
vides such a function.) You can then inspect the source code to see where the
elements of the t vector are being stored. If you don’t have access to the source
code of your math library (which is pretty rare in the game industry), you can
always call the function with an easy-to-recognize translation like (4, 3, 2), and
then inspect the resulting matrix. If row 3 contains the values 4.0f, 3.0f,
2.0f, 1.0f, then the vectors are in the rows, otherwise the vectors are in the
columns.

## 5.4 Quaternions

We’ve seen that a 3 × 3 matrix can be used to represent an arbitrary rotation
in three dimensions. However, a matrix is not always an ideal representation
of a rotation, for a number of reasons:

1.
We need nine floating-point values to represent a rotation, which seems
excessive considering that we only have three degrees of freedom—
pitch, yaw and roll.

2.
Rotating a vector requires a vector-matrix multiplication, which involves
three dot products, or a total of nine multiplications and six additions.
We would like to find a rotational representation that is less expensive
to calculate, if possible.

3.
In games and computer graphics, it’s often important to be able to find
rotations that are some percentage of the way between two known rota-
tions. For example, if we are to smoothly animate a camera from some
starting orientation A to some final orientation B over the course of a few
seconds, we need to be able to find lots of intermediate rotations between
A and B over the course of the animation. It turns out to be difficult to
do this when the A and B orientations are expressed as matrices.

Thankfully, there is a rotational representation that overcomes these three
problems. It is a mathematical object known as a quaternion. A quaternion
looks a lot like a four-dimensional vector, but it behaves quite differently.
We usually write quaternions using non-italic, non-boldface type, like this:
q =
[
qx
qy
qz
qw
]
.
Quaternions were developed by Sir William Rowan Hamilton in 1843 as
an extension to the complex numbers.
(Specifically, a quaternion may be
interpreted as a four-dimensional complex number, with a single real axis


<!-- source-pdf-page: 414 -->

and three imaginary axes represented by the imaginary numbers i, j and k.
As such, a quaternion can be written in “complex form” as follows: q =
iqx + jqy + kqz + qw.) Quaternions were first used to solve problems in the
area of mechanics. Technically speaking, a quaternion obeys a set of rules
known as a four-dimensional normed division algebra over the real numbers.
Thankfully, we won’t need to understand the details of these rather esoteric
algebraic rules. For our purposes, it will suffice to know that the unit-length
quaternions (i.e., all quaternions obeying the constraint q2
x + q2
y + q2
z + q2
w = 1)
represent three-dimensional rotations.
There are a lot of great papers, web pages and presentations on quaternions
available on the web for further reading. Here’s one of my favorites: http://
graphics.ucsd.edu/courses/cse169_w05/CSE169_04.ppt.

### 5.4.1 Unit Quaternions as 3D Rotations

A unit quaternion can be visualized as a three-dimensional vector plus a fourth
scalar coordinate. The vector part qV is the unit axis of rotation, scaled by the
sine of the half-angle of the rotation. The scalar part qS is the cosine of the
half-angle. So the unit quaternion q can be written as follows:

q =
[
qV
qS
]

=
[
a sin θ

2
]
,

2
cos θ

where a is a unit vector along the axis of rotation, and θ is the angle of rotation.
The direction of the rotation follows the right-hand rule, so if your thumb points
in the direction of a, positive rotations will be in the direction of your curved
fingers.
Of course, we can also write q as a simple four-element vector:

q =
[qx
qy
qz
qw
]
,where

qx = qVx = ax sin θ

2,

qy = qVy = ay sin θ

2,

qz = qVz = az sin θ

2,

qw = qS = cos θ

2.

A unit quaternion is very much like an axis+angle representation of a ro-
tation (i.e., a four-element vector of the form
[a
θ]
). However, quaternions
are more convenient mathematically than their axis+angle counterparts, as we
shall see below.


<!-- source-pdf-page: 415 -->

### 5.4.2 Quaternion Operations

Quaternions support some of the familiar operations from vector algebra, such
as magnitude and vector addition. However, we must remember that the
sum of two unit quaternions does not represent a 3D rotation, because such a
quaternion would not be of unit length. As a result, you won’t see any quater-
nion sums in a game engine, unless they are scaled in some way to preserve
the unit length requirement.

5.4.2.1
Quaternion Multiplication

One of the most important operations we will perform on quaternions is that
of multiplication. Given two quaternions p and q representing two rotations
P and Q, respectively, the product pq represents the composite rotation (i.e.,
rotation Q followed by rotation P). There are actually quite a few different
kinds of quaternion multiplication, but we’ll restrict this discussion to the va-
riety used in conjunction with 3D rotations, namely the Grassman product.
Using this definition, the product pq is defined as follows:

pq =
[(pSqV + qSpV + pV × qV)
(pSqS −pV · qV)
]
.

Notice how the Grassman product is defined in terms of a vector part, which
ends up in the x, y and z components of the resultant quaternion, and a scalar
part, which ends up in the w component.

5.4.2.2
Conjugate and Inverse

The inverse of a quaternion q is denoted q−1 and is defined as a quaternion
that, when multiplied by the original, yields the scalar 1 (i.e., qq−1 = 0i + 0j +
0k + 1). The quaternion
[
0
0
0
1
]
represents a zero rotation (which makes
sense since sin(0) = 0 for the first three components, and cos(0) = 1 for the
last component).
In order to calculate the inverse of a quaternion, we must first define a
quantity known as the conjugate. This is usually denoted q∗and it is defined
as follows:

q∗=
[−qV
qS
]
.

In other words, we negate the vector part but leave the scalar part unchanged.
Given this definition of the quaternion conjugate, the inverse quaternion
q−1 is defined as follows:

q−1 = q∗

|q|2 .


<!-- source-pdf-page: 416 -->

Our quaternions are always of unit length (i.e., |q| = 1), because they represent
3D rotations. So, for our purposes, the inverse and the conjugate are identical:

q−1 = q∗=
[−qV
qS
]
when
|q| = 1.

This fact is incredibly useful, because it means we can always avoid doing
the (relatively expensive) division by the squared magnitude when inverting a
quaternion, as long as we know a priori that the quaternion is normalized. This
also means that inverting a quaternion is generally much faster than inverting
a 3 × 3 matrix—a fact that you may be able to leverage in some situations when
optimizing your engine.

Conjugate and Inverse of a Product

The conjugate of a quaternion product (pq) is equal to the reverse product of
the conjugates of the individual quaternions:

(pq)∗= q∗p∗.

Likewise, the inverse of a quaternion product is equal to the reverse product
of the inverses of the individual quaternions:

(pq)−1 = q−1p−1.
(5.8)

This is analogous to the reversal that occurs when transposing or inverting
matrix products.

### 5.4.3 Rotating Vectors with Quaternions

How can we apply a quaternion rotation to a vector? The first step is to rewrite
the vector in quaternion form. A vector is a sum involving the unit basis vec-
tors i, j and k. A quaternion is a sum involving i, j and k, but with a fourth
scalar term as well. So it makes sense that a vector can be written as a quater-
nion with its scalar term qS equal to zero. Given the vector v, we can write a
corresponding quaternion v =
[v
0] =
[vx
vy
vz
0]
.
In order to rotate a vector v by a quaternion q, we premultiply the vector
(written in its quaternion form v) by q and then post-multiply it by the inverse
quaternion q−1. Therefore, the rotated vector v′ can be found as follows:

v′ = rotate(q, v) = qvq−1.

This is equivalent to using the quaternion conjugate, because our quaternions
are always unit length:

v′ = rotate(q, v) = qvq∗.
(5.9)


<!-- source-pdf-page: 417 -->

The rotated vector v′ is obtained by simply extracting it from its quaternion
form v′.
Quaternion multiplication can be useful in all sorts of situations in real
games. For example, let’s say that we want to find a unit vector describ-
ing the direction in which an aircraft is flying. We’ll further assume that in
our game, the positive z-axis always points toward the front of an object by
convention. So the forward unit vector of any object in model space is always
FM ≡
[
0
0
1
]
by definition. To transform this vector into world space, we
can simply take our aircraft’s orientation quaternion q and use it with Equa-
tion (5.9) to rotate our model-space vector FM into its world-space equivalent
FW (after converting these vectors into quaternion form, of course):

FW = qFMq−1 = q
[0
0
1
0]
q−1.

5.4.3.1
Quaternion Concatenation

Rotations can be concatenated in exactly the same way that matrix-based trans-
formations can, by multiplying the quaternions together. For example, con-
sider three distinct rotations, represented by the quaternions q1, q2 and q3,
with matrix equivalents R1, R2 and R3. We want to apply rotation 1 first, fol-
lowed by rotation 2 and finally rotation 3. The composite rotation matrix Rnet
can be found and applied to a vector v as follows:

Rnet = R1R2R3;
v′ = vR1R2R3
= vRnet.

Likewise, the composite rotation quaternion qnet can be found and applied to
vector v (in its quaternion form, v) as follows:

qnet = q3q2q1;

v′ = q3q2q1 v q−1
1 q−1
2 q−1
3
= qnet v q−1
net.

Notice how the quaternion product must be performed in an order opposite
to that in which the rotations are applied (q3q2q1). This is because quater-
nion rotations always multiply on both sides of the vector, with the uninverted
quaternions on the left and the inverted quaternions on the right. As we saw
in Equation (5.8), the inverse of a quaternion product is the reverse product of
the individual inverses, so the uninverted quaternions read right-to-left while
the inverted quaternions read left-to-right.


<!-- source-pdf-page: 418 -->

### 5.4.4 Quaternion-Matrix Equivalence

We can convert any 3D rotation freely between a 3 × 3 matrix representation R
and a quaternion representation q. If we let q = [qV qS] = [qVx qVy qVz qS]
=
[x
y
z
w]
, then we can find R as follows:






1 −2y2 −2z2
2xy + 2zw
2xz −2yw
2xy −2zw
1 −2x2 −2z2
2yz + 2xw
2xz + 2yw
2yz −2xw
1 −2x2 −2y2

.

R =

Likewise, given R, we can find q as follows (where q[0] = qVx, q[1]
= qVy, q[2] = qVz and q[3] = qS). This code assumes that we are using
row vectors in C/C++ (i.e., that the rows of the matrix correspond to the
rows of the matrix R shown above). The code was adapted from a Gamasu-
tra article by Nick Bobic, published on July 5, 1998, which is available here:
http://www.gamasutra.com/view/feature/3278/rotating_objects_using_
quaternions.php. For a discussion of some even faster methods for convert-
ing a matrix to a quaternion, leveraging various assumptions about the na-
ture of the matrix, see http://www.euclideanspace.com/maths/geometry/
rotations/conversions/matrixToQuaternion/index.htm.

void matrixToQuaternion(
const float
R[3][3],
float
q[/*4*/])
{
float trace = R[0][0] + R[1][1] + R[2][2];

// check the diagonal
if (trace > 0.0f)
{
float s = sqrt(trace + 1.0f);
q[3] = s * 0.5f;

float t = 0.5f / s;
q[0] = (R[2][1] - R[1][2]) * t;
q[1] = (R[0][2] - R[2][0]) * t;
q[2] = (R[1][0] - R[0][1]) * t;
}
else
{
// diagonal is negative
int i = 0;
if (R[1][1] > R[0][0]) i = 1;
if (R[2][2] > R[i][i]) i = 2;

static const int NEXT[3] = {1, 2, 0};


<!-- source-pdf-page: 419 -->

int j = NEXT[i];
int k = NEXT[j];

float s = sqrt((R[i][j]
- (R[j][j] + R[k][k]))
+ 1.0f);

q[i] = s * 0.5f;

float t;
if (s != 0.0)
t = 0.5f / s;
else
t = s;

q[3] = (R[k][j] - R[j][k]) * t;
q[j] = (R[j][i] + R[i][j]) * t;
q[k] = (R[k][i] + R[i][k]) * t;
}
}

Let’s pause for a moment to consider notational conventions. In this book,
we write our quaternions like this: [ x y z w ]. This differs from the [ w x y z ]
convention found in many academic papers on quaternions as an extension of
the complex numbers. Our convention arises from an effort to be consistent
with the common practice of writing homogeneous vectors as [ x y z 1 ] (with
the w = 1 at the end). The academic convention arises from the parallels be-
tween quaternions and complex numbers. Regular two-dimensional complex
numbers are typically written in the form c = a + jb, and the corresponding
quaternion notation is q = w + ix + jy + kz. So be careful out there—make
sure you know which convention is being used before you dive into a paper
head first!

### 5.4.5 Rotational Linear Interpolation

Rotational interpolation has many applications in the animation, dynamics
and camera systems of a game engine. With the help of quaternions, rotations
can be easily interpolated just as vectors and points can.

The easiest and least computationally intensive approach is to perform a
four-dimensional vector LERP on the quaternions you wish to interpolate.
Given two quaternions qA and qB representing rotations A and B, we can find
an intermediate rotation qLERP that is β percent of the way from A to B as fol-


<!-- source-pdf-page: 420 -->
> Visual fallback for diagrams/images: [PDF page 420](../../../visual_pages/page_0420.jpg)

qB

qLERP = LERP(qA, qB, 0.4)

qA

Figure 5.23. Linear interpolation (LERP) between two quaternions qA and qB.

lows:

qLERP = LERP(qA, qB, β) = (1 −β)qA + βqB

|(1 −β)qA + βqB|



T





(1 −β)qAx + βqBx
(1 −β)qAy + βqBy
(1 −β)qAz + βqBz
(1 −β)qAw + βqBw









.





= normalize

Notice that the resultant interpolated quaternion had to be renormalized.
This is necessary because the LERP operation does not preserve a vector’s
length in general.
Geometrically, qLERP = LERP(qA, qB, β) is the quaternion whose orienta-
tion lies β percent of the way from orientation A to orientation B, as shown (in
two dimensions for clarity) in Figure 5.23. Mathematically, the LERP opera-
tion results in a weighted average of the two quaternions, with weights (1 −β)
and β (notice that these two weights sum to 1).

5.4.5.1
Spherical Linear Interpolation

The problem with the LERP operation is that it does not take account of the fact
that quaternions are really points on a four-dimensional hypersphere. A LERP
effectively interpolates along a chord of the hypersphere, rather than along the
surface of the hypersphere itself. This leads to rotation animations that do not
have a constant angular speed when the parameter β is changing at a constant
rate. The rotation will appear slower at the end points and faster in the middle
of the animation.
To solve this problem, we can use a variant of the LERP operation known
as spherical linear interpolation, or SLERP for short. The SLERP operation uses
sines and cosines to interpolate along a great circle of the 4D hypersphere,


<!-- source-pdf-page: 421 -->
> Visual fallback for diagrams/images: [PDF page 421](../../../visual_pages/page_0421.jpg)

qB

qLERP = LERP(qA, qB, 0.4)

qSLERP = SLERP(qA, qB, 0.4)

0.4 along arc

0.4 along chord

qA

Figure 5.24. Spherical linear interpolation along a great circle arc of a 4D hypersphere.

rather than along a chord, as shown in Figure 5.24. This results in a constant
angular speed when β varies at a constant rate.
The formula for SLERP is similar to the LERP formula, but the weights
(1 −β) and β are replaced with weights wp and wq involving sines of the angle
between the two quaternions.

SLERP(p, q, β) = wpp + wqq,

where

wp = sin(1 −β)θ

sin θ
,

wq = sin βθ

sin θ .

The cosine of the angle between any two unit-length quaternions can be
found by taking their four-dimensional dot product. Once we know cos θ, we
can calculate the angle θ and the various sines we need quite easily:

cos θ = p · q = pxqx + pyqy + pzqz + pwqw;

θ = cos−1(p · q).

5.4.5.2
To SLERP or Not to SLERP (That’s Still the Question)

The jury is still out on whether or not to use SLERP in a game engine. Jonathan
Blow wrote a great article positing that SLERP is too expensive, and LERP’s
quality is not really that bad—therefore, he suggests, we should understand
SLERP but avoid it in our game engines (see http://number-none.com/pro
duct/Understanding%20Slerp,%20Then%20Not%20Using%20It/index.html).


<!-- source-pdf-page: 422 -->

On the other hand, some of my colleagues at Naughty Dog have found that
a good SLERP implementation performs nearly as well as LERP. (For exam-
ple, on the PS3’s SPUs, Naughty Dog’s Ice team’s implementation of SLERP
takes 20 cycles per joint, while its LERP implementation takes 16.25 cycles per
joint.) Therefore, I’d personally recommend that you profile your SLERP and
LERP implementations before making any decisions. If the performance hit
for SLERP isn’t unacceptable, I say go for it, because it may result in slightly
better-looking animations. But if your SLERP is slow (and you cannot speed
it up, or you just don’t have the time to do so), then LERP is usually good
enough for most purposes.

## 5.5 Comparison of Rotational Representations

We’ve seen that rotations can be represented in quite a few different ways. This
section summarizes the most common rotational representations and outlines
their pros and cons. No one representation is ideal in all situations. Using the
information in this section, you should be able to select the best representation
for a particular application.

### 5.5.1 Euler Angles

We briefly explored Euler angles in Section 5.3.9.1. A rotation represented via
Euler angles consists of three scalar values: yaw, pitch and roll. These quanti-
ties are sometimes represented by a 3D vector
[
θY
θP
θR
]
.
The benefits of this representation are its simplicity, its small size (three
floating-point numbers) and its intuitive nature—yaw, pitch and roll are easy
to visualize. You can also easily interpolate simple rotations about a single
axis. For example, it’s trivial to find intermediate rotations between two dis-
tinct yaw angles by linearly interpolating the scalar θY. However, Euler angles
cannot be interpolated easily when the rotation is about an arbitrarily oriented
axis.
In addition, Euler angles are prone to a condition known as gimbal lock.
This occurs when a 90-degree rotation causes one of the three principal axes
to “collapse” onto another principal axis. For example, if you rotate by 90
degrees about the x-axis, the y-axis collapses onto the z-axis. This prevents
any further rotations about the original y-axis, because rotations about y and
z have effectively become equivalent.
Another problem with Euler angles is that the order in which the rotations
are performed around each axis matters. The order could be PYR, YPR, RYP
and so on, and each ordering may produce a different composite rotation. No


<!-- source-pdf-page: 423 -->

one standard rotation order exists for Euler angles across all disciplines (al-
though certain disciplines do follow specific conventions). So the rotation an-
gles
[θY
θP
θR
]
do not uniquely define a particular rotation—you need to
know the rotation order to interpret these numbers properly.
A final problem with Euler angles is that they depend upon the mapping
from the x-, y- and z-axes onto the natural front, left/right and up directions for
the object being rotated. For example, yaw is always defined as rotation about
the up axis, but without additional information we cannot tell whether this
corresponds to a rotation about x, y or z.

### 5.5.2 3 × 3 Matrices

A 3 × 3 matrix is a convenient and effective rotational representation for a
number of reasons. It does not suffer from gimbal lock, and it can repre-
sent arbitrary rotations uniquely. Rotations can be applied to points and vec-
tors in a straightforward manner via matrix multiplication (i.e., a series of dot
products). Most CPUs and all GPUs now have built-in support for hardware-
accelerated dot products and matrix multiplication. Rotations can also be re-
versed by finding an inverse matrix, which for a pure rotation matrix is the
same thing as finding the transpose—a trivial operation. And 4 × 4 matrices
offer a way to represent arbitrary affine transformations—rotations, transla-
tions and scaling—in a totally consistent way.
However, rotation matrices are not particularly intuitive. Looking at a big
table of numbers doesn’t help one picture the corresponding transformation
in three-dimensional space. Also, rotation matrices are not easily interpolated.
Finally, a rotation matrix takes up a lot of storage (nine floating-point numbers)
relative to Euler angles (three floats).

### 5.5.3 Axis + Angle

We can represent rotations as a unit vector, defining the axis of rotation plus
a scalar for the angle of rotation. This is known as an axis+angle representa-
tion, and it is sometimes denoted by the four-dimensional vector
[a
θ] =
[
ax
ay
az
θ
]
, where a is the axis of rotation and θ the angle in radians. In a
right-handed coordinate system, the direction of a positive rotation is defined
by the right-hand rule, while in a left-handed system, we use the left-hand rule
instead.
The benefits of the axis+angle representation are that it is reasonably intu-
itive and also compact. (It only requires four floating-point numbers, as op-
posed to the nine required for a 3 × 3 matrix.)


<!-- source-pdf-page: 424 -->

One important limitation of the axis+angle representation is that rotations
cannot be easily interpolated. Also, rotations in this format cannot be ap-
plied to points and vectors in a straightforward way—one needs to convert
the axis+angle representation into a matrix or quaternion first.

### 5.5.4 Quaternions

As we’ve seen, a unit-length quaternion can represent 3D rotations in a manner
analogous to the axis+angle representation. The primary difference between
the two representations is that a quaternion’s axis of rotation is scaled by the
sine of the half-angle of rotation, and instead of storing the angle in the fourth
component of the vector, we store the cosine of the half-angle.
The quaternion formulation provides two immense benefits over the axis
+angle representation. First, it permits rotations to be concatenated and ap-
plied directly to points and vectors via quaternion multiplication. Second, it
permits rotations to be easily interpolated via simple LERP or SLERP oper-
ations. Its small size (four floating-point numbers) is also a benefit over the
matrix formulation.

### 5.5.5 SRT Transformations

By itself, a quaternion can only represent a rotation, whereas a 4 × 4 matrix can
represent an arbitrary affine transformation (rotation, translation and scale).
When a quaternion is combined with a translation vector and a scale factor (either
a scalar for uniform scaling or a vector for nonuniform scaling), then we have a
viable alternative to the 4 × 4 matrix representation of affine transformations.
We sometimes call this an SRT transform, because it contains a scale factor, a
rotation quaternion and a translation vector. (It’s also sometimes called an
SQT, because the rotation is a quaternion.)

SRT =
[
s
q
t
]
(uniform scale s),
or
SRT =
[s
q
t]
(nonuniform scale vector s).

SRT transforms are widely used in computer animation because of their
smaller size (eight floats for uniform scale, or ten floats for nonuniform scale,
as opposed to the 12 floating-point numbers needed for a 4 × 3 matrix) and
their ability to be easily interpolated. The translation vector and scale factor
are interpolated via LERP, and the quaternion can be interpolated with either
LERP or SLERP.


<!-- source-pdf-page: 425 -->

### 5.5.6 Dual Quaternions

A rigid transformation is a transformation involving a rotation and a transla-
tion—a “corkscrew” motion. Such transformations are prevalent in animation
and robotics. A rigid transformation can be represented using a mathematical
object known as a dual quaternion. The dual quaternion representation offers a
number of benefits over the typical vector-quaternion representation. The key
benefit is that linear interpolation blending can be performed in a constant-
speed, shortest-path, coordinate-invariant manner, similar to using LERP for
translation vectors and SLERP for rotational quaternions (see Section 5.4.5.1),
but in a way that is easily generalizable to blends involving three or more trans-
forms.
A dual quaternion is like an ordinary quaternion, except that its four com-
ponents are dual numbers instead of regular real-valued numbers. A dual num-
ber can be written as the sum of a non-dual part and a dual part as follows:
ˆa = a + εb. Here ε is a magical number called the dual unit, defined in such a
way that ε2 = 0 (yet without ε itself being zero). This is analogous to the imag-
inary number j = √−1 used when writing a complex number as the sum of a
real and an imaginary part: c = a + jb.
Because each dual number can be represented by two real numbers (the
non-dual and dual parts, a and b), a dual quaternion can be represented by an
eight-element vector. It can also be represented as the sum of two ordinary
quaternions, where the second one is multiplied by the dual unit, as follows:
ˆq = qa + εqb.
A full discussion of dual numbers and dual quaternions is beyond our
scope here.
However, the excellent paper entitled, “Dual Quaternions for
Rigid Transformation Blending” by Kavan et al. outlines the theory and prac-
tice of using dual quaternions to represent rigid transformations—it is avail-
able online at https://bit.ly/2vjD5sz. Note that in this paper, a dual number
is written in the form ˆa = a0 + εaε, whereas I have used a + εb above to under-
score the similarity between dual numbers and complex numbers.1

### 5.5.7 Rotations and Degrees of Freedom

The term “degrees of freedom” (or DOF for short) refers to the number of mu-
tually independent ways in which an object’s physical state (position and ori-
entation) can change. You may have encountered the phrase “six degrees of

1Personally I would have preferred the symbol a1 over a0, so that a dual number would be
written ˆa = (1)a1 + (ε)aε. Just as when we plot a complex number in the complex plane, we can
think of the real unit as a “basis vector” along the real axis, and the dual unit ε as a “basis vector”
along the dual axis.


<!-- source-pdf-page: 426 -->

freedom” in fields such as mechanics, robotics and aeronautics. This refers to
the fact that a three-dimensional object (whose motion is not artificially con-
strained) has three degrees of freedom in its translation (along the x-, y- and
z-axes) and three degrees of freedom in its rotation (about the x-, y- and z-axes),
for a total of six degrees of freedom.
The DOF concept will help us to understand how different rotational rep-
resentations can employ different numbers of floating-point parameters, yet
all specify rotations with only three degrees of freedom. For example, Euler
angles require three floats, but axis+angle and quaternion representations use
four floats, and a 3 × 3 matrix takes up nine floats. How can these representa-
tions all describe 3-DOF rotations?
The answer lies in constraints. All 3D rotational representations employ
three or more floating-point parameters, but some representations also have
one or more constraints on those parameters. The constraints indicate that the
parameters are not independent—a change to one parameter induces changes
to the other parameters in order to maintain the validity of the constraint(s).
If we subtract the number of constraints from the number of floating-point
parameters, we arrive at the number of degrees of freedom—and this number
should always be three for a 3D rotation:

NDOF = Nparameters −Nconstraints.
(5.10)

The following list shows Equation (5.10) in action for each of the rotational
representations we’ve encountered in this book.

•
Euler Angles. 3 parameters −0 constraints = 3 DOF.

•
Axis+Angle. 4 parameters −1 constraint = 3 DOF.
Constraint: Axis is constrained to be unit length.

•
Quaternion. 4 parameters −1 constraint = 3 DOF.
Constraint: Quaternion is constrained to be unit length.

•
3 × 3 Matrix. 9 parameters −6 constraints = 3 DOF.
Constraints: All three rows and all three columns must be of unit length
(when treated as three-element vectors).

## 5.6 Other Useful Mathematical Objects

As game engineers, we will encounter a host of other mathematical objects
in addition to points, vectors, matrices and quaternions. This section briefly
outlines the most common of these.


<!-- source-pdf-page: 427 -->
> Visual fallback for diagrams/images: [PDF page 427](../../../visual_pages/page_0427.jpg)

t = 1
t = 2
t = 3

0

0

t = 0

t = –1

Figure 5.25. Parametric equation of a line.

Figure 5.26. Parametric equation of a ray.

### 5.6.1 Lines, Rays and Line Segments

An infinite line can be represented by a point P0 plus a unit vector u in the
direction of the line. A parametric equation of a line traces out every possible
point P along the line by starting at the initial point P0 and moving an arbitrary
distance t along the direction of the unit vector v. The infinitely large set of
points P becomes a vector function of the scalar parameter t:

P(t) = P0 + t u,
where −∞< t < ∞.
(5.11)

This is depicted in Figure 5.25.
A ray is a line that extends to infinity in only one direction. This is easily
expressed as P(t) with the constraint t ≥0, as shown in Figure 5.26.
A line segment is bounded at both ends by P0 and P1. It too can be repre-
sented by P(t), in either one of the following two ways (where L = P1 −P0,
L = |L| is the length of the line segment, and u = (1/L)L is a unit vector in
the direction of L):

1.
P(t) = P0 + t u, where 0 ≤t ≤L, or

2.
P(t) = P0 + t L, where 0 ≤t ≤1.

The latter format, depicted in Figure 5.27, is particularly convenient be-
cause the parameter t is normalized; in other words, t always goes from zero
to one, no matter which particular line segment we are dealing with. This
means we do not have to store the constraint L in a separate floating-point pa-
rameter; it is already encoded in the vector L = L u (which we have to store
anyway).

### 5.6.2 Spheres

Spheres are ubiquitous in game engine programming. A sphere is typically
defined as a center point C plus a radius r, as shown in Figure 5.28. This packs
nicely into a four-element vector,
[Cx
Cy
Cz
r]
. As we saw when we dis-
cussed SIMD vector processing, there are distinct benefits to being able to pack
data into a vector containing four 32-bit floats (i.e., a 128-bit package).


<!-- source-pdf-page: 428 -->
> Visual fallback for diagrams/images: [PDF page 428](../../../visual_pages/page_0428.jpg)

1

1
0

0

Figure 5.27. Parametric equation of a line segment, with normalized parameter t.

Figure 5.28. Point-radius representation of a sphere.

### 5.6.3 Planes

A plane is a 2D surface in 3D space. As you may recall from high-school alge-
bra, the equation of a plane is often written as follows:

Ax + By + Cz + D = 0.

This equation is satisfied only for the locus of points P =
[x
y
z]
that lie on
the plane.
Planes can be represented by a point P0 and a unit vector n that is nor-
mal to the plane. This is sometimes called point-normal form, as depicted in
Figure 5.29.

It’s interesting to note that when the parameters A, B and C from the tra-
ditional plane equation are interpreted as a 3D vector, that vector lies in the
direction of the plane normal. If the vector
[A
B
C]
is normalized to unit
length, then the normalized vector
[a
b
c] = n, and the normalized param-
eter d = D/
√

Figure 5.29. A plane in
point-normal form.

A2 + B2 + C2 is just the distance from the plane to the origin.
The sign of d is positive if the plane’s normal vector n is pointing toward the
origin (i.e., the origin is on the “front” side of the plane) and negative if the
normal is pointing away from the origin (i.e., the origin is “behind” the plane).


<!-- source-pdf-page: 429 -->

Another way of looking at this is that the plane equation and the point-
normal form are really just two ways of writing the same equation. Imagine
testing whether or not an arbitrary point P =
[x
y
z]
lies on the plane. To
do this, we find the signed distance from point P to the origin along the normal
n =
[a
b
c]
, and if this signed distance is equal to the signed distance d =
−n · P0 from the plane from the origin, then P must lie on the plane. So let’s
set them equal and expand some terms:

(signed distance P to origin) = (signed distance plane to origin)
n · P = n · P0
n · P −n · P0 = 0

ax + by + cz −n · P0 = 0
ax + by + cz + d = 0.
(5.12)

Equation (5.12) only holds when the point P lies on the plane. But what
happens when the point P does not lie on the plane? In this case, the left-
hand side of the plane equation (ax + by + cz, which is equal to n · P) tells how
far “off” the point is from being on the plane. This expression calculates the
difference between the distance from P to the origin and the distance from the
plane to the origin. In other words, the left-hand side of Equation (5.12) gives
us the perpendicular distance h between the point and the plane! This is just
another way to write Equation (5.2) from Section 5.2.4.7.

h = (P −P0) · n;
h = ax + by + cz + d.
(5.13)

A plane can actually be packed into a four-element vector, much like a
sphere can. To do so, we observe that to describe a plane uniquely, we need
only the normal vector n =
[
a
b
c
]
and the distance from the origin d. The
four-element vector L =
[
n
d
] =
[
a
b
c
d
]
is a compact and convenient
way to represent and store a plane in memory. Note that when P is written in
homogeneous coordinates with w = 1, the equation (L · P) = 0 is yet another
way of writing (n · P) = −d. These equations are satisfied for all points P that
lie on the plane L.
Planes defined in four-element vector form can be easily transformed from
one coordinate space to another. Given a matrix MA→B that transforms points
and (non-normal) vectors from space A to space B, we already know that to
transform a normal vector such as the plane’s n vector, we need to use the in-
verse transpose of that matrix, (M−1
A→B)T. So it shouldn’t be a big surprise to
learn that applying the inverse transpose of a matrix to a four-element plane
vector L will, in fact, correctly transform that plane from space A to space B. We
won’t derive or prove this result any further here, but a thorough explanation
of why this little “trick” works is provided in Section 4.2.3 of [32].


<!-- source-pdf-page: 430 -->
> Visual fallback for diagrams/images: [PDF page 430](../../../visual_pages/page_0430.jpg)

### 5.6.4 Axis-Aligned Bounding Boxes (AABB)

An axis-aligned bounding box (AABB) is a 3D cuboid whose six rectangular
faces are aligned with a particular coordinate frame’s mutually orthogonal
axes. As such, an AABB can be represented by a six-element vector containing
the minimum and maximum coordinates along each of the 3 principal axes,
[ xmin, ymin, zmin, xmax, ymax, zmax ], or two points Pmin and Pmax.
This simple representation allows for a particularly convenient and inex-
pensive method of testing whether a point P is inside or outside any given
AABB. We simply test if all of the following conditions are true:

Px ≥xmin and Px ≤xmax and
Py ≥ymin and Py ≤ymax and
Pz ≥zmin and Pz ≤zmax.

Because intersection tests are so speedy, AABBs are often used as an “early
out” collision check; if the AABBs of two objects do not intersect, then there is
no need to do a more detailed (and more expensive) collision test.

### 5.6.5 Oriented Bounding Boxes (OBB)

An oriented bounding box (OBB) is a cuboid that has been oriented so as to
align in some logical way with the object it bounds. Usually an OBB aligns
with the local-space axes of the object. Hence, it acts like an AABB in local
space, although it may not necessarily align with the world-space axes.
Various techniques exist for testing whether or not a point lies within an
OBB, but one common approach is to transform the point into the OBB’s
“aligned” coordinate system and then use an AABB intersection test as pre-
sented above.

### 5.6.6 Frusta

As shown in Figure 5.30, a frustum is a group of six planes that define a trun-
cated pyramid shape. Frusta are commonplace in 3D rendering because they
conveniently define the viewable region of the 3D world when rendered via a
perspective projection from the point of view of a virtual camera. Four of the
planes bound the edges of the screen space, while the other two planes repre-
sent the the near and far clipping planes (i.e., they define the minimum and
maximum z coordinates possible for any visible point).
One convenient representation of a frustum is as an array of six planes, each
of which is represented in point-normal form (i.e., one point and one normal
vector per plane).

Right

Bottom

Figure 5.30. A frustum.


<!-- source-pdf-page: 431 -->

Testing whether a point lies inside a frustum is a bit involved, but the basic
idea is to use dot products to determine whether the point lies on the front or
back side of each plane. If it lies inside all six planes, it is inside the frustum.
A helpful trick is to transform the world-space point being tested by apply-
ing the camera’s perspective projection to it. This takes the point from world
space into a space known as homogeneous clip space. In this space, the frustum
is just an axis-aligned cuboid (AABB). This permits much simpler in/out tests
to be performed.

### 5.6.7 Convex Polyhedral Regions

A convex polyhedral region is defined by an arbitrary set of planes, all with nor-
mals pointing inward (or outward). The test for whether a point lies inside
or outside the volume defined by the planes is relatively straightforward; it
is similar to a frustum test, but with possibly more planes. Convex regions
are very useful for implementing arbitrarily shaped trigger regions in games.
Many engines employ this technique; for example, the Quake engine’s ubiq-
uitous brushes are just volumes bounded by planes in exactly this way.

## 5.7 Random Number Generation

Random numbers are ubiquitous in game engines, so it behooves us to have a
brief look at the two most common random number generators (RNG), the lin-
ear congruential generator and the Mersenne Twister. It’s important to realize
that random number generators don’t actually generate random numbers—
they merely produce a complex, but totally deterministic, predefined sequence
of values. For this reason, we call the sequences they produce pseudorandom,
and technically speaking we should really call them “pseudorandom number
generators” (PRNG). What differentiates a good generator from a bad one is
how long the sequence of numbers is before it repeats (its period), and how
well the sequences hold up under various well-known randomness tests.

### 5.7.1 Linear Congruential Generators

Linear congruential generators are a very fast and simple way to generate a
sequence of pseudorandom numbers. Depending on the platform, this algo-
rithm is sometimes used in the C standard library’s rand() function. How-
ever, your mileage may vary, so don’t count on rand() being based on any
particular algorithm. If you want to be sure, you’ll be better off implementing
your own random number generator.


<!-- source-pdf-page: 432 -->

The linear congruential algorithm is explained in detail in the book Numer-
ical Recipes in C, so I won’t go into the details of it here.
What I will say is that this random number generator does not produce par-
ticularly high-quality pseudorandom sequences. Given the same initial seed
value, the sequence is always exactly the same. The numbers produced do
not meet many of the criteria widely accepted as desirable, such as a long pe-
riod, low- and high-order bits that have similarly long periods, and absence of
sequential or spatial correlation between the generated values.

### 5.7.2 Mersenne Twister

The Mersenne Twister pseudorandom number generator algorithm was de-
signed specifically to improve upon the various problems of the linear congru-
ential algorithm. Wikipedia provides the following description of the benefits
of the algorithm:

1.
It was designed to have a colossal period of 219937 −1 (the creators of the
algorithm proved this property). In practice, there is little reason to use
larger ones, as most applications do not require 219937 unique combina-
tions (219937 ≈4.3 × 106001).

2.
It has a very high order of dimensional equidistribution. Note that this
means, by default, that there is negligible serial correlation between suc-
cessive values in the output sequence.

3.
It passes numerous tests for statistical randomness, including the strin-
gent Diehard tests.

4.
It is fast.

Various implementations of the Twister are available on the web, includ-
ing a particularly cool one that uses SIMD vector instructions for an extra
speed boost, called SFMT (SIMD-oriented fast Mersenne Twister). SFMT can
be downloaded from http://www.math.sci.hiroshima-u.ac.jp/~m-mat/MT/
SFMT/index.html.

### 5.7.3 Mother-of-All, Xorshift and KISS99

In 1994, George Marsaglia, a computer scientist and mathematician best
known for developing the Diehard battery of tests of randomness (http://
www.stat.fsu.edu/pub/diehard), published a pseudorandom number gener-
ation algorithm that is much simpler to implement and runs faster than the
Mersenne Twister algorithm. He claimed that it could produce a sequence


<!-- source-pdf-page: 433 -->

of 32-bit pseudorandom numbers with a period of non-repetition of 2250. It
passed all of the Diehard tests and still stands today as one of the best pseu-
dorandom number generators for high-speed applications. He called his al-
gorithm the Mother of All Pseudorandom Number Generators, because it seemed
to him to be the only random number generator one would ever need.
Later, Marsaglia published another generator called Xorshift, which is be-
tween Mersenne and Mother-of-All in terms of randomness, but runs slightly
faster than Mother.
Marsaglia also developed a series of random number generators that are
collectively called KISS (Keep It Simple Stupid). The KISS99 algorithm is a
popular choice, because it has a large period (2123) and passes all tests in the
TestU01 test suite (https://bit.ly/2r5FmSP).
You can read about George Marsaglia at http://en.wikipedia.org/wiki/
George_Marsaglia, and about the Mother-of-All generator at ftp://ftp.forth.
org/pub/C/mother.c and at http://www.agner.org/random. You can down-
load a PDF of George’s paper on Xorshift at http://www.jstatsoft.org/v08/
i14/paper.

### 5.7.4 PCG

Another very popular and high-quality family of pseudorandom number gen-
erators is called PCG. It works by combining a congruential generator for its
state transitions (the “CG” in PCG) with permutation functions to generate its
output (the “P” in PCG). You can read more about this family of PRNGs at
http://www.pcg-random.org/.


<!-- source-pdf-page: 434 -->
> Visual fallback for diagrams/images: [PDF page 434](../../../visual_pages/page_0434.jpg)

Part II
Low-Level
Engine Systems


<!-- source-pdf-page: 435 -->
> Visual fallback for diagrams/images: [PDF page 435](../../../visual_pages/page_0435.jpg)

Taylor & Francis

Taylor & Francis Group
http://taylorandfrancis.com
