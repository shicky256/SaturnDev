#include "sgl.h"
#include "sega_sys.h"

#include "linescroll.h"
#include "assetrefs.h"

#define		NBG1_CEL_ADR		VDP2_VRAM_A0
#define		NBG1_MAP_ADR		( VDP2_VRAM_A0 + 0x10000 )
#define		NBG1_COL_ADR		( VDP2_COLRAM + 0x00200 )
#define		NBG1_LNSCR_ADR		( VDP2_VRAM_B0 )

#define		lineScrTable		((Uint32*)NBG1_LNSCR_ADR)

#define		NBG2_CEL_ADR		( VDP2_VRAM_A0 )
#define		NBG2_MAP_ADR		( VDP2_VRAM_A0 + 0x10000)
#define		NBG2_COL_ADR		( VDP2_COLRAM + 0x00400 )

#define colorRam ((Uint16*)NBG1_COL_ADR)
#define COLOR(r,g,b) ((Uint16)((b << 10) | (g << 5) | r))

static FIXED bg1X = toFIXED(0.0);
static FIXED bg1Y = toFIXED(0.0);
static FIXED bg2X = toFIXED(0.0);
static FIXED bg2Y = toFIXED(0.0);

void initLinescroll(void) {
	int i;
	
	for (i = 0; i < 224; i++) //write scroll offset table
		lineScrTable[i] = slSin(DEGtoANG(i << 2)) << 6;
	
	for (i = 0; i <= 16; i++) //write color gradient
		colorRam[i] = COLOR(i, 0, i);
	
	//init road
	slCharNbg1(COL_TYPE_256, CHAR_SIZE_2x2);
	slPageNbg1((void *)NBG1_CEL_ADR, 0 , PNB_1WORD|CN_10BIT);
	slPlaneNbg1(PL_SIZE_1x1);
	slMapNbg1((void *)NBG1_MAP_ADR , (void *)NBG1_MAP_ADR , (void *)NBG1_MAP_ADR , (void *)NBG1_MAP_ADR);
	slLineScrollModeNbg1(lineSZ1 | lineHScroll);
	slLineScrollTable1((void *)NBG1_LNSCR_ADR);
	
	Cel2VRAM(cel_gradient, (void *)NBG1_CEL_ADR, 4 * 64 * 4);
	Map2VRAM(map_gradient, (void *)NBG1_MAP_ADR, 64, 64, 1, 0); 
	// Pal2CRAM(pal_gradient, (void *)NBG1_COL_ADR, 256);
	slScrPosNbg1(toFIXED(0), toFIXED(0));
	
	// //init clouds
	// slCharNbg2(COL_TYPE_256, CHAR_SIZE_2x2);
	// slPageNbg2((void *)NBG2_CEL_ADR, 0 , PNB_1WORD|CN_10BIT);
	// slPlaneNbg2(PL_SIZE_1x1);
	// slMapNbg2((void *)NBG2_MAP_ADR , (void *)NBG2_MAP_ADR , (void *)NBG2_MAP_ADR , (void *)NBG2_MAP_ADR);
	// slScrPosNbg2(toFIXED(0), toFIXED(0));
	// slPriorityNbg2(4);
	// slColRateNbg2(0x05);
	
}

void updateLinescroll(void) {
	static int count = 0;
	
	int i;
	if (count == 2) {
		for (i = 15; i >= 0; i--) {
			if (i == 0)
				colorRam[i] = colorRam[15];
			else
				colorRam[i] = colorRam[i - 1];
		}
	count = 0;
	}
	else {
		count++;
	}
	bg1X -= toFIXED(2);	
	bg1Y += toFIXED(0.5);
	slScrPosNbg1(bg1X, bg1Y);
}
