#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "8bkc-hal.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"

#include "powerbtn_menu.h"
//ToDo: we can save half the flash for the graphics (=25K?) by saving it as RGB565 with a defined transparent
//color instead of as RGBA.

static const char graphics[]={
#include "graphics.inc"
};


static void renderGfx(uint16_t *ovl, int dx, int dy, int sx, int sy, int sw, int sh) {
	uint32_t *gfx=(uint32_t*)graphics;
	int x, y, i;
	if (dx<0) {
		sx-=dx;
		sw+=dx;
		dx=0;
	}
	if ((dx+sw)>KC_SCREEN_W) {
		sw-=((dx+sw)-KC_SCREEN_W);
		dx=KC_SCREEN_W-sw;
	}
	if (dy<0) {
		sy-=dy;
		sh+=dy;
		dy=0;
	}
	if ((dy+sh)>KC_SCREEN_H) {
		sh-=((dy+sh)-KC_SCREEN_H);
		dy=KC_SCREEN_H-sh;
	}

	for (y=0; y<sh; y++) {
		for (x=0; x<sw; x++) {
			i=gfx[(sy+y)*80+(sx+x)];
			if (i&0x80000000) ovl[(dy+y)*KC_SCREEN_W+(dx+x)]=kchal_fbval_rgb((i>>0)&0xff, (i>>8)&0xff, (i>>16)&0xff);
		}
	}
}

#define SCROLLSPD 4

#define SCN_VOLUME 0
#define SCN_BRIGHT 1
#define SCN_PWRDWN 2
#define SCN_EXIT 3

#define SCN_COUNT 4


//Show in-game menu reachable by pressing the power button
int powerbtn_menu_show(uint16_t *fb) {
	//We'll try to save the current framebuffer image to do the overlay on. If this fails (because out of memory)
	//we just use a black background.
	assert(fb);
	uint16_t *oldfb=malloc(KC_SCREEN_W*KC_SCREEN_H*sizeof(uint16_t));
	if (oldfb!=NULL) memcpy(oldfb, fb, KC_SCREEN_W*KC_SCREEN_H*sizeof(uint16_t));
	int io, newIo, oldIo=0;
	int menuItem=0;
	int prevItem=0;
	int scroll=0;
	int doRefresh=1;
	int powerReleased=0;
	int oldArrowsTick=-1;
	while(1) {
		if (oldfb) {
			for (int i=0; i<KC_SCREEN_W*KC_SCREEN_H; i++) {
				//Dim by 50%
				uint16_t v=(oldfb[i]<<8)|(oldfb[i]>>8);
				v=(v>>1)&0x7bef;
				fb[i]=(v<<8)|(v>>8);
			}
		} else {
			//black background
			memset(fb, 0, KC_SCREEN_W*KC_SCREEN_H*sizeof(uint16_t));
		}
		newIo=kchal_get_keys();
		//Filter out only newly pressed buttons
		io=(oldIo^newIo)&newIo;
		oldIo=newIo;
		if (io&KC_BTN_UP && !scroll) {
			menuItem++;
			if (menuItem>=SCN_COUNT) menuItem=0;
			scroll=-SCROLLSPD;
		}
		if (io&KC_BTN_DOWN && !scroll) {
			menuItem--;
			if (menuItem<0) menuItem=SCN_COUNT-1;
			scroll=SCROLLSPD;
		}
		if ((newIo&KC_BTN_LEFT) || (newIo&KC_BTN_RIGHT)) {
			int v=128;
			if (menuItem==SCN_VOLUME) v=kchal_get_volume();
			if (menuItem==SCN_BRIGHT) v=kchal_get_brightness();
			if (newIo&KC_BTN_LEFT) v-=2;
			if (newIo&KC_BTN_RIGHT) v+=2;
			if (menuItem==SCN_VOLUME) {
				if (v<0) v=0;
				if (v>255) v=255;
				kchal_set_volume(v);
				doRefresh=1;
			}
			if (menuItem==SCN_BRIGHT) {
				if (v<1) v=1;
				if (v>100) v=100;
				kchal_set_brightness(v);
				doRefresh=1;
			}
		}
		if ((io&KC_BTN_A) || (io&KC_BTN_B)) {
			if (menuItem==SCN_PWRDWN) {
				free(oldfb);
				kchal_wait_keys_released();
				return POWERBTN_MENU_POWERDOWN;
			}
			if (menuItem==SCN_EXIT) {
				free(oldfb);
				kchal_wait_keys_released();
				return POWERBTN_MENU_EXIT;
			}
		}
		if (io&KC_BTN_POWER_LONG) {
			free(oldfb);
			return POWERBTN_MENU_POWERDOWN;
		}

		if (!(io&KC_BTN_POWER)) powerReleased=1;
		if (io&KC_BTN_START || (powerReleased && (io&KC_BTN_POWER))) {
			free(oldfb);
			kchal_wait_keys_released();
			return POWERBTN_MENU_NONE;
		}

		if (scroll>0) scroll+=SCROLLSPD;
		if (scroll<0) scroll-=SCROLLSPD;
		if (scroll>KC_SCREEN_H || scroll<-KC_SCREEN_H) {
			prevItem=menuItem;
			scroll=0;
			doRefresh=1; //show last scroll thing
		}
		if (prevItem!=menuItem) renderGfx(fb, (KC_SCREEN_W-80)/2, ((KC_SCREEN_H-64)/2)+16+scroll, 0,32*prevItem,80,32);
		if (scroll) {
			doRefresh=1;
			renderGfx(fb, (KC_SCREEN_W-80)/2, ((KC_SCREEN_H-64)/2)+16+scroll+((scroll>0)?-KC_SCREEN_H:KC_SCREEN_H), 0,32*menuItem,80,32);
			oldArrowsTick=-1; //to force arrow redraw
		} else {
			renderGfx(fb, ((KC_SCREEN_W-80)/2), ((KC_SCREEN_H-64)/2)+16, 0,32*menuItem,80,32);
			//Render arrows
			int t=xTaskGetTickCount()/(400/portTICK_PERIOD_MS);
			t=(t&1);
			if (t!=oldArrowsTick) {
				doRefresh=1;
				renderGfx(fb, ((KC_SCREEN_W-80)/2)+36, ((KC_SCREEN_H-64)/2)+0, t?0:8, 134, 8, 8);
				renderGfx(fb, ((KC_SCREEN_W-80)/2)+36, ((KC_SCREEN_H-64)/2)+56, t?16:24, 134, 8, 8);
				oldArrowsTick=t;
			}
		}
		
		//Handle volume/brightness bars
		if (scroll==0 && (menuItem==SCN_VOLUME)) {
			int v=0;
			if (menuItem==SCN_VOLUME) v=kchal_get_volume();
			if (v<0) v=0;
			if (v>255) v=255;
			renderGfx(fb, ((KC_SCREEN_W-80)/2)+14, ((KC_SCREEN_H-64)/2)+25+16, 14, 130, (v*60)/256, 4);
		}
		if (scroll==0 && (menuItem==SCN_BRIGHT)) {
			int v=0;
			if (menuItem==SCN_BRIGHT) v=kchal_get_brightness();
			if (v<1) v=1;
			if (v>100) v=100;
			renderGfx(fb, ((KC_SCREEN_W-80)/2)+14, ((KC_SCREEN_H-64)/2)+25+16, 14, 130, (v*60)/100, 4);
		}
		
		if (doRefresh) {
			kchal_send_fb(fb);
		}
		doRefresh=0;
		vTaskDelay(20/portTICK_PERIOD_MS);
	}
}

