# ST7789 + ESP32-C3 + Kaoruko GIF

This package converts the uploaded 498x498 GIF into 10 RGB565 frames at 96x96 pixels,
then stores them as a compact LZSS stream in flash. Frames are decoded into DMA buffers
as they are displayed. The source GIF has 10 frames with 100 ms per frame (about 10 FPS).

Files:
- `kaoruko_gif.h`: frame dimensions, count, delays
- `kaoruko_gif.c`: compressed frame data and the RGB565 frame decoder
- `lcd_display.c`: parking UI + animation task + LCD mutex + two DMA buffers
- `CMakeLists_snippet.txt`: how to add the new C source to your component

The animation is placed at `(x=144, y=100)` and occupies `96x96` pixels.
The parking information is kept on the left/top side.

The code uses two DMA-capable animation buffers because ESP-IDF's LCD color transfers can run asynchronously; alternating buffers avoids overwriting a buffer that may still be used by SPI DMA.

Use the same wiring and LCD GPIO configuration you already have:
SCK=4, MOSI=6, DC=7, RST=3, CS=10, BL=5, VCC=3V3, GND=GND.
