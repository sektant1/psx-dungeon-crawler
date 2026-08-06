# 6 Engine Support Systems

> Source PDF pages: 436-499
> Extraction mode: PyMuPDF text blocks; line breaks and printed hyphenation are preserved.

<!-- source-pdf-page: 436 -->

E
very game engine requires some low-level support systems that manage
mundane but crucial tasks, such as starting up and shutting down the
engine, configuring engine and game features, managing the engine’s memory
usage, handling access to file system(s), providing access to the wide range of
heterogeneous asset types used by the game (meshes, textures, animations,
audio, etc.), and providing debugging tools for use by the game development
team. This chapter will focus on the lowest-level support systems found in
most game engines. In the chapters that follow, we will explore some of the
larger core systems, including resource management, human interface devices
and in-game debugging tools.

## 6.1 Subsystem Start-Up and Shut-Down

A game engine is a complex piece of software consisting of many interacting
subsystems. When the engine first starts up, each subsystem must be config-
ured and initialized in a specific order. Interdependencies between subsys-
tems implicitly define the order in which they must be started—i.e., if subsys-
tem B depends on subsystem A, then A will need to be started up before B can
be initialized. Shut-down typically occurs in the reverse order, so B would
shut down first, followed by A.


<!-- source-pdf-page: 437 -->

### 6.1.1 C++ Static Initialization Order (or Lack Thereof)

Since the programming language used in most modern game engines is C++,
we should briefly consider whether C++’s native start-up and shut-down se-
mantics can be leveraged in order to start up and shut down our engine’s sub-
systems. In C++, global and static objects are constructed before the program’s
entry point (main(), or WinMain() under Windows) is called. However,
these constructors are called in a totally unpredictable order. The destructors
of global and static class instances are called after main() (or WinMain())
returns, and once again they are called in an unpredictable order. Clearly this
behavior is not desirable for initializing and shutting down the subsystems
of a game engine, or indeed any software system that has interdependencies
between its global objects.

This is somewhat unfortunate, because a common design pattern for im-
plementing major subsystems such as the ones that make up a game engine is
to define a singleton class (often called a manager) for each subsystem. If C++
gave us more control over the order in which global and static class instances
were constructed and destroyed, we could define our singleton instances as
globals, without the need for dynamic memory allocation. For example, we
could write:

class RenderManager
{
public:
RenderManager()
{
// start up the manager...
}

~RenderManager()
{
// shut down the manager...
}

// ...
};

// singleton instance
static RenderManager gRenderManager;

Alas, with no way to directly control construction and destruction order, this
approach won’t work.


<!-- source-pdf-page: 438 -->

6.1.1.1
Construct On Demand

There is one C++ “trick” we can leverage here. A static variable that is de-
clared within a function will not be constructed before main() is called,
but rather on the first invocation of that function. So if our global single-
ton is function-static, we can control the order of construction for our global
singletons.

class RenderManager
{
public:
// Get the one and only instance.
static RenderManager& get()
{
// This function-static will be constructed on the
// first call to this function.
static RenderManager sSingleton;
return sSingleton;
}

RenderManager()
{
// Start up other managers we depend on, by
// calling their get() functions first...
VideoManager::get();
TextureManager::get();

// Now start up the render manager.
// ...
}

~RenderManager()
{
// Shut down the manager.
// ...
}
};

You’ll find that many software engineering textbooks suggest this design
or a variant that involves dynamic allocation of the singleton as shown below.

static RenderManager& get()
{
static RenderManager* gpSingleton = nullptr;
if (gpSingleton == nullptr)
{
gpSingleton = new RenderManager;


<!-- source-pdf-page: 439 -->

}
ASSERT(gpSingleton);
return *gpSingleton;
}

Unfortunately, this still gives us no way to control destruction order. It
is possible that C++ will destroy one of the managers upon which the
RenderManager depends for its shut-down procedure,
prior to the
RenderManager’s destructor being called. In addition, it’s difficult to predict
exactly when the RenderManager singleton will be constructed, because the
construction will happen on the first call to RenderManager::get()—and
who knows when that might be? Moreover, the programmers using the class
may not be expecting an innocuous-looking get() function to do something
expensive, like allocating and initializing a heavyweight singleton. This is an
unpredictable and dangerous design. Therefore, we are prompted to resort to
a more direct approach that gives us greater control.

### 6.1.2 A Simple Approach That Works

Let’s presume that we want to stick with the idea of singleton managers for
our subsystems. In this case, the simplest “brute-force” approach is to define
explicit start-up and shut-down functions for each singleton manager class.
These functions take the place of the constructor and destructor, and in fact
we should arrange for the constructor and destructor to do absolutely nothing.
That way, the start-up and shut-down functions can be explicitly called in the
required order from within main() (or from some overarching singleton object
that manages the engine as a whole). For example:

class RenderManager
{
public:
RenderManager()
{
// do nothing
}

~RenderManager()
{
// do nothing
}

void startUp()
{
// start up the manager...


<!-- source-pdf-page: 440 -->

}

void shutDown()
{
// shut down the manager...
}

// ...
};

class PhysicsManager
{ /* similar... */ };

class AnimationManager
{ /* similar... */ };

class MemoryManager
{ /* similar... */ };

class FileSystemManager { /* similar... */ };

// ...

RenderManager
gRenderManager;
PhysicsManager
gPhysicsManager;
AnimationManager
gAnimationManager;
TextureManager
gTextureManager;
VideoManager
gVideoManager;
MemoryManager
gMemoryManager;
FileSystemManager
gFileSystemManager;
// ...

int main(int argc, const char* argv)
{
// Start up engine systems in the correct order.
gMemoryManager.startUp();
gFileSystemManager.startUp();
gVideoManager.startUp();
gTextureManager.startUp();
gRenderManager.startUp();
gAnimationManager.startUp();
gPhysicsManager.startUp();
// ...

// Run the game.
gSimulationManager.run();

// Shut everything down, in reverse order.
// ...
gPhysicsManager.shutDown();


<!-- source-pdf-page: 441 -->

gAnimationManager.shutDown();
gRenderManager.shutDown();
gFileSystemManager.shutDown();
gMemoryManager.shutDown();

return 0;
}

There are “more elegant” ways to accomplish this. For example, you could
have each manager register itself into a global priority queue and then walk
this queue to start up all the managers in the proper order. You could define the
manager-to-manager dependency graph by having each manager explicitly
list the other managers upon which it depends and then write some code to
calculate the optimal start-up order given their interdependencies. You could
use the construct-on-demand approach outlined above. In my experience, the
brute-force approach always wins out, because of the following:

•
It’s simple and easy to implement.

•
It’s explicit. You can see and understand the start-up order immediately
by just looking at the code.

•
It’s easy to debug and maintain. If something isn’t starting early enough,
or is starting too early, you can just move one line of code.

One minor disadvantage to the brute-force manual start-up and shut-down
method is that you might accidentally shut things down in an order that isn’t
strictly the reverse of the start-up order. But I wouldn’t lose any sleep over it.
As long as you can start up and shut down your engine’s subsystems success-
fully, you’re golden.

### 6.1.3 Some Examples from Real Engines

Let’s take a brief look at some examples of engine start-up and shut-down
taken from real game engines.

6.1.3.1
OGRE

OGRE is by its authors’ admission a rendering engine, not a game engine
per se. But by necessity it provides many of the low-level features found in
full-fledged game engines, including a simple and elegant start-up and shut-
down mechanism. Everything in OGRE is controlled by the singleton object
Ogre::Root. It contains pointers to every other subsystem in OGRE and man-
ages their creation and destruction. This makes it very easy for a programmer
to start up OGRE—just new an instance of Ogre::Root and you’re done.


<!-- source-pdf-page: 442 -->

Here are a few excerpts from the OGRE source code so we can see what it’s
doing:

OgreRoot.h
class _OgreExport Root : public Singleton<Root>
{
// <some code omitted...>

// Singletons
LogManager* mLogManager;
ControllerManager* mControllerManager;
SceneManagerEnumerator* mSceneManagerEnum;
SceneManager* mCurrentSceneManager;
DynLibManager* mDynLibManager;
ArchiveManager* mArchiveManager;
MaterialManager* mMaterialManager;
MeshManager* mMeshManager;
ParticleSystemManager* mParticleManager;
SkeletonManager* mSkeletonManager;
OverlayElementFactory* mPanelFactory;
OverlayElementFactory* mBorderPanelFactory;
OverlayElementFactory* mTextAreaFactory;
OverlayManager* mOverlayManager;
FontManager* mFontManager;
ArchiveFactory *mZipArchiveFactory;
ArchiveFactory *mFileSystemArchiveFactory;
ResourceGroupManager* mResourceGroupManager;
ResourceBackgroundQueue* mResourceBackgroundQueue;
ShadowTextureManager* mShadowTextureManager;

// etc.
};
OgreRoot.cpp
Root::Root(const String& pluginFileName,
const String& configFileName,
const String& logFileName) :
mLogManager(0),
mCurrentFrame(0),
mFrameSmoothingTime(0.0f),
mNextMovableObjectTypeFlag(1),
mIsInitialised(false)
{
// superclass will do singleton checking
String msg;

// Init
mActiveRenderer = 0;


<!-- source-pdf-page: 443 -->

mVersion
= StringConverter::toString(OGRE_VERSION_MAJOR)
+ "."
+ StringConverter::toString(OGRE_VERSION_MINOR)
+ "."
+ StringConverter::toString(OGRE_VERSION_PATCH)
+ OGRE_VERSION_SUFFIX + " "
+ "(" + OGRE_VERSION_NAME + ")";
mConfigFileName = configFileName;

// create log manager and default log file if there
// is no log manager yet
if(LogManager::getSingletonPtr() == 0)
{
mLogManager = new LogManager();
mLogManager->createLog(logFileName, true, true);
}

// dynamic library manager
mDynLibManager = new DynLibManager();
mArchiveManager = new ArchiveManager();

// ResourceGroupManager
mResourceGroupManager = new ResourceGroupManager();

// ResourceBackgroundQueue
mResourceBackgroundQueue
= new ResourceBackgroundQueue();

// and so on...
}

OGRE provides a templated Ogre::Singleton base class from which all of
its singleton (manager) classes derive. If you look at its implementation, you’ll
see that Ogre::Singleton does not use deferred construction but instead re-
lies on Ogre::Root to explicitly new each singleton. As we discussed above,
this is done to ensure that the singletons are created and destroyed in a well-
defined order.

6.1.3.2
Naughty Dog’s Uncharted and The Last of Us Series

The engine created by Naughty Dog, Inc. for its Uncharted and The Last of Us
series of games uses a similar explicit technique for starting up its subsystems.
You’ll notice by looking at the following code that engine start-up is not al-
ways a simple sequence of allocating singleton instances. A wide range of
operating system services, third-party libraries and so on must all be started


<!-- source-pdf-page: 444 -->

up during engine initialization. Also, dynamic memory allocation is avoided
wherever possible, so many of the singletons are statically allocated objects
(e.g., g_fileSystem, g_languageMgr, etc.) It’s not always pretty, but it
gets the job done.

Err BigInit()
{
init_exception_handler();

U8* pPhysicsHeap = new(kAllocGlobal, kAlign16)
U8[ALLOCATION_GLOBAL_PHYS_HEAP];
PhysicsAllocatorInit(pPhysicsHeap,
ALLOCATION_GLOBAL_PHYS_HEAP);

g_textDb.Init();
g_textSubDb.Init();
g_spuMgr.Init();

g_drawScript.InitPlatform();

PlatformUpdate();

thread_t init_thr;
thread_create(&init_thr, threadInit, 0, 30,
64*1024, 0, "Init");

char masterConfigFileName[256];
snprintf(masterConfigFileName,
sizeof(masterConfigFileName),
MASTER_CFG_PATH);
{
Err err = ReadConfigFromFile(
masterConfigFileName);
if (err.Failed())
{
MsgErr("Config file not found (%s).\n",
masterConfigFileName);
}
}

memset(&g_discInfo, 0, sizeof(BootDiscInfo));
int err1 = GetBootDiscInfo(&g_discInfo);
Msg("GetBootDiscInfo() : 0x%x\n", err1);
if(err1 == BOOTDISCINFO_RET_OK)
{
printf("titleId
: [%s]\n",
g_discInfo.titleId);


<!-- source-pdf-page: 445 -->

printf("parentalLevel : [%d]\n",
g_discInfo.parentalLevel);
}

g_fileSystem.Init(g_gameInfo.m_onDisc);

g_languageMgr.Init();
if (g_shouldQuit) return Err::kOK;

// and so on...

## 6.2 Memory Management

As game developers, we are always trying to make our code run more quickly.
The performance of any piece of software is dictated not only by the algorithms
it employs, or the efficiency with which those algorithms are coded, but also
by how the program utilizes memory (RAM). Memory affects performance in
two ways:

1.
Dynamic memory allocation via malloc() or C++’s global operator new
is a very slow operation. We can improve the performance of our code
by either avoiding dynamic allocation altogether or by making use of
custom memory allocators that greatly reduce allocation costs.

2.
On modern CPUs, the performance of a piece of software is often dom-
inated by its memory access patterns. As we’ll see, data that is located in
small, contiguous blocks of memory can be operated on much more effi-
ciently by the CPU than if that same data were to be spread out across
a wide range of memory addresses. Even the most efficient algorithm,
coded with the utmost care, can be brought to its knees if the data upon
which it operates is not laid out efficiently in memory.

In this section, we’ll learn how to optimize our code’s memory utilization
along these two axes.

### 6.2.1 Optimizing Dynamic Memory Allocation

Dynamic memory allocation via malloc() and free() or C++’s global new
and delete operators—also known as heap allocation—is typically very slow.
The high cost can be attributed to two main factors. First, a heap allocator
is a general-purpose facility, so it must be written to handle any allocation
size, from one byte to one gigabyte. This requires a lot of management over-
head, making the malloc() and free() functions inherently slow. Second,


<!-- source-pdf-page: 446 -->

on most operating systems a call to malloc() or free() must first context-
switch from user mode into kernel mode, process the request and then context-
switch back to the program. These context switches can be extraordinarily
expensive. One rule of thumb often followed in game development is:

Keep heap allocations to a minimum, and never allocate from the
heap within a tight loop.

Of course, no game engine can entirely avoid dynamic memory allocation,
so most game engines implement one or more custom allocators. A custom
allocator can have better performance characteristics than the operating sys-
tem’s heap allocator for two reasons. First, a custom allocator can satisfy re-
quests from a preallocated memory block (itself allocated using malloc() or
new, or declared as a global variable). This allows it to run in user mode and
entirely avoid the cost of context-switching into the operating system. Second,
by making various assumptions about its usage patterns, a custom allocator
can be much more efficient than a general-purpose heap allocator.
In the following sections, we’ll take a look at some common kinds of
custom allocators.
For additional information on this topic, see Christian
Gyrling’s excellent blog post, http://www.swedishcoding.com/2008/08/31/
are-we-out-of-memory.

6.2.1.1
Stack-Based Allocators

Many games allocate memory in a stack-like fashion. Whenever a new game
level is loaded, memory is allocated for it. Once the level has been loaded,
little or no dynamic memory allocation takes place.
At the conclusion of
the level, its data is unloaded and all of its memory can be freed. It makes
a lot of sense to use a stack-like data structure for these kinds of memory
allocations.
A stack allocator is very easy to implement. We simply allocate a large con-
tiguous block of memory using malloc() or global new, or by declaring a
global array of bytes (in which case the memory is effectively allocated out of
the executable’s BSS segment). A pointer to the top of the stack is maintained.
All memory addresses below this pointer are considered to be in use, and all
addresses above it are considered to be free. The top pointer is initialized to
the lowest memory address in the stack. Each allocation request simply moves
the pointer up by the requested number of bytes. The most recently allocated
block can be freed by simply moving the top pointer back down by the size of
the block.


<!-- source-pdf-page: 447 -->
> Visual fallback for diagrams/images: [PDF page 447](../../../visual_pages/page_0447.jpg)

Obtain marker after allocating blocks A and B.

A
B

Allocate additional blocks C, D and E.

A
B
C
D
E

Free back to marker.

A
B

Figure 6.1. Stack allocation and freeing back to a marker.

It is important to realize that with a stack allocator, memory cannot be
freed in an arbitrary order. All frees must be performed in an order oppo-
site to that in which they were allocated. One simple way to enforce these
restrictions is to disallow individual blocks from being freed at all. Instead,
we can provide a function that rolls the stack top back to a previously marked
location, thereby freeing all blocks between the current top and the roll-back
point.
It’s important to always roll the top pointer back to a point that lies at the
boundary between two allocated blocks, because otherwise new allocations
would overwrite the tail end of the top-most block. To ensure that this is done
properly, a stack allocator often provides a function that returns a marker rep-
resenting the current top of the stack. The roll-back function then takes one of
these markers as its argument. This is depicted in Figure 6.1. The interface of
a stack allocator often looks something like this.

class StackAllocator
{
public:
// Stack marker: Represents the current top of the
// stack. You can only roll back to a marker, not to
// arbitrary locations within the stack.
typedef U32 Marker;


<!-- source-pdf-page: 448 -->
> Visual fallback for diagrams/images: [PDF page 448](../../../visual_pages/page_0448.jpg)

// Constructs a stack allocator with the given total
// size.
explicit StackAllocator(U32 stackSize_bytes);

// Allocates a new block of the given size from stack
// top.
void* alloc(U32 size_bytes);

// Returns a marker to the current stack top.
Marker getMarker();

// Rolls the stack back to a previous marker.
void freeToMarker(Marker marker);

// Clears the entire stack (rolls the stack back to
// zero).
void clear();

private:
// ...
};

Double-Ended Stack Allocators

A single memory block can actually contain two stack allocators—one that
allocates up from the bottom of the block and one that allocates down from
the top of the block. A double-ended stack allocator is useful because it uses
memory more efficiently by allowing a trade-off to occur between the memory
usage of the bottom stack and the memory usage of the top stack. In some
situations, both stacks may use roughly the same amount of memory and meet
in the middle of the block. In other situations, one of the two stacks may eat
up a lot more memory than the other stack, but all allocation requests can still
be satisfied as long as the total amount of memory requested is not larger than
the block shared by the two stacks. This is depicted in Figure 6.2.
In Midway’s Hydro Thunder arcade game, all memory allocations are made
from a single large block of memory managed by a double-ended stack allo-
cator. The bottom stack is used for loading and unloading levels (race tracks),
while the top stack is used for temporary memory blocks that are allocated and
freed every frame. This allocation scheme worked extremely well and ensured
that Hydro Thunder never suffered from memory fragmentation problems (see
Section 6.2.1.4). Steve Ranck, Hydro Thunder’s lead engineer, describes this al-
location technique in depth in [8, Section 1.9].


<!-- source-pdf-page: 449 -->
> Visual fallback for diagrams/images: [PDF page 449](../../../visual_pages/page_0449.jpg)

Lower
Upper

Figure 6.2. A double-ended stack allocator.

6.2.1.2
Pool Allocators

It’s quite common in game engine programming (and software engineering in
general) to allocate lots of small blocks of memory, each of which are the same
size. For example, we might want to allocate and free matrices, or iterators, or
links in a linked list, or renderable mesh instances. For this type of memory
allocation pattern, a pool allocator is often the perfect choice.
A pool allocator works by preallocating a large block of memory whose
size is an exact multiple of the size of the elements that will be allocated. For
example, a pool of 4 × 4 matrices would be an exact multiple of 64 bytes—
that’s 16 elements per matrix times four bytes per element (assuming each
element is a 32-bit float). Each element within the pool is added to a linked
list of free elements; when the pool is first initialized, the free list contains all of
the elements. Whenever an allocation request is made, we simply grab the next
free element off the free list and return it. When an element is freed, we simply
tack it back onto the free list. Both allocations and frees are O(1) operations,
since each involves only a couple of pointer manipulations, no matter how
many elements are currently free. (The notation O(1) is an example of “big
O” notation. In this case it means that the execution times of both allocations
and frees are roughly constant and do not depend on things like the number
of elements currently in the pool. See Section 6.3.3 for an explanation of “big
O” notation.)
The linked list of free elements can be a singly-linked list, meaning that we
need a single pointer (four bytes on 32-bit machines or eight bytes on 64-bit
machines) for each free element. Where should we obtain the memory for all
these pointers? Certainly they could be stored in a separate preallocated mem-
ory block, occupying (sizeof(void*) * numElementsInPool) bytes.
However, this is unduly wasteful. The key is to realize that the memory blocks
residing on the free list are, by definition, free memory blocks. So why not store
each free list “next” pointer within the free block itself? This little “trick” works
as long as elementSize >= sizeof(void*). We don’t waste any mem-
ory, because our free list pointers all reside inside the free memory blocks—in
memory that wasn’t being used for anything anyway!
If each element is smaller than a pointer, then we can use pool element in-


<!-- source-pdf-page: 450 -->

dices instead of pointers to implement our linked list. For example, if our pool
contains 16-bit integers, then we can use 16-bit indices as the “next pointers”
in our linked list. This works as long as the pool doesn’t contain more than
216 = 65,536 elements.

6.2.1.3
Aligned Allocations

As we saw in Section 3.3.7.1, every variable and data object has an alignment
requirement. An 8-bit integer variable can be aligned to any address, but a
32-bit integer or floating-point variable must be 4-byte aligned, meaning its
address can only end in the nibbles 0x0, 0x4, 0x8 or 0xC. A 128-bit SIMD vector
value generally has a 16-byte alignment requirement, meaning that its memory
address can end only in the nibble 0x0. On the PS3, memory blocks that are
to be transferred to an SPU via the direct memory access (DMA) controller
should be 128-byte aligned for maximum DMA throughput, meaning they can
only end in the bytes 0x00 or 0x80.
All memory allocators must be capable of returning aligned memory
blocks. This is relatively straightforward to implement. We simply allocate
a little bit more memory than was actually requested, shift the address of the
memory block upward slightly so that it is aligned properly, and then return
the shifted address. Because we allocated a bit more memory than was re-
quested, the returned block will still be large enough, even with the slight
upward shift.
In most implementations, the number of additional bytes allocated is equal
to the alignment minus one, which is the worst-case alignment shift we can
make. For example, if we want a 16-byte aligned memory block, the worst
case would be to get back an unaligned pointer that ends in 0x1, because it
would require us to apply a shift of 15 bytes to bring it to a 16-byte boundary.
Here’s one possible implementation of an aligned memory allocator:

// Shift the given address upwards if/as necessary to
// ensure it is aligned to the given number of bytes.
inline uintptr_t AlignAddress(uintptr_t addr, size_t align)
{
const size_t mask = align - 1;
assert((align & mask) == 0); // pwr of 2
return (addr + mask) & ~mask;
}

// Shift the given pointer upwards if/as necessary to
// ensure it is aligned to the given number of bytes.
template<typename T>
inline T* AlignPointer(T* ptr, size_t align)


<!-- source-pdf-page: 451 -->

{
const uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
const uintptr_t addrAligned = AlignAddress(addr, align);
return reinterpret_cast<T*>(addrAligned);
}

// Aligned allocation function. IMPORTANT: 'align'
// must be a power of 2 (typically 4, 8 or 16).
void* AllocAligned(size_t bytes, size_t align)
{
// Determine worst case number of bytes we'll need.
size_t worstCaseBytes = bytes + align - 1;

// Allocate unaligned block.
U8* pRawMem = new U8[worstCaseBytes];

// Align the block.
return AlignPointer(pRawMem, align);
}

The alignment “magic” is performed by the function AlignAddress().
Here’s how it works: Given an address and a desired alignment L, we can
align that address to an L-byte boundary by first adding L −1 to it, and
then stripping off the N least-significant bits of the resulting address, where
N = log2(L). For example, to align any address to a 16-byte boundary, we
shift it up by 15 bytes and then mask off the N = log2(16) = 4 least-significant
bits.
To strip off these bits, we need a mask that we can apply to the address using
the bitwise AND operator. Because L is always a power of two, L −1 will be
a mask with binary 1s in the N least-significant bits and binary 0s everywhere
else. So all we need to do is invert this mask and then AND it with the address
(addr & ~mask).

Freeing Aligned Blocks

When an aligned block is later freed, we will be passed the shifted address,
not the original address that we allocated. But in order to free the memory,
we need to free the address that was actually returned by new. How can we
convert an aligned address back into its original, unaligned address?
One simple approach is to store the shift (i.e., the difference between the
aligned address and the original address) some place where our free function
will be able to find it. Recall that we actually allocate align - 1 extra bytes in
AllocAligned(), in order to give us some room to align the pointer. Those


<!-- source-pdf-page: 452 -->
> Visual fallback for diagrams/images: [PDF page 452](../../../visual_pages/page_0452.jpg)

extra bytes are a perfect place to store the shift value. The smallest shift we’ll
ever make is one byte, so that’s the minimum space we’ll have to store the
offset. Therefore, given an aligned pointer p, we can simply store the shift as
a one-byte value at address p - 1.
However, there’s one problem: It’s possible that the raw address returned
by new will already be aligned. In that case, the code we presented above
would not shift the raw address at all, meaning there’d be no extra bytes into
which to store the offset. To overcome this, we simply allocate L extra bytes,
instead of L −1, and then we always shift the raw pointer up to the next L-byte
boundary, even if it was already aligned. Now the maximum shift will be L
bytes, and the minimum shift will be 1 byte. So there will always be at least
one extra byte into which we can store our shift value.
Storing the shift in a single byte works for alignments up to and including
128. We never ever shift a pointer by zero bytes, so we can make this scheme
work for up to 256-byte alignment by interpreting the impossible shift value
of zero as a 256-byte shift. (For larger alignments, we’d have to allocate even
more bytes, and shift the pointer even further up, to make room for a wider
“header”.)
Here’s how the modified AllocAligned() function and its correspond-
ing FreeAligned() function could be implemented. The process of allocat-
ing and freeing aligned blocks is illustrated in Figure 6.3.

// Aligned allocation function. IMPORTANT: 'align'
// must be a power of 2 (typically 4, 8 or 16).
void* AllocAligned(size_t bytes, size_t align)
{
// Allocate 'align' more bytes than we need.
size_t actualBytes = bytes + align;

// Allocate unaligned block.
U8* pRawMem = new U8[actualBytes];

// Align the block. If no alignment occurred,
// shift it up the full 'align' bytes so we
// always have room to store the shift.
U8* pAlignedMem = AlignPointer(pRawMem, align);
if (pAlignedMem == pRawMem)
pAlignedMem += align;

// Determine the shift, and store it.
// (This works for up to 256-byte alignment.)
ptrdiff_t shift = pAlignedMem - pRawMem;
assert(shift > 0 && shift <= 256);


<!-- source-pdf-page: 453 -->
> Visual fallback for diagrams/images: [PDF page 453](../../../visual_pages/page_0453.jpg)

pAlignedMem[-1] = static_cast<U8>(shift & 0xFF);

return pAlignedMem;
}

void FreeAligned(void* pMem)
{
if (pMem)
{
// Convert to U8 pointer.
U8* pAlignedMem = reinterpret_cast<U8*>(pMem);

// Extract the shift.
ptrdiff_t shift = pAlignedMem[-1];
if (shift == 0)
shift = 256;

// Back up to the actual allocated address,
// and array-delete it.
U8* pRawMem = pAlignedMem - shift;
delete[] pRawMem;
}
}

Figure 6.3. Aligned memory allocation with a 16-byte alignment requirement. The difference be-
tween the allocated memory address and the adjusted (aligned) address is stored in the byte im-
mediately preceding the adjusted address, so that it may be retrieved during free.

6.2.1.4
Single-Frame and Double-Buffered Memory Allocators

Virtually all game engines allocate at least some temporary data during the
game loop. This data is either discarded at the end of each iteration of the
loop or used on the next frame and then discarded. This allocation pattern is so
common that many engines support single-frame and double-buffered allocators.


<!-- source-pdf-page: 454 -->

Single-Frame Allocators

A single-frame allocator is implemented by reserving a block of memory and
managing it with a simple stack allocator as described above. At the beginning
of each frame, the stack’s “top” pointer is cleared to the bottom of the memory
block. Allocations made during the frame grow toward the top of the block.
Rinse and repeat.

StackAllocator g_singleFrameAllocator;

// Main Game Loop
while (true)
{
// Clear the single-frame allocator's buffer every
// frame.
g_singleFrameAllocator.clear();

// ...

// Allocate from the single-frame buffer. We never
// need to free this data! Just be sure to use it
// only this frame.
void* p = g_singleFrameAllocator.alloc(nBytes);

// ...
}

One of the primary benefits of a single-frame allocator is that allocated
memory needn’t ever be freed—we can rely on the fact that the allocator will
be cleared at the start of every frame. Single-frame allocators are also blind-
ingly fast. The one big negative is that using a single-frame allocator requires
a reasonable level of discipline on the part of the programmer. You need to
realize that a memory block allocated out of the single-frame buffer will only
be valid during the current frame. Programmers must never cache a pointer to
a single-frame memory block across the frame boundary!

Double-Buffered Allocators

A double-buffered allocator allows a block of memory allocated on frame i to
be used on frame (i + 1). To accomplish this, we create two single-frame stack
allocators of equal size and then ping-pong between them every frame.

class DoubleBufferedAllocator
{
U32
m_curStack;


<!-- source-pdf-page: 455 -->

StackAllocator m_stack[2];

public:

void swapBuffers()
{
m_curStack = (U32)!m_curStack;
}

void clearCurrentBuffer()
{
m_stack[m_curStack].clear();
}

void* alloc(U32 nBytes)
{
return m_stack[m_curStack].alloc(nBytes);
}

// ...
};

// ...

DoubleBufferedAllocator g_doubleBufAllocator;

// Main Game Loop
while (true)
{
// Clear the single-frame allocator every frame as
// before.
g_singleFrameAllocator.clear();

// Swap the active and inactive buffers of the double-
// buffered allocator.
g_doubleBufAllocator.swapBuffers();

// Now clear the newly active buffer, leaving last
// frame's buffer intact.
g_doubleBufAllocator.clearCurrentBuffer();

// ...

// Allocate out of the current buffer, without
// disturbing last frame's data. Only use this data
// this frame or next frame. Again, this memory never


<!-- source-pdf-page: 456 -->
> Visual fallback for diagrams/images: [PDF page 456](../../../visual_pages/page_0456.jpg)

// needs to be freed.
void* p = g_doubleBufAllocator.alloc(nBytes);

// ...
}

This kind of allocator is extremely useful for caching the results of asyn-
chronous processing on a multicore game console like the Xbox 360, Xbox One,
PlayStation 3 or PlayStation 4. On frame i, we can kick off an asynchronous
job on one of the PS4’s cores, for example, handing it the address of a destina-
tion buffer that has been allocated from our double-buffered allocator. The job
runs and produces its results some time before the end of frame i, storing them
into the buffer we provided. On frame (i + 1), the buffers are swapped. The
results of the job are now in the inactive buffer, so they will not be overwritten
by any double-buffered allocations that might be made during this frame. As
long as we use the results of the job before frame (i + 2), our data won’t be
overwritten.

### 6.2.2 Memory Fragmentation

Another problem with dynamic heap allocations is that memory can become
fragmented over time. When a program first runs, its heap memory is entirely
free. When a block is allocated, a contiguous region of heap memory of the
appropriate size is marked as “in use,” and the remainder of the heap remains
free. When a block is freed, it is marked as such, and adjacent free blocks are
merged into a single, larger free block. Over time, as allocations and deallo-
cations of various sizes occur in random order, the heap memory begins to
look like a patchwork of free and used blocks. We can think of the free regions
as “holes” in the fabric of used memory. When the number of holes becomes
large, and/or the holes are all relatively small, we say the memory has become
fragmented. This is illustrated in Figure 6.4.
The problem with memory fragmentation is that allocations may fail even
when there are enough free bytes to satisfy the request. The crux of the prob-
lem is that allocated memory blocks must always be contiguous. For example,
in order to satisfy a request of 128 KiB, there must exist a free “hole” that is
128 KiB or larger. If there are two holes, each of which is 64 KiB in size, then
enough bytes are available but the allocation fails because they are not contigu-
ous bytes.
Memory fragmentation is not as much of a problem on operating sys-
tems that support virtual memory. A virtual memory system maps discontigu-
ous blocks of physical memory known as pages into a virtual address space, in


<!-- source-pdf-page: 457 -->
> Visual fallback for diagrams/images: [PDF page 457](../../../visual_pages/page_0457.jpg)

free

After one allocation...

free
used

After eight allocations...

After eight allocations and three frees...

After n allocations and m frees...

Figure 6.4. Memory fragmentation.

which the pages appear to the application to be contiguous. Stale pages can
be swapped to the hard disk when physical memory is in short supply and
reloaded from disk when they are needed. For a detailed discussion of how
virtual memory works, see http://en.wikipedia.org/wiki/Virtual_memory.
Most embedded systems cannot afford to implement a virtual memory system.
While some modern consoles do technically support it, most console game en-
gines still do not make use of virtual memory due to the inherent performance
overhead.

6.2.2.1
Avoiding Fragmentation with Stack and Pool Allocators

The detrimental effects of memory fragmentation can be avoided by using
stack and/or pool allocators.

•
A stack allocator is impervious to fragmentation because allocations are
always contiguous, and blocks must be freed in an order opposite to that
in which they were allocated. This is illustrated in Figure 6.5.


<!-- source-pdf-page: 458 -->
> Visual fallback for diagrams/images: [PDF page 458](../../../visual_pages/page_0458.jpg)

Single free block, always contiguous
Allocated blocks, always contiguous

allocation

deallocation

Figure 6.5. A stack allocator is free from fragmentation problems.

Allocated and free blocks all the same size

Figure 6.6. A pool allocator is not degraded by fragmentation.

•
A pool allocator is also free from fragmentation problems. Pools do be-
come fragmented, but the fragmentation never causes premature out-of-
memory conditions as it does in a general-purpose heap. Pool allocation
requests can never fail due to a lack of a large enough contiguous free
block, because all of the blocks are exactly the same size. This is shown
in Figure 6.6.

6.2.2.2
Defragmentation and Relocation

When differently sized objects are being allocated and freed in a random order,
neither a stack-based allocator nor a pool-based allocator can be used. In such
cases, fragmentation can be avoided by periodically defragmenting the heap.
Defragmentation involves coalescing all of the free “holes” in the heap by shift-
ing allocated blocks from higher memory addresses down to lower addresses
(thereby shifting the holes up to higher addresses). One simple algorithm is
to search for the first “hole” and then take the allocated block immediately
above the hole and shift it down to the start of the hole. This has the effect
of “bubbling up” the hole to a higher memory address. If this process is re-
peated, eventually all the allocated blocks will occupy a contiguous region of
memory at the low end of the heap’s address space, and all the holes will have
bubbled up into one big hole at the high end of the heap. This is illustrated in
Figure 6.7.
The shifting of memory blocks described above is not particularly tricky to
implement. What is tricky is accounting for the fact that we’re moving allocated
blocks of memory around. If anyone has a pointer into one of these allocated
blocks, then moving the block will invalidate the pointer.


<!-- source-pdf-page: 459 -->
> Visual fallback for diagrams/images: [PDF page 459](../../../visual_pages/page_0459.jpg)

A
B
C
D
E

A
B
C
D
E

A
B
C
D
E

A
B
C
D
E

A
B
C
D
E

Figure 6.7. Defragmentation by shifting allocated blocks to lower addresses.

The solution to this problem is to patch any and all pointers into a shifted
memory block so that they point to the correct new address after the shift. This
procedure is known as pointer relocation. Unfortunately, there is no general-
purpose way to find all the pointers that point into a particular region of mem-
ory. So if we are going to support memory defragmentation in our game en-
gine, programmers must either carefully keep track of all the pointers man-
ually so they can be relocated, or pointers must be abandoned in favor of
something inherently more amenable to relocation, such as smart pointers or
handles.
A smart pointer is a small class that contains a pointer and acts like a
pointer for most intents and purposes. But because a smart pointer is a class,
it can be coded to handle memory relocation properly. One approach is to
arrange for all smart pointers to add themselves to a global linked list. When-
ever a block of memory is shifted within the heap, the linked list of all smart
pointers can be scanned, and each pointer that points into the shifted block of
memory can be adjusted appropriately.
A handle is usually implemented as an index into a non-relocatable table,
which itself contains the pointers. When an allocated block is shifted in mem-
ory, the handle table can be scanned and all relevant pointers found and up-
dated automatically. Because the handles are just indices into the pointer table,
their values never change no matter how the memory blocks are shifted, so the
objects that use the handles are never affected by memory relocation.
Another problem with relocation arises when certain memory blocks can-
not be relocated. For example, if you are using a third-party library that does
not use smart pointers or handles, it’s possible that any pointers into its data
structures will not be relocatable. The best way around this problem is usu-
ally to arrange for the library in question to allocate its memory from a special
buffer outside of the relocatable memory area. The other option is to simply


<!-- source-pdf-page: 460 -->

accept that some blocks will not be relocatable. If the number and size of the
non-relocatable blocks are both small, a relocation system will still perform
quite well.
It is interesting to note that all of Naughty Dog’s engines have supported
defragmentation. Handles are used wherever possible to avoid the need to
relocate pointers. However, in some cases raw pointers cannot be avoided.
These pointers are carefully tracked and relocated manually whenever a mem-
ory block is shifted due to defragmentation. A few of Naughty Dog’s game
object classes are not relocatable for various reasons. However, as mentioned
above, this doesn’t pose any practical problems, because the number of such
objects is always very small, and their sizes are tiny when compared to the
overall size of the relocatable memory area.

Amortizing Defragmentation Costs

Defragmentation can be a slow operation because it involves copying memory
blocks. However, we needn’t fully defragment the heap all at once. Instead,
the cost can be amortized over many frames. We can allow up to N allocated
blocks to be shifted each frame, for some small value of N like 8 or 16. If
our game is running at 30 frames per second, then each frame lasts 1/30 of a
second (33 ms). So, the heap can usually be completely defragmented in less
than one second without having any noticeable effect on the game’s frame rate.
As long as allocations and deallocations aren’t happening at a faster rate than
the defragmentation shifts, the heap will remain mostly defragmented at all
times.
This approach is only valid when the size of each block is relatively small,
so that the time required to move a single block does not exceed the time al-
lotted to relocation each frame. If very large blocks need to be relocated, we
can often break them up into two or more subblocks, each of which can be re-
located independently. This hasn’t proved to be a problem in Naughty Dog’s
engine, because relocation is only used for dynamic game objects, and they are
never larger than a few kibibytes—and usually much smaller.

## 6.3 Containers

Game programmers employ a wide variety of collection-oriented data struc-
tures, also known as containers or collections. The job of a container is always
the same—to house and manage zero or more data elements; however, the de-
tails of how they do this vary greatly, and each type of container has its pros
and cons. Common container data types include, but are certainly not limited


<!-- source-pdf-page: 461 -->

to, the following.

•
Array. An ordered, contiguous collection of elements accessed by in-
dex. The length of the array is usually statically defined at compile time.
It may be multidimensional. C and C++ support these natively (e.g.,
int a[5]).

•
Dynamic array. An array whose length can change dynamically at run-
time (e.g., the C++ standard library’s std::vector).

•
Linked list. An ordered collection of elements not stored contiguously
in memory but rather linked to one another via pointers (e.g., the C++
standard library’s std::list).

•
Stack.
A container that supports the last-in-first-out (LIFO) model
for adding and removing elements, also known as push/pop (e.g.,
std::stack).

•
Queue. A container that supports the first-in-first-out (FIFO) model for
adding and removing elements (e.g., std::queue).

•
Deque. A double-ended queue—supports efficient insertion and removal
at both ends of the array (e.g., std::deque).

•
Tree. A container in which elements are grouped hierarchically. Each
element (node) has zero or one parent and zero or more children. A tree
is a special case of a DAG (see below).

•
Binary search tree (BST). A tree in which each node has at most two chil-
dren, with an order property to keep the nodes sorted by some well-
defined criteria. There are various kinds of binary search trees, including
red-black trees, splay trees, AVL trees, etc.

•
Binary heap. A binary tree that maintains itself in sorted order, much like
a binary search tree, via two rules: the shape property, which specifies
that the tree must be fully filled and that the last row of the tree is filled
from left to right; and the heap property, which states that every node
is, by some user-defined criterion, “greater than” or “equal to” all of its
children.

•
Priority queue. A container that permits elements to be added in any or-
der and then removed in an order defined by some property of the ele-
ments themselves (i.e., their priority). A priority queue is typically imple-
mented as a heap (e.g., std::priority_queue), but other implemen-
tations are possible. A priority queue is a bit like a list that stays sorted
at all times, except that a priority queue only supports retrieval of the
highest-priority element, and it is rarely implemented as a list under the
hood.


<!-- source-pdf-page: 462 -->

•
Dictionary. A table of key-value pairs. A value can be “looked up” ef-
ficiently given the corresponding key. A dictionary is also known as a
map or hash table, although technically a hash table is just one possible
implementation of a dictionary (e.g., std::map, std::hash_map).

•
Set. A container that guarantees that all elements are unique accord-
ing to some criteria. A set acts like a dictionary with only keys, but no
values.

•
Graph. A collection of nodes connected to one another by unidirectional
or bidirectional pathways in an arbitrary pattern.

•
Directed acyclic graph (DAG). A collection of nodes with unidirectional
(i.e., directed) interconnections, with no cycles (i.e., there is no nonempty
path that starts and ends on the same node).

### 6.3.1 Container Operations

Game engines that make use of container classes inevitably make use of vari-
ous commonplace algorithms as well. Some examples include:

•
Insert. Add a new element to the container. The new element might be
placed at the beginning of the list, or the end, or in some other location;
or the container might not have a notion of ordering at all.

•
Remove. Remove an element from the container; this may require a find
operation (see below). However, if an iterator is available that refers to
the desired element, it may be more efficient to remove the element using
the iterator.

•
Sequential access (iteration). Accessing each element of the container in
some “natural” predefined order.

•
Random access. Accessing elements in the container in an arbitrary order.

•
Find. Search a container for an element that meets a given criterion.
There are all sorts of variants on the find operation, including finding
in reverse, finding multiple elements, etc. In addition, different types of
data structures and different situations call for different algorithms (see
http://en.wikipedia.org/wiki/Search_algorithm).

•
Sort. Sort the contents of a container according to some given criteria.
There are many different sorting algorithms, including bubble sort, se-
lection sort, insertion sort, quicksort and so on. (See http://en.wikipedia.
org/wiki/Sorting_algorithm for details.)


<!-- source-pdf-page: 463 -->

### 6.3.2 Iterators

An iterator is a little class that “knows” how to efficiently visit the elements in
a particular kind of container. It acts like an array index or pointer—it refers to
one element in the container at a time, it can be advanced to the next element,
and it provides some sort of mechanism for testing whether or not all elements
in the container have been visited. As an example, the first of the following two
code snippets iterates over a C-style array using a pointer, while the second
iterates over a linked list using almost identical syntax.

void processArray(int container[], int numElements)
{
int* pBegin = &container[0];
int* pEnd = &container[numElements];

for (int* p = pBegin; p != pEnd; p++)
{
int element = *p;
// process element...
}
}

void processList(std::list<int>& container)
{
std::list<int>::iterator pBegin = container.begin();
std::list<int>::iterator pEnd = container.end();

for (auto p = pBegin; p != pEnd; ++p)
{
int element = *p;
// process element...
}
}

The key benefits to using an iterator over attempting to access the con-
tainer’s elements directly are as follows:

•
Direct access would break the container class’ encapsulation. An itera-
tor, on the other hand, is typically a friend of the container class, and as
such it can iterate efficiently without exposing any implementation de-
tails to the outside world. (In fact, most good container classes hide their
internal details and cannot be iterated over without an iterator.)

•
An iterator can simplify the process of iterating. Most iterators act like
array indices or pointers, so a simple loop can be written in which the


<!-- source-pdf-page: 464 -->

iterator is incremented and compared against a terminating condition—
even when the underlying data structure is arbitrarily complex. For ex-
ample, an iterator can make an in-order depth-first tree traversal look no
more complex than a simple array iteration.

6.3.2.1
Preincrement versus Postincrement

Notice in the processArray() example that we are using C++’s postincre-
ment operator, p++, rather than the preincrement operator, ++p. This is a sub-
tle but sometimes important optimization. The preincrement operator incre-
ments the contents of the variable before its (now modified) value is used in
the expression. The postincrement operator increments the contents of the
variable after it has been used. This means that writing ++p introduces a data
dependency into your code—the CPU must wait for the increment operation
to be completed before its value can be used in the expression. On a deeply
pipelined CPU, this introduces a stall. On the other hand, with p++ there is no
data dependency. The value of the variable can be used immediately, and the
increment operation can happen later or in parallel with its use. Either way,
no stall is introduced into the pipeline.
Of course, within the “update” expression of a for loop, there should be no
difference between pre- and postincrement. This is because any good compiler
will recognize that the value of the variable isn’t used in update_expr. But in
cases where the value is used, postincrement is preferable because it doesn’t
introduce a stall in the CPU’s pipeline.
It can be wise to make an exception to this little rule of thumb for classes
with overloaded increment operators, as is common practice in iterator classes.
By definition, the postincrement operator must return an unmodified copy of
the object on which it is called. Depending on the size and complexity of
the data members of the class, the added cost of copying the iterator can tip
the scales toward a preference for preincrement when using such classes in
performance-critical loops. (Preincrement isn’t necessarily better than postin-
crement in such a simple example as the function processList() shown
above, but I’ve implemented it with preincrement to highlight the difference.)

### 6.3.3 Algorithmic Complexity

The choice of which container type to use for a given application depends upon
the performance and memory characteristics of the container being consid-
ered. For each container type, we can determine the theoretical performance
of common operations such as insertion, removal, find and sort.
We usually indicate the amount of time T that an operation is expected to


<!-- source-pdf-page: 465 -->

take as a function of the number of elements n in the container:

T = f (n).

Rather than try to find the exact function f, we concern ourselves only with
finding the overall order of the function. For example, if the actual theoretical
function were any of the following,

T = 5n2 + 17,

T = 102n2 + 50n + 12,
T = 1

2n2,

we would, in all cases, simplify the expression down to its most relevant
term—in this case n2. To indicate that we are only stating the order of the func-
tion, not its exact equation, we use “big O” notation and write

T = O(n2).

The order of an algorithm can usually be determined via an inspection of
the pseudocode. If the algorithm’s execution time is not dependent upon the
number of elements in the container at all, we say it is O(1) (i.e., it completes
in constant time). If the algorithm performs a loop over the elements in the con-
tainer and visits each element once, such as in a linear search of an unsorted
list, we say the algorithm is O(n). If two loops are nested, each of which po-
tentially visits each node once, then we say the algorithm is O(n2). If a divide-
and-conquer approach is used, as in a binary search (where half of the list is
eliminated at each step), then we would expect that only ⌊log2(n) + 1⌋ele-
ments will actually be visited by the algorithm in the worst case, and hence
we refer to it as an O(log n) operation. If an algorithm executes a subalgo-
rithm n times, and the subalgorithm is O(log n), then the resulting algorithm
would be O(n log n).
To select an appropriate container class, we should look at the operations
that we expect to be most common, then select the container whose perfor-
mance characteristics for those operations are most favorable. The most com-
mon orders you’ll encounter are listed here from fastest to slowest: O(1),
O(log n), O(n), O(n log n), O(n2), O(nk) for k > 2.
We should also take the memory layout and usage characteristics of
our containers into account.
For example, an array (e.g., int a[5] or
std::vector) stores its elements contiguously in memory and requires no
overhead storage for anything other than the elements themselves. (Note that


<!-- source-pdf-page: 466 -->

a dynamic array does require a small fixed overhead.) On the other hand, a
linked list (e.g., std::list) wraps each element in a “link” data structure
that contains a pointer to the next element and possibly also a pointer to the
previous element, for a total of up to 16 bytes of overhead per element on a
64-bit machine. Also, the elements in a linked list need not be contiguous in
memory and often aren’t. A contiguous block of memory is usually much
more cache-friendly than a set of disparate memory blocks. Hence, for high-
speed algorithms, arrays are usually better than linked lists in terms of cache
performance (unless the nodes of the linked list are themselves allocated from
a small, contiguous block of memory). But a linked list is better for situations
in which speed of inserting and removing elements is of prime importance.

### 6.3.4 Building Custom Container Classes

Many game engines provide their own custom implementations of the com-
mon container data structures. This practice is especially prevalent in console
game engines and games targeted at mobile phone and PDA platforms. The
reasons for building these classes yourself include:

•
Total control. You control the data structure’s memory requirements, the
algorithms used, when and how memory is allocated, etc.
•
Opportunities for optimization. You can optimize your data structures and
algorithms to take advantage of hardware features specific to the con-
sole(s) you are targeting; or fine-tune them for a particular application
within your engine.
•
Customizability. You can provide custom algorithms not prevalent in
the C++ standard library or third-party libraries like Boost (for exam-
ple, searching for the n most relevant elements in a container, instead of
just the single most relevant).
•
Elimination of external dependencies. Since you built the software yourself,
you are not beholden to any other company or team to maintain it. If
problems arise, they can be debugged and fixed immediately rather than
waiting until the next release of the library (which might not be until after
you have shipped your game!)
•
Control over concurrent data structures. When you write your own con-
tainer classes, you have full control over the means by which they are
protected against concurrent access on a multithreaded or multicore sys-
tem. For example, on the PS4, Naughty Dog uses lightweight “spin lock”
mutexes for the majority of our concurrent data structures, because they
work well with our fiber-based job scheduling system. A third-party
container library might not have given us this kind of flexibility.


<!-- source-pdf-page: 467 -->

We cannot cover all possible data structures here, but let’s look at a few
common ways in which game engine programmers tend to tackle containers.

6.3.4.1
To Build or Not to Build

We will not discuss the details of how to implement all of these data types and
algorithms here—a plethora of books and online resources are available for
that purpose. However, we will concern ourselves with the question of where
to obtain implementations of the types and algorithms that you need. As game
engine designers, we have basically three choices:

1.
Build the needed data structures manually.
2.
Make use of the STL-style containers provided by the C++ standard li-
brary.
3.
Rely on a third-party library such as Boost (http://www.boost.org).

The C++ standard library and third-party libraries like Boost are attractive
options, because they provide a rich and powerful set of container classes cov-
ering pretty much every type of data structure imaginable. In addition, these
packages provide a powerful suite of template-based generic algorithms—impl-
ementations of common algorithms, such as finding an element in a container,
which can be applied to virtually any type of data object. However, these im-
plementations may not be appropriate for some kinds of game engines. Let’s
take a moment to investigate some of the pros and cons of each approach.

The C++ Standard Library

The benefits of the C++ standard library’s STL-style container classes include:

•
They offer a rich set of features.
•
Their implementations are robust and fully portable.

However, these container classes also have some drawbacks, including:

•
The header files are cryptic and difficult to understand (although the
documentation is quite good).
•
General-purpose container classes are often slower than a data structure
that has been crafted specifically to solve a particular problem.
•
A generic container may consume more memory than a custom-
designed data structure.
•
The C++ standard library does a lot of dynamic memory allocation, and
it’s sometimes challenging to control its appetite for memory in a way
that is suitable for high-performance, memory-limited console games.


<!-- source-pdf-page: 468 -->

•
The templated allocator system provided by the standard C++ library
isn’t flexible enough to allow these containers to be used with certain
kinds of memory allocators, such as stack-based allocators (see Section
6.2.1.1).

The Medal of Honor: Pacific Assault engine for the PC made heavy use of
what was known at the time as the standard template library (STL). And while
MOHPA did have its share of frame rate problems, the team was able to work
around the performance problems caused by STL (primarily by carefully lim-
iting and controlling its use). OGRE, the popular object-oriented rendering
library that we use for some of the examples in this book, also makes heavy
use of STL-style containers. However, at Naughty Dog we prohibit the use
of STL containers in runtime game code (although we do permit their use in
offline tools code). Your mileage may vary: Using the STL-style containers
provided by the C++ standard library on a game engine project is certainly
feasible, but it should be used with care.

Boost

The Boost project was started by members of the C++ Standards Committee
Library Working Group, but it is now an open source project with many con-
tributors from across the globe. The aim of the project is to produce libraries
that extend and work together with the standard C++ library, for both com-
mercial and non-commercial use. Many of the Boost libraries have already
been included in the C++ standard library as of C++11, and more components
are included in the Standards Committee’s Library Technical Report (TR2),
which is a step toward becoming part of a future C++ standard. Here is a brief
summary of what Boost brings to the table:

•
Boost provides a lot of useful facilities not available in the C++ standard
library.

•
In some cases, Boost provides alternatives or work-arounds for problems
with the design or implementation of some classes in the C++ standard
library.

•
Boost does a great job of handling some very complex problems, like
smart pointers. (Bear in mind that smart pointers are complex beasts,
and they can be performance hogs. Handles are usually preferable; see
Section 16.5 for details.)

•
The Boost libraries’ documentation is usually excellent. Not only does
the documentation explain what each library does and how to use it, but


<!-- source-pdf-page: 469 -->

in most cases it also provides an excellent in-depth discussion of the de-
sign decisions, constraints and requirements that went into constructing
the library. As such, reading the Boost documentation is a great way to
learn about the principles of software design.

If you are already using the C++ standard library, then Boost can serve as
an excellent extension and/or alternative to many of its features. However, be
aware of the following caveats:

•
Most of the core Boost classes are templates, so all that one needs in order
to use them is the appropriate set of header files. However, some of the
Boost libraries build into rather large .lib files and may not be feasible
for use in very small-scale game projects.

•
While the worldwide Boost community is an excellent support network,
the Boost libraries come with no guarantees. If you encounter a bug, it
will ultimately be your team’s responsibility to work around it or fix it.

•
The Boost libraries are distributed under the Boost Software License.
Read the license information (http://www.boost.org/more/license_
info.html) carefully to be sure it is right for your engine.

Folly

Folly is an open source library developed by Andrei Alexandrescu and the
engineers at Facebook. Its goal is to extend the standard C++ library and the
Boost library (rather than to compete with these libraries), with an emphasis
on ease of use and the development of high-performance software. You can
read about it by searching online for the article entitled “Folly: The Facebook
Open Source Library” which is hosted at https://www.facebook.com/. The
library itself is available on GitHub here: https://github.com/facebook/folly.

Loki

There is a rather esoteric branch of C++ programming known as template meta-
programming. The core idea is to use the compiler to do a lot of the work that
would otherwise have to be done at runtime by exploiting the template feature
of C++ and in effect “tricking” the compiler into doing things it wasn’t orig-
inally designed to do. This can lead to some startlingly powerful and useful
programming tools.
By far the most well-known and probably most powerful template meta-
programming library for C++ is Loki, a library designed and written by Andrei


<!-- source-pdf-page: 470 -->

Alexandrescu (whose home page is at http://www.erdani.org). The library
can be obtained from SourceForge at http://loki-lib.sourceforge.net.
Loki is extremely powerful; it is a fascinating body of code to study and
learn from. However, its two big weaknesses are of a practical nature: (a)
its code can be daunting to read and use, much less truly understand, and (b)
some of its components are dependent upon exploiting “side-effect” behaviors
of the compiler that require careful customization in order to be made to work
on new compilers. So Loki can be somewhat tough to use, and it is not as
portable as some of its “less-extreme” counterparts. Loki is not for the faint of
heart. That said, some of Loki’s concepts such as policy-based programming can
be applied to any C++ project, even if you don’t use the Loki library per se. I
highly recommend that all software engineers read Andrei’s ground-breaking
book, Modern C++ Design [3], from which the Loki library was born.

### 6.3.5 Dynamic Arrays and Chunky Allocation

Fixed size C-style arrays are used quite a lot in game programming, because
they require no memory allocation, are contiguous and hence cache-friendly,
and support many common operations such as appending data and searching
very efficiently.
When the size of an array cannot be determined a priori, programmers tend
to turn either to linked lists or dynamic arrays. If we wish to maintain the per-
formance and memory characteristics of fixed-length arrays, then the dynamic
array is often the data structure of choice.
The easiest way to implement a dynamic array is to allocate an n-element
buffer initially and then grow the list only if an attempt is made to add more
than n elements to it. This gives us the favorable characteristics of a fixed
size array but with no upper bound. Growing is implemented by allocating
a new larger buffer, copying the data from the original buffer into the new
buffer, and then freeing the original buffer. The size of the buffer is increased
in some orderly manner, such as adding n to it on each grow, or doubling it
on each grow. Most of the implementations I’ve encountered never shrink the
array, only grow it (with the notable exception of clearing the array to zero
size, which might or might not free the buffer). Hence the size of the array
becomes a sort of “high water mark.” The std::vector class works in this
manner.
Of course, if you can establish a high water mark for your data, then you’re
probably better off just allocating a single buffer of that size when the engine
starts up. Growing a dynamic array can be incredibly costly due to reallocation
and data copying costs. The impact of these things depends on the sizes of the


<!-- source-pdf-page: 471 -->
> Visual fallback for diagrams/images: [PDF page 471](../../../visual_pages/page_0471.jpg)

buffers involved. Growing can also lead to fragmentation when discarded
buffers are freed. So, as with all data structures that allocate memory, caution
must be exercised when working with dynamic arrays. Dynamic arrays are
probably best used during development, when you are as yet unsure of the
buffer sizes you’ll require. They can always be converted into fixed size arrays
once suitable memory budgets have been established.

### 6.3.6 Dictionaries and Hash Tables

A dictionary is a table of key-value pairs. A value in the dictionary can be
looked up quickly, given its key. The keys and values can be of any data type.
This kind of data structure is usually implemented either as a binary search
tree or as a hash table.
In a binary tree implementation, the key-value pairs are stored in the nodes
of the binary tree, and the tree is maintained in key-sorted order. Looking up
a value by key involves performing an O(log n) binary search.
In a hash table implementation, the values are stored in a fixed size table,
where each slot in the table represents one or more keys. To insert a key-value
pair into a hash table, the key is first converted into integer form via a process
known as hashing (if it is not already an integer). Then an index into the hash
table is calculated by taking the hashed key modulo the size of the table. Finally,
the key-value pair is stored in the slot corresponding to that index. Recall that
the modulo operator (% in C/C++) finds the remainder of dividing the integer
key by the table size. So if the hash table has five slots, then a key of 3 would
be stored at index 3 (3 % 5 == 3), while a key of 6 would be stored at index
1 (6 % 5 == 1). Finding a key-value pair is an O(1) operation in the absence
of collisions.

6.3.6.1
Collisions: Open and Closed Hash Tables

Sometimes two or more keys end up occupying the same slot in the hash table.
This is known as a collision. There are two basic ways to resolve a collision,
giving rise to two different kinds of hash tables:

•
Open. In an open hash table (see Figure 6.8), collisions are resolved by
simply storing more than one key-value pair at each index, usually in the
form of a linked list. This approach is easy to implement and imposes
no upper bound on the number of key-value pairs that can be stored.
However, it does require memory to be allocated dynamically whenever
a new key-value pair is added to the table.
•
Closed. In a closed hash table (see Figure 6.9), collisions are resolved via
a process of probing until a vacant slot is found. (“Probing” means ap-


<!-- source-pdf-page: 472 -->
> Visual fallback for diagrams/images: [PDF page 472](../../../visual_pages/page_0472.jpg)

plying a well-defined algorithm to search for a free slot.) This approach
is a bit more difficult to implement, and it imposes an upper limit on
the number of key-value pairs that can reside in the table (because each
slot can hold only one key-value pair). But the main benefit of this kind
of hash table is that it uses up a fixed amount of memory and requires
no dynamic memory allocation. Therefore, it is often a good choice in a
console engine.

Confusingly, closed hash tables are sometimes said to use open addressing,
while open hash tables are said to use an addressing method known as chain-
ing, so named due to the linked lists at each slot in the table.

6.3.6.2
Hashing

Hashing is the process of turning a key of some arbitrary data type into an
integer, which can be used modulo the table size as an index into the table.
Mathematically, given a key k, we want to generate an integer hash value h
using the hash function H and then find the index i into the table as follows:

h = H(k),
i = h mod N,

where N is the number of slots in the table, and the symbol mod represents
the modulo operation, i.e., finding the remainder of the quotient h/N.
If the keys are unique integers, the hash function can be the identity func-
tion, H(k) = k. If the keys are unique 32-bit floating-point numbers, a hash
function might simply reinterpret the bit pattern of the 32-bit float as if it were
a 32-bit integer.

U32 hashFloat(float f)
{

Slot 0

(55, apple)
(0, orange)

Slot 1

(26, grape)

Slot 2

Slot 3

(33, plum)

Slot 4

Figure 6.8. An open hash table.


<!-- source-pdf-page: 473 -->
> Visual fallback for diagrams/images: [PDF page 473](../../../visual_pages/page_0473.jpg)

collision!

0

(55, apple)
(0, orange)

(55, apple)

0

1

(26, grape)

(26, grape)

1

2

2

(33, plum)

(33, plum)

3

3

probe to
find new
slot

(0, orange)

4

4

Figure 6.9. A closed hash table.

union
{
float m_asFloat;
U32
m_asU32;
} u;

u.m_asFloat = f;
return u.m_asU32;
}

If the key is a string, we can employ a string hashing function, which combines
the ASCII or UTF codes of all the characters in the string into a single 32-bit
integer value.
The quality of the hashing function H(k) is crucial to the efficiency of the
hash table. A “good” hashing function is one that distributes the set of all valid
keys evenly across the table, thereby minimizing the likelihood of collisions.
A hash function must also be reasonably quick to calculate, and deterministic
in the sense that it must produce the exact same output every time it is called
with an indentical input.
Strings are probably the most prevalent type of key you’ll encounter, so
it’s particularly helpful to know a “good” string hashing function. Table 6.1
lists a number of well-known hashing algorithms, their throughput ratings
(based on benchmark measurements and then converted into a rating of Low,
Medium or High) and their score on the SMHasher test (https://github.com/
aappleby/smhasher). Please note that the relative throughputs listed in the
table are for rough comparison purposes only. Many factors contribute to
the throughput of a hash function, including the hardware on which it is run
and the properties of the input data. Cryptographic hashes are deliberately
slow, as their focus is on producing a hash that is extremely unlikely to collide
with the hashes of other input strings, and for which the task of determining


<!-- source-pdf-page: 474 -->
> Visual fallback for diagrams/images: [PDF page 474](../../../visual_pages/page_0474.jpg)

Name
Throughput
Score
Cryptographic?

xxHash
High
10
No

MurmurHash 3a
High
10
No

SBox
Medium
9
No‡

Lookup3
Medium
9
No

CityHash64
Medium
10
No

CRC32
Low
9
No

MD5-32
Low
10
Yes

SHA1-32
Low
10
Yes

Table 6.1. Comparison of well-known hashing algorithms in terms of their relative throughput and
their scores on the SMHasher test. ‡Note that SBox is not itself a cryptographic hash, but it is one
component of the symmetric key algorithms used in cryptography.

a string that would produce a given hash value is extremely computationally
difficult.
For more information on hash functions, see the excellent article by Paul
Hsieh available at http://www.azillionmonkeys.com/qed/hash.html.

6.3.6.3
Implementing a Closed Hash Table

In a closed hash table, the key-value pairs are stored directly in the table, rather
than in a linked list at each table entry. This approach allows the programmer
to define a priori the exact amount of memory that will be used by the hash
table. A problem arises when we encounter a collision—two keys that end up
wanting to be stored in the same slot in the table. To address this, we use a
process known as probing.
The simplest approach is linear probing. Imagine that our hashing function
has yielded a table index of i, but that slot is already occupied; we simply try
slots (i + 1), (i + 2) and so on until an empty slot is found (wrapping around
to the start of the table when i = N). Another variation on linear probing is
to alternate searching forwards and backwards, (i + 1), (i −1), (i + 2), (i −2)
and so on, making sure to modulo the resulting indices into the valid range of
the table.
Linear probing tends to cause key-value pairs to “clump up.” To avoid
these clusters, we can use an algorithm known as quadratic probing. We start
at the occupied table index i and use the sequence of probes ij = (i ± j2) for
j = 1, 2, 3, . . . . In other words, we try (i + 12), (i −12), (i + 22), (i −22) and so


<!-- source-pdf-page: 475 -->

on, remembering to always modulo the resulting index into the valid range of
the table.
When using closed hashing, it is a good idea to make your table size a
prime number. Using a prime table size in conjunction with quadratic probing
tends to yield the best coverage of the available table slots with minimal clus-
tering. See http://stackoverflow.com/questions/1145217/why-should-hash
-functions-use-a-prime-number-modulus for a good discussion of why prime
hash table sizes are preferable.

6.3.6.4
Robin Hood Hashing

Robin Hood hashing is another probing method for closed hash tables that has
gained popularity recently. This probing scheme improves the performance of
a closed hash table, even when the table is nearly full. For a good discussion of
how Robin Hood hashing works, see https://www.sebastiansylvan.com/post
/robin-hood-hashing-should-be-your-default-hash-table-implementation/.

## 6.4 Strings

Strings are ubiquitous in almost every software project, and game engines are
no exception. On the surface, the string may seem like a simple, fundamental
data type. But, when you start using strings in your projects, you will quickly
discover a wide range of design issues and constraints, all of which must be
carefully accounted for.

### 6.4.1 The Problem with Strings

The most fundamental question about strings is how they should be stored and
managed in your program. In C and C++, strings aren’t even an atomic type—
they are implemented as arrays of characters. The variable length of strings
means we either have to hard-code limitations on the sizes of our strings, or
we need to dynamically allocate our string buffers.
Another big string-related problem is that of localization—the process of
adapting your software for release in other languages. This is also known as
internationalization, or I18N for short. Any string that you display to the user
in English must be translated into whatever languages you plan to support.
(Strings that are used internally to the program but are never displayed to the
user are exempt from localization, of course.) This not only involves making
sure that you can represent all the character glyphs of all the languages you
plan to support (via an appropriate set of fonts), but it also means ensuring
that your game can handle different text orientations. For example, traditional


<!-- source-pdf-page: 476 -->

Chinese text is oriented vertically instead of horizontally (although modern
Chinese and Japanese are commonly written horizontally and left-to-right),
and some languages like Hebrew read right-to-left. Your game also needs to
gracefully deal with the possibility that a translated string will be either much
longer or much shorter than its English counterpart.
Finally, it’s important to realize that strings are used internally within a
game engine for things like resource file names and object ids. For example,
when a game designer lays out a level, it’s highly convenient to permit him or
her to identify the objects in the level using meaningful names, like “Player-
Camera,” “enemy-tank-01” or “explosionTrigger.”
How our engine deals with these internal strings often has pervasive rami-
fications on the performance of the game. This is because strings are inherently
expensive to work with at runtime. Comparing or copying ints or floats
can be accomplished via simple machine language instructions. On the other
hand, comparing strings requires an O(n) scan of the character arrays using a
function like strcmp() (where n is the length of the string). Copying a string
requires an O(n) memory copy, not to mention the possibility of having to
dynamically allocate the memory for the copy. During one project I worked
on, we profiled our game’s performance only to discover that strcmp() and
strcpy() were the top two most expensive functions! By eliminating un-
necessary string operations and using some of the techniques outlined in this
section, we were able to all but eliminate these functions from our profile and
increase the game’s frame rate significantly. (I’ve heard similar stories from
developers at a number of different studios.)

### 6.4.2 String Classes

Many C++ programmers prefer to use a string class, such as the C++ standard
library’s std::string, rather than deal directly with character arrays. Such
classes can make working with strings much more convenient for the program-
mer. However, a string class can have hidden costs that are difficult to see
until the game is profiled. For example, passing a string to a function using a
C-style character array is fast because the address of the first character is typi-
cally passed in a hardware register. On the other hand, passing a string object
might incur the overhead of one or more copy constructors, if the function is
not declared or used properly. Copying strings might involve dynamic mem-
ory allocation, causing what looks like an innocuous function call to end up
costing literally thousands of machine cycles.
Because of the myriad issues with string classes, I generally prefer to avoid
them in runtime game code. However, if you feel a strong urge to use a string


<!-- source-pdf-page: 477 -->

class, make sure you pick or implement one that has acceptable runtime per-
formance characteristics—and be sure all programmers that use it are aware of
its costs. Know your string class: Does it treat all string buffers as read-only?
Does it utilize the copy on write optimization? (See http://en.wikipedia.org
/wiki/Copy-on-write.) In C++11, does it provide a move constructor? Does
it own the memory associated with the string, or can it reference memory that
it does not own? (See http://www.boost.org/doc/libs/1_57_0/libs/utility
/doc/html/string_ref.html for more on the issue of memory ownership in
string classes.) As a rule of thumb, always pass string objects by reference,
never by value (as the latter often incurs string-copying costs). Profile your
code early and often to ensure that your string class isn’t becoming a major
source of lost frame rate!
One situation in which a specialized string class does seem justifiable to me
is when storing and managing file system paths. Here, a hypothetical Path
class could add significant value over a raw C-style character array. For ex-
ample, it might provide functions for extracting the filename, file extension
or directory from the path. It might hide operating system differences by
automatically converting Windows-style backslashes to UNIX-style forward
slashes or some other operating system’s path separator. Writing a Path class
that provides this kind of functionality in a cross-platform way could be highly
valuable within a game engine context. (See Section 7.1.1.4 for more details on
this topic.)

### 6.4.3 Unique Identiﬁers

The objects in any virtual game world need to be uniquely identified in some
way.
For example, in Pac Man we might encounter game objects named
“pac_man,” “blinky,” “pinky,” “inky” and “clyde.” Unique object identifiers
allow game designers to keep track of the myriad objects that make up their
game worlds and also permit those objects to be found and operated on at
runtime by the engine. In addition, the assets from which our game objects
are constructed—meshes, materials, textures, audio clips, animations and so
on—all need unique identifiers as well.
Strings seem like a natural choice for such identifiers. Assets are often
stored in individual files on disk, so they can usually be identified uniquely
by their file paths, which of course are strings. And game objects are created
by game designers, so it is natural for them to assign their objects understand-
able string names, rather than have to remember integer object indices, or 64-
or 128-bit globally unique identifiers (GUIDs). However, the speed with which
comparisons between unique identifiers can be made is of paramount impor-


<!-- source-pdf-page: 478 -->

tance in a game, so strcmp() simply doesn’t cut it. We need a way to have
our cake and eat it too—a way to get all the descriptiveness and flexibility of
a string, but with the speed of an integer.

6.4.3.1
Hashed String Ids

One good solution is to hash our strings. As we’ve seen, a hash function maps
a string onto a semi-unique integer. String hash codes can be compared just
like any other integers, so comparisons are fast. If we store the actual strings
in a hash table, then the original string can always be recovered from the hash
code. This is useful for debugging purposes and to permit hashed strings to
be displayed on-screen or in log files. Game programmers sometimes use the
term string id to refer to such a hashed string. The Unreal engine uses the term
name instead (implemented by class FName).
As with any hashing system, collisions are a possibility (i.e., two different
strings might end up with the same hash code). However, with a suitable
hash function, we can all but guarantee that collisions will not occur for all
reasonable input strings we might use in our game. After all, a 32-bit hash
code represents more than four billion possible values. So, if our hash function
does a good job of distributing strings evenly throughout this very large range,
we are unlikely to collide. At Naughty Dog, we started out using a variant of
the CRC-32 algorithm to hash our strings, and we encountered only a handful
of collisions during many years of development on Uncharted and The Last of
Us. And when a collision did occur, fixing it was a simple matter of slightly
altering one of the strings (e.g., append a “2” or a “b” to one of the strings,
or use a totally different but synonymous string). That being said, Naughty
Dog has moved to a 64-bit hashing function for The Last of Us Part II and all of
our future game titles; this should essentially eliminate the possibility of hash
collisions, given the quantity and typical lengths of the strings we use in any
one game.

6.4.3.2
Some Implementation Ideas

Conceptually, it’s easy enough to run a hash function on your strings in order
to generate string ids. Practically speaking, however, it’s important to con-
sider when the hash will be calculated. Most game engines that use string ids
do the hashing at runtime. At Naughty Dog, we permit runtime hashing of
strings, but we also use C++11’s user-defined literals feature to transform the
syntax "any_string"_sid directly into a hashed integer value at compile
time. This permits string ids to be used anywhere that an integer manifest
constant can be used, including the constant case labels of a switch state-
ment. (The result of a function call that generates a string id at runtime is not


<!-- source-pdf-page: 479 -->

a constant, so it cannot be used as a case label.)
The process of generating a string id from a string is sometimes called
interning the string, because in addition to hashing it, the string is typically
also added to a global string table. This allows the original string to be re-
covered from the hash code later. You may also want your tools to be ca-
pable of hashing strings into string ids.
That way, when the tool gener-
ates data for consumption by your engine, the strings will already have been
hashed.
The main problem with interning a string is that it is a slow operation.
The hashing function must be run on the string, which can be an expensive
proposition, especially when a large number of strings are being interned. In
addition, memory must be allocated for the string, and it must be copied into
the lookup table. As a result (if you are not generating string ids at compile-
time), it is usually best to intern each string only once and save off the result for
later use. For example, it would be preferable to write code like this because
the latter implementation causes the strings to be unnecessarily re-interned
every time the function f() is called.

static StringId
sid_foo = internString("foo");
static StringId
sid_bar = internString("bar");

// ...

void f(StringId id)
{
if (id == sid_foo)
{
// handle case of id == "foo"
}
else if (id == sid_bar)
{
// handle case of id == "bar"
}
}

The following approach is less efficient:

void f(StringId id)
{
if (id == internString("foo"))
{
// handle case of id == "foo"
}


<!-- source-pdf-page: 480 -->

else if (id == internString("bar"))
{
// handle case of id == "bar"
}
}

Here’s one possible implementation of internString().

stringid.h

typedef U32 StringId;

extern StringId internString(const char* str);

stringid.cpp

static HashTable<StringId, const char*> gStringIdTable;

StringId internString(const char* str)
{
StringId sid = hashCrc32(str);

HashTable<StringId, const char*>::iterator it
= gStringIdTable.find(sid);

if (it == gStringTable.end())
{
// This string has not yet been added to the
// table. Add it, being sure to copy it in case
// the original was dynamically allocated and
// might later be freed.
gStringTable[sid] = strdup(str);
}

return sid;
}

Another idea employed by the Unreal Engine is to wrap the string id
and a pointer to the corresponding C-style character array in a tiny class.
In the Unreal Engine, this class is called FName.
At Naughty Dog we
do the same and wrap our string ids in a StringId class.
We define
a macro so that SID("any_string") produces an instance of this class,
with its hashed value produced by our user-defined string literal syntax
"any_string"_sid.


<!-- source-pdf-page: 481 -->

Using Debug Memory for Strings

When using string ids, the strings themselves are only kept around for human
consumption. When you ship your game, you almost certainly won’t need the
strings—the game itself should only ever use the ids. As such, it’s a good idea
to store your string table in a region of memory that won’t exist in the retail
game. For example, a PS3 development kit has 256 MiB of retail memory, plus
an additional 256 MiB of “debug” memory that is not available on a retail unit.
If we store our strings in debug memory, we needn’t worry about their impact
on the memory footprint of the final shipping game. (We just need to be careful
never to write production code that depends on the strings being available!)

### 6.4.4 Localization

Localization of a game (or any software project) is a big undertaking. It is a
task best handled by planning for it from day one and accounting for it at every
step of development. However, this is not done as often as we all would like.
Here are some tips that should help you plan your game engine project for
localization. For an in-depth treatment of software localization, see [34].

6.4.4.1
Unicode

The problem for most English-speaking software developers is that they are
trained from birth (or thereabouts!) to think of strings as arrays of eight-bit
ASCII character codes (i.e., characters following the ANSI standard). ANSI
strings work great for a language with a simple alphabet, like English. But,
they just don’t cut it for languages with complex alphabets containing a great
many more characters, sometimes totally different glyphs than English’s 26
letters. To address the limitations of the ANSI standard, the Unicode character
set system was devised.
The basic idea behind Unicode is to assign every character or glyph from
every language in common use around the globe to a unique hexadecimal code
known as a code point. When storing a string of characters in memory, we se-
lect a particular encoding—a specific means of representing the Unicode code
points for each character—and following those rules, we lay down a sequence
of bits in memory that represent the string. UTF-8 and UTF-16 are two com-
mon encodings. You should select the specific encoding standard that best
suits your needs.
Please set down this book right now and read the article entitled, “The
Absolute Minimum Every Software Developer Absolutely, Positively Must
Know About Unicode and Character Sets (No Excuses!)” by Joel Spolsky.
You can find it here: http://www.joelonsoftware.com/articles/Unicode.html.
(Once you’ve done that, please pick up the book again!)


<!-- source-pdf-page: 482 -->

UTF-32

The simplest Unicode encoding is UTF-32. In this encoding, each Unicode
code point is encoded into a 32-bit (4-byte) value. This encoding wastes a lot
of space, for two reasons: First, most strings in Western European languages
do not use any of the highest-valued code points, so an average of at least
16 bits (2 bytes) is usually wasted per character. Second, the highest Unicode
code point is 0x10FFFF, so even if we wanted to create a string that uses every
possible Unicode glyph, we’d still only need 21 bits per character, not 32.
That said, UTF-32 does have simplicity in its favor. It is a fixed-length encod-
ing, meaning that every character occupies the same number of bits in mem-
ory (32 bits to be precise). As such, we can determine the length of any UTF-32
string by taking its length in bytes and dividing by four.

UTF-8

In the UTF-8 encoding scheme, the code points for each character in a string
are stored using eight-bit (one-byte) granularity, but some code points occupy
more than one byte. Hence the number of bytes occupied by a UTF-8 character
string is not necessarily the length of the string in characters. This is known
as a variable-length encoding, or a multibyte character set (MBCS), because each
character in a string may take one or more bytes of storage.
One of the big benefits of the UTF-8 encoding is that it is backwards-
compatible with the ANSI encoding. This works because the first 127 Unicode
code points correspond numerically to the old ANSI character codes. This
means that every ANSI character will be represented by exactly one byte in
UTF-8, and a string of ANSI characters can be interpreted as a UTF-8 string
without modification.
To represent higher-valued code points, the UTF-8 standard uses multi-
byte characters.
Each multibyte character starts with a byte whose most-
significant bit is 1 (i.e., its value lies in the range 128–255, inclusive). Such
high-valued bytes will never appear in an ANSI string, so there is no am-
biguity when distinguishing between single-byte characters and multibyte
characters.

UTF-16

The UTF-16 encoding employs a somewhat simpler, albeit more expensive ap-
proach. Each character in a UTF-16 string is represented by either one or two
16-bit values. The UTF-16 encoding is known as a wide character set (WCS)
because each character is at least 16 bits wide, instead of the eight bits used by


<!-- source-pdf-page: 483 -->

“regular” ANSI chars and their UTF-8 counterparts.
In UTF-16, the set of all possible Unicode code points is divided into 17
planes containing 216 code points each. The first plane is known as the basic mul-
tilingual plane (BMP). It contains the most commonly used code points across
a wide range of languages. As such, many UTF-16 strings can be represented
entirely by code points within the first plane, meaning that each character in
such a string is represented by only one 16-bit value. However, if a character
from one of the other planes (known as supplementary planes) is required in a
string, it is represented by two consecutive 16-bit values.
The UCS-2 (2-byte universal character set) encoding is a limited subset
of the UTF-16 encoding, utilizing only the basic multilingual page. As such,
it cannot represent characters whose Unicode code points are numerically
higher than 0xFFFF. This simplifies the format, because every character is
guaranteed to occupy exactly 16 bits (two bytes).
In other words, UCS-2
is a fixed-length character encoding, while in general UTF-8 and UTF-16 are
variable-length encodings.
If we know a priori that a UTF-16 string only utilizes code points from the
BMP (or if we are dealing with a UCS-2 encoded string), we can determine the
number of characters in the string by simply dividing the number of bytes by
two. Of course, if supplemental planes are used in a UTF-16 string, this simple
“trick” no longer works.
Note that a UTF-16 encoding can be little-endian or big-endian (see Section
3.3.2.1), depending on the native endianness of your target CPU. When storing
UTF-16 text on-disc, it’s common to precede the text data with a byte order mark
(BOM) indicating whether the individual 16-bit characters are stored in little-
or big-endian format. (This is true of UTF-32 encoded string data as well, of
course.)

6.4.4.2
char versus wchar_t

The standard C/C++ library defines two data types for dealing with character
strings—char and wchar_t. The char type is intended for use with legacy
ANSI strings and with multibyte character sets (MBCS), including (but not
limited to) UTF-8. The wchar_t type is a “wide” character type, intended to
be capable of representing any valid code point in a single integer. As such,
its size is compiler- and system-specific. It could be eight bits on a system
that does not support Unicode at all. It could be 16 bits if the UCS-2 encoding
is assumed for all wide characters, or if a multi-word encoding like UTF-16
is being employed. Or it could be 32 bits if UTF-32 is the “wide” character
encoding of choice.
Because of this inherent ambiguity in the definition of wchar_t, if you


<!-- source-pdf-page: 484 -->
> Visual fallback for diagrams/images: [PDF page 484](../../../visual_pages/page_0484.jpg)

need to write truly portable string-handling code, you’ll need to define your
own character data type(s) and provide a library of functions for dealing with
whatever Unicode encoding(s) you need to support. However, if you are tar-
geting a specific platform and compiler, you can write your code within the
limits of that particular implementation, at the loss of some portability.
The following article does a good job of outlining the pros and cons of
using the wchar_t data type: http://icu-project.org/docs/papers/unicode_
wchar_t.html.

6.4.4.3
Unicode under Windows

Under Windows, the wchar_t data type is used exclusively for UTF-16 en-
coded Unicode strings, and the char type is used for ANSI strings and legacy
Windows code page string encodings. When reading the Windows API docs,
the term “Unicode” is therefore always synonymous with “wide character set”
(WCS) and UTF-16 encoding. This is a bit confusing, because of course Uni-
code strings can in general be encoded in the “non-wide” multibyte UTF-8
format.
The Windows API defines three sets of character/string manipulation
functions: one set for single-byte character set ANSI strings (SBCS), one set
for multibyte character set (MBCS) strings, and one set for wide character set
strings. The ANSI functions are essentially the old-school “C-style” string
functions we all grew up with. The MBCS string functions handle a variety
of multibyte encodings and are primarily designed for dealing with legacy
Windows code pages encodings. The WCS functions handle Unicode UTF-16
strings.
Throughout the Windows API, a prefix or suffix of “w,” “wcs” or “W” in-
dicates a wide character set (UTF-16) encoding; a prefix or suffix of “mb” indi-
cates a multibyte encoding; and a prefix or suffix of “a” or “A,” or the lack of
any prefix or suffix, indicates an ANSI or Windows code pages encoding. The
C++ standard library uses a similar convention—for example, std::string
is its ANSI string class, while std::wstring is its wide character equivalent.
Unfortunately, the names of the functions aren’t always 100% consistent. This
all leads to some confusion among programmers who aren’t in the know. (But
you aren’t one of those programmers!) Table 6.2 lists some examples.
Windows also provides functions for translating between ANSI char-
acter strings, multibyte strings and wide UTF-16 strings.
For example,
wcstombs() converts a wide UTF-16 string into a multibyte string accord-
ing to the currently active locale setting.
The Windows API uses a little preprocessor trick to allow you to write code
that is at least superficially portable between wide (Unicode) and non-wide


<!-- source-pdf-page: 485 -->
> Visual fallback for diagrams/images: [PDF page 485](../../../visual_pages/page_0485.jpg)

ANSI
WCS
MBCS
strcmp()
wcscmp()
_mbscmp()
strcpy()
wcscpy()
_mbscpy()
strlen()
wcslen()
_mbstrlen()

Table 6.2. Variants of some common C standard library string functions for use with ANSI, wide
and multibyte character sets.

(ANSI/MBCS) string encodings. The generic character data type TCHAR is
defined to be a typedef to char when building your application in “ANSI
mode,” and it’s defined to be a typedef to wchar_t when building your ap-
plication in “Unicode mode.” The macro _T() is used to convert an eight-bit
string literal (e.g., char* s = "this is a string";) into a wide string
literal (e.g., wchar_t* s = L"this is a string";) when compiling in
“Unicode mode.” Likewise, a suite of “fake” API functions are provided that
“automagically” morph into their appropriate 8-bit or 16-bit variant, depend-
ing on whether you are building in “Unicode mode” or not. These magic
character-set-independent functions are either named with no prefix or suf-
fix, or with a “t,” “tcs” or “T” prefix or suffix.
Complete documentation for all of these functions can be found on Mi-
crosoft’s MSDN website. Here’s a link to the documentation for strcmp()
and its ilk, from which you can quite easily navigate to the other related string-
manipulation functions using the tree view on the left-hand side of the page, or
via the search bar: http://msdn2.microsoft.com/en-us/library/kk6xf663(VS.
80).aspx.

6.4.4.4
Unicode on Consoles

The Xbox 360 software development kit (XDK) uses WCS strings pretty much
exclusively, for all strings—even for internal strings like file paths. This is cer-
tainly one valid approach to the localization problem, and it makes for very
consistent string handling throughout the XDK. However, the UTF-16 encod-
ing is a bit wasteful on memory, so different game engines may employ differ-
ent conventions. At Naughty Dog, we use eight-bit char strings throughout
our engine, and we handle foreign languages via a UTF-8 encoding. The choice
of encoding is not particularly important, as long as you select one as early in
the project as possible and stick with it consistently.

6.4.4.5
Other Localization Concerns

Even once you have adapted your software to use Unicode characters, there
is still a host of other localization problems to contend with. For one thing,


<!-- source-pdf-page: 486 -->
> Visual fallback for diagrams/images: [PDF page 486](../../../visual_pages/page_0486.jpg)

Id
English
French
p1score
“Player 1 Score”
“Joueur 1 Score”
p2score
“Player 2 Score”
“Joueur 2 Score”
p1wins
“Player one wins!”
“Joueur un gagne!”
p2wins
“Player two wins!”
“Joueur deux gagne!”

Table 6.3. Example of a string database used for localization.

strings aren’t the only place where localization issues arise. Audio clips in-
cluding recorded voices must be translated. Textures may have English words
painted into them that require translation.
Many symbols have different
meanings in different cultures. Even something as innocuous as a no-smoking
sign might be misinterpreted in another culture. In addition, some markets
draw the boundaries between the various game-rating levels differently. For
example, in Japan a Teen-rated game is not permitted to show blood of any
kind, whereas in North America small red blood spatters are considered ac-
ceptable.
For strings, there are other details to worry about as well. You will need to
manage a database of all human-readable strings in your game, so that they
can all be reliably translated. The software must display the proper language
given the user’s installation settings. The formatting of the strings may be to-
tally different in different languages—for example, Chinese is sometimes writ-
ten vertically, and Hebrew reads right-to-left. The lengths of the strings will
vary greatly from language to language. You’ll also need to decide whether to
ship a single DVD or Blu-ray disc that contains all languages or ship different
discs for particular territories.
The most crucial components in your localization system will be the cen-
tral database of human-readable strings and an in-game system for looking up
those strings by id. For example, let’s say you want a heads-up display that
lists the score of each player with “Player 1 Score:” and “Player 2 Score:” labels
and that also displays the text “Player 1 Wins” or “Player 2 Wins” at the end
of a round. These four strings would be stored in the localization database un-
der unique ids that are understandable to you, the developer of the game. So
our database might use the ids “p1score,” “p2score,” “p1wins” and “p2wins,”
respectively. Once our game’s strings have been translated into French, our
database would look something like the simple example shown in Table 6.3.
Additional columns can be added for each new language your game supports.
The exact format of this database is up to you. It might be as simple as
a Microsoft Excel worksheet that can be saved as a comma-separated values
(CSV) file and parsed by the game engine or as complex as a full-fledged Or-


<!-- source-pdf-page: 487 -->

acle database. The specifics of the string database are largely unimportant to
the game engine, as long as it can read in the string ids and the correspond-
ing Unicode strings for whatever language(s) your game supports. (However,
the specifics of the database may be very important from a practical point of
view, depending upon the organizational structure of your game studio. A
small studio with in-house translators can probably get away with an Excel
spreadsheet located on a network drive. But a large studio with branch offices
in Britain, Europe, South America and Japan would probably find some kind
of distributed database a great deal more amenable.)
At runtime, you’ll need to provide a simple function that returns the Uni-
code string in the “current” language, given the unique id of that string. The
function might be declared like this:

wchar_t getLocalizedString(const char* id);

and it might be used like this:

void drawScoreHud(const Vector3& score1Pos,
const Vector3& score2Pos)
{
renderer.displayTextOrtho(getLocalizedString("p1score"),
score1Pos);

renderer.displayTextOrtho(getLocalizedString("p2score"),
score2Pos);

// ...
}

Of course, you’ll need some way to set the “current” language globally. This
might be done via a configuration setting, which is fixed during the installa-
tion of the game. Or you might allow users to change the current language on
the fly via an in-game menu. Either way, the setting is not difficult to imple-
ment; it can be as simple as a global integer variable specifying the index of
the column in the string table from which to read (e.g., column one might be
English, column two French, column three Spanish and so on).
Once you have this infrastructure in place, your programmers must re-
member to never display a raw string to the user. They must always use the id of
a string in the database and call the look-up function in order to retrieve the
string in question.

6.4.4.6
Case Study: Naughty Dog’s Localization Tool

At Naughty Dog, we use a localization database that we developed in-house.
The localization tool’s back end consists of a MySQL database located on a


<!-- source-pdf-page: 488 -->
> Visual fallback for diagrams/images: [PDF page 488](../../../visual_pages/page_0488.jpg)

Figure 6.10. Naughty Dog’s localization tool’s main window, showing a list of pure text assets used
in the menus and HUD. The user has just performed a search for an asset called MENU_NEWGAME.

Figure 6.11. Detailed asset view, showing the MENU_NEWGAME string.


<!-- source-pdf-page: 489 -->
> Visual fallback for diagrams/images: [PDF page 489](../../../visual_pages/page_0489.jpg)

server that is accessible both to the developers within Naughty Dog and also
to the various external companies with which we work to translate our text
and speech audio clips into the various languages our games support. The
front end is a web interface that “speaks” to the database, allowing users to
view all of the text and audio assets, edit their contents, provide translations
for each asset, search for assets by id or by content and so on.
In Naughty Dog’s localization tool, each asset is either a string (for use in
the menus or HUD) or a speech audio clip with optional subtitle text (for use
as in-game dialog or within cinematics). Each asset has a unique identifier,
which is represented as a hashed string id (see Section 6.4.3.1). If a string is
required for use in the menus or HUD, we look it up by its id and get back a
Unicode (UTF-8) string suitable for display on-screen. If a line of dialog must
be played, we likewise look up the audio clip by its id and use the data in-
engine to look up its corresponding subtitle (if any). The subtitle is treated
just like a menu or HUD string, in that it is returned by the localization tool’s
API as a UTF-8 string suitable for display.
Figure 6.10 shows the main interface of the localization tool, in this case
displayed in the Chrome web browser. In this image, you can see that the
user has typed in the id MENU_NEWGAME in order to look up the string “NEW
GAME” (used on the game’s main menu for launching a new game). Fig-
ure 6.11 shows the detailed view of the MENU_NEWGAME asset. If the user
hits the “Text Translations” button in the upper-left corner of the asset de-
tails window, the screen shown in Figure 6.12 comes up, allowing the user
to enter or edit the various translations of the string. Figure 6.13 shows an-
other tab on the localization tool’s main page, this time listing audio speech
assets. Finally, Figure 6.14 depicts the detailed asset view for the speech as-
set BADA_GAM_MIL_ESCAPE_OVERPASS_001 (“We missed all the action”),
showing translations of this line of dialog into some of the supported lan-
guages.

## 6.5 Engine Conﬁguration

Game engines are complex beasts, and they invariably end up having a large
number of configurable options. Some of these options are exposed to the
player via one or more options menus in-game. For example, a game might
expose options related to graphics quality, the volume of music and sound
effects, or controller configuration. Other options are created for the benefit
of the game development team only and are either hidden or stripped out
of the game completely before it ships. For example, the player character’s


<!-- source-pdf-page: 490 -->
> Visual fallback for diagrams/images: [PDF page 490](../../../visual_pages/page_0490.jpg)

Figure 6.12. Text translations of the string “NEW GAME” into all languages supported by Naughty
Dog’s The Last of Us.

maximum walk speed might be exposed as an option so that it can be fine-
tuned during development, but it might be changed to a hard-coded value
prior to ship.

### 6.5.1 Loading and Saving Options

A configurable option can be implemented trivially as a global variable or a
member variable of a singleton class. However, configurable options are not
particularly useful unless their values can be configured, stored on a hard disk,
memory card or other storage medium, and later retrieved by the game. There
are a number of simple ways to load and save configuration options:

•
Text configuration files. By far the most common method of saving and
loading configuration options is by placing them into one or more text


<!-- source-pdf-page: 491 -->
> Visual fallback for diagrams/images: [PDF page 491](../../../visual_pages/page_0491.jpg)

Figure 6.13. Naughty Dog’s localization tool’s main window again, this time showing a list of speech
audio assets with accompanying subtitle text.

Figure 6.14.
Detailed asset view showing recorded translations for the speech asset
BADA_GAM_MIL_ESCAPE_OVERPASS_001 (“We missed all the action”).


<!-- source-pdf-page: 492 -->

files. The format of these files varies widely from engine to engine, but it
is usually very simple. For example, Windows INI files (which are used
by the OGRE renderer) consist of flat lists of key-value pairs grouped
into logical sections. The JSON format is another common choice for
configurable game options files. XML is another viable option, although
most developers these days find JSON to be less verbose and easier to
read than XML.

•
Compressed binary files. Most modern consoles have hard disk drives in
them, but older consoles could not afford this luxury. As a result, all
game consoles since the Super Nintendo Entertainment System (SNES)
have come equipped with proprietary removable memory cards that
permit both reading and writing of data. Game options are sometimes
stored on these cards, along with saved games. Compressed binary files
are the format of choice on a memory card, because the storage space
available on these cards is often very limited.

•
The Windows registry. The Microsoft Windows operating system pro-
vides a global options database known as the registry. It is stored as
a tree, where the interior nodes (known as registry keys) act like file
folders, and the leaf nodes store the individual options as key-value
pairs. That being said, I don’t recommend using the Windows registry
for storing engine configuration information. The registry is a mono-
lithic database that can easily be corrupted, lost (when Windows is rein-
stalled), or thrown out-of-sync with the files in the filesystem. For more
on the weaknesses of the Windows registry, see https://blog.coding
horror.com/was-the-windows-registry-a-good-idea/.

•
Command line options. The command line can be scanned for option set-
tings. The engine might provide a mechanism for controlling any option
in the game via the command line, or it might expose only a small subset
of the game’s options here.

•
Environment variables. On personal computers running Windows, Linux
or MacOS, environment variables are sometimes used to store configu-
ration options as well.

•
Online user profiles. With the advent of online gaming communities like
Xbox Live, each user can create a profile and use it to save achievements,
purchased and unlockable game features, game options and other infor-
mation. The data are stored on a central server and can be accessed by
the player wherever an Internet connection is available.


<!-- source-pdf-page: 493 -->

### 6.5.2 Per-User Options

Most game engines differentiate between global options and per-user options.
This is necessary because most games allow each player to configure the game
to his or her liking. It is also a useful concept during development of the game,
because it allows each programmer, artist and designer to customize his or her
work environment without affecting other team members.
Obviously care must be taken to store per-user options in such a way that
each player “sees” only his or her options and not the options of other play-
ers on the same computer or console. In a console game, the user is typically
allowed to save his or her progress, along with per-user options such as con-
troller preferences, in “slots” on a memory card or hard disk. These slots are
usually implemented as files on the media in question.
On a Windows machine, each user has a folder under C:\Users contain-
ing information such as the user’s desktop, his or her My Documents folder,
his or her Internet browsing history and temporary files and so on. A hid-
den subfolder named AppData is used to store per-user information on a per-
application basis; each application creates a folder under AppData and can use
it to store whatever per-user information it requires.
Windows games sometimes store per-user configuration data in the reg-
istry. The registry is arranged as a tree, and one of the top-level children of the
root node, called HKEY_CURRENT_USER, stores settings for whichever user
happens to be logged on. Every user has his or her own subtree in the registry
(stored under the top-level subtree HKEY_USERS), and HKEY_CURRENT_USER
is really just an alias to the current user’s subtree. So games and other ap-
plications can manage per-user configuration options by simply reading and
writing them to keys under the HKEY_CURRENT_USER subtree.

### 6.5.3 Conﬁguration Management in Some Real Engines

In this section, we’ll take a brief look at how some real game engines manage
their configuration options.

6.5.3.1
Example: Quake’s Cvars

The Quake family of engines uses a configuration management system known
as console variables, or cvars for short. A cvar is just a floating-point or string
global variable whose value can be inspected and modified from within
Quake’s in-game console. The values of some cvars can be saved to disk and
later reloaded by the engine.
At runtime, cvars are stored in a global linked list. Each cvar is a dy-
namically allocated instance of struct cvar_t, which contains the variable’s


<!-- source-pdf-page: 494 -->

name, its value as a string or float, a set of flag bits, and a pointer to the next
cvar in the linked list of all cvars. Cvars are accessed by calling Cvar_Get(),
which creates the variable if it doesn’t already exist and modified by calling
Cvar_Set(). One of the bit flags, CVAR_ARCHIVE, controls whether or not
the cvar will be saved into a configuration file called config.cfg. If this flag is
set, the value of the cvar will persist across multiple runs of the game.

6.5.3.2
Example: OGRE

The OGRE rendering engine uses a collection of text files in Windows INI for-
mat for its configuration options. By default, the options are stored in three
files, each of which is located in the same folder as the executable program:

•
plugins.cfg contains options specifying which optional engine plug-ins
are enabled and where to find them on disk.
•
resources.cfg contains a search path specifying where game assets (a.k.a.
media, a.k.a. resources) can be found.
•
ogre.cfg contains a rich set of options specifying which renderer (DirectX
or OpenGL) to use and the preferred video mode, screen size, etc.

Out of the box, OGRE provides no mechanism for storing per-user con-
figuration options. However, the OGRE source code is freely available, so
it would be quite easy to change it to search for its configuration files in the
user’s home directory instead of in the folder containing the executable. The
Ogre::ConfigFile class makes it easy to write code that reads and writes
brand new configuration files as well.

6.5.3.3
Example: The Uncharted and The Last of Us Series

Naughty Dog’s engine makes use of a number of configuration mechanisms.

In-Game Menu Settings

The Naughty Dog engine supports a powerful in-game menu system, allow-
ing developers to control global configuration options and invoke commands.
The data types of the configurable options must be relatively simple (primar-
ily Boolean, integer and floating-point variables), but this limitation has not
prevented the developers at Naughty Dog from creating literally hundreds of
useful menu-driven options.
Each configuration option is implemented as a global variable, or a mem-
ber of a singleton struct or class. When the menu option that controls an op-
tion is created, the address of the variable is provided, and the menu item
directly controls its value. As an example, the following function creates a


<!-- source-pdf-page: 495 -->

submenu item containing some options for Naughty Dog’s rail vehicles (sim-
ple vehicles that ride on splines which have been used in pretty much every
Naughty Dog game, from the “Out of the Frying Pan” jeep chase level in Un-
charted: Drake’s Fortune to the truck convoy / jeep chase sequence in Uncharted
4). It defines menu items controlling three global variables: two Booleans
and one floating-point value. The items are collected onto a menu, and a
special item is returned that will bring up the menu when selected. Presum-
ably the code calling this function adds this item to the parent menu that it is
building.

DMENU::ItemSubmenu * CreateRailVehicleMenu()
{
extern bool g_railVehicleDebugDraw2D;
extern bool g_railVehicleDebugDrawCameraGoals;
extern float g_railVehicleFlameProbability;

DMENU::Menu * pMenu
= new DMENU::Menu("RailVehicle");

pMenu->PushBackItem(
new DMENU::ItemBool("Draw 2D Spring Graphs",
DMENU::ToggleBool,
&g_railVehicleDebugDraw2D));

pMenu->PushBackItem(
new DMENU::ItemBool("Draw Goals (Untracked)",
DMENU::ToggleBool,
&g_railVehicleDebugDrawCameraGoals));

DMENU::ItemFloat * pItemFloat;
pItemFloat = new DMENU::ItemFloat(
"FlameProbability",
DMENU::EditFloat, 5, "%5.2f",
&g_railVehicleFlameProbability);

pItemFloat->SetRangeAndStep(0.0f, 1.0f, 0.1f, 0.01f);
pMenu->PushBackItem(pItemFloat);

DMENU::ItemSubmenu * pSubmenuItem;
pSubmenuItem = new DMENU::ItemSubmenu(
"RailVehicle...", pMenu);

return pSubmenuItem;
}

The value of any option can be saved by simply marking it with the cir-
cle button on the Dualshock joypad when the corresponding menu item is se-


<!-- source-pdf-page: 496 -->

lected. The menu settings are saved in an INI-style text file, allowing the saved
global variables to retain the values across multiple runs of the game. The abil-
ity to control which options are saved on a per-menu-item basis is highly useful,
because any option that is not saved will take on its programmer-specified de-
fault value. If a programmer changes a default, all users will “see” the new
value, unless of course a user has saved a custom value for that particular op-
tion.

Command Line Arguments

The Naughty Dog engine scans the command line for a predefined set of spe-
cial options. The name of the level to load can be specified, along with a num-
ber of other commonly used arguments.

Scheme Data Deﬁnitions

The vast majority of engine and game configuration information in the
Naughty Dog engine (used to produce the Uncharted and The Last of Us se-
ries) is specified using a Lisp-like language called Scheme. Using a propri-
etary data compiler, data structures defined in the Scheme language are trans-
formed into binary files that can be loaded by the engine. The data compiler
also spits out header files containing C struct declarations for every data
type defined in Scheme. These header files allow the engine to properly in-
terpret the data contained in the loaded binary files. The binary files can even
be recompiled and reloaded on the fly, allowing developers to alter the data
in Scheme and see the effects of their changes immediately (as long as data
members are not added or removed, as that would require a recompile of the
engine).
The following example illustrates the creation of a data structure specifying
the properties of an animation. It then exports three unique animations to
the game. You may have never read Scheme code before, but for this relatively
simple example it should be pretty self-explanatory. One oddity you’ll notice
is that hyphens are permitted within Scheme symbols, so simple-animation
is a single symbol (unlike in C/C++ where simple-animation would be the
subtraction of two variables, simple and animation).

simple-animation.scm

;; Define a new data type called simple-animation.
(deftype simple-animation ()
(
(name
string)
(speed
float
:default 1.0)


<!-- source-pdf-page: 497 -->

(fade-in-seconds
float
:default 0.25)
(fade-out-seconds float
:default 0.25)
)
)

;; Now define three instances of this data structure...
(define-export anim-walk
(new simple-animation
:name "walk"
:speed 1.0
)
)

(define-export anim-walk-fast
(new simple-animation
:name "walk"
:speed 2.0
)
)

(define-export anim-jump
(new simple-animation
:name "jump"
:fade-in-seconds 0.1
:fade-out-seconds 0.1
)
)

This Scheme code would generate the following C/C++ header file:

simple-animation.h

// WARNING: This file was automatically generated from
// Scheme. Do not hand-edit.

struct SimpleAnimation
{
const char* m_name;
float
m_speed;
float
m_fadeInSeconds;
float
m_fadeOutSeconds;
};

In-game, the data can be read by calling the LookupSymbol() function,
which is templated on the data type returned, as follows:


<!-- source-pdf-page: 498 -->

#include "simple-animation.h"
void someFunction()
{
SimpleAnimation* pWalkAnim
= LookupSymbol<SimpleAnimation*>(
SID("anim-walk"));

SimpleAnimation* pFastWalkAnim
= LookupSymbol<SimpleAnimation*>(
SID("anim-walk-fast"));

SimpleAnimation* pJumpAnim
= LookupSymbol<SimpleAnimation*>(
SID("anim-jump"));

// use the data here...
}

This system gives the programmers a great deal of flexibility in defining all
sorts of configuration data—from simple Boolean, floating-point and string
options all the way to complex, nested, interconnected data structures. It is
used to specify detailed animation trees, physics parameters, player mechanics
and so on.


<!-- source-pdf-page: 499 -->
> Visual fallback for diagrams/images: [PDF page 499](../../../visual_pages/page_0499.jpg)

Taylor & Francis

Taylor & Francis Group
http://taylorandfrancis.com
