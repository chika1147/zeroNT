#pragma once
#include <stdint.h>

void gdt_init();
void idt_init();
void pic_init();
void pit_init(uint32_t hz);
void ps2_init();

uint64_t timer_ticks();
bool keyboard_pop(uint8_t& scancode);
bool mouse_pop(uint8_t& data);
uint8_t mouse_packet_size();
