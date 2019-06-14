#pragma once

void spi_lcd_send(int x, int y, int w, int h, uint16_t *scr);
void spi_lcd_init();
void st7735r_poweroff();
void setBrightness(int value);