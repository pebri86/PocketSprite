#include <stdlib.h>
#include <stdio.h>

#include "rom/ets_sys.h"
#include "smsplus-main.h"
#include <string.h>
#include "nvs.h"
#include "8bkc-hal.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"

const char graphics[]={
#include "graphics.inc"
};


void renderGfx(uint32_t *ovl, int dx, int dy, int sx, int sy, int sw, int sh) {
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
			if (i&0x80000000) ovl[(dy+y)*KC_SCREEN_W+(dx+x)]=i;
		}
	}
}


#define SCROLLSPD 4

#define SCN_VOLUME 0
#define SCN_BRIGHT 1
#define SCN_CHROM 2
#define SCN_PWRDWN 3
#define SCN_RESET 4
#define SCN_EXIT 5

#define SCN_COUNT 6


static void menuShowSnapshotting(uint32_t *overlay) {
	memset(overlay, 0, KC_SCREEN_W*KC_SCREEN_H*4);
	renderGfx(overlay, (KC_SCREEN_W-80)/2, ((KC_SCREEN_H-64)/2)+14, 0, 268, 80, 40);
	vidRenderOverlay();
}

//Show in-game menu reachable by pressing the power button
int menuShow() {
	int io, newIo, oldIo=0;
	int powerReleased=0;
	int menuItem=0;
	int prevItem=0;
	int scroll=0;
	int doRefresh=1;
	int oldArrowsTick=-1;
	uint32_t *overlay=vidGetOverlayBuf();
	kchal_sound_mute(1);
	while(1) {
		esp_task_wdt_feed();
		memset(overlay, 0, KC_SCREEN_W*KC_SCREEN_H*4);
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
				menuShowSnapshotting(overlay);
				return EMU_RUN_POWERDOWN;
			}
			if (menuItem==SCN_CHROM) {
				menuShowSnapshotting(overlay);
				return EMU_RUN_NEWROM;
			}
			if (menuItem==SCN_RESET) {
				kchal_sound_mute(0);
				return EMU_RUN_RESET;
			}
			if (menuItem==SCN_EXIT) {
				menuShowSnapshotting(overlay);
				return EMU_RUN_EXIT;
			}
		}
		if (io&KC_BTN_POWER_LONG) {
			menuShowSnapshotting(overlay);
			return EMU_RUN_POWERDOWN;
		}

		if (!(io&KC_BTN_POWER)) powerReleased=1;
		if (io&KC_BTN_START || (powerReleased && (io&KC_BTN_POWER))) {
			kchal_sound_mute(0);
			return EMU_RUN_CONT;
		}

		if (scroll>0) scroll+=SCROLLSPD;
		if (scroll<0) scroll-=SCROLLSPD;
		if (scroll>KC_SCREEN_H || scroll<-KC_SCREEN_H) {
			prevItem=menuItem;
			scroll=0;
			doRefresh=1; //show last scroll thing
		}
		if (prevItem!=menuItem) renderGfx(overlay, ((KC_SCREEN_W-80)/2)+0, ((KC_SCREEN_H-64)/2)+16+scroll, 0,32*prevItem,80,32);
		if (scroll) {
			doRefresh=1;
			renderGfx(overlay, ((KC_SCREEN_W-80)/2)+0, ((KC_SCREEN_H-64)/2)+16+scroll+((scroll>0)?-KC_SCREEN_H:KC_SCREEN_H), 0,32*menuItem,80,32);
			oldArrowsTick=-1; //to force arrow redraw
		} else {
			renderGfx(overlay, ((KC_SCREEN_W-80)/2)+0, ((KC_SCREEN_H-64)/2)+16, 0,32*menuItem,80,32);
			//Render arrows
			int t=xTaskGetTickCount()/(400/portTICK_PERIOD_MS);
			t=(t&1);
			if (t!=oldArrowsTick) {
				doRefresh=1;
				renderGfx(overlay, ((KC_SCREEN_W-80)/2)+36, ((KC_SCREEN_H-64)/2)+0, t?0:8, 308, 8, 8);
				renderGfx(overlay, ((KC_SCREEN_W-80)/2)+36, ((KC_SCREEN_H-64)/2)+56, t?16:24, 308, 8, 8);
				oldArrowsTick=t;
			}
		}
		
		//Handle volume/brightness bars
		if (scroll==0 && (menuItem==SCN_VOLUME)) {
			int v=0;
			if (menuItem==SCN_VOLUME) v=kchal_get_volume();
			if (v<0) v=0;
			if (v>255) v=255;
			renderGfx(overlay, ((KC_SCREEN_W-80)/2)+14, ((KC_SCREEN_H-64)/2)+25+16, 14, 193, (v*60)/256, 4);
		}
		if (scroll==0 && (menuItem==SCN_BRIGHT)) {
			int v=0;
			if (menuItem==SCN_BRIGHT) v=kchal_get_brightness();
			if (v<1) v=1;
			if (v>100) v=100;
			renderGfx(overlay, ((KC_SCREEN_W-80)/2)+14, ((KC_SCREEN_H-64)/2)+25+16, 14, 193, (v*60)/100, 4);
		}
		
		if (doRefresh) {
			vidRenderOverlay();
		}
		doRefresh=0;
		vTaskDelay(20/portTICK_PERIOD_MS);
	}
}

