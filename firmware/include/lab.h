#pragma once
// Register lab (test builds only, -D DISPLAY_LAB): a screen to send raw controller registers to the panel and
// see their effect at once, e.g. to hunt for settings that remove stripes. Not part of the released images.
#ifdef DISPLAY_LAB
void lab_run();                // blocks until "Schliessen"; keeps the connection to Home Assistant alive
#endif
