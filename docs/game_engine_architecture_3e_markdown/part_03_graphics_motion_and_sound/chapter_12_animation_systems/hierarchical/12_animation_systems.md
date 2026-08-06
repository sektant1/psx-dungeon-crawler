# 12 Animation Systems

> Source PDF pages: 740-835
> Extraction mode: PyMuPDF text blocks; line breaks and printed hyphenation are preserved.

<!-- source-pdf-page: 740 -->

T
he majority of modern 3D games revolve around characters—often human
or humanoid, sometimes animal or alien. Characters are unique because
they need to move in a fluid, organic way. This poses a host of new tech-
nical challenges, over and above what is required to simulate and animate
rigid objects like vehicles, projectiles, soccer balls and Tetris pieces. The task
of imbuing characters with natural-looking motion is handled by an engine
component known as the character animation system.
As we’ll see, an animation system gives game designers a powerful suite
of tools that can be applied to non-characters as well as characters. Any game
object that is not 100% rigid can take advantage of the animation system. So
whenever you see a vehicle with moving parts, a piece of articulated machin-
ery, trees waving gently in the breeze or even an exploding building in a game,
chances are good that the object makes at least partial use of the game engine’s
animation system.

## 12.1 Types of Character Animation

Character animation technology has come a long way since Donkey Kong. At
first, games employed very simple techniques to provide the illusion of lifelike
movement. As game hardware improved, more-advanced techniques became


<!-- source-pdf-page: 741 -->
> Visual fallback for diagrams/images: [PDF page 741](../../../visual_pages/page_0741.jpg)

feasible in real time. Today, game designers have a host of powerful animation
methods at their disposal. In this section, we’ll take a brief look at the evolution
of character animation and outline the three most-common techniques used in
modern game engines.

### 12.1.1 Cel Animation

The precursor to all game animation techniques is known as traditional anima-
tion, or hand-drawn animation. This is the technique used in the earliest ani-
mated cartoons. The illusion of motion is produced by displaying a sequence
of still pictures known as frames in rapid succession. Real-time 3D rendering
can be thought of as an electronic form of traditional animation, in that a se-
quence of still full-screen images is presented to the viewer over and over to
produce the illusion of motion.

Cel animation is a specific type of traditional animation. A cel is a transpar-
ent sheet of plastic on which images can be painted or drawn. An animated
sequence of cels can be placed on top of a fixed background painting or draw-
ing to produce the illusion of motion without having to redraw the static back-
ground over and over.

The electronic equivalent to cel animation is a technology known as sprite
animation. A sprite is a small bitmap that can be overlaid on top of a full-screen
background image without disrupting it, often drawn with the aid of special-
ized graphics hardware. Hence, a sprite is to 2D game animation what a cel
was to traditional animation. This technique was a staple during the 2D game
era. Figure 12.1 shows the famous sequence of sprite bitmaps that were used
to produce the illusion of a running humanoid character in almost every Mat-
tel Intellivision game ever made. The sequence of frames was designed so that
it animates smoothly even when it is repeated indefinitely—this is known as
a looping animation. This particular animation would be called a run cycle in
modern parlance, because it makes the character appear to be running. Char-
acters typically have a number of looping animation cycles, including various
idle cycles, a walk cycle and a run cycle.

Figure 12.1. The sequence of sprite bitmaps used in most Intellivision games.


<!-- source-pdf-page: 742 -->
> Visual fallback for diagrams/images: [PDF page 742](../../../visual_pages/page_0742.jpg)

### 12.1.2 Rigid Hierarchical Animation

Early 3D games like Doom continued to make use of a sprite-like animation
system: Its monsters were nothing more than camera-facing quads, each of
which displayed a sequence of texture bitmaps (known as an animated texture)
to produce the illusion of motion. And this technique is still used today for
low-resolution and/or distant objects—for example crowds in a stadium, or
hordes of soldiers fighting a distant battle in the background. But for high-
quality foreground characters, 3D graphics brought with it the need for im-
proved character animation methods.
The earliest approach to 3D character animation is a technique known as
rigid hierarchical animation. In this approach, a character is modeled as a col-
lection of rigid pieces. A typical breakdown for a humanoid character might
be pelvis, torso, upper arms, lower arms, upper legs, lower legs, hands, feet
and head. The rigid pieces are constrained to one another in a hierarchical
fashion, analogous to the manner in which a mammal’s bones are connected
at the joints. This allows the character to move naturally. For example, when
the upper arm is moved, the lower arm and hand will automatically follow it.
A typical hierarchy has the pelvis at the root, with the torso and upper legs as
its immediate children and so on as shown below:

Pelvis
Torso
UpperRightArm
LowerRightArm
RightHand
UpperLeftArm
UpperLeftArm
LeftHand
Head
UpperRightLeg
LowerRightLeg
RightFoot
UpperLeftLeg
UpperLeftLeg
LeftFoot

The big problem with the rigid hierarchy technique is that the behavior of
the character’s body is often not very pleasing due to “cracking” at the joints.
This is illustrated in Figure 12.2. Rigid hierarchical animation works well for
robots and machinery that really are constructed of rigid parts, but it breaks
down under scrutiny when applied to “fleshy” characters.


<!-- source-pdf-page: 743 -->
> Visual fallback for diagrams/images: [PDF page 743](../../../visual_pages/page_0743.jpg)

### 12.1.3 Per-Vertex Animation and Morph Targets

Rigid hierarchical animation tends to look unnatural because it is rigid. What
we really want is a way to move individual vertices so that triangles can stretch
to produce more natural-looking motion.

One way to achieve this is to apply a brute-force technique known as per-
vertex animation. In this approach, the vertices of the mesh are animated by
an artist, and motion data is exported, which tells the game engine how to
move each vertex at runtime. This technique can produce any mesh deforma-
tion imaginable (limited only by the tessellation of the surface). However, it
is a data-intensive technique, since time-varying motion information must be
stored for each vertex of the mesh. For this reason, it has little application to
real-time games.

A variation on this technique known as morph target animation is used in
some real-time games. In this approach, the vertices of a mesh are moved by
an animator to create a relatively small set of fixed, extreme poses. Animations
are produced by blending between two or more of these fixed poses at runtime.
The position of each vertex is calculated using a simple linear interpolation
(LERP) between the vertex’s positions in each of the extreme poses.

The morph target technique is often used for facial animation, because the
human face is an extremely complex piece of anatomy, driven by roughly 50
muscles. Morph target animation gives an animator full control over every
vertex of a facial mesh, allowing him or her to produce both subtle and extreme
movements that approximate the musculature of the face well. Figure 12.3
shows a set of facial morph targets.

As computing power continues to increase, some studios are using jointed
facial rigs containing hundreds of joints as an alternative to morph targets.
Other studios combine the two techniques, using jointed rigs to achieve the
primary pose of the face and then applying small tweaks via morph targets.

Figure 12.2. Cracking at the joints is a big problem in rigid hierarchical animation.


<!-- source-pdf-page: 744 -->
> Visual fallback for diagrams/images: [PDF page 744](../../../visual_pages/page_0744.jpg)

### 12.1.4 Skinned Animation

As the capabilities of game hardware improved further, an animation technol-
ogy known as skinned animation was developed. This technique has many of
the benefits of per-vertex and morph target animation—permitting the trian-
gles of an animated mesh to deform. But it also enjoys the much more effi-
cient performance and memory usage characteristics of rigid hierarchical an-
imation. It is capable of producing reasonably realistic approximations to the
movement of skin and clothing.

Skinned animation was first used by games like Super Mario 64, and it is
still the most prevalent technique in use today, both by the game industry and
the feature film industry. A host of famous modern game and movie char-
acters, including the dinosaurs from Jurrassic Park, Solid Snake (Metal Gear
Solid 4), Gollum (Lord of the Rings), Nathan Drake (Uncharted), Buzz Lightyear
(Toy Story), Marcus Fenix (Gears of War) and Joel (The Last of Us) were all ani-
mated, in whole or in part, using skinned animation techniques. The remain-
der of this chapter will be devoted primarily to the study of skinned/skeletal
animation.

In skinned animation, a skeleton is constructed from rigid “bones,” just as
in rigid hierarchical animation. However, instead of rendering the rigid pieces
on-screen, they remain hidden. A smooth continuous triangle mesh called a
skin is bound to the joints of the skeleton; its vertices track the movements of
the joints. Each vertex of the skin mesh can be weighted to multiple joints, so
the skin can stretch in a natural way as the joints move.

In Figure 12.4, we see Crank the Weasel, a game character designed by
Eric Browning for Midway Home Entertainment in 2001. Crank’s outer skin
is composed of a mesh of triangles, just like any other 3D model. However,
inside him we can see the rigid bones and joints that make his skin move.

Figure 12.3. A set of facial morph targets for the Ellie character in The Last of Us: Remastered
(© 2014/™SIE. Created and developed by Naughty Dog, PlayStation 4).


<!-- source-pdf-page: 745 -->
> Visual fallback for diagrams/images: [PDF page 745](../../../visual_pages/page_0745.jpg)

### 12.1.5 Animation Methods as Data Compression Techniques

The most flexible animation system conceivable would give the animator con-
trol over literally every infinitesimal point on an object’s surface. Of course,
animating like this would result in an animation that contains a potentially
infinite amount of data! Animating the vertices of a triangle mesh is a simpli-
fication of this ideal—in effect, we are compressing the amount of information
needed to describe an animation by restricting ourselves to moving only the
vertices. (Animating a set of control points is the analog of vertex animation
for models constructed out of higher-order patches.) Morph targets can be
thought of as an additional level of compression, achieved by imposing addi-
tional constraints on the system—vertices are constrained to move only along
linear paths between a fixed number of predefined vertex positions. Skeletal
animation is just another way to compress vertex animation data by imposing
constraints. In this case, the motions of a relatively large number of vertices
are constrained to follow the motions of a relatively small number of skeletal
joints.

When considering the trade-offs between various animation techniques, it
can be helpful to think of them as compression methods, analogous in many
respects to video compression techniques. We should generally aim to select
the animation method that provides the best compression without producing
unacceptable visual artifacts. Skeletal animation provides the best compres-
sion when the motion of a single joint is magnified into the motions of many
vertices. A character’s limbs act like rigid bodies for the most part, so they can

Figure 12.4. Eric Browning’s Crank the Weasel character, with internal skeletal structure.


<!-- source-pdf-page: 746 -->
> Visual fallback for diagrams/images: [PDF page 746](../../../visual_pages/page_0746.jpg)

Figure 12.5. The pelvis joint of this character connects to four other joints (tail, spine and two legs),
and so it produces four bones.

be moved very efficiently with a skeleton. However, the motion of a face tends
to be much more complex, with the motions of individual vertices being more
independent. To convincingly animate a face using the skeletal approach, the
required number of joints approaches the number of vertices in the mesh, thus
diminishing its effectiveness as a compression technique. This is one reason
why morph target techniques are often favored over the skeletal approach for
facial animation. (Another common reason is that morph targets tend to be a
more natural way for animators to work.)

## 12.2 Skeletons

A skeleton is comprised of a hierarchy of rigid pieces known as joints. In the
game industry, we often use the terms “joint” and “bone” interchangeably, but
the term bone is actually a misnomer. Technically speaking, the joints are the
objects that are directly manipulated by the animator, while the bones are sim-
ply the empty spaces between the joints. As an example, consider the pelvis
joint in the Crank the Weasel character model. It is a single joint, but because it
connects to four other joints (the tail, the spine and the left and right hip joints),
this one joint appears to have four bones sticking out of it. This is shown in
more detail in Figure 12.5. Game engines don’t care a whip about bones—
only the joints matter. So whenever you hear the term “bone” being used in
the industry, remember that 99% of the time we are actually speaking about
joints.


<!-- source-pdf-page: 747 -->
> Visual fallback for diagrams/images: [PDF page 747](../../../visual_pages/page_0747.jpg)

### 12.2.1 The Skeleal Hierarchy

As we’ve mentioned, the joints in a skeleton form a hierarchy or tree structure.
One joint is selected as the root, and all other joints are its children, grandchil-
dren and so on. A typical joint hierarchy for skinned animation looks almost
identical to a typical rigid hierarchy. For example, a humanoid character’s
joint hierarchy might look something like the one depicted in Figure 12.6.
We usually assign each joint an index from 0 to N −1. Because each joint
has one and only one parent, the hierarchical structure of a skeleton can be
fully described by storing the index of its parent with each joint. The root joint
has no parent, so its parent index is usually set to an invalid value such as
−1.

### 12.2.2 Representing a Skeleton in Memory

A skeleton is usually represented by a small top-level data structure that con-
tains an array of data structures for the individual joints. The joints are usually
listed in an order that ensures a child joint will always appear after its parent
in the array. This implies that joint zero is always the root of the skeleton.
Joint indices are usually used to refer to joints within animation data struc-
tures. For example, a child joint typically refers to its parent joint by specifying
its index. Likewise, in a skinned triangle mesh, a vertex refers to the joint or
joints to which it is bound by index. This is much more efficient than referring
to joints by name, both in terms of the amount of storage required (a joint in-
dex can be 8 bits wide, as long as we are willing to accept a maximum of 256
joints per skeleton) and in terms of the amount of time it takes to look up a
referenced joint (we can use the joint index to jump immediately to a desired
joint in the array).
Each joint data structure typically contains the following information:

Figure 12.6.
Example
of a skeletal hier-
archy,
as it would
appear in Maya’s Hy-
pergraph
Hierarchy
view.

•
The name of the joint, either as a string or a hashed 32-bit string id.

•
The index of the joint’s parent within the skeleton.

•
The inverse bind pose transform of the joint. The bind pose of a joint is the
position, orientation and scale of that joint at the time it was bound to the
vertices of the skin mesh. We usually store the inverse of this transforma-
tion for reasons we’ll explore in more depth in the following sections.

A typical skeleton data structure might look something like this:

struct Joint
{
Matrix4x3
m_invBindPose; // inverse bind pose


<!-- source-pdf-page: 748 -->
> Visual fallback for diagrams/images: [PDF page 748](../../../visual_pages/page_0748.jpg)

// transform
const char* m_name;
// human-readable joint
// name
U8
m_iParent;
// parent index or 0xFF
// if root
};

struct Skeleton
{
U32
m_jointCount;
// number of joints
Joint*
m_aJoint;
// array of joints
};

## 12.3 Poses

No matter what technique is used to produce an animation, be it cel-based,
rigid hierarchical or skinned/skeletal, every animation takes place over time.
A character is imbued with the illusion of motion by arranging the character’s
body into a sequence of discrete, still poses and then displaying those poses
in rapid succession, usually at a rate of 30 or 60 poses per second. (Actually, as
we’ll see in Section 12.4.1.1, we often interpolate between adjacent poses rather
than displaying a single pose verbatim.) In skeletal animation, the pose of the
skeleton directly controls the vertices of the mesh, and posing is the animator’s
primary tool for breathing life into her characters. So clearly, before we can
animate a skeleton, we must first understand how to pose it.
A skeleton is posed by rotating, translating and possibly scaling its joints in
arbitrary ways. The pose of a joint is defined as the joint’s position, orientation
and scale, relative to some frame of reference. A joint pose is usually repre-
sented by a 4 × 4 or 4 × 3 matrix, or by an SRT data structure (scale, quaternion
rotation and vector translation). The pose of a skeleton is just the set of all of
its joints’ poses and is normally represented as a simple array of matrices or
SRTs.

### 12.3.1 Bind Pose

Two different poses of the same skeleton are shown in Figure 12.7. The pose
on the left is a special pose known as the bind pose, also sometimes called the
reference pose or the rest pose. This is the pose of the 3D mesh prior to being
bound to the skeleton (hence the name). In other words, it is the pose that the
mesh would assume if it were rendered as a regular, unskinned triangle mesh,
without any skeleton at all. The bind pose is also called the T-pose because


<!-- source-pdf-page: 749 -->
> Visual fallback for diagrams/images: [PDF page 749](../../../visual_pages/page_0749.jpg)

the character is usually standing with his feet slightly apart and his arms out-
stretched in the shape of the letter T. This particular stance is chosen because
it keeps the limbs away from the body and each other, making the process of
binding the vertices to the joints easier.

Figure 12.7. Two different poses of the same skeleton. The pose on the left is the special pose
known as bind pose.

### 12.3.2 Local Poses

A joint’s pose is most often specified relative to its parent joint. A parent-
relative pose allows a joint to move naturally. For example, if we rotate the
shoulder joint, but leave the parent-relative poses of the elbow, wrist and fin-
gers unchanged, the entire arm will rotate about the shoulder in a rigid man-
ner, as we’d expect. We sometimes use the term local pose to describe a parent-
relative pose. Local poses are almost always stored in SRT format, for reasons
we’ll explore when we discuss animation blending.
Graphically, many 3D authoring packages like Maya represent joints as
small spheres. However, a joint has a rotation and a scale, not just a translation,
so this visualization can be a bit misleading. In fact, a joint actually defines a
coordinate space no different in principle from the other spaces we’ve encoun-
tered (like model space, world space or view space). So it is best to picture a
joint as a set of Cartesian coordinate axes. Maya gives the user the option of
displaying a joint’s local coordinate axes—this is shown in Figure 12.8.
Mathematically, a joint pose is nothing more than an affine transformation.
The pose of joint j can be written as the 4 × 4 affine transformation matrix Pj,
which is comprised of a translation vector Tj, a 3 × 3 diagonal scale matrix


<!-- source-pdf-page: 750 -->
> Visual fallback for diagrams/images: [PDF page 750](../../../visual_pages/page_0750.jpg)

Figure 12.8. Every joint in a skeletal hierarchy deﬁnes a set of local coordinate space axes, known
as joint space.

Sj and a 3 × 3 rotation matrix Rj. The pose of an entire skeleton Pskel can be
written as the set of all poses Pj, where j ranges from 0 to N −1:

Pj =
[SjRj
0
Tj
1

]
,

Pskel =
{
Pj
}
N−1

j=0 .

12.3.2.1
Joint Scale

Some game engines assume that joints will never be scaled, in which case Sj
is simply omitted and assumed to be the identity matrix. Other engines make
the assumption that scale will be uniform if present, meaning it is the same
in all three dimensions. In this case, scale can be represented using a single
scalar value sj. Some engines even permit nonuniform scale, in which case scale
can be compactly represented by the three-element vector sj =
[sjx
sjy
sjz
]
.
The elements of the vector sj correspond to the three diagonal elements of the
3 × 3 scaling matrix Sj, so it is not really a vector per se. Game engines almost
never permit shear, so Sj is almost never represented by a full 3 × 3 scale/shear
matrix, although it certainly could be.
There are a number of benefits to omitting or constraining scale in a pose
or animation. Clearly using a lower-dimensional scale representation can save
memory. (Uniform scale requires a single floating-point scalar per joint per
animation frame, while nonuniform scale requires three floats, and a full 3 × 3
scale-shear matrix requires nine.) Restricting our engine to uniform scale has
the added benefit of ensuring that the bounding sphere of a joint will never


<!-- source-pdf-page: 751 -->

be transformed into an ellipsoid, as it could be when scaled in a nonuniform
manner. This greatly simplifies the mathematics of frustum and collision tests
in engines that perform such tests on a per-joint basis.

12.3.2.2
Representing a Joint Pose in Memory

As we mentioned above, joint poses are usually stored in SRT format. In C++,
such a data structure might look like this, where Q is first to ensure proper
alignment and optimal structure packing. (Can you see why?)

struct JointPose
{
Quaternion
m_rot;
// R
Vector3
m_trans; // T
F32
m_scale; // S (uniform scale only)
};

If nonuniform scale is permitted, we might define a joint pose like this instead:

struct JointPose
{
Quaternion
m_rot;
// R
Vector4
m_trans; // T
Vector4
m_scale; // S
};

The local pose of an entire skeleton can be represented as follows, where it
is understood that the array m_aLocalPose is dynamically allocated to con-
tain just enough occurrences of JointPose to match the number of joints in
the skeleton.

struct SkeletonPose
{
Skeleton*
m_pSkeleton;
// skeleton + num joints
JointPose* m_aLocalPose; // local joint poses
};

12.3.2.3
The Joint Pose as a Change of Basis

It’s important to remember that a local joint pose is specified relative to the
joint’s immediate parent. Any affine transformation can be thought of as trans-
forming points and vectors from one coordinate space to another. So when the
joint pose transform Pj is applied to a point or vector that is expressed in the co-
ordinate system of the joint j, the result is that same point or vector expressed
in the space of the parent joint.


<!-- source-pdf-page: 752 -->
> Visual fallback for diagrams/images: [PDF page 752](../../../visual_pages/page_0752.jpg)

As we’ve done in earlier chapters, we’ll adopt the convention of using sub-
scripts to denote the direction of a transformation. Since a joint pose takes
points and vectors from the child joint’s space (C) to that of its parent joint (P),
we can write it (PC→P)j. Alternatively, we can introduce the function p(j),
which returns the parent index of joint j, and write the local pose of joint j as
Pj→p(j).
On occasion we will need to transform points and vectors in the opposite
direction—from parent space into the space of the child joint. This transfor-
mation is just the inverse of the local joint pose. Mathematically, Pp(j)→j =
(
Pj→p(j)
)−1
.

### 12.3.3 Global Poses

Sometimes it is convenient to express a joint’s pose in model space or world
space. This is called a global pose. Some engines express global poses in matrix
form, while others use the SRT format.
Mathematically, the model-space pose of a joint (j →M) can be found
by walking the skeletal hierarchy from the joint in question all the way to the
root, multiplying the local poses (j →p(j)) as we go. Consider the hierarchy
shown in Figure 12.9. The parent space of the root joint is defined to be model
space, so p(0) ≡M. The model-space pose of joint J2 can therefore be written
as follows:
P2→M = P2→1P1→0P0→M.

Likewise, the model-space pose of joint J5 is just

P5→M = P5→4P4→3P3→0P0→M.

In general, the global pose (joint-to-model transform) of any joint j can be
written as follows:

Pj→M =
0
∏
i=j
Pi→p(i),
(12.1)

where it is understood that i becomes p(i) (the parent of joint i) after each
iteration in the product, and p(0) ≡M.

12.3.3.1
Representing a Global Pose in Memory

We can extend our SkeletonPose data structure to include the global pose
as follows, where again we dynamically allocate the m_aGlobalPose array
based on the number of joints in the skeleton:


<!-- source-pdf-page: 753 -->
> Visual fallback for diagrams/images: [PDF page 753](../../../visual_pages/page_0753.jpg)

1
2

0

yM

3
4
5

xM

Figure 12.9. A global pose can be calculated by walking the hierarchy from the joint in question
towards the root and model-space origin, concatenating the child-to-parent (local) transforms
of each joint as we go.

struct SkeletonPose
{
Skeleton*
m_pSkeleton;
// skeleton + num joints
JointPose* m_aLocalPose;
// local joint poses
Matrix44*
m_aGlobalPose; // global joint poses
};

## 12.4 Clips

In a film, every aspect of each scene is carefully planned out before any ani-
mations are created. This includes the movements of every character and prop
in the scene, and even the movements of the camera. This means that an en-
tire scene can be animated as one long, contiguous sequence of frames. And
characters need not be animated at all whenever they are off-camera.
Game animation is different. A game is an interactive experience, so one
cannot predict beforehand how the characters are going to move and behave.
The player has full control over his or her character and usually has partial
control over the camera as well. Even the decisions of the computer-driven
non-player characters are strongly influenced by the unpredictable actions of
the human player. As such, game animations are almost never created as long,
contiguous sequences of frames. Instead, a game character’s movement must
be broken down into a large number of fine-grained motions. We call these
individual motions animation clips, or sometimes just animations.
Each clip causes the character to perform a single well-defined action.
Some clips are designed to be looped—for example, a walk cycle or run cy-
cle. Others are designed to be played once—for example, throwing an object
or tripping and falling to the ground. Some clips affect the entire body of the
character—the character jumping into the air for instance. Other clips affect


<!-- source-pdf-page: 754 -->
> Visual fallback for diagrams/images: [PDF page 754](../../../visual_pages/page_0754.jpg)

t = 0
t = (0.4)T
t = T
t = (0.8)T

Figure 12.10. The local timeline of an animation showing poses at selected time indices. Images
courtesy of Naughty Dog, Inc., © 2014/™SIE.

only a part of the body—perhaps the character waving his right arm. The
movements of any one game character are typically broken down into liter-
ally thousands of clips.

The only exception to this rule is when game characters are involved in a
noninteractive portion of the game, known as an in-game cinematic (IGC), non-
interactive sequence (NIS) or full-motion video (FMV). Noninteractive sequences
are typically used to communicate story elements that do not lend themselves
well to interactive gameplay, and they are created in much the same way
computer-generated films are made (although they often make use of in-game
assets like character meshes, skeletons and textures). The terms IGC and NIS
typically refer to noninteractive sequences that are rendered in real time by
the game engine itself. The term FMV applies to sequences that have been
prerendered to an MP4, WMV or other type of movie file and are played back
at runtime by the engine’s full-screen movie player.

A variation on this style of animation is a semi-interactive sequence known
as a quick time event (QTE). In a QTE, the player must hit a button at the right
moment during an otherwise noninteractive sequence in order to see the suc-
cess animation and proceed; otherwise, a failure animation is played, and the
player must try again, possibly losing a life or suffering some other conse-
quence as a result.

### 12.4.1 The Local Timeline

We can think of every animation clip as having a local timeline, usually de-
noted by the independent variable t. At the start of a clip, t = 0, and at the
end, t = T, where T is the duration of the clip. Each unique value of the vari-
able t is called a time index. An example of this is shown in Figure 12.10.


<!-- source-pdf-page: 755 -->
> Visual fallback for diagrams/images: [PDF page 755](../../../visual_pages/page_0755.jpg)

12.4.1.1
Pose Interpolation and Continuous Time

It’s important to realize that the rate at which frames are displayed to the
viewer is not necessarily the same as the rate at which poses are created by
the animator. In both film and game animation, the animator almost never
poses the character every 1/30 or 1/60 of a second. Instead, the animator gen-
erates important poses known as key poses or key frames at specific times within
the clip, and the computer calculates the poses in between via linear or curve-
based interpolation. This is illustrated in Figure 12.11.
Because of the animation engine’s ability to interpolate poses (which we’ll
explore in depth later in this chapter), we can actually sample the pose of the
character at any time during the clip—not just on integer frame indices. In other
words, an animation clip’s timeline is continuous. In computer animation, the
time variable t is a real (floating-point) number, not an integer.
Film animation doesn’t take full advantage of the continuous nature of
the animation timeline, because its frame rate is locked at exactly 24, 30 or
60 frames per second. In film, the viewer sees the characters’ poses at frames
1, 2, 3 and so on—there’s never any need to find a character’s pose on frame
3.7, for example. So in film animation, the animator doesn’t pay much (if any)
attention to how the character looks in between the integral frame indices.
In contrast, a real-time game’s frame rate always varies a little, depending
on how much load is currently being placed on the CPU and GPU. Also, game
animations are sometimes time-scaled in order to make the character appear
to move faster or slower than originally animated. So in a real-time game,
an animation clip is almost never sampled on integer frame numbers. In the-
ory, with a time scale of 1.0, a clip should be sampled at frames 1, 2, 3 and
so on. But in practice, the player might actually see frames 1.1, 1.9, 3.2 and
so on. And if the time scale is 0.5, then the player might actually see frames

Figure 12.11. An animator creates a relatively small number of key poses, and the engine ﬁlls in the
rest of the poses via interpolation.


<!-- source-pdf-page: 756 -->
> Visual fallback for diagrams/images: [PDF page 756](../../../visual_pages/page_0756.jpg)

Frames:

26
27
28
29
30
1
2
3
4
5
...

31
Samples:

30
29
28
27
26
6
5
4
3
2
1

Figure 12.12. A one-second animation sampled at 30 frames per second is 30 frames in duration
and consists of 31 samples.

1.1, 1.4, 1.9, 2.6, 3.2 and so on. A negative time scale can even be used to play
an animation in reverse. So in game animation, time is both continuous and
scalable.

12.4.1.2
Time Units

Because an animation’s timeline is continuous, time is best measured in units
of seconds. Time can also be measured in units of frames, presuming we de-
fine the duration of a frame beforehand. Typical frame durations are 1/30 or
1/60 of a second for game animation. However, it’s important not to make
the mistake of defining your time variable t as an integer that counts whole
frames. No matter which time units are selected, t should be a real (floating-
point) quantity, a fixed-point number or an integer that measures very small
subframe time intervals. The goal is to have sufficient resolution in your time
measurements for doing things like “tweening” between frames or scaling an
animation’s playback speed.

12.4.1.3
Frame versus Sample

Unfortunately, the term frame has more than one common meaning in the game
industry. This can lead to a great deal of confusion. Sometimes a frame is taken
to be a period of time that is 1/30 or 1/60 of a second in duration. But in other
contexts, the term frame is applied to a single point in time (e.g., we might speak
of the pose of the character “at frame 42”).
I personally prefer to use the term sample to refer to a single point in time,
and I reserve the word frame to describe a time period that is 1/30 or 1/60
of a second in duration. So for example, a one-second animation created at
a rate of 30 frames per second would consist of 31 samples and would be 30
frames in duration, as shown in Figure 12.12. The term “sample” comes from
the field of signal processing. A continuous-time signal (i.e., a function f (t))
can be converted into a set of discrete data points by sampling that signal at
uniformly spaced time intervals. See Section 14.3.2.1 for more information on
sampling.


<!-- source-pdf-page: 757 -->
> Visual fallback for diagrams/images: [PDF page 757](../../../visual_pages/page_0757.jpg)

12.4.1.4
Frames, Samples and Looping Clips

When a clip is designed to be played over and over repeatedly, we say it is
looped. If we imagine two copies of a 1 s (30-frame/31-sample) clip laid back-
to-front, then sample 31 of the first clip will coincide exactly in time with sam-
ple 1 of the second clip, as shown in Figure 12.13. For a clip to loop properly,
then, we can see that the pose of the character at the end of the clip must exactly
match the pose at the beginning. This, in turn, implies that the last sample of
a looping clip (in our example, sample 31) is redundant. Many game engines
therefore omit the last sample of a looping clip.
This leads us to the following rules governing the number of samples and
frames in any animation clip:

•
If a clip is non-looping, an N-frame animation will have N + 1 unique
samples.

•
If a clip is looping, then the last sample is redundant, so an N-frame ani-
mation will have N unique samples.

...

...

...
...

30
29
28
27
26
5
4
3
2
1

30
29
28
27
26

31

1

6
5
4
3
2

Figure 12.13. The last sample of a looping clip coincides in time with its ﬁrst sample and is, therefore,
redundant.

12.4.1.5
Normalized Time (Phase)

It is sometimes convenient to employ a normalized time unit u, such that u = 0
at the start of the animation, and u = 1 at the end, no matter what its duration
T may be. We sometimes refer to normalized time as the phase of the animation
clip, because u acts like the phase of a sine wave when the animation is looped.
This is illustrated in Figure 12.14.
Normalized time is useful when synchronizing two or more animation
clips that are not necessarily of the same absolute duration. For example, we
might want to smoothly cross-fade from a 2-second (60-frame) run cycle into
a 3-second (90-frame) walk cycle. To make the cross-fade look good, we want
to ensure that the two animations remain synchronized at all times, so that the


<!-- source-pdf-page: 758 -->
> Visual fallback for diagrams/images: [PDF page 758](../../../visual_pages/page_0758.jpg)

u = 0
u = 0.4
u = 1
u = 0.8

Figure 12.14. An animation clip, showing normalized time units. Images courtesy of Naughty Dog,
Inc., © 2014/™SIE.

feet line up properly in both clips. We can accomplish this by simply setting
the normalized start time of the walk clip, uwalk, to match the normalized time
index of the run clip, urun. We then advance both clips at the same normalized
rate so that they remain in sync. This is quite a bit easier and less error-prone
than doing the synchronization using the absolute time indices twalk and trun.

### 12.4.2 The Global Timeline

Just as every animation clip has a local timeline (whose clock starts at 0 at the
beginning of the clip), every character in a game has a global timeline (whose
clock starts when the character is first spawned into the game world, or per-
haps at the start of the level or the entire game). In this book, we’ll use the time
variable τ to measure global time, so as not to confuse it with the local time
variable t.
We can think of playing an animation as simply mapping that clip’s local
timeline onto the character’s global timeline. For example, Figure 12.15 illus-
trates playing animation clip A starting at a global time of τstart = 102 seconds.
As we saw above, playing a looping animation is like laying down an in-
finite number of back-to-front copies of the clip onto the global timeline. We
can also imagine looping an animation a finite number of times, which corre-
sponds to laying down a finite number of copies of the clip. This is illustrated
in Figure 12.16.

102 sec

start

105 sec
110 sec

Clip A
t = 0 sec
5 sec

Figure 12.15. Playing animation clip A starting at a global time of 102 seconds.


<!-- source-pdf-page: 759 -->
> Visual fallback for diagrams/images: [PDF page 759](../../../visual_pages/page_0759.jpg)

102 sec

start

110 sec

105 sec

Clip A
...

Clip A

Figure 12.16. Playing a looping animation corresponds to laying down multiple back-to-back copies
of the clip.

Time-scaling a clip makes it appear to play back more quickly or more
slowly than originally animated. To accomplish this, we simply scale the im-
age of the clip when it is laid down onto the global timeline. Time-scaling is
most naturally expressed as a playback rate, which we’ll denote R. For exam-
ple, if an animation is to play back at twice the speed (R = 2), then we would
scale the clip’s local timeline to one-half (1/R = 0.5) of its normal length when
mapping it onto the global timeline. This is shown in Figure 12.17.
Playing a clip in reverse corresponds to using a time scale of −1, as shown
in Figure 12.18.
In order to map an animation clip onto a global timeline, we need the fol-
lowing pieces of information about the clip:

•
its global start time τstart,

start

t
t

R
(scale t by 1/R = 0.5)

t

Figure 12.17. Playing an animation at twice the speed corresponds to scaling its local timeline by a
factor of 1/2.

102 sec

start

105 sec
110 sec

 Clip A

t = 5 sec
0 sec

R = –1

(ﬂip t)

Clip A
t = 0 sec
5 sec

Figure 12.18. Playing a clip in reverse corresponds to a time scale of −1.


<!-- source-pdf-page: 760 -->

•
its playback rate R,
•
its duration T, and
•
the number of times it should loop, which we’ll denote N.

Given this information, we can map from any global time τ to the correspond-
ing local time t, and vice versa, using the following two relations:

t = (τ −τstart)R,
(12.2)

τ = τstart + 1

Rt.

If the animation doesn’t loop (N = 1), then we should clamp t into the
valid range [0, T] before using it to sample a pose from the clip:

t = clamp
[
(τ −τstart)R
]
T

0 .

If the animation loops forever (N = ∞), then we bring t into the valid
range by taking the remainder of the result after dividing by the duration T.
This is accomplished via the modulo operator (mod, or % in C/C++), as shown
below:
t =
((τ −τstart)R
)
mod T.

If the clip loops a finite number of times (1 < N < ∞), we must first clamp
t into the range [0, NT] and then modulo that result by T in order to bring t
into a valid range for sampling the clip:

)
mod T.

t =
(
clamp
[
(τ −τstart)R
]
NT

0

Most game engines work directly with local animation timelines and don’t
use the global timeline directly. However, working directly in terms of global
times can have some incredibly useful benefits. For one thing, it makes syn-
chronizing animations trivial.

### 12.4.3 Comparison of Local and Global Clocks

The animation system must keep track of the time indices of every animation
that is currently playing. To do so, we have two choices:

•
Local clock. In this approach, each clip has its own local clock, usually
represented by a floating-point time index stored in units of seconds or
frames, or in normalized time units (in which case it is often called the
phase of the animation). At the moment the clip begins to play, the local


<!-- source-pdf-page: 761 -->
> Visual fallback for diagrams/images: [PDF page 761](../../../visual_pages/page_0761.jpg)

time index t is usually taken to be zero. To advance the animations for-
ward in time, we advance the local clocks of each clip individually. If a
clip has a non-unit playback rate R, the amount by which its local clock
advances must be scaled by R.

•
Global clock. In this approach, the character has a global clock, usually
measured in seconds, and each clip simply records the global time at
which it started playing, τstart. The clips’ local clocks are calculated from
this information using Equation (12.2).

The local clock approach has the benefit of being simple, and it is the most
obvious choice when designing an animation system. However, the global
clock approach has some distinct advantages, especially when it comes to syn-
chronizing animations, either within the context of a single character or across
multiple characters in a scene.

12.4.3.1
Synchronizing Animations with a Local Clock

With a local clock approach, we said that the origin of a clip’s local timeline
(t = 0) is usually defined to coincide with the moment at which the clip starts
playing. Thus, to synchronize two or more clips, they must be played at ex-
actly the same moment in game time. This seems simple enough, but it can
become quite tricky when the commands used to play the animations are com-
ing from disparate engine subsystems.
For example, let’s say we want to synchronize the player character’s punch
animation with a non-player character’s corresponding hit reaction animation.
The problem is that the player’s punch is initiated by the player subsystem in
response to detecting that a button was hit on the joy pad. Meanwhile, the
non-player character’s (NPC) hit reaction animation is played by the artificial
intelligence (AI) subsystem. If the AI code runs before the player code in the
game loop, there will be a one-frame delay between the start of the player’s
punch and the start of the NPC’s reaction. And if the player code runs before
the AI code, then the opposite problem occurs when an NPC tries to punch the
player. If a message-passing (event) system is used to communicate between
the two subsystems, additional delays might be incurred (see Section 16.8 for
more details). This problem is illustrated in Figure 12.19.

void GameLoop()
{
while (!quit)
{
// preliminary updates...


<!-- source-pdf-page: 762 -->
> Visual fallback for diagrams/images: [PDF page 762](../../../visual_pages/page_0762.jpg)

UpdateAllNpcs(); // react to punch event
// from last frame

// more updates...

UpdatePlayer(); // punch button hit - start punch
// anim, and send event to NPC to
// react

// still more updates...
}
}

Frame N+1
Frame N

NPC

Update

Update

Queue Event

play anim

send: Punch

Player

Update

play anim

Player

Player Punch
(local t = 0)
request start (frame N)

Anim

NPC
Anim

Hit Reaction
(local t = 0)
start (frame N+1)

Figure 12.19. The order of execution of disparate gameplay systems can introduce animation syn-
chronization problems when local clocks are used.

12.4.3.2
Synchronizing Animations with a Global Clock

A global clock approach helps to alleviate many of these synchronization prob-
lems, because the origin of the timeline (τ = 0) is common across all clips by
definition. If two or more animations’ global start times are numerically equal,
the clips will start in perfect synchronization. If their playback rates are also
equal, then they will remain in sync with no drift. It no longer matters when
the code that plays each animation executes. Even if the AI code that plays
the hit reaction ends up running a frame later than the player’s punch code,
it is still trivial to keep the two clips in sync by simply noting the global start
time of the punch and setting the global start time of the reaction animation to
match it. This is shown in Figure 12.20.
Of course, we do need to ensure that the two characters’ global clocks
match, but this is trivial to do. We can either adjust the global start times to


<!-- source-pdf-page: 763 -->
> Visual fallback for diagrams/images: [PDF page 763](../../../visual_pages/page_0763.jpg)

Frame N+1
Frame N

NPC

Update

Update

Queue Event

play anim

send: Punch

Player

Update

play anim

Player

Player Punch
(global start time:

(frame N)

)
start at global

Anim

NPC
Anim

NPC Hit Reaction
(global start time:

(frame N)

)
start at global

Figure 12.20. A global clock approach can alleviate animation synchronization problems.

take account of any differences in the characters’ clocks, or we can simply have
all characters in the game share a single master clock.

### 12.4.4 A Simple Animation Data Format

Typically, animation data is extracted from a Maya scene file by sampling the
pose of the skeleton discretely at a rate of 30 or 60 samples per second. A sam-
ple comprises a full pose for each joint in the skeleton. The poses are usually
stored in SRT format: For each joint j, the scale component is either a single
floating-point scalar Sj or a three-element vector Sj =
[Sjx
Sjy
Sjz
]
. The
rotational component is of course a four-element quaternion Qj = [Qjx
Qjy
Qjz
Qjw]. And the translational component is a three-element vector Tj =

0
1
2
3
4
5
6
7
8
9

...

T0

y
x

z

y
x

Q0

z
w

S0

y
x

z

T1

Q1

S1

Figure 12.21. An uncompressed animation clip contains 10 channels of ﬂoating-point data per sam-
ple, per joint.


<!-- source-pdf-page: 764 -->
> Visual fallback for diagrams/images: [PDF page 764](../../../visual_pages/page_0764.jpg)

[Tjx
Tjy
Tjz
]
. We sometimes say that an animation consists of up to 10 chan-
nels per joint, in reference to the 10 components of Sj, Qj, and Tj. This is illus-
trated in Figure 12.21.
In C++, an animation clip can be represented in many different ways. Here
is one possibility:

struct JointPose { ... }; // SRT, defined as above

struct AnimationSample
{
JointPose*
m_aJointPose; // array of joint
// poses
};

struct AnimationClip
{
Skeleton*
m_pSkeleton;
F32
m_framesPerSecond;
U32
m_frameCount;
AnimationSample* m_aSamples; // array of samples
bool
m_isLooping;
};

An animation clip is authored for a specific skeleton and generally won’t
work on any other skeleton. As such, our example AnimationClip data
structure contains a reference to its skeleton, m_pSkeleton. (In a real engine,
this might be a unique skeleton id rather than a Skeleton* pointer. In this
case, the engine would presumably provide a way to quickly and conveniently
look up a skeleton by its unique id.)
The number of JointPoses in the m_aJointPose array within each sam-
ple is presumed to match the number of joints in the skeleton. The number
of samples in the m_aSamples array is dictated by the frame count and by
whether or not the clip is intended to loop. For a non-looping animation,
the number of samples is (m_frameCount + 1). However, if the anima-
tion loops, then the last sample is identical to the first sample and is usually
omitted. In this case, the sample count is equal to m_frameCount.
It’s important to realize that in a real game engine, animation data isn’t
actually stored in this simplistic format. As we’ll see in Section 12.8, the data
is usually compressed in various ways to save memory.

### 12.4.5 Continuous Channel Functions

The samples of an animation clip are really just definitions of continuous func-
tions over time. You can think of these as 10 scalar-valued functions of time


<!-- source-pdf-page: 765 -->
> Visual fallback for diagrams/images: [PDF page 765](../../../visual_pages/page_0765.jpg)

per joint, or as two vector-valued functions and one quaternion-valued func-
tion per joint. Theoretically, these channel functions are smooth and continuous
across the entire clip’s local timeline, as shown in Figure 12.22 (with the excep-
tion of explicitly authored discontinuities like camera cuts). In practice, how-
ever, many game engines interpolate linearly between the samples, in which
case the functions actually used are piecewise linear approximations to the un-
derlying continuous functions. This is depicted in Figure 12.23.

### 12.4.6 Metachannels

Many games permit additional “metachannels” of data to be defined for an
animation. These channels can encode game-specific information that doesn’t
have to do directly with posing the skeleton but which needs to be synchro-
nized with the animation.
It is quite common to define a special channel that contains event triggers
at various time indices, as shown in Figure 12.24. Whenever the animation’s
local time index passes one of these triggers, an event is sent to the game engine,
which can respond as it sees fit. (We’ll discuss events in detail in Chapter 16.)
One common use of event triggers is to denote at which points during the

Figure 12.22. The animation samples in a clip deﬁne continuous functions over time.

Qy3

t

Figure 12.23. Many game engines use a piecewise linear approximation when interpolating channel
functions.


<!-- source-pdf-page: 766 -->
> Visual fallback for diagrams/images: [PDF page 766](../../../visual_pages/page_0766.jpg)

0
1
2
3
4
5
6
7
8
9

...

T0

Q0

S0

T1

Q1

S1

Events

Footstep
Left
Footstep
Right
Reload
Weapon

Figure 12.24.
A special event trigger channel can be added to an animation clip in order to
synchronize sound effects, particle effects and other game events with an animation.

animation certain sound or particle effects should be played. For example,
when the left or right foot touches the ground, a footstep sound and a “cloud
of dust” particle effect could be initiated.
Another common practice is to permit special joints, known in Maya as
locators, to be animated along with the joints of the skeleton itself. Because a
joint or locator is just an affine transform, these special joints can be used to
encode the position and orientation of virtually any object in the game.
A typical application of animated locators is to specify how the game’s
camera should be positioned and oriented during an animation. In Maya, a
locator is constrained to a camera, and the camera is then animated along with
the joints of the character(s) in the scene. The camera’s locator is exported and
used in-game to move the game’s camera around during the animation. The
field of view (focal length) of the camera, and possibly other camera attributes,
can also be animated by placing the relevant data into one or more additional
floating-point channels.
Other examples of non-joint animation channels include:

•
texture coordinate scrolling,

•
texture animation (a special case of texture coordinate scrolling in which
frames are arranged linearly within a texture, and the texture is scrolled
by one complete frame at each iteration),


<!-- source-pdf-page: 767 -->
> Visual fallback for diagrams/images: [PDF page 767](../../../visual_pages/page_0767.jpg)

•
animated material parameters (color, specularity, transparency, etc.),

•
animated lighting parameters (radius, cone angle, intensity, color, etc.),
and

•
any other parameters that need to change over time and are in some way
synchronized with an animation.

### 12.4.7 Relationship between Meshes, Skeletons and Clips

The UML diagram in Figure 12.25 shows how animation clip data interfaces
with the skeletons, poses, meshes and other data in a game engine. Pay partic-
ular attention to the cardinality and direction of the relationships between these
classes. The cardinality is shown just beside the tip or tail of the relationship
arrow between classes—a one represents a single instance of the class, while
an asterisk indicates many instances. For any one type of character, there will
be one skeleton, one or more meshes and one or more animation clips. The
skeleton is the central unifying element—the skins are attached to the skele-
ton but don’t have any relationship with the animation clips. Likewise, the
clips are targeted at a particular skeleton, but they have no “knowledge” of
the skin meshes. Figure 12.26 illustrates these relationships.
Game designers often try to reduce the number of unique skeletons in the
game to a minimum, because each new skeleton generally requires a whole
new set of animation clips. To provide the illusion of many different types of
characters, it is usually better to create multiple meshes skinned to the same
skeleton when possible, so that all of the characters can share a single set of
animations.

12.4.7.1
Animation Retargeting

We said above that an animation is typically only compatible with a single
skeleton. This limitation can be overcome via animation retargeting techniques.
Retargeting means using an animation authored for one skeleton to ani-
mate a different skeleton. If the two skeletons are morphologically identical,
retargeting may boil down to a simple matter of joint index remapping. But
when the two skeletons don’t match exactly, the retargeting problem becomes
more complex. At Naughty Dog, the animators define a special pose known
as the retarget pose. This pose captures the essential differences between the
bind poses of the source and target skeletons, allowing the runtime retargeting
system to adjust source poses so they will work more naturally on the target
character.
Other more-advanced techniques exist for retargeting animations authored
for one skeleton so that they work on a different skeleton. For more infor-


<!-- source-pdf-page: 768 -->
> Visual fallback for diagrams/images: [PDF page 768](../../../visual_pages/page_0768.jpg)

Skeleton

SkeletonJoint

-uniqueId : int
-jointCount : int
-joints : SkeletonJoint

-name : string
-parentIndex : int
-invBindPose : Matrix44

1
*

1

1

*

*

Mesh

AnimationClip

-indices : int
-vertices : Vertex
-skeletonId : int

-nameId : int
-duration : float
-poseSamples : AnimationPose

1

1

*

*

SQT

Vertex
-scale : Vector3
-rotation : Quaternion
-translation : Vector3

AnimationPose

-position : Vector3
-normal : Vector3
-uv : Vector2
-jointIndices : int
-jointWeights : float

-jointPoses : SQT

1
*

Figure 12.25. UML diagram of shared animation resources.

mation, see “Feature Points Based Facial Animation Retargeting” by Ludovic
Dutreve et al. (https://bit.ly/2HL9Cdr) and “Real-time Motion Retargeting
to Highly Varied User-Created Morphologies” by Chris Hecker et al. (https://
bit.ly/2vviG3x).

Skin A

Clip 1

Skeleton

Skin B

Clip 2

Skin C

Clip 3

...

Clip N

other skeletons...

...
...

Figure 12.26. Many animation clips and one or more meshes target a single skeleton.


<!-- source-pdf-page: 769 -->

## 12.5 Skinning and Matrix Palette Generation

We’ve seen how to pose a skeleton by rotating, translating and possibly scaling
its joints. And we know that any skeletal pose can be represented mathemat-

ically as a set of local
(
Pj→p(j)
)
or global
(
Pj→M
)
joint pose transformations,
one for each joint j. Next, we will explore the process of attaching the vertices
of a 3D mesh to a posed skeleton. This process is known as skinning.

### 12.5.1 Per-Vertex Skinning Information

A skinned mesh is attached to a skeleton by means of its vertices. Each vertex
can be bound to one or more joints. If bound to a single joint, the vertex tracks
that joint’s movement exactly. If bound to two or more joints, the vertex’s
position becomes a weighted average of the positions it would have assumed
had it been bound to each joint independently.
To skin a mesh to a skeleton, a 3D artist must supply the following addi-
tional information at each vertex:

•
the index or indices of the joint(s) to which it is bound, and
•
for each joint, a weighting factor describing how much influence that joint
should have on the final vertex position.

The weighting factors are assumed to add to one, as is customary when calcu-
lating any weighted average.
Usually a game engine imposes an upper limit on the number of joints to
which a single vertex can be bound. A four-joint limit is typical for a number of
reasons. First, four 8-bit joint indices can be packed into a 32-bit word, which
is convenient. Also, while it’s pretty easy to see a difference in quality between
a two-, three- and even a four-joint-per-vertex model, most people cannot see
a quality difference as the number of joints per vertex is increased beyond four.
Because the joint weights must sum to one, the last weight can be omitted
and often is. (It can be calculated at runtime as w3 = 1 −(w0 + w1 + w2).) As
such, a typical skinned vertex data structure might look as follows:

struct SkinnedVertex
{
float
m_position[3];
// (Px, Py, Pz)
float
m_normal[3];
// (Nx, Ny, Nz)
float
m_u, m_v;
// texture coords (u,v)
U8
m_jointIndex[4];
// joint indices
float
m_jointWeight[3]; // joint weights (last
// weight omitted)
};


<!-- source-pdf-page: 770 -->
> Visual fallback for diagrams/images: [PDF page 770](../../../visual_pages/page_0770.jpg)

### 12.5.2 The Mathematics of Skinning

The vertices of a skinned mesh track the movements of the joint(s) to which
they are bound. To make this happen mathematically, we would like to find a
matrix that can transform the vertices of the mesh from their original positions
(in bind pose) into new positions that correspond to the current pose of the
skeleton. We shall call such a matrix a skinning matrix.
Like all mesh vertices, the position of a skinned vertex is specified in model
space. This is true whether its skeleton is in bind pose or in any other pose.
So the matrix we seek will transform vertices from model space (bind pose)
to model space (current pose). Unlike the other transforms we’ve seen thus
far, such as the model-to-world transform or the world-to-view transform, a
skinning matrix is not a change of basis transform. It morphs vertices into
new positions, but the vertices are in model space both before and after the
transformation.

12.5.2.1
Simple Example: One-Jointed Skeleton

Let us derive the basic equation for a skinning matrix. To keep things simple at
first, we’ll work with a skeleton consisting of a single joint. We therefore have
two coordinate spaces to work with: model space, which we’ll denote with
the subscript M, and the joint space of our one and only joint, which will be
indicated by the subscript J. The joint’s coordinate axes start out in bind pose,
which we’ll denote with the superscript B. At any given moment during an
animation, the joint’s axes move to a new position and orientation in model
space—we’ll indicate this current pose with the superscript C.
Now consider a single vertex that is skinned to our joint. In bind pose,
its model-space position is vB
M. The skinning process calculates the vertex’s
new model-space position in the current pose, vC
M. This is illustrated in Fig-
ure 12.27.
The “trick” to finding the skinning matrix for a given joint is to realize that
the position of a vertex bound to a joint is constant when expressed in that joint’s
coordinate space. So we take the bind-pose position of the vertex in model space,
convert it into joint space, move the joint into its current pose, and finally con-
vert the vertex back into model space. The net effect of this round trip from
model space to joint space and back again is to “morph” the vertex from bind
pose into the current pose.
Referring to the illustration in Figure 12.28, let’s assume that the coordi-
nates of the vertex vB
M are (4, 6) in model space (when the skeleton is in bind
pose). We convert this vertex into its equivalent joint-space coordinates vj,
which are roughly (1, 3) as shown in the diagram. Because the vertex is bound


<!-- source-pdf-page: 771 -->
> Visual fallback for diagrams/images: [PDF page 771](../../../visual_pages/page_0771.jpg)

Bind Pose
Joint Space
Axes

Bind pose
vertex position,
in model space

yB

Current
Pose Joint
Space Axes

vM
B

yM
xB

yC

vM
C

xC

Current pose
vertex position,
in model space

xM

Model Space Axes

Figure 12.27. Bind pose and current pose of a simple, one-joint skeleton and a single vertex bound
to that joint.

1. Transform into
    joint space

2. Move joint into
    current pose

yB

yM
xB

v j
v j

vM
B

yC

vM
C

xC

xM

3. Transform back
    into model space

Figure 12.28. By transforming a vertex’s position into joint space, it can be made to “track” the
joint’s movements.

to the joint, its joint-space coordinates will always be (1, 3) no matter how the
joint may move. Once we have the joint in the desired current pose, we con-
vert the vertex’s coordinates back into model space, which we’ll denote with
the symbol vC
M. In our diagram, these coordinates are roughly (18, 2). So the
skinning transformation has morphed our vertex from (4, 6) to (18, 2) in model
space, due entirely to the motion of the joint from its bind pose to the current
pose shown in the diagram.

Looking at the problem mathematically, we can denote the bind pose of the
joint j in model space by the matrix Bj→M. This matrix transforms a point or
vector whose coordinates are expressed in joint j’s space into an equivalent
set of model-space coordinates. Now, consider a vertex whose coordinates are
expressed in model space with the skeleton in bind pose. To convert these
vertex coordinates into the space of joint j, we simply multiply it by the inverse


<!-- source-pdf-page: 772 -->

bind pose matrix, BM→j =
(
Bj→M
)−1:

vj = vB
MBM→j = vB
M
(
Bj→M
)−1 .
(12.3)

Likewise, we can denote the joint’s current pose (i.e., any pose that is not
bind pose) by the matrix Cj→M. To convert vj from joint space back into model
space, we simply multiply it by the current pose matrix as follows:

vC
M = vjCj→M.

If we expand vj using Equation (12.3), we obtain an equation that takes our
vertex directly from its position in bind pose to its position in the current pose:

vC
M = vjCj→M

= vB
M
(
Bj→M
)−1 Cj→M
(12.4)

= vB
MKj.

The combined matrix Kj =
(
Bj→M
)−1 Cj→M is known as a skinning matrix.

12.5.2.2
Extension to Multijointed Skeletons

In the example above, we considered only a single joint. However, the math we
derived above actually applies to any joint in any skeleton imaginable, because
we formulated everything in terms of global poses (i.e., joint space to model
space transforms). To extend the above formulation to a skeleton containing
multiple joints, we therefore need to make only two minor adjustments:

1.
We must make sure that our Bj→M and Cj→M matrices are calculated
properly for the joint in question, using Equation (12.1). Bj→M and Cj→M
are just the bind pose and current pose equivalents, respectively, of the
matrix Pj→M used in that equation.

2.
We must calculate an array of skinning matrices Kj, one for each joint j.
This array is known as a matrix palette. The matrix palette is passed to the
rendering engine when rendering a skinned mesh. For each vertex, the
renderer looks up the appropriate joint’s skinning matrix in the palette
and uses it to transform the vertex from bind pose into current pose.

We should note here that the current pose matrix Cj→M changes every
frame as the character assumes different poses over time. However, the in-
verse bind-pose matrix is constant throughout the entire game, because the
bind pose of the skeleton is fixed when the model is created. Therefore, the


<!-- source-pdf-page: 773 -->

matrix
(
Bj→M
)−1 is generally cached with the skeleton, and needn’t be calcu-
lated at runtime. Animation engines generally calculate local poses for each

joint
(
Cj→p(j)
)
, then use Equation (12.1) to convert these into global poses
(
Cj→M
)
, and finally multiply each global pose by the corresponding cached

inverse bind pose matrix
(
Bj→M
)−1 in order to generate a skinning matrix (Kj)
for each joint.

12.5.2.3
Incorporating the Model-to-World Transform

Every vertex must eventually be transformed from model space into world
space. Some engines therefore premultiply the palette of skinning matrices by
the object’s model-to-world transform. This can be a useful optimization, as
it saves the rendering engine one matrix multiply per vertex when rendering
skinned geometry. (With hundreds of thousands of vertices to process, these
savings can really add up!)
To incorporate the model-to-world transform into our skinning matrices,
we simply concatenate it to the regular skinning matrix equation, as follows:

(
Kj
)

W =
(
Bj→M
)−1 Cj→MMM→W.

Some engines bake the model-to-world transform into the skinning matri-
ces like this, while others don’t. The choice is entirely up to the engineering
team and is driven by all sorts of factors. For example, one situation in which
we would definitely not want to do this is when a single animation is being ap-
plied to multiple characters simultaneously—a technique known as animation
instancing that is sometimes used for animating large crowds of characters. In
this case we need to keep the model-to-world transforms separate so that we
can share a single matrix palette across all characters in the crowd.

12.5.2.4
Skinning a Vertex to Multiple Joints

When a vertex is skinned to more than one joint, we calculate its final position
by assuming it is skinned to each joint individually, calculating a model-space
position for each joint and then taking a weighted average of the resulting posi-
tions. The weights are provided by the character rigging artist, and they must
always sum to one. (If they do not sum to one, they should be renormalized
by the tools pipeline.)
The general formula for a weighted average of N quantities a0 through
aN−1, with weights w0 through wN−1 and with ∑wi = 1 is:

a =
N−1
∑
i=0
wiai.


<!-- source-pdf-page: 774 -->

This works equally well for vector quantities ai. So, for a vertex skinned to
N joints with indices j0 through jN−1 and weights w0 through wN−1, we can
extend Equation (12.4) as follows:

vC
M =
N−1
∑
i=0
wivB
MKji,

where Kji is the skinning matrix for the joint ji.

## 12.6 Animation Blending

The term animation blending refers to any technique that allows more than one
animation clip to contribute to the final pose of the character. To be more pre-
cise, blending combines two or more input poses to produce an output pose for
the skeleton.
Blending usually combines two or more poses at a single point in time, and
generates an output at that same moment in time. In this context, blending
is used to combine two or more animations into a host of new animations,
without having to create them manually. For example, by blending an injured
walk animation with an uninjured walk, we can generate various intermediate
levels of apparent injury for our character while he is walking. As another
example, we can blend between an animation in which the character is aiming
to the left and one in which he’s aiming to the right, in order to make the
character aim along any desired angle between the two extremes. Blending
can be used to interpolate between extreme facial expressions, body stances,
locomotion modes and so on.
Blending can also be used to find an intermediate pose between two known
poses at different points in time. This is used when we want to find the pose
of a character at a point in time that does not correspond exactly to one of the
sampled frames available in the animation data. We can also use temporal
animation blending to smoothly transition from one animation to another, by
gradually blending from the source animation to the destination over a short
period of time.

### 12.6.1 LERP Blending

Given a skeleton with N joints, and two skeletal poses Pskel
A
=
{(PA)j
} |N−1
j=0
and Pskel
B
=
{(PB)j
} |N−1
j=0 , we wish to find an intermediate pose Pskel
LERP be-
tween these two extremes. This can be done by performing a linear interpola-
tion (LERP) between the local poses of each individual joint in each of the two


<!-- source-pdf-page: 775 -->
> Visual fallback for diagrams/images: [PDF page 775](../../../visual_pages/page_0775.jpg)

source poses. This can be written as follows:

(PLERP)j = LERP
((PA)j, (PB)j, β
)
(12.5)

= (1 −β)(PA)j + β(PB)j.

The interpolated pose of the whole skeleton is simply the set of interpolated
poses for all of the joints:

Pskel
LERP =
{(PLERP)j
}

N−1
j=0 .
(12.6)

In these equations, β is called the blend percentage or blend factor. When
β = 0, the final pose of the skeleton will exactly match Pskel
A ; when β = 1,
the final pose will match Pskel
B
. When β is between zero and one, the final
pose is an intermediate between the two extremes. This effect is illustrated in
Figure 12.11.
We’ve glossed over one small detail here: We are linearly interpolating joint
poses, which means interpolating 4 × 4 transformation matrices. But, as we saw
in Chapter 5, interpolating matrices directly is not practical. This is one of the
reasons why local poses are usually expressed in SRT format—doing so allows
us to apply the LERP operation defined in Section 5.2.5 to each component of
the SRT individually. The linear interpolation of the translation component T
of an SRT is just a straightforward vector LERP:

(TLERP)j = LERP
((TA)j, (TB)j, β
)
(12.7)
= (1 −β)(TA)j + β(TB)j.

The linear interpolation of the rotation component is a quaternion LERP or
SLERP (spherical linear interpolation):

(QLERP)j = normalize
(
LERP
((QA)j, (QB)j, β
))
(12.8)
= normalize
((1 −β)(QA)j + β(QB)j
)
.

or

(QSLERP)j = SLERP
((QA)j, (QB)j, β
)
(12.9)

= sin ((1 −β)θ)

sin(θ)
(QA)j + sin(βθ)

sin(θ) (QB)j.

Finally, the linear interpolation of the scale component is either a scalar or vec-
tor LERP, depending on the type of scale (uniform or nonuniform scale) sup-
ported by the engine:

(SLERP)j = LERP
((SA)j, (SB)j, β
)
(12.10)
= (1 −β)(SA)j + β(SB)j.


<!-- source-pdf-page: 776 -->

or
(SLERP)j = LERP
((SA)j, (SB)j, β
)
(12.11)
= (1 −β)(SA)j + β(SB)j.

When linearly interpolating between two skeletal poses, the most natural-
looking intermediate pose is generally one in which each joint pose is interpo-
lated independently of the others, in the space of that joint’s immediate parent.
In other words, pose blending is generally performed on local poses. If we were
to blend global poses directly in model space, the results would tend to look
biomechanically implausible.
Because pose blending is done on local poses, the linear interpolation of
any one joint’s pose is totally independent of the interpolations of the other
joints in the skeleton. This means that linear pose interpolation can be per-
formed entirely in parallel on multiprocessor architectures.

### 12.6.2 Applications of LERP Blending

Now that we understand the basics of LERP blending, let’s have a look at some
typical gaming applications.

12.6.2.1
Temporal Interpolation

As we mentioned in Section 12.4.1.1, game animations are almost never sam-
pled exactly on integer frame indices. Because of variable frame rate, the
player might actually see frames 0.9, 1.85 and 3.02, rather than frames 1, 2 and
3 as one might expect. In addition, some animation compression techniques
involve storing only disparate key frames, spaced at uneven intervals across
the clip’s local timeline. In either case, we need a mechanism for finding in-
termediate poses between the sampled poses that are actually present in the
animation clip.
LERP blending is typically used to find these intermediate poses. As an
example, let’s imagine that our animation clip contains evenly spaced pose
samples at times 0, ∆t, 2∆t, 3∆t and so on. To find a pose at time t = 2.18∆t,
we simply find the linear interpolation between the poses at times 2∆t and
3∆t, using a blend percentage of β = 0.18.
In general, we can find the pose at time t given pose samples at any two
times t1 and t2 that bracket t, as follows:

Pj(t) = LERP
(
Pj(t1), Pj(t2), β(t)
)
(12.12)
= (1 −β(t)) Pj(t1) + β(t)Pj(t2),
(12.13)

where the blend factor β(t) can be determined by the ratio

β(t) = t −t1

t2 −t1
.
(12.14)


<!-- source-pdf-page: 777 -->
> Visual fallback for diagrams/images: [PDF page 777](../../../visual_pages/page_0777.jpg)

12.6.2.2
Motion Continuity: Cross-Fading

Game characters are animated by piecing together a large number of fine-
grained animation clips. If your animators are any good, the character will
appear to move in a natural and physically plausible way within each indi-
vidual clip. However, it is notoriously difficult to achieve the same level of
quality when transitioning from one clip to the next. The vast majority of the
“pops” we see in game animations occur when the character transitions from
one clip to the next.
Ideally, we would like the movements of each part of a character’s body
to be perfectly smooth, even during transitions. In other words, the three-
dimensional paths traced out by each joint in the skeleton as it moves should
contain no sudden “jumps.” We call this C0 continuity; it is illustrated in Fig-
ure 12.29.
Not only should the paths themselves be continuous, but their first deriva-
tives (velocity) should be continuous as well. This is called C1 continuity (or
continuity of velocity and momentum). The perceived quality and realism
of an animated character’s movement improves as we move to higher- and
higher-order continuity. For example, we might want to achieve C2 continu-
ity, in which the second derivatives of the motion paths (acceleration curves)
are also continuous.
Strict mathematical continuity up to C1 or higher is often infeasible to
achieve. However, LERP-based animation blending can be applied to achieve
a reasonably pleasing form of C0 motion continuity. It usually also does a
pretty good job of approximating C1 continuity. When applied to transitions
between clips in this manner, LERP blending is sometimes called cross-fading.
LERP blending can introduce unwanted artifacts, such as the dreaded “sliding
feet” problem, so it must be applied judiciously.
To cross-fade between two animations, we overlap the timelines of the two
clips by some reasonable amount, and then blend the two clips together. The
blend percentage β starts at zero at time tstart, meaning that we see only clip A

Tx7
discontinuity

Tx7

t

t

C0 continuous
not C0 continuous

Figure 12.29. The channel function on the left has C0 continuity, while the path on the right does
not.


<!-- source-pdf-page: 778 -->
> Visual fallback for diagrams/images: [PDF page 778](../../../visual_pages/page_0778.jpg)

when the cross-fade begins. We gradually increase β until it reaches a value of
one at time tend. At this point only clip B will be visible, and we can retire clip
A altogether. The time interval over which the cross-fade occurs (∆tblend =
tend −tstart) is sometimes called the blend time.

Types of Cross-Fades

There are two common ways to perform a cross-blended transition:

•
Smooth transition. Clips A and B both play simultaneously as β increases
from zero to one. For this to work well, the two clips must be looping an-
imations, and their timelines must be synchronized so that the positions
of the legs and arms in one clip match up roughly with their positions in
the other clip. (If this is not done, the cross-fade will often look totally
unnatural.) This technique is illustrated in Figure 12.30.

•
Frozen transition. The local clock of clip A is stopped at the moment clip
B starts playing. Thus, the pose of the skeleton from clip A is frozen
while clip B gradually takes over the movement. This kind of transitional
blend works well when the two clips are unrelated and cannot be time-
synchronized, as they must be when performing a smooth transition. This
approach is depicted in Figure 12.31.

We can also control how the blend factor β varies during the transition.
In Figure 12.30 and Figure 12.31, the blend factor varied linearly with time.
To achieve an even smoother transition, we could vary β according to a cubic
function of time, such as a one-dimensional Bézier. When such a curve is ap-
plied to a currently running clip that is being blended out, it is known as an
ease-out curve; when it is applied to a new clip that is being blended in, it is
known as an ease-in curve. This is shown in Figure 12.32.

Clip A

Clip B

tstart
tend

t

Figure 12.30. A smooth transition, in which the local clocks of both clips keep running during the
transition.


<!-- source-pdf-page: 779 -->
> Visual fallback for diagrams/images: [PDF page 779](../../../visual_pages/page_0779.jpg)

A’s local timeline

freezes here

Clip A

Clip B

tstart
tend

t

Figure 12.31. A frozen transition, in which clip A’s local clock is stopped during the transition.

The equation for a Bézier ease-in/ease-out curve is given below. It returns
the value of β at any time t within the blend interval. βstart is the blend factor
at the start of the blend interval tstart, and βend is the final blend factor at time
tend. The parameter u is the normalized time between tstart and tend, and for con-
venience we’ll also define v = 1 −u (the inverse normalized time). Note that
the Bézier tangents Tstart and Tend are taken to be equal to the corresponding
blend factors βstart and βend, because this yields a well-behaved curve for our
purposes:

let u =
(
t −tstart
tend −tstart

)

and v = 1 −u.
β(t) = (v3)βstart + (3v2u)Tstart + (3vu2)Tend + (u3)βend
= (v3 + 3v2u)βstart + (3vu2 + u3)βend.

Core Poses

This is an appropriate time to mention that motion continuity can actually be

Clip A

Clip B

tstart
tend

t

Figure 12.32. A smooth transition, with a cubic ease-in/ease-out curve applied to the blend factor.


<!-- source-pdf-page: 780 -->
> Visual fallback for diagrams/images: [PDF page 780](../../../visual_pages/page_0780.jpg)

Path of
Movement

Targeted
Pivotal

Figure 12.33. In pivotal movement, the character faces the direction she is moving and pivots about
her vertical axis to turn. In targeted movement, the movement direction need not match the facing
direction.

achieved without blending if the animator ensures that the last pose in any
given clip matches the first pose of the clip that follows it. In practice, anima-
tors often decide upon a set of core poses—for example, we might have a core
pose for standing upright, one for crouching, one for lying prone and so on.
By making sure that the character starts in one of these core poses at the begin-
ning of every clip and returns to a core pose at the end, C0 continuity can be
achieved by simply ensuring that the core poses match when animations are
spliced together. C1 or higher-order motion continuity can also be achieved
by ensuring that the character’s movement at the end of one clip smoothly
transitions into the motion at the start of the next clip. This can be achieved
by authoring a single smooth animation and then breaking it into two or more
clips.

12.6.2.3
Directional Locomotion

LERP-based animation blending is often applied to character locomotion.
When a real human being walks or runs, he can change the direction in which
he is moving in two basic ways: First, he can turn his entire body to change
direction, in which case he always faces in the direction he’s moving. I’ll call
this pivotal movement, because the person pivots about his vertical axis when
he turns. Second, he can keep facing in one direction while walking forward,
backward or sideways (known as strafing in the gaming world) in order to
move in a direction that is independent of his facing direction. I’ll call this tar-
geted movement, because it is often used in order to keep one’s eye—or one’s
weapon—trained on a target while moving. These two movement styles are
illustrated in Figure 12.33.

Targeted Movement

To implement targeted movement, the animator authors three separate looping


<!-- source-pdf-page: 781 -->
> Visual fallback for diagrams/images: [PDF page 781](../../../visual_pages/page_0781.jpg)

animation clips—one moving forward, one strafing to the left, and one straf-
ing to the right. I’ll call these directional locomotion clips. The three directional
clips are arranged around the circumference of a semicircle, with forward at
0 degrees, left at 90 degrees and right at −90 degrees. With the character’s
facing direction fixed at 0 degrees, we find the desired movement direction on
the semicircle, select the two adjacent movement animations and blend them
together via LERP-based blending. The blend percentage β is determined by
how close the angle of movement is to the angles of two adjacent clips. This is
illustrated in Figure 12.34.

Note that we did not include backward movement in our blend, for a full
circular blend. This is because blending between a sideways strafe and a back-
ward run cannot be made to look natural in general. The problem is that when
strafing to the left, the character usually crosses its right foot in front of its left
so that the blend into the pure forward run animation looks correct. Likewise,
the right strafe is usually authored with the left foot crossing in front of the
right. When we try to blend such strafe animations directly into a backward
run, one leg will start to pass through the other, which looks extremely awk-
ward and unnatural. There are a number of ways to solve this problem. One
feasible approach is to define two hemispherical blends, one for forward mo-
tion and one for backward motion, each with strafe animations that have been
crafted to work properly when blended with the corresponding straight run.
When passing from one hemisphere to the other, we can play some kind of ex-
plicit transition animation so that the character has a chance to adjust its gait
and leg crossing appropriately.

Run
Forward

Right
Strafe

Strafe

Left

Figure 12.34. Targeted movement can be implemented by blending together looping locomotion
clips that move in each of the four principal directions.


<!-- source-pdf-page: 782 -->
> Visual fallback for diagrams/images: [PDF page 782](../../../visual_pages/page_0782.jpg)

Clip A

Clip B
Clip C
Clip D Clip E

b

b0
b1
b2
b3
b4

1
b
b
b
b
−
−
=
β

1
2

Figure 12.35. A generalized linear blend between N animation clips.

Pivotal Movement

To implement pivotal movement, we can simply play the forward locomotion
loop while rotating the entire character about its vertical axis to make it turn.
Pivotal movement looks more natural if the character’s body doesn’t remain
bolt upright when it is turning—real humans tend to lean into their turns a
little bit. We could try slightly tilting the vertical axis of the character as a
whole, but that would cause problems with the inner foot sinking into the
ground while the outer foot comes off the ground. A more natural-looking
result can be achieved by animating three variations on the basic forward walk
or run—one going perfectly straight, one making an extreme left turn and one
making an extreme right turn. We can then LERP-blend between the straight
clip and the extreme left turn clip to implement any desired lean angle.

### 12.6.3 Complex LERP Blends

In a real game engine, characters make use of a wide range of complex blends
for various purposes. It can be convenient to “prepackage” certain commonly
used types of complex blends for ease of use. In the following sections, we’ll
investigate a few popular types of prepackaged complex blends.

12.6.3.1
Generalized One-Dimensional LERP Blending

LERP blending can be easily extended to more than two animation clips, us-
ing a technique I call one-dimensional LERP blending. We define a new blend
parameter b that lies in any linear range desired (e.g., from −1 to +1, or from
0 to 1, or even from 27 to 136). Any number of clips can be positioned at arbi-
trary points along this range, as shown in Figure 12.35. For any given value of
b, we select the two clips immediately adjacent to it and blend them together
using Equation (12.5). If the two adjacent clips lie at points b1 and b2, then
the blend percentage β can be determined using a technique analogous to that
used in Equation (12.14), as follows:

β(t) = b −b1

b2 −b1
.
(12.15)


<!-- source-pdf-page: 783 -->
> Visual fallback for diagrams/images: [PDF page 783](../../../visual_pages/page_0783.jpg)

Strafe

Run
Fwd

Strafe

Right

Left

b
b3
b1

b2

Run
Forward

Strafe
Right
Strafe
Left

Figure 12.36. The directional clips used in targeted movement can be thought of as a special case
of one-dimensional LERP blending.

Targeted movement is just a special case of one-dimensional LERP blend-
ing. We simply straighten out the circle on which the directional animation
clips were placed and use the movement direction angle θ as the parameter b
(with a range of −90 to 90 degrees). Any number of animation clips can be
placed onto this blend range at arbitrary angles. This is shown in Figure 12.36.

12.6.3.2
Simple Two-Dimensional LERP Blending

Sometimes we would like to smoothly vary two aspects of a character’s motion
simultaneously. For example, we might want the character to be capable of
aiming his weapon vertically and horizontally. Or we might want to allow our
character to vary her pace length and the separation of her feet as she moves.
We can extend one-dimensional LERP blending to two dimensions in order to
achieve these kinds of effects.
If we know that our 2D blend involves only four animation clips, and if
those clips are positioned at the four corners of a square region, then we can
find a blended pose by performing two 1D blends. Our generalized blend
factor b becomes a two-dimensional blend vector b =
[bx
by
]
. If b lies within
the square region bounded by our four clips, we can find the resulting pose by
following these steps:

1.
Using the horizontal blend factor bx, find two intermediate poses, one
between the top two animation clips and one between the bottom two
clips. These two poses can be found by performing two simple one-


<!-- source-pdf-page: 784 -->
> Visual fallback for diagrams/images: [PDF page 784](../../../visual_pages/page_0784.jpg)

by

bx

Figure 12.37. A simple formulation for 2D animation blending between four clips at the corners of
a square region.

dimensional LERP blends.

2.
Using the vertical blend factor by, find the final pose by LERP-blending
the two intermediate poses together.

This technique is illustrated in Figure 12.37.

12.6.3.3
Triangular Two-Dimensional LERP Blending

The simple 2D blending technique we investigated in the previous section only
works when the animation clips we wish to blend lie at the corners of a rect-
angular region. How can we blend between an arbitrary number of clips po-
sitioned at arbitrary locations in our 2D blend space?
Let’s imagine that we have three animation clips that we wish to blend to-
gether. Each clip, designated by the index i, corresponds to a particular blend
coordinate bi =
[bix
biy
]
in our two-dimensional blend space; these three
blend coordinates form a triangle within the blend space. Each of the three
clips defines a set of joint poses
{ (Pi)j
}

N−1
j=0 , where (Pi)j is the pose of joint j
as defined by clip i, and N is the number of joints in the skeleton. We wish to
find the interpolated pose of the skeleton corresponding to an arbitrary point
b within the triangle, as illustrated in Figure 12.38.
But how can we calculate a LERP blend between three animation clips?
Thankfully, the answer is simple: the LERP function can actually operate on
any number of inputs, because it is really just a weighted average. As with any
weighted average, the weights must add to one. In the case of a two-input
LERP blend, we used the weights β and (1 −β), which of course add to one.
For a three-input LERP, we simply use three weights, α, β and γ = (1 −α −β).


<!-- source-pdf-page: 785 -->
> Visual fallback for diagrams/images: [PDF page 785](../../../visual_pages/page_0785.jpg)

by

Clip A
b0

b1

Final
Blend

Clip B

b

bx

Clip C

b2

Figure 12.38. Two-dimensional animation blending between three animation clips.

Then we calculate the LERP as follows:

(PLERP)j = α (P0)j + β (P1)j + γ (P2)j .
(12.16)

Given the two-dimensional blend vector b, we find the blend weights α,
β and γ by finding the barycentric coordinates of the point b relative to the tri-
angle formed by the three clips in two-dimensional blend space (http://en.
wikipedia.org/wiki/Barycentric_coordinates_%28mathematics%29). In gen-
eral, the barycentric coordinates of a point b within a triangle with vertices b1,
b2 and b3 are three scalar values (α, β, γ) that satisfy the relations

b = αb0 + βb1 + γb2,
(12.17)

and
α + β + γ = 1.

These are exactly the weights we seek for our three-clip weighted average.
Barycentric coordinates are illustrated in Figure 12.39.
Note that plugging the barycentric coordinate (1, 0, 0) into Equation (12.17)
yields b0, while (0, 1, 0) gives us b1 and (0, 0, 1) produces b2. Likewise, plug-
ging these blend weights into Equation (12.16) gives us poses (P0)j, (P1)j and
(P2)j for each joint j, respectively. Furthermore, the barycentric coordinate ( 1

3,

1
3, 1

3) lies at the centroid of the triangle and gives us an equal blend between the
three poses. This is exactly what we’d expect.

12.6.3.4
Generalized Two-Dimensional LERP Blending

The barycentric coordinate technique can be extended to an arbitrary number
of animation clips positioned at arbitrary locations within the two-dimension-
al blend space. We won’t describe it in its entirety here, but the basic idea is
to use a technique known as Delaunay triangulation (http://en.wikipedia.org/


<!-- source-pdf-page: 786 -->
> Visual fallback for diagrams/images: [PDF page 786](../../../visual_pages/page_0786.jpg)

by

b0

α

b1

β

b

γ

bx

b2

Figure 12.39. Various barycentric coordinates within a triangle.

wiki/Delaunay_triangulation) to find a set of triangles given the positions of
the various animation clips bi. Once the triangles have been determined, we
can find the triangle that encloses the desired point b and then perform a three-
clip LERP blend as described above. This technique was used in FIFA soccer
by EA Sports in Vancouver, implemented within their proprietary “ANT” an-
imation framework. It is shown in Figure 12.40.

### 12.6.4 Partial-Skeleton Blending

A human being can control different parts of his or her body independently.
For example, I can wave my right arm while walking and pointing at some-
thing with my left arm. One way to implement this kind of movement in a

Clip A
b0

Clip B
b1

by

Clip C

b2

b4
b5

Clip D
Clip E

Clip F

b3

b6

Clip G

b9

Clip J

bx

b7
b8

Clip H
Clip I

Figure 12.40. Delaunay triangulation between an arbitrary number of animation clips positioned
at arbitrary locations in two-dimensional blend space.


<!-- source-pdf-page: 787 -->

game is via a technique known as partial-skeleton blending.
Recall from Equations (12.5) and (12.6) that when doing regular LERP
blending, the same blend percentage β was used for every joint in the skele-
ton. Partial-skeleton blending extends this idea by permitting the blend per-
centage to vary on a per-joint basis. In other words, for each joint j, we define
a separate blend percentage βj. The set of all blend percentages for the entire

skeleton
{
βj
}

N−1
j=0 is sometimes called a blend mask because it can be used to
“mask out” certain joints by setting their blend percentages to zero.
As an example, let’s say we want our character to wave at someone using
his right arm and hand. Moreover, we want him to be able to wave whether
he’s walking, running or standing still. To implement this using partial blend-
ing, the animator defines three full-body animations: Walk, Run and Stand.
The animator also creates a single waving animation, Wave. A blend mask
is created in which the blend percentages are zero everywhere except for the
right shoulder, elbow, wrist and finger joints, where they are equal to one:

βj =
{ 1
when j within right arm,
0
otherwise.

When Walk, Run or Stand is LERP-blended with Wave using this blend mask,
the result is a character who appears to be walking, running or standing while
waving his right arm.
Partial blending is useful, but it has a tendency to make a character’s move-
ments look unnatural. This occurs for two basic reasons:

•
An abrupt change in the per-joint blend factors can cause the movements
of one part of the body to appear disconnected from the rest of the body.
In our example, the blend factors change abruptly at the right shoulder
joint. Hence the animation of the upper spine, neck and head are being
driven by one animation, while the right shoulder and arm joints are
being entirely driven by a different animation. This can look odd. The
problem can be mitigated somewhat by gradually changing the blend
factors rather than doing it abruptly. (In our example, we might select a
blend percentage of 0.9 at the right shoulder, 0.5 on the upper spine and
0.2 on the neck and mid-spine.)

•
The movements of a real human body are never totally independent. For
example, one would expect a person’s wave to look more “bouncy” and
out of control when he or she is running than when he or she is standing
still. Yet with partial blending, the right arm’s animation will be identical
no matter what the rest of the body is doing. This problem is difficult to


<!-- source-pdf-page: 788 -->

overcome using partial blending. Instead, many game developers have
turned to a more natural-looking technique known as additive blending.

### 12.6.5 Additive Blending

Additive blending approaches the problem of combining animations in a to-
tally new way. It introduces a new kind of animation called a difference clip,
which, as its name implies, represents the difference between two regular an-
imation clips. A difference clip can be added onto a regular animation clip in
order to produce interesting variations in the pose and movement of the char-
acter. In essence, a difference clip encodes the changes that need to be made to
one pose in order to transform it into another pose. Difference clips are often
called additive animation clips in the game industry. We’ll stick with the term
difference clip in this book because it more accurately describes what is going
on.
Consider two input clips called the source clip (S) and the reference clip (R).
Conceptually, the difference clip is D = S −R. If a difference clip D is added
to its original reference clip, we get back the source clip (S = D + R). We
can also generate animations that are partway between R and S by adding a
percentage of D to R, in much the same way that LERP blending finds inter-
mediate animations between two extremes. However, the real beauty of the
additive blending technique is that once a difference clip has been created, it
can be added to other unrelated clips, not just to the original reference clip.
We’ll call these animations target clips and denote them with the symbol T.
As an example, if the reference clip has the character running normally and
the source clip has him running in a tired manner, then the difference clip will
contain only the changes necessary to make the character look “tired” while
running. If this difference clip is now applied to a clip of the character walking,
the resulting animation can make the character look tired while walking. A
whole host of interesting and very natural-looking animations can be created
by adding a single difference clip onto various “regular” animation clips, or a
collection of difference clips can be created, each of which produces a different
effect when added to a single target animation.

12.6.5.1
Mathematical Formulation

A difference animation D is defined as the difference between some source
animation S and some reference animation R. So conceptually, the difference
pose (at a single point in time) is D = S −R. Of course, we’re dealing with
joint poses, not scalar quantities, so we cannot simply subtract the poses. In
general, a joint pose is a 4 × 4 affine transformation matrix that transforms


<!-- source-pdf-page: 789 -->

points and vectors from the child joint’s local space to the space of its parent
joint. The matrix equivalent of subtraction is multiplication by the inverse
matrix. So given the source pose Sj and the reference pose Rj for any joint j in
the skeleton, we can define the difference pose Dj at that joint as follows. (For
this discussion, we’ll drop the C →P or j →p(j) subscript, as it is understood
that we are dealing with child-to-parent pose matrices.)

Dj = SjR−1
j
.

“Adding” a difference pose Dj onto a target pose Tj yields a new additive
pose Aj. This is achieved by simply concatenating the difference transform
and the target transform as follows:

Aj = DjTj =
(
SjR−1
j
)
Tj.
(12.18)

We can verify that this is correct by looking at what happens when the differ-
ence pose is “added” back onto the original reference pose:

Aj = DjRj
= SjR−1
j
Rj
= Sj.

In other words, adding the difference animation D back onto the original ref-
erence animation R yields the source animation S, as we’d expect.

Temporal Interpolation of Difference Clips

As we learned in Section 12.4.1.1, game animations are almost never sampled
on integer frame indices. To find a pose at an arbitrary time t, we must of-
ten temporally interpolate between adjacent pose samples at times t1 and t2.
Thankfully, difference clips can be temporally interpolated just like their non-
additive counterparts. We can simply apply Equations (12.12) and (12.14) di-
rectly to our difference clips as if they were ordinary animations.
Note that a difference animation can only be found when the input clips
S and R are of the same duration. Otherwise there would be a period of time
during which either S or R is undefined, meaning D would be undefined as
well.

Additive Blend Percentage

In games, we often wish to blend in only a percentage of a difference animation
to achieve varying degrees of the effect it produces. For example, if a difference
clip causes the character to turn his head 80 degrees to the right, blending in


<!-- source-pdf-page: 790 -->

50% of the difference clip should make him turn his head only 40 degrees to
the right.
To accomplish this, we turn once again to our old friend LERP. We wish
to interpolate between the unaltered target animation and the new animation
that would result from a full application of the difference animation. To do
this, we extend Equation (12.18) as follows:

Aj = LERP
(
Tj, DjTj, β
)
(12.19)
= (1 −β)
(
Tj
) + β
(
DjTj
)
.

As we saw in Chapter 5, we cannot LERP matrices directly. So Equation
(11.16) must be broken down into three separate interpolations for S, Q and T,
just as we did in Equations (12.7) through (12.11).

12.6.5.2
Additive Blending versus Partial Blending

Additive blending is similar in some ways to partial blending. For example,
we can take the difference between a standing clip and a clip of standing while
waving the right arm. The result will be almost the same as using a partial
blend to make the right arm wave. However, additive blends suffer less from
the “disconnected” look of animations combined via partial blending. This is
because, with an additive blend, we are not replacing the animation for a sub-
set of joints or interpolating between two potentially unrelated poses. Rather,
we are adding movement to the original animation—possibly across the entire
skeleton. In effect, a difference animation “knows” how to change a charac-
ter’s pose in order to get him to do something specific, like being tired, aiming
his head in a certain direction, or waving his arm. These changes can be ap-
plied to a reasonably wide variety of animations, and the result often looks
very natural.

12.6.5.3
Limitations of Additive Blending

Of course, additive animation is not a silver bullet. Because it adds movement
to an existing animation, it can have a tendency to over-rotate the joints in the
skeleton, especially when multiple difference clips are applied simultaneously.
As a simple example, imagine a target animation in which the character’s left
arm is bent at a 90 degree angle. If we add a difference animation that also
rotates the elbow by 90 degrees, then the net effect would be to rotate the arm
by 90 + 90 = 180 degrees. This would cause the lower arm to interpenetrate
the upper arm—not a comfortable position for most individuals!
Clearly we must be careful when selecting the reference clip and also when
choosing the target clips to which to apply it. Here are some simple rules of
thumb:


<!-- source-pdf-page: 791 -->
> Visual fallback for diagrams/images: [PDF page 791](../../../visual_pages/page_0791.jpg)

•
Keep hip rotations to a minimum in the reference clip.

•
The shoulder and elbow joints should usually be in neutral poses in the
reference clip to minimize over-rotation of the arms when the difference
clip is added to other targets.

•
Animators should create a new difference animation for each core pose
(e.g., standing upright, crouched down, lying prone, etc.). This allows
the animator to account for the way in which a real human would move
when in each of these stances.

These rules of thumb can be a helpful starting point, but the only way to
really learn how to create and apply difference clips is by trial and error or by
apprenticing with animators or engineers who have experience creating and
applying difference animations. If your team hasn’t used additive blending
in the past, expect to spend a significant amount of time learning the art of
additive blending.

### 12.6.6 Applications of Additive Blending

12.6.6.1
Stance Variation

One particularly striking application of additive blending is stance variation.
For each desired stance, the animator creates a one-frame difference anima-
tion. When one of these single-frame clips is additively blended with a base
animation, it causes the entire stance of the character to change drastically
while he continues to perform the fundamental action he’s supposed to per-
form. This idea is illustrated in Figure 12.41.

Target Clip
(and Reference)

Target +
Difference A

Target +
Difference B

Figure 12.41. Two single-frame difference animations A and B can cause a target animation clip to
assume two totally different stances. (Character from Uncharted: Drake’s Fortune, © 2007/® SIE.
Created and developed by Naughty Dog.)


<!-- source-pdf-page: 792 -->
> Visual fallback for diagrams/images: [PDF page 792](../../../visual_pages/page_0792.jpg)

Target Clip
(and Reference)

Target +
Difference A

Target +
Difference B

Target +
Difference C

Figure 12.42. Additive blends can be used to add variation to a repetitive idle animation. Images
courtesy of Naughty Dog, Inc., © 2014/™SIE.

12.6.6.2
Locomotion Noise

Real humans don’t run exactly the same way with every footfall—there is vari-
ation in their movement over time. This is especially true if the person is dis-
tracted (for example, by attacking enemies). Additive blending can be used to
layer randomness, or reactions to distractions, on top of an otherwise entirely
repetitive locomotion cycle. This is illustrated in Figure 12.42.

12.6.6.3
Aim and Look-At

Another common use for additive blending is to permit the character to look
around or to aim his weapon. To accomplish this, the character is first ani-
mated doing some action, such as running, with his head or weapon facing
straight ahead. Then the animator changes the direction of the head or the
aim of the weapon to the extreme right and saves off a one-frame or multi-
frame difference animation. This process is repeated for the extreme left, up
and down directions. These four difference animations can then be additively
blended onto the original straight-ahead animation clip, causing the character
to aim right, left, up, down or anywhere in between.
The angle of the aim is governed by the additive blend factor of each clip.
For example, blending in 100% of the right additive causes the character to aim
as far right as possible. Blending 50% of the left additive causes him to aim at


<!-- source-pdf-page: 793 -->
> Visual fallback for diagrams/images: [PDF page 793](../../../visual_pages/page_0793.jpg)

Target Clip
(and Reference)

Target +
Difference Right

Target +
Difference Left

0% Right

0% Left
100% Right
100% Left

Figure 12.43. Additive blending can be used to aim a weapon. Screenshots courtesy of Naughty
Dog, Inc., © 2014/™SIE.

an angle that is one-half of his leftmost aim. We can also combine this with an
up or down additive to aim diagonally. This is demonstrated in Figure 12.43.

12.6.6.4
Overloading the Time Axis

It’s interesting to note that the time axis of an animation clip needn’t be used
to represent time. For example, a three-frame animation clip could be used to
provide three aim poses to the engine—a left aim pose on frame 1, a forward
aim pose on frame 2 and a right aim pose on frame 3. To make the character
aim to the right, we can simply fix the local clock of the aim animation on frame
3. To perform a 50% blend between aiming forward and aiming right, we can
dial in frame 2.5. This is a great example of leveraging existing features of the
engine for new purposes.

## 12.7 Post-Processing

Once a skeleton has been posed by one or more animation clips and the results
have been blended together using linear interpolation or additive blending, it
is often necessary to modify the pose prior to rendering the character. This is
called animation post-processing. In this section, we’ll look at a few of the most
common kinds of animation post-processing.


<!-- source-pdf-page: 794 -->

### 12.7.1 Procedural Animations

A procedural animation is any animation generated at runtime rather than being
driven by data exported from an animation tool such as Maya. Sometimes,
hand-animated clips are used to pose the skeleton initially, and then the pose
is modified in some way via procedural animation as a post-processing step.
A procedural animation can also be used as an input to the system in place of
a hand-animated clip.
For example, imagine that a regular animation clip is used to make a vehicle
appear to be bouncing up and down on the terrain as it moves. The direction in
which the vehicle travels is under player control. We would like to adjust the
rotation of the front wheels and steering wheel so that they move convincingly
when the vehicle is turning. This can be done by post-processing the pose
generated by the animation. Let’s assume that the original animation has the
front tires pointing straight ahead and the steering wheel in a neutral position.
We can use the current angle of turn to create a quaternion about the vertical
axis that will deflect the front tires by the desired amount. This quaternion can
be multiplied with the front tire joints’ Q channel to produce the final pose of
the tires. Likewise, we can generate a quaternion about the axis of the steering
column and multiply it into the steering wheel joint’s Q channel to deflect it.
These adjustments are made to the local pose, prior to global pose calculation
and matrix palette generation (see Section 12.5).
As another example, let’s say that we wish to make the trees and bushes
in our game world sway naturally in the wind and get brushed aside when
characters move through them. We can do this by modeling the trees and
bushes as skinned meshes with simple skeletons. Procedural animation can
be used, in place of or in addition to hand-animated clips, to cause the joints
to move in a natural-looking way. We might apply one or more sinusoids, or
a Perlin noise function, to the rotation of various joints to make them sway in
the breeze, and when a character moves through a region containing a bush
or grass, we can deflect its root joint quaternion radially outward to make it
appear to be pushed over by the character.

### 12.7.2 Inverse Kinematics

Let’s say we have an animation clip in which a character leans over to pick up
an object from the ground. In Maya, the clip looks great, but in our production
game level, the ground is not perfectly flat, so sometimes the character’s hand
misses the object or appears to pass through it. In this case, we would like to
adjust the final pose of the skeleton so that the hand lines up exactly with the
target object. A technique known as inverse kinematics (IK) can be used to make


<!-- source-pdf-page: 795 -->
> Visual fallback for diagrams/images: [PDF page 795](../../../visual_pages/page_0795.jpg)

Figure 12.44. Inverse kinematics attempts to bring an end effector joint into a target global pose
by minimizing the error between them.

this happen.

A regular animation clip is an example of forward kinematics (FK). In for-
ward kinematics, the input is a set of local joint poses, and the output is a
global pose and a skinning matrix for each joint. Inverse kinematics goes in
the other direction: The input is the desired global pose of a single joint, which
is known as the end effector. We solve for the local poses of other joints in the
skeleton that will bring the end effector to the desired location.

Mathematically, IK boils down to an error minimization problem. As with
most minimization problems, there might be one solution, many or none at all.
This makes intuitive sense: If I try to reach a doorknob that is on the other side
of the room, I won’t be able to reach it without walking over to it. IK works best
when the skeleton starts out in a pose that is reasonably close to the desired
target. This helps the algorithm to focus in on the “closest” solution and to do
so in a reasonable amount of processing time. Figure 12.44 shows IK in action.

Imagine a two-joint skeleton, each of which can rotate only about a single
axis. The rotation of these two joints can be described by a two-dimensional
angle vector θ =
[
θ1
θ2
]
. The set of all possible angles for our two joints
forms a two-dimensional space called configuration space. Obviously, for more
complex skeletons with more degrees of freedom per joint, configuration space
becomes multidimensional, but the concepts described here work equally well
no matter how many dimensions we have.

Now imagine plotting a three-dimensional graph, where for each combi-
nation of joint rotations (i.e., for each point in our two-dimensional configu-
ration space), we plot the distance from the end effector to the desired target.
An example of this kind of plot is shown in Figure 12.45. The “valleys” in
this three-dimensional surface represent regions in which the end effector is
as close as possible to the target. When the height of the surface is zero, the
end effector has reached its target. Inverse kinematics, then, attempts to find


<!-- source-pdf-page: 796 -->
> Visual fallback for diagrams/images: [PDF page 796](../../../visual_pages/page_0796.jpg)

dtarget

 2

Minimum

 1

Figure 12.45. A three-dimensional plot of the distance from the end effector to the target for each
point in two-dimensional conﬁguration space. IK ﬁnds the local minimum.

minima (low points) on this surface.
We won’t get into the details of solving the IK minimization problem
here. You can read more about IK at http://en.wikipedia.org/wiki/Inverse_
kinematics and in Jason Weber’s article, “Constrained Inverse Kinematics”
[47].

### 12.7.3 Rag Dolls

A character’s body goes limp when he dies or becomes unconscious. In such
situations, we want the body to react in a physically realistic way with its sur-
roundings. To do this, we can use a rag doll. A rag doll is a collection of phys-
ically simulated rigid bodies, each one representing a semi-rigid part of the
character’s body, such as his lower arm or his upper leg. The rigid bodies are
constrained to one another at the joints of the character in such a way as to
produce natural-looking “lifeless” body movement. The positions and orien-
tations of the rigid bodies are determined by the physics system and are then
used to drive the positions and orientations of certain key joints in the charac-
ter’s skeleton. The transfer of data from the physics system to the skeleton is
typically done as a post-processing step.
To really understand rag doll physics, we must first have an understanding
of how the collision and physics systems work. Rag dolls are covered in more
detail in Sections 13.4.8.7 and 13.5.3.8.

## 12.8 Compression Techniques

Animation data can take up a lot of memory. A single joint pose might be com-
posed of ten floating-point channels (three for translation, four for rotation and


<!-- source-pdf-page: 797 -->

up to three more for scale). Assuming each channel contains a 4-byte floating-
point value, a one-second clip sampled at 30 samples per second would occupy
4 bytes × 10 channels × 30 samples/second = 1200 bytes per joint per second,
or a data rate of about 1.17 KiB per joint per second. For a 100-joint skeleton
(which is small by today’s standards), an uncompressed animation clip would
occupy 117 KiB per joint per second. If our game contained 1,000 seconds of
animation (which is on the low side for a modern game), the entire dataset
would occupy a whopping 114.4 MiB. That’s quite a lot, considering that a
PlayStation 3 has only 256 MiB of main RAM and 256 MiB of video RAM.
Sure, the PS4 has 8 GiB of RAM. But even so—we would rather have much
richer animations with a lot more variety than waste memory unnecessarily.
Therefore, game engineers invest a significant amount of effort into compress-
ing animation data in order to permit the maximum richness and variety of
movement at the minimum memory cost.

### 12.8.1 Channel Omission

One simple way to reduce the size of an animation clip is to omit channels
that are irrelevant. Many characters do not require nonuniform scaling, so
the three scale channels can be reduced to a single uniform scale channel. In
some games, the scale channel can actually be omitted altogether for all joints
(except possibly the joints in the face). The bones of a humanoid character
generally cannot stretch, so translation can often be omitted for all joints ex-
cept the root, the facial joints and sometimes the collar bones. Finally, because
quaternions are always normalized, we can store only three components per
quat (e.g., x, y and z) and reconstruct the fourth component (e.g., w) at run-
time.
As a further optimization, channels whose pose does not change over the
course of the entire animation can be stored as a single sample at time t = 0
plus a single bit indicating that the channel is constant for all other values of t.
Channel omission can significantly reduce the size of an animation clip. A
100-joint character with no scale and no translation requires only 303 chan-
nels—three channels for the quaternions at each joint, plus three channels for
the root joint’s translation. Compare this to the 1,000 channels that would be
required if all ten channels were included for all 100 joints.

### 12.8.2 Quantization

Another way to reduce the size of an animation is to reduce the size of each
channel. A floating-point value is normally stored in 32-bit IEEE format. This
format provides 23 bits of precision in the mantissa and an 8-bit exponent.


<!-- source-pdf-page: 798 -->

However, it’s often not necessary to retain that kind of precision and range in
an animation clip. When storing a quaternion, the channel values are guaran-
teed to lie in the range [−1, 1]. At a magnitude of 1, the exponent of a 32-bit
IEEE float is zero, and 23 bits of precision give us accuracy down to the sev-
enth decimal place. Experience shows that a quaternion can be encoded well
with only 16 bits of precision, so we’re really wasting 16 bits per channel if we
store our quats using 32-bit floats.
Converting a 32-bit IEEE float into an n-bit integer representation is called
quantization. There are actually two components to this operation: Encoding is
the process of converting the original floating-point value to a quantized in-
teger representation. Decoding is the process of recovering an approximation
to the original floating-point value from the quantized integer. (We can only
recover an approximation to the original data—quantization is a lossy compres-
sion method because it effectively reduces the number of bits of precision used
to represent the value.)
To encode a floating-point value as an integer, we first divide the valid
range of possible input values into N equally sized intervals. We then deter-
mine within which interval a particular floating-point value lies and represent
that value by the integer index of its interval. To decode this quantized value,
we simply convert the integer index into floating-point format and shift and
scale it back into the original range. N is usually chosen to correspond to the
range of possible integer values that can be represented by an n-bit integer.
For example, if we’re encoding a 32-bit floating-point value as a 16-bit integer,
the number of intervals would be N = 216 = 65,536.
Jonathan Blow wrote an excellent article on the topic of floating-point
scalar quantization in the Inner Product column of Game Developer Magazine,
available at https://bit.ly/2J92oiU. The article presents two ways to map a
floating-point value to an interval during the encoding process: We can either
truncate the float to the next lowest interval boundary (T encoding), or we can
round the float to the center of the enclosing interval (R encoding). Likewise,
it describes two approaches to reconstructing the floating-point value from its
integer representation: We can either return the value of the left-hand side of
the interval to which our original value was mapped (L reconstruction), or we
can return the value of the center of the interval (C reconstruction). This gives
us four possible encode/decode methods: TL, TC, RL and RC. Of these, TL
and RC are to be avoided because they tend to remove or add energy to the
dataset, which can often have disastrous effects. TC has the benefit of being
the most efficient method in terms of bandwidth, but it suffers from a severe
problem—there is no way to represent the value zero exactly. (If you encode
0.0f, it becomes a small positive value when decoded.) RL is therefore usually


<!-- source-pdf-page: 799 -->

the best choice and is the method we’ll demonstrate here.
The article only talks about quantizing positive floating-point values, and
in the examples, the input range is assumed to be [0, 1] for simplicity. How-
ever, we can always shift and scale any floating-point range into the range
[0, 1]. For example, the range of quaternion channels is [−1, 1], but we can
convert this to the range [0, 1] by adding one and then dividing by two.
The following pair of routines encode and decode an input floating-point
value lying in the range [0, 1] into an n-bit integer, according to Jonathan
Blow’s RL method. The quantized value is always returned as a 32-bit un-
signed integer (U32), but only the least-significant n bits are actually used, as
specified by the nBits argument. For example, if you pass nBits==16, you
can safely cast the result to a U16.

U32 CompressUnitFloatRL(F32 unitFloat, U32 nBits)
{
// Determine the number of intervals based on the
// number of output bits we've been asked to produce.
U32 nIntervals = 1u << nBits;

// Scale the input value from the range [0, 1] into
// the range [0, nIntervals - 1]. We subtract one
// interval because we want the largest output value
// to fit into nBits bits.
F32 scaled = unitFloat * (F32)(nIntervals - 1u);

// Finally, round to the nearest interval center. We
// do this by adding 0.5f and then truncating to the
// next-lowest interval index (by casting to U32).
U32 rounded = (U32)(scaled + 0.5f);

// Guard against invalid input values.
if (rounded > nIntervals - 1u)
rounded = nIntervals - 1u;

return rounded;
}

F32 DecompressUnitFloatRL(U32 quantized, U32 nBits)
{
// Determine the number of intervals based on the
// number of bits we used when we encoded the value.
U32 nIntervals = 1u << nBits;

// Decode by simply converting the U32 to an F32, and
// scaling by the interval size.


<!-- source-pdf-page: 800 -->

F32 intervalSize = 1.0f / (F32)(nIntervals - 1u);
F32 approxUnitFloat = (F32)quantized * intervalSize;

return approxUnitFloat;
}

To handle arbitrary input values in the range [min, max], we can use these
routines:

U32 CompressFloatRL(F32 value, F32 min, F32 max,
U32 nBits)
{
F32 unitFloat = (value - min) / (max - min);
U32 quantized = CompressUnitFloatRL(unitFloat,
nBits);
return quantized;
}

F32 DecompressFloatRL(U32 quantized, F32 min, F32 max,
U32 nBits)
{
F32 unitFloat = DecompressUnitFloatRL(quantized,
nBits);
F32 value = min + (unitFloat * (max - min));
return value;
}

Let’s return to our original problem of animation channel compression. To
compress and decompress a quaternion’s four components into 16 bits per
channel, we simply call CompressFloatRL() and DecompressFloatRL()
with min = −1, max = 1 and n = 16:

inline U16 CompressRotationChannel(F32 qx)
{
return (U16)CompressFloatRL(qx, -1.0f, 1.0f, 16u);
}

inline F32 DecompressRotationChannel(U16 qx)
{
return DecompressFloatRL((U32)qx, -1.0f, 1.0f, 16u);
}

Compression of translation channels is a bit trickier than rotations, because
unlike quaternion channels, the range of a translation channel could theoreti-
cally be unbounded. Thankfully, the joints of a character don’t move very far
in practice, so we can decide upon a reasonable range of motion and flag an


<!-- source-pdf-page: 801 -->

error if we ever see an animation that contains translations outside the valid
range. In-game cinematics are an exception to this rule—when an IGC is ani-
mated in world space, the translations of the characters’ root joints can grow
very large. To address this, we can select the range of valid translations on
a per-animation or per-joint basis, depending on the maximum translations
actually achieved within each clip. Because the data range might differ from
animation to animation, or from joint to joint, we must store the range with the
compressed clip data. This will add a tiny amount of data to each animation
clip, but the impact is generally negligible.

// We'll use a 2 m range -- your mileage may vary.
F32 MAX_TRANSLATION = 2.0f;

inline U16 CompressTranslationChannel(F32 vx)
{
// Clamp to valid range...
if (vx < -MAX_TRANSLATION)
vx = -MAX_TRANSLATION;
if (vx > MAX_TRANSLATION)
vx = MAX_TRANSLATION;

return (U16)CompressFloatRL(vx,
-MAX_TRANSLATION, MAX_TRANSLATION, 16);
}

inline F32 DecompressTranslationChannel(U16 vx)
{
return DecompressFloatRL((U32)vx,
-MAX_TRANSLATION, MAX_TRANSLATION, 16);
}

### 12.8.3 Sampling Frequency and Key Omission

Animation data tends to be large for three reasons: first, because the pose of
each joint can contain upwards of ten channels of floating-point data; second,
because a skeleton contains a large number of joints (250 or more for a hu-
manoid character on PS3 or Xbox 360, and more than 800 on some PS4 and
Xbox One games); third, because the pose of the character is typically sampled
at a high rate (e.g., 30 frames per second). We’ve seen some ways to address
the first problem. We can’t really reduce the number of joints for our high-
resolution characters, so we’re stuck with the second problem. To attack the
third problem, we can do two things:

•
Reduce the sample rate overall. Some animations look fine when exported


<!-- source-pdf-page: 802 -->
> Visual fallback for diagrams/images: [PDF page 802](../../../visual_pages/page_0802.jpg)

at 15 samples per second, and doing so cuts the animation data size in
half.
•
Omit some of the samples. If a channel’s data varies in an approximately
linear fashion during some interval of time within the clip, we can omit
all of the samples in this interval except the endpoints. Then, at runtime,
we can use linear interpolation to recover the dropped samples.

The latter technique is a bit involved, and it requires us to store information
about the time of each sample. This additional data can erode the savings we
achieved by omitting samples in the first place. However, some game engines
have used this technique successfully.

### 12.8.4 Curve-Based Compression

One of the most powerful, easiest-to-use and best-thought-out animation APIs
I’ve ever worked with is Granny, by Rad Game Tools. Granny stores anima-
tions not as a regularly spaced sequence of pose samples but as a collection of
nth-order, nonuniform, nonrational B-splines, describing the paths of a joint’s
S, Q and T channels over time. Using B-splines allows channels with a lot of
curvature to be encoded using only a few data points.
Granny exports an animation by sampling the joint poses at regular inter-
vals, much like traditional animation data. For each channel, Granny then fits
a set of B-splines to the sampled dataset to within a user-specified tolerance.
The end result is an animation clip that is usually significantly smaller than
its uniformly sampled, linearly interpolated counterpart. This process is illus-
trated in Figure 12.46.

Qx1

t

Figure 12.46. One form of animation compression ﬁts B-splines to the animation channel data.

### 12.8.5 Wavelet Compression

Another way to compress animation data is to apply signal processing theory
to the problem, via a technique known as wavelet compression. A wavelet is a
function whose amplitude oscillates like a wave but whose duration is very


<!-- source-pdf-page: 803 -->

short, like a brief ripple in a pond. Wavelet functions are carefully crafted to
give them desirable properties for use in signal processing.
In wavelet compression, an animation curve is decomposed into a sum of
orthonormal wavelets, in much the same way that an arbitrary signal can be
represented as a train of delta functions or a sum of sinusoids. We discuss sig-
nal processing and linear time-invariant systems in some depth in Section 14.2;
the concepts presented there form the foundations necessary to understand
wavelet compression. A full discussion of wavelet-based compression tech-
niques is well beyond the scope of this book, but you can read more about it
online. Search for “wavelet” to find introductory articles on the topic, and then
try searching for “Animation Compression: Signal Processing” on Nicholas
Frechette’s blog for a great article on how wavelet compression was imple-
mented for Thief (2014) by Eidos Montreal.

### 12.8.6 Selective Loading and Streaming

The cheapest animation clip is the one that isn’t in memory at all. Most games
don’t need every animation clip to be in memory simultaneously. Some clips
apply only to certain classes of character, so they needn’t be loaded during
levels in which that class of character is never encountered. Other clips ap-
ply to one-off moments in the game. These can be loaded or streamed into
memory just before being needed and dumped from memory once they have
played.
Most games load a core set of animation clips into memory when the game
first boots and keep them there for the duration of the game. These include
the player character’s core move set and animations that apply to objects that
reappear over and over throughout the game, such as weapons or power-ups.
All other animations are usually loaded on an as-needed basis. Some game
engines load animation clips individually, but many package them together
into logical groups that can be loaded and unloaded as a unit.

## 12.9 The Animation Pipeline

The operations performed by the low-level animation engine form a pipeline
that transforms its inputs (animation clips and blend specifications) into the
desired outputs (local and global poses, plus a matrix palette for rendering).
For each animating character and object in the game, the animation pipe-
line takes one or more animation clips and corresponding blend factors as in-
put, blends them together, and generates a single local skeletal pose as out-
put. It also calculates a global pose for the skeleton and a palette of skinning


<!-- source-pdf-page: 804 -->
> Visual fallback for diagrams/images: [PDF page 804](../../../visual_pages/page_0804.jpg)

matrices for use by the rendering engine. Post-processing hooks are usually
provided, which permit the local pose to be modified prior to final global pose
and matrix palette generation. This is where inverse kinematics (IK), rag doll
physics and other forms of procedural animation are applied to the skeleton.
The stages of this pipeline are:

1.
Clip decompression and pose extraction. In this stage, each individual clip’s
data is decompressed, and a static pose is extracted for the time index in
question. The output of this phase is a local skeletal pose for each input
clip. This pose might contain information for every joint in the skeleton
(a full-body pose), for only a subset of joints (a partial pose), or it might be
a difference pose for use in additive blending.

2.
Pose blending. In this stage, the input poses are combined via full-body
LERP blending, partial-skeleton LERP blending and/or additive blend-
ing. The output of this stage is a single local pose for all joints in the
skeleton. This stage is of course only executed when blending more than
one animation clip together—otherwise the output pose from stage 1 can
be used directly.

3.
Global pose generation. In this stage, the skeletal hierarchy is walked, and
local joint poses are concatenated in order to generate a global pose for
the skeleton.

4.
Post-processing. In this optional stage, the local and/or global poses of
the skeleton can be modified prior to finalization of the pose.
Post-
processing is used for inverse kinematics, rag doll physics and other
forms of procedural animation adjustment.

5.
Recalculation of global poses. Many types of post-processing require global
pose information as input but generate local poses as output. After such
a post-processing step has run, we must recalculate the global pose from
the modified local pose. Obviously, a post-processing operation that
does not require global pose information can be done between stages
2 and 3, thus avoiding the need for global pose recalculation.

6.
Matrix palette generation. Once the final global pose has been generated,
each joint’s global pose matrix is multiplied by the corresponding in-
verse bind pose matrix. The output of this stage is a palette of skinning
matrices suitable for input to the rendering engine.

A typical animation pipeline is depicted in Figure 12.47.


<!-- source-pdf-page: 805 -->
> Visual fallback for diagrams/images: [PDF page 805](../../../visual_pages/page_0805.jpg)

Inputs

Skeleton

Local
Clock(s)

Outputs

Decompression
and
Pose Extraction

Pose
Blending

Clip(s)

Local
Pose

Post-
Processing

Global
Pose Calc.

Blend
Specification

Global
Pose

Game Play
Systems

Skinning
Matrix
Calc.

Rendering
Engine
Matrix
Palette

Figure 12.47. A typical animation pipeline.

## 12.10 Action State Machines

The actions of a game character (standing, walking, running, jumping, etc.) are
usually best modeled via a finite state machine, commonly known as the action
state machine (ASM). The ASM subsystem sits atop the animation pipeline and
provides a state-driven animation interface for use by virtually all higher-level
game code.
Each state in an ASM corresponds to an arbitrarily complex blend of simul-
taneous animation clips. Some states might be very simple—for example, the
“idle” state might be comprised of a single full-body animation. Other states
might be more complex. A “running” state might correspond to a semicircular
blend, with strafing left, running forward and strafing right at the −90 degree,
0 degree and +90 degree points, respectively. The “running while shooting”
state might include a semicircular directional blend, plus additive or partial-
skeleton blend nodes for aiming the character’s weapon up, down, left and
right, and additional blends to permit the character to look around with its
eyes, head and shoulders. More additive animations might be included to
control the character’s overall stance, gait and foot spacing while locomoting
and to provide a degree of “humanness” through random movement varia-
tions.


<!-- source-pdf-page: 806 -->
> Visual fallback for diagrams/images: [PDF page 806](../../../visual_pages/page_0806.jpg)

Gesture Layer (LERP)

J
K

Gesture Layer (Additive)

H
I

Variation Layer (Additive)

D
E
G

F

Base Layer

State A
State B
State C

Time (

)

Figure 12.48. A layered action state machine, showing how each layer’s state transitions are tem-
porally independent. In this example, the base layer describes the character’s full-body stance and
movement. A variation layer provides variety by applying additive clips to the character’s pose. Fi-
nally, two gesture layers, one additive and one partial, permit the character to aim or point at
objects in the world around it.

A character’s ASM also ensures that characters can transition smoothly
from state to state. During a transition from state A to state B, the final output
poses of both states are usually blended together to provide a smooth cross-fade
between them.
Most high-quality animation engines also permit different parts of a char-
acter’s body to be doing different, independent or semi-independent actions
simultaneously. For instance, a character might be running, aiming and fir-
ing a weapon with its arms, and speaking a line of dialog with its facial joints.
The movements of different parts of the body aren’t generally in perfect sync
either—certain parts of the body tend to “lead” the movements of other parts
(e.g., the head leads a turn, followed by the shoulders, the hips and finally the
legs). In traditional animation, this well-known technique is known as antici-
pation [51]. This kind of complex movement can be realized by allowing mul-
tiple independent state machines to control a single character. Usually each
state machine exists in a separate state layer, as shown in Figure 12.48. The
output poses from each layer’s ASM are blended together into a final compos-
ite pose.
All of this means that at any given moment in time, multiple animation


<!-- source-pdf-page: 807 -->

clips are contributing to the final pose of a character’s skeleton. For each char-
acter, then, we need a way to track all of the currently-playing clips, and to
describe how exactly they should be blended together in order to produce the
character’s final pose. Generally speaking, there are two ways to do this:

1.
Flat weighted average. In this approach, the engine maintains a flat list
of all animation clips that are currently contributing to a character’s fi-
nal pose, with one blend weight per clip. The animations are blended
together as one big weighted average to produce the final pose.

2.
Blend trees. In this approach, each contributing clip is represented by
the leaf nodes of a tree. The interior nodes of this tree represent vari-
ous blending operations that are being performed on the clips. Multiple
blend operations are composed to form action states. Additional blend
nodes are introduced to represent transient cross-fades. And in a layered
ASM, the output poses obtained from the action states in each layer are
blended together. The final pose of the character is thus produced at the
root of this potentially complex blend tree.

### 12.10.1 The Flat Weighted Average Approach

In the flat weighted average approach, every animation clip that is currently
playing on a given character is associated with a blend weight indicating how
much it should contribute to its final pose. A flat list of all active animation
clips (i.e., clips whose blend weights are nonzero) is maintained. To calculate
the final pose of the skeleton, we extract a pose at the appropriate time index
for each of the N active clips. Then, for each joint of the skeleton, we calculate
a simple N-point weighted average of the translation vectors, rotation quater-
nions and scale factors extracted from the N active animations. This yields the
final pose of the skeleton.
The equation for the weighted average of a set of N vectors {vi} is as fol-
lows:

N−1
∑
i=0
wivi

vavg =

.

N−1
∑
i=0
wi

If the weights are normalized, meaning they sum to one, then this equation can
be simplified to the following:

vavg =
N−1
∑
i=0
wivi,
when
N−1
∑
i=0
wi = 1.


<!-- source-pdf-page: 808 -->

In the case of N = 2, if we let w0 = (1 −β) and w1 = β, the weighted average
reduces to the familiar equation for the linear interpolation (LERP) between
two vectors:

vavg = w0vA + w1vB
= (1 −β) vA + βvB
= LERP [vA, vB, β] .

We can apply this same weighted average formulation equally well to quater-
nions by simply treating them as four-element vectors.

12.10.1.1
Example: OGRE

The OGRE animation system works in exactly this way. An Ogre::Entity
represents an instance of a 3D mesh (e.g., one particular character walking
around in the game world).
The Entity aggregates an object called an
Ogre::AnimationStateSet,
which
in
turn
maintains
a
list
of
Ogre::AnimationState objects, one for each active animation.
The
Ogre::AnimationState class is shown in the code snippet below. (A few
irrelevant details have been omitted for clarity.)

/** Represents the state of an animation clip and the
weight of its influence on the overall pose of the
character.
*/
class AnimationState
{
protected:
String
mAnimationName; // reference to
// clip
Real
mTimePos;
// local clock
Real
mWeight;
// blend weight
bool
mEnabled;
// is this anim
// running?
bool
mLoop;
// should the
// anim loop?

public:
/// API functions...
};

Each AnimationState keeps track of one animation clip’s local clock and
its blend weight. When calculating the final pose of the skeleton for a partic-
ular Ogre::Entity, OGRE’s animation system simply loops through each


<!-- source-pdf-page: 809 -->
> Visual fallback for diagrams/images: [PDF page 809](../../../visual_pages/page_0809.jpg)

active AnimationState in its AnimationStateSet. A skeletal pose is ex-
tracted from the animation clip corresponding to each state at the time index
specified by that state’s local clock. For each joint in the skeleton, an N-point
weighted average is then calculated for the translation vectors, rotation quater-
nions and scales, yielding the final skeletal pose.
It is interesting to note that OGRE has no concept of a playback rate (R). If
it did, we would have expected to see a data member like this in the
Ogre::AnimationState class:

Real
mPlaybackRate;

Of course, we can still make animations play more slowly or more quickly
in OGRE by simply scaling the amount of time we pass to the addTime()
function, but unfortunately, OGRE does not support animation time scaling
out of the box.

12.10.1.2
Example: Granny

The Granny animation system, by Rad Game Tools (http://www.radgame
tool.com/granny.html), provides a flat, weighted average animation blending
system similar to OGRE’s. Granny permits any number of animations to be
played on a single character simultaneously. The state of each active anima-
tion is maintained in a data structure known as a granny_control. Granny
calculates a weighted average to determine the final pose, automatically nor-
malizing the weights of all active clips. In this sense, its architecture is virtually
identical to that of OGRE’s animation system.
Where Granny really shines is in its handling of time. Granny uses the
global clock approach discussed in Section 12.4.3. It allows each clip to be
looped an arbitrary number of times or infinitely. Clips can also be time-scaled;
a negative time scale allows an animation to be played in reverse.

12.10.1.3
Cross-Fades with a Flat Weighted Average

In an animation engine that employs the flat weighted average architecture,
cross-fades are implemented by adjusting the weights of the clips themselves.
Recall that any clip whose weight wi = 0 will not contribute to the current
pose of the character, while those whose weights are nonzero are averaged
together to generate the final pose. If we wish to transition smoothly from
clip A to clip B, we simply ramp up clip B’s weight wB, while simultaneously
ramping down clip A’s weight wA. This is illustrated in Figure 12.49.
Cross-fading in a weighted average architecture becomes a bit trickier
when we wish to transition from one complex blend to another. As an ex-
ample, let’s say we wish to transition the character from walking to jumping.


<!-- source-pdf-page: 810 -->
> Visual fallback for diagrams/images: [PDF page 810](../../../visual_pages/page_0810.jpg)

w

w

w

tstart
tend

t

Figure 12.49. A simple cross-fade from clip A to clip B, as implemented in a weighted average ani-
mation architecture.

Let’s assume that the walk movement is produced by a three-way average be-
tween clips A, B and C, and that the jump movement is produced by a two-way
average between clips D and E.
We want the character to look like he’s smoothly transitioning from walk-
ing to jumping, without affecting how the walk or jump animations look in-
dividually. So during the transition, we want to ramp down the ABC clips
and ramp up the DE clips while keeping the relative weights of the ABC and
DE clip groups constant. If the cross-fade’s blend factor is denoted by λ, we
can meet this requirement by simply setting the weights of both clip groups to
their desired values and then multiplying the weights of the source group by
(1 −λ) and the weights of the destination group by λ.
Let’s look at a concrete example to convince ourselves that this will work
properly. Imagine that before the transition from ABC to DE, the nonzero
weights are as follows: wA = 0.2, wB = 0.3 and wC = 0.5. After the tran-
sition, we want the nonzero weights to be wD = 0.33 and wE = 0.66. So, we
set the weights as follows:

wA = (1 −λ)(0.2),
wD = λ(0.33),
wB = (1 −λ)(0.3),
wE = λ(0.66).
(12.20)

wC = (1 −λ)(0.5),

From Equations (12.20), you should be able to convince yourself of the follow-
ing:

1.
When λ = 0, the output pose is the correct blend of clips A, B and C,
with zero contribution from clips D and E.

2.
When λ = 1, the output pose is the correct blend of clips D and E, with
no contribution from A, B or C.

3.
When 0 < λ < 1, the relative weights of both the ABC group and the
DE group remain correct, although they no longer add to one. (In fact,
group ABC’s weights add to (1 −λ), and group DE’s weights add to λ.)


<!-- source-pdf-page: 811 -->
> Visual fallback for diagrams/images: [PDF page 811](../../../visual_pages/page_0811.jpg)

Figure 12.50. A binary LERP blend, represented by a binary expression tree.

For this approach to work, the implementation must keep track of
the logical groupings between clips (even though, at the lowest level, all
of the clips’ states are maintained in one big, flat array—for example, the
Ogre::AnimationStateSet in OGRE). In our example above, the system
must “know” that A, B and C form a group, that D and E form another group,
and that we wish to transition from group ABC to group DE. This requires
additional metadata to be maintained, on top of the flat array of clip states.

### 12.10.2 Blend Trees

Some animation engines represent a character’s clip state not as a flat weighted
average but rather as a tree of blend operations. An animation blend tree is an
example of what is known in compiler theory as an expression tree or a syntax
tree. The interior nodes of such a tree are operators, and the leaf nodes serve
as the inputs to those operators. (More correctly, the interior nodes represent
the nonterminals of the grammar, while the leaf nodes represent the terminals.)
In the following sections, we’ll briefly revisit the various kinds of animation
blends we learned about in Sections 12.6.3 and 12.6.5 and see how each can be
represented by an expression tree.

12.10.2.1
Binary LERP Blend Trees

As we saw in Section 12.6.1, a binary linear interpolation (LERP) blend takes
two input poses and blends them together into a single output pose. A blend
weight β controls the percentage of the second input pose that should appear
at the output, while (1 −β) specifies the percentage of the first input pose.
This can be represented by the binary expression tree shown in Figure 12.50.

12.10.2.2
Generalized One-Dimensional Blend Trees

In Section 12.6.3.1, we learned that it can be convenient to define a generalized
one-dimensional LERP blend by placing an arbitrary number of clips along a
linear scale. A blend factor b specifies the desired blend along this scale. Such
a blend can be pictured as an n-input operator, as shown in Figure 12.51.
Given a specific value for b, such a linear blend can always be transformed
into a binary LERP blend. We simply use the two clips immediately adjacent to


<!-- source-pdf-page: 812 -->
> Visual fallback for diagrams/images: [PDF page 812](../../../visual_pages/page_0812.jpg)

b

b

Clip A

b

Clip B

LERP
Output Pose

Clip C

b

Clip D

b

b

For this specific value of
b, this tree converts to...

 = 0

LERP
Clip B

Output Pose

Clip C

 = 1

Figure 12.51. A multi-input expression tree can be used to represent a generalized 1D blend. Such a
tree can always be transformed into a binary expression tree for any speciﬁc value of the blend
factor b.

LERP
Top Left

b

Top Right

Output Pose
LERP

b

LERP
Bottom Left

Bottom Right

Figure 12.52. A simple 2D LERP blend, implemented as cascaded binary blends.

b as the inputs to the binary blend and calculate the blend weight β as specified
in Equation (12.15)

12.10.2.3
Two-Dimensional LERP Blend Trees

In Section 12.6.3.2, we saw how a two-dimensional LERP blend can be realized
by simply cascading the results of two binary LERP blends. Given a desired
two-dimensional blend point b =
[
bx
by
]
, Figure 12.52 shows how this kind
of blend can be represented in tree form.

12.10.2.4
Additive Blend Trees

Section 12.6.5 described additive blending. This is a binary operation, so it can
be represented by a binary tree node, as shown in Figure 12.53. A single blend
weight β controls the amount of the additive animation that should appear at


<!-- source-pdf-page: 813 -->
> Visual fallback for diagrams/images: [PDF page 813](../../../visual_pages/page_0813.jpg)

Figure 12.53. An additive blend represented as a binary tree.

Clip A

+

Diff Clip B

+

Diff Clip C

+

Output Pose

Diff Clip D

Figure 12.54. In order to additively blend more than one difference pose onto a regular “base” pose,
a cascaded binary expression tree must be used.

the output—when β = 0, the additive clip does not affect the output at all,
while when β = 1, the additive clip has its maximum effect on the output.

Additive blend nodes must be handled carefully, because the inputs are
not interchangeable (as they are with most types of blend operators). One of
the two inputs is a regular skeletal pose, while the other is a special kind of
pose known as a difference pose (also known as an additive pose). A difference
pose may only be applied to a regular pose, and the result of an additive blend
is another regular pose. This implies that the additive input of a blend node
must always be a leaf node, while the regular input may be a leaf or an interior
node. If we want to apply more than one additive animation to our character,
we must use a cascaded binary tree with the additive clips always applied to
the additive inputs, as shown in Figure 12.54.

12.10.2.5
Layered Blend Trees

We said at the beginning of Section 12.10 that complex character movement
can be produced by arranging multiple independent state machines into state
layers. The output poses from each layer’s ASM are blended together into a
final composite pose. When this is implemented using blend trees, the net
effect is to combine the blend trees of each active state together into one über
tree, as illustrated in Figure 12.55.


<!-- source-pdf-page: 814 -->
> Visual fallback for diagrams/images: [PDF page 814](../../../visual_pages/page_0814.jpg)

K

H

F

B

Time

Net blend tree

at time

Tree

B

+

Tree

+

F

Tree

LERP

H

Tree

K

Figure 12.55. A layered state machine converts the blend trees from multiple states into a single,
uniﬁed tree.

12.10.2.6
Cross-Fades with Blend Trees

As a character transitions from state to state within each layer of a layered
ASM, we often wish to provide a smooth cross-fade between states. Imple-
menting a cross-fade in an expression tree based ASM is a bit more intuitive
than it is in a weighted average architecture. Whether we’re transitioning from
one clip to another or from one complex blend to another, the approach is al-
ways the same: We simply introduce a transient binary LERP node between
the roots of the blend trees of each state to handle the cross-fade.
We’ll denote the blend factor of the cross-fade node with the symbol λ as
before. Its top input is the source state’s blend tree (which can be a single clip
or a complex blend), and its bottom input is the destination state’s tree (again a
clip or a complex blend). During the transition, λ is ramped from zero to one.
Once λ = 1, the transition is complete, and the cross-fade LERP node and its
top input tree can be retired. This leaves its bottom input tree as the root of


<!-- source-pdf-page: 815 -->
> Visual fallback for diagrams/images: [PDF page 815](../../../visual_pages/page_0815.jpg)

Before
Cross-Fade

Tree

A

During
Cross-Fade

Tree

A

Tree

B

After
Cross-Fade

Tree

B

Figure 12.56. A cross-fade between two arbitrary blend trees A and B.

the overall blend tree for the given state layer, thus completing the transition.
This process is illustrated in Figure 12.56.

### 12.10.3 State and Blend Tree Speciﬁcations

Animators, game designers and programmers usually cooperate to create the
animation and control systems for the central characters in a game. These de-
velopers need a way to specify the states that make up a character’s ASM,
to lay out the tree structure of each blend tree, and to select the clips that
will serve as their inputs. Although the states and blend trees could be hard-
coded, most modern game engines provide a data-driven means of defining
animation states. The goal of a data-driven approach is to permit a user to cre-
ate new animation states, remove unwanted states, fine-tune existing states
and then see the effects of his or her changes reasonably quickly. In other
words, the central goal of a data-driven animation engine is to enable rapid
iteration.
To build an arbitrarily complex blend tree, we really only require four
atomic types of blend nodes: clips, binary LERP blends, binary additive blends
and possibly ternary (triangular) LERP blends. Virtually any blend tree imag-
inable can be created as compositions of these atomic nodes.


<!-- source-pdf-page: 816 -->

A blend tree built exclusively from atomic nodes can quickly become large
and unwieldy. As a result, many game engines permit custom compound
node types to be predefined for convenience. The N-dimensional linear blend
node discussed in Sections 12.6.3.4 and 12.10.2.2 is an example of a compound
node. One can imagine myriad complex blend node types, each one address-
ing a particular problem specific to the particular game being made. A soccer
game might define a node that allows the character to dribble the ball. A war
game could define a special node that handles aiming and firing a weapon.
A brawler could define custom nodes for each fight move the characters can
perform. Once we have the ability to define custom node types, the sky’s the
limit.
The means by which the users enter animation state data varies widely.
Some game engines employ a simple, bare-bones approach, allowing anima-
tion states to be specified in a text file with a simple syntax. Other engines pro-
vide a slick, graphical editor that permits animation states to be constructed by
dragging atomic components such as clips and blend nodes onto a canvas and
linking them together in arbitrary ways. Such editors usually provide a live
preview of the character so that the user can see immediately how the charac-
ter will look in the final game. In my opinion, the specific method chosen has
little bearing on the quality of the final game—what matters most is that the
user can make changes and see the results of those changes reasonably quickly
and easily.

12.10.3.1
Example: The Naughty Dog Engine

The animation engine used in Naughty Dog’s Uncharted and The Last of Us
franchises employs a simple, text-based approach to specifying animation
states. For reasons related to Naughty Dog’s rich history with the Lisp lan-
guage (see Section 16.9.5.1), state specifications in the Naughty Dog engine
are written in a customized version of the Scheme programming language
(which itself is a Lisp variant). Two basic state types can be used: simple and
complex.

Simple States

A simple state contains a single animation clip. For example:

(define-state simple
:name "pirate-b-bump-back"
:clip "pirate-b-bump-back"
:flags (anim-state-flag no-adjust-to-ground)
)


<!-- source-pdf-page: 817 -->

Don’t let the Lisp-style syntax throw you. All this block of code does is to de-
fine a state named “pirate-b-bump-back” whose animation clip also happens
to be named “pirate-b-bump-back.” The :flags parameter allows users to
specify various Boolean options on the state.

Complex States

A complex state contains an arbitrary tree of LERP or additive blends. For ex-
ample, the following state defines a tree that contains a single binary LERP
blend node, with two clips (“walk-l-to-r” and “run-l-to-r”) as its inputs:

(define-state complex
:name "move-l-to-r"
:tree
(anim-node-lerp
(anim-node-clip "walk-l-to-r")
(anim-node-clip "run-l-to-r")
)
)

The :tree argument allows the user to specify an arbitrary blend tree, com-
posed of LERP or additive blend nodes and nodes that play individual anima-
tion clips.
From this, we can see how the (define-state simple ...) example
shown above might really work under the hood—it probably defines a com-
plex blend tree containing a single “clip” node, like this:

(define-state complex
:name "pirate-b-unimog-bump-back"
:tree (anim-node-clip "pirate-b-unimog-bump-back")
:flags (anim-state-flag no-adjust-to-ground)
)

The following complex state shows how blend nodes can be cascaded into
arbitrarily deep blend trees:

(define-state complex
:name "move-b-to-f"
:tree
(anim-node-lerp
(anim-node-additive
(anim-node-additive
(anim-node-clip "move-f")
(anim-node-clip "move-f-look-lr")
)


<!-- source-pdf-page: 818 -->
> Visual fallback for diagrams/images: [PDF page 818](../../../visual_pages/page_0818.jpg)

move-f

+

move-f-look-lr

+

move-f-look-ud

LERP

move-b

+

move-b-look-lr

+

move-b-look-ud

Figure 12.57. Blend tree corresponding to the example state “move-b-to-f.”

(anim-node-clip "move-f-look-ud")
)
(anim-node-additive
(anim-node-additive
(anim-node-clip "move-b")
(anim-node-clip "move-b-look-lr")
)
(anim-node-clip "move-b-look-ud")
)
)
)

This corresponds to the tree shown in Figure 12.57.

Rapid Iteration

Naughty Dog’s animation team achieves rapid iteration with the help of four
important tools:

1.
An in-game animation viewer allows a character to be spawned into the
game and its animations controlled via an in-game menu.
2.
A simple command-line tool allows animation scripts to be recompiled
and reloaded into the running game on the fly. To tweak a character’s
animations, the user can make changes to the text file containing the an-
imation state specifications, quickly reload the animation states and im-
mediately see the effects of his or her changes on an animating character
in the game.
3.
The engine continually keeps track of all state transitions performed by
each character during the last few seconds of gameplay. This allows us
to pause the game and then literally rewind the animations to scrutinize
them and debug problems that are noticed while playing.


<!-- source-pdf-page: 819 -->
> Visual fallback for diagrams/images: [PDF page 819](../../../visual_pages/page_0819.jpg)

4.
The Naughty Dog engine also offers a host of “live update” tools. For
example, animators can tweak their animations in Maya and see them
update virtually instantaneously in the game.

12.10.3.2
Example: Unreal Engine 4

Unreal Engine 4 (UE4) provides its users with five tools for working with skele-
tal animations and skeletal meshes: The Skeleton Editor, the Skeletal Mesh
Editor, the Animation Editor, the Animation Blueprint Editor, and the Physics
Editor.

•
The Skeleton Editor is essentially a rigging tool. It allows users to view
and modify skeletons, add sockets to joints, and test out the movement of
the skeleton. A socket is sometimes called an attach point in other engines
(see Section 12.11.1).

•
The Skeletal Mesh Editor allows users to edit properties of the meshes
that are skinned to animating skeletons.

•
The Animation Editor allows users to import, create and manage anima-
tion assets. In this editor, the compression and timing of animation clips
(which UE4 calls Sequences) can be adjusted. Clips can be combined
into predefined Blend Spaces, and in-game cinematics can be defined by
creating Animation Montages.

•
The Animation Blueprint Editor allows users to apply the power of Un-
real Engine’s Blueprints visual scripting system to controlling characters’
animation state machines. This editor is depicted in Figure 12.58.

•
The Physics Editor allows users to model a hierarchy of rigid bodies that
drive the skeleton’s motion when ragdoll physics is active.

A complete discussion of Unreal Engine’s animation tools is beyond our scope
here, but you can read more about it by searching for “Unreal Skeletal Mesh
Animation System” online.

### 12.10.4 Transitions

To create a high-quality animating character, we must carefully manage the
transitions between states in the action state machine to ensure that the splices
between animations do not have a jarring and unpolished appearance. Most
modern animation engines provide a data-driven mechanism for specifying
exactly how transitions should be handled. In this section, we’ll explore how
this mechanism works.


<!-- source-pdf-page: 820 -->
> Visual fallback for diagrams/images: [PDF page 820](../../../visual_pages/page_0820.jpg)

Figure 12.58. The Unreal Engine 4 animation Blueprints editor. (See Color Plate XXVI.)

12.10.4.1
Kinds of Transitions

There are many different ways to manage the transition between states. If we
know that the final pose of the source state exactly matches the first pose of
the destination state, we can simply “pop” from one state to another. Other-
wise, we can cross-fade from one state to the next. Cross-fading is not always
a suitable choice when transitioning from state to state. For example, there
is no way that a cross-fade can produce a realistic transition from lying on
the ground to standing upright. For this kind of state transition, we need one
or more custom animations. This kind of transition is often implemented by
introducing special transitional states into the state machine. These states are
intended for use only when going from one state to another—they are never
used as a steady-state node. But because they are full-fledged states, they can
be comprised of arbitrarily complex blend trees. This provides maximum flex-
ibility when authoring custom-animated transitions.

12.10.4.2
Transition Parameters

When describing a particular transition between two states, we generally need
to specify various parameters, controlling exactly how the transition will oc-
cur. These include but are not limited to the following.

•
Source and destination states. To which state(s) does this transition apply?


<!-- source-pdf-page: 821 -->

•
Transition type. Is the transition immediate, cross-faded or performed via
a transitional state?

•
Duration. For cross-faded transitions, we need to specify how long the
cross-fade should take.

•
Ease-in/ease-out curve type. In a cross-faded transition, we may wish to
specify the type of ease-in/ease-out curve to use to vary the blend factor
during the fade.

•
Transition window. Certain transitions can only be taken when the source
animation is within a specified window of its local timeline. For exam-
ple, a transition from a punch animation to an impact reaction might
only make sense when the arm is in the second half of its swing. If an at-
tempt to perform the transition is made during the first half of the swing,
the transition would be disallowed (or a different transition might be se-
lected instead).

12.10.4.3
The Transition Matrix

Specifying transitions between states can be challenging, because the number
of possible transitions is usually very large. In a state machine with n states,
the worst-case number of possible transitions is n2. We can imagine a two-
dimensional square matrix with every possible state listed along both the ver-
tical and horizontal axes. Such a table can be used to specify all of the possible
transitions from any state along the vertical axis to any other state along the
horizontal axis.
In a real game, this transition matrix is usually quite sparse, because not all
state-to-state transitions are possible. For example, transitions are usually dis-
allowed from a death state to any other state. Likewise, there is probably no
way to go from a driving state to a swimming state (without going through at
least one intermediate state that causes the character to jump out of his vehi-
cle). The number of unique transitions in the table may be significantly less
even than the number of valid transitions between states. This is because we
can often reuse a single transition specification between many different pairs
of states.

12.10.4.4
Implementing a Transition Matrix

There are all sorts of ways to implement a transition matrix. We could use a
spreadsheet application to tabulate all the transitions in matrix form, or we
might permit transitions to be authored in the same text file used to author
our action states. If a graphical user interface is provided for state editing,
transitions could be added to this GUI as well. In the following sections, we’ll


<!-- source-pdf-page: 822 -->

take a brief look at a few transition matrix implementations from real game
engines.

Example: Wildcarded Transitions in Medal of Honor: Paciﬁc Assault

On Medal of Honor: Pacific Assault (MOHPA), we used the sparseness of the
transition matrix to our advantage by supporting wildcarded transition spec-
ifications. For each transition specification, the names of both the source and
destination states could contain asterisks (*) as a wildcard character. This al-
lowed us to specify a single default transition from any state to any other state
(via the syntax from="*" to="*") and then refine this global default eas-
ily for entire categories of states. The refinement could be taken all the way
down to custom transitions between specific state pairs when necessary. The
MOHPA transition matrix looked something like this:

<transitions>
<!-- global default -->
<trans from="*" to="*"
type=frozen duration=0.2>

<!-- default for any walk to any run -->
<trans from="walk*" to="run*"
type=smooth
duration=0.15>

<!-- special handling from any prone to any getting-up
-- action (only valid from 2 sec to 7.5 sec on the
-- local timeline) -->
<trans from="*prone" to="*get-up"
type=smooth
duration=0.1
window-start=2.0
window-end=7.5>
...
</transitions>

Example: First-Class Transitions in Uncharted

In some animation engines, high-level game code requests transitions from
the current state to a new state by naming the destination state explicitly. The
problem with this approach is that the calling code must have intimate knowl-
edge of the names of the states and of which transitions are valid when in a
particular state.


<!-- source-pdf-page: 823 -->

In Naughty Dog’s engine, this problem is overcome by turning state tran-
sitions from secondary implementation details into first-class entities. Each
state provides a list of valid transitions to other states, and each transition is
given a unique name. The names of the transitions are standardized in order
to make the effect of each transition predictable. For example, if a transition
is called “walk,” then it always goes from the current state to a walking state
of some kind, no matter what the current state is. Whenever the high-level
animation control code wants to transition from state A to state B, it asks for a
transition by name (rather than requesting the destination state explicitly). If
such a transition can be found and is valid, it is taken; otherwise, the request
fails.
The following example state defines four transitions named “reload,”
“step-left,” “step-right” and “fire.” The (transition-group ...) line in-
vokes a previously defined group of transitions; it is useful when the same set
of transitions is to be used in multiple states. The (transition-end ...)
command specifies a transition that is taken upon reaching the end of the
state’s local timeline if no other transition has been taken before then.

(define-state complex
:name "s_turret-idle"
:tree (aim-tree
(anim-node-clip "turret-aim-all--base")
"turret-aim-all--left-right"
"turret-aim-all--up-down"
)
:transitions (
(transition "reload" "s_turret-reload"
(range - -) :fade-time 0.2)

(transition "step-left" "s_turret-step-left"
(range - -) :fade-time 0.2)

(transition "step-right" "s_turret-step-right"
(range - -) :fade-time 0.2)

(transition "fire" "s_turret-fire"
(range - -) :fade-time 0.1)

(transition-group "combat-gunout-idle^move")

(transition-end "s_turret-idle")
)
)

The beauty of this approach may be difficult to see at first. Its primary


<!-- source-pdf-page: 824 -->

purpose is to allow transitions and states to be modified in a data-driven man-
ner, without requiring changes to the C++ source code in many cases. This
degree of flexibility is accomplished by shielding the animation control code
from knowledge of the structure of the state graph. For example, let’s say that
we have ten different walking states (normal, scared, crouched, injured and
so on). All of them can transition into a jumping state, but different kinds
of walks might require different jump animations (e.g., normal jump, scared
jump, jump from crouch, injured jump, etc.). For each of the ten walking states,
we define a transition simply called “jump.” At first, we can point all of these
transitions to a single generic “jump” state, just to get things up and running.
Later, we can fine-tune some of these transitions so that they point to custom
jump states. We can even introduce transitional states between some of the
“walk” states and their corresponding “jump” states. All sorts of changes can
be made to the structure of the state graph and the parameters of the transitions
without affecting the C++ source code—as long as the names of the transitions
don’t change.

### 12.10.5 Control Parameters

From a software engineering perspective, it can be challenging to orchestrate
all of the blend weights, playback rates and other control parameters of a com-
plex animating character. Different blend weights have different effects on the
way the character animates. For example, one weight might control the charac-
ter’s movement direction, while others control its movement speed, horizontal
and vertical weapon aim, head/eye look direction and so on. We need some
way of exposing all of these blend weights to the code that is responsible for
controlling them.
In a flat weighted average architecture, we have a flat list of all the anima-
tion clips that could possibly be played on the character. Each clip state has a
blend weight, a playback rate and possibly other control parameters. The code
that controls the character must look up individual clip states by name and ad-
just each one’s blend weight appropriately. This makes for a simple interface,
but it shifts most of the responsibility for controlling the blend weights to the
character control system. For example, to adjust the direction in which a char-
acter is running, the character control code must know that the “run” action is
comprised of a group of animation clips, named something like “StrafeLeft,”
“RunForward,” “StrafeRight” and “RunBackward.” It must look up these clip
states by name and manually control all four blend weights in order to achieve
a particular angled run animation. Needless to say, controlling animation pa-
rameters in such a fine-grained way can be tedious and can lead to difficult-


<!-- source-pdf-page: 825 -->

to-understand source code.
In a blend tree, a different set of problems arise. Thanks to the tree struc-
ture, the clips are grouped naturally into functional units. Custom tree nodes
can encapsulate complex character motions. These are both helpful advan-
tages over the flat weighted average approach. However, the control param-
eters are buried within the tree. Code that wishes to control the horizontal
look-at direction of the head and eyes needs a priori knowledge of the struc-
ture of the blend tree so that it can find the appropriate nodes in the tree in
order to control their parameters.
Different animation engines solve these problems in different ways. Here
are some examples:

•
Node search. Some engines provide a way for higher-level code to find
blend nodes in the tree. For example, relevant nodes in the tree can be
given special names, such as “HorizAim” for the node that controls hor-
izontal weapon aiming. The control code can simply search the tree for
a node of a particular name; if one is found, then we know what effect
adjusting its blend weight will have.

•
Named variables. Some engines allow names to be assigned to the indi-
vidual control parameters. The controlling code can look up a control
parameter by name in order to adjust its value.

•
Control structure. In other engines, a simple data structure, such as an
array of floating-point values or a C struct, contains all of the control
parameters for the entire character. The nodes in the blend tree(s) are
connected to particular control parameters, either by being hard-coded
to use certain struct members or by looking up the parameters by name
or index.

Of course, there are many other alternatives as well. Every animation en-
gine tackles this problem in a slightly different way, but the net effect is always
roughly the same.

## 12.11 Constraints

We’ve seen how action state machines can be used to specify complex blend
trees and how a transition matrix can be used to control how transitions be-
tween states should work. Another important aspect of character animation
control is to constrain the movement of the characters and/or objects in the
scene in various ways. For example, we might want to constrain a weapon
so that it always appears to be in the hand of the character who is carrying it.


<!-- source-pdf-page: 826 -->
> Visual fallback for diagrams/images: [PDF page 826](../../../visual_pages/page_0826.jpg)

… child
skeleton
follows

child
skeleton
moves…

… parent
skeleton
unaffected

parent
skeleton
moves…

Figure 12.59. An attachment, showing how movement of the parent automatically produces move-
ment of the child but not vice versa.

We might wish to constrain two characters so that they line up properly when
shaking hands. A character’s feet are often constrained so that they line up
with the floor, and its hands might be constrained to line up with the rungs
on a ladder or the steering wheel of a vehicle. In this section, we’ll take a brief
look at how these constraints are handled in a typical animation system.

### 12.11.1 Attachments

Virtually all modern game engines permit objects to be attached to one an-
other. At its simplest, object-to-object attachment involves constraining the
position and/or orientation of a particular joint JA within the skeleton of ob-
ject A so that it coincides with a joint JB in the skeleton of object B. An at-
tachment is usually a parent-child relationship. When the parent’s skeleton
moves, the child object is adjusted to satisfy the constraint. However, when
the child moves, the parent’s skeleton is usually not affected. This is illustrated
in Figure 12.59.
Sometimes it can be convenient to introduce an offset between the parent
joint and the child joint. For example, when placing a gun into a character’s
hand, we could constrain the “Grip” joint of the gun so that it coincides with
the “RightWrist” joint of the character. However, this might not produce the
correct alignment of the gun with the hand. One solution to this problem is
to introduce a special joint into one of the two skeletons. For example, we
could add a “RightGun” joint to the character’s skeleton, make it a child of
the “RightWrist” joint, and position it so that when the “Grip” joint of the
gun is constrained to it, the gun looks like it is being held naturally by the
character. The problem with this approach, however, is that it increases the


<!-- source-pdf-page: 827 -->
> Visual fallback for diagrams/images: [PDF page 827](../../../visual_pages/page_0827.jpg)

Figure 12.60. An attach point acts like an extra joint between the parent and the child.

number of joints in the skeleton. Each joint has a processing cost associated
with animation blending and matrix palette calculation and a memory cost for
storing its animation keys. So adding new joints is often not a viable option.
We know that an additional joint added for attachment purposes will not
contribute to the pose of the character—it merely introduces an additional
transform between the parent and child joint in an attachment. What we re-
ally want, then, is a way to mark certain joints so that they can be ignored by
the animation blending pipeline but can still be used for attachment purposes.
Such special joints are sometimes called attach points. They are illustrated in
Figure 12.60.
Attach points might be modeled in Maya just like regular joints or locators,
although many game engines define attach points in a more convenient man-
ner. For example, they might be specified as part of the action state machine
text file or via a custom GUI within the animation authoring tool. This allows
the animators to focus only on the joints that affect the look of the character,
while the power to control attachments is put conveniently into the hands of
the people who need it—the game designers and the engineers.

### 12.11.2 Interobject Registration

The interactions between game characters and their environments is growing
ever more complex and nuanced with each new title. Hence, it is important
to have a system that allows characters and objects to be aligned with one
another when animating. Such a system can be used for in-game cinematics
and interactive gameplay elements alike.
Imagine that an animator, working in Maya or some other animation tool,
sets up a scene involving two characters and a door object. The two characters
shake hands, and then one of them opens the door and they both walk through
it. The animator can ensure that all three actors in the scene line up perfectly.


<!-- source-pdf-page: 828 -->
> Visual fallback for diagrams/images: [PDF page 828](../../../visual_pages/page_0828.jpg)

yA

yB

yC

ymaya

xA

xB
xC

xmaya

Figure 12.61. Original Maya scene containing three actors and
a reference locator.

Figure 12.62. The reference locator is encoded in each
actor’s animation ﬁle.

However, when the animations are exported, they become three separate clips,
to be played on three separate objects in the game world. The two characters
might have been under AI or player control prior to the start of this animated
sequence. How, then, can we ensure that the three objects line up correctly
with one another when the three clips are played back in-game?

12.11.2.1
Reference Locators

One good solution is to introduce a common reference point into all three an-
imation clips. In Maya, the animator can drop a locator (which is just a 3D
transform, much like a skeletal joint) into the scene, placing it anywhere that
seems convenient. Its location and orientation are actually irrelevant, as we’ll
see. The locator is tagged in some way to tell the animation export tools that
it is to be treated specially.

When the three animation clips are exported, the tools store the position
and orientation of the reference locator, expressed in coordinates that are rela-
tive to the local object space of each actor, into all three clip’s data files. Later,
when the three clips are played back in-game, the animation engine can look
up the relative position and orientation of the reference locator in all three
clips. It can then transform the origins of the three objects in such a way as
to make all three reference locators coincide in world space. The reference
locator acts much like an attach point (Section 12.11.1) and, in fact, could be
implemented as one. The net effect—all three actors now line up with one
another, exactly as they had been aligned in the original Maya scene.

Figure 12.61 illustrates how the door and the two characters from the above
example might be set up in a Maya scene. As shown in Figure 12.62, the refer-
ence locator appears in each exported animation clip (expressed in that actor’s
local space). In-game, these local-space reference locators are aligned to a fixed
world-space locator in order to realign the actors, as shown in Figure 12.63.


<!-- source-pdf-page: 829 -->
> Visual fallback for diagrams/images: [PDF page 829](../../../visual_pages/page_0829.jpg)

yworld

xworld

Figure 12.63. At runtime, the local-space reference transforms are aligned to a world-space refer-
ence locator, causing the actors to line up properly.

12.11.2.2
Finding the World-Space Reference Location

We’ve glossed over one important detail here—who decides what the world-
space position and orientation of the reference locator should be? Each anima-
tion clip provides the reference locator’s transform in the coordinate space of
its actor. But we need some way to define where that reference locator should
be in world space.
In our example with the door and the two characters shaking hands, one of
the actors is fixed in the world (the door). So one viable solution is to ask the
door for the location of the reference locator and then align the two characters
to it. The commands to accomplish this might look similar to the following
pseudocode.

void playShakingHandsDoorSequence(
Actor& door,
Actor& characterA,
Actor& characterB)
{
// Find the world-space transform of the reference
// locator as specified in the door's animation.
Transform refLoc = getReferenceLocatorWs(door,
"shake-hands-door");

// Play the door's animation in-place. (It's
// already in the correct place.)
playAnimation("shake-hands-door", door);

// Play the two characters' animations relative to
// the world-space reference locator obtained from
// the door.
playAnimationRelativeToReference(
"shake-hands-character-a", characterA, refLoc);
playAnimationRelativeToReference(
"shake-hands-character-b", characterB, refLoc);
}


<!-- source-pdf-page: 830 -->

Another option is to define the world-space transform of the reference loca-
tor independently of the three actors in the scene. We could place the reference
locator into the world using our world-building tool, for example (see Section
15.3). In this case, the pseudocode above should be changed to look something
like this:

void playShakingHandsDoorSequence(
Actor& door,
Actor& characterA,
Actor& characterB,
Actor& refLocatorActor)
{
// Find the world-space transform of the reference
// locator by simply querying the transform of an
// independent actor (presumably placed into the
// world manually).
Transform refLoc = getActorTransformWs(
refLocatorActor);

// Play all animations relative to the world-space
// reference locator obtained above.
playAnimationRelativeToReference("shake-hands-door",
door, refLoc);
playAnimationRelativeToReference(
"shake-hands-character-a", characterA, refLoc);
playAnimationRelativeToReference(
"shake-hands-character-b", characterB, refLoc);
}

### 12.11.3 Grabbing and Hand IK

Even after using an attachment to connect two objects, we sometimes find that
the alignment does not look exactly right in-game. For example, a character
might be holding a rifle in her right hand, with her left hand supporting the
stock. As the character aims the weapon in various directions, we may notice
that the left hand no longer aligns properly with the stock at certain aim angles.
This kind of joint misalignment is caused by LERP blending. Even if the joints
in question are aligned perfectly in clip A and in clip B, LERP blending does
not guarantee that those joints will be in alignment when A and B are blended
together.
One solution to this problem is to use inverse kinematics (IK) to correct the
position of the left hand. The basic approach is to determine the desired tar-
get position for the joint in question. IK is then applied to a short chain of
joints (usually two, three or four joints), starting with the joint in question and


<!-- source-pdf-page: 831 -->

progressing up the hierarchy to its parent, grandparent and so on. The joint
whose position we are trying to correct is known as the end effector. The IK
solver adjusts the orientations of the end effector’s parent joint(s) in order to
get the end effector as close as possible to the target.
The API for an IK system usually takes the form of a request to enable or
disable IK on a particular chain of joints, plus a specification of the desired tar-
get point. The actual IK calculation is usually done internally by the low-level
animation pipeline. This allows it to do the calculation at the proper time—
namely, after intermediate local and global skeletal poses have been calculated
but before the final matrix palette calculation.
Some animation engines allow IK chains to be defined a priori. For ex-
ample, we might define one IK chain for the left arm, one for the right arm
and two for the two legs. Let’s assume for the purposes of this example that
a particular IK chain is identified by the name of its end-effector joint. (Other
engines might use an index or handle or some other unique identifier, but the
concept remains the same.) The function to enable an IK calculation might
look something like this:

void enableIkChain(Actor& actor,
const char* endEffectorJointName,
const Vector3& targetLocationWs);

and the function to disable an IK chain might look like this:

void disableIkChain(Actor& actor,
const char* endEffectorJointName);

IK is usually enabled and disabled relatively infrequently, but the world-
space target location must be kept up-to-date every frame (if the target is mov-
ing). Therefore, the low-level animation pipeline always provides some mech-
anism for updating an active IK target point. For example, the pipeline might
allow us to call enableIkChain() multiple times. The first time it is called,
the IK chain is enabled, and its target point is set. All subsequent calls sim-
ply update the target point. Another way to keep IK targets up-to-date is to
link them to dynamic objects in the game. For example, an IK target might
be specified as a handle to a rigid game object, or a joint within an animated
object.
IK is well-suited to making minor corrections to joint alignment when the
joint is already reasonably close to its target. It does not work nearly as well
when the error between a joint’s desired location and its actual location is large.
Note also that most IK algorithms solve only for the position of a joint. You may


<!-- source-pdf-page: 832 -->
> Visual fallback for diagrams/images: [PDF page 832](../../../visual_pages/page_0832.jpg)

Figure 12.64. In the animation authoring package, the character moves forward in space, and
its feet appear grounded. Image courtesy of Naughty Dog, Inc. (UNCHARTED: Drake’s Fortune
© 2007/® SIE. Created and developed by Naughty Dog.)

need to write additional code to ensure that the orientation of the end effector
aligns properly with its target as well. IK is not a cure-all, and it may have
significant performance costs. So always use it judiciously.

### 12.11.4 Motion Extraction and Foot IK

In games, we usually want the locomotion animations of our characters to look
realistic and “grounded.” One of the biggest factors contributing to the real-
ism of a locomotion animation is whether or not the feet slide around on the
ground. Foot sliding can be overcome in a number of ways, the most common
of which are motion extraction and foot IK.

12.11.4.1
Motion Extraction

Let’s imagine how we’d animate a character walking forward in a straight
line. In Maya (or his or her animation package of choice), the animator makes
the character take one complete step forward, first with the left foot and then
with the right foot. The resulting animation clip is known as a locomotion cycle,
because it is intended to be looped indefinitely, for as long as the character
is walking forward in-game. The animator takes care to ensure that the feet
of the character appear grounded and don’t slide as it moves. The character
moves from its initial location on frame 0 to a new location at the end of the
cycle. This is shown in Figure 12.64.
Notice that the local-space origin of the character remains fixed during the
entire walk cycle. In effect, the character is “leaving his origin behind him” as
he takes his step forward. Now imagine playing this animation as a loop. We


<!-- source-pdf-page: 833 -->
> Visual fallback for diagrams/images: [PDF page 833](../../../visual_pages/page_0833.jpg)

Figure 12.65.
Walk cycle after zeroing out the root joint’s forward motion.
Image courtesy
of Naughty Dog, Inc. (UNCHARTED: Drake’s Fortune © 2007/® SIE. Created and developed by
Naughty Dog.)

would see the character take one complete step forward, and then pop back
to where he was on the first frame of the animation. Clearly this won’t work
in-game.
To make this work, we need to remove the forward motion of the character,
so that his local-space origin remains roughly under the center of mass of the
character at all times. We could do this by zeroing out the forward translation
of the root joint of the character’s skeleton. The resulting animation clip would
make the character look like he’s “moonwalking,” as shown in Figure 12.65.
In order to get the feet to appear to “stick” to the ground the way they did
in the original Maya scene, we need the character to move forward by just the
right amount each frame. We could look at the distance the character moved,
divide by the amount of time it took for him to get there, and hence find his
average movement speed. But a character’s forward speed is not constant
when walking. This is especially evident when a character is limping (quick
forward motion on the injured leg, followed by slower motion on the “good”
leg), but it is true for all natural-looking walk cycles.
Therefore, before we zero out the forward motion of the root joint, we first
save the animation data in a special “extracted motion” channel. This data
can be used in-game to move the local-space origin of the character forward
by the exact amount that the root joint had moved in Maya each frame. The net
result is that the character will walk forward exactly as he was authored, but
now his local-space origin comes along for the ride, allowing the animation to
loop properly. This is shown in Figure 12.66.
If the character moves forward by 4 feet in the animation and the animation


<!-- source-pdf-page: 834 -->
> Visual fallback for diagrams/images: [PDF page 834](../../../visual_pages/page_0834.jpg)

Figure 12.66. Walk cycle in-game, with extracted root motion data applied to the local-space origin
of the character. Image courtesy of Naughty Dog, Inc. (UNCHARTED: Drake’s Fortune © 2007/®
SIE. Created and developed by Naughty Dog.)

takes one second to complete, then we know that the character is moving at
an average speed of 4 feet/second. To make the character walk at a different
speed, we can simply scale the playback rate of the walk cycle animation. For
example, to make the character walk at 2 feet/second, we can simply play the
animation at half speed (R = 0.5).

12.11.4.2
Foot IK

Motion extraction does a good job of making a character’s feet appear ground-
ed when it is moving in a straight line (or, more correctly, when it moves in a
path that exactly matches the path animated by the animator). However, a real
game character must be turned and moved in ways that don’t coincide with
the original hand-animated path of motion (e.g., when moving over uneven
terrain). This results in additional foot sliding.
One solution to this problem is to use IK to correct for any sliding in the
feet. The basic idea is to analyze the animations to determine during which
periods of time each foot is fully in contact with the ground. At the moment
a foot contacts the ground, we note its world-space location. For all subse-
quent frames while that foot remains on the ground, we use IK to adjust the
pose of the leg so that the foot remains fixed to the proper location. This tech-
nique sounds easy enough, but getting it to look and feel right can be very
challenging. It requires a lot of iteration and fine-tuning. And some natural
human motions—like leading into a turn by increasing your stride—cannot be
produced by IK alone.
In addition, there is a big trade-off between the look of the animations and
the feel of the character, particularly for a human-controlled character. It’s gen-
erally more important for the player character control system to feel responsive


<!-- source-pdf-page: 835 -->

and fun than it is for the character’s animations to look perfect. The upshot is
this: Do not take the task of adding foot IK or motion extraction to your game
lightly. Budget time for a lot of trial and error, and be prepared to make trade-
offs to ensure that your player character not only looks good but feels good as
well.

### 12.11.5 Other Kinds of Constraints

There are plenty of other possible kinds of constraint systems that can be
added to a game animation engine. Some examples include:

•
Look-at. This is the ability for characters to look at points of interest in
the environment. A character might look at a point with only his or her
eyes, with eyes and head, or with eyes, head and a twist of the entire
upper body. Look-at constraints are sometimes implemented using IK
or procedural joint offsets, although a more natural look can often be
achieved via additive blending.

•
Cover registration. This is the ability for a character to align perfectly with
an object that is serving as cover. This is often implemented via the ref-
erence locator technique described above.
•
Cover entry and departure. If a character can take cover, animation blend-
ing and custom entry and departure animations must usually be used to
get the character into and out of cover.
•
Traversal aids. The ability for a character to navigate over, under, around
or through obstacles in the environment can add a lot of life to a game.
This is often done by providing custom animations and using a reference
locator to ensure proper registration with the obstacle being overcome.
