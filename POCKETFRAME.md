# PocketFrame in PocketOPDS

PocketOPDS uses the same e-ink UI language as PocketChat:

- monochrome surfaces with no gradients or shadows;
- 16 px outer margins and inset black row rules;
- a 6 px vertical rail for navigation and primary actions;
- an 8 px square marker for secondary actions and content;
- outlined, text-only format badges;
- a minimum 88 px touch row;
- stable layouts designed for full e-ink refreshes.

The active application renderer is `src/pocketopds_ui.cpp`. It owns the whole
frame, including headers, navigation buttons, rows, server actions, scrolling,
errors, book details, and the bottom download action. It deliberately does not
use InkView `OpenList`, because that widget adds firmware-specific headers,
Home/Back controls, and selection styling that cannot match PocketFrame.

New screens must use this renderer's geometry rather than introducing local
spacing, separator, or button styles. Firmware panels must remain disabled
with `SetPanelType(0)` so the layout starts at the real framebuffer origin on
first launch.
