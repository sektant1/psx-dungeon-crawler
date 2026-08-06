# 4 Parallelism and Concurrent Programming

> Source PDF pages: 222-223
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
