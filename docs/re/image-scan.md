# Scanning the game image: sections and the .bss problem

Several locators read the loaded eduke32.exe image (imagescan.h). This note records which parts of the image they
may search and why.

- `ek_image_sections` drops .bss: it has SizeOfRawData 0, so there are no raw bytes to search.
- `ek_image_data_extent` exists for the same reason. A membership test built from the byte-searchable sections refused
  every .bss array: Polymer's drawingstate was refused and the whole-map sweep stayed unavailable until the check used
  the extent of the containing section.
- `ek_image_count_calls` (counting `call rel32` sites that reach a given function) was lifted out of camerascan.cpp so
  every locator shares it.

Related: [polymer-sweep.md](polymer-sweep.md), [freecam.md](freecam.md).

## Method
Reading the PE section table and exception directory of the stock build (r10669).
