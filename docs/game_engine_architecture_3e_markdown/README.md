# Game Engine Architecture, Third Edition - Markdown Extraction

Source: `Game Engine Architecture, Third Edition -- Jason Gregory, Jason Gregory -- Game Engine Architecture, 3, 2019 -- A K Peters_CRC Press -- isbn13 9781138035454 -- 55fe37649db461314f6b54d4c79b4d6d -- Anna’s Archive.pdf`

This archive contains:

- **565 numbered headings**, each in its own Markdown file.
- `hierarchical/`: each chapter/section/subsection file includes its complete subtree until the next heading of the same or higher level.
- `atomic/`: each heading file contains only the text until the immediately following numbered heading.
- `verbatim_pages_pdftotext/`: one independent raw Markdown extraction per PDF page, with running headers/page numbers retained.
- `full_book_verbatim_pdftotext.md`: the entire independent page extraction in one Markdown file.
- `visual_pages/`: 526 rendered fallback pages covering image-heavy pages, figure/table pages, color plates, and pages with little extractable text.
- Front matter, all five part title pages, bibliography, index, and color plates are included.
- `manifest.json`, `manifest.csv`, and `EXTRACTED_OUTLINE.md` map every numbered heading to its files and PDF page range.

## Fidelity note

No PDF-to-text pipeline can honestly guarantee character-perfect recovery of every equation, diagram label, embedded image word, ligature, or reading order. This package reduces that risk by combining two independent text extraction paths (PyMuPDF and Poppler `pdftotext`) and rendered visual fallbacks. Printed line-break hyphenation is intentionally preserved in the structured files rather than guessed or rewritten.
