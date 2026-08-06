# 4 Parallelism and Concurrent Programming

> Source PDF pages: 222-377
> Extraction mode: PyMuPDF text blocks; line breaks and printed hyphenation are preserved.

<!-- source-pdf-page: 222 -->

C
omputing performance—typically measured in millions of instructions
per second (MIPS) or floating-point operations per second (FLOPS)—has
been improving at a staggeringly rapid and consistent rate over the past four
decades.
In the late 1970s, the Intel 8087 floating-point coprocessor could
muster only about 50 kFLOPS (5 × 104 FLOPS), while at roughly the same
time a Cray-1 supercomputer the size of a large refrigerator could opeate at a
rate of roughly 160 MFLOPS (1.6 × 108 FLOPS). Today, the CPU in game con-
soles like the Playstation 4 or the Xbox One produces roughly 100 GFLOPS
(1011 FLOPS) of processing power, and the fastest supercomputer, currently
China’s Sunway TaihuLight, has a LINPACK benchmark score of 93 PFLOPS
(peta-FLOPS, or a staggering 9.3 × 1016 floating-point operations per second).
This is an improvement of seven orders of magnitude for personal computers,
and eight orders of magnitude for supercomputers.
Many factors contributed to this rapid rate of improvement. In the early
days, the move from vacuum tubes to solid-state transistors permitted the
miniaturization of computing hardware. The number of transistors that could
be etched onto a single chip rose dramatically as new kinds of transistors, new
types of digital logic, new substrate materials and new manufacturing pro-
cesses were developed. These advances also contributed to improvements in


<!-- source-pdf-page: 223 -->
> Visual fallback for diagrams/images: [PDF page 223](../../../visual_pages/page_0223.jpg)

Figure 4.1. Two ﬂows of control operating on independent data are not considered concurrent
because they are not prone to data races.

power consumption and dramatic increases in CPU clock speeds. And start-
ing in the 1990s, computer hardware manufacturers have increasingly turned
to parallelism as a means of improving computing performance.
Writing software that runs correctly and efficiently on parallel computing
hardware is significantly more difficult than writing software for the serial
computers of yesteryear. It requires a deep understanding of how the hard-
ware actually works. What’s more, taking full advantage of the multicore
CPUs found in modern computing platforms requires an approach to soft-
ware design called concurrent programming. In a concurrent software system,
multiple flows of control cooperate to solve a common problem. These flows
of control must be carefully coordinated. Many of the techniques that work
well in serial programs break down when applied to concurrent programs.
As such, it’s important for modern programmers (in all industries, including
games) to have a solid understanding of parallel computing hardware, and to
be well versed in concurrent programming techniques.

## 4.1 Deﬁning Concurrency and Parallelism

### 4.1.1 Concurrency

A concurrent piece of software utilizes multiple flows of control to solve a prob-
lem. These flows of control might be implemented as multiple threads running
within the context of a single process, or multiple cooperating processes run-
ning either on a single computer or multiple computers. Multiple flows of
control can also be implemented within a process using other techniques such
as fibers or coroutines.
The primary factor that distinguishes concurrent programming from sequen-
tial programming is the reading and/or writing of shared data. As shown in


<!-- source-pdf-page: 224 -->
> Visual fallback for diagrams/images: [PDF page 224](../../../visual_pages/page_0224.jpg)

Figure 4.2. Two ﬂows of control both reading from a shared data ﬁle and/or writing to a shared
data ﬁle are examples of concurrency.

Figure 4.1, if we have two or more flows of control, each operating on a to-
tally independent block of data, then this is not technically an example of
concurrency—it’s just “computing at the same time.”

The central problem of concurrent programming is how to coordinate mul-
tiple readers and/or multiple writers of a shared data file, in such a way as to
ensure predictable, correct results. At the heart of this problem is a special
kind of race condition known as a data race, in which two or more flows of con-
trol “compete” to be the first to read, modify and write a chunk of shared data.
The crux of the concurrency problem is to identify and eliminate data races.
Two examples of concurrency are illustrated in Figure 4.2.

We’ll explore the techiques programmers use to avoid data races and
thereby write reliable concurrent programs starting in Section 4.5. But before
we do that, let’s take a look at how parallel computer hardware can both pro-
vide an effective platform for running concurrent software, and improve the
execution speed of sequential programs as well.

### 4.1.2 Parallelism

In computer engineering, the term parallelism refers to any situation in which
two or more distinct hardware components are operating simultaneously. In
other words, parallel computer hardware can perform more than one task at a
time. In contrast, serial computer hardware is capable of doing only one thing
at a time.

Prior to 1989, consumer-grade computing devices were exclusively serial
machines. Examples include the MOS Technology 6502 CPU, which was used
in the Apple II and Commodore 64 personal computers, and the Intel 8086,
80286 and 80386 CPUs which were at the heart of early IBM PCs and clones.


<!-- source-pdf-page: 225 -->

Today, parallel computing hardware is ubiquitous. One obvious exam-
ple of hardware parallelism is a multicore CPU, such as the Intel Core™i7
or the AMD Ryzen™7. But parallelism can be employed at a wide range of
granularities. For example, a single CPU might contain multiple ALUs and
therefore be capable of performing multiple independent calculations in par-
allel. And at the other end of the spectrum, a cluster of computers working
in tandem to solve a common problem is also an example of hardware paral-
lelism.

4.1.2.1
Implicit versus Explicit Parallelism

One way to classify the various forms of parallelism in computer hardware
design is to consider the purpose of parallelism in each. In other words, what
problem does parallelism solve in a given design? Thinking along these lines,
we can divide parallelism coarsely into two categories:

•
implicit parallelism, and
•
explicit parallelism.

Implicit parallelism refers to the use of parallel hardware components within
a CPU for the purpose of improving the performance of a single instruction
stream. This is also known as instruction level parallelism (ILP), because the CPU
executes instructions from a single stream (a single thread) but each instruction
is executed with some degree of hardware parallelism. Examples of implicit
parallelism include:

•
pipelining,
•
superscalar architectures, and
•
very long instruction word (VLIW) architectures.

We’ll explore implicit parallelism in Section 4.2. GPUs also make extensive use
of implicit parallelism; we’ll take a closer look at the design and programming
of GPUs in Section 4.11.
Explicit parallelism refers to the use of duplicated hardware components
within a CPU, computer or computer system, for the purpose of running more
than one instruction stream simultaneously. In other words, explicitly parallel
hardware is designed to run concurrent software more efficiently than would
be possible on a serial computing platform. The most common examples of
explicit parallelism are:

•
hyperthreaded CPUs,
•
multicore CPUs,


<!-- source-pdf-page: 226 -->

•
multiprocessor computers,
•
computer clusters,
•
grid computing, and
•
cloud computing.

We’ll investigate these explicitly parallel architectures further in Section 4.3.

### 4.1.3 Task versus Data Parallelism

Another way to understand parallelism is to divide it into two broad categories
based on the kind of work being done in parallel.

•
Task parallelism. When multiple heterogeneous operations are performed
in parallel, we call this task parallelism. Performing animation calcula-
tions on one core while performing collision checks on another would
be an example of this form of parallelism.

•
Data parallelism. When a single operation is performed on multiple data
items in parallel, it is called data parallelism. Calculating 1000 skinning
matrices by running 250 matrix calculations on each of four cores would
be an example of data parallelism.

Most real concurrent programs make use of both task and data parallelism to
varying degrees.

### 4.1.4 Flynn’s Taxonomy

Yet another way to classify the varying degrees of parallelism we’ll encounter
in computing hardware is to use Flynn’s Taxonomy. Proposed by Michael J.
Flynn of Stanford University in 1966, this approach breaks down parallelism
into a two-dimensional space. Along one axis, we have the number of parallel
flows of control (which Flynn refers to as the number of instructions running
in parallel at any given moment). On the other axis, we have the number of
distinct data streams being operated on by each instruction in the program.
The space is thus divided into four quadrants:

•
Single instruction, single data (SISD): A single instruction stream operating
on a single data stream.
•
Multiple instruction, multiple data (MIMD): Multiple instruction streams
operating on multiple independent data streams.
•
Single instruction, multiple data (SIMD): A single instruction stream op-
erating on multiple data streams (i.e., performing the same sequence of
operations on multiple independent streams of data simultaneously).


<!-- source-pdf-page: 227 -->
> Visual fallback for diagrams/images: [PDF page 227](../../../visual_pages/page_0227.jpg)

Figure 4.3. Example of SISD. A single ALU performs the multiply ﬁrst, followed by the divide.

•
Multiple instruction, single data (MISD): Multiple instruction streams all
operating on a single data stream. (MISD is rarely used in games, but
one common application is to provide fault tolerance via redundancy.)

4.1.4.1
Single versus Multiple Data

It’s important to realize here that a “data stream” isn’t just an array of numbers.
Most arithmetic operators are binary—they operate on two inputs to produce
a single output. When applied to binary arithmetic, the term “single data”
refers to a single pair of inputs, with a single output. As an example, let’s have
a look at how two binary arithmetic operations, a multiply (a × b) and a divide
(c/d), might be accomplished under each of the four Flynn categories:

•
In a SISD architecture, a single ALU performs the multiply first, followed
by the divide. This is illustrated in Figure 4.3.

•
In a MIMD architecture, two ALUs perform operations in parallel, oper-
ating on two independent instruction streams. This is shown in Figure
4.4.

•
The MIMD classification also applies to the case in which a single ALU
processes two independent instruction streams via time-slicing, as shown
in Figure 4.5.

•
In a SIMD architecture, a single “wide ALU” known as a vector pro-
cessing unit (VPU) performs the multiply first, followed by the divide,
but each instruction operates on a pair of four-element input vectors
and produces a four-element output vector. Figure 4.6 illustrates this
approach.


<!-- source-pdf-page: 228 -->
> Visual fallback for diagrams/images: [PDF page 228](../../../visual_pages/page_0228.jpg)

Figure 4.4. Example of MIMD. Two ALUs perform operations in parallel.

Instruction Stream

Instruction Stream

mul a,b

sub g,h

div c,d

mul i,j

...

...

ALU

Figure 4.5. Example of time-sliced MIMD. A single ALU performs operations on behalf of two inde-
pendent instruction streams, perhaps by alternating between them.

•
In a MISD architecture, two ALUs process the same instruction stream
(multiply first, followed by divide) and ideally produce identical results.
Illustrated in Figure 4.7, this architecture is primarily useful for imple-
menting fault tolerance via redundancy. ALU 1 acts as a “hot spare” for
ALU 0 and vice-versa, meaning that if one of the ALUs experiences a
failure, the system can seamlessly switch to the other.

4.1.4.2
GPU Parallelism: SIMT

In recent years, a fifth classification has been added to Flynn’s taxonomy to
account for the design of graphics processing units (GPU). Single instruction
multiple thread (SIMT) is essentially a hybrid between SIMD and MIMD, used
primarily in the design of GPUs. It mixes SIMD processing (a single instruc-


<!-- source-pdf-page: 229 -->
> Visual fallback for diagrams/images: [PDF page 229](../../../visual_pages/page_0229.jpg)

Instruction Stream

vmul a,b

vdiv c,d

...

VPU

Figure 4.6. Example of SIMD. A single vector processing unit (VPU) performs the multiply ﬁrst,
followed by the divide, but each instruction operates on a pair of four-element input vectors and
produces a four-element output vector.

Instruction Stream

mul a,b

div c,d

...

ALU0
ALU1

Can Hot-Swap

On Failure

Figure 4.7. Example of MISD. Two ALUs process the same instruction stream (multiply ﬁrst, followed
by divide) and ideally produce identical results.

tion operating on multiple data streams simultaneously) with multithreading
(more than one instruction stream sharing a processor via time-slicing).

The term “SIMT” was coined by NVIDIA, but it can be used to describe
the design of any GPU. The term manycore is also often used to refer to a SIMT
design (i.e., a GPU consisting of a relatively large number of lightweight SIMD
cores) whereas the term multicore refers to MIMD designs (i.e., a CPU with a
relatively smaller number of heavyweight general purpose cores). We’ll take
a closer look at the SIMT design employed by GPUs in Section 4.11.


<!-- source-pdf-page: 230 -->

### 4.1.5 Orthogonality of Concurrency and Parallelism

We should stress here that concurrent software doesn’t require parallel hard-
ware, and parallel hardware isn’t only for running concurrent software. For
example, a concurrent multithreaded program can run on a single, serial CPU
core via preemptive multitasking (see Section 4.4.4). Likewise, instruction
level parallelism is intended to improve the performance of a single thread,
and therefore benefits both concurrent and sequential software. So while they
are closely related, concurrency and parallelism are really orthogonal con-
cepts.

As long as our system involves multiple readers and/or multiple writers of a
shared data object, we have a concurrent system. Concurrency can be achieved
via preemptive multitasking (on serial or parallel hardware) or via true paral-
lelism (in which each thread executes on a distinct core)—the techniques we’ll
learn in this chapter will be applicable either way.

### 4.1.6 Roadmap of the Chapter

In the following sections, we’ll first turn our attention to implicit parallelism,
and how best to optimize our software to take advantage of it. Next, we’ll re-
view the most common forms of explicit parallelism. Then we’ll explore the
various concurrent programming techniques used to harness explicitly paral-
lel computing platforms. Finally, we’ll round out the discussion of parallel
programming by discussing SIMD vector processing, and how it applies to
GPU design and general-purpose GPU programming (GPGPU) techniques.

## 4.2 Implicit Parallelism

In Section 4.1.2.1 we said that implicit parallelism is the use of parallel com-
puting hardware for the purpose of improving the execution speed of a sin-
gle thread. CPU manufacturers began using implicit parallelism in their con-
sumer products in the late 1980s, in an attempt to make existing code run faster
on each new generation of CPUs within a given product line.

There are a number of ways to apply parallelism to the problem of im-
proving a CPU’s single-threaded performance. The most common are pipelined
CPU architectures, superscalar designs, and very long instruction word (VLIW)
architectures. We’ll begin by having a look at how a pipelined CPU functions,
and then address the other two variants of implicit parallelism.


<!-- source-pdf-page: 231 -->
> Visual fallback for diagrams/images: [PDF page 231](../../../visual_pages/page_0231.jpg)

Clock
Cycle

Fetch
Dec
Exec
Mem
WB

A

0

1

A

A

2

A

3

A

4

B

5

B

6

Figure 4.8. In a non-pipelined CPU, instruction stages are idle much of the time.

### 4.2.1 Pipelining

In order for a single machine language instruction to be executed by the CPU,
it must pass through a number of distinct stages. Every CPU’s design is a bit
different—some CPU designs employ a large number of granular stages, while
others utilize a smaller number of coarse-grained stages. However every CPU
implements the following basic stages in one way or another:

•
Fetch. The instruction to be executed is read from the memory location
pointed to by the instruction pointer register (IP).

•
Decode. The instruction word is decomposed into its opcode, addressing
mode and optional operands.

•
Execute. Based on the opcode, the appropriate functional unit within the
CPU is selected (ALU, FPU, memory controller, etc.). The instruction
is dispatched to the selected component for processing, along with any
relevant operand data. The functional unit then performs its operation.

•
Memory access. If the instruction involves reading or writing memory,
the memory controller performs the appropriate operation during this
stage.

•
Register write-back. The functional unit executing the instruction (ALU,
FPU, etc.) writes its results back into the destination register.

Figure 4.8 traces the path of two instructions named “A” and “B” through
the five execution phases of a serial CPU. You’ll notice right away that this
diagram contains a lot of blank space: While one stage is busy doing its thing to
an instruction, all the other stages are twiddling their thumbs, doing nothing.


<!-- source-pdf-page: 232 -->
> Visual fallback for diagrams/images: [PDF page 232](../../../visual_pages/page_0232.jpg)

Instruction Stream

B
A
C
D
...

GPRs

WB

Fetch

Decode
ALU
Mem
Ctrl

L1
Cache

L2 &
Main RAM

Figure 4.9. Components of a pipelined scalar CPU.

Clock
Cycle

Fetch
Dec
Exec
Mem
WB

A

0

1

B

A

C

B

A

2

D

C

B

A

3

E

D

C

B

A

4

F

E

D

C

B

5

F
G

E

D

C

6

Figure 4.10. Ideal ﬂow of instructions through a pipelined CPU.

Each of the stages of instruction execution is actually handled by differ-
ent hardware within the CPU, as shown in Figure 4.9. The control unit (CU)
and memory controller handle the instruction fetch stage. A different circuit
within the CU then handles the decode stage. The ALU, FPU or VPU handles
the lion’s share of the execute stage. The memory stage is performed by the
memory controller. And finally, the write-back stage primarily involves the
registers. This division of labor amongst different circuits within the CPU is
the key to making the CPU more efficient: We just need to keep all the stages’
hardware busy all the time.
The solution is known as pipelining. Instead of waiting for each instruction
to complete all five stages before starting to execute the next one, we begin the
execution of a new instruction on every clock cycle. Multiple instructions are
therefore “in flight” simultaneously. This process is illustrated in Figure 4.10.
Pipelining is a bit like doing laundry. If you have a large number of loads
to do, it wouldn’t be very efficient to wait until each load has been washed and


<!-- source-pdf-page: 233 -->

dried before starting the next one—while the washer is busy, the dryer would
be sitting idle, and vice versa. It’s much better to keep both machines busy at
all times, by starting the second load washing just as soon as the first load goes
into the dryer, and so on.
Pipelining is a form of parallelism known as instruction-level parallelism
(ILP). For the most part, ILP is designed to be transparent to the programmer.
Ideally, at a given clock speed, a program that runs properly on a scalar CPU
should be able to run correctly—but faster—on a pipelined CPU, as long as the
two processors support the same instruction set architecture (ISA) of course.
In theory, a CPU with a pipeline that is N stages deep can execute a program
N times faster than its serial counterpart. However, as we’ll explore in Section
4.2.4, pipelining doesn’t always perform as well as we would expect, thanks
to various kinds of dependencies between instructions in the instruction stream.
Programmers who are interested in writing high-performance code therefore
cannot remain oblivious to ILP. We must embrace it, understand it, and some-
times adjust the design of our code and/or data in order to get the most out
of a pipelined CPU.

### 4.2.2 Latency versus Throughput

The latency of a pipeline is the amount of time required to completely process
a single instruction. This is just the sum of the latencies of all the stages in the
pipeline. Denoting latencies with the time variable T, we can write:

Tpipeline =
N−1
∑
i=0
Ti
(4.1)

for a pipeline with N stages.
The throughput or bandwidth of a pipeline is a measure of how many instruc-
tions it can process per unit time. The throughput of a pipeline is determined
by the latency of its slowest stage—much as a chain is only as strong as its
weakest link. The throughput can be thought of as a frequency f, measured
in instructions per second. It can be written as follows:

f =
1
max(Ti).
(4.2)

### 4.2.3 Pipeline Depths

We said that each stage in a CPU can potentially have a different latency (Ti),
and that the stage with the longest latency dictates the throughput of the entire
processor. On each clock cycle, the other stages sit idle waiting for the longest


<!-- source-pdf-page: 234 -->
> Visual fallback for diagrams/images: [PDF page 234](../../../visual_pages/page_0234.jpg)

stage to complete. Ideally, then, we’d like all of the stages in our CPU to have
roughly the same latency.
This goal can be achieved by increasing the total number of stages in the
pipeline: If one stage is taking much longer than the others, it can be broken
into two or more shorter stages in an attempt to make all the latencies roughly
equal. However, we can’t just keep subdividing stages forever. The larger
the number of stages, the higher the overall instruction latency will be. This
increases the cost of pipeline stalls (see Section 4.2.4). Therefore CPU man-
ufacturers try to strike a balance between increasing throughput via deeper
pipelines, and keeping the overall instruction latency in check. As a result,
real CPU pipelines range from 4 or 5 stages at minimum, to something on the
order of 30 stages at most.

### 4.2.4 Stalls

Sometimes the CPU is unable to issue a new instruction on a particular clock
cycles. This is called a stall. On such a clock cycle, the first stage in the pipeline
lies idle. On the next clock cycle, the second stage will be idle, and so on. A
stall can therefore be thought of as a “bubble” of idle time that propagates
through the pipeline at a rate of one stage per clock cycle. These bubbles are
sometimes called delay slots.

### 4.2.5 Data Dependencies

Stalls are caused by dependencies between instructions in the instruction stream
being executed. For example, consider the following sequence of instructions:

mov
ebx,5
;; load the value 5 into register EBX
imul eax,10
;; multiply the contents of EAX by 10
;; (result stored in EAX)
add
eax,7
;; add 7 to EAX (result stored in EAX)

Ideally, we’d like to issue the mov, imul and add instructions on three con-
secutive clock cycles, to keep the pipeline as busy as possible. But in this case,
the results of the imul instruction are used by the add instruction that follows
it, so the CPU must wait until the imul has made it all the way through the
pipeline before issuing the add. If the pipeline contains five stages, that means
four cycles are wasted (see Figure 4.11). These kinds of dependencies between
instructions are called data dependencies.
There are actually three kinds of dependencies between instructions that
can cause stalls:

•
data dependencies,


<!-- source-pdf-page: 235 -->
> Visual fallback for diagrams/images: [PDF page 235](../../../visual_pages/page_0235.jpg)

Figure 4.11. A data dependency between instructions causes a pipeline stall.

•
control dependencies (also known as branch dependencies), and

•
structural dependencies (also known as resource dependencies).

First we’ll discuss how to avoid data dependencies, then we’ll have a look at
branch dependencies and how to mitigate their effects. Finally, we’ll introduce
superscalar CPU architectures and discuss how they can give rise to structural
dependencies in a pipelined CPU.

4.2.5.1
Instruction Reordering

To mitigate the effects of a data dependency, we need to find some other in-
structions for the CPU to execute while it waits for the dependent instruction
to make its way through the pipeline. This can often be accomplished by re-
ordering the instructions in the program (while taking care not to change the
behavior of the program in the process). For any given pair of interdependent
instructions, we want to find some nearby instructions that are not dependent
on them, and move those instructions up or down so that they end up running
between the dependent instruction pair, thus filling the “bubble” with useful
work.
Instruction reordering may of course be done by hand, by an adventurous
programmer who doesn’t mind diving into assembly language programming.
Thankfully, however, this is often not necessary: Today’s optimizing compilers
are very good at reordering instructions automatically to reduce or eliminate
the impact of data dependencies.
As programmers, of course we shouldn’t blindly trust the compiler to op-
timize our code perfectly—when writing high-performance code, it’s always
a good idea to have a look at the disassembly and verify that the compiler
did a reasonable job. But that said, we should also remember the 80/20 rule


<!-- source-pdf-page: 236 -->

(Section 2.3) and only spend time optimizing the 20% or less of our code that
actually has a noticable impact on overall performance.

4.2.5.2
Out-of-Order Execution

Compilers and programmers aren’t the only ones capable of reordering a se-
quence of machine language instructions to prevent stalls. Many of today’s
CPUs support a feature known as out-of-order execution, which enables them
to dynamically detect data dependencies between instructions, and automat-
ically resolve them.
To accomplish this feat, the CPU looks ahead in the instruction stream
and analyzes the instructions’ register usage in order to detect dependencies
between them. When a dependency is found, the “look-ahead window” is
searched for another instruction that is not dependent on any of the currently-
executing instructions. If one is found, it is issued (out of order!) to keep the
pipeline busy. The details of how out-of-order execution works is beyond our
scope here. Suffice it to say that as programmers, we cannot rely on the CPU
to execute the instructions in the same order we (or the compiler) wrote them.
Both the compiler’s optimizer and the CPU’s out-of-order execution logic
take great care to ensure that the behavior of the program doesn’t change as
a result of instruction reordering. However, as we’ll see in Section 4.9.3, com-
piler optimizations and out-of-order execution can cause bugs in a concurrent
program (i.e., a program consisting of multiple threads that share data). This
is one of the many reasons why concurrent programming requires more care
than serial programming.

### 4.2.6 Branch Dependencies

What happens when a pipelined CPU encounters a conditional branch instruc-
tion (e.g., an if statement, or the conditional expression at the end of a for
or while loop)? To answer this question, let’s consider the following C/C++
code:

int SafeIntegerDivide(int a, int b, int defaultVal)
{
return (b != 0) ? a / b : defaultVal;
}

If we looked at the disassembly for this function, it might look something like
this on an Intel x86 CPU:


<!-- source-pdf-page: 237 -->
> Visual fallback for diagrams/images: [PDF page 237](../../../visual_pages/page_0237.jpg)

Figure 4.12. A dependency between a comparison instruction and a conditional branch instruction
is called a branch dependency.

; function preamble omitted for clarity...

; first, put the default into the return register
mov
eax,dword ptr [defaultVal]

mov
esi,dword ptr [b] ; check (b != 0)
cmp
esi,0
jz
SkipDivision

mov
eax, dword ptr[a] ; divisor (a) must be in EDX:EAX
cdq
; ... so sign-extend into EDX
idiv esi
; quotient lands in EAX

SkipDivision:
; function postamble omitted for clarity...
ret
; EAX is the return value

The dependency here is between the cmp (compare) instruction and the jz
(jump if equal to zero) instruction. The CPU cannot issue the conditional jump
until it knows the results of the comparison. This is called a branch dependency
(also known as a control dependency). See Figure 4.12 for an illustration of a
branch dependency.

4.2.6.1
Speculative Execution

One way CPUs deal with branch dependencies is via a technique known as
speculative execution, also known as branch prediction. Whenever a branch in-
struction is encountered, the CPU tries to guess at which branch is going to
be taken. It continues to issue the instructions from the selected branch, in
the hopes that its guess was correct. Of course, the CPU won’t know for sure
whether it guessed correctly until the dependent instruction pops out at the
end of the pipeline. If the guess ends up being wrong, the CPU has executed


<!-- source-pdf-page: 238 -->

instructions that shouldn’t have been executed at all. So the pipeline must be
flushed and restarted at the first instruction of the correct branch. This is called
a branch penalty.
The simplest guess a CPU can make is to assume that branches are never
taken. The CPU just keeps executing instructions in sequential order, and
only jumps the instruction pointer to a new location when its guess is proven
wrong. This approach is I-cache friendly, in the sense that the CPU always
prefers the branch whose instructions are most likely to be in the cache.
Another slightly more advanced approach to branch prediction is to as-
sume that backward branches are always taken and forward branches are
never taken. A backward branch is the kind found at the end of a while or
for loop, so such branches tend to be more prevalent than forward branches.
Most high-quality CPUs include branch prediction hardware that can im-
prove the quality these “static” guesses significantly. A branch predictor can
track the results of a branch instruction over multiple iterations of a loop and
discover patterns that help it make better guesses on subsequent iterations.
PS3 game programmers had to deal with the poor performance of
“branchy” code all the time, because the branch predictors on the Cell pro-
cessor were frankly pretty terrible. But the AMD Jaguar CPU found in the
PS4 and Xbox One has highly advanced branch prediction hardware, so game
programmers can breathe a little easier when writing code for the PS4.

4.2.6.2
Predication

Another way to mitigate the effects of branch dependencies is to simply avoid
branching altogether. Consider again the SafeIntegerDivide() function,
although we’ll modify it slightly to work in terms of floating-point values in-
stead of integers:

float SafeFloatDivide(float a, float b, float d)
{
return (b != 0.0f) ? a / b : d;
}

This simple function calculates one of two answers, depending on the re-
sults of the conditional test b != 0. Instead of using a conditional branch
statement to return one of these two answers, we can instead arrange for our
conditional test to generate a bit mask consisting of all zeros (0x0U) if the con-
dition is false, and all ones (0xFFFFFFFFU) if it is true. We can then execute
both branches, generating two alterate answers. Finally, we use the mask to
produce the final answer that we’ll return from the function.


<!-- source-pdf-page: 239 -->

The following pseudocode illustrates the idea behind predication. (Note
that this code won’t run as-is. In particular, you can’t mask a float with
an unsigned int and obtain a float result—you’d need to use a union to
reinterpret the bit patterns of the floats as if they were unsigned integers
when applying the mask.)

int SafeFloatDivide_pred(float a, float b, float d)
{
// convert Boolean (b != 0.0f) into either 1U or 0U
const unsigned condition = (unsigned)(b != 0.0f);

// convert 1U -> 0xFFFFFFFFU
// convert 0U -> 0x00000000U
const unsigned mask = 0U - condition;

// calculate quotient (will be QNaN if b == 0.0f)
const float q = a / b;

// select quotient when mask is all ones, or default
// value d when mask is all zeros (NOTE: this won't
// work as written -- you'd need to use a union to
// interpret the floats as unsigned for masking)
const float result = (q & mask) | (d & ~mask);
return result;
}

Let’s take a closer look at how this works:

•
The test b != 0.0f produces a bool result. We convert this into an
unsigned integer by simply casting it. This results in either the value
1U (corresponding to true) or 0U (corresponding to false).

•
We convert this unsigned result into a bit mask by subtracting it from
0U. Zero minus zero is still zero, and zero minus one is −1, which is
0xFFFFFFFFU in 32-bit unsigned integer arithmetic.

•
Next, we go ahead and calculate the quotient. We run this code regard-
less of the result of the non-zero test, thereby side-stepping any branch
dependency issues.

•
We now have our two answers ready to go: The quotient q and the
default value d. We want to apply the mask in order to “select” one
or the other value. But to do this, we need to reinterpret the floating-
point bit patterns of q and d as if they were unsigned integers. The most
portable way to accomplish this in C/C++ is to use a union containing


<!-- source-pdf-page: 240 -->

two members, one of which interprets a 32-bit value as a float, the
other of which interprets it as an unsigned.

•
The mask is applied as follows: We bitwise AND the quotient q with the
mask, producing a bit pattern that matches q if the mask is all ones, but
is all zeros if the mask is all zeros. We bitwise AND the default value
d with the complement of the mask, yielding all zeros if the mask is all
ones, or the bit pattern of d if the mask is all zeros. Finally, we bitwise
OR these two values together, effectively selecting either the value of q or
the value of d.

The use of a mask to select one of two possible values like this is called pred-
ication because we run both code paths (the one that returns a / b and
the one that returns d) but each code path is predicated on the results of the
test (a != 0), via the mask. Because we are selecting one of two possible
values, this is also often called a select operation.
Going to all this trouble to avoid a branch may seem like overkill. And it
can be—its usefulness depends on the relative cost of a branch versus the pred-
icated alternative on your target hardware. Predication really shines when
a CPU’s ISAs provide special machine language instructions for performing
a select operation. For example, the PowerPC ISA offers an integer select
instruction isel, a floating-point select instruction fsel, and even a SIMD
vector select instruction vecsel, and their use can definitely result in perfor-
mance improvements on PowerPC based platforms (like the PS3).
It’s important to realize that predication only works when both branches
can be executed safely. Performing a divide by zero operation in floating-point
generates a quiet not-a-number (QNaN), but an integer divide by zero throws
an exception that will crash your game (unless it is caught). That’s why we
converted this example to floating-point before applying predication to it.

### 4.2.7 Superscalar CPUs

The pipelined CPU we described in Section 4.2.1 is what is called a scalar pro-
cessor. This means that it can start executing at most one instruction per clock
cycle. Yes, multiple instructions are “in flight” at any given moment, but only
one new instruction is sent down the pipeline every clock cycle.
At its core, parallelism is about making use of multiple hardware compo-
nents simultaneously. So one way to double the throughput of a CPU (at least
in theory!) would be to duplicate most of the components on the chip, in such
a way that two instructions could be launched each clock cycle. This is called
a superscalar architecture.


<!-- source-pdf-page: 241 -->
> Visual fallback for diagrams/images: [PDF page 241](../../../visual_pages/page_0241.jpg)

Figure 4.13. A pipelined superscalar CPU contains multiple execution components (ALUs, FPUs
and/or VPUs) fed by a single instruction scheduler which typically supports out-of-order and spec-
ulative execution.

In a superscalar CPU, two (or more) instances of the circuitry that man-
ages each stage of the pipeline1 is present on-chip. The CPU still fetches in-
structions from a single instruction stream, but instead of issuing the one in-
struction pointed to by the IP during each clock cycle, the next two instructions
are fetched and dispatched during each clock cycle. Figure 4.13 illustrates the
hardware components found in a two-way superscalar CPU, and Figure 4.14
traces the path of ten instructions, “A” through “N,” as they move through
this CPU’s two parallel pipelines.

4.2.7.1
Complexity of Superscalar Designs

Implementing a superscalar CPU isn’t quite as simple as “copying and past-
ing” two identical CPU cores onto a die. Although it’s reasonable to envision
a superscalar CPU as two parallel instruction pipelines, these two pipelines
are fed from a single instruction stream. Some some kind of control logic is
therefore required at the front end of these parallel pipes. Just as on a CPU
that supports out-of-order execution, a superscalar CPU’s control logic looks
ahead in the instruction stream in an attempt to identify dependencies between
instructions, and then issues instructions out of order in an attempt to mitigate
their effects.
In addition to data and branch dependencies, a superscalar CPU is prone
to a third kind of dependency known as a resource dependency. This kind of
dependency arises when two or more consecutive instructions all require the

1Technically pipelining and superscalar designs are two independent forms of parallelism. A
pipelined CPU needn’t be superscalar. Likewise, a superscalar CPU needn’t be pipelined, al-
though the majority of them are.


<!-- source-pdf-page: 242 -->
> Visual fallback for diagrams/images: [PDF page 242](../../../visual_pages/page_0242.jpg)

Clock
Cycle

E1
M0
M1
W0
W1
F0
F1
D0
D1
E0

A

B

0

C
D

A

B

1

E
F

C
D

A

B

2

G

H

E
F

C
D

B
A

3

H
I
J

G

E
F

C
D

B
A

4

K
L

H
I
J

G

E
F

C
D

5

I
J
K
L
M
N

H
G

E
F

6

Figure 4.14. Best-case execution of 14 instructions “A” through “N” on a superscalar pipelined CPU
over seven clock cycles.

same functional unit within the CPU. For example, let’s imagine we have a
superscalar CPU with two integer ALUs but only one FPU. Such a processor
is capable of issuing two integer arithmetic instructions on each clock. But if
two floating-point arithmetic instructions are encountered in the instruction
stream, they cannot both be issued on the same clock cycle because the re-
source required by the second (the FPU) will already be in use by the first. As
such, the control logic that manages instruction dispatch on a superscalar CPU
is even more complex than that found on a scalar CPU that supports out-of-
order execution.

4.2.7.2
Superscalar and RISC

A two-way superscalar CPU requires roughly two times the silicon real-estate
of a comparable scalar CPU design. In order to free up transistors, most super-
scalar CPUs are therefore reduced instruction set (RISC) processors. The ISA of
a RISC processor provides a comparatively small set of instructions, each with
a very focused purpose. More complex operations are peformed by building
up sequences of these simpler instructions. In contrast, the ISA of a complex in-
struction set computer (CISC) offers a much wider variety of instructions, each
of which may be capable of performing more complex operations.

### 4.2.8 Very Long Instruction Word (VLIW)

We saw in Section 4.2.7.1 that a superscalar CPU contains highly complex in-
struction dispatch logic. This logic takes up valuable real-estate on the CPU


<!-- source-pdf-page: 243 -->
> Visual fallback for diagrams/images: [PDF page 243](../../../visual_pages/page_0243.jpg)

die. Also, CPUs are only capable of looking ahead in the instruction stream by
a relatively small number of instructions when analyzing dependencies and
looking for opportunities for out-of-order and/or superscalar instruction dis-
patch. This limits the effectiveness of the dynamic optimizations a CPU can
perform.
A somewhat simpler way to implement instruction level parallelism is
to design a CPU that has multiple compute elements (ALUs, FPUs, VPUs)
on-chip, but leaves the task of dispatching instructions to those compute ele-
ments entirely to the programmer and/or compiler. That way, all the complex
instruction-dispatch logic can be eliminated, and those transistors devoted in-
stead to implementing more compute elements or a larger cache. As a side
benefit, programmers and compilers ought to be better at optimizing the dis-
patching of the instructions in their programs than the CPU could ever be,
because they can select instructions for dispatch from a much wider window
(typically an entire function’s worth of instructions).
To allow programmers and/or compilers to dispatch instructions to multi-
ple compute elements on each clock cycle, the instruction word is extended so
that it contains two or more “slots,” each corresponding to a compute element
on the chip. For example, if our hypothetical CPU contained two integer ALUs
and two FPUs, a programmer or compiler would need to be able to encode up
to two integer and two floating-point operations within each instruction word.
We call this a very long instruction word (VLIW) design. The VLIW architecture
is illustrated in Figure 4.15.
We can look to the Playstation 2 for a concrete example of VLIW architec-
ture: The PS2 contained two coprocessors called vector units (VU0 and VU1),
each of which was capable of dispatching two instructions per clock cycle.
Each instruction word was comprised of two slots called low and high.
It
was often a challenge to fill both slots effectively when hand-coding in as-
sembly language, although tools were developed that helped programmers to
convert a one-instruction-per-clock program into an efficient two-instructions-
per-clock format.
There are trade-offs between the superscalar and VLIW approaches. Be-
cause it lacks the complex scheduling, out-of-order execution and branch pre-
diction logic of a superscalar CPU, a VLIW processor is much simpler, and
can therefore potentially make heavier use of parallelism than its superscalar
counterparts. However, it can be very tough to transform a serial program into
a form that takes full advantage of the parallelism in a VLIW. This makes the
job of the programmer and/or compiler more difficult. That said, a number
of advances have been made to overcome some of these limitations, includ-
ing variable-width VLIW designs. For example, see http://researcher.watson.


<!-- source-pdf-page: 244 -->
> Visual fallback for diagrams/images: [PDF page 244](../../../visual_pages/page_0244.jpg)

Figure 4.15. A pipelined VLIW CPU architecture consisting of two integer ALUs and two ﬂoating-
point FPUs. Each very long instruction word consists of two integer and two ﬂoating-point op-
erations, which are dispatched to the corresponding functional units. Notice the absence of the
complex instruction scheduling logic that would be present in a superscalar CPU.

ibm.com/researcher/view_group_subpage.php?id=2834.

## 4.3 Explicit Parallelism

Explicit parallelism is designed to make concurrent software run more effi-
ciently. Hence all explicitly parallel hardware designs permit more than one
instruction stream to be processed in parallel. We’ll list a few common explic-
itly parallel designs below, increasing in granularity from hyperthreading at the
most fine-grained end of the spectrum to cloud computing at the most coarse-
grained end.

### 4.3.1 Hyperthreading

As we saw in Section 4.2.5.2, some pipelined CPUs are capable of issuing
instructions out of order as a means of reducing pipeline stalls. Normally a
pipelined CPU executes instructions in program order; but sometimes the next
instruction in the instruction stream cannot be issued due to a dependency on
an in-flight instruction. This creates a delay slot into which another instruction
could theoretically be issued. An OOO CPU can “look ahead” in the instruc-
tion stream and select an instruction to issue out-of-order into such a delay
slot.


<!-- source-pdf-page: 245 -->
> Visual fallback for diagrams/images: [PDF page 245](../../../visual_pages/page_0245.jpg)

B
A
C
...

Hyperthread 0

GPRs
FPRs

F/D

Q
P
R
...

Hyperthread 1

GPRs
FPRs

F/D

Shared
Resources

Scheduler

L1
Mem
Ctrl
ALU
ALU
FPU
FPU

Figure 4.16. A hyperthreaded CPU containing two front ends (each consisting of a fetch/decode
unit and a register ﬁle), but with a single back end containing ALUs, FPUs, a memory controller, an
L1 cache, and an out-of-order instruction scheduler. The scheduler issues instructions from both
front-end threads to the shared back-end components.

With only a single instruction stream, the CPU’s options are somewhat
limited when selecting an instruction to issue into a delay slot. But what if
the CPU could select its instructions from two separate instruction streams at
once? This is the principle behind a hyperthreaded (HT) CPU core.
Technically speaking, an HT core consists of two register files and two in-
struction decode units, but with a single “back end” for executing instruc-
tions, and a single shared L1 cache. This design enables an HT core to run
two independent threads, while requiring fewer transistors than a dual core
CPU, thanks to the shared back end and L1 cache. Of course, this sharing of
hardware components also results in lower instruction throughput relative to
a comparable dual core CPU, because the threads contend for these shared re-
sources. Figure 4.16 illustrates the key components in a typical hyperthreaded
CPU design.

### 4.3.2 Multicore CPUs

A CPU core can be defined as a self-contained unit capable of executing instruc-
tions from at least one instruction stream. Every CPU design we’ve looked at
until now could therefore qualify as a “core.” When more than one core is
included on a single CPU die, we call it a multicore CPU.


<!-- source-pdf-page: 246 -->
> Visual fallback for diagrams/images: [PDF page 246](../../../visual_pages/page_0246.jpg)

B
A
C
...

Core 0

F/D

GPRs
ALU
FPRs
FPU

L1
Mem
Ctrl

Sched

Q
P
R
...

L2

Core 1

F/D

GPRs
ALU
FPRs
FPU

L1
Mem
Ctrl

Sched

Figure 4.17. A simple multicore CPU design.

The specific design within each core can be any of the designs we’ve looked
at thus far—each core might employ a simple serial design, a pipelined design,
a superscalar architecture, a VLIW design, or might be a hyperthreaded core.
Figure 4.17 illustrates a simple example of a multicore CPU design.
The PlayStation 4 and Xbox One game consoles both contain multicore
CPUs. Each contains an accelerated processing unit (APU) consisting of two
quad-core AMD Jaguar modules, integrated onto a single die with a GPU,
memory controller and video codec. (Of these eight cores, seven are avail-
able for use by game applications. However, roughly half of the bandwidth
on the seventh core is reserved for operating system use.) The Xbox One X also
contains an eight-core APU, but its cores are based on proprietary technology
developed in partnership with AMD, rather than on the Jaguar microarchitec-
ture like its predecessor. Figure 4.18 shows a block diagram of the PS4 hard-
ware architecture, and Figure 4.19 presents a block diagram of the Xbox One
hardware architecture.

### 4.3.3 Symmetric versus Asymmetric Multiprocessing

The symmetry of a parallel computing platform has to do with how the CPU
cores in the machine are treated by the operating system. In symmetric mul-
tiprocessing (SMP), the available CPU cores in the machine (provided by any
combination of hyperthreading, multicore CPUs or multiple CPUs on a single


<!-- source-pdf-page: 247 -->
> Visual fallback for diagrams/images: [PDF page 247](../../../visual_pages/page_0247.jpg)

AMD Jaguar CPU @ 1.6 GHz

CPC 0

CPC 0

Core 0

Core 1

Core 4

Core 5

L1 D$
32 KiB
8-way

L1 I$
32 KiB
2-way

L1 D$
32 KiB
8-way

L1 I$
32 KiB
2-way

L1 D$
32 KiB
8-way

L1 I$
32 KiB
2-way

L1 D$
32 KiB
8-way

L1 I$
32 KiB
2-way

L2 Cache
2 MiB / 16-way

L2 Cache
2 MiB / 16-way

CPU Bus (20 GiB/s)

L1 D$
32 KiB
8-way

L1 I$
32 KiB
2-way

L1 D$
32 KiB
8-way

L1 I$
32 KiB
2-way

L1 D$
32 KiB
8-way

L1 I$
32 KiB
2-way

L1 D$
32 KiB
8-way

L1 I$
32 KiB
2-way

Core 2

Core 3

Core 6

Core 7

snoop
snoop

Cache Coherent
Memory Controller

“Onion” Bus
(10 GiB/s each way)

AMD Radeon GPU
(comparable to 7870)
@ 800 MHz
1152 stream processors

Main RAM
8 GiB GDDR5

“Garlic” Bus
(176 GiB/s)
(non cache-coherent)

Figure 4.18. Simpliﬁed view of the PS4’s architecture.

motherboard) are homogeneous in terms of design and ISA, and are treated
equally by the operating system. Any thread can be scheduled to execute on
any core. (Note, however, that it is possible in such systems to specify an
affinity for a thread, causing it to be more likely, or even guaranteed, to be
scheduled on a particular core.)

The PlayStation 4 and the Xbox One are examples of SMP. Both of these
consoles contain eight cores, of which seven are available for use by the pro-
grammer, and the application is free to run threads on any of the available
cores.

In asymmetric multiprocessing (AMP), the CPU cores are not necessarily ho-
mogeneous, and the operating system does not treat them equally. In AMP,
one “master” CPU core typically runs the operating system, and the other
cores are treated as “slaves” to which workloads are distributed by the master
core.


<!-- source-pdf-page: 248 -->
> Visual fallback for diagrams/images: [PDF page 248](../../../visual_pages/page_0248.jpg)

AMD Jaguar CPU @ 1.75 GHz

CPC 0

CPC 0

Core 0

Core 1

Core 4

Core 5

L1 D$
32 KiB
8-way

L1 I$
32 KiB
2-way

L1 D$
32 KiB
8-way

L1 I$
32 KiB
2-way

L1 D$
32 KiB
8-way

L1 I$
32 KiB
2-way

L1 D$
32 KiB
8-way

L1 I$
32 KiB
2-way

L2 Cache
2 MiB / 16-way

L2 Cache
2 MiB / 16-way

L1 D$
32 KiB
8-way

L1 I$
32 KiB
2-way

L1 D$
32 KiB
8-way

L1 I$
32 KiB
2-way

L1 D$
32 KiB
8-way

L1 I$
32 KiB
2-way

L1 D$
32 KiB
8-way

L1 I$
32 KiB
2-way

30 GiB/s

Core 2

Core 3

Core 6

Core 7

30 GiB/s

Cache Coherent
Memory Access

(cache-coherent)

68 GiB/s

AMD Radeon GPU
(comparable to 7790)
@ 853 MHz
768 stream processors

(non cache-coherent)

Main RAM
8 GiB GDDR3

eSRAM
32 MiB

up to
204 GiB/s

Figure 4.19. Simpliﬁed view of the Xbox One’s architecture.

The cell broadband engine (CBE) used in the PlayStation 3 is an example of
AMP; it employs a main CPU known as the “power processing unit” (PPU)
which is based on the PowerPC ISA, along with eight coprocessors known as
“synergystic processing units” (SPUs) which are based around a completely
different ISA. (See Section 3.5.5 for more on the PS3’s hardware architecture.)

### 4.3.4 Distributed Computing

Yet another way to achieve parallelism in computing is to make use of mul-
tiple stand-alone computers working in concert. This is known as distributed
computing in the most general sense. There are various ways to architect a
distributed computing system, including:

•
computer clusters,
•
grid computing, and


<!-- source-pdf-page: 249 -->
> Visual fallback for diagrams/images: [PDF page 249](../../../visual_pages/page_0249.jpg)

User Processes

OS Processes

Kernel

Drivers

CPU
Memory
Devices

Figure 4.20. The kernel and device drivers sit directly on top of the hardware, and run in privileged
mode. All other operating system software and all user programs are implemented on top of the
kernel and driver layer, and run in a somewhat restricted user mode.

•
cloud computing.

We’ll focus exclusively on parallelism within a single computer in this book,
but you can read more about distributed computing by searching for the above
terms online.

## 4.4 Operating System Fundamentals

Now that we have a solid understanding of the basics of parallel computer
hardware, let’s turn our attention to the services provided by the operating
system that make concurrent programming possible.

### 4.4.1 The Kernel

Modern operating systems handle a wide variety of tasks, across a wide range
of granularities. These range from the handling of keyboard and mouse events
or the scheduling of programs for preemptive multitasking at one end of the spec-
trum, to managing a printer queue or the network stack at the other end. The
“core” of the operating system—the part that handles all of the most funda-
mental and lowest-level operations—is called the kernel. The rest of the oper-
ating system, and all user programs, are built atop the services provided by
the kernel. This architecture is illustrated in Figure 4.20.

4.4.1.1
Kernel Mode versus User Mode

The kernel and its device drives run in a special mode called protected mode,
privileged mode or kernel mode, while all other programs in the system (including
all other parts of the operating system that aren’t part of the kernel) operate
in user mode. As the name suggests, software running in privileged mode has


<!-- source-pdf-page: 250 -->
> Visual fallback for diagrams/images: [PDF page 250](../../../visual_pages/page_0250.jpg)

Figure 4.21. Example of CPU protection rings, showing four rings. The kernel runs in ring 0, de-
vice drivers run in ring 1, trusted programs with I/O permissions run in ring 2, and all other user
programs run in ring 3.

full access to all of the hardware in the computer, whereas user mode software
is restricted in various ways in order to ensure stability of the computer system
as a whole. Software running in user mode can access low-level services only
by making a special kernel call—a request for the kernel to perform a low-level
operation on the user program’s behalf. This ensures that a program can’t
inadvertently or maliciously destabilize the system.
In practice, operating systems may implement multiple protection rings.
The kernel runs in ring 0, which is the most trusted ring and has all possible
privileges within the system. Device drivers might run in ring 1, trusted pro-
grams with I/O permissions might run in ring 2, while all other “untrusted”
user programs run in ring 3. But this is just one example—the number of rings
varies from CPU to CPU and from OS to OS, as does the assignment of subsys-
tems to the various rings. The protection ring concept is illustrated in Figure
4.21.

4.4.1.2
Kernel Mode Privileges

Kernel mode (ring 0) software has access to all of the machine language in-
structions defined by the CPU’s ISA. This includes a powerful subset of in-
structions called privileged instructions. These privileged instructions might
allow certain registers to be modified that are normally off-limits (e.g., to con-
trol virtual memory mapping, or to mask and unmask interrupts). Or they
might allow certain regions of memory to be accessed, or allow other nor-


<!-- source-pdf-page: 251 -->

mally restricted operations to be performed. Examples of privileged instruc-
tions on the Intel x86 processor include wrmsr (write to model-specific regis-
ter) and cli (clear interrupts). By restricting the use of these powerful instruc-
tions only to “trusted” software like the kernel, system stability and security is
improved.
With these privileged ML instructions, the kernel can implement security
measures. For example, the kernel typically locks down certain pages of vir-
tual memory so that they cannot be written to by a user program. Both the
kernel’s software and all of its internal record-keeping data are kept in pro-
tected memory pages. This ensures that a user program won’t stomp on the
kernel, and thereby crash the entire system.

### 4.4.2 Interrupts

An interrupt is a signal sent to the CPU in order to notify it of an important
low-level event, such as a keypress on the keyboard, a signal from a peripheral
device, or the expiration of a timer. When such an event occurs, an interrupt
request (IRQ) is raised. If the operating system wishes to respond to the event,
it pauses (interrupts) whatever processing had been going on, and calls a spe-
cial kind of function called an interrupt service routine (ISR). The ISR function
performs some operation in response to the event (ideally doing so as quickly
as possible) and then control is returned back to whatever program had been
running prior to the interrupt having been raised.
There are two kinds of interrupt: hardware interrupts and software interrupts.
A hardware interrupt is requested by placing a non-zero voltage onto one of
the pins of the CPU. Hardware interrupts might be raised by devices such as
a keyboard or mouse, or by a periodic timer circuit on the motherboard or
within the CPU itself. Because it’s triggered by an external device, a hardware
interrupt can happen at any time—even right in the middle of executing a CPU
instruction. As such, there may be a tiny delay between the moment when a
hardware interrupt is physically raised and when the CPU is in a suitable state
to handle it.
A software interrupt is triggered by software rather than by a voltage on a
CPU pin. It has the same basic effect as a hardware interrupt, in that it causes
the operation of the CPU to be interrupted and a service routine to be called.
A software interrupt can be triggered explicitly by executing an “interrupt”
machine language instruction. Or one may be triggered in response to an erro-
neous condition detected by the CPU while running a piece of software—these
are called traps or sometimes exceptions (although the latter term should not be
confused with language-level exception handling). For example, if an ALU is


<!-- source-pdf-page: 252 -->

instructed to perform a divide-by-zero operation, a software interrupt will be
raised. The operating system normally handles such interrupts by crashing
the program in question and producing a core dump file. However, a debug-
ger attached to the program could catch this interrupt and instead cause the
program to break into the debugger for inspection.

### 4.4.3 Kernel Calls

In order for user software to perform a privileged operation, such as mapping
or unmapping physical memory pages in the virtual memory system or ac-
cessing a raw network socket, the user program must make a request to the
kernel. The kernel responds by performing the operation in a safe manner on
behalf of the user program. Such a request is called a kernel call or system call.
On most systems, calling into the kernel is accomplished via a software
interrupt.2 In the case of an interrupt-triggered system call, the user program
places any input arguments in a specific place (either in memory or in registers)
and then issues a “software interrupt” instruction with an integer argument
that specifies which kernel operation is being requested. This causes the CPU
to be put into a mode with elevated privileges, staves the state of the calling
program, and then causes the appropriate kernel interrupt service routine to
be called. Presuming that the kernel allows the request to proceed, it performs
the requested operation (in privileged mode) and then control is returned to
the caller (after first restoring its execution state). This switch from a user mode
program into the kernel is an example of context switching. See Section 4.4.6.5
for more on context switching.
On most modern operating systems, the user program doesn’t execute a
software interrupt or system call instruction manually, for example via inline
assembly code. That would be messy and error-prone. Instead, a user program
calls a kernel API function, which in turn marshalls the arguments and trips the
software interrupt. This is why system calls appear to be regular function calls
from the point of view of the user program.

### 4.4.4 Preemptive Multitasking

The earliest minicomputers and personal computers ran a single program
at a time. They were inherently serial computers, capable of reading a pro-
gram from a single instruction stream, and executing one instruction from this
stream at a time. The disk operating systems (DOS) in those days weren’t much

2On some systems, a special variant of the call instruction is used to call into the kernel. For
example, on MIPS processors this instruction is named syscall.


<!-- source-pdf-page: 253 -->
> Visual fallback for diagrams/images: [PDF page 253](../../../visual_pages/page_0253.jpg)

Figure 4.22. Three full-screen programs running on the Apple II computer. Programs on the Apple
II were always full-screen because it could only run one program at a time. From left to right:
Copy II Plus, the AppleWorks word processor, and The Locksmith.

more than glorified device drivers, allowing programs to interface with de-
vices such as tape, floppy and hard disk drives. The entire computer would
be devoted to running a single program at a time. Figure 4.22 shows a few
full-screen programs running on an Apple II computer.
As operating systems and computer hardware became more advanced, it
became possible to run more than one program on a serial computer at a time.
On shared mainframe computer systems, a technique known as multiprogram-
ming would allow one program to run while another was waiting for a time-
consuming request to be satisifed by a peripheral device. Classic Mac OS and
versions of Windows prior to Windows NT and Windows 95 used a technique
known as cooperative multitasking, in which only one program would be run-
ning on the machine at a time, but each program would regularly yield the CPU
so that another program could get a chance to run. In this way, each program
ended up with a periodic “slice” of CPU time. Technically, this technique is
known as time division multiplexing (TDM) or temporal multithreading (TMT).
Informally it’s called time-slicing.
Cooperative multitasking suffered from one big problem: Time-slicing re-
quired the cooperation of each and every program in the system. One “rogue”
program could consume all of the CPU’s time if it failed to yield to other
programs periodically. The PDP-6 Monitor and Multics operating systems
solved this problem by introducing a technique known as preemptive multi-
tasking. This technology was later adopted by the UNIX operating system and
all of its variants, along with later versions of Mac OS and Windows.
In preemptive multitasking, programs still share the CPU by time-slicing.
However the scheduling of programs is controlled by the operating system,
not via cooperation between the programs themselves. As a result, each pro-


<!-- source-pdf-page: 254 -->

gram gets a regular, consistent and reliable time slice on the CPU. The time
slice during which one particular program is allowed to run on the CPU is
sometimes called the program’s quantum. To implement preemptive multi-
tasking, the operating system responds to a regularly-timed hardware inter-
rupt in order to periodically context switch between the different programs run-
ning on the system. We’ll see how context switching works in more depth in
the next section (4.4.6.5).
We should note here that preemptive multitasking is used even on mul-
ticore machines, because typically the number of threads is greater than the
number of cores. For example, if we were to have 100 threads and only four
CPU cores, then the kernel would use preemptive multitasking to time-slice
between 25 threads on each core.

### 4.4.5 Processes

A process is the operating system’s way of managing a running instance of a
program contained in an executable file (.exe on Windows, .elf on Linux). A
process only exists while its program is actually running—when an instance
of a program exits, is killed, or crashes, the OS destroys the process associated
with that instance. Multiple processes can be running on a computer system
at any given time. This might include multiple instances of the same program.
Programmers interact with processes via an API provided by the operat-
ing system. The details of this API differ from OS to OS, but the key concepts
are roughly the same across all of them. A complete discussion of any one
operating system’s process API is beyond the scope of this book, but for the
purposes of illustrating the concepts we’ll focus primarily on the API style of
UNIX-like operating systems such as Linux, BSD and MacOS. But we’ll make
note of situations in which Windows or game console operating systems de-
viate significantly from the core ideas of a UNIX-like process API.

4.4.5.1
Anatomy of a Process

Under the hood, a process consists of:

•
a process id (PID) that uniquely identifies the process within the operating
system;

•
a set of permissions, such as which user “owns” each process and to which
user group it belongs;

•
a reference to the process’s parent process, if any,

•
a virtual memory space containing the process’s “view” of physical mem-
ory (see Section 4.4.5.2 for more information);


<!-- source-pdf-page: 255 -->

•
the values of all defined environment variables;
•
the set of all open file handles in use by the process;
•
the current working directory for the process,
•
resources for managing synchronization and communication between pro-
cesses in the system, such as message queues, pipes, and semaphores;
•
one or more threads.

A thread encapsulates a running instance of a single stream of machine lan-
guage instructions. By default, a process contains a single thread. But as we’ll
discuss in depth in Section 4.4.6, more than one thread can be created within
a process, allowing more than one instruction stream to run concurrently. The
kernel schedules all of the threads in the system (from all currently-running
processes) to run on the available cores. It uses preemptive multitasking to
time-slice between threads when there are more threads than there are cores.
We should stress here that threads are the fundamental unit of program exe-
cution within an operating system, not processes. A process merely provides
an environment within which its thread(s) can run, including a virtual mem-
ory map and a set of resources that are used by and shared between all threads
within that process. Whenever a thread is scheduled to run on a core, its pro-
cess becomes active, and that process’s resources and environment become
available for use by the thread while it runs. So when we say that a thread is
running on a core, remember that it is always doing so within the context of
exactly one process.

4.4.5.2
Virtual Memory Map of a Process

You’ll recall from Section 3.5.2 that a program generally never works directly
with physical memory addresses.3 Rather, the program accesses memory in
terms of virtual addresses, and the CPU and operating system cooperate to
remap these virtual addresses into physical addresses. We said that the remap-
ping of virtual to physical addresses happens in terms of contiguous blocks of
addresses called pages, and that a page table is used by the OS to map virtual
page indices to physical page indices.
Every process has its own virtual page table. This means that every pro-
cess has its own custom view of memory. This is one of the primary ways in
which the operating system provides a secure and stable execution environ-
ment. Two processes cannot corrupt each other’s memory, because the physi-
cal pages owned by one process are simply not mapped into the other process’s
address space (unless they explicitly share pages). Also, pages owned by the

3User programs always work in terms of virtual memory addresses, but the kernel can work
directly with physical addresses.


<!-- source-pdf-page: 256 -->

kernel are protected from inadvertent or deliberate corruption by a user pro-
cess because they are mapped to a special range of addresses known as kernel
space which can only be accessed by code running in kernel mode.
A process’s virtual page table effectively defines its memory map. The mem-
ory map typically contains:

•
the text, data and BSS sections as read in from the program’s executable
file;
•
a view of any shared libraries (DLLs, PRXs) used by the program;
•
a call stack for each thread;
•
a region of memory called the heap for dynamic memory allocation;
•
possibly some pages of memory that are shared with other processes; and
•
a range of kernel space addresses which are inaccessible to the process
(but become accessible whenever a kernel call executes).

Text, Data and BSS Sections

When a program is first run, the kernel creates a process internally and as-
signs it a unique PID. It then sets up a virtual page map for the process—in
other words, it creates the virtual address space of the process. It then allocates
physical pages as necessary and maps them into the virtual address space by
adding entries to the process’s page table.
The kernel reads the executable file (text, data and BSS sections) into mem-
ory by allocating virtual pages and loading the data into them. This allows
the program’s code and global data to be “visible” within the process’s virtual
address space. The machine code in an executable file is actually relocatable,
meaning that its addresses are specified as relative offsets rather than abso-
lute memory addresses. These relative addresses are fixed up by the operating
system, meaning they are converted back into real (virtual) addresses, prior to
running the program. (For more on the format of an executable file, see Section
3.3.5.1.)

Call Stack

Every running thread needs a call stack (see Section 3.3.5.2). When a process
is first run, the kernel creates a single default thread for it. The kernel allocates
physical memory pages for this thread’s call stack, and maps them into the
process’ virtual address space so the stack can be “seen” by the thread. The
values of the stack pointer (SP) and base pointer (BP) are initialized to point
to the bottom of the empty stack. Finally, the thread starts executing at the


<!-- source-pdf-page: 257 -->

entry point of the program. (In C/C++ this is typically main(), or WinMain()
under Windows.)

Heap

Processes can allocate memory dynamically via malloc() and free() in C, or
global new and delete in C++. These requests come from a region of memory
called the heap. Physical pages of memory are allocated on demand by the
kernel in order to fulfill dynamic allocation requests; these pages are mapped
into the virtual address space of the process as memory is allocated by it, and
pages whose contents have been completely freed are unmapped and returned
to the system.

Shared Libraries

All non-trivial programs depend on external libraries. A library can be stati-
cally linked into a program, meaning that a copy of the libary’s code is placed
into the executable file itself. Most operating systems also support the con-
cept of shared libraries. In this case, the program contains only references to the
library’s API functions, not a copy of the library’s machine code. Shared li-
braries are called dynamic link libraries (DLL) under Windows. On the PlaySta-
tion 4, the OS supports a kind of dynamically linked library called a PRX.
(Interestingly, the name PRX comes from the PlayStation 3, where it stood
for PPU Relocatable Executable, in reference to the main processor in the PS3
which was called the PPU.)
Shared libraries generally work as follows: The first time a shared library
is needed by a process, the OS loads that library into physical memory, and
maps a view of it into the process’s virtual address space. The addresses of
the functions and global variables provided by the shared library are patched
into the program’s machine code, allowing it to call them as if they had been
statically linked into the executable.
The benefit of shared libraries only becomes evident when a second pro-
cess is run that uses the same shared library. Rather than loading a copy of
the library’s code and global variables, the already-loaded physical pages are
simply mapped into the virtual address space of the new process. This saves
memory and speeds up the process of running all but the first process that uses
a given shared library.
Shared libraries have other benefits, too. For example, a shared library can
be updated, say to fix some bugs, and in theory all programs that use that
shared library will immediately benefit (without having to be relinked and
redistributed to users). That said, in practice updating shared libraries can


<!-- source-pdf-page: 258 -->

inadvertently cause compatibility problems amongst the programs that use
them. This leads to a proliferation of different versions of each shared library
within the system—a situation affectionately known as “DLL hell” amongst
Windows developers. To work around these problems, Windows moved to
a system of manifests that help to guarantee compatibility between shared li-
braries and the programs that use them.

Kernel Pages

On most operating systems, the address space of a process is actually divided
into two large contiguous blocks—user space and kernel space. For example,
on 32-bit Windows, user space corresponds to the address range from address
0x0 through 0x7FFFFFFF (the lower 2 GiB of the address space), while ker-
nel space corresponds to addresses between 0x80000000 and 0xFFFFFFFF (the
upper 2 GiB of the space). On 64-bit Windows, user space corresponds to the
8 TiB range of addresses from 0x0 through 0x7FF’FFFFFFFF, and the gigantic
248 TiB range from 0xFFFF0800’00000000 through 0xFFFFFFFF’FFFFFFFF is
reserved for use by the kernel (although not all of it is actually used).

User space is mapped through a virtual page table that is unique to each
process. However, kernel space uses a separate virtual page table that is shared
between all processes. This is done so that all processes in the system have a
consistent “view” of the kernel’s internal data.

Normally, user processes are prevented from accessing the kernel’s
pages—if they try to do so, a page fault will occur and the program will crash.
However, when a user process makes a system call, a context switch (see Sec-
tion 4.4.6.5) is performed into the kernel. This puts the CPU into privileged
mode, allowing the kernel to access the kernel space address ranges (as well
as the virtual pages of the current process). The kernel runs its code in privi-
leged mode, updates its internal data structures as necessary, and finally puts
the CPU back into user mode and returns control to the user program. For
more details on how user and kernel space memory mapping works under
Windows, search for “virtual address spaces” on https://docs.microsoft.com.

It’s interesting (and a bit frightening!) to note that the recently-discovered
“Meltdown” and “Spectre” exploits make use of a CPU’s out-of-order and spec-
ulative execution logic (respectively) to trick it into accessing data located in
memory pages that are normally protected from a user-mode process. For
more on these exploits and how operating systems are protecting themselves
against them, see https://meltdownattack.com/.


<!-- source-pdf-page: 259 -->
> Visual fallback for diagrams/images: [PDF page 259](../../../visual_pages/page_0259.jpg)

Figure 4.23. A process’s memory map as it might look under 32-bit Windows.

Example Process Memory Map

Figure 4.23 depicts the memory map of a process as it might look under 32-bit
Windows. All of the process’s virtual pages are mapped into user space—
the lower 2 GiB of the address space. The executable files’ text, data and BSS
segments are mapped at a low memory address, followed by the heap in a
higher range, followed by any shared memory pages. The call stack is mapped
at the high end of the user address space. Finally, the operating system’s kernel
pages are mapped into the upper 2 GiB of the address space.

The actual addresses for each of the regions in the memory map aren’t
predictable. This is partly because each program’s segments are of different
sizes and hence will map to different address ranges. Also, the numeric val-
ues of the addresses actually change between runs of the same executable pro-
gram, thanks to a security measure known as address space layout randomization
(ASLR).


<!-- source-pdf-page: 260 -->
> Visual fallback for diagrams/images: [PDF page 260](../../../visual_pages/page_0260.jpg)

### 4.4.6 Threads

A thread encapsulates a running instance of a single stream of machine lan-
guage instructions. Each thread within a process is comprised of:

•
a thread id (TID) which is unique within its process, but may or may not
be unique across the entire operating system;

•
the thread’s call stack—a contiguous block of memory containing the
stack frames of all currently-executing functions;

•
the values of all special- and general-purpose registers4 including the
instruction pointer (IP), which points at the current instruction in the
thread’s instruction stream, the base pointer (BP) and stack pointer (SP)
which define the current function’s stack frame;

•
a block of general-purpose memory associated with each thread, known
as thread local storage (TLS).

By default, a process contains a single main thread and hence executes a
single instruction stream. This thread begins executing at the entry point of
the program—typically the main() function. However, all modern operating
systems are capable of executing more than one concurrent instruction stream
within the context of a single process.
You can think of a thread as the fundamental unit of execution within the op-
erating system. A thread provides the minimum resources required to execute
an instruction stream—a call stack and a set of registers. The process merely
provides the environment in which one or more threads execute. This is illus-
trated in Figure 4.24.

4.4.6.1
Thread Libraries

All operating systems that support multithreading provide a collection of sys-
tem calls for creating and manipulating threads. A few portable thread li-
braries are also available, the best known of which are the IEEE POSIX 1003.1c
standard thread library (pthread) and the C11 and C++11 standard thread li-
braries. The Sony PlayStation 4 SDK provides a set of thread functions prefixed
with sce that map pretty much directly to the POSIX thread API.
The various thread APIs differ in their details, but all of them support the
following basic operations:

1.
Create. A function or class constructor that spawns a new thread.

4Technically a thread’s execution context only encompasses the values of registers that are vis-
ible in user mode; it excludes the values of certain privileged-mode registers.


<!-- source-pdf-page: 261 -->
> Visual fallback for diagrams/images: [PDF page 261](../../../visual_pages/page_0261.jpg)

Process

Thread

Thread

Thread

File
Descriptors

Execution
Context

Execution
Context

Execution
Context

Virtual
Memory

Registers

Registers

Registers

Heap

Call Stack

Call Stack

Call Stack

DLLs

BSS

Data

Text

Other
Resources

Figure 4.24. A process encapsulates the resources required to run one or more threads. Each
thread encapsulates an execution context comprised of the contents of the CPU’s registers and a
call stack.

2.
Terminate. A function that terminates the calling thread.

3.
Request to exit. A function that allows one thread to request another
thread to exit.

4.
Sleep. A function that puts the current thread to sleep for a specified
length of time.

5.
Yield. A function that yields the remainder of the thread’s time slice so
other threads can get a chance to run.

6.
Join. A function that puts the calling thread to sleep until another thread
or group of threads has terminated.

4.4.6.2
Thread Creation and Termination

When an executable file is run, the process created by the OS to encapsulate it
automatically contains a single thread, and execution of this thread begins at
the entry point of the program—in C/C++ this is the special function main().
This “main thread” can spawn new threads if desired, by making calls to
an operating system specific function such as pthread_create() (POSIX
threads), CreateThread() (Windows), or by instantiating an instance of a


<!-- source-pdf-page: 262 -->

thread class such as std::thread (C++11). The new thread begins execu-
tion at an entry point function whose address is provided by the caller.
Once created, a thread will continue to exist until it terminates. The execu-
tion of a thread can terminate in a number of ways:

•
It can end “naturally” by returning from its entry point function. (In the
special case of the main thread, returning from main() not only ends
the thread, but also ends the entire process.)

•
It can call a function such as pthread_exit() to explicitly terminate its
execution before having returned from its entry point function.

•
It can be killed externally by another thread. In this case, the external
thread makes a request to cancel the thread in question, but the thread
may not respond immediately to the request, or it may ignore the request
entirely. The cancelability of a thread is determined when that thread is
created.

•
It can be forcibly killed because its process has ended. (A process termi-
nates when the main thread returns from the main() entry point func-
tion, when any thread calls exit() to kill the process explicitly, or when
an external actor kills the process.)

4.4.6.3
Joining Threads

It’s common for one thread to spawn one or more child threads, do some useful
work of its own, and then wait for the child threads to be done with their work
before continuing. For example, let’s say the main thread wants to perform
1000 computations, and let’s assume further that this program is running on a
quad-core machine. The most efficient approach would be to divide the work
into four equally-sized chunks, and spawn four threads to do the processing
in parallel. Once the computations are complete, let’s assume that the main
thread wants to perform a checksum on the results. The resulting code might
look something like this:

ComputationResult g_aResult[1000];

void Compute(void* arg)
{
uintptr_t startIndex = (uintptr_t)arg;
uintptr_t endIndex = startIndex + 250;
for (uintptr_t i = startIndex; i < endIndex; ++i)
{
g_aResult[i] = ComputeOneResult(...);


<!-- source-pdf-page: 263 -->

}
}

void main()
{
pthread_t
tid[4];

for (int i = 0; i < 4; ++i)
{
const uintptr_t startIndex = i * 250;
pthread_create(&tid[i], nullptr,
Compute, (void*)startIndex);
}

// perhaps do some other useful work...

// wait for computations to be done
for (int i = 0; i < 4; ++i)
{
pthread_join(&tid[i], nullptr);
}

// all threads are done, so we can do our checksum
unsigned checksum = Sha1(g_aResult,
1000*sizeof(ComputationResult));

// ...
}

4.4.6.4
Polling, Blocking and Yielding

Normallly a thread runs until it terminates. But sometimes a running thread
needs to wait for some future event to occur. For example, a thread might need
to wait for a time-consuming operation to complete, or for some resource to
become available. In such a situation, we have three options:

1.
The thread can poll,

2.
it can block, or

3.
it can yield while polling.

Polling

Polling involves a thread sitting in a tight loop, waiting for a condition to be-


<!-- source-pdf-page: 264 -->

come true. It’s a bit like kids in the back seat on a road trip, repeatedly asking,
“Are we there yet? Are we there yet?” Here’s an example:

// wait for condition to become true
while (!CheckCondition())
{
// twiddle thumbs
}

// the condition is now true and we can continue...

It should be obvious that this approach, while simple, has the potential of
burning CPU cycles unnecessarily. This approach is sometimes called a spin-
wait or a busy-wait.

Blocking

If we expect our thread to wait for a relatively long period of time for a con-
dition to become true, busy-waiting is not a good option. Ideally we’d like
to put our thread to sleep so that it doesn’t waste CPU resources, and rely on
the kernel to wake it back up when the condition becomes true at some future
time. This is called blocking the thread.
A thread blocks by making a special kind of operating system call known
as a blocking function. If the condition is already true at the moment a blocking
function is called, the function won’t actually block—it will simply return im-
mediately. But if the condition is false, the kernel will put the thread to sleep,
and add the thread and the condition on which it is waiting into a table. Later,
when the condition becomes true, the kernel uses this internal table to identify
and wake up any threads that are waiting on that condition.
There are all sorts of OS functions that block. Here are a few examples:

•
Opening a file. Most functions that open a file such as fopen() will block
the calling thread until the file has actually been opened (which may take
hundreds or even thousands of cycles). Some functions, like open() un-
der Linux, offer a non-blocking option (O_NONBLOCK) to support asyn-
chronous file I/O.

•
Explicit sleeping.
Some functions explicitly put the calling thread to
sleep for a specified length of time. Variants include usleep() (Linux),
Sleep() (Windows) std::this_thread::sleep_until() (C++11
standard library) and pthread_sleep() (POSIX threads).

•
Joining with another thread. A function such as pthread_join() blocks
the calling thread until the thread being waited on has terminated.


<!-- source-pdf-page: 265 -->

•
Waiting for a mutex lock. Functions like pthread_mutex_wait() at-
tempt to obtain an exclusive lock on a resource via an operating system
object known as a mutex (see Section 4.6). If no other thread holds the
lock, the function grants the lock to the calling thread and returns im-
mediately; otherwise, the calling thread is put to sleep until the lock can
be obtained.

Operating system calls aren’t the only functions that can block. Any user-
space function that ultimately calls a blocking OS function is itself considered
to be a blocking function. It’s a good idea to document such a function, so that
the programmers who use it will know that it has the potential to block.

Yielding

This technique falls part-way between polling and blocking. The thread polls
the condition in a loop, but on each iteration it relinquishes the remain-
der of its time slice by calling pthread_yield() (POSIX), Sleep(0) or
SwitchToThread() (Windows), or an equivalent system call.
Here’s an example:

// wait for condition to become true
while (!CheckCondition())
{
// yield the remainder of my time slice
pthread_yield(nullptr);
}

// the condition is now true and we can continue...

This approach tends to result in fewer wasted cycles and better power con-
sumption than a pure busy-wait loop.
Yielding the CPU still involves a kernel call, and is therefore quite expen-
sive. Some CPUs provide a lightweight “pause” instruction. (For example, on
an Intel x86 ISA with SSE2, the _mm_pause() intrinsic emits such an instruc-
tion.) This kind of instruction reduces the power consumption of a busy-wait
loop by simply waiting for the CPU’s instruction pipeline to empty out before
allowing execution to continue:

// wait for condition to become true
while (!CheckCondition())
{
// Intel SSE2 only:
// reduce power consumption by pausing for ~40 cycles
_mm_pause();


<!-- source-pdf-page: 266 -->
> Visual fallback for diagrams/images: [PDF page 266](../../../visual_pages/page_0266.jpg)

}

// the condition is now true and we can continue...

See https://software.intel.com/en-us/comment/1134767 and http://software.
intel.com/en-us/forums/topic/309231 for an in-depth discussion of how and
why to use a pause instruction in a busy-wait loop.

4.4.6.5
Context Switching

Every thread maintained by the kernel exists in one of three states:5

•
Running. The thread is actively running on a core.

•
Runnable. The thread is able to run, but it is waiting to receive a time slice
on a core.

•
Blocked. The thread is asleep, waiting for some condition to become true.

A context switch occurs whenever the kernel causes a thread to transition from
one of these states to another.
A context switch always happens in privileged mode on the CPU—in re-
sponse to the hardware interrupt that drives preemptive multitasking (i.e.,
transitions between Running and Runnable), in response to an explicit block-
ing kernel call made by a running thread (i.e., a transition from Running
or Runnable to Blocked), or in response to a waited-on condition becoming
true, thus “waking” a sleeping thread (i.e., transitioning it from Blocked to
Runnable). The kernel’s thread state machine is illustrated in Figure 4.25.
When a thread is in the Running state, it is actively making use of a CPU
core. The core’s registers contain information pertinent to the execution of that
thread, such as its instruction pointer (IP), stack and base pointers (SP and BP),
and the contents of various general-purpose registers (GPRs). The thread also
maintains a call stack, which stores local variables and return addresses for
the currently-running function and the entire stack of functions that ultimately
called it. Together, this information is known as the thread’s execution context.
Whenever a thread transitions away from the Running state to either
Runnable or Blocked, the contents of the CPU’s registers are saved to a mem-
ory block that has been reserved for the thread by the kernel. Later, when a
Runnable thread transitions back to the Running state, the kernel repopulates
the CPU’s registers with that thread’s saved register contents.

5Some operating systems make use of additional states, but such states are implementation
details that we can safely ignore for our purposes.


<!-- source-pdf-page: 267 -->
> Visual fallback for diagrams/images: [PDF page 267](../../../visual_pages/page_0267.jpg)

Blocked
Threads
Runnable

Threads

Wake

Thread 1

Thread 4

Thread 0

Thread 6

Running
Threads

Thread 3

Thread 7

Schedule

End of
Quantum
Sleep/Block

Thread 2

Thread 5

Core 0

Core 1

L1 D$
F/D

L1 D$
F/D

Scheduler

L1 I$

Scheduler

L1 I$

Registers

Registers

ALU
FPU
VPU

ALU
FPU
VPU

Figure 4.25. Every thread can be in one of three states: Running, Runnable or Blocked.

We should note here that a thread’s call stack need not be saved or restored
explicitly during a context switch. This is because each thread’s call stack al-
ready resides in a distinct region within its process’ virtual memory map. The
act of saving and restoring the contents of the CPU’s registers includes saving
and restoring the stack and base pointers (SP and BP), and thereby effectively
saves and restores the thread’s call stack “for free.”
During a context switch, if the incoming thread resides in a different pro-
cess from that of the outgoing thread, the kernel also needs to save off the state
of the outgoing process’ virtual memory map, and set up the virtual memory
map of the incoming process. You’ll recall from Section 3.5.2 that a virtual
memory map is defined by a virtual page table. The act of saving and restor-
ing a virtual memory map therefore involves saving and restoring a pointer to
this page table, which is usually maintained in a special privileged CPU reg-
ister. The translation lookaside buffer (TLB) must also be flushed whenever
an inter-process context switch occurs (see Section 3.5.2.4). These additional
steps make context switching between processes more expensive than context
switching between threads within a single process.

4.4.6.6
Thread Priorities and Afﬁnity

For the most part, the kernel handles the job of scheduling threads to run on
the available cores in the machine. However, programmers do have two ways


<!-- source-pdf-page: 268 -->

to affect how threads are scheduled: priority and affinity.
A thread’s priority controls how it is scheduled relative to other Runnable
threads in the system. Higher-priority threads generally take precedence over
lower-priority threads. Different operating systems offer different numbers of
priority levels. For example, Windows threads can belong to one of six priority
classes, and there are seven distinct priority levels within each class. These two
values are combined to produce a total of 32 distinct “base priorities” which
are used when scheduling threads.
The simplest thread scheduling rule is this: As long as at least one higher-
priority Runnable thread exists, no lower-priority threads will be scheduled
to run. The idea behind this approach is that most threads in the system will
be created at some default priority level, and hence will share the process-
ing resources fairly. But once in a while, a higher-priority thread can become
Runnable. When it does, it runs as close to immediately as possible, hopefully
exits after a relatively short period of time, and thereby returns control to all
of the lower-priority threads.
Such a simple priority-based scheduling algorithm can lead to a situation
in which a small number of high-priority threads run continually, thereby pre-
venting any lower-priority threads from running. This is known as starvation.
Some operating systems attempt to mitigate the ill effects of starvation by in-
troducing exceptions to the simple scheduling rule that aim to give at least
some CPU time to starving lower-priority threads.
Another way in which programmers can control thread scheduling is via
a thread’s affinity. This setting requests that the kernel either lock a thread to a
particular core, or that it should at least prefer one or more cores over the others
when scheduling the thread.

4.4.6.7
Thread Local Storage

We said that all threads within a process share the process’s resources, includ-
ing its virtual memory space. There is one exception to this rule—each thread
is given a private memory block known as thread local storage (TLS). This al-
lows threads to keep track of data that shouldn’t be shared with other pro-
cesses. For example, each thread might maintain a private memory allocator.
We can think of the TLS memory block as being a part of the thread’s execution
context.
In practice, TLS memory blocks are usually visible to all threads within
a process. They’re typically not protected, the way operating system virtual
memory pages are. Instead, the OS grants each thread its own TLS block, all
mapped into the process’s virtual address space at different numerical ad-
dresses, and a system call is provided that allows any one thread to obtain


<!-- source-pdf-page: 269 -->
> Visual fallback for diagrams/images: [PDF page 269](../../../visual_pages/page_0269.jpg)

Figure 4.26. The Threads window in Visual Studio is the primary interface for debugging multithreaded programs.

the address of its private TLS block.

4.4.6.8
Thread Debugging

All good debuggers nowadays provide tools for debugging multithreaded ap-
plications. In Microsoft Visual Studio, the Threads Window is the central tool
for this purpose. Whenever you break into the debugger, this window lists
all the threads currently in existence within the application. Double-clicking
on a thread makes its execution context active within the debugger. Once a
thread’s context has been activated, you can walk up and down its call stack
via the Call Stack window, and view local variables within each function’s
scope via the Watch windows. This works even if the thread is in its Runnable
or Blocked state. The Visual Studio Threads window is shown in Figure 4.26.

### 4.4.7 Fibers

In preemptive multitasking, thread scheduling is handled automatically by
the kernel. This is often convenient, but sometimes programmers find it de-
sirable to have control over the scheduling of workloads in their programs.
For example, when implementing a job system for a game engine (discussed in
Section 8.6.4), we might want to allow jobs to explicitly yield the CPU to other
jobs, without worrying about the possibility of preemption “pulling the rug
out” from under our jobs as they run. In other words, sometimes we want to
use cooperative rather than preemptive multitasking.


<!-- source-pdf-page: 270 -->

Some operating systems provide just such a cooperative multitasking
mechanism: They are known as fibers. A fiber is a lot like a thread, in that it
represents a running instance of a stream of machine language instructions. A
fiber has a call stack and register state (an execution context), just like a thread.
However, the big difference is that a fiber is never scheduled directly by the
kernel. Instead, fibers run within the context of a thread, and are scheduled
cooperatively, by each other.
In this section, we’ll talk about Windows fibers specifically. Some other op-
erating systems, such as Sony’s PlayStation 4 SDK, provide very similar fiber
APIs.

4.4.7.1
Fiber Creation and Destruction

How do we convert a thread-based process into a fiber-based one? Every
process starts with a single thread when it first runs; hence processes are
thread-based by default. When a thread calls the function ConvertThreadTo
Fiber(), a new fiber is created within the context of the calling thread. This
“bootstraps” the process so that it can create and schedule more fibers. Other
fibers are created by calling CreateFiber() and passing it the address of
a function that will serve as its entry point. Any running fiber can coopera-
tively schedule a different fiber to run within its thread by calling SwitchTo
Fiber(). When a fiber is no longer needed, it can be destroyed by calling
DeleteFiber().

4.4.7.2
Fiber States

A fiber can be in one of two states: Active or Inactive. When a fiber is in its
Active state, it is assigned to a thread, and executes on its behalf. When a fiber
is in its Inactive state, it is sitting on the sidelines, not consuming the resources
of any thread, just waiting to be activated. Windows calls an Active fiber the
“selected” fiber for a given thread.
An Active fiber can deactivate itself and make another fiber active by call-
ing SwitchToFiber(). This is the only way that fibers can switch between
the Active and Inactive states.
Whether or not an Active fiber is actively executing on a CPU core is de-
termined by the state of its enclosing thread. When an Active fiber’s thread is
in the Running state, that fiber’s machine language instructions are being ex-
ecuted on a core. When an Active fiber’s thread is in the Runnable or Blocked
state, its instructions of course cannot execute, because the entire thread is sit-
ting on the sidelines, either waiting to be scheduled on a core or waiting for a
condition to become true.


<!-- source-pdf-page: 271 -->

It’s important to understand that fibers don’t themselves have a Blocked
state, the way threads do. In other words, it’s not possible to put a fiber to
sleep waiting on a condition. Only its thread can be put to sleep. Because of
this restriction, whenever a fiber needs to wait for a condition to become true,
it either busy-waits or it calls SwitchToFiber in order to yield control to an-
other fiber while it waits. Making a blocking OS call from within a fiber is
usually a pretty big no-no. Doing so would put the fiber’s enclosing thread to
sleep, thereby preventing that fiber from doing anything—including schedul-
ing other fibers to run cooperatively.

4.4.7.3
Fiber Migration

A fiber can migrate from thread to thread, but only by passing through its
Inactive state. As an example, consider a fiber F that is running within the
context of thread A. Fiber F calls SwitchToFiber(G) to activate a different
fiber named G inside thread A. This puts fiber F into its Inactive state (meaning
it is no longer associated with any thread). Now let’s assume that another
thread named B is running fiber H. If fiber H calls SwitchToFiber(F), then
fiber F has effectively migrated from thread A to thread B.

4.4.7.4
Debugging with Fibers

Because fibers are provided by the OS, debugging tools and profiling tools
should be able to “see” them, just the way they can “see” threads. For example,
when debugging on the PS4 using SN Systems’ Visual Studio debugger plug-
in for Clang, fibers automatically show up in the Threads window as if they
were threads. You can double-click a fiber to activate it within the Watch and
Call Stack windows, and then walk up and down its call stack just as you
normally would with a thread.

If you’re considering using fibers in your game engine, it’s a good idea
to check out your debugger’s capabilities on your target platform before you
commit a lot of time and effort to a fiber-based design. If your debugger
and/or target platform doesn’t provide good tools for debugging fibers, that
could be a deal breaker.

4.4.7.5
Further Reading on Fibers

You can read more about Windows fibers here: https://msdn.microsoft.com/
en-us/library/windows/desktop/ms682661(v=vs.85).aspx.


<!-- source-pdf-page: 272 -->

### 4.4.8 User-Level Threads and Coroutines

Both threads and fibers tend to be rather “heavy weight” because these fa-
cilities are provided by the kernel.
This implies that most functions that
you’d call to manipulate threads or fibers involve a context switch into kernel
space—not a cheap operation.
But there are lighter-weight alternatives to
threads and fibers. These mechanisms allow programmers to code in terms
of multiple independent flows of control, each with its own execution context,
but without the high cost of making kernel calls. Collectively, these facilities
are known as user-level threads.
User-level threads are implemented entirely in user space.
The kernel
knows nothing about them. Each user-level thread is represented by an or-
dinary data structure that keeps track of the thread’s id, possibly a human-
readable name, and execution context information (the contents of CPU reg-
isters and a call stack). A user-level thread library provides API functions for
creating and destroying threads, and context switching between them. Each
user-level thread runs within the context of a “real” thread or fiber that has
been provided by the operating system.
The trick to implementing a user-level thread library is figuring out how to
implement a context switch. If you think about it, a context switch mostly boils
down to swapping the contents of the CPU’s registers. After all, the registers
contain all of the information needed to describe a thread’s execution context—
including the instruction pointer and the call stack. So by writing some clever
assembly language code, it’s possible to implement a context switch. And once
you have a context switch, the rest of your user-level thread library is nothing
more than data management.
User-level threads aren’t supported very well in C and C++, but some
portable and non-portable solutions do exist.
POSIX provided a collec-
tion of functions for managing lightweight thread execution contexts via its
ucontext.h header file (https://en.wikipedia.org/wiki/Setcontext), but this
API has since been deprecated. The C++ Boost library provides a portable
user-level thread library. (Search for “context” on http://www.boost.org/ for
documentation on this library.)

4.4.8.1
Coroutines

Coroutines are a particular type of user-level thread that can prove very useful
for writing inherently asynchronous programs, like web servers—and games!
A coroutine is a generalization of the concept of a subroutine. Whereas a sub-
routine can only exit by returning control to its caller, a coroutine can also
exit by yielding to another coroutine. When a coroutine yields, its execution


<!-- source-pdf-page: 273 -->

context is maintained in memory. The next time the coroutine is called (by
being yielded to by some other coroutine) it continues from where it left off.
Subroutines call each other in a heirarchical fashion. Subroutine A calls
B, which calls C, which returns to B, which returns to A. But coroutines call
each other symmetrically. Coroutine A can yield to B, which can yield to A,
ad infinitum. This back and forth calling pattern doesn’t lead to an infinitely
deepening call stack, because each coroutine maintains its own private execu-
tion context (call stack and register contents). Thus yielding from coroutine
A to coroutine B acts more like a context switch between threads than like a
function call. But because coroutines are implemented with user-level threads,
these context switches are very efficient.
Here’s a pseudocode example of a system in which one coroutine continu-
ally produces data that is consumed by another coroutine:

Queue g_queue;

coroutine void Produce()
{
while (true)
{
while (!g_queue.IsFull())
{
CreateItemAndAddToQueue(g_queue);
}
YieldToCoroutine(Consume);

// continues from here on next yield...
}
}

coroutine void Consume()
{
while (true)
{
while (!g_queue.IsEmpty())
{
ConsumeItemFromQueue(g_queue);
}
YieldToCoroutine(Produce);

// continues from here on next yield...
}
}

Coroutines are most often provided by high-level languages like Ruby, Lua


<!-- source-pdf-page: 274 -->

and Google’s Go. It’s also possible to use coroutines in C or C++. The C++
Boost library provides a solid implementation of coroutines, but Boost requires
you to compile and link against a pretty huge codebase. If you want something
leaner, you may want to try rolling your own coroutine library. The following
blog post by Malte Skarupke demonstrates that doing so isn’t quite as onerous
a task as you might at first imagine: https://probablydance.com/2013/02/20/
handmade-coroutines-for-windows/.

4.4.8.2
Kernel Threads versus User Threads

The term “kernel thread” has two very different meanings, and this can be-
come a major source of confusion as you read more about multithreading. So
let’s demystify the term. The two definitions are as follows:

1.
On Linux, a “kernel thread” is a special kind of thread created for internal
use by the kernel itself, which runs only while the CPU is in privileged
mode. The kernel also creates threads for use by processes (via an API
such as pthread or C++11’s std::thread). These threads run in user
space within the context of a process. In this sense of the term, any thread
that runs in privileged mode is a kernel thread, and any thread that runs
in user mode (in the context of a single-threaded or multithreaded pro-
cess) is a “user thread.”

2.
The term “kernel thread” can also be used to refer to any thread that
is known to and scheduled by the kernel. Using this definition, a kernel
thread can execute in either kernel space or user space, and the term
“user thread” only applies to a flow of control that is managed entirely
by a user-space program without the kernel being involved at all, such as
a coroutine.

Using definition #2, a fiber blurs the line between “kernel thread” and “user
thread.” On the one hand, the kernel is aware of fibers and maintains a sepa-
rate call stack for each one. On the other hand, a fiber is not scheduled by the
kernel—it can run only when another fiber or thread explicitly hands control
to it via a call such as SwitchToFiber().

### 4.4.9 Further Reading on Processes and Threads

We’ve covered the basics of processes, threads and fibers in the preceding sec-
tions, but really we’ve only just scratched the surface. For more information,
check out some of the following websites:


<!-- source-pdf-page: 275 -->

•
For an introduction to threads, see https://www.cs.uic.edu/~jbell/
CourseNotes/OperatingSystems/4_Threads.html.

•
The full pthread API docs are available online; just search for “pthread
documentation.”

•
For documentation on the Windows thread API, seach for “Process and
Thread Functions” on https://msdn.microsoft.com/.

•
For more information on thread scheduling, search for Nikita Ishkov’s
“A Complete Guide to Linux Process Scheduling” online.

•
For a great introduction to Go’s implementation of coroutines (which are
known as “goroutines”), watch this presentation by Rob Pike: https://
www.youtube.com/watch?v=f6kdp27TYZs.

## 4.5 Introduction to Concurrent Programming

There’s a wide range of explicitly parallel computing hardware out there, but
how can we take advantage of it as programmers? The answer lies in con-
current programming techniques. In concurrent software, a workload is broken
down into two or more flows of control that can run semi-independently. As
we saw in Section 4.1, in order for a system to qualify as concurrent, it must
involve multiple readers and/or multiple writers of shared data.
Rob Pike, a Distinguished Engineer at Google Inc. who specializes in dis-
tributed and concurrent systems and programming languages, defines con-
currency as “the composition of independently executing computations.” This
definition underscores the idea that the multiple flows of control in a concur-
rent system normally operate semi-independently, but their computations are
composed by sharing data and by synchronizing their operations in various ways.
Concurrency can take many forms. Some examples include:

•
a piped chain of commands running under Linux or Windows, such as
cat render.cpp | grep "light",

•
a single process comprised of multiple threads that share a virtual mem-
ory space and operate on a common dataset,

•
a thread group comprised of thousands of threads running on a GPU, all
cooperating to render a scene,

•
a multiplayer video game, sharing a common game state between clients
running on multiple PCs or game consoles.


<!-- source-pdf-page: 276 -->

### 4.5.1 Why Write Concurrent Software?

Concurrent programs are sometimes written because a model of multiple
semi-independent flows of control simply matches the problem better than
a single flow-of-control design. A concurrent design might also be chosen to
best make use of a multicore computing platform, even if the problem at hand
might be more naturally suited to a sequential design.

### 4.5.2 Concurrent Programming Models

In order for the various threads within a concurrent program to cooperate,
they need to share data, and they need to synchronize their activities. In other
words, they need to communicate. There are two basic ways in which concur-
rent threads can communicate:

•
Message passing. In this communication mode, concurrent threads pass
messages between one another in order to share data and synchronize
their activities. The messages might be sent across a network, passed
between processes using a pipe, or transmitted via a message queue in
memory that is accessible to both sender and receiver. This approach
works both for threads running on a single computer (either within a
single process or across multiple processes), and for threads within pro-
cesses running on physically distinct computers (e.g., a computer cluster
or a grid of machines spread across the globe).

•
Shared memory. In this communication mode, two or more threads are
granted access to the same block of physical memory, and can therefore
operate directly on any data objects residing in that memory area. Di-
rect access to shared memory only works when all threads are running
on a single computer with a bank of physical RAM that can be “seen”
by all CPU cores. Threads within a single process always share a vir-
tual address space, so they can share memory “for free.” Threads within
different processes can also share memory by mapping certain physical
memory pages into all of the processes’ virtual address spaces.

It’s interesting to note that the illusion of shared memory between phys-
ically separate computers can be implemented on top of a message-passing
system—this technique is known as distributed shared memory. Likewise, a
message-passing mechanism can be implemented on top of a shared mem-
ory architecture, by implementing a message queue that resides in the shared
memory pool.


<!-- source-pdf-page: 277 -->

Each approach has its pros and cons. Physically-shared memory is the most
efficient way to share a large amount of data, because that data doesn’t have
to be copied for transmission between threads. On the other hand, as we’ll
see in Sections 4.5.3 and 4.7, the sharing of resources of any kind (memory or
other resources) brings with it a host of synchronization problems that tend
to be difficult to reason about, and are very tricky to account for in a manner
that guarantees correctness of the program. A message-passing design tends
to lessen the impacts of (but not eliminate) these kinds of problems.
In this book, we’ll focus primarily on shared memory concurrency. We do
this for two reasons: First, this is the kind of concurrency you’re most likely to
encounter as a game progammer, because game engines are typically imple-
mented as single-process multithreaded programs. (Networked multiplayer
games are a notable exception to this rule, since they make heavy use of mes-
sage passing.) Second, shared memory concurrency is a more difficult topic to
get your head around. Once you understand concurrency within a shared
memory environment, message passing techniques should prove relatively
easy to learn.

### 4.5.3 Race Conditions

A race condition is defined as any situation in which the behavior of a program
is dependent on timing. In other words, in the presence of a race condition,
the behavior of the program can change when the relative sequence of events
occurring across the system changes, due to variability in the lengths of time
taken by the various flows of control to perform their tasks.

4.5.3.1
Critical Races

Sometimes race conditions are innocuous—the behavior of the program may
change somewhat depending on timing, but the race causes no ill effects. On
the other hand, a critical race is a race condition that has the potential to cause
incorrect program behavior.
The kinds of bugs caused by critical races often seem “strange” or even
“impossible” to programmers who aren’t experienced with them. Examples
include:

•
intermittent or seemingly random bugs or crashes,

•
incorrect results,

•
data structures that get into corrupted states,

•
bugs that magically disappear when you switch to a debug build,


<!-- source-pdf-page: 278 -->
> Visual fallback for diagrams/images: [PDF page 278](../../../visual_pages/page_0278.jpg)

•
bugs that are around for a while, and then go away for a few days, only
to return again (usually the night before E3!),

•
bugs that go away when logging (a.k.a., “printf() debugging”) is
added to the program in an attempt to discover the source of the prob-
lem.

Programmers often call these kinds of issues Heisenbugs.

4.5.3.2
Data Races

A data race is a critical race condition in which two or more flows of control
intefere with one another while reading and/or writing a block of shared data,
resulting in data corruption. Data races are the central problem of concurrent
programming. Writing concurrent programs always boils down to eliminating
data races, either by carefully controlling access to shared data, or by replacing
shared data with private, independent copies of data (thereby transforming a
concurrency problem into a sequential programming problem).
To better understand data races, consider the following simple snippet of
C/C++ code:

int g_count = 0;

inline void IncrementCount()
{
++g_count;
}

If you compiled this code for an Intel x86 CPU and viewed the disassembly, it
would look something like this:

mov
eax,[g_count]
; read g_count into register EAX
inc
eax
; increment the value
mov
[g_count],eax
; write EAX back into g_count

This is an example of a read-modify-write (RMW) operation.
Now imagine that two threads A and B were both to call the Increment
Count() function concurrently (either in parallel or via preemptive multi-
threading). Under normal operation, if each thread called the function exactly
once, we’d expect the final value of g_count to be 2, because either thread A
increments g_count and then thread B increments it, or vice-versa. This is
illustrated in Table 4.1.
Next let’s consider the case of our two threads running on a single-core
machine with preemptive multitasking. Let’s say that thread A runs first, and


<!-- source-pdf-page: 279 -->
> Visual fallback for diagrams/images: [PDF page 279](../../../visual_pages/page_0279.jpg)

Thread A
Thread B
Value of

Action
EAX
Action
EAX
g_count

?
?
0

Read
0
?
0

Increment
1
?
0

Write
1
?
1

1
Read
1
1

1
Increment
2
1

1
Write
2
2

Table 4.1. Example of correct operation of a simple two-threaded concurrent program. First thread
A reads the contents of a shared variable, increments the value, and writes the results back into
the shared variable. At a later time, thread B does the same steps. The ﬁnal value of the shared
variable is 2 as expected.

Thread A
Thread B
Value of

Action
EAX
Action
EAX
g_count

?
?
0

Read
0
?
0

0
Read
0
0

0
Increment
1
0

0
Write
1
1

Increment
1
1
1

Write
1
1
1

Table 4.2. Example of a race condition. Thread A reads the value of the shared variable, but then
is preempted by thread B, which also reads the (same) value. By the time both threads have incre-
mented the value and written their results back to the shared variable, the global variable contains
the incorrect value 1 instead of the exected value of 2.

has just finished executing the first mov instruction when a context switch to
thread B occurs. Instead of thread A executing its inc instruction, thread B
runs its first mov instruction. After some time thread B’s quantum expires,
and the kernel context switches back to thread A, which continues where it
left off and executes the inc instruction. Table 4.2 illustrates what happens.
Hint: It’s not good! The final value of g_count is no longer 2 as it should be.


<!-- source-pdf-page: 280 -->
> Visual fallback for diagrams/images: [PDF page 280](../../../visual_pages/page_0280.jpg)

A
d
a
e
r
h
T
B
d
a
e
r
h
T
A
d
a
e
r
h
T

Core

modify
read
write
modify
read
write
...

Time

Thread A

...
Core 0

modify
read
write

Thread B

Core 1

modify
read
write

...

Time

Thread A

...
Core 0

modify
read
write

Thread B

Core 1

modify
read
write

...

Time

Figure 4.27. Three ways in which a data race can occur within a read-modify-write operation. Top:
Two threads racing on a single CPU core. Middle: Two threads overlapping on two separate cores
and offset by one instruction. Bottom: Two threads overlapping in perfect synchronization on
two cores.

If we run our two threads on parallel hardware, a similar bug can occur, al-
though for a slightly different reason. As in the single-core case, we might get
lucky: The two read-modify-write operations might not overlap at all, and the
result will be correct. However, if the two read-modify-write operations over-
lap, either offset from one another or in perfect synchronization, both threads
can end up loading the same value of g_count into their respective EAX reg-
isters. Both will increment the value, and both will write it to memory. One
thread will overwrite the results of the other, but it doesn’t really matter—
because they both loaded the same initial value, the final value of g_count
will end up being incorrect, just as it was in the single-core scenario. The three
data race scenarios (preemption, offset overlap, and perfect synchronization)
are illustrated in Figure 4.27.

### 4.5.4 Critical Operations and Atomicity

Whenever one operation is interrupted by another, we have the potential for
a data race bug. However, not all interruptions actually cause bugs. For ex-
ample, if a thread is performing an operation on a chunk of data that can only
be “seen” by that one thread, no data race can occur. Such an operation can
be interrupted at any moment by any other operation without consequence.


<!-- source-pdf-page: 281 -->
> Visual fallback for diagrams/images: [PDF page 281](../../../visual_pages/page_0281.jpg)

B
A
Thread 0

D
C
Thread 1

F
E
Thread 2

Figure 4.28. Because each step in an algorithm takes a ﬁnite amount of time to be performed, it
becomes difﬁcult to answer questions about the relative ordering of the steps in a multithreaded
program. For example, does operation B happen before or after operation C?

Likewise, if an operation on one data object is interrupted by an operation on a
different object, there’s no way for those two operations to interefere with one
another,6 and hence no data race bugs are possible. Data race bugs only occur
when an operation on a shared object is interrupted by another operation on
that same object. So we can only talk meaningfully about data races in relation
to a particular shared data object.
Let’s use the term critical operation to refer to any operation that can possi-
bly read or mutate one particular shared object. To guarantee that the shared
object is free from data race bugs, we must ensure that none of its critical op-
erations can interrupt one another. When a critical operation is made uninter-
ruptable in this manner, it is called an atomic operation. Alternatively, we can
say that such an operation has the property of atomicity.

4.5.4.1
Invocation and Response

When we first learn to program, we’re usually taught that the time required
to perform each step in an algorithm is not relevant to the correctness of the
algorithm—all that matters is that the steps are performed in the proper order.
This simple model works well for sequential (single-threaded) programs. But
in the presence of multiple threads, it’s not possible to define the order of a set
of operations when those operations each have a finite duration. This idea is
illustrated in Figure 4.28.
In a concurrent system, the only way to define the notion of order is to
restrict ourselves to talking about instantaneous events. Given any pair of in-
stantaneous events, there are only three possibilities: event A happens before
event B, event A happens after event B, or the two events are simultaneous.
(Perfectly simultaneous events are rare, but they can occur in a multicore com-
puter in which some or all cores share a synchronized clock.)
Any operation with a finite duration can be broken down into two instan-
taneous events—its invocation (the moment at which the operation begins) and

6This is only strictly true if the two objects reside on different cache lines.


<!-- source-pdf-page: 282 -->
> Visual fallback for diagrams/images: [PDF page 282](../../../visual_pages/page_0282.jpg)

Figure 4.29. Any code snippet that includes a critical operation can be partitioned into three sec-
tions. The critical operation itself is bounded above by its invocation, and below by its response.

Figure 4.30. Another example, this time in assembly language, of a code snippet partitioned into
three sections, bounded by the invocation and response of a critical operation.

its response (the moment at which it is considered to be complete). When we
look at any code snippet that includes a critical operation on some shared data
object, we can thus divide it into three sections, with the instantaneous invoca-
tion and response events demarking the boundaries between them. Note that
we’re talking here about the order in which events occur as they were written in
the source code—this is known as program order.

•
Preamble section: All code that occurs before the critical operation’s invo-
cation, in program order.

•
Critical section: The code that comprises the critical operation itself.

•
Postamble section: All code that occurs after the critical operation’s re-
sponse, in program order.

This notion of partitioning a block of code into three sections is illustrated in
Figures 4.29 and 4.30.


<!-- source-pdf-page: 283 -->
> Visual fallback for diagrams/images: [PDF page 283](../../../visual_pages/page_0283.jpg)

4.5.4.2
Atomicity Deﬁned

As we saw in Section 4.5.3.2, a data race bug can occur when a critical operation
is interrupted by another critical operation on the same shared object. This can
happen:

•
when one thread preempts another on a single core, or
•
when two or more critical operations overlap across multiple cores.

Thinking in terms of invocation and response, we can pin down the general
notion of interruption a bit more precisely: An interruption occurs whenever
the invocation and/or response of one operation occurs between the invocation
and response of another operation. But as we’ve said, not all interruptions lead
to data race bugs. A critical operation on a particular shared object can only be
affected by a data race if its invocation and response are interrupted by another
critical operation on that same object. Therefore, we can define the atomicity of
a critical operation as follows:

A critical operation can be said to have executed atomically if its in-
vocation and response are not interrupted by another critical op-
eration on that same object.

We should stress here that it’s perfectly fine for a critical operation to be
interrupted by other noncritical operations, or by critical operations affecting
other unrelated data objects. Only when two critical operations on the same
object interrupt one another does a data race bug occur. Figure 4.31 illustrates
various cases—one in which a critical operation succeeds in executing atomi-
cally, and three in which it does not.
We can guarantee that a critical operation will be executed atomically if we
can make it appear, from the point of view of all other threads in the system,
to have occurred instantaneously. In other words, it must appear as though the
invocation and response of the operation are simultaneous, or that the critical
operation itself has a zero duration. That way, there can be no possibility of
another critical operation’s invocation or response “sneaking in” between the
invocation and response of the operation in question.

4.5.4.3
Making an Operation Atomic

But how can we transform a critical operation into an atomic operation? The
easiest and most reliable way to accomplish this is to use a special object called
a mutex. A mutex is an object provided by the operating system that acts like a
padlock, in that it can be locked and unlocked by a thread. Given two critical


<!-- source-pdf-page: 284 -->
> Visual fallback for diagrams/images: [PDF page 284](../../../visual_pages/page_0284.jpg)

Atomic

A

RA
IA

B

C

RB
(before IA)

IC
(after RA)

Non-Atomic

A

RA
IA

B

C

D

RB
(after IA)

RC
(after IA)
IC
(before RA)

ID
(before RA)

Figure 4.31. Top: Critical operation A can be said to have executed atomically, because it was not
interrupted by any other invocations or responses from critical operations on the same shared
object. Bottom: Three scenarios in which critical operation A executed nonatomically because
it was interrupted by the invocation and/or response of another critical operation on the same
object.

operations on a particular shared data object, we guard the invocation of each
operation with the acquistion of the mutex, and release the mutex at each one’s
response. Because the OS guarantees that a mutex can only be acquired by
one thread at a time, we can thus be certain that the invocation or response
of one operation can never happen in between the invocation and response
of the other. From the point of view of the global ordering of the events in a
concurrent system, a critical operation guarded with a mutex lock appears to
be instantaneous.
Mutexes are part of a collection of concurrency tools provided by the op-
erating system known as thread synchronization primitives. We’ll explore thread
synchronization primitives in Section 4.6.

4.5.4.4
Atomicity as Serialization

Consider a group of threads, all attempting to perform a single operation on a
shared data object. Without atomicity, these operations might happen simul-
taneously, or they might overlap in all sorts of unpredictable ways over time.
This is illustrated in Figure 4.32.
However, making the operation atomic guarantees that only one thread
will ever be performing it at any given moment in time. This has the effect
of serializing the operations—what used to be a jumble of overlapping opera-


<!-- source-pdf-page: 285 -->
> Visual fallback for diagrams/images: [PDF page 285](../../../visual_pages/page_0285.jpg)

B
A
0

C
1

D
2

E
3

Figure 4.32. Without atomicity, operations performed by multiple threads can overlap in unpre-
dictable ways over time.

B
A
0

C
1

D
2

E
3

Figure 4.33. By wrapping each critical operation in a mutex lock-unlock pair, we force the opera-
tions to execute sequentially.

tions is transformed into an orderly sequential sequence of atomic operations.
Making an operation atomic gives us no control over what the order will end
up being; all we can say for certain is that the operations will be performed in
some sequential order. This idea is illustrated in Figure 4.33.

4.5.4.5
Data-Centric Consistency Models

The concepts of atomicity and the serialization of operations within a concur-
rent system are part of a larger topic known as data-centric consistency models.
A consistency model is a contract between a data store, such as a shared data
object in a concurrent system or a database in a distributed system, and a col-
lection of threads that share that data store. It makes reasoning about the be-
havior of the data store easier—as long as the threads follow the rules of the
contract, the programmer can be certain that the data store will behave in a
consistent and predictable manner, and its data will not become corrupted.
A data store that provides a guarantee of atomicity can be said to be lin-
earizable. The topic of data-centric consistency is a bit beyond our scope here,
but you can read more about it online. Here are a few good places to start:


<!-- source-pdf-page: 286 -->

•
Search for “consistency model” and “linearizability” on Wikipedia;

•
https://www.cse.buffalo.edu/~stevko/courses/cse486/spring13/
lectures/26-consistency2.pdf;

•
http://www.cs.cmu.edu/~srini/15-446/S09/lectures/
10-consistency.pdf.

## 4.6 Thread Synchronization Primitives

Every operating system that supports concurrency provides a suite of tools
known as thread synchronization primitives. These tools provide two services to
concurrent programmers:

1.
The ability to share resources between threads by making critical opera-
tions atomic.

2.
The ability to synchronize the operation of two or more threads:

a.
by enabling a thread to go to sleep while it waits for a resource to
become available or for one or more other threads to complete a
task, and

b.
by enabling a running thread to notify one or more sleeping threads
by waking them up.

We should note here that while these thread synchronization primitives are
robust and relatively easy to use, they are generally quite expensive. This is
because these tools are provided by the kernel. Interacting with any of them
therefore requires a kernel call, which involves a context switch into protected
mode. Such context switches can cost upwards of 1000 clock cycles. Because
of their high cost, some concurrent programmers prefer to implement their
own atomicity and synchronization tools, or they turn to lock-free programming
to improve the efficiency of their concurrent software. Nevertheless, a solid
understanding of these synchronization primitives is an important part of any
concurrent programmer’s toolkit.

### 4.6.1 Mutexes

A mutex is an operating system object that allows critical operations to be
made atomic.
A mutex can be in one of two states:
unlocked or locked.
(These two states are sometimes called released and acquired, or signaled and
nonsignaled, respectively.)


<!-- source-pdf-page: 287 -->

The most important property of a mutex is that it guarantees that only one
thread will ever be holding a lock on it at any given time. So, if we wrap all
critical operations for a particular shared data object in a mutex lock, those op-
erations become atomic relative to one another. In other words, the operations
become mutually exclusive. This is where the name “mutex” comes from—it’s
short for “mutual exclusion.”
A mutex is represented either by a regular C++ object or by a handle to an
opaque kernel object. Its API is typically comprised of the following functions:

1.
create() or init(). A function call or class constructor that creates
the mutex.
2.
destroy(). A function call or destructor that destroys the mutex.
3.
lock() or acquire(). A blocking function that locks the mutex on be-
half of the calling thread, but puts the thread to sleep (see Section 4.4.6.4)
if the lock is currently held by another thread.
4.
try_lock() or try_acquire().
A non-blocking function that at-
tempts to lock the mutex, but returns immediately if the lock cannot be
acquired.
5.
unlock() or release(). A non-blocking function that releases the
lock on the mutex.
In most operating systems, only the thread that
locked a mutex is permitted to unlock it.

When a mutex is locked by a thread in the system, we say it is in a non-
signaled state. When the thread releases the lock, the mutex becomes signaled.
If one or more other threads is asleep (blocked) waiting on the mutex, the act
of signaling it causes the kernel to select one of these waiting threads and
wake it up. In some operating systems, it’s possible for a thread to explic-
itly wait for a kernel object such as a mutex to become signaled. Under Win-
dows, the WaitForSingleObject() and WaitForMultipleObjects()
OS calls serve this purpose.

4.6.1.1
POSIX

Now that we have an understanding of how mutexes work, let’s take a look at
a few examples. The POSIX thread library exposes kernel mutex objects via a
C-style functional interface. Here’s how we’d use it to turn our shared counter
example from Section 4.5.3.2 into an atomic operation:

#include <pthread.h>

int g_count = 0;
pthread_mutex_t g_mutex;


<!-- source-pdf-page: 288 -->

inline void IncrementCount()
{
pthread_mutex_lock(&g_mutex);
++g_count;
pthread_mutex_unlock(&g_mutex);
}

Note that in the interest of clarity and brevity, we’ve omitted the code, nor-
mally executed by the main thread, that calls pthread_mutex_init() to
initialize the mutex prior to spawning the threads that will use it, and that calls
pthread_mutex_destroy() to destroy the mutex once all other threads
have exited.

4.6.1.2
C++ Standard Library

Starting with C++11, the C++ standard library exposes kernel mutexes via the
class std::mutex. Here’s how we’d use it to make the increment of a shared
counter atomic:

#include <mutex>

int g_count = 0;
std::mutex g_mutex;

inline void IncrementCount()
{
g_mutex.lock();
++g_count;
g_mutex.unlock();
}

The constructor and destructor of the std::mutex class handles the initial-
ization and destruction of the underlying kernel mutex object, making it a little
bit easier to use than pthread_mutex_t.

4.6.1.3
Windows

Under Windows, a mutex is represented by an opaque kernel object and refer-
enced through a handle. A mutex is “locked” by waiting for it to become sig-
naled, using the general-purpose WaitForSingleObject() function. Un-
locking a mutex is accomplished by calling ReleaseMutex(). Rewriting our
simple example using Windows mutexes, and again omitting the details of the
creation and destruction of the mutex object, we arrive at the following code:


<!-- source-pdf-page: 289 -->

#include <windows.h>

int g_count = 0;
HANDLE g_hMutex;

inline void IncrementCount()
{
if (WaitForSingleObject(g_hMutex, INFINITE)
== WAIT_OBJECT_0)
{
++g_count;
ReleaseMutex(g_hMutex);
}
else
{
// learn to deal with failure...
}
}

### 4.6.2 Critical Sections

In most operating systems, a mutex can be shared between processes. As such,
it is a data structure that is managed internally by the kernel. This means
that all operations performed on a mutex involve a kernel call, and hence a
context switch into protected mode on the CPU. This makes mutexes relatively
expensive, even when no other threads are contending for the lock.
Some operating systems provide less-expensive alternatives to a mutex.
For example, Microsoft Windows provides a locking mechanism known as a
critical section. The terminology and API look a bit different to that of a mutex,
but a critical section under Windows is really just a low-cost mutex.
The API of a critical section looks like this:

1.
InitializeCriticalSection(). Constructs a critical section ob-
ject.

2.
DeleteCriticalSection(). Destroys an initialized critical section
object.

3.
EnterCriticalSection(). A blocking function that locks a critical
section on behalf of the calling thread, but busy-waits or puts the thread
to sleep if the lock is currently held by another thread.

4.
TryEnterCriticalSection().
A non-blocking function that at-
tempts to lock a critical section, but returns immediately if the lock can-
not be acquired.


<!-- source-pdf-page: 290 -->

5.
LeaveCriticalSection(). A non-blocking function that releases the
lock on a critical section object.

Here’s how we’d implement an atomic increment using the Windows crit-
ical section API:

#include <windows.h>

int g_count = 0;
CRITICAL_SECTION g_critsec;

inline void IncrementCount()
{
EnterCriticalSection(&g_critsec);
++g_count;
LeaveCriticalSection(&g_critsec);
}

As before, we’ve omitted some details. The main thread normally initializes
the critical section prior to spawning the threads that use it, and would of
course clean it up once the threads have all exited.
How is the low cost of a critical section achieved? When a thread first at-
tempts to enter (lock) a critical section that is already locked by another thread,
an inexpensive spin lock is used to wait until the other thread has left (unlocked)
that critical section. A spin lock does not require a context switch into the ker-
nel, making it a few thousand clock cycles cheaper than a mutex. Only if the
thread busy-waits for too long is the thread put to sleep, as it would be with a
regular mutex. This less-expensive approach works because, unlike a mutex, a
critical section cannot be shared across process boundaries. We’ll discuss spin
locks in more depth in Section 4.9.7.
Some other operating systems provide “cheap” mutex variants as well. For
example, Linux supports a thing called a “futex” that acts somewhat like a
critical section under Windows. Its use is beyond our scope here, but you can
read more about futexes at https://www.akkadia.org/drepper/futex.pdf.

### 4.6.3 Condition Variables

In concurrent programming, we often need to send signals between threads
in order to synchronize their activities. One example of this is the ubiquitous
producer-consumer problem which we introduced in Section 4.4.8.1. In this
problem, we have two threads: A producer thread calculates or otherwise gen-
erates some data, and that data is read and put to use by a consumer thread.
Obviously the consumer thread cannot consume the data until the producer


<!-- source-pdf-page: 291 -->

has produced it. Hence the producer thread needs a way to notify the con-
sumer that its data is ready for consumption.
We could consider using a global Boolean variable as a signalling mech-
anism. The following code snippet illustrates the idea, using POSIX threads.
(Some details have been omitted for clarity.)

Queue
g_queue;
pthread_mutex_t g_mutex;
bool
g_ready = false;

void* ProducerThread(void*)
{
// keep on producing forever...
while (true)
{
pthread_mutex_lock(&g_mutex);

// fill the queue with data
ProduceDataInto(&g_queue);

g_ready = true;
pthread_mutex_unlock(&g_mutex);

// yield the remainder of my timeslice
// to give the consumer a chance to run
pthread_yield();
}
return nullptr;
}

void* ConsumerThread(void*)
{
// keep on consuming forever...
while (true)
{
// wait for the data to be ready
while (true)
{
// read the value into a local,
// making sure to lock the mutex
pthread_mutex_lock(&g_mutex);
const bool ready = g_ready;
pthread_mutex_unlock(&g_mutex);

if (ready)
break;


<!-- source-pdf-page: 292 -->

}

// consume the data
pthread_mutex_lock(&g_mutex);
ConsumeDataFrom(&g_queue);
g_ready = false;
pthread_mutex_unlock(&g_mutex);

// yield the remainder of my timeslice
// to give the producer a chance to run
pthread_yield();
}
return nullptr;
}

Besides the fact that this example is somewhat contrived, there’s one big
problem with it: The consumer thread spins in a tight loop, polling the value
of g_ready. As we discussed in Section 4.4.6.4, busy-waiting like this wastes
valuable CPU cycles.
Ideally, we’d like a way to block the consumer thread (put it to sleep) while
the producer does its work, and then wake it up when the data is ready to be
consumed. This can be accomplished by making use of a new kind of kernel
object called a condition variable (CV).
A condition variable isn’t actually a variable that stores a condition. Rather,
it’s a queue of waiting (sleeping) threads, combined with a mechanism that al-
lows a running thread to wake up the sleeping threads at a time of its choosing.
(Perhaps “wait queue” would have been a better name for these things.) The
sleep and wake operations are performed in an atomic way with the help of a
mutex provided by the program, plus a little help from the kernel.
The API for a condition variable typically looks something like this:

1.
create() or init(). A function call or class constructor that creates a
condition variable.
2.
destroy(). A function call or destructor that destroys a condition vari-
able.
3.
wait(). A blocking function that puts the calling thread to sleep.
4.
notify(). A non-blocking function that wakes up any threads that are
currently asleep waiting on the condition variable.

Let’s rewrite our simple producer-consumer example using a CV:

Queue
g_queue;
pthread_mutex_t g_mutex;


<!-- source-pdf-page: 293 -->

bool
g_ready = false;
pthread_cond_t
g_cv;

void* ProducerThreadCV(void*)
{
// keep on producing forever...
while (true)
{
pthread_mutex_lock(&g_mutex);

// fill the queue with data
ProduceDataInto(&g_queue);

// notify and wake up the consumer thread
g_ready = true;
pthread_cond_signal(&g_cv);
pthread_mutex_unlock(&g_mutex);
}
return nullptr;
}

void* ConsumerThreadCV(void*)
{
// keep on consuming forever...
while (true)
{
// wait for the data to be ready
pthread_mutex_lock(&g_mutex);
while (!g_ready)
{
// go to sleep until notified... the mutex
// will be relased for us by the kernel
pthread_cond_wait(&g_cv, &g_mutex);

// when it wakes up, the kernel makes sure
// that this thread holds the mutex again
}

// consume the data
ConsumeDataFrom(&g_queue);
g_ready = false;
pthread_mutex_unlock(&g_mutex);
}
return nullptr;
}

The consumer thread calls pthread_cond_wait() to go to sleep until


<!-- source-pdf-page: 294 -->

g_ready becomes true.
The producer works for a while producing its
data.
When the data is ready, the producer sets the global g_ready
flag to true,
and then wakes up the sleeping consumer by calling
pthread_cond_signal(). The consumer then consumes the data. In this
example, the consumer and producer ping-pong back and forth like this in-
definitely.
You probably noticed that the consumer thread locks its mutex before en-
tering the while loop that checks the g_ready flag. When it waits on the condi-
tion variable, it apparently goes to sleep while holding the mutex lock! Normally
this would be a big no-no: If a thread goes to sleep while holding a lock, it will
almost certainly lead to a deadlock situation (see Section 4.7.1). However, this
is not a problem when using a condition variable. That’s because the kernel
actually does a little slight of hand, unlocking the mutex after the thread has
been safely put to bed. Later, when the sleeping thread is woken back up, the
kernel does some more slight of hand to ensure that the lock will once again
be held by the freshly awoken thread.
You may have noticed another oddity: The consumer thread still uses a
while loop to check the value of g_ready, despite also using a condition vari-
able to wait for the flag to become true. The reason that this loop is necessary
is that threads can sometimes be awoken spuriously by the kernel. As a re-
sult, when the call to pthread_cond_wait() returns, the value of g_ready
might not actually be true yet. So we must keep polling in a loop until the
condition really is true.

### 4.6.4 Semaphores

Just as a mutex acts like an atomic Boolean flag, a semaphore acts like an atomic
counter whose value is never allowed to drop below zero. We can think of a
semaphore as a special kind of mutex that allows more than one thread to acquire
it simultaneously.
A semaphore can be used to permit a group of threads to share a limited
set of resources. For example, let’s suppose that we’re implementing a render-
ing system that allows text and 2D images to be rendered to offscreen buffers,
for the purposes of drawing the game’s heads-up display (HUD) and in-game
menus. Due to memory constraints, let’s further assume that we can only af-
ford to allocate four of these buffers. A semaphore can be used to ensure that
no more than four threads are permitted to render into these buffers at any
given moment.
The API of a semaphore is typically comprised of the following functions:

1.
init(). Initializes a semaphore object, and sets its counter to a specified


<!-- source-pdf-page: 295 -->

initial value.
2.
destroy(). Destroys a semaphore object.
3.
take() or wait().
If the counter value encapsulated by a given
semaphore is greater than zero, this function decrements the counter
and returns immediately. If its counter value is currently zero, this func-
tion blocks (puts the thread to sleep) until the semaphore’s counter rises
above zero again.
4.
give(), post() or signal(). Increments the encapsulated counter
value by one, thereby opening up a “slot” for another thread to take()
the semaphore. If a thread is currently asleep waiting on the semaphore
when give() is called, that thread will wake up from its call to take()
or wait().7

So, to implement a resource pool that can be accessed by up to N threads
at a time, we would simply create a semaphore and initialize its counter to N.
A thread gains access to the resource pool by calling take(), and releases its
hold on the resource pool when it is done by calling give().
We say that a semaphore is signaled whenever its count is greater than zero,
and it is nonsignaled when its counter is equal to zero. This is why the func-
tions that take and give a semaphore are named wait() and signal(), re-
spectively, in some APIs: If the semaphore isn’t signaled when a thread calls
this function, the thread will wait for the semaphore to become signaled.

4.6.4.1
Mutex versus Binary Semaphore

A semaphore whose initial value is set to 1 is called a binary semaphore. One
might think that a binary semaphore is identical to a mutex. Certainly both
objects permit only one thread to acquire it at a time. However, these two
synchronization objects are not equivalent, and are typically used for quite
different purposes.
The key difference between a mutex and a binary semaphore is that a mutex
can only be unlocked by the thread that locked it. A semaphore’s counter, on
the other hand, can be incremented by one thread and later decremented by
another thread. This implies that a binary semaphore can be “unlocked” by a
different thread than the one that “locked” it. Or really, we should say that a
binary semaphore can be given by a different thread than the one that takes it.
This seemingly subtle difference between mutexes and binary semaphores
leads to very different use cases for these two kinds of synchronization object.

7When you read about semaphores, you may discover some authors using the function names
p() and v() instead of wait() and signal(). These letters come from the Dutch names for these two
operations.


<!-- source-pdf-page: 296 -->

A mutex is used to make an operation atomic. But a binary semaphore is typi-
cally used to send a signal from one thread to another.
Consider again our producer-consumer example, in which the producer
needs to notify the consumer when the data it produces is ready for con-
sumption. This notification mechanism can be implemented using two bi-
nary semaphores, one that allows the producer to wake the consumer, and
one that allows the consumer to wake the producer. We can think of these
semaphores as representing the number of elements that are used and free
within a buffer that’s shared between the two threads, although in this sim-
ple example the buffer can only hold a single item. As such, we’ll call the
semaphores g_semUsed and g_semFree, respectively. Here’s what the code
would look like, using POSIX semaphores:

Queue g_queue;
sem_t g_semUsed; // initialized to 0
sem_t g_semFree; // initialized to 1

void* ProducerThreadSem(void*)
{
// keep on producing forever...
while (true)
{
// produce an item (can be done non-
// atomically because it's local data)
Item item = ProduceItem();

// decrement the free count
// (wait until there's room)
sem_wait(&g_semFree);

AddItemToQueue(&g_queue, item);

// increment the used count
// (notify consumer that there's data)
sem_post(&g_semUsed);
}
return nullptr;
}

void* ConsumerThreadSem(void*)
{
// keep on consuming forever...
while (true)
{
// decrement the used count


<!-- source-pdf-page: 297 -->

// (wait for the data to be ready)
sem_wait(&g_semUsed);

Item item = RemoveItemFromQueue(&g_queue);

// increment the free count
// (notify producer that there's room)
sem_post(&g_semFree);

// consume the item (can be done non-
// atomically because it's local data)
ConsumeItem(item);
}
return nullptr;
}

4.6.4.2
Implementing a Semaphore

It turns out that one can implement a semaphore in terms of a mutex, a con-
dition variable and an integer. In that sense, a semaphore is a “higher-level”
construct than either a mutex or a condition variable. Here’s what the imple-
mentation looks like:

class Semaphore
{
private:
int
m_count;
pthread_mutex_t m_mutex;
pthread_cond_t
m_cv;

public:
explicit Semaphore(int initialCount)
{
m_count = initialCount;
pthread_mutex_init(&m_mutex, nullptr);
pthread_cond_init(&m_cv, nullptr);
}

void Take()
{
pthread_mutex_lock(&m_mutex);
// put the thread to sleep as long as
// the count is zero
while (m_count == 0)
pthread_cond_wait(&m_cv, &m_mutex);


<!-- source-pdf-page: 298 -->

--m_count;
pthread_mutex_unlock(&m_mutex);
}

void Give()
{
pthread_mutex_lock(&m_mutex);
++m_count;
// if the count was zero before the
// increment, wake up a waiting thread
if (m_count == 1)
pthread_cond_signal(&m_cv);
pthread_mutex_unlock(&m_mutex);
}

// aliases for other commonly-used function names
void Wait()
{ Take(); }
void Post()
{ Give(); }
void Signal() { Give(); }
void Down()
{ Take(); }
void Up()
{ Give(); }
void P()
{ Take(); } // Dutch "proberen" = "test"
void V()
{ Give(); } // Dutch "verhogen" =
// "increment"
};

### 4.6.5 Windows Events

Windows provides a mechanism called an event object that is similar in function
to a condition variable, but much simpler to use. Once an event object has been
created, a thread can go to sleep by calling WaitForSingleObject(), and
that thread can be awoken by another thread by calling SetEvent(). Rewrit-
ing our producer-consumer example using event objects yields the following
very simple implementation:

#include <windows.h>

Queue g_queue;
Handle g_hUsed; // initialized to false (nonsignaled)
Handle g_hFree; // initialized to true (signaled)

void* ProducerThreadEv(void*)
{
// keep on producing forever...
while (true)
{
// produce an item (can be done non-


<!-- source-pdf-page: 299 -->

// atomically because it's local data)
Item item = ProduceItem();

// wait until there's room
WaitForSingleObject(&g_hFree);

AddItemToQueue(&g_queue, item);

// notify consumer that there's data
SetEvent(&g_hUsed);
}
return nullptr;
}

void* ConsumerThreadEv(void*)
{
// keep on consuming forever...
while (true)
{
// wait for the data to be ready
WaitForSingleObject(&g_hUsed);

Item item = RemoveItemFromQueue(&g_queue);

// notify producer that there's room
SetEvent(&g_hFree);

// consume the item (can be done non-
// atomically because it's local data)
ConsumeItem(item);
}
return nullptr;
}

void MainThread()
{
// create event in the nonsignalled state
g_hUsed = CreateEvent(nullptr, false,
false, nullptr);
g_hFree = CreateEvent(nullptr, false,
true, nullptr);

// spawn our threads
CreateThread(nullptr, 0x2000, ConsumerThreadEv,
0, 0, nullptr);
CreateThread(nullptr, 0x2000, ProducerThreadEv,
0, 0, nullptr);


<!-- source-pdf-page: 300 -->

// ...
}

## 4.7 Problems with Lock-Based Concurrency

In Section 4.5.3.2, we learned that data races can lead to incorrect program be-
havior in a concurrent system. We saw that the solution to this problem is
to make operations on shared data objects atomic. One way to achieve atom-
icity is to wrap these operations in locks, which are often implemented us-
ing operating-system provided thread synchronization primitives such as mu-
texes.
However, atomicity is only part of the concurrency story. There are other
problems that can plague a concurrent system, even when all shared-data op-
erations have been carefully protected by locks. In the following sections, we’ll
briefly explore the most common of these problems.

### 4.7.1 Deadlock

Deadlock is a situation in which no thread in the system can make progress,
resulting in a hang. When a deadlock occurs, all threads are in their Blocked
states, waiting on some resource to become available. But because no threads
are Runnable, none of those resources can ever become available, so the entire
program hangs.
For a deadlock to occur, we need at least two threads and two resources.
For example, Thread 1 holds Resource A but is waiting for Resource B; and at
the same time, Thread 2 holds Resource B but is waiting for Resource A. This
situation is illustrated by the following code snippet:

void Thread1()
{
g_mutexA.lock(); // holds lock for Resource A
g_mutexB.lock(); // sleeps waiting for Resource B
// ...
}

void Thread2()
{
g_mutexB.lock(); // holds lock for Resource B
g_mutexA.lock(); // sleeps waiting for Resource A
// ...
}


<!-- source-pdf-page: 301 -->
> Visual fallback for diagrams/images: [PDF page 301](../../../visual_pages/page_0301.jpg)

R1

T0

T2

T0

T2

R0

R2

T1

T1

R3

Figure 4.34. Left: Dark arrows show resources currently held by threads. Light dashed arrows
show threads waiting on resources to become available. Right: We can eliminate the resources
and simply draw the dependencies (light dashed arrows) between threads. A cycle in such a thread
dependency graph indicates a deadlock.

Other more complex kinds of deadlock can of course occur as well, involving
more threads and more resources. But the key factor that defines any deadlock
situation is a circular dependency between threads and their resources.
To analyze a system for the possibility of deadlock, we can draw a graph
of our threads, our resources, and the dependencies between them, as shown
in Figure 4.34. In this graph, we’ve used squares to represent threads and
circles to represent resources (or more precisely, the mutex locks that protect
them). Solid arrows connect resources to the threads that currently hold locks
on them. Dashed arrows connect threads to the resources they are waiting
for. We can actually eliminate the resource nodes in the graph for simplicity
if we like, leaving only the dashed lines connecting threads that are waiting
on other threads. If such a dependency graph ever contains a cycle, we have a
deadlock.
Actually a cycle in the dependency graph isn’t quite enough to produce a
deadlock. Strictly speaking, there are four necessary and sufficient conditions
for deadlock, known as the Coffman conditions:

1.
Mutual exclusion. A single thread can be granted exclusive access to a
single resource via a mutex lock.

2.
Hold and wait. A thread must be holding one lock when it goes to sleep
waiting on another lock.

3.
No lock preemption. No one (not even the kernel) is allowed to forcibly


<!-- source-pdf-page: 302 -->

break a lock held by a sleeping thread.

4.
Circular wait. There must exist a cycle in the thread dependency graph.

Avoiding deadlock always boils down to preventing one or more of the
Coffman conditions from holding true. Since violating conditions #1 and #3
would be “cheating,” solutions usually focus on avoiding conditions #2 and
#4.
Hold and wait can be avoided by reducing the number of locks. In our
simple example, if Resource A and Resource B were both protected by a single
lock L, then deadlock could not occur. Either Thread 1 would obtain the lock,
and gain exclusive access to both resources while Thread 2 waits, or Thread 2
would obtain the lock while Thread 1 waits.
The circular wait condition can be avoided by imposing a global order to all
lock-taking in the system. In our simple two-threaded example, if we were to
ensure that Resource A’s lock is always taken before Resource B’s lock, dead-
lock would be avoided. This would work because one thread will always ob-
tain a lock on Resource A before attempting to take any other locks. This ef-
fectively puts all other contending threads to sleep, thereby ensuring that the
attempt to take a lock on Resource B will always succeed.

### 4.7.2 Livelock

Another solution to the deadlock problem is for threads to try to take
locks without going to sleep (using a function such as pthread_mutex_
trylock()). If a lock cannot be obtained, the thread sleeps for some short
period of time and then retries the lock.
When threads use an explicit strategy, such as timed retries, in order to
avoid or resolve deadlocks, a new problem can arise: The threads can end
up spending all of their time just trying to resolve the deadlock, rather than
performing any real work. This is known as livelock.
As a simple example of livelock, consider again our example of two threads
1 and 2 contending over two resources A and B. Whenever a thread is unable
to obtain a lock, it releases any locks it already holds and waits for a fixed
timeout before trying again. If both threads use the same timeout, we can get
into a situation in which the same degenerate situation simply repeats over
and over. Our threads become “stuck” forever trying to resolve the conflict,
and neither one ever gets a chance to do its real job. Livelock is akin to a
stalemate in chess.
Livelock can be avoided by using an asymmetric deadlock resolution al-
goirthm. For example, we could ensure that only one thread, either chosen at


<!-- source-pdf-page: 303 -->

random or based on priority, ever takes action to resolve a deadlock when one
is detected.

### 4.7.3 Starvation

Starvation is defined as any situation in which one or more threads fail to re-
ceive any execution time on the CPU. Starvation can happen when one or more
higher-priority threads fail to relinquish control of the CPU, thereby prevent-
ing lower-priority threads from running. Livelock is another kind of starva-
tion, in which a deadlock-resolution algorithm effectively starves all threads
of their ability to do “real” work.
Priority-based starvation is normally avoided by ensuring that high-
priority threads never run for very long. Ideally a multithreaded program
would consist of a pool of lower-priority threads that normally share the sys-
tem’s CPU resources fairly. Once in a while, a higher-priority thread runs,
takes care of its business quickly, and then ends, returning the CPU resources
to the lower-priority threads.

### 4.7.4 Priority Inversion

Mutex locks can lead to a situation known as priority inversion, in which a low-
priority thread acts as though it is the highest-priority thread in the system.
Consider two threads, L and H, with a low and high priority, respectively.
Thread L takes a mutex lock and then is preempted by H. If H attempts to take
this same lock, then H will be put to sleep because L already holds the lock.
This permits L to run even though it is lower priority than H—in violation of
the principle that lower-priority threads should not run while a higher-priority
thread is runnable.
Priority inversion can also occur if a medium-priority thread M preempts
thread L while it is holding a lock needed by H. In this case, L goes to sleep
while M runs, preventing it from releasing the lock. When H runs, it is there-
fore unable to obtain the lock; it goes to sleep, and now M’s priority has effec-
tively been inverted with that of thread H.
The consequences of priority inversion may be neglible. For example, if
the lower-priority thread immediately relinquishes the lock, the duration of
the priority inversion may be short and it may go unnoticed or produce only
minor ill effects. However, in extreme cases, priority inversion can lead to
deadlock or other kinds of system failure. For example, a priority inversion
can cause a high-priority thread to miss a critical deadline.
Solutions to the priority inversion problem include:


<!-- source-pdf-page: 304 -->

•
Avoiding locks that can be taken by both low- and high-priority threads.
(This solution is often not feasible.)
•
Assigning a very high priority to the mutex itself. Any thread that takes
the mutex has its priority temporarily raised to that of the mutex, thus
ensuring it cannot be preempted while holding the lock.
•
Random priority boosting. In this approach, threads that are actively
holding locks are randomly boosted in priority until they exit their crit-
ical sections. This approach is used in the Windows scheduling model.

### 4.7.5 The Dining Philosophers

The famous dining philosophers problem is a great illustration of the problems
of deadlock, livelock and starvation. It describes a situation in which five
philosophers sit around a circular table, each with a plate of spaghetti in front
of her. Between each philosopher sits a single chopstick. The philosophers
wish to alternate between thinking (which they can do without any chop-
sticks), and eating (a task for which a philosopher requires two chopsticks).
The problem is to define a pattern of behavior for the philosophers that en-
sures they can all alternate between thinking and eating without experienc-
ing deadlock, livelock or starvation. (Obviously the philosophers represent
threads, and the chopsticks represent mutex locks.)
You can read about this well-known problem online, so we won’t devote a
lot of space to discussing it here. However, it will be instructive to consider a
few of the most common solutions to the problem:

•
Global order. A dependency cycle can occur if a philosopher were always
to pick up the chopstick to her left first (or always the one to her right).
One solution to this problem is to assign each chopstick a unique global
index. Whenever a philosopher wishes to eat, he or she always picks up
the chopstick with the lowest index first. This prevents the dependency
cycle, and hence avoids deadlock.

•
Central arbitor. In this solution, a central arbitor (the “waiter”) either
grants a philosopher two chopsticks or none. This avoids the hold and
wait problem by ensuring that no philosopher ever gets into a situation
in which he or she holds only one chopstick, thereby preventing dead-
lock.

•
Chandra-Misra. In this solution, chopsticks are marked either dirty or
clean, and the philosophers send each other messages requesting chop-
sticks. You can read more about this solution by searching for “chandy-
misra” online.


<!-- source-pdf-page: 305 -->

•
N −1 philosophers. For a table with N philosophers and N chopsticks,
an integer semaphore can be used to limit the number of philosophers
who are permitted to pick up chopsticks at any given time to N −1. This
solves the deadlock and livelock problems, because even in degenerate
situations, at least one philosopher will succeed in obtaining two chop-
sticks. It does, however, permit one of the philosophers to starve, unless
an additional “fairness” criterion is introduced.

## 4.8 Some Rules of Thumb for Concurrency

The solutions to the dining philosophers problem we described in the preced-
ing section hint at some general principles and rules of thumb which we can
apply to virtually any concurrent programming problem. Let’s take a brief
look at a few of them.

### 4.8.1 Global Ordering Rules

In a concurrent program, the order in which events occur is not dictated by the
order of the instructions in the program, as it is in a single-threaded program.
If ordering is required in a concurrent system, that ordering must be imposed
globally, across all threads.
This is one reason why a doubly linked list is not a concurrent data struc-
ture. A doubly linked list is designed the way it is in order to provide fast
(O(N)) insertion and deletion of elements at any location within the list. Un-
derlying this design is an assumption that program order is equivalent to data
order—that when an ordered sequence of operations is performed on a list,
the resulting list will contain correspondingly ordered elements. For example,
let’s say we are given a linked list that contains the ordered elements { A, B, C
}. If we execute the following two operations:

1.
insert D before C,

2.
insert E before C,

the assumption is that the resulting list will contain the ordered elements { A,
B, D, E, C }.
In a single-threaded program, this assumption holds true. But in a multi-
threaded system, program order no longer dictates data order. If one thread
performs the insertion of D before C, and another thread performs the insertion
of E before C, we have a race condition that could lead to any of the following
results:


<!-- source-pdf-page: 306 -->

•
{ A, B, D, E, C },

•
{ A, B, E, D, C }, or

•
{ A, B, corrupted data }.

A corrupted list can occur if the data structure’s operations aren’t properly
protected with critical sections. For example, both D’s and E’s “next” pointers
might end up pointing to C.
A global ordering rule is the only viable solution to this problem. We first
need to ask ourselves why the order of the elements in the list matters, and
if it even matters at all. If order is unimportant, we can use a singly-linked
list and always append the elements—an operation that can be implemented
reliably either with locks or in a lock-free manner. And if a global ordering
is required, we need to identify a stable and deterministic ordering criterion
that does not depend on the order in which program events happen to occur.
For example, we might sort the list alphabetically, or by priority, or some other
useful criterion. The answers to these questions in turn dictate what kind of
data structure we’ll want to use. Attempting to use a doubly-linked list in a
concurrent way (i.e., giving multiple threads mutable access to the list) is like
trying to fit a square peg into a round hole.

### 4.8.2 Transaction-Based Algorithms

In the central arbiter solution to the dining philosophers problem, the arbiter
or “waiter” hands out chopsticks in pairs: Either a philosopher receives all of
the resources he needs (two chopsticks), or he receives none of them. This is
known as a transaction.
A transaction can be more precisely defined as an indivisible bundle of re-
sources and/or operations. Threads in a concurrent system submit transaction
requests to a central arbiter of some kind. A transaction either succeeds in its
entirety, or it fails in its entirety (because some other thread’s transaction is
actively being processed when the request arrives). If the transaction fails, its
thread keeps resubmitting the transaction request until it succeeds (possibly
waiting for a short time between retries).
Transaction-based algorithms are common in concurrent and distributed
systems programming. And as we’ll see in Section 4.9, the concept of a trans-
action underlies most lock-free data structures and algorithms.

### 4.8.3 Minimizing Contention

The most efficient concurrent system would be one in which all threads run
without ever having to wait for a lock. This ideal can never be fully achieved,


<!-- source-pdf-page: 307 -->

of course, but concurrent systems programmers do attempt to minimize the
amount of contention between threads.
As an example, consider a group of threads that are producing data and
storing it into a central repository. Every time one of these threads attempts
to store its data in the repository, it contends with all of the other threads for
this shared resource. A simple solution that can sometimes work is to give
each thread its own private repository. The threads can now produce data
independently of one another, with no contention. When all of the threads are
done producing their output, a central thread can collate the results.
The analog to this approach in the case of the dining philosophers prob-
lem would be to give each philosopher two chopsticks from the outset. Doing
so would of course remove all concurrency from the problem—without any
shared resources, there is no concurrency. In real-world concurrent systems
we can’t remove all sharing of resources, but we can certainly look for ways to
minimize resource sharing in order to minimize lock contention.

### 4.8.4 Thread Safety

Generally speaking, a class or functional API is called thread-safe when its func-
tions can be safely called by any thread in a multithreaded process. For any
one function, thread safety is typically achieved by entering a critical section
at the top of the function’s body, performing some work, and then leaving the
critical section just prior to returning. A class whose functions are all thread-
safe is sometimes called a monitor. (The term “monitor” is also used to refer to
a class that uses condition variables internally to permit clients to sleep while
waiting for its protected resources to become available.)
Thread-safety is a convenient feature for a class or interface to offer. But
it also imposes overhead that is sometimes unnecessary. For example, this
overhead would be wasteful if the interface is being used in a single-threaded
program, or if it’s being used exclusively by one thread in a multithreaded
program.
Thread-safety can also become a problem when an interface function needs
to be called reentrantly. For example, if a class provides two thread-safe func-
tions A() and B(), then these functions cannot call one another because each
one independently enters and leaves the critical section. One solution to this
problem is to use reentrant locks (see Section 4.9.7.3). Another is to implement
“unsafe” versions of the functions in an interface, and then “wrap” each of
them in thread-safe variant that simply enters a critical section, calls the “un-
safe” function, and then leaves the critical section. That way, we can call the
“unsafe” functions internally to the system, but the external interface of the


<!-- source-pdf-page: 308 -->

system remains thread-safe.
In my view, it’s a bad idea to attempt to handle concurrent programming
by simply creating classes and APIs that are 100% thread-safe. Doing so leads
to interfaces that are unnecessarily heavy-weight, and it encourages program-
mers to ignore the fact that they are working in a concurrent environment.
Instead, we should recognize and embrace the presence of concurrency in our
software, and aim to design data structures and algorithms that address this
concurrency explicitly. The goal should be to produce a software system that
minimizes contention and dependencies between threads, and minimizes the
use of locks. Achieving a completely lock-free system is a lot of work and is
tough to get right. (See Section 4.9.) But striving in the direction of lock-freedom
(i.e., minimizing the use of locks) is a far better strategy than over-using locks
in a futile attempt to create “water-tight” interfaces that allow programmers
to be oblivious of concurrency.

## 4.9 Lock-Free Concurrency

Thus far, all of our solutions to the problem of race conditions in a concurrent
system have revolved around using mutex locks to make critical operations
atomic, and possibly leveraging condition variables and the kernel’s ability to
put threads to sleep to avoid them wasting valuable CPU cycles in a busy-wait
loop. During that discussion, we mentioned that there’s another way to avoid
race conditions that can potentially be more efficient. That approach is known
as lock-free concurrency.
Contrary to popular belief, the term “lock-free” doesn’t actually refer to
the elimination of mutex locks, although that is certainly a component of the
approach. In fact, “lock-free” refers to the practice of preventing threads from
going to sleep while waiting on a resource to become available. In other words,
in lock-free programming we never allow a thread to block. So perhaps the
term “blocking-free” would have been more descriptive.
Lock-free programming is actually just one of a collection of non-blocking
concurrent programming techniques. When a thread is blocked, it ceases to
make progress. The goal of all of these techniques is to provide guarantees
about the progress that can be made by the threads in the system, and by the
system as a whole. We can organize these techniques into the following cat-
egories, listed in order of increasing strength of the progress guarantees they
provide:

•
Blocking. A blocking algorithm is one in which a thread can be put to
sleep while waiting for shared resources to become available. A block-


<!-- source-pdf-page: 309 -->

ing algorithm is prone to deadlock, livelock, starvation and priority in-
version.

•
Obstruction freedom. An algorithm is obstruction-free if we can guarantee
that a single thread will always complete its work in a bounded number
of steps, when all of the other threads in the system are suddenly sus-
pended. The single thread that continues to run is said to be perform-
ing a solo execution in this scenario, and an obstruction-free algorithm is
sometimes called solo-terminating because the solo thread eventually ter-
minates while all other threads are suspended. No algorithm that uses
a mutex lock or spin lock can be obstruction-free, because if any thread
were to be suspended while it is holding a lock, the solo thread might
become stuck forever waiting for that lock.

•
Lock freedom.
The technical definition of lock freedom is that in any
infinitely-long run of the program, an infinite number of operations will
be completed. Intuitively, a lock-free algorithm guarantees that some
thread in the system can always make progress; in other words, if one
thread is arbitrarily suspended, all others can still make progress. This
again precludes the use of mutex or spin locks, because if a thread hold-
ing a lock is suspended, it can cause other threads to block. A lock-free
algorithm is typically transaction-based: A transaction may fail if an-
other thread interrupts it, in which case the transaction is rolled back and
retried until it succeeds. This approach avoids deadlock, but it can allow
some threads to starve. In other words, certain threads might get stuck in
a loop of failing and retrying their transactions indefinitely, while other
threads’ transactions always succeed.

•
Wait freedom. A wait-free algorithm provides all the guarantees of lock
freedom, but also guarantees starvation freedom. In other words, all
threads can make progress, and no thread is allowed to starve indefi-
nitely.

The term “lock-free programming” is sometimes used loosely to refer to any
algorithm that avoids blocking, but technically speaking the correct term for
obstruction-free, lock-free and wait-free algorithms as a whole is “non-blocking
algorithm.”
The topic of non-blocking algorithms is vast, and is still an open area of
research. A complete discussion of the topic would require an entire book of
its own. In this chapter, we’ll introduce some of the basic principles of lock-
free programming. We’ll begin by exploring the true casues of data race bugs.
Then we’ll have a look at how mutexes are actually implemented under the


<!-- source-pdf-page: 310 -->

hood, and learn how to implement our own inexpensive spin locks. Finally,
we’ll present an implementation of a simple lock-free linked list. This discus-
sion ought to be sufficient to give you a flavor for what lock-free data structures
and algorithms tend to look like, and should provide a solid jumping off point
for further reading and experimentation with lock-free techniques.

### 4.9.1 Causes of Data Race Bugs

In Section 4.5.3.2, we said that data race bugs occur when a critical operation
is interrupted by another critical operation on the same shared data. As it
turns out, there are two other ways in which data race bugs can arise, and if
we’re going to implement our own spin locks or write lock-free algorithms,
we’ll need to understand them all. Data race bugs can be introduced into a
concurrent program:

•
via the interruption of one critical operation by another,
•
by the instruction reordering optimizations performed by the compiler
and CPU, and
•
as a result of hardware-specific memory ordering semantics.

Let’s break these down a bit further:

•
Threads interrupt one another all the time as a result of preemptive mul-
titasking and/or by running them on multiple cores. However, when a
critical operation is interrupted by another critical operation on the same
shared data object, a data race bug can occur.

•
Optimizing compilers often reorder instructions in an attempt to min-
imize pipeline stalls. Likewise, the out-of-order execution logic within
the CPU can cause instructions to be executed in an order that differs
from program order. Instruction reordering is guaranteed not to alter
the observable behavior of a single-threaded program. But it can alter
the way in which two or more threads cooperate to share data, thereby
introducing bugs into a concurrent program.

•
Thanks to aggressive optimizations within a computer’s memory con-
troller, the effects of a read or write instruction can sometimes become
delayed relative to other reads and/or writes in the system. As with
compiler optimizations and OOO execution, these memory controller
optimizations are designed never to alter the observable behavior of a
single-threaded program. However, these optimizations can change the
order of a critical pair of reads and/or writes in a concurrent system,


<!-- source-pdf-page: 311 -->

thereby preventing threads from sharing data in a predictable way. In
this book, we’ll refer to these kinds of concurrency bugs as memory order-
ing bugs.

In order to guarantee that a critical operation is free from data races, and is
therefore atomic, we must ensure that none of these three things can happen.

### 4.9.2 Implementing Atomicity

First, let’s tackle the problem of making a critical operation atomic (i.e., unin-
terruptable). Before, we waved our hands a bit and said that by wrapping our
critical operations in a mutex lock/unlock pair, we can magically transform
them into atomic operations. But how does a mutex actually work?

4.9.2.1
Atomicity by Disabling Interrupts

To prevent other threads from interrupting our operation, we could try dis-
abling interrupts just prior to performing the operation, making sure to reen-
able them after the operation has been completed. That would certainly pre-
vent the kernel from context-switching to another thread in the middle of our
atomic operation. However, this approach only works on a single-core ma-
chine using preemptive multitasking.
Interrupts are disabled by executing a machine language instruction (such
as cli, “clear interrupt enable bit,” on an Intel x86 architecture). But this kind
of instruction only affects the core that executed it. The other cores would
continue to run their threads (with interrupts, and therefore preemptive multi-
threading, still enabled), and those threads could still interrupt our operation.
So this approach has only limited applicability in the real world.

4.9.2.2
Atomic Instructions

The term “atomic” suggests the notion of breaking an operation down into
smaller and smaller pieces, until we reach a granularity that is indivisible. This
idea raises the question: Are there any machine language instructions that are
naturally atomic? In other words, does the CPU guarantee that some instruc-
tions are uninterruptable?
The answer to these questions is “yes,” but with a few caveats. There are
most certainly some machine language instructions that can never be assumed
to execute atomically. Other instructions are atomic, but only when operating
on certain kinds of data. Some CPUs permit virtually any instruction to be
forced to execute atomically by specifying a prefix on the instruction in assem-
bly language. (The Intel x86 ISA’s lock prefix is one example.)


<!-- source-pdf-page: 312 -->

This is good news for concurrent programmers. In fact, it is the existence
of these atomic instructions that permits us to implement atomicity tools such
as mutexes and spin locks, which in turn permit us to make larger-scale oper-
ations atomic.
Different CPUs and ISAs provide different sets of atomic instructions, gov-
erned by different rules. But we can generalize all atomic instructions as falling
into two categories:

•
atomic reads and writes, and

•
atomic read-modify-write (RMW) instructions.

4.9.2.3
Atomic Reads and Writes

On most CPUs, we can be reasonably certain that a read or write of a four-
byte-aligned 32-bit integer will be atomic. That being said, every processor is
different, so it’s important to always consult your CPU’s ISA documentation
before relying on the atomicity of any particular instruction.
Some CPUs also support atomic reads and writes of smaller or larger ob-
jects, such as single bytes or 64-bit integers, again assuming they are aligned
to a multiple of their own size. This is true because on most CPUs, reading
and writing an aligned integer whose width in bits is equal to or smaller than
the width of a register (or sometimes the width of a cache line) can be per-
formed in a single memory access cycle. Because a CPU performs its operation
in lock-step with a discrete clock, a memory cycle can’t be interrupted, even
by another core. As a result, the read or write is effectively atomic.
Misaligned reads and writes usually don’t have this atomicity property.
This is because in order to read or write a misaligned object, the CPU usually
composes two aligned memory accesses. As such, the read or write might be
interrupted, and we cannot assume it will be atomic. (See Section 3.3.7.1 for
more details on alignment.)

4.9.2.4
Atomic Read-Modify-Write

Atomic reads and writes aren’t enough to implement atomic operations in a
general sense. In order to implement a locking mechanism like a mutex, we
need to be able to read a variable’s contents from memory, perform some op-
eration on that variable, and then write the results back to memory, all without
being interrupted.
All modern CPUs support concurrency by providing at least one atomic
read-modify-write (RMW) instruction. In the following sections, we’ll have a
look at a few different kinds of atomic RMW instructions. Each has its pros
and cons. All of them can be used to implement a mutex or spin lock.


<!-- source-pdf-page: 313 -->

4.9.2.5
Test and Set

The simplest RMW instruction is known as test-and-set (TAS). The TAS instruc-
tion doesn’t actually test and set a value. Rather, it atomically sets a Boolean
variable to 1 (true) and returns its previous value (so that the value can be
tested).

// pseudocode for the test-and-set instruction
bool TAS(bool* pLock)
{
// atomically...
const bool old = *pLock;
*pLock = true;
return old;
}

The test-and-set instruction can be used to implement a simple kind of lock
called a spin lock. Here’s some pseudocode illustrating the idea. In this exam-
ple, we using a hypothetical compiler intrinsic called _tas() to emit a TAS
machine language instruction into our code. Different compilers will provide
different intrinsics for this instruction, if the target CPU supports it. For exam-
ple, under Visual Studio the TAS intrinsic is named _interlockedbittest
andset().

void SpinLockTAS(bool* pLock)
{
while (_tas(pLock) == true)
{
// someone else has lock -- busy-wait...
PAUSE();
}

// when we get here, we know that we successfully
// stored a value of true into *pLock AND that it
// previously contained false, so no one else has
// the lock -- we're done
}

Here, the PAUSE() macro indicates the use of a compiler intrinsic, such as
Intel’s SSE2 _mm_pause(), to reduce power consumption during the busy-
wait loop. See Section 4.4.6.4 for details on why it’s advisable to use a pause
instruction inside a busy-wait loop where possible.
It’s important to stress here that the above example is intended for illus-
trative purposes only. It is not 100% correct because it lacks proper memory


<!-- source-pdf-page: 314 -->

fencing. We’ll present a complete working example of a spin lock in Section
4.9.7.

4.9.2.6
Exchange

Some ISAs like Intel x86 offer an atomic exchange instruction. This instruction
swaps the contents of two registers, or a register and a location in memory. On
x86, it is atomic by default when exchanging a register with memory (meaning
it acts as if the instruction had been preceded with the lock prefix).
Here’s how to implement a spin lock using an atomic exchange instruction.
In this example, we’re using Visual Studio’s _InterlockedExchange()
compiler intrinsic to emit the Intel x86 xchg instruction into our code. (Again,
note that without proper memory fencing this example is incomplete and
won’t work reliably. See Section 4.9.7 for a complete implementation.)

void SpinLockXCHG(bool* pLock)
{
bool old = true;
while (true)
{
// emit the xchg instruction for 8-bit words
_InterlockedExchange8(old, pLock);
if (!old)
{
// if we get back false,
// then the lock succeeded
break;
}
PAUSE();
}
}

Under Microsoft Visual Studio, all of the “interlocked” functions named
with a leading underscore are compiler intrinsics—they simply emit the ap-
propriate assembly language instruction directly into your code. The Win-
dows SDK provides a set of similarly-named functions without the leading
underscore—those functions are implemented in terms of the intrinsic where
possible, but they’re much more expensive because they involve making a ker-
nel call.

4.9.2.7
Compare and Swap

Some CPUs provide an instruction known as compare-and-swap (CAS). This in-
struction checks the existing value in a memory location, and atomically swaps
it with a new value if and only if the existing value matches an expected value


<!-- source-pdf-page: 315 -->

provided by the programmer. It returns true if the operation succeeded, mean-
ing that the memory location contained the expected value. It returns false if
the operation failed because the location’s contents were not as expected.
CAS can operate on values larger than a Boolean. Variants are typically
provided at least for 32- and 64-bit integers, although smaller word sizes may
also be supported.
The behavior of the CAS instruction is illustrated by the following pseu-
docode:

// pseudocode for compare and swap
bool CAS(int* pValue, int expectedValue, int newValue)
{
// atomically...
if (*pValue == expectedValue)
{
*pValue = newValue;
return true;
}
return false;
}

To implement any atomic read-modify-write operation using CAS, we gen-
erally apply the following strategy:

1.
Read the old value of the variable we’re attempting to update.

2.
Modify the value in whatever way we see fit.

3.
When writing the result, use the CAS instruction instead of a regular
write.

4.
Iterate until the CAS succeeds.

The CAS instruction allows us to detect data races, by comparing the value
that’s actually in the memory location at the time of the write with its value
before the invocation of our read-modify-write operation. In the absence of a
race, the CAS instruction acts just like a write instruction. But if the value in
memory changes between the read and the write, we know that some other
thread beat us to the punch. In that case, we back off and try again.
Implementing a spin lock via compare-and-swap would look something
like this, again using a hypothetical compiler intrinsic called _cas() to emit
the CAS instruction. (This example once again omits the memory fences that
would be required to make it work reliably on all hardware—see Section 4.9.7
for a fully-functional spin lock.)


<!-- source-pdf-page: 316 -->

void SpinLockCAS(int* pValue)
{
const int kLockValue = -1; // 0xFFFFFFFF
while (!_cas(pValue, 0, kLockValue))
{
// must be locked by someone else -- retry
PAUSE();
}
}

And here’s how we’d implement an atomic increment using CAS.

void AtomicIncrementCAS(int* pValue)
{
while (true)
{
const int oldValue = *pValue; // atomic read
const int newValue = oldValue + 1;
if (_cas(pValue, oldValue, newValue))
{
break; // success!
}
PAUSE();
}
}

On the Intel x86 ISA, the CAS instruction is called cmpxchg, and it can be
emitted with Visual Studio’s _InterlockedCompareExchange() compiler
intrinsic.

4.9.2.8
ABA Problem

We should mention that the CAS instruction suffers from an inability to detect
one specific kind of data race. Consider an atomic RMW write operation in
which the read sees the value A. Before we’re able to issue the CAS instruction,
another thread preempts us, or runs on another core, and writes two values
into the location we’re trying to atomically update: First it writes the value B,
and then it writes the value A again. When our CAS instruction does finally
execute, it won’t be able to tell the difference between the first A it read and
the A that was written by the other thread. Hence it will “think” that no data
race occurred, when in fact one did. This is known as the ABA problem.

4.9.2.9
Load Linked/Store Conditional

Some CPUs break the compare-and-swap operation into a pair of instructions
known as load linked and store conditional (LL/SC). The load linked instruction


<!-- source-pdf-page: 317 -->

reads the value of a memory location atomically, and also stores the address in
a special CPU register known as the link register. The store conditional instruc-
tion writes a value into the given address, but only if the address matches the
contents of the link register. It returns true if the write succeeded, or false if it
failed.
Any write operation on the bus (including a store conditional) clears the
link register to zero. This means that an LL/SC instruction pair is capable
of detecting data races, because if any write occurs between the LL and SC
instructions, the SC will fail.
An LL/SC instruction pair is used in much the same way a regular read
is paired with a CAS instruction. Specifically, an atomic read-modify-write
operation would be implemented using the following strategy:

1.
Read the old value of the variable via an LL instruction.

2.
Modify the value in whatever way we see fit.

3.
Write the result using the SC instruction.

4.
Iterate until the SC succeeds.

Here’s how we’d implement an atomic increment using LL/SC (using the
hypothetical compiler intrinsics _ll() and _sc() to emit the LL and SC in-
structions, respectively):

void AtomicIncrementLLSC(int* pValue)
{
while (true)
{
const int oldValue = _ll(*pValue);
const int newValue = oldValue + 1;
if (_sc(pValue, newValue))
{
break; // success!
}
PAUSE();
}
}

Because the link register is cleared by any bus write, the SC instruction
may fail spuriously. But that doesn’t affect the correctness of an atomic RMW
implemented with LL/SC—it just means that our busy-wait loop might end
up executing a few more iterations than we’d expect.


<!-- source-pdf-page: 318 -->
> Visual fallback for diagrams/images: [PDF page 318](../../../visual_pages/page_0318.jpg)

load compare store

CAS

WB
M2
E
M1
D
F

load & link

LL

F

D

E

M

WB

SC

F

D

E

M

WB

check link & store

Figure 4.35. The compare and swap (CAS) instruction reads a memory location, performs a com-
parison, and then conditionally writes to that same location. It therefore requires two memory
access stages, making it more difﬁcult to implement within a simple pipelined CPU architecture
than the linked/store conditional (LL/SC) instruction pair, each of which requires only a single
memory access stage.

4.9.2.10
Advantages of LL/SC over CAS

The LL/SC instruction pair offers two distinct advantages over the single CAS
instruction.
First, because the SC instruction fails whenever any write is performed on
the bus, an LL/SC pair is not prone to the ABA problem.
Second, an LL/SC pair is more pipeline-friendly than a CAS instruction.
The simplest pipeline is comprised of five stages: fetch, decode, execute, mem-
ory access and register write-back. But a CAS instruction requires two mem-
ory access cycles: One to read the memory location so it can be compared to
the expected value, and one to write the result if the comparison passes. This
means that a pipeline that supports CAS has to include an additional memory
access stage that goes unused most of the time. On the other hand, the LL
and SC instructions each only require a single memory access cycle, so they
fit more naturally into a pipeline with only one memory access stage. A com-
parison of CAS and LL/SC from a pipelining perspective is shown in Figure
4.35.

4.9.2.11
Strong and Weak Compare-Exchange

The C++11 standard library provides portable functions for performing atomic
compare-exchange operations. These may be implemented by a CAS instruc-
tion on some target hardware, or by an LL/SC instruction pair on other hard-
ware. Because of the possibility of spurious failures of the store-conditional
instruction, C++11 provides two varieties of compare-exchange: strong and
weak. Strong compare-exchange “hides” spurious SC failures from the pro-


<!-- source-pdf-page: 319 -->

grammer, while weak compare-exchange does not. Search for “Strong Com-
pare and Exchange Lawrence Crowl” online for a paper on the rationale be-
hind strong and weak compare-exchange functions in C++11.

4.9.2.12
Relative Strength of Atomic RMW Instructions

It’s interesting to note that the TAS instruction is weaker than the CAS and
LL/SC instructions in terms of achieving consensus between multiple threads
in a concurrent system. Consensus in this context refers to an agreement be-
tween threads about the value of a shared variable (even if some threads in the
system experience a failure).

Because the TAS instruction only operates on a Boolean value, it can only
solve a problem known as the wait-free consensus problem for two concurrent
threads. The CAS instruction operates on a 32-bit value, so it can solve this
problem for any number of threads.

The topic of wait-free consensus is well beyond our scope here; it’s mostly
of interest when building fault-tolerant systems. If you’re interested in fault
tolerance, you can read more about the consensus problem by searching for
“consensus (computer science)” on Wikipedia.

### 4.9.3 Barriers

Interruptions aren’t the only cause of data race bugs. Compilers and CPUs
also conspire to introduce subtle bugs into our concurrent programs by means
of the instruction reordering optimizations they perform, as described in Section
4.2.5.1.

The cardinal rule of compiler optimizations and out-of-order execution is
that their optimizations shall have no visible effects on the behavior of a single
thread. However, neither the compiler nor the CPU’s control logic has any
knowledge of what other threads might be running in the system, or what
they might be doing. As a result, the cardinal rule isn’t sufficient to prevent
instruction reordering optimizations from introducing bugs into concurrent
programs.

The thread synchronization primitives provided by the operating system
(mutexes et al.) are carefully crafted to avoid the concurrency bugs that can
be caused by instruction reordering optimizations. But now that we’re inves-
tigating how mutexes are implemented, let’s take a look at how to avoid these
problems manually.


<!-- source-pdf-page: 320 -->

4.9.3.1
How Instruction Reordering Causes Concurrency Bugs

As an example of the kinds of problems instruction reordering can cause in
concurrent software, consider again the producer-consumer problem from
Section 4.6.3. We’ve simplified the example, and removed the mutexes so we
can expose the bugs that can be introduced by instruction reordering.

int32_t
g_data = 0;
int32_t
g_ready = 0;

void ProducerThread()
{
// produce some data
g_data = 42;

// inform the consumer
g_ready = 1;
}

void ConsumerThread()
{
// wait for the data to be ready
while (!g_ready)
PAUSE();

// consume the data
ASSERT(g_data == 42);
}

On a CPU on which aligned reads and writes of 32-bit integers are atomic,
this example doesn’t actually require mutexes. However, there’s nothing to
prevent the compiler or the CPU’s out-of-order execution logic from reorder-
ing the producer’s write of 1 into g_ready so that it occurs before the write
of 42 into g_data. Likewise, in theory the compiler could reorder the con-
sumer’s check that g_data is equal to 42 so that it happens before the while
loop. So even though all of our reads and writes are atomic, this code may not
behave reliably.

Instruction reordering really happens at the assembly language level, so
it may be a lot more subtle than a reordering of the statements in a C/C++
program. For example, the following C/C++ code:

A = B + 1;
B = 0;


<!-- source-pdf-page: 321 -->

would produce the following Intel x86 assembly code:

mov
eax,[B]
add
eax,1
mov
[A],eax
mov
[B],0

The compiler could very well reorder the instructions as follows, without pro-
ducing any noticeable effect in a single-threaded execution:

mov
eax,[B]
mov
[B],0
;; write to B before A!
add
eax,1
mov
[A],eax

If a second thread were waiting for B to become zero before reading the value
of A, it would cease to function correctly if this compiler optimization were to
be applied.
Jeff Preshing wrote a great blog post on this topic, available at http://
preshing.com/20120625/memory-ordering-at-compile-time/. (This is where
the assembly language example above comes from.) I highly recommend all
of Jeff’s posts on concurrent programming, so do be sure to check them out.

4.9.3.2
Volatile in C/C++ (and Why It Doesn’t Help Us)

How can we prevent the compiler from reordering critical sequences of reads
and writes? In C and C++, the volatile type qualifier guarantees that con-
secutive reads or writes of a variable cannot be “optimized away” by the com-
piler, so this sounds like a promising idea. However, it doesn’t work reliably
for a number of reasons.
The volatile qualifier in C/C++ was really designed to make memory-
mapped I/O and signal handlers work reliably. As such, the only guarantee it
provides is that the contents of a variable marked volatile won’t be cached
in a register—the variable’s value will be re-read directly from memory every
time it’s accessed. Some compilers do guarantee that instructions will not be
reordered across a read or write of a volatile variable, but not all of them
do, and some only provide this guarantee when targeting certain CPUs, or
only when a particular command-line option is passed to the compiler. The
C and C++ standards don’t require this behavior, so we certainly cannot rely
on it when writing portable code. (See https://msdn.microsoft.com/en-us/
magazine/dn973015.aspx for an in-depth discussion of this topic.)


<!-- source-pdf-page: 322 -->

Moreover, the volatile keyword in C/C++ does nothing to prevent the
CPU’s out-of-order execution logic from reordering the instructions at run-
time. And it cannot prevent cache coherency related issues either (see Section
4.9.3). So, at least in C and C++, the volatile keyword won’t help us to write
reliable concurrent software.8

4.9.3.3
Compiler Barriers

One reliable way of preventing the compiler from reordering read and write
instructions across critical operation boundaries is to explicitly instruct it not
to do so. This can be accomplished by inserting a special pseudoinstruction
into our code known as a compiler barrier.
Different compilers express barriers with different syntax. With GCC, a
compiler barrier can be inserted via some inline assembly syntax as shown
below; under Microsoft Visual C++, the compiler intrinsic _ReadWrite
Barrier() has the same effect.

int32_t
g_data = 0;
int32_t
g_ready = 0;

void ProducerThread()
{
// produce some data
g_data = 42;

// dear compiler: please don't reorder
// instructions across this barrier!
asm volatile("" ::: "memory")

// inform the consumer
g_ready = 1;
}

void ConsumerThread()
{
// wait for the data to be ready
while (!g_ready)
PAUSE();

// dear compiler: please don't reorder
// instructions across this barrier!
asm volatile("" ::: "memory")

8In some languages including Java and C#, the volatile type qualifier does guarantee atomicity,
and can be used to implement concurrent data structures and algorithms. See Section 4.9.6 for
more on this topic.


<!-- source-pdf-page: 323 -->

// consume the data
ASSERT(g_data == 42);
}

There are other ways to prevent the compiler from reordering instructions.
For example, most function calls serve as an implicit compiler barrier. This
makes sense, because the compiler doesn’t know anything9 about the side ef-
fects of a function call. As such, it can’t assume that the state of memory will
be the same before and after the call, which means most optimizations aren’t
permitted across a function call. Some optimizing compilers do make an ex-
ception to this rule for inline functions.
Unfortunately, compiler barriers don’t prevent the CPU’s out-of-order ex-
ecution logic from reordering instructions at runtime. Some ISAs provide a
special instruction for this purpose (e.g., PowerPC’s isync instruction). In
Section 4.9.4.5, we’ll learn about a collection of machine language instructions
known as memory fences which serve as instruction reordering barriers for both
the compiler and the CPU, but more importantly also prevent memory reorder-
ing bugs. So atomic instructions and fences are all we really need to write
reliable mutexes, as well as spin locks and other lock-free algorithms.

### 4.9.4 Memory Ordering Semantics

In Section 4.9.1, we said that in addition to the compiler or CPU actually re-
ordering the machine language instructions in our programs, it’s possible for
read and write instructions to become effectively reordered in a concurrent sys-
tem. Specifically, in a multicore machine with a multilevel memory cache, two
or more cores can sometimes disagree about the apparent order in which a se-
quence of read and write instructions occurred, even when the instructions
were actually executed in the order we intended. Obviously, such disagree-
ments can cause subtle bugs in concurrent software.
These mysterious and vexing problems can only occur on a multicore ma-
chine with a multilevel cache. A single CPU core always “sees” the effects
of its own read and write instructions in the order they were executed; only
when there are two or more cores can disagreements arise. What’s more, dif-
ferent CPUs have different memory ordering behavior, meaning that these

9Function calls only serve as implicit barriers when the compiler is unable to “see” the defi-
nition of the function, such as when the function is defined in a separate translation unit. Link
time optimizations (LTO) can introduce concurrency bugs by providing a way for the compiler’s
optimizer to see the definitions of functions it otherwise could not have seen, thereby effectively
eliminating these implicit barriers.


<!-- source-pdf-page: 324 -->

strange effects can differ from machine to machine when running the exact
same source program!
Thankfully, all is not lost. Every CPU is governed by a strict set of rules
known as its memory ordering semantics. These rules provide various guaran-
tees about how reads and writes propagate between cores, and they provide
programmers with the tools necessary to enforce a particular ordering, when
the default semantics are insufficient.
Some CPUs offer only weak guarantees by default, while others provide
stronger guarantees and hence require less intervention on the part of the pro-
grammer. So, if we can understand how to overcome memory ordering issues
on a CPU with the weakest memory ordering semantics, we can be pretty sure
those techniques will also work on CPUs with stronger default semantics.

4.9.4.1
Memory Caching Revisited

In order to understand how these mysterious memory reordering effects can
occur, we need to look more closely at how a multilevel memory cache works.
In Section 3.5.4, we described in detail how a memory cache avoids the very
high memory access latency of main RAM by keeping frequently-used data in
the cache. This means that as long as a data object is present in the cache, the
CPU will always try to work with that cached copy, rather than reaching out
to access the copy in main RAM.
Let’s briefly review how this works by considering the following simple
(and totally contrived) function:

constexpr int
COUNT = 16;
alignas(64) float
g_values[COUNT];
float
g_sum = 0.0f;

void CalculateSum()
{
g_sum = 0.0f;
for (int i = 0; i < COUNT; ++i)
{
g_sum += g_values[i];
}
}

The first statement sets g_sum to zero. Presuming that the contents of g_sum
aren’t already in the L1 cache, the cache line containing it will be read into L1 at
this point. Likewise, on the first iteration of the loop, the cache line containing
all of the elements of the g_values array will be loaded into L1. (They should
all fit presuming our cache lines are at least 64 bytes wide, because we aligned


<!-- source-pdf-page: 325 -->
> Visual fallback for diagrams/images: [PDF page 325](../../../visual_pages/page_0325.jpg)

the array to a 64-byte boundary with C++11’s alignas specifier.) Subsequent
iterations will read the copy of g_values that resides in the L1 cache, rather
than reading them from main RAM.
During each iteration, g_sum is updated. The compiler might optimize
this by keeping the sum in a register until the end of the loop. But whether
or not this optimization is performed, we know that the g_sum variable will
be written to at some point during this function. When that happens, again
the CPU will write to the copy of g_sum that exists in the L1 cache, rather than
writing directly to main RAM.
Eventually, of course, the “master” copy of g_sum has to be updated. The
memory cache hardware does this automatically by triggering a write-back op-
eration that copies the cache line from L1 back to main RAM. But the write-
back doesn’t usually happen right away; it’s typically deferred until the mod-
ified variable is read again.10

4.9.4.2
Multicore Cache Coherency Protocols

In a multicore machine, memory caching gets a lot more complicated. Figure
4.36 illustrates a simple dual-core machine, in which each core has its own
private L1 cache, and the two cores share an L2 cache and a large bank of main
RAM. To keep the following discussion as simple as possible, let’s ignore the
L2 cache and treat it as being roughly equivalent to main RAM.
Suppose that the simplified producer-consumer example shown in Section
4.9.3.3 is running on this dual-core machine. The producer thread runs on
Core 1, and the consumer thread runs on Core 2. Let’s further assume for the
purposes of this discussion that none of the instructions in either thread have
been reordered.

int32_t
g_data = 0;
int32_t
g_ready = 0;

void ProducerThread() // running on Core 1
{
g_data = 42;
// assume no instruction reordering across this line
g_ready = 1;
}

10Some memory cache hardware does allow cached writes to write-through to main RAM im-
mediately. We can safely ignore write-through caches for the purposes of the present discussion.


<!-- source-pdf-page: 326 -->
> Visual fallback for diagrams/images: [PDF page 326](../../../visual_pages/page_0326.jpg)

Core 0
Core 1

L10
L11

Interconnect Bus (ICB)

Memory Controller

L2 & Main RAM

Figure 4.36. A dual core machine with a local L1 cache per core, connected to a memory controller
via the interconnect bus (ICB). The memory controller implements a cache coherency protocol
such as MESI to ensure that both cores have a consistent view of the contents of memory within
the CPU’s cache coherency domain.

void ConsumerThread() // running on Core 2
{
while (!g_ready)
PAUSE();
// assume no instruction reordering across this line
ASSERT(g_data == 42);
}

Now consider what happens when the producer (on Core 1) writes to
g_ready. In the name of efficiency, this write causes Core 1’s L1 cache to be
updated, but it won’t trigger a write-back to main RAM until some time later.
This means that for some finite amount of time after the write has occurred,
the most up-to-date value of g_ready don’t exist anywhere but inside Core
1’s L1 cache.
Let’s say that the consumer (running on Core 2) attempts to read g_ready
at some time after the producer set it to 1. Like Core 1, Core 2 prefers to
read from the cache whenever possible, to avoid the high cost of reading main
RAM. Core 2’s local L1 cache does not contain a copy of g_ready, but Core 1’s
L1 cache does. So ideally Core 2 would like to ask Core 1 for its copy, because
that would still be a lot less expensive than getting the data from main RAM.
And in this particular case, doing so would also have the distinct advantage
of returning the most up-to-date value.
A cache coherency protocol is a communication mechanism that permits cores
to share data between their local L1 caches in this way. Most CPUs use either
the MESI or MOESI protocol.


<!-- source-pdf-page: 327 -->

4.9.4.3
The MESI Protocol

Under the MESI protocol, each cache line can be in one of four states:

•
Modified. This cache line has been modified (written to) locally.
•
Exclusive. The main RAM memory block corresponding to this cache
line exists only in this core’s L1 cache—no other core has a copy of it.
•
Shared. The main RAM memory block corresponding to this cache line
exists in more than one core’s L1 cache, and all cores have an identical
copy of it.
•
Invalid. This cache line no longer contains valid data—the next read will
need to obtain the line either from another core’s L1 cache, or from main
RAM.

The MOESI protocol adds another state named Owned, which allows cores to
share modified data without writing it back to main RAM first. We’ll focus on
MESI here in the name of simplicity.
Under the MESI protocol, all cores’ L1 caches are connected via a special
bus called the interconnect bus (ICB). Collectively, the L1 caches, any higher-
level caches, and main RAM form what is known as a cache coherency domain.
The protocol ensures that all cores have a consistent “view” of the data in this
domain.
We can get a feel for how the MESI state machine works by returning to
our example.

•
Let’s assume that Core 1 (the producer) first tries to read the current
value of g_ready for some reason. Presuming that this variable doesn’t
already exist in any core’s L1 cache, the cache line that contains it is
loaded into Core 1’s L1 cache. The cache line is put into the Exclusive
state, meaning that no other core has this line.

•
Now let’s say that Core 2 (the consumer) attempts to read g_ready. A
Read message is sent over the ICB. Core 1 has this cache line, so it re-
sponds with a copy of the data. At this point, the cache line is put into
the Shared state on both cores, indicating that both have an identical
copy of the line.

•
Next, the producer on Core 1 writes a 1 into g_ready. This updates the
value in Core 1’s L1 cache, and puts its copy of the line into the Modified
state. An Invalidate message is sent across the ICB, causing Core 2’s copy
of the line to be put into the Invalid state. This indicates that Core 2’s
copy of the line containing g_ready is no longer up-to-date.


<!-- source-pdf-page: 328 -->

•
The next time that Core 2 (the consumer) tries to read g_ready, it finds
that its locally-cached copy is Invalid. It sends a Read message across the
ICB and obtains the newly-modified line from Core 1’s L1 cache. This
causes both cores’ cache lines to be put into the Shared state once again.
It also triggers a write-back of the line to main RAM.

A complete discussion of the MESI protocol is beyond our scope, but this ex-
ample should give you a good feel for how it works to allow multiple cores to
share data between their L1 caches while minimizing accesses to main RAM.

4.9.4.4
How MESI Can Go Wrong

Based on the discussion of the MESI protocol in the preceding section, it would
seem that the problem of data sharing between L1 caches in a multicore ma-
chine has been solved in a watertight way. How, then, can the memory order-
ing bugs we’ve hinted at actually happen?
There’s a one-word answer to that question: Optimization. On most hard-
ware, the MESI protocol is highly optimized to minimize latency. This means
that some operations aren’t actually performed immediately when messages
are received over the ICB. Instead, they are deferred to save time. As with
compiler optimizations and CPU out-of-order execution optimizations, MESI
optimizations are carefully crafted so as to be undetectable by a single thread.
But, as you might expect, concurrent programs once again get the raw end of
this deal.
For example, our producer (running on Core 1) writes 42 into g_data and
then immediately writes 1 into g_ready. Under certain circumstances, op-
timizations in the MESI protocol can cause the new value of g_ready to be-
come visible to other cores within the cache coherency domain before the up-
dated value of g_data becomes visible. This can happen, for example, if Core
1 already has g_ready’s cache line in its local L1 cache, but does not have
g_data’s line yet. This means that the consumer (on Core 2) can potentially
see a value of 1 for g_ready before it sees a value of 42 in g_data, resulting in
a data race bug.
This state of affairs can be summarized as follows:

Optimizations within a cache coherency protocol can make two
read and/or write instructions appear to happen, from the point of
view of other cores in the system, in an order that is opposite to
the order in which the instructions were actually executed.


<!-- source-pdf-page: 329 -->

4.9.4.5
Memory Fences

When the apparent order of two instructions gets reversed by our cache co-
herency protocol, we say that the first instruction (in program order) has passed
the second. There are four ways in which one instruction can pass another:

1.
A read can pass another read,

2.
a read can pass a write,

3.
a write can pass another write, or

4.
a write can pass a read.

To prevent the memory effects of a read or write instruction passing other
reads and/or writes, modern CPUs provide special machine language instruc-
tions known as memory fences, also known as memory barriers.
In theory, a CPU could provide individual fence instructions to prevent
each of these four cases from happening. For example, a ReadRead fence would
only prevent reads from passing other reads, but would not prevent any of the
other cases. Also, a fence instruction could be unidirectional or bidirectional.
A one-way fence would guarantee that no reads or writes that precede it in pro-
gram order can end up having an effect after it, but not vice-versa. A bidirec-
tional fence, on the other hand, would prevent “leakage” of memory effects in
either direction across the fence. Theoretically, then, we could imagine a CPU
that provides twelve distinct fence instructions—a bidirectional, forward, and
reverse variant of each of the four basic fence types listed above.
Thankfully real CPUs don’t usually provide all twelve kinds of fences. In-
stead, an ISA typically specifies a handful of fence instructions which serve as
combinations of these theoretical fence types.
The strongest kind of fence is called a full fence. It ensures that all reads
and writes occurring before the fence in program order will never appear to
have occurred after it, and likewise that all reads and writes occurring after it
will never appear to have happened before it. In other words, a full fence is a
two-way barrier that affects both reads and writes.
A full fence is actually very expensive to realize in hardware. CPU de-
signers don’t like forcing programmers to use an expensive construct when a
cheaper one will do, so most CPUs provide a variety of less-expensive fence
instructions which provide weaker guarantees than those provided by a full
fence.
All fence instructions have two very useful side-effects:

1.
They serve as compiler barriers, and


<!-- source-pdf-page: 330 -->

2.
they prevent the CPU’s out-of-order logic from reordering instructions
across the fence.

This means that when we use a fence to prevent the memory ordering bugs
that are caused by our CPU’s cache coherency protocol, it also serves to prevent
instruction reordering. So atomic instructions and memory fences are all we
really need to write reliable mutexes and spin locks, and to write other lock-free
algorithms as well.

4.9.4.6
Acquire and Release Semantics

No matter what the specific fence instructions look like under a particular ISA,
we can reason about their effects by thinking in terms of the semantics they
provide—in other words, the guarantees they enforce about the behavior of
reads and writes in the system.
Memory ordering semantics are really properties of read or write instruc-
tions, not properties of the fences themselves. The fences merely provide a
way for programmers to ensure that a read or write instruction has a particu-
lar memory ordering semantic. There are really only three memory ordering
semantics we typically need to worry about:

•
Release semantics. This semantic guarantees that a write to shared mem-
ory can never be passed by any other read or write that precedes it in
program order. When this semantic is applied to a shared write, we call
it a write-release. This semantic operates in the forward direction only—it
says nothing about preventing memory operations that occur after the
write-release from appearing to happen before it.

•
Acquire semantics.
This semantic guarantees that a read from shared
memory can never be passed by any other read or write that occurs af-
ter it in program order. When this semantic is applied to a shared read,
we call it a read-acquire. This semantic operates in the reverse direction
only—it does nothing to prevent memory operations that occur before
the read-acquire from having their effects seen after it.

•
Full fence semantics. This bidirectional semantic ensures that all memory
operations appear to occur in program order across the boundary created
by a fence instruction in the code. No read or write that occurs before the
fence in program order can appear to have occurred after the fence, and
likewise no read or write that is after the fence in program order can
appear to have occurred before it.


<!-- source-pdf-page: 331 -->

The individual fence instructions provided by any particular ISA typically pro-
vide at least one of these three memory ordering semantics. The details of how
each fence instruction actually provides these semantic guarantees are CPU-
specific, and for the most part as concurrent programmers, we don’t care. As
long as we can express the concept of write-release, read-acquire and full fence
in our source code, we should be able to write a reliable spin lock or code other
lock-free algorithms.

4.9.4.7
When to Use Acquire and Release Semantics

A write-release is most often used in a producer scenario—in which a thread
performs two consecutive writes (e.g., writing to g_data and then g_ready),
and we need to ensure that all other threads will see the two writes in the cor-
rect order. We can enforce this ordering by making the second of these two
writes a write-release. To implement this, a fence instruction that provides re-
lease semantics is placed before the write-release instruction. Technically speak-
ing, when a core executes a fence instruction with release semantics, it waits
until all prior writes have been fully committed to memory within the cache
coherency domain before executing the second write (the write-release).
A read-acquire is typically used in a consumer scenario—in which a thread
performs two consecutive reads in which the second is conditional on the first
(e.g., only reading g_data after a read of the flag g_ready comes back true).
We enforce this ordering by making sure that the first read is a read-acquire.
To implement this, a fence instruction that provides acquire semantics is placed
after the read-acquire instruction. Technically speaking, when a core executes
a fence instruction with acquire semantics, it waits until all writes from other
cores have been fully flushed into the cache coherency domain before it con-
tinues on to execute the second read. This ensures that the second read will
never appear to have occurred before the read-acquire.
Here’s our producer-consumer example again, in full lock-free glory, using
acquire and release fences to impose the necessary memory ordering seman-
tics:

int32_t
g_data = 0;
int32_t
g_ready = 0;

void ProducerThread() // running on Core 1
{
g_data = 42;

// make the write to g_ready into a write-release
// by placing a release fence *before* it
RELEASE_FENCE();


<!-- source-pdf-page: 332 -->

g_ready = 1;
}

void ConsumerThread() // running on Core 2
{
// make the read of g_ready into a read-acquire
// by placing an acquire fence *after* it
while (!g_ready)
PAUSE();
ACQUIRE_FENCE();

// we can now read g_data safely...
ASSERT(g_data == 42);
}

For an excellent in-depth presentation of exactly why acquire and release
fences are required under the MESI cache coherency protocol, see http://
www.swedishcoding.com/2017/11/10/multi-core-programming-and-cache-
coherency/.

4.9.4.8
CPU Memory Models

We mentioned in Section 4.9.4 that some CPUs provide stronger memory order
semantics by default than others. On a CPU with strong memory semantics,
read and/or write instructions behave like a certain kind of fence by default,
without the programmer having to specify a fence instruction explicitly. For
example, the DEC Alpha has notoriously weak semantics by default, requiring
careful fencing in almost all situations. At the other end of the spectrum, an
Intel x86 CPU has quite strong memory ordering semantics by default. For a
great discussion of weak and strong memory ordering, see http://preshing.
com/20120930/weak-vs-strong-memory-models/.

4.9.4.9
Fence Instructions on Real CPUs

Now that we understand the theory behind memory ordering semantics, let’s
take a very brief look at memory fence instructions on some real CPUs.
The Intel x86 ISA specifies three fence instructions: sfence provides re-
lease semantics, lfence provides acquire semantics, and mfence acts as a
full fence. Certain x86 instructions may also be prefixed by a lock modifier
to make them behave atomically, and to provide a memory fence prior to ex-
ecution of the instruction. The x86 ISA is strongly ordered by default, mean-
ing that fences aren’t actually required in many cases where they would be
on CPUs with weaker default ordering semantics. But there are some cases
in which these fence instructions are required. For an example, see the post


<!-- source-pdf-page: 333 -->

entitled, “Who ordered memory fences on an x86?”
by Bartosz Milewski
(https://bit.ly/2HuXpfo).
The PowerPC ISA is quite weakly ordered, so explicit fence instructions
are usually required to ensure correct semantics. The PowerPC makes a dis-
tinction between reads and writes to memory versus reads and writes to I/O
devices, and hence it offers a variety of fence instructions that differ primar-
ily in how they handle memory versus I/O. A full fence on PowerPC is pro-
vided by the sync instruction, but there’s also a “lightweight” fence called
lwsync, a fence for I/O operations called eieio (ensure in-order execu-
tion of I/O), and even a pure instruction reordering barrier isync that does
not provide any memory ordering semantics. You can read more about the
PowerPC’s fence instructions here: https://www.ibm.com/developerworks/
systems/articles/powerpc.html.
The ARM ISA provides a pure instruction reordering barrier called isb,
two full memory fence instructions dmb and dsb, a one-way read-acquire in-
struction ldar and a one-way write-release instruction stlr. Interestingly,
this ISA rolls acquire and release semantics into the read and write instruc-
tions themselves, rather than as separate fence instructions. For more infor-
mation, see http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.
den0024a/CJAIAJFI.html.

### 4.9.5 Atomic Variables

Using atomic instructions and memory fences directly can be tedious and
error-prone, not to mention being totally non-portable.
Thankfully, as
of C++11, the class template std::atomic<T> allows virtually any data
type to be converted into an atomic variable.
(A specialized class called
std::atomic_flag encapsulates an atomic Boolean variable.) In addition
to atomicity, the std::atomic family of templates provides its variables with
“full fence” memory ordering semantics by default (although weaker seman-
tics can be specified if desired). This frees us to write lock-free code without
having to worry about any of the three causes of data race bugs.
Using these facilities, our producer-consumer example can be written as
follows:

std::atomic<float> g_data;
std::atomic_flag g_ready = false;

void ProducerThread()
{
// produce some data
g_data = 42;


<!-- source-pdf-page: 334 -->

// inform the consumer
g_ready = true;
}

void ConsumerThread()
{
// wait for the data to be ready
while (!g_ready)
PAUSE();

// consume the data
ASSERT(g_data == 42);
}

Notice that this code looks nearly identical to the erroneous code we first pre-
sented when talking about race conditions in Section 4.5.3. By simply wrap-
ping our variables in std::atomic, we’ve converted a concurrent program
that is prone to data race bugs into one that is race-free.
Under the hood, the implementations of std::atomic<T> and std::
atomic_flag are, of course, complex. The standard C++ library has to be
portable, so its implementation makes use of whichever atomic and barrier
machine language instructions happen to be available on the target platform.
What’s more, the std::atomic<T> template can wrap any type imagin-
able, but of course CPUs don’t provide atomic instructions for manipulating
arbitrarily-sized data objects. As such, the std::atomic<T> template must
be specialized by size: When the template is applied to a 32- or 64-bit type, it
can be implemented without locks, using atomic machine language instruc-
tions directly. But when it is applied to larger types, mutex locks are used
to ensure correct atomic behavior. (You can call is_lock_free() on any
atomic variable to find out whether its implementation really is lock-free on
your target hardware.)

4.9.5.1
C++ Memory Order

By default, C++ atomic variables make use of full memory barriers, to ensure
that they will work correctly in all possible use cases. However, it is possible
to relax these guarantees, by passing a memory order semantic (an optional ar-
gument of type std::memory_order) to functions that manipulate atomic
variables. The documentation regarding std::memory_order is pretty con-
fusing, so let’s demystify it. Here are the possible memory order settings, and
what they mean:

1.
Relaxed. An atomic operation performed with relaxed memory order


<!-- source-pdf-page: 335 -->

semantics guarantees only atomicity. No barrier or fence is used.

2.
Consume. A read performed with consume semantics guarantees that
no other read or write within the same thread can be reordered before this
read. In other words, this semantic only serves to prevent compiler op-
timizations and out-of-order execution from reordering the instructions—
it does nothing to ensure any particular memory ordering semantics
within the cache coherency domain. It is typically implemented with
an instruction reordering barrier like the PowerPC’s isync instruction.

3.
Release. A write performed with release semantics guarantees that no
other read or write in this thread can be reordered after it, and the write
is guaranteed to be visible by other threads reading the same address. It
employs a release fence in the CPU’s cache coherency domain to accom-
plish this.

4.
Acquire. A read performed with acquire semantics guarantees consume
semantics, and in addition it guarantees that writes to the same address
by other threads will be visible to this thread. It does this via an acquire
fence in the CPU’s cache coherency domain.

5.
Acquire/Release. This semantic (the default) is the safest, because it ap-
plies a full memory fence.

It’s important to realize that using a memory ordering specifier doesn’t guar-
antee that a particular semantic will actually be used on every platform. All it
does is to guarantee that the semantics will be at least that strong—a stronger
semantic might be employed on some target hardware. For example, on Intel
x86, the relaxed memory order isn’t possible because the CPU’s default mem-
ory ordering semantics are relatively strong. On an Intel CPU, any request for
a relaxed read operation will actually end up having acquire semantics.
Using these memory order specifiers requires switching from std::
atomic’s overloaded assignment and cast operators to explicit calls to
store() and load(). Here’s our simple producer-consumer example again,
this time using std::memory_order specifiers to provide our release and
acquire barriers:

std::atomic<float> g_data;
std::atomic<bool> g_ready = false;

void ProducerThread()
{
// produce some data


<!-- source-pdf-page: 336 -->

g_data.store(42, std::memory_order_relaxed);

// inform the consumer
g_ready.store(true, std::memory_order_release);
}

void ConsumerThread()
{
// wait for the data to be ready
while (!g_ready.load(std::memory_order_acquire))
PAUSE();

// consume the data
ASSERT(g_data.load(std::memory_order_relaxed) == 42);
}

It’s important to remember the 80/20 rule when using “relaxed” memory
ordering semantics like this. These semantics are easy to get wrong, so you
should probably only use non-default memory ordering with std::atomic
when you can prove, via profiling, that the performance improvement is re-
ally necessary—and that changing your code to use explicit memory ordering
semantics does, in fact, produce the benefits you expect!
A complete discussion of how to use memory ordering semantics in C++11
is beyond our scope, but you can read more by searching online for the article
entitled, “Implementing Scalable Atomic Locks for Multi-Core Intel® EM64T
and IA32 Architectures” by Michael Chynoweth. This forum discussion of-
fers some interesting insights as well, and illustrates just how complex this
kind of programming can get: https://groups.google.com/forum/#!topic/
boost-developers-archive/Qlrat5ASrnM.

### 4.9.6 Concurrency in Interpreted Programming Languages

Thus far we’ve only discussed concurrency in the context of compiled lan-
guages like C and C++, and in assembly language. These languages com-
pile or assemble down to raw machine code that is executed directly by the
CPU. As such, atomic operations and locks must be implemented with the aid
of special machine language instructions that provide atomic operations and
cache coherent memory barriers, with additional help from the kernel (which
makes sure threads are put to sleep and awoken appropriately) and the com-
piler (which respects barrier instructions when optimizing the code).
The story is somewhat different for interpreted programming languages
like Java and C#. Programs written in these languages execute in the context
of a virtual machine (VM): Java programs run inside the Java Virtual Machine


<!-- source-pdf-page: 337 -->

(JVM), and C# programs run within the context of the Common Language
Runtime (CLR). A virtual machine is essentially a software emulation of a
CPU, reading bytecoded instructions one by one and executing them. A VM
also acts a bit like an emulated operating system kernel: It provides its own
notion of “threads” (comprised of bytecoded instructions), and it handles all
of the details of scheduling those threads itself. Because the operation of a VM
is implemented entirely in software, an interpreted language like Java or C#
can provide powerful concurrent synchronization facilities that are not as con-
strained by the hardware as they would be in a compiled language like C or
C++.
One example of this principle in action is the volatile type qualifier. We
said in Section 4.6 that in C/C++ a volatile variable is not atomic. How-
ever, in Java and C#, the volatile type qualifier does guarantee atomicity.
Operations on volatile variables in these languages cannot be optimized, and
they cannot be interrupted by another thread. Moreover, all reads of a volatile
variable in Java and C# are effectively performed directly from main memory
rather than from the cache, and likewise all writes are effectively written to
main RAM rather than to the cache. All of these guarantees can be provided
in part because the virtual machine has full control over the execution of the
bytecoded instruction streams that comprise each app.
A full discussion of the atomicity and thread synchronization facilities pro-
vided by interpreted languages like C# and Java is well beyond the scope of
this book. But now that you have a solid understanding of the principles
behind atomicity at the lowest levels, understanding these facilities in other
higher-level languages should be a snap. For further reading, the following
websites are a good start:

•
C#: Search for “Parallel Processing and Concurrency in the .NET Frame-
work” on https://docs.microsoft.com.

•
Java: https://docs.oracle.com/javase/tutorial/essential/concurrency/.

### 4.9.7 Spin Locks

When discussing atomic machine language instructions in Section 4.9.2.2, we
presented some code snippets illustrating how a spin lock might be imple-
mented using each of those instructions. But because of instruction reordering
and memory ordering semantics, those examples weren’t 100% correct. In this
section, we’ll present an industrial-strength spin lock, and then explore a few
useful variants.


<!-- source-pdf-page: 338 -->

4.9.7.1
Basic Spin Lock

A spin lock can be implemented using a std::atomic_flag, either wrapped
in a C++ class or accessed via a simple functional API. The spin lock is acquired
by using a TAS instruction to atomically set the flag to true, retrying in a while
loop until the TAS succeeds. It is unlocked by atomically writing a value of
false into the flag.
When acquiring a spin lock, it’s important to use read-acquire memory or-
dering semantics to read the lock’s current contents as part of the TAS opera-
tion. This fence guards against the rare scenario in which the lock is observed
as having been released when in reality some other thread hasn’t completely
exited its critical section yet. In C++11, this can be accomplished by pass-
ing std::memory_order_acquire to the test_and_set() call. In raw
assembly language, we’d place an acquire fence instruction after the TAS in-
struction.
When releasing a spin lock, it’s likewise important to use write-release se-
mantics to ensure that all writes performed after the call to Unlock() aren’t
observed by other threads as if they had happened before the lock was re-
leased.
Here’s a complete implementation of a TAS-based spin lock, using correct
and minimal memory ordering semantics:

class SpinLock
{
std::atomic_flag
m_atomic;

public:
SpinLock() : m_atomic(false) { }

bool TryAcquire()
{
// use an acquire fence to ensure all subsequent
// reads by this thread will be valid
bool alreadyLocked = m_atomic.test_and_set(
std::memory_order_acquire);

return !alreadyLocked;
}

void Acquire()
{
// spin until successful acquire
while (!TryAcquire())
{


<!-- source-pdf-page: 339 -->

// reduce power consumption on Intel CPUs
// (can substitute with std::this_thread::yield()
// on platforms that don't support CPU pause, if
// thread contention is expected to be high)
PAUSE();
}
}

void Release()
{
// use release semantics to ensure that all prior
// writes have been fully committed before we unlock
m_atomic.clear(std::memory_order_release);
}
};

4.9.7.2
Scoped Locks

It’s often inconvenient and error-prone to manually unlock a mutex or spin
lock, especially when the function using the lock has multiple return sites. In
C++ we can use a simple wrapper class called a scoped lock to ensure that a lock
is automatically released when a particular scope is exited. It works by simply
acquiring the lock in the constructor, and releasing it in the destructor.

template<class LOCK>
class ScopedLock
{
typedef LOCK lock_t;
lock_t* m_pLock;

public:
explicit ScopedLock(lock_t& lock) : m_pLock(&lock)
{
m_pLock->Acquire();
}

~ScopedLock()
{
m_pLock->Release();
}
};

This scoped lock class can be used with any spin lock or mutex that
has a conformant interface (i.e., any lock class that supports the functions
Acquire() and Release()). Here’s how it might be used:


<!-- source-pdf-page: 340 -->

SpinLock g_lock;

int ThreadSafeFunction()
{
// the scoped lock acts like a "janitor"
// because it cleans up for us!
ScopedLock<decltype(g_lock)> janitor(g_lock);

// do some work...

if (SomethingWentWrong())
{
// lock will be released here
return -1;
}

// so some more work...

// lock will also be released here
return 0;
}

4.9.7.3
Reentrant Locks

A vanilla spin lock will cause a thread to deadlock if it ever tries to reacquire a
lock that it already holds. This can happen whenever two or more thread-safe
functions attempt to call one another reentrantly from within the same thread.
For example, given two functions:

SpinLock g_lock;

void A()
{
ScopedLock<decltype(g_lock)> janitor(g_lock);

// do some work...
}

void B()
{
ScopedLock<decltype(g_lock)> janitor(g_lock);

// do some work...

// make a call to A() while holding the lock
A();
// deadlock!


<!-- source-pdf-page: 341 -->

// do some more work...
}

We can relax this reentrancy restriction if we can arrange for our spin lock
class to cache the id of the thread that has locked it. That way, the lock can
“know” the difference between a thread trying to reacquire its own lock (which
we wish to allow) versus a thread trying to grab a lock that’s already held by a
different thread (which should cause the caller to wait). To make sure that calls
to Acquire() and Release() are done in matching pairs, we’ll also want to
add a reference count to the class. Here’s a functional implementation, using
appropriate memory fencing:

class ReentrantLock32
{
std::atomic<std::size_t>
m_atomic;
std::int32_t
m_refCount;

public:
ReentrantLock32() : m_atomic(0), m_refCount(0) { }

void Acquire()
{
std::hash<std::thread::id> hasher;
std::size_t tid = hasher(std::this_thread::get_id());

// if this thread doesn't already hold the lock...
if (m_atomic.load(std::memory_order_relaxed) != tid)
{
// ... spin wait until we do hold it
std::size_t unlockValue = 0;
while (!m_atomic.compare_exchange_weak(
unlockValue,
tid,
std::memory_order_relaxed, // fence below!
std::memory_order_relaxed))
{
unlockValue = 0;
PAUSE();
}
}

// increment reference count so we can verify that
// Acquire() and Release() are called in pairs
++m_refCount;


<!-- source-pdf-page: 342 -->

// use an acquire fence to ensure all subsequent
// reads by this thread will be valid
std::atomic_thread_fence(std::memory_order_acquire);
}

void Release()
{
// use release semantics to ensure that all prior
// writes have been fully committed before we unlock
std::atomic_thread_fence(std::memory_order_release);

std::hash<std::thread::id> hasher;
std::size_t tid = hasher(std::this_thread::get_id());
std::size_t actual = m_atomic.load(std::memory_order_relaxed);
assert(actual == tid);

--m_refCount;
if (m_refCount == 0)
{
// release lock, which is safe because we own it
m_atomic.store(0, std::memory_order_relaxed);
}
}

bool TryAcquire()
{
std::hash<std::thread::id> hasher;
std::size_t tid = hasher(std::this_thread::get_id());

bool acquired = false;

if (m_atomic.load(std::memory_order_relaxed) == tid)
{
acquired = true;
}
else
{
std::size_t unlockValue = 0;
acquired = m_atomic.compare_exchange_strong(
unlockValue,
tid,
std::memory_order_relaxed, // fence below!
std::memory_order_relaxed);
}
if (acquired)
{
++m_refCount;


<!-- source-pdf-page: 343 -->

std::atomic_thread_fence(
std::memory_order_acquire);
}
return acquired;
}
};

4.9.7.4
Readers-Writer Locks

In a system in which multiple threads can read and write a shared data object,
we could control access to the object using a mutex or a spin lock. However,
multiple threads should be allowed to read the shared object concurrently. It’s
only when the shared object is being mutated that we need to ensure mutual
exclusivity. What we’d like is a kind of lock that allows any number of readers
to acquire it concurrently. Whenever a writer thread attempts to acquire the
lock, it should wait until all readers are finished, and then it should acquire the
lock in a special “exclusive” mode that prevents any other readers or writers
from gaining access until it has completed its mutation of the shared object.
This kind of lock is called a readers-writer lock (also known as a shared-exclusive
lock or a push lock).
We can implement a readers-writer lock in a manner similar to how we
implemented our reentrant lock. However, instead of storing the thread id in
the atomic variable, we’ll store a reference count indicating how many read-
ers currently hold the lock. Each time a reader acquires the lock, the count is
incremented; each time a reader releases the lock, the count is decremented.
But how, then, can we also provide an “exclusive” lock mode for the
writer? All we need to do is to reserve one (very high) reference count value
and use it to denote that a writer currently holds the lock. If our reference
count is an unsigned 32-bit integer, the value 0xFFFFFFFFU could do nicely
as the reserved value. Even easier, we can simply reserve the most-significant
bit, meaning that reference counts from 0 to 0x7FFFFFFFU represent reader
locks, and the reserved value 0x80000000U represents a writer lock (with no
other values being valid).
A readers-writer lock suffers from starvation problems: A writer that holds
the lock too long can cause all of the readers to starve, and likewise a lot of read-
ers can cause the writers to starve. A sequential lock is one possible alternative
that tackles the starvation issue (see https://en.wikipedia.org/wiki/Seqlock
for details). Check out https://lwn.net/Articles/262464 for a description of
yet another interesting locking technique, used in the Linux kernel, that sup-
ports multiple concurrent readers and writers, called read-copy-update (RCU).


<!-- source-pdf-page: 344 -->

We’ll leave the implementation of a readers-writer lock up to you as an ex-
ercise! However, if you’d like to compare notes, you can find a fully-functional
implementation on this book’s website (www.gameenginebook.com).

4.9.7.5
Lock-Not-Needed Assertions

No matter how you slice it, locks are expensive. Mutexes are expensive even
in the absence of contention. In a low-contention scenario, spin locks are rela-
tively cheap, but they still introduce a non-zero cost into any piece of software.
It’s often the case that the programmer knows a priori that a lock isn’t re-
quired. In a game engine, for example, each iteration of the game loop is usu-
ally performed in distinct stages. If a shared data structure is accessed exclu-
sively by a single thread early in the frame, and that same data is accessed
again by a single thread later in the frame, then we don’t actually need a lock.
Yes, in theory those two threads could overlap, and if they were to do so a lock
would definitely be needed. But in practice, given the way our game loop is
structured, we might know that such overlap can never occur.
In this kind of situation, we have a few choices. We could put the locks
in, just in case. That way, if someone rearranges the order in which things are
done during the frame, and ends up making these threads overlap, we’d be
covered. Another option is to just ignore the possibility of overlap and not
lock anything.
There is a third option which I find more appealing in such scenarios: We
can assert that a lock isn’t required. This has two benefits. First, it can be done
very cheaply, and in fact the assertions can be stripped prior to shipping your
game. Second, it automatically detects problems if our assumptions about the
overlap of the threads proves to be incorrect—or if the assumptions are broken
later on by a refactor of the code. There’s no standardized name for this kind
of assertion, so we’ll call them lock-not-needed assertions in this book.
So how do we detect that a lock is needed? One way would be to use
an atomic Boolean variable, complete with proper memory fencing, and use it
like a mutex. Except that instead of actually acquiring a mutex lock, we would
simply assert that the Boolean is false, and then set it to true atomically. And
instead of releasing a lock, we assert that our Boolean is true, and then set it to
false atomically. This approach would work, but it would be just as expensive
as an uncontended spin lock. We can do better.
The “trick” is to realize that we only care about detecting overlaps between
critical operations on a shared object. And that detection needn’t be 100% re-
liable. A 90% hit rate is probably just fine. If two critical operations ever do
overlap, there may be times when we fail to detect it. But if your game is be-
ing run multiple times a day, every day, by a team of 100 or more developers,


<!-- source-pdf-page: 345 -->

plus a QA department consisting of at least another 10 or 20 people, you can
be pretty sure someone will detect the problem if one exists.
So, instead of an atomic Boolean, we’ll just use a volatile Boolean. As
we’ve stated, the volatile keyword doesn’t do much to prevent concurrent
race bugs. But it does guarantee that reads and writes of the Boolean won’t
be optimized away, and that’s really all we need. We’ll get a reasonably good
detection rate, and the test is dirt cheap.

class UnnecessaryLock
{
volatile bool
m_locked;

public:
void Acquire()
{
// assert no one already has the lock
assert(!m_locked);

// now lock (so we can detect overlapping
// critical operations if they happen)
m_locked = true;
}
void Release()
{
// assert correct usage (that Release()
// is only called after Acquire())
assert(m_locked);

// unlock
m_locked = false;
}
};

#if ASSERTIONS_ENABLED
#define BEGIN_ASSERT_LOCK_NOT_NECESSARY(L) (L).Acquire()
#define END_ASSERT_LOCK_NOT_NECESSARY(L)
(L).Release()
#else
#define BEGIN_ASSERT_LOCK_NOT_NECESSARY(L)
#define END_ASSERT_LOCK_NOT_NECESSARY(L)
#endif

// Example usage...

UnnecessaryLock g_lock;


<!-- source-pdf-page: 346 -->

void EveryCriticalOperation()
{
BEGIN_ASSERT_LOCK_NOT_NECESSARY(g_lock);

printf("perform critical op...\n");

END_ASSERT_LOCK_NOT_NECESSARY(g_lock);
}

We could also wrap the locks in a janitor (see Section 3.1.1.6), like this:

class UnnecessaryLockJanitor
{
UnnecessaryLock* m_pLock;
public:
explicit
UnnecessaryLockJanitor(UnnecessaryLock& lock)
: m_pLock(&lock) { m_pLock->Acquire(); }
~UnnecessaryLockJanitor() { m_pLock->Release(); }
};

#if ASSERTIONS_ENABLED
#define ASSERT_LOCK_NOT_NECESSARY(J,L) \
UnnecessaryLockJanitor J(L)
#else
#define ASSERT_LOCK_NOT_NECESSARY(J,L)
#endif

// Example usage...

UnnecessaryLock g_lock;

void EveryCriticalOperation()
{
ASSERT_LOCK_NOT_NECESSARY(janitor, g_lock);

printf("perform critical op...\n");
}

We implemented this at Naughty Dog and it successfully caught a num-
ber of cases of critical operations overlapping when the programmers had as-
sumed they never could do so. So this little gem is tried and true.

### 4.9.8 Lock-Free Transactions

This is supposed to be a section on lock-free programming, but thus far we’ve
spent all of our time on writing spin locks! Perhaps counterintuitively, the


<!-- source-pdf-page: 347 -->

act of writing a spin lock is an example of lock-free programming, from the
perspective of the implementation of the spin lock itself. We’ve also learned a
lot about atomic instructions, compiler barriers and memory fences along the
way. So this has been a useful exercise. However, we haven’t really explored
the principles of lock-free programming per se; for that purpose it will be in-
structive to look at an example other than a spin lock. The topic of lock-free
and non-blocking algorithms is huge. It really deserves its own book, so we
won’t attempt to cover the topic in depth here. But we can at least get a feel
for how lock-free approaches usually work.
The goal of lock-free programming is of course to avoid taking locks that
will either put a thread to sleep, or cause it to get caught up in a busy-wait
loop inside a spin lock. To perform a critical operation in a lock-free manner,
we need to think of each such operation as a transaction that can either succeed
in its entirety, or fail in its entirety. If it fails, the transaction is simply retried
until it does succeed.
To implement any transaction, no matter how complex, we perform the
majority of the work locally (i.e., using data that’s visible only to the current
thread, rather than operating directly on the shared data). When all of our
ducks are in a row and the transaction is ready to commit, we execute a single
atomic instruction, such as CAS or LL/SC. If this atomic instruction succeeds,
we have successfully “published” our transaction globally—it becomes a per-
manent part of the shared data structure on which we’re operating. But if the
atomic instruction fails, that means some other thread was attempting to com-
mit a transaction at the same time we were.
This fail-and-retry approach works because whenever one thread fails to
commit its transaction, we know that it was because some other thread man-
aged to succeed. As a result, one thread in the system is always making forward
progress (just maybe not us). And that is the definition of lock-free.

### 4.9.9 A Lock-Free Linked List

As a concrete example, let’s look at a simple lock-free singly-linked list. The
only operation we’ll support in this discussion is push_front().
To prepare the transaction, we allocate the new Node and populate it with
data. We also set its next pointer to point to whichever node is currently at the
head of the linked list. The transaction is now ready to commit atomically.
The commit itself consists of a call to compare_exchange_weak() on
the head pointer, which we declared as an atomic pointer to a Node. If this
call succeeds, we’ve inserted our new node at the head of the linked list and
we’re done. But if it fails, we need to retry. This involves re-initializing our


<!-- source-pdf-page: 348 -->
> Visual fallback for diagrams/images: [PDF page 348](../../../visual_pages/page_0348.jpg)

Step 1: Prepare Transaction

Head
A
B

C

Step 2: Attempt CAS with Head
(Retry if Head no longer points to A)

CAS
Head
A
B

C

Figure 4.37. A lock-free implementation of insertion at the head of a singly-linked list. Top: The
transaction is prepared by setting the next pointer of the new node to point to the current head
of the list. Bottom: The transaction is committed by using an atomic CAS operation to swap the
head pointer with a pointer to the new node. If the CAS fails, we return to the top and try again
until it succeeds.

new node’s next pointer to point to what is now potentially a brand new head
node (presumably inserted by another thread—this is perhaps why we failed
in the first place). This two-stage process is illustrated in Figure 4.37.
In the code below, you won’t see an explicit re-initialization of the node’s
next pointer. That’s because compare_exchange_weak() does the re-init-
ialization step for us. (How convenient!) Here’s what the code would look
like:

template< class T >
class SList
{
struct Node
{
T
m_data;
Node*
m_pNext;
};
std::atomic< Node* > m_head { nullptr };

public:
void push_front(T data)
{
// prep the transaction locally
auto pNode = new Node();
pNode->m_data = data;


<!-- source-pdf-page: 349 -->

pNode->m_pNext = m_head;

// commit the transaction atomically
// (retrying until it succeeds)
while (!m_head.compare_exchange_weak(
pNode->m_pNext, pNode))
{ }
}
};

### 4.9.10 Further Reading on Lock-Free Programming

Concurrency is a vast and profound topic, and in this chapter we’ve only just
scratched the surface. As always, the goal of this book is merely to build aware-
ness and serve as a jumping-off point for further learning.

•
For a complete discussion of implementing a lock-free singly-linked list,
check out Herb Sutter’s talk at CppCon 2014, which is where the example
above came from. The talk is available on YouTube in two parts:

◦
https://www.youtube.com/watch?v=c1gO9aB9nbs, and
◦
https://www.youtube.com/watch?v=CmxkPChOcvw.

•
This lecture by Geoff Langdale of CMU provides a great overview: https:
//www.cs.cmu.edu/~410-s05/lectures/L31_LockFree.pdf.

•
Also be sure to check out this presentation by Samy Al Bahra for a
clear and easily digestible overview of pretty much every topic under
the sun related to concurrent programming: http://concurrencykit.org/
presentations/lockfree_introduction/#/.

•
Mike Acton’s excellent talk on concurrent thinking is a must-read; it
is available at http://cellperformance.beyond3d.com/articles/public/
concurrency_rabit_hole.pdf.

•
These two online books are great resources for concurrent programming:
http://greenteapress.com/semaphores/LittleBookOfSemaphores.pdf and
https://www.kernel.org/pub/linux/kernel/people/paulmck/perfbook/
perfbook.2011.01.02a.pdf.

•
Some excellent articles on lock-free programming and how atomics, bar-
riers and fences work can be found on Jeff Preshing’s blog: http://
preshing.com/20120612/an-introduction-to-lock-free-programming.


<!-- source-pdf-page: 350 -->

•
This page has great information about memory barriers on Linux: https:
//www.mjmwired.net/kernel/Documentation/memory-barriers.txt#305

## 4.10 SIMD/Vector Processing

In Section 4.1.4, we introduced a form of parallelism known as single instruc-
tion multiple data (SIMD). This refers to the ability of most modern micropro-
cessors to perform a mathematical operation on multiple data items in par-
allel, using a single machine instruction. In this section, we’ll explore SIMD
techniques in some detail, and conclude the chapter with a brief discussion of
how SIMD and multithreading are combined into a form of parallelsm known
single instruction multiple thread (SIMT), which forms the basis of all modern
GPUs.

Intel first introduced its MMX11 instruction set with their Pentium line
of CPUs in 1994. These instructions permitted SIMD calculations to be per-
formed on eight 8-bit integers, four 16-bit integers, or two 32-bit integers
packed into special 64-bit MMX registers. Intel followed this up with vari-
ous revisions of an extended instruction set called streaming SIMD extensions,
or SSE, the first version of which appeared in the Pentium III processor.

The SSE instruction set utilizes 128-bit registers that can contain integer or
IEEE floating-point data. The SSE mode most commonly used by game en-
gines is called packed 32-bit floating-point mode. In this mode, four 32-bit float
values are packed into a single 128-bit register. An operation such as addition
or multiplication can thus be performed on four pairs of floats in parallel
by taking two of these 128-bit registers as its inputs. Intel has since intro-
duced various upgrades to the SSE instruction set, named SSE2, SSE3, SSSE3
and SSE4. In 2007, AMD introduced its own variants named XOP, FMA4 and
CVT16.

In 2011, Intel introduced a new, wider SIMD register file and accompany-
ing instruction set named advanced vector extensions (AVX). AVX registers are
256 bits wide, permitting a single instruction to operate on pairs of up to eight
32-bit floating-point operands in parallel. The AVX2 instruction set is an ex-
tension to AVX. Some Intel CPUs now support AVX-512, an extension to AVX
permitting 16 32-bit floats to be packed into a 512-bit register.

11Officially, MMX is a meaningless initialism trademarked by Intel. Unofficially, developers
consider it to stand for “multimedia extensions” or “matrix math extensions.”


<!-- source-pdf-page: 351 -->
> Visual fallback for diagrams/images: [PDF page 351](../../../visual_pages/page_0351.jpg)

32 bits
32 bits
32 bits
32 bits

x
y
z
w

Figure 4.38. The four components of an SSE register in 32-bit ﬂoating-point mode.

### 4.10.1 The SSE Instruction Set and Its Registers

The SSE instruction set includes a wide variety of operations, with many vari-
ants for operating on differently-sized data elements within SSE registers. For
the purposes of the present discussion, however, we’ll stick to the relatively
small subset of instructions that deal with packed 32-bit floating-point data.
These instructions are denoted with a ps suffix, indicating that we’re deal-
ing with packed data (p), and that each element is a single-precision float
(s). However most of the upcoming discussions extend intuitively into AVX’s
256- and 512-bit modes; see https://software.intel.com/en-us/articles/intro-
duction-to-intel-advanced-vector-extensions for an overview of AVX.
The SSE registers are named XMMi, where i is an integer between 0 and 15
(e.g., XMM0, XMM1, and so on). In packed 32-bit floating-point mode, each
128-bit XMMi register contains four 32-bit floats. In AVX, the registers are 256
bits wide and are named YMMi; in AVX-512, they are 512 bits in width and
are named ZMMi.
In this chapter, we’ll often refer to the individual floats within an SSE reg-
ister as
[x
y
z
w]
, just as they would be when doing vector/matrix math
in homogeneous coordinates on paper (see Figure 4.38). It doesn’t usually
matter what you call the elements of an SSE register, as long as you’re con-
sistent about how you interpret each element. The most general approach is
to think of an SSE vector r as containing the elements
[
r0
r1
r2
r3
]
. Most
SSE documentation uses this convention, although some documentation uses
a
[
w
x
y
z
]
convention, so be careful out there!

4.10.1.1
The __m128 Data Type

In order for SSE instructions to perform arithmetic on packed floating-point
data, that data must reside in one of the XMMi registers. For long-term stor-
age, packed floating-point data can of course reside in memory, but it must be
transferred from RAM into an SSE register prior to being used for any calcu-
lations, and the results subsequently transferred back to RAM.
To make working with SSE and AVX data easier, C and C++ compilers pro-
vide special data types that represent packed arrays of floats. The __m128
type encapsulates a packed array of four floats for use with the SSE intrin-
sics. (The __m256 and __m512 types likewise represent packed arrays of eight


<!-- source-pdf-page: 352 -->

and 16 floats, respectively, for use with AVX intrinsics.)
The __m128 data type and its kin can be used to declare global variables,
automatic variables, function arguments and return types, and even class and
structure members. Declaring automatic variables and function arguments to
be of type __m128 often results in the compiler treating those values as di-
rect proxies for SSE registers. But using the __m128 type to declare global
variables, structure/class members, and sometimes local variables results in
the data being stored as a 16-byte aligned array of float in memory. Using
a memory-based __m128 variable in an SSE calculation will cause the com-
piler to implicitly emit instructions for loading the data from memory into an
SSE register prior to performing the calculation on it, and likewise to emit in-
structions for storing the results of the calculation back into the memory that
“backs” each such variable. As such, it’s a good idea to check the disassem-
bly to make sure that you’re not doing unnecessary loads and stores of SSE
registers when using the __m128 type (and its AVX relatives).

4.10.1.2
Alignment of SSE Data

Whenever data that is intended for use in an XMMi register is stored in mem-
ory, it must be 16-byte (128-bit) aligned. (Likewise, data intended for use with
AVX’s 256-bit YMMi registers must be 32-byte (256-bit) aligned, and data for
use with the 512-bit ZMMi registers must be 64-byte (512-bit) aligned.)
The compiler ensures that global and local variables of type __m128 are
aligned automatically. It also pads struct and class members so that any
__m128 members are aligned properly relative to the start of the object, and
ensures that the alignment of the entire struct or class is equal to the worst-
case alignment of its members. This means that declaring a global or local
variable instance of a struct or class that includes at least one __m128 member
will be 16-byte aligned by the compiler automatically.
However, all dynamically allocated instances of such a struct or class need
to be aligned manually. Likewise, any array of float that you intend to use
with SSE instructions must be properly aligned; you can ensure this via the
C++11 alignas specifier. See Section 6.2.1.3 for more information on aligned
memory allocations.

4.10.1.3
SSE Compiler Intrinsics

We could work directly with the SSE and AVX assembly language instruc-
tions, perhaps using our compiler’s inline assembly syntax. However, writing
code like this is not only non-portable, it’s also a big pain in the butt! To make
life easier, modern compilers provide intrinsics—special syntax that looks and


<!-- source-pdf-page: 353 -->

behaves like a regular C function, but is actually boiled down to inline as-
sembly code by the compiler. Many intrinsics translate into a single assembly
language instruction, although some are macros that translate into a sequence
of instructions.
In order to use SSE and AVX intrinsics, your .cpp file must #include
<xmmintrin.h> in Visual Studio, or <x86intrin.h> when compiling with
Clang or gcc.

4.10.1.4
Some Useful SSE Intrinsics

There are a lot of SSE intrinsics, but for the purposes of our discussion here we
can start with only five of them:

•
__m128 _mm_set_ps(float w, float z, float y, float x);
This intrinsic initializes an __m128 variable with the four floating-point
values provided.

•
__m128 _mm_load_ps(const float* pData);
This intrinsic loads four floats from a C-style array into an __m128
variable. The input array must be 16-byte aligned.

•
void _mm_store_ps(float* pData, __m128 v);
This intrinsic stores the contents of an __m128 variable into a C-style
array of four floats, which must be 16-byte aligned.

•
__m128 _mm_add_ps(__m128 a, __m128 b);
This intrinsic adds the four pairs of floats contained in the variables
a and b in parallel and returns the result.

•
__m128 _mm_mul_ps(__m128 a, __m128 b);
This intrinsic multiplies the four pairs of floats contained in the vari-
ables a and b in parallel and returns the result.

You may have noticed that the arguments x, y, z and w are passed to the
_mm_set_ps() function in reverse order. This strange convention probably
arises from the fact that Intel CPUs are little-endian. Just as a single floating-
point value with the bit pattern 0x12345678 would be stored in memory as
the bytes 0x78, 0x56, 0x34, 0x12 in order of increasing addresses, so too are
the contents of an SSE register stored in memory in an order that’s opposite
to the order those components actually appear within the register. In other
words, not only are the four bytes comprising each float in an SSE register
stored in little-endian order, but so too are the four floats themselves. All of this
is just a question of naming convention: There’s really no “most-significant”


<!-- source-pdf-page: 354 -->

or “least-significant” float within an SSE register. So we can either treat the
in-memory order as the “correct” order and consider _mm_set_ps() to be
“backwards,” or we can treat the arguments of _mm_set_ps() as being in
the “correct” order and think of all in-memory vectors as being “backwards.”
We’ll stick with the former convention, since it means that we’ll be able to
read off our vectors more naturally: A homogeneous vector v consisting of
members (vx, vy, vz, vw) would be stored in a C/C++ array as float v[] =
{ vx, vy, vz, vw }, but would be passed to _mm_set_ps() as w, z, y,
x.
Here’s a small code snippet that loads two four-element floating-point vec-
tors, adds them, and prints the results.

#include <xmmintrin.h>

void TestAddSSE()
{
alignas(16) float A[4];
alignas(16) float B[4] = { 2.0f, 4.0f, 6.0f, 8.0f };

// set a = (1, 2, 3, 4) from literal values, and
// load b = (2, 4, 6, 8) from a floating-point array
// just to illustrate the two ways of doing this
// (remember that _mm_set_ps() is backwards!)
__m128 a = _mm_set_ps(4.0f, 3.0f, 2.0f, 1.0f);
__m128 b = _mm_load_ps(&B[0]);

// add the vectors
__m128 r = _mm_add_ps(a, b);

// store '__m128 a' into a float array for printing
_mm_store_ps(&A[0], a);

// store result into a float array for printing
alignas(16) float R[4];
_mm_store_ps(&R[0], r);

// inspect results
printf("a = %.1f %.1f %.1f %.1f\n",
A[0], A[1], A[2], A[3]);
printf("b = %.1f %.1f %.1f %.1f\n",
B[0], B[1], B[2], B[3]);
printf("r = %.1f %.1f %.1f %.1f\n",
R[0], R[1], R[2], R[3]);
}


<!-- source-pdf-page: 355 -->

4.10.1.5
AltiVec vector Types

As a quick aside, the GNU C/C++ compiler gcc (used to compile code for the
PS3, for example) provides support for the PowerPC’s AltiVec instruction set,
which provides support for SIMD operations, much as SSE does on Intel pro-
cessors. 128-bit vector types can be declared like regular C/C++ types, but
they are preceded by the keyword vector. For example, a SIMD variable con-
taining four floats would be declared vector float. gcc also provides a
means of writing literal SIMD values into your source code. For example, you
can initialize a vector float with a value like this:

vector float v = (vector float)(-1.0f, 2.0f, 0.5f, 1.0f);

The corresponding Visual Studio code is a tad more clunky:

// use compiler intrinsic to load "literal" value
// (remember _mm_set_ps() is backwards!)
__m128 v = _mm_set_ps(1.0f, 0.5f, 2.0f, -1.0f);

We won’t cover AltiVec explicitly in this chapter, but once you understand SSE
it’ll be very easy to learn.

### 4.10.2 Using SSE to Vectorize a Loop

SIMD offers the potential to speed up certain kinds of calculations by a factor of
four, because a single machine language instruction can perform an arithmetic
operation on four floating-point values in parallel. Let’s see how this can be
done using SSE intrinsics.
First, consider a simple loop that adds two arrays of floats pairwise, stor-
ing each result into an output array:

void AddArrays_ref(int count,
float* results,
const float* dataA,
const float* dataB,
{
for (int i = 0; i < count; ++i)
{
results[i] = dataA[i] + dataB[i];
}
}

We can speed up this loop significantly using SSE intrinsics, like this:


<!-- source-pdf-page: 356 -->

void AddArrays_sse(int count,
float* results,
const float* dataA,
const float* dataB)
{
// NOTE: the caller needs to ensure that the size of
// all 3 arrays are equal, and a multiple of four!
assert(count % 4 == 0);

for (int i = 0; i < count; i += 4)
{
__m128 a = _mm_load_ps(&dataA[i]);
__m128 b = _mm_load_ps(&dataB[i]);
__m128 r = _mm_add_ps(a, b);
_mm_store_ps(&results[i], r);
}
}

In this version, we loop over the values four at a time. We load blocks of four
floats into SSE registers, add them in parallel, and store the results into a corre-
sponding block of four floats within the result array. This is called vectorizing
our loop. (In this example, we’re assuming that the sizes of the three arrays
are equal, and that the size is a multiple of four. The caller is responsible for
padding the arrays with one, two or three dummy values each, as necessary, in
order to meet this requirement.)
Vectorization can lead to a significant speed improvement. This particular
example isn’t quite four times faster, due to the overhead of having to load
the values in groups of four and store the results on each iteration; but when
I measured these functions running on very large arrays of floats, the non-
vectorized loop took roughly 3.8 times as long to do its work as the vectorized
loop.

### 4.10.3 Vectorized Dot Products

Let’s apply vectorization to a somewhat more interesting task: Calculating
dot products. Given two arrays of four-element vectors, the goal is to calculate
their dot products pairwise, and store the results into an output array of floats.
Here’s a reference implementation without using SSE. In this implemen-
tation, each contiguous block of four floats within the a[] and b[] input
arrays is interpreted as one homogeneous four-element vector.

void DotArrays_ref(int count,
float r[],
const float a[],


<!-- source-pdf-page: 357 -->

const float b[])
{
for (int i = 0; i < count; ++i)
{
// treat each block of four floats as a
// single four-element vector
const int j = i * 4;

r[i] = a[j+0]*b[j+0]
// ax*bx
+ a[j+1]*b[j+1]
// ay*by
+ a[j+2]*b[j+2]
// az*bz
+ a[j+3]*b[j+3]; // aw*bw
}
}

4.10.3.1
A First Attempt (That’s Slow)

Here’s a first attempt at using SSE intrinsics for this task:

void DotArrays_sse_horizontal(int count,
float r[],
const float a[],
const float b[])
{
for (int i = 0; i < count; ++i)
{
// treat each block of four floats as a
// single four-element vector
const int j = i * 4;

__m128 va = _mm_load_ps(&a[j]); // ax,ay,az,aw
__m128 vb = _mm_load_ps(&b[j]); // bx,by,bz,bw

__m128 v0 = _mm_mul_ps(va, vb);

// add across the register...
__m128 v1 = _mm_hadd_ps(v0, v0);
// (v0w+v0z, v0y+v0x, v0w+v0z, v0y+v0x)
__m128 vr = _mm_hadd_ps(v1, v1);
// (v0w+v0z+v0y+v0x, v0w+v0z+v0y+v0x,
//
v0w+v0z+v0y+v0x, v0w+v0z+v0y+v0x)

_mm_store_ss(&r[i], vr); // extract vr.x as a float
}
}

This implementation required a new instruction: _mm_hadd_ps() (horizontal


<!-- source-pdf-page: 358 -->

add). This intrinsic operates on a single input register (x, y, z, w) and calculates
two sums: s = x + y and t = z + w. It stores these two sums into the four slots
of the destination register as (t, s, t, s). Performing this operation twice allows
us to calculate the sum d = x + y + z + w. This is called adding across a register.
Adding across a register is not usually a good idea because it’s a very slow
operation. Profiling the DotArrays_sse() implementation shows that it ac-
tually takes a little bit more time than the reference implementation. Using SSE
here has actually slowed us down!12

4.10.3.2
A Better Approach

The key to realizing the power of SIMD parallelism for dot products is to figure
out a way to avoid having to add across a register. This can be done, but we’ll
have to transpose our input vectors first. By storing them in transposed order,
we can calculate our dot product in just the same way that we calculated it
when using floats: We multiply the x components, then add that result to
the product of the y components, then add that result to the product of the z
components, and finally add that result to the product of the w components.
Here’s what the code looks like:

void DotArrays_sse(int count,
float r[],
const float a[],
const float b[])
{
for (int i = 0; i < count; i += 4)
{
__m128 vaX = _mm_load_ps(&a[(i+0)*4]); // a[0,4,8,12]
__m128 vaY = _mm_load_ps(&a[(i+1)*4]); // a[1,5,9,13]
__m128 vaZ = _mm_load_ps(&a[(i+2)*4]); // a[2,6,10,14]
__m128 vaW = _mm_load_ps(&a[(i+3)*4]); // a[3,7,11,15]

__m128 vbX = _mm_load_ps(&b[(i+0)*4]); // b[0,4,8,12]
__m128 vbY = _mm_load_ps(&b[(i+1)*4]); // b[1,5,9,13]
__m128 vbZ = _mm_load_ps(&b[(i+2)*4]); // b[2,6,10,14]
__m128 vbW = _mm_load_ps(&b[(i+3)*4]); // b[3,7,11,15]

__m128 result;
result = _mm_mul_ps(vaX, vbX);
result = _mm_add_ps(result, _mm_mul_ps(vaY, vbY));
result = _mm_add_ps(result, _mm_mul_ps(vaZ, vbZ));

12With SSE4, Intel introduced the intrinsic _mm_dp_ps() (and the corresponding dpps instruc-
tion) which calculates a dot product with somewhat lower latency than the version involving
two invocations of _mm_hadd_ps(). But all horizontal adds are very expensive, and should be
avoided wherever possible.


<!-- source-pdf-page: 359 -->

result = _mm_add_ps(result, _mm_mul_ps(vaW, vbW));

_mm_store_ps(&r[i], result);
}
}

The MADD Instruction

It’s interesting to note that a multiply followed by an add is such a common
operation that it has its own name—madd. Some CPUs provide a single SIMD
instruction for performing a madd operation. For example, the PowerPC Al-
tiVec intrinsic vec_madd() performs this operation. So in AltiVec, the guts of
our DotArrays() function could be made just a little bit simpler:

vector float result = vec_mul(vaX, vbX);
result = vec_madd(vaY, vbY, result));
result = vec_madd(vaZ, vbZ, result));
result = vec_madd(vaW, vbW, result));

4.10.3.3
Transpose as We Go

The above implementation assumes that the input data has already been trans-
posed by the caller. In other words, the a[] array is assumed to contain the
components {a0, a4, a8, a12, a1, a5, a9, a13, ...} and likewise for the b[] array. If
we want to operate on input data that’s in the same format as it was for the ref-
erence implementation, we’ll have to do the transposition within our function.
Here’s how:

void DotArrays_sse_transpose(int count,
float r[],
const float a[],
const float b[])
{
for (int i = 0; i < count; i += 4)
{
__m128 vaX = _mm_load_ps(&a[(i+0)*4]); // a[0,1,2,3]
__m128 vaY = _mm_load_ps(&a[(i+1)*4]); // a[4,5,6,7]
__m128 vaZ = _mm_load_ps(&a[(i+2)*4]); // a[8,9,10,11]
__m128 vaW = _mm_load_ps(&a[(i+3)*4]); // a[12,13,14,15]

__m128 vbX = _mm_load_ps(&b[(i+0)*4]); // b[0,1,2,3]
__m128 vbY = _mm_load_ps(&b[(i+1)*4]); // b[4,5,6,7]
__m128 vbZ = _mm_load_ps(&b[(i+2)*4]); // b[8,9,10,11]
__m128 vbW = _mm_load_ps(&b[(i+3)*4]); // b[12,13,14,15]


<!-- source-pdf-page: 360 -->

_MM_TRANSPOSE4_PS(vaX, vaY, vaZ, vaW);
// vaX = a[0,4,8,12]
// vaY = a[1,5,9,13]
// ...
_MM_TRANSPOSE4_PS(vbX, vbY, vbZ, vbW);
// vbX = b[0,4,8,12]
// vbY = b[1,5,9,13]
// ...

__m128 result;
result = _mm_mul_ps(vaX, vbX);
result = _mm_add_ps(result, _mm_mul_ps(vaY, vbY));
result = _mm_add_ps(result, _mm_mul_ps(vaZ, vbZ));
result = _mm_add_ps(result, _mm_mul_ps(vaW, vbW));

_mm_store_ps(&r[i], result);
}
}

Those two calls to _MM_TRANSPOSE() are actually invocations of a somewhat
complex macro that uses shuffle instructions to move the components of the
four input registers around. Thankfully shuffling isn’t a particularly expen-
sive operation, so transposing our vectors as we calculate the dot products
doesn’t introduce too much overhead. Profiling all three implementations of
DotArrays() shows that our final version (the one that transposes the vec-
tors as it goes) is roughly 3.5 times faster than the reference implementation.

4.10.3.4
Shufﬂe and Transpose

For the curious reader, here’s what the _MM_TRANSPOSE() macro looks like:

#define _MM_TRANSPOSE4_PS(row0, row1, row2, row3)
\
{ __m128 tmp3, tmp2, tmp1, tmp0;
\
\
tmp0
= _mm_shuffle_ps((row0), (row1), 0x44);
\
tmp2
= _mm_shuffle_ps((row0), (row1), 0xEE);
\
tmp1
= _mm_shuffle_ps((row2), (row3), 0x44);
\
tmp3
= _mm_shuffle_ps((row2), (row3), 0xEE);
\
\
(row0) = _mm_shuffle_ps(tmp0, tmp1, 0x88);
\
(row1) = _mm_shuffle_ps(tmp0, tmp1, 0xDD);
\
(row2) = _mm_shuffle_ps(tmp2, tmp3, 0x88);
\
(row3) = _mm_shuffle_ps(tmp2, tmp3, 0xDD); }

Those crazy hexadecimal numbers are bit-packed four-element fields called


<!-- source-pdf-page: 361 -->

shuffle masks. They tell the _mm_shuffle() intrinsic how exactly to shuffle
the components. These bit-packed fields are a common source of confusion,
possibly because of the naming conventions used in most documentation. But
it’s actually pretty simple: A shuffle mask is constructed out of four integers,
each of which represents one of the components of an SSE register (and hence
can have a value between 0 and 3).

#define SHUFMASK(p,q,r,s) \
(p | (q<<2) | (r<<4) | (s<<6))

Passing two SSE registers a and b along with a shuffle mask to the
_mm_shuffle_ps() intrinsic results in the specified components of a and
b appearing in the output register r as follows:

__m128 a = ...;
__m128 b = ...;
__m128 r = _mm_shuffle_ps(a, b,
SHUFMASK(p,q,r,s));
// r == ( a[p], a[q], b[r], b[s] )

### 4.10.4 Vector-Matrix Multiplication with SSE

Now that we understand how to perform a dot product, we can multiply a
four-element vector with a 4 × 4 matrix. To do it, we simply need to perform
four dot products between the input vector and each of the four rows of the
input matrix.
We’ll start by defining a Mat44 class that encapsulates four SSE vectors,
representing its four rows. We’ll use a union so that we can easily access the
individual members of the matrix as floats. (This works because instances
of our Mat44 class will always reside in memory, never directly in SSE regis-
ters.)

union Mat44
{
float
c[4][4]; // components
__m128 row[4];
// rows
};

The function to multiply a vector and a matrix looks like this:

__m128 MulVecMat_sse(const __m128& v, const Mat44& M)
{
// first transpose v
__m128 vX = _mm_shuffle_ps(v, v, 0x00); // (vx,vx,vx,vx)


<!-- source-pdf-page: 362 -->

__m128 vY = _mm_shuffle_ps(v, v, 0x55); // (vy,vy,vy,vy)
__m128 vZ = _mm_shuffle_ps(v, v, 0xAA); // (vz,vz,vz,vz)
__m128 vW = _mm_shuffle_ps(v, v, 0xFF); // (vw,vw,vw,vw)

__m128 r =
_mm_mul_ps(vX, M.row[0]);
r = _mm_add_ps(r, _mm_mul_ps(vY, M.row[1]));
r = _mm_add_ps(r, _mm_mul_ps(vZ, M.row[2]));
r = _mm_add_ps(r, _mm_mul_ps(vW, M.row[3]));
return r;
}

The shuffles are used to replicate one component of v (either vx, vy, vz or vw)
across all four lanes of an SSE register. This has the effect of transposing v
prior to performing the dot product with the rows of M, which are already
transposed. (Remember that vector-matrix multiplication normally involves
taking dot products between an input vector and the columns of the matrix.
Here, we’re transposing v into four SSE registers, and then doing our math,
component-wise, with the rows of the matrix.)

### 4.10.5 Matrix-Matrix Multiplication with SSE

Multiplying two 4 × 4 matrices with SSE intrinsics is trivial once we have a
function to multiply a vector and a matrix. Here’s what the code looks like:

void MulMatMat_sse(Mat44& R, const Mat44& A, const Mat44& B)
{
R.row[0] = MulVecMat_sse(A.row[0], B);
R.row[1] = MulVecMat_sse(A.row[1], B);
R.row[2] = MulVecMat_sse(A.row[2], B);
R.row[3] = MulVecMat_sse(A.row[3], B);
}

### 4.10.6 Generalized Vectorizaton

Because an SSE register contains four floating-point values, it’s tempting to
think of it as a natural “fit” for the components of a four-element homoge-
neous vector v, and to think that the best use of SSE is for doing 3D vector
math. However, this is a very limiting way of thinking about SIMD paral-
lelism.
Most “batched” operations, in which a single computation is performed re-
peatedly on a large dataset, can be vectorized using SIMD parallelism. If you
think about it, the components of a SIMD register really function like parallel
“lanes” in which arbitrary processing can be performed. Working with float


<!-- source-pdf-page: 363 -->

variables gives us a single lane, but working with 128-bit (four-element) SIMD
variables allows us to do that same calculation in parallel across four lanes—
in other words, we can perform our calculations four at a time. Working with
256-bit AVX registers gives us eight lanes, allowing us to perform our calcula-
tions eight at a time. And AVX-512 gives us 16 lanes, letting us do 16 calcula-
tions at a time.
The easiest way to write vectorized code is to start out by writing it as a
single-lane algorithm (just using floats). Once it works, we can convert it
to operate N elements at a time, using SIMD registers that have an N-lane
capacity. We’ve already seen this process in action: In Section 4.10.3, we first
wrote a loop that performed a large batch of dot products one at a time, and
then we converted to use SSE so that we could perform those dot products
four at a time.
One nice side-effect of vectorizing your code in this way is that you can reap
the benefits of wider SIMD hardware with little adjustment to your code. On a
machine with only SSE support, you perform four operations per iteration of
your loop; on a machine that supports AVX, you simply change it to do eight
operations per iteration; and on an AVX-512 system, you can do 16 operations
per iteration.
Interestingly, most optimizing compilers can vectorize some kinds of single-
lane loops automatically. In fact, when writing the above examples, it took
some doing to force the compiler not to vectorize my single-lane code, so that I
could compare its performance to my SIMD implementation! Once again, it’s
always a good idea to look at the disassembly when writing optimized code—
you may discover the compiler is doing more (or less) than you thought!

### 4.10.7 Vector Predication

Let’s take a look at another (totally contrived) example. This example will re-
inforce the ideas of generalized vectorization, but it will also serve to illustrate
another useful technique: vector predication.
Imagine that we needed to take the square roots of a large array of floats.
We’d start out by writing it as a single-lane loop, like this:

#include <cmath>

void SqrtArray_ref(float* __restrict__ r,
const float* __restrict__ a,
int count)
{
for (unsigned i = 0; i < count; ++i)
{


<!-- source-pdf-page: 364 -->

if (a[i] >= 0.0f)
r[i] = std::sqrt(a[i]);
else
r[i] = 0.0f;
}
}

Next, let’s convert this loop into SSE, performing four square roots at a time:

#include <xmmintrin.h>

void SqrtArray_sse_broken(float* __restrict__ r,
const float* __restrict__ a,
int count)
{
assert(count % 4 == 0);
__m128 vz = _mm_set1_ps(0.0f); // all zeros

for (int i = 0; i < count; i += 4)
{
__m128 va = _mm_load_ps(a + i);

__m128 vr;
if (_mm_cmpge_ps(va, vz)) // ???
vr = _mm_sqrt_ps(va);
else
vr = vz;

_mm_store_ps(r + i, vr);
}
}

This seems simple enough: We simply stride through the input array four
floats at a time, load groups of four floats into an SSE register, and then
perform four parallel square roots with _mm_sqrt_ps().
However, there’s one small gotcha in this loop. We need to check whether
the input values are greater than or equal to zero, because the square root of a
negative number is imaginary (and will therefore produce QNaN). The intrin-
sic _mm_cmpge_ps() compares the values of two SSE registers, component-
wise, to see if they are greater than or equal to a vector of values supplied by
the caller. However, this function doesn’t return a bool. How can it? We’re
comparing four values to four other values, so some of them may pass the test
while others fail it. That means we can’t just do an if check against the results
of _mm_cmpge_ps().13

13The if check in the single-lane reference implementation also prevents the compiler from au-
tomatically vectorizing the loop.


<!-- source-pdf-page: 365 -->

Does this spell doom for our vectorized implementation?
Thankfully
not. All SSE comparison intrinsics like _mm_cmpge_ps() produce a four-
component result, stored in an SSE register. But instead of containing four
floating-point values, the result consists of four 32-bit masks. Each mask con-
tains all binary 1s (0xFFFFFFFFU) if the corresponding component in the input
register passed the test, and all binary 0s (0x0U) if that component failed the
test.
We can use the results of an SSE comparison intrinsic by applying it as a
bit mask in order to select between one of two possible results. In our example,
when the input value passes the test (is greater than or equal to zero), we want
to select the square root of that input value; when it fails the test (is negative),
we want to select a value of zero. This is called predication, and because we’re
applying it to SIMD vectors, it’s called vector predication.
In Section 4.2.6.2, we saw how to do predication with floating-point val-
ues, using bitwise AND, OR and NOT operators. Here’s an excerpt from that
example:

// ...

// select quotient when mask is all ones, or default
// value d when mask is all zeros (NOTE: this won't
// work as written -- you'd need to use a union to
// interpret the floats as unsigned for masking)
const float result = (q & mask) | (d & ~mask);
return result;
}

It’s no different here, we just need to use SSE versions of these bitwise opera-
tors:

#include <xmmintrin.h>

void SqrtArray_sse(float* __restrict__ r,
const float* __restrict__ a,
int count)
{
assert(count % 4 == 0);
__m128 vz = _mm_set1_ps(0.0f);

for (int i = 0; i < count; i += 4)
{
__m128 va = _mm_load_ps(a + i);


<!-- source-pdf-page: 366 -->

// always do the quotient, but it may end
// up producing QNaN in some or all lanes
__m128 vq = _mm_sqrt_ps(va);

// now select between vq and vz, depending
// on whether the input was greater than
// or equal to zero or not
__m128 mask = _mm_cmpge_ps(va, zero);

// (vq & mask) | (vz & ~mask)
__m128 qmask = _mm_and_ps(mask, vq);
__m128 znotmask = _mm_andnot_ps(mask, vz);
__m128 vr = _mm_or_ps(qmask, znotmask);

_mm_store_ps(r + i, vr);
}
}

It’s convenient and commonplace to encapsulate this vector predication op-
eration in a function, which is typically called vector select. PowerPC’s AltiVec
ISA provides an intrinsic called vec_sel() for this purpose. It works like
this:

// pseudocode illustrating how AltiVec's vec_sel() intrinsic
// works
vector float vec_sel(vector float falseVec,
vector float trueVec,
vector bool mask)
{
vector float r;
for (each lane i)
{
if (mask[i] == 0)
r[i] = falseVec[i];
else
r[i] = trueVec[i];
}
return r;
}

SSE2 provided no vector select instruction, but thankfully one was introduced
in SSE4—it is emitted by the intrinsic _mm_blendv_ps().

Let’s take a look at how we might implement a vector select operation our-
selves. We can write it like this:


<!-- source-pdf-page: 367 -->

__m128 _mm_select_ps(const __m128 a,
const __m128 b,
const __m128 mask)
{
// (b & mask) | (a & ~mask)
__m128 bmask = _mm_and_ps(mask, b);
__m128 anotmask = _mm_andnot_ps(mask, a);
return _mm_or_ps(bmask, anotmask);
}

Or if we’re feeling brave, we can accomplish the same thing with exclusive
OR:

__m128 _mm_select_ps(const __m128 a,
const __m128 b,
const __m128 mask)
{
// (((a ^ b) & mask) ^ a)
__m128 diff = _mm_xor_ps(a, b);
return _mm_xor_ps(a, _mm_and_ps(mask, diff));
}

See if you can figure out how this works. Here are two hints: The exclusive
OR operator calculates the bitwise difference between two values. Two XORs in
a row leave the input value unchanged ((a ^ b) ^ b == a).

## 4.11 Introduction to GPGPU Programming

We said in the preceding section that most optimizing compilers can vectorize
some code automatically, if the target CPU includes a SIMD vector processing
unit, and if the source code meets certain requirements (such as not involving
complex branching). Vectorization is also one of the pillars of general-purpose
GPU programming (GPGPU). In this section, we’ll take a brief introductory
look at how a GPU differs from a CPU in terms of its hardware architecture,
and how the concepts of SIMD parallelism and vectorization enable program-
mers to write compute shaders that are capable of processing large amounts of
data in parallel on a GPU.

### 4.11.1 Data-Parallel Computations

A GPU is a specialized coprocessor designed specifically to accelerate those
computations that involve a high degree of data parallelism. It does so by com-
bining SIMD parallelism (vectorized ALUs) with MIMD parallelism (by em-
ploying a form of preemptive multithreading). NVIDIA coined the term single
instruction multiple thread (SIMT) to describe this SIMD/MIMD hybrid design,


<!-- source-pdf-page: 368 -->

although the design is not unique to NVIDIA GPUs—although the specifics
of GPU designs vary from vendor to vendor and from product line to prod-
uct line in significant ways, all GPUs employ the general principles of SIMT
parallelism in their designs.
GPUs are designed specifically to perform data-parallel computations on
very large datasets. In order for a computational task to be well-suited to ex-
ecution on a GPU, the computations performed on any one element of the
dataset must be independent of the results of computations on other elements.
In other words, it must be possible to process the elements in any order.
The simple examples of SIMD vectorization that we presented starting in
Section 4.10.3 are all examples of data-parallel computations. Recall this func-
tion, which processes two potentially very large arrays of input vectors and
produces an output array containing the scalar dot products of those vectors:

void DotArrays_ref(unsigned count,
float r[],
const float a[],
const float b[])
{
for (unsigned i = 0; i < count; ++i)
{
// treat each block of four floats as a
// single four-element vector
const unsigned j = i * 4;

r[i] = a[j+0]*b[j+0]
// ax*bx
+ a[j+1]*b[j+1]
// ay*by
+ a[j+2]*b[j+2]
// az*bz
+ a[j+3]*b[j+3]; // aw*bw
}
}

The computation performed by each iteration of this loop is independent of the
computations performed by the other iterations. That means that we are free
to perform the computations in any order we see fit. Moreover, this property
is what allows us to apply SSE or AVX intrinsics to vectorize the loop—instead
of performing the computations one-at-a-time, we can use SIMD parallelism
to perform four, eight or 16 computations simultaneously, thereby reducing
the iteration count by a factor of four, eight or 16, respectively.
Now imagine taking SIMD parallelization to an extreme. What if we had a
SIMD VPU with 1024 lanes? In that case, we could divide the total number of
iterations by 1024—and when the input arrays contain 1024 elements or fewer,
we could literally execute the entire loop in a single iteration!


<!-- source-pdf-page: 369 -->

This is, roughly speaking, what a GPU does.
However, it doesn’t use
SIMDs that are actually 1024 lanes wide each. A GPU’s SIMD units are typ-
ically eight or 16 lanes wide, but they process workloads in batches of 32 or 64
elements at a time. What’s more, a GPU contains many such SIMD units. So a
large workload can be dispatched across these SIMD units in parallel, resulting
in the GPU being capable of processing literally thousands of data elements in
parallel.
Data-parallel computations are just what the doctor ordered when apply-
ing a pixel shader (also known as a fragment shader) to millions of pixels, or a
vertex shader to hundreds of thousands or even millions of 3D mesh vertices,
every frame at 30 or 60 FPS. But modern GPUs expose their computational
power to programmers, allowing us to write general-purpose compute shaders.
As long as the computations we wish to perform on a large dataset have the
property of being largely independent of one another, they can probably be
executed by a GPU more efficiently than they could be on a CPU.

### 4.11.2 Compute Kernels

In Section 4.10.3, we saw that in order to vectorize a loop like the one in the
DotArrays_ref() function, we must rewrite the code. The vectorized ver-
sion of the function makes use of SSE or AVX intrinsics; our scalar data types
are replaced by vector types such as SSE’s __m128 or AltiVec’s vector float;
and the loop is hard-coded to iterate four, eight or 16 elements at a time.
When writing a GPGPU compute shader, we take a different tack. Instead
of hard-coding the loop to operate in fixed-size batches, we leave the loop as a
“single-lane” computation using scalar data types. Then, we extract the body
of the loop into a separate function known as a kernel. Here’s what the example
above would look like as a kernel:

void DotKernel(unsigned i,
float r[],
const float a[],
const float b[])
{
// treat each block of four floats as a
// single four-element vector
const unsigned j = i * 4;

r[i] = a[j+0]*b[j+0]
// ax*bx
+ a[j+1]*b[j+1]
// ay*by
+ a[j+2]*b[j+2]
// az*bz
+ a[j+3]*b[j+3]; // aw*bw
}


<!-- source-pdf-page: 370 -->

void DotArrays_gpgpu1(unsigned count,
float r[],
const float a[],
const float b[])
{
for (unsigned i = 0; i < count; ++i)
{
DotKernel(i, r, a, b);
}
}

The DotKernel() function is now in a form that’s suitable for conversion
into a compute shader. It processes just one element of the input data, and pro-
duces a single output element. This is analogous to how a pixel/fragment
shader receives a single input pixel/fragment color and transforms it into a
single output color, or to how a vertex shader receives a single input vertex
and produces a single output vertex. The GPU effectively runs the for loop
for us, calling our kernel function once for each element of our dataset.
GPGPU compute kernels are typically written in a special shading language
which can be compiled into machine code that’s understandable by the GPU.
Shading languages are usually very close to C in syntax, so converting a C
or C++ loop into a GPU compute kernel isn’t usually a particularly difficult
undertaking. Some examples of shading languages include DirectX’s HLSL
(high-level shader language), OpenGL’s GLSL, NVIDIA’s Cg and CUDA C
languages, and OpenCL.
Some shading languages require you to move your kernel code into special
source files, separate from your C++ application code. OpenCL and CUDA
C, however, are extensions to the C++ language itself. As such, they permit
programmers to write compute kernels as regular C/C++ functions, with only
minor syntactic adjustments, and to invoke those kernels on the GPU with
relatively simple syntax.
As a concrete example, here’s our DotKernel() function written in
CUDA C:

__global__ void DotKernel_CUDA(int count,
float* r,
const float* a,
const float* b)
{
// CUDA provides a magic "thread index" to each
// invocation of the kernel -- this serves as
// our loop index i


<!-- source-pdf-page: 371 -->

size_t i = threadIdx.x;

// make sure the index is valid
if (i < count)
{
// treat each block of four floats as a
// single four-element vector
const unsigned j = i * 4;

r[i] = a[j+0]*b[j+0]
// ax*bx
+ a[j+1]*b[j+1]
// ay*by
+ a[j+2]*b[j+2]
// az*bz
+ a[j+3]*b[j+3]; // aw*bw
}
}

You’ll notice that the loop index i is taken from a variable called threadIdx
within the kernel function itself. The thread index is a “magic” input provided
by the compiler, in much the same way that the this pointer “magically”
points to the current instance within a C++ class member function. We’ll talk
more about the thread index in the next section.

### 4.11.3 Executing a Kernel

Given that we’ve written a compute kernel, let’s see how to execute it on the
GPU. The details differ from shading language to shading language, but the
key concepts are roughly equivalent. For example, here’s how we’d kick off
our compute kernel in CUDA C:

void DotArrays_gpgpu2(unsigned count,
float r[],
const float a[],
const float b[])
{
// allocate "managed" buffers that are visible
// to both CPU and GPU
int *cr, *ca, *cb;
cudaMallocManaged(&cr, count * sizeof(float));
cudaMallocManaged(&ca, count * sizeof(float) * 4);
cudaMallocManaged(&cb, count * sizeof(float) * 4);

// transfer the data into GPU-visible memory
memcpy(ca, a, count * sizeof(float) * 4);
memcpy(cb, b, count * sizeof(float) * 4);


<!-- source-pdf-page: 372 -->

// run the kernel on the GPU
DotKernel_CUDA <<<1, count>>> (cr, ca, cb, count);

// wait for GPU to finish
cudaDeviceSynchronize();

// return results and clean up
memcpy(r, cr, count * sizeof(float));
cudaFree(cr);
cudaFree(ca);
cudaFree(cb);
}

A bit of set-up code is required to allocate the input and output buffers as
“managed” memory that is visible to both CPU and GPU, and to copy the
input data into them.
The CUDA-specific triple angled brackets notation
(<<<G,N>>>) then executes the compute kernel on the GPU, by submitting
a request to the driver. The call to cudaDeviceSynchronize() forces the
CPU to wait until the GPU has done its work (in much the same way that
pthread_join() forces one thread to wait for the completion of another).
Finally, we free the GPU-visible data buffers.
Let’s have a closer look at the <<<G,N>>> angled bracket notation. The
second argument N within the angled brackets allows us to specify the dimen-
sions of our input data. This corresponds to the number of iterations of our
loop that we want the GPU to perform. It can actually be a one-, two- or three-
dimensional quantity, allowing us to process one-, two- or three-dimensional
input arrays. However, just like in C/C++, a multidimensional array is re-
ally just a one-dimensional array that’s indexed in a special way. For exam-
ple, in C/C++ a two-dimensional array access written like [row][column]
is really equivalent to a one-dimensional array access [row*numColumns +
column]. The same principle applies to multidimensional GPU buffers.
The first argument G in the angled brackets tells the driver how many thread
groups (known as thread blocks in NVIDIA terminology) to use when running
this compute kernel. A compute job with a single thread group is constrained
to run on a single compute unit on the GPU. A compute unit is esssentially a
core within the GPU—a hardware component that is capable of executing an
instruction stream. Passing a larger number for G allows the driver to divide
the workload up across more than one compute unit.

### 4.11.4 GPU Threads and Thread Groups

The G argument tells the GPU driver into how many thread groups to divide
our work. As you might expect, a thread group is comprised of some number


<!-- source-pdf-page: 373 -->
> Visual fallback for diagrams/images: [PDF page 373](../../../visual_pages/page_0373.jpg)

GPU

Compute Unit

CU 0
CU 1

SIMD

F/D

Scalar
ALU

SIMD

Scheduler

CU 2
CU 3

SIMD

L1

SIMD

LDS

CU 4
CU 5

Large Register File

CU 6
CU 7

...

...

Figure 4.39. A typical GPU is comprised of multiple compute units (CU), each of which acts like a
stripped down CPU core. A CU contains an instruction fetch/decode unit, an L1 cache, possibly a
block of local data storage RAM (LDS), a scalar ALU, and a number of SIMD units for executing
vectorized code.

of GPU threads. But what exactly does the term “thread” mean in the context
of a GPU?

Every GPU kernel is compiled into an instruction stream consisting of a se-
quence of GPU machine language instructions, in much the same way that a
C/C++ function is compiled down into a stream of CPU instructions. So a
GPU “thread” is equivalent to a CPU “thread,” in the sense that both kinds of
threads represent a stream of machine language instructions that can be exe-
cuted by one or more cores. However, a GPU executes its threads in a manner
that is somewhat different from the way in which a CPU executes its threads.
As a result, the term “thread” has a subtly different meaning when applied
to GPU compute kernels than it has when applied to running programs on a
CPU.

In order to understand this difference in terminology, let’s take a brief
glimpse at the architecture of a GPU. We said in Section 4.11.1 that a GPU is
comprised of multiple compute units, each of which contains some number of
SIMD units. We can think of a compute unit like a stripped down CPU core:
It contains an instruction fetch/decode unit, possibly some memory caches, a
regular “scalar” ALU, and usually somewhere in the neighborhood of four
SIMD units, which serve much the same function as the vector processing
unit (VPU) in a SIMD-enabled CPU. This architecture is illustrated in Figure
4.39.

The SIMD units in a CU have different lane widths on different GPUs,
but for the sake of this discussion let’s assume we’re working on an AMD


<!-- source-pdf-page: 374 -->

Radeon™Graphics Core Next (GCN) architecture, in which the SIMDs are 16
lanes wide. The CU is not capable of speculative or out-of-order execution—it
simply reads a stream of instructions and executes them one by one, using the
SIMD units to apply each instruction14 to 16 elements of input data in parallel.
To execute a compute kernel on a CU, the driver first subdivides the in-
put data buffers into blocks consisting of 64 elements each. For each of these
64-element blocks of data, the compute kernel is invoked on one CU. Such an
invocation is called a wavefront (also known as a warp in NVIDIA speak). When
executing a wavefront, the CU fetches and decodes the instructions of the ker-
nel one by one. Each instruction is applied to 16 data elements in lock step
using a SIMD unit. Internally, the SIMD unit consists of a four-stage pipeline,
so it takes four clock cycles to complete. So rather than allow the stages of this
pipeline to sit idle for three clock cycles out of every four, the CU applies the
same instruction three more times, to three more blocks of 16 data elements.
This is why a wavefront consists of 64 data elements, even though the SIMDs
in the CU are only 16 lanes wide.
Because of this somewhat peculiar way in which a CU executes an instruc-
tion stream, the term “GPU thread” actually refers to a single SIMD lane. You
can think of a GPU thread, therefore, as a single iteration of the original non-
vectorized loop that we started with, before we converted its body into a com-
pute kernel. Alternatively, you can think of a GPU thread as a single invoca-
tion of the kernel function, operating on a single input datum and producing
a single output datum. The fact that the GPU actually runs multiple GPU
threads in parallel (i.e., the fact that it really runs the kernel once per wave-
front, but processes 64 data elements at a time) is just an implementation de-
tail. By insulating the programmer from having to think about the details of
how the computation is vectorized on any particular GPU, compute kernels
(and graphics shaders as well) can be written in a portable manner.

4.11.4.1
From SIMD to SIMT

The term single instruction multiple thread (SIMT) was introduced to underscore
the fact that a GPU doesn’t just use SIMD parallelism—it also uses a form of
preemptive multithreading to time-slice between wavefronts. Let’s take a brief
look at why this is done.
A SIMD unit runs a wavefront by applying each instruction in the shader
program to 64 data elements at a time, essentially in lock-step. (We can ignore
the fact that the wavefront is processed in subgroups of 16 elements each for
the purposes of the present discussion.) However, any one instruction in the

14The compute units on a GPU do contain a scalar ALU and therefore can perform some in-
structions in “single lane” fashion, operating on a single input datum at a time.


<!-- source-pdf-page: 375 -->
> Visual fallback for diagrams/images: [PDF page 375](../../../visual_pages/page_0375.jpg)

Wavefront 0
Wavefront 1
Wavefront 2
Wavefront 3

Stall

Stall

Stall

Runnable

Stall

Runnable

Done

Runnable

Done

Runnable

Done

Time

Done

Figure 4.40. Whenever one wavefront stalls, for example due to memory access latency, the SIMD
unit context switches to another wavefront to ﬁll the delay slot.

program might end up having to access memory, and that introduces a large
stall while the SIMD unit waits for the memory controller to respond.

To fill these large delay slots, a SIMD unit time-slices between multiple
wavefronts (taken from a single shader program, or potentially from many
unrelated shader programs). Whenever one wavefront stalls, the SIMD unit
context switches to another wavefront, thereby keeping the unit busy (as long
as there are runnable wavefronts to switch to). This strategy is illustrated in
Figure 4.40.

As you might well imagine, the SIMD units in a GPU need to perform con-
text switches at a very high frequency. During a context switch on a CPU, the
state of the outgoing thread’s registers are saved to memory so they won’t be
lost, and then the state of the incoming thread’s registers are read from mem-
ory into the CPU’s registers so that it can continue executing where it left off.
However, on a GPU it would take too much time to save the state of each
wavefront’s SIMD registers every time a context switch occurred.

To eliminate the cost of saving registers during context switches, each
SIMD unit contains a very large register file. The number of physical regis-
ters in this register file is many times larger than the number of logical regis-
ters available to any one wavefront (typically on the order of ten times larger).


<!-- source-pdf-page: 376 -->

This means that the contents of the logical registers for up to ten wavefronts
can be maintained at all times in these physical registers. And that in turn
implies that context switches can be performed between wavefronts without
saving or restoring any registers whatsoever.

### 4.11.5 Further Reading

Obviously GPGPU programming is a huge topic, and as always we’ve only
just scratched the surface in this book. For more information on GPGPU and
graphics shader programming, check out the following online tutorials and
resources:

•
Introduction to CUDA programming: https://developer.nvidia.com/
how-to-cuda-c-cpp

•
OpenCL learning resources: https://developer.nvidia.com/opencl

•
HLSL programming guide and reference manual: https://msdn.micro
soft.com/en-us/library/bb509561(v=VS.85).aspx

•
Introduction to the OpenGL shading language: https://www.khronos.
org/opengl/wiki/OpenGL_Shading_Language

•
AMD Radeon™GCN architecture whitepaper:
https://www.amd.
com/Documents/GCN_Architecture_whitepaper.pdf


<!-- source-pdf-page: 377 -->
> Visual fallback for diagrams/images: [PDF page 377](../../../visual_pages/page_0377.jpg)

Taylor & Francis

Taylor & Francis Group
http://taylorandfrancis.com
