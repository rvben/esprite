// A trivial Arduino sketch: fill the screen so a screenshot is non-blank.
#include "Arduino_GFX_Library.h"
static Arduino_GFX gfx(120, 80);
void setup() { gfx.begin(); gfx.fillScreen(RED); }
void loop() {}
