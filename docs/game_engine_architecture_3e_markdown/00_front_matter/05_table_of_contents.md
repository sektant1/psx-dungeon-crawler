# Table of Contents

> Source PDF pages: 8-13
> Extraction mode: PyMuPDF text blocks; line breaks and printed hyphenation are preserved.

<!-- source-pdf-page: 8 -->
> Visual fallback for diagrams/images: [PDF page 8](../visual_pages/page_0008.jpg)

Contents

Preface
xiii

I
Foundations
1

1
Introduction
3

1.1
Structure of a Typical Game Team
5

1.2
What Is a Game?
8

1.3
What Is a Game Engine?
11

1.4
Engine Differences across Genres
13

1.5
Game Engine Survey
31

1.6
Runtime Engine Architecture
38

1.7
Tools and the Asset Pipeline
59

2 Tools of the Trade
69

2.1
Version Control
69

2.2
Compilers, Linkers and IDEs
78

2.3
Proﬁling Tools
99


<!-- source-pdf-page: 9 -->

viii
CONTENTS

2.4
Memory Leak and Corruption Detection
101

2.5
Other Tools
102

3 Fundamentals of Software Engineering for Games
105

3.1
C++ Review and Best Practices
105

3.2
Catching and Handling Errors
119

3.3
Data, Code and Memory Layout
131

3.4
Computer Hardware Fundamentals
164

3.5
Memory Architectures
181

4 Parallelism and Concurrent Programming
203

4.1
Deﬁning Concurrency and Parallelism
204

4.2
Implicit Parallelism
211

4.3
Explicit Parallelism
225

4.4
Operating System Fundamentals
230

4.5
Introduction to Concurrent Programming
256

4.6
Thread Synchronization Primitives
267

4.7
Problems with Lock-Based Concurrency
281

4.8
Some Rules of Thumb for Concurrency
286

4.9
Lock-Free Concurrency
289

4.10
SIMD/Vector Processing
331

4.11
Introduction to GPGPU Programming
348

5 3D Math for Games
359

5.1
Solving 3D Problems in 2D
359

5.2
Points and Vectors
360

5.3
Matrices
375

5.4
Quaternions
394

5.5
Comparison of Rotational Representations
403

5.6
Other Useful Mathematical Objects
407

5.7
Random Number Generation
412


<!-- source-pdf-page: 10 -->

CONTENTS
ix

II
Low-Level Engine Systems
415

6 Engine Support Systems
417

6.1
Subsystem Start-Up and Shut-Down
417

6.2
Memory Management
426

6.3
Containers
441

6.4
Strings
456

6.5
Engine Conﬁguration
470

7 Resources and the File System
481

7.1
File System
482

7.2
The Resource Manager
493

8 The Game Loop and Real-Time Simulation
525

8.1
The Rendering Loop
525

8.2
The Game Loop
526

8.3
Game Loop Architectural Styles
529

8.4
Abstract Timelines
532

8.5
Measuring and Dealing with Time
534

8.6
Multiprocessor Game Loops
544

9 Human Interface Devices
559

9.1
Types of Human Interface Devices
559

9.2
Interfacing with a HID
561

9.3
Types of Inputs
563

9.4
Types of Outputs
569

9.5
Game Engine HID Systems
570

9.6
Human Interface Devices in Practice
587

10 Tools for Debugging and Development
589

10.1
Logging and Tracing
589

10.2
Debug Drawing Facilities
594

10.3
In-Game Menus
601

10.4
In-Game Console
604

10.5
Debug Cameras and Pausing the Game
605

10.6
Cheats
606


<!-- source-pdf-page: 11 -->

x
CONTENTS

10.7
Screenshots and Movie Capture
606

10.8
In-Game Proﬁling
608

10.9
In-Game Memory Stats and Leak Detection
615

III
Graphics, Motion and Sound
619

11 The Rendering Engine
621

11.1
Foundations of Depth-Buffered Triangle Rasterization
622

11.2
The Rendering Pipeline
667

11.3
Advanced Lighting and Global Illumination
697

11.4
Visual Effects and Overlays
710

11.5
Further Reading
719

12 Animation Systems
721

12.1
Types of Character Animation
721

12.2
Skeletons
727

12.3
Poses
729

12.4
Clips
734

12.5
Skinning and Matrix Palette Generation
750

12.6
Animation Blending
755

12.7
Post-Processing
774

12.8
Compression Techniques
777

12.9
The Animation Pipeline
784

12.10
Action State Machines
786

12.11
Constraints
806

13 Collision and Rigid Body Dynamics
817

13.1
Do You Want Physics in Your Game?
818

13.2
Collision/Physics Middleware
823

13.3
The Collision Detection System
825

13.4
Rigid Body Dynamics
854

13.5
Integrating a Physics Engine into Your Game
892

13.6
Advanced Physics Features
909


<!-- source-pdf-page: 12 -->

CONTENTS
xi

14 Audio
911

14.1
The Physics of Sound
912
14.2
The Mathematics of Sound
924
14.3
The Technology of Sound
941

14.4
Rendering Audio in 3D
955
14.5
Audio Engine Architecture
974

14.6
Game-Speciﬁc Audio Features
995

IV
Gameplay
1013

15 Introduction to Gameplay Systems
1015

15.1
Anatomy of a Game World
1016

15.2
Implementing Dynamic Elements: Game Objects
1021
15.3
Data-Driven Game Engines
1024
15.4
The Game World Editor
1025

16 Runtime Gameplay Foundation Systems
1039

16.1
Components of the Gameplay Foundation System
1039
16.2
Runtime Object Model Architectures
1043
16.3
World Chunk Data Formats
1062

16.4
Loading and Streaming Game Worlds
1069
16.5
Object References and World Queries
1079

16.6
Updating Game Objects in Real Time
1086
16.7
Applying Concurrency to Game Object Updates
1101
16.8
Events and Message-Passing
1114

16.9
Scripting
1134
16.10
High-Level Game Flow
1157

V
Conclusion
1159

17 You Mean There’s More?
1161

17.1
Some Engine Systems We Didn’t Cover
1161
17.2
Gameplay Systems
1162

Bibliography
1167

Index
1171


<!-- source-pdf-page: 13 -->
> Visual fallback for diagrams/images: [PDF page 13](../visual_pages/page_0013.jpg)

Taylor & Francis

Taylor & Francis Group
http://taylorandfrancis.com
