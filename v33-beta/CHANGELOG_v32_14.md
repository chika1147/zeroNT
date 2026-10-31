# zeroNT v32.14 MOUSE STABLE

- Keeps the v32.13 original UI and all v32.12 background processing.
- Correct PS/2 X/Y decode using the packet sign bits (true signed 9-bit motion).
- Drops overflowed motion packets instead of turning them into cursor jumps.
- IRQ1/IRQ12 now filter the shared i8042 data port by the AUX status bit.
- Pure pointer motion updates only the small cursor rectangle instead of presenting the whole framebuffer.
- Full UI redraws still happen for clicks, dragging, resizing, selections, wheel scrolling, state changes, and periodic status refresh.
- The backbuffer remains cursor-free so the old cursor can be erased without flicker.
