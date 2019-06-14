
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "rom/ets_sys.h"
#include "fb.h"
#include "lcd.h"
#include <string.h>
#include "8bkc-hal.h"
#include "menu.h"

#include "esp_task_wdt.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"



static uint16_t *frontbuff=NULL, *backbuff=NULL;
static volatile uint16_t *toRender=NULL;
static volatile uint16_t *overlay=NULL;
struct fb fb;
static SemaphoreHandle_t renderSem;

static bool doShutdown=false;

void vid_preinit()
{
}

void gnuboy_esp32_videohandler();
void lineTask();

void videoTask(void *pvparameters) {
	gnuboy_esp32_videohandler();
}


void vid_init()
{
	doShutdown=false;
	frontbuff=malloc(160*144*2);
	backbuff=malloc(160*144*2);
	fb.w = 160;
	fb.h = 144;
	fb.pelsize = 2;
	fb.pitch = 160*2;
	fb.ptr = (unsigned char*)frontbuff;
	fb.enabled = 1;
	fb.dirty = 0;

	fb.indexed = 0;
	fb.cc[0].r = fb.cc[2].r = 3;
	fb.cc[1].r = 2;
	fb.cc[0].l = 11;
	fb.cc[1].l = 5;
	fb.cc[2].l = 0;
	
	gbfemtoMenuInit();
	memset(frontbuff, 0, 160*144*2);
	memset(backbuff, 0, 160*144*2);

	renderSem=xSemaphoreCreateBinary();
	xTaskCreatePinnedToCore(&videoTask, "videoTask", 1024*2, NULL, 5, NULL, 1);
	ets_printf("Video inited.\n");
}


void vid_close()
{
	doShutdown=true;
	xSemaphoreGive(renderSem);
	vTaskDelay(100); //wait till video thread shuts down... pretty dirty
	free(frontbuff);
	free(backbuff);
	vQueueDelete(renderSem);
}

void vid_settitle(char *title)
{
}

void vid_setpal(int i, int r, int g, int b)
{
}

extern int patcachemiss, patcachehit, frames;


void vid_begin()
{
//	vram_dirty(); //use this to find out a viable size for patcache
	frames++;
	patcachemiss=0;
	patcachehit=0;
	esp_task_wdt_feed();
}

void vid_end()
{
	overlay=NULL;
	toRender=(uint16_t*)fb.ptr;
	xSemaphoreGive(renderSem);
	if (fb.ptr == (unsigned char*)frontbuff ) {
		fb.ptr = (unsigned char*)backbuff;
	} else {
		fb.ptr = (unsigned char*)frontbuff;
	}
//	printf("Pcm %d pch %d\n", patcachemiss, patcachehit);
}

uint16_t *vidGetOverlayBuf() {
	return (uint16_t*)fb.ptr;
}

void vidRenderOverlay() {
	overlay=(uint16_t*)fb.ptr;
	if (fb.ptr == (unsigned char*)frontbuff ) toRender=(uint16_t*)backbuff; else toRender=(uint16_t*)frontbuff;
	xSemaphoreGive(renderSem);
}

void kb_init()
{
}

void kb_close()
{
}

void kb_poll()
{
}

void ev_poll()
{
	kb_poll();
}


uint16_t oledfb[KC_SCREEN_W*KC_SCREEN_H];

#if 1
//#define NEAREST_NEIGHBOR_ALG

static uint16_t getPixel(const uint16_t *bufs, int x, int y, int w1, int h1, int w2, int h2, float x_ratio, float y_ratio)
{
    uint16_t col;
#ifdef NEAREST_NEIGHBOR_ALG
    /* Resize using nearest neighbor alghorithm */
    /* Simple and fastest way but low quality   */
    int x2 = floor(x*x_ratio);
    int y2 = floor(y*y_ratio);
    col = bufs[(y2*w1)+x2];

    return col;
#else
    /* Resize using bilinear interpolation */
    /* higher quality but lower performance, */
    int xv, yv, red, green, blue, a, b, c, d, index;
	float x_diff, y_diff;

    xv = floor(x_ratio * x);
    yv = floor(y_ratio * y);

    x_diff = ((x_ratio * x)) - (xv);
    y_diff = ((y_ratio * y)) - (yv);

    index = yv * w1 + xv;

    a = bufs[index];
    b = bufs[index + 1];
    c = bufs[index + w1];
    d = bufs[index + w1 + 1];

    red = (((a >> 11) & 0x1f) * (1 - x_diff) * (1 - y_diff) + ((b >> 11) & 0x1f) * (x_diff) * (1 - y_diff) +
           ((c >> 11) & 0x1f) * (y_diff) * (1 - x_diff) + ((d >> 11) & 0x1f) * (x_diff * y_diff));

    green = (((a >> 5) & 0x3f) * (1 - x_diff) * (1 - y_diff) + ((b >> 5) & 0x3f) * (x_diff) * (1 - y_diff) +
             ((c >> 5) & 0x3f) * (y_diff) * (1 - x_diff) + ((d >> 5) & 0x3f) * (x_diff * y_diff));

    blue = (((a)&0x1f) * (1 - x_diff) * (1 - y_diff) + ((b)&0x1f) * (x_diff) * (1 - y_diff) +
            ((c)&0x1f) * (y_diff) * (1 - x_diff) + ((d)&0x1f) * (x_diff * y_diff));

    col = ((int)red << 11) | ((int)green << 5) | ((int)blue);

    return col;
#endif
}

#else

//Averages four pixels into one
int getAvgPix(uint16_t* bufs, int pitch, int x, int y) {
	int col;
	if (x<0 || x>=160) return 0;
	//16-bit: E79C
	//15-bit: 739C
	col=(bufs[x+(y*(pitch>>1))]&0xE79C)>>2;
	col+=(bufs[(x+1)+(y*(pitch>>1))]&0xE79C)>>2;
	col+=(bufs[x+((y+1)*(pitch>>1))]&0xE79C)>>2;
	col+=(bufs[(x+1)+((y+1)*(pitch>>1))]&0xE79C)>>2;
	return col&0xffff;
}

//Averages four pixels into one, but does subpixel rendering to give a slightly higher
//X resolution at the cost of color fringing.
//Bitmasks:
//RRRR.RGGG.GGGB.BBBB
//1111.0111.1101.1110 = F7DE
//0000.1000.0010.0001 = 0821
//1111.1000.0000.0000 = F800
//0000.0111.1110.0000 = 07E0
//0000.0000.0001.1111 = 001F
//so (RGB565val&0xF7DE)>>1 halves the R, G, B color components.
int getAvgPixSubpixrendering(uint16_t* bufs, int pitch, int x, int y) {
	uint32_t *pixduo=(uint32_t*)bufs;
	if (x<0 || x>=160) return 0;
	//Grab top and bottom two pixels.
	uint32_t c1=pixduo[(x/2)+(y*(pitch>>2))];
	uint32_t c2=pixduo[(x/2)+((y+1)*(pitch>>2))];
	//Average the two.
	uint32_t c=((c1&0xF7DEF7DE)+(c2&0xF7DEF7DE))>>1;
	//The averaging action essentially killed the least significant bit of all colors; if
	//both were one the resulting color should be one more. Compensate for that here.
	c+=(c1&c1)&0x08210821;

	//Take the various components from the pixels and return the composite.
	uint32_t red_comp=c&0xF800;
	uint32_t green_comp=c&0x07E0;
	green_comp+=(c>>16)&0x07E0;
	green_comp=(green_comp/2)&0x7E0;
	uint32_t blue_comp=(c>>16)&0x001F;
	return red_comp+green_comp+blue_comp;
}

//Averages 6 pixels into one (area of w=2, h=3), but does subpixel rendering to give a slightly higher
//X resolution at the cost of color fringing. This is slightly more elaborate as we cannot just use additions,
//shifts and bitmasks.
#define RED(i) (((i)>>11) & 0x1F)
#define GREEN(i) (((i)>>5) & 0x3F)
#define BLUE(i) (((i)>>0) & 0x1F)
int getAvgPixSubpixrenderingThreeLines(uint16_t* bufs, int pitch, int x, int y) {
	int r=0, g=0, b=0;
	for (int line=0; line<3; line++) {
		r+=RED(bufs[x+((y+line)*(pitch>>1))]);
		g+=GREEN(bufs[x+((y+line)*(pitch>>1))]);
		g+=GREEN(bufs[x+1+((y+line)*(pitch>>1))]);
		b+=BLUE(bufs[x+1+((y+line)*(pitch>>1))]);
	}
	r=r/3;
	g=g/6;
	b=b/3;
	return (r<<11)+(g<<5)+(b);
}

#endif

int addOverlayPixel(uint16_t p, uint16_t ov) {
	int or, og, ob, a;
	int br, bg, bb;
	int r,g,b;
	br=((p>>11)&0x1f)<<3;
	bg=((p>>5)&0x3f)<<2;
	bb=((p>>0)&0x1f)<<3;

	a=(ov)&0xff;
	//hack: Always show background darker
	a=(a/2)+128;

	ob=(ov>>16)&0xff;
	og=(ov>>8)&0xff;
	or=(ov>>0)&0xff;

	r=(br*(256-a))+(or*a);
	g=(bg*(256-a))+(og*a);
	b=(bb*(256-a))+(ob*a);

	return ((r>>(3+8))<<11)+((g>>(2+8))<<5)+((b>>(3+8))<<0);
}

//This thread runs on core 1.
void gnuboy_esp32_videohandler() {
	int x, y;
	uint16_t *oledfbptr;
	uint16_t c;
	uint16_t *ovl;
#if 1
	float x_ratio = 160/(float)KC_SCREEN_W;
    float y_ratio = 144/(float)KC_SCREEN_H;
#else
	int x_ratio = ((160<<16)/KC_SCREEN_W) +1;
    int y_ratio = ((144<<16)/KC_SCREEN_H) +1;
#endif
	volatile uint16_t *rendering;
	printf("Video thread running\n");
	memset(oledfb, 0, sizeof(oledfb));
	while(!doShutdown) {
		//if (toRender==NULL) 
		xSemaphoreTake(renderSem, portMAX_DELAY);
		rendering=toRender;
		ovl=(uint16_t*)overlay;
		oledfbptr=oledfb;
#if 1
		for (y=0; y<KC_SCREEN_H; y++) {
			for (x=0; x<KC_SCREEN_W; x++) {
				c=getPixel((uint16_t*)rendering, x, y, 160, 144, KC_SCREEN_W, KC_SCREEN_H, x_ratio, y_ratio);
				if (ovl) c=addOverlayPixel(c, *ovl++);
				*oledfbptr++=((c>>8) | (c<<8));
			}
		}
#else
		for (y=0; y<KC_SCREEN_H; y++) {
			int doThreeLines=((y%4)==0);
			for (x=0; x<KC_SCREEN_W; x++) {
				int x2 = ((x*x_ratio)>>16);
    			int y2 = ((y*y_ratio)>>16);
				if (!doThreeLines) {
					c=getAvgPixSubpixrendering((uint16_t*)rendering, 160*2, x2, y2);
				} else {
					c=getAvgPixSubpixrenderingThreeLines((uint16_t*)rendering, 160*2, x2, y2);
				}
				if (ovl) c=addOverlayPixel(c, *ovl++);
				*oledfbptr++=(c>>8)+((c&0xff)<<8);
			}
		}
#endif
		kchal_send_fb(oledfb);
	}
	vTaskDelete(NULL);
}





