# Validation Report

- PDF page count: **1240**
- Raw `pdftotext` page files produced: **1240**
- Retained PyMuPDF text blocks: **10383**
- Unique numbered headings detected: **565**
- Duplicate numbered headings: **0**
- Numbered outline entries checked: **124**
- Missing numbered outline entries: **0**
- Chapters 1-17 detected: **yes**
- PyMuPDF retained characters: **2,366,488**
- `pdftotext -raw` characters: **2,417,892**
- Pages with no raw extractable text: **[1]**
- Rendered visual fallback pages: **526**

The heading detector requires every numbered PDF bookmark to exist in the detailed heading set. The build aborts on duplicate numbers, missing chapters, page-count mismatch, or missing outline headings.
