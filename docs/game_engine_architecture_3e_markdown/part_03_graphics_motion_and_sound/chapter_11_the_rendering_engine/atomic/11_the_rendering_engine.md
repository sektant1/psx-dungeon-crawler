# 11 The Rendering Engine

> Source PDF pages: 640-641
> Extraction mode: PyMuPDF text blocks; line breaks and printed hyphenation are preserved.

<!-- source-pdf-page: 640 -->

W
hen most people think about computer and video games, the first thing
that comes to mind is the stunning three-dimensional graphics. Real-
time 3D rendering is an exceptionally broad and profound topic, so there’s
simply no way to cover all of the details in a single chapter. Thankfully there
are a great many excellent books and other resources available on this topic.
In fact, real-time 3D graphics is perhaps one of the best covered of all the tech-
nologies that make up a game engine. The goal of this chapter, then, is to pro-
vide you with a broad understanding of real-time rendering technology and
to serve as a jumping-off point for further learning. After you’ve read through
these pages, you should find that reading other books on 3D graphics seems
like a journey through familiar territory. You might even be able to impress
your friends at parties (…or alienate them…).
We’ll begin by laying a solid foundation in the concepts, theory and mathe-
matics that underlie any real-time 3D rendering engine. Next, we’ll have a look
at the software and hardware pipelines used to turn this theoretical framework
into reality. We’ll discuss some of the most common optimization techniques
and see how they drive the structure of the tools pipeline and the runtime
rendering API in most engines. We’ll end with a survey of some of the ad-
vanced rendering techniques and lighting models in use by game engines to-
day. Throughout this chapter, I’ll point you to some of my favorite books and


<!-- source-pdf-page: 641 -->
> Visual fallback for diagrams/images: [PDF page 641](../../../visual_pages/page_0641.jpg)

other resources that should help you to gain an even deeper understanding of
the topics we’ll cover here.
