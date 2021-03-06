/*
To-Dos:

Have a game over graphic when it's game over
Separate out enemy logic


*/

#include	"sgl.h"
#include	"sega_sys.h"

#include "game.h"
#include "assetrefs.h"
#include "linescroll.h"
#include "sprattrs.c"
#include "spritelist.h"
#include "tilemap.h"

#define		NBG3_CEL_ADR		( VDP2_VRAM_A1 )
#define		NBG3_MAP_ADR		( VDP2_VRAM_B1 + 0x2000)
#define		NBG3_COL_ADR		( VDP2_COLRAM + 0x00600 )
#define		BACK_COL_ADR		( VDP2_VRAM_A1 + 0x1fffe )

//player movement stuff
#define PLAYER_STATE_FALLING 0
#define PLAYER_STATE_RISING 1
#define PLAYER_STATE_DEAD 2
int playerState = PLAYER_STATE_FALLING;
FIXED scale = toFIXED(0.25);
FIXED scaleSpeed = toFIXED(0.0);

//game state stuff
#define GAME_STATE_START 0
#define GAME_STATE_FADEIN 1
#define GAME_STATE_NORMAL 2 
#define GAME_STATE_FADEOUT 3
#define GAME_STATE_COMPLETE 4
#define GAME_STATE_CURSOR 5
#define GAME_STATE_GAMEOVER 6
int gameState; //for main game state machine

//fake playfield
Uint16 playfield[1024]; //32 * 32
Uint16* playfieldPtr;

//sprite stuff
SPRITE_INFO sprites[MAX_SPRITES];
SPRITE_INFO defaultSprite;
int eye1Index;
int eye2Index;
#define NUM_PLAYER_SPRITES 4 //the number of sprites used for player face animation
Uint8 numSprites; //the number of sprites the engine's keeping track of
Uint8 dispFace; //1 if we're displaying the "player face" sprites, 0 otherwise

//screen dimensions
FIXED screenX = toFIXED(0.0);
FIXED screenY = toFIXED(0.0);
#define SCREEN_BOUND_L toFIXED(-160)
#define SCREEN_BOUND_R toFIXED(160)
#define SCREEN_BOUND_T toFIXED(-112)
#define SCREEN_BOUND_B toFIXED(112)
FIXED startX = toFIXED(0.0);
FIXED startY = toFIXED(0.0);

//bg stuff
Uint16 bgLayers;
Uint8 bgMode = MODE_TILEMAP;

int score = 0;
int lives = DEFAULT_LIVES;
int frames = 0;

//function prototypes
static void set_sprite(PICTURE *pcptr , Uint32 NbPicture, TEXTURE *txptr);
static void handleInput(void);
static void initSprites(void);	
static void initVDP2(void);
static void updateBG(void);
static void handlePlayerMovement(void);
static Uint8 handleSpriteCollision(FIXED x, FIXED y);
static FIXED handleGroundCollision(FIXED x, FIXED y);
static Uint8 spriteCollisionBehavior(int index, int collidingIndex);
static void handleSpriteRemoval(int index, int points);
static void writeBlock(Uint16 x, Uint16 y, Uint16 data);
static FIXED* setShotVelocity(FIXED playerX, FIXED playerY, FIXED spriteX, FIXED spriteY);
static void updateSprites(void);
static Uint8 checkShotCollision(int index);
static void dispSprites(void);
static void drawPlayField(void);
static int getNumDigits(int num);
static void dispNum(int number, FIXED x, FIXED y);
static inline void updateHud(void);
static void dispLives(void);
static void dispScore(void);
static int dispGameOver();

static void set_sprite(PICTURE *pcptr, Uint32 NbPicture, TEXTURE *texptr)
{
	TEXTURE *txptr;
 
	for(; NbPicture-- > 0; pcptr++){
		txptr = texptr + pcptr->texno;
		slDMACopy((void *)pcptr->pcsrc,
			(void *)(SpriteVRAM + ((txptr->CGadr) << 3)),
			(Uint32)((txptr->Hsize * txptr->Vsize * 4) >> (pcptr->cmode)));
	}
}
#define EYE_MAX_XPOS toFIXED(-7)
#define EYE_MIN_XPOS toFIXED(-13)
#define EYE_MAX_YPOS toFIXED(-10)
#define EYE_MIN_YPOS toFIXED(0)
#define EYE_MOVE_SPEED toFIXED(0.5)
#define EYE_DISTANCE toFIXED(23)
#define BASE_MOVE_SPEED toFIXED(2.0)
static void handleInput(void)
{
	int moveSpeed = BASE_MOVE_SPEED; //slMulFX(scale, BASE_MOVE_SPEED); //speed the player moves at
	
	Uint16 data = ~Smpc_Peripheral[0].data;
	if (playerState != PLAYER_STATE_DEAD) {
		if (data & PER_DGT_KR) {
			screenX += moveSpeed;
			if (sprites[eye1Index].pos[X] < EYE_MAX_XPOS)
				sprites[eye1Index].pos[X] += EYE_MOVE_SPEED;
		}
		else if (data & PER_DGT_KL) {
			screenX -= moveSpeed;
			if (sprites[eye1Index].pos[X] > EYE_MIN_XPOS)
				sprites[eye1Index].pos[X] -= EYE_MOVE_SPEED;
		}
		if (data & PER_DGT_KU) {
			screenY -= moveSpeed;
			if (sprites[eye1Index].pos[Y] > EYE_MAX_YPOS)
				sprites[eye1Index].pos[Y] -= EYE_MOVE_SPEED;
		}
		else if (data & PER_DGT_KD) {
			screenY += moveSpeed;
			if (sprites[eye1Index].pos[Y] < EYE_MIN_YPOS)
				sprites[eye1Index].pos[Y] += EYE_MOVE_SPEED;
		}
		sprites[eye2Index].pos[X] = sprites[eye1Index].pos[X] + EYE_DISTANCE;
		sprites[eye2Index].pos[Y] = sprites[eye1Index].pos[Y];
	}
	//debug cursor movement
	if (data & PER_DGT_TA) {
		if (gameState == GAME_STATE_NORMAL) {
			gameState = GAME_STATE_CURSOR;
			scale = toFIXED(1.0);
			moveSpeed = toFIXED(0.5);
		}
		else {
			gameState = GAME_STATE_NORMAL;
			moveSpeed = toFIXED(2.0);
		}
	}
	if (gameState == GAME_STATE_CURSOR) {
		if (data & PER_DGT_TB)
			scale += toFIXED(0.1);
		else if (data & PER_DGT_TC)
			scale -= toFIXED(0.1);
	}
	
	
}

static void initSprites(void)
{
	int i;
	numSprites = 0;
	defaultSprite.pos[X] = toFIXED(0.0); //start offscreen (160 + sprite width)
	defaultSprite.pos[Y] = toFIXED(0.0);
	defaultSprite.pos[Z] = toFIXED(169);
	defaultSprite.pos[S] = toFIXED(1.0); 
	defaultSprite.ang = 0;
	defaultSprite.attr = &CIRCLE_ATTR;
	defaultSprite.type = TYPE_CIRCLE;
	defaultSprite.dx = toFIXED(0.0);
	defaultSprite.dy = toFIXED(0.0);
	defaultSprite.state = SPRITE_STATE_NORM;
	defaultSprite.scratchpad = 0;
	
	//add eye sprites
	SPRITE_INFO info = defaultSprite;
	info.attr = &SCLERA_ATTR;
	info.type = TYPE_STATIC;
	info.pos[X] = toFIXED(-12);
	info.pos[Y] = toFIXED(-8);
	info.pos[Z] = toFIXED(160);
	addSprite(info);
	info.pos[X] = toFIXED(11);
	addSprite(info);
	info.pos[X] = 0;
	info.pos[Y] = 0;
	
	info.attr = &EYE_ATTR;
	eye1Index = addSprite(info);
	eye2Index = addSprite(info);
	
	sprites[eye1Index].pos[X] = toFIXED(-10);
	sprites[eye1Index].pos[Y] = toFIXED(-7);
	sprites[eye2Index].pos[X] = toFIXED(10);
	sprites[eye2Index].pos[Y] = toFIXED(-7);
	
}


static void initVDP2(void)
{
	slColRAMMode(CRM16_2048);
	slBack1ColSet((void *)BACK_COL_ADR , 0);
	
	switch (bgMode) {
		case MODE_TILEMAP:
			initTilemap();
			bgLayers = TILEMAP_BGS;
			slColorCalc(CC_RATE | CC_TOP | NBG2ON | NBG3ON);
			slColorCalcOn(NBG2ON | NBG3ON);
		break;
		case MODE_LINESCROLL:
			initLinescroll();
			bgLayers = LINESCROLL_BGS;
			slColorCalc(CC_RATE | CC_TOP | NBG3ON);
			slColorCalcOn(NBG3ON);
		break;
	}
	
	//init face
	slCharNbg3(COL_TYPE_256, CHAR_SIZE_2x2);
	slPageNbg3((void *)NBG3_CEL_ADR, 0 , PNB_1WORD|CN_10BIT);
	slPlaneNbg3(PL_SIZE_1x1);
	slMapNbg3((void *)NBG3_MAP_ADR , (void *)NBG3_MAP_ADR , (void *)NBG3_MAP_ADR , (void *)NBG3_MAP_ADR);
	Cel2VRAM(cel_face, (void *)NBG3_CEL_ADR, 16 * 64 * 4);
	//offset parameter seems to be # of tiles before start of this bg's tiles (256 byte) in that vram bank * 2
	Map2VRAM(map_face, (void *)NBG3_MAP_ADR, 64, 64, 3, 0);
	Pal2CRAM(pal_face, (void *)NBG3_COL_ADR, 256);
	slScrPosNbg3(toFIXED(-160.0) + toFIXED(32.0), toFIXED(-116.0) + toFIXED(32.0)); //plus half width of sprite
	slPriorityNbg3(7);
	slColRateNbg3(0x00);
	
	// slColorCalcOn(NBG2ON | NBG3ON);
	slScrAutoDisp(bgLayers);
	
	// slScrAutoDisp(RBG0ON);
}

static void updateBG(void)
{	
	if (gameState == GAME_STATE_NORMAL) {
		handlePlayerMovement();
	}
	switch(bgMode) {
		case MODE_TILEMAP:
			updateTilemap();
		break;
		case MODE_LINESCROLL:
			updateLinescroll();
		break;
	}
}

static void handlePlayerMovement(void)
{
	#define GRAVITY toFIXED(0.007)
	static int playerNode;
	static FIXED playerFallSpeed;
	switch (playerState) {
	case PLAYER_STATE_FALLING:
		slPrint("fall", slLocate(0,8));
		slPrintFX(scaleSpeed, slLocate(0,2));
		slPrintFX(scale, slLocate(0,3));
		scaleSpeed += GRAVITY;
		scale += scaleSpeed;
		if (scale >= toFIXED(5)) { //when we "hit the ground"
			playerState = PLAYER_STATE_RISING;
			scaleSpeed = handleGroundCollision((screenX >> 4), (screenY >> 4));//toFIXED(-0.25); //we're setting the speed here to avoid rounding errors causing bouncing less height each time
		//	slPrintFX((screenX >> 4), slLocate(0,1));
			//slPrintFX((screenY >> 4), slLocate(0,2));
			if (!handleSpriteCollision(screenX, screenY)) {
				if (!scaleSpeed) {
					playerState = PLAYER_STATE_DEAD;
					bgLayers &= ~NBG3ON;
					slScrAutoDisp(bgLayers); //turn off player's bg layer
					dispFace = 0;
					SPRITE_INFO tmp = defaultSprite;
					tmp.attr = &PLAYER_ATTR;
					tmp.type = TYPE_NULL;
					tmp.pos[X] = screenX;
					tmp.pos[Y] = screenY;
					tmp.pos[S] = toFIXED(1.0);
					playerNode = addSprite(tmp);
					playerFallSpeed = toFIXED(0.0);
				}
			}
		}
	break;
	case PLAYER_STATE_RISING:
		slPrintFX(scaleSpeed, slLocate(0,2));
		slPrintFX(scale, slLocate(0,3));
		slPrint("rise", slLocate(0,8));
		scaleSpeed += GRAVITY;
		scale += scaleSpeed;
		if (scaleSpeed >= 0) {
			playerState = PLAYER_STATE_FALLING;
		}
	break;
	case PLAYER_STATE_DEAD:
		if (sprites[playerNode].pos[S] > toFIXED(0.0)) {
			playerFallSpeed += toFIXED(0.005);
			sprites[playerNode].pos[S] -= playerFallSpeed;
			sprites[playerNode].ang += 5;
		}   
		else { //reset player pos
			if (lives == 0) {
				gameState = GAME_STATE_GAMEOVER;
				deleteSprite(playerNode);
				return;
			}
			scale = toFIXED(1.0);
			scaleSpeed = toFIXED(0.0);
			screenX = startX;
			screenY = startY;
			playerState = PLAYER_STATE_FALLING;
			bgLayers |= NBG3ON;
			slScrAutoDisp(bgLayers);
			dispFace = 1;
			deleteSprite(playerNode);
			loadLevel(playfieldPtr); //don't want there to be no level to spawn onto, so we reload it
			lives--;
		}
	break;
	}	
}

static Uint8 handleSpriteCollision(FIXED x, FIXED y)
{
	//NEED TO RETURN 1 AFTER COLLISION!
	//if ball x > sprite x1 and < sprite x2 
	int i;
	for (i = 0; i < MAX_SPRITES; i++) {
		if (sprites[i].state != SPRITE_STATE_NODISP) {
			if (sprites[i].pos[X] - SPR_SIZE[sprites[i].type] < x && sprites[i].pos[X] + SPR_SIZE[sprites[i].type] > x) {
				if (sprites[i].pos[Y] - SPR_SIZE[sprites[i].type] < y && sprites[i].pos[Y] + SPR_SIZE[sprites[i].type] > y) {
					return spriteCollisionBehavior(i, (int)NULL);
				}
			}

		}
	}
	return 0;
}

static Uint8 spriteCollisionBehavior(int index, int collidingIndex) {
	SPRITE_INFO tmp = defaultSprite;
	FIXED* pos;
	//if not provided another sprite index, assume we want to compare to screen pos
	FIXED x = (collidingIndex ? sprites[collidingIndex].pos[X] : screenX);
	FIXED y = (collidingIndex ? sprites[collidingIndex].pos[Y] : screenY);
	switch (sprites[index].type) {
		case TYPE_CIRCLE:
			//add 3 bullets where sprite was, remove sprite, display score
			tmp.attr = &SHOT_ATTR;
			tmp.type = TYPE_SHOT;
			tmp.scratchpad = (collidingIndex ? sprites[collidingIndex].scratchpad + 1 : 0);
			tmp.pos[X] = x;
			tmp.pos[Y] = y;
			pos = setShotVelocity(x, y, sprites[index].pos[X], sprites[index].pos[Y]);
			tmp.dx = pos[0];
			tmp.dy = pos[1];
			addSprite(tmp);		
			tmp.dx = pos[2];
			tmp.dy = pos[3];
			addSprite(tmp);	
			tmp.dx = pos[4];
			tmp.dy = pos[5];
			addSprite(tmp);	
			handleSpriteRemoval(index, POINTS[TYPE_CIRCLE] << tmp.scratchpad);
			return 1;
		break;
		case TYPE_PUSH:
			sprites[index].dx = (sprites[index].pos[X] - x);
			sprites[index].dy = (sprites[index].pos[Y] - y);
			sprites[index].scratchpad = 1; //mark sprite as "pushed" - don't move until it's stopped
			return 1;
		break;
	}
	return 0;
}

static void handleSpriteRemoval(int index, int points) {
	score += points;
	if (score > 999999999) //looks better to wrap around at 1 billion than 4.whatever billion
		score = 0;
	dispNum(points, sprites[index].pos[X], sprites[index].pos[Y]);
	deleteSprite(index);
}

//returns speed that ball should travel at
static FIXED handleGroundCollision(FIXED x, FIXED y) {
	#define BLOCK_THRESHOLD_LOW toFIXED(0.2)
	#define BLOCK_THRESHOLD_HIGH toFIXED(0.8)
	FIXED xDecimal = x & 0x0000ffff;
	FIXED yDecimal = y & 0x0000ffff;
	Uint16 currBlock = MapRead(playfield, fixedToUint16(x), fixedToUint16(y));
	if (currBlock == 0x0000) //if no ground
		return 0;
	if (xDecimal > BLOCK_THRESHOLD_HIGH || xDecimal < BLOCK_THRESHOLD_LOW) { //either less than .2 or greater than .8: trigger block to left and block
		writeBlock(fixedToUint16(x) - 1, fixedToUint16(y), 0x0000);
		writeBlock(fixedToUint16(x), fixedToUint16(y), 0x0000);
		
		if (yDecimal > BLOCK_THRESHOLD_HIGH || yDecimal < BLOCK_THRESHOLD_LOW) { //if y is in same threshold, trigger blocks to top as well
			writeBlock(fixedToUint16(x) - 1, fixedToUint16(y) - 1, 0x0000);
			writeBlock(fixedToUint16(x), fixedToUint16(y) - 1, 0x0000);
		}
	}
	else { 
		writeBlock(fixedToUint16(x), fixedToUint16(y), 0x0000); //otherwise, just trigger block
		if (yDecimal > BLOCK_THRESHOLD_HIGH || yDecimal < BLOCK_THRESHOLD_LOW)
			writeBlock(fixedToUint16(x) - 1, fixedToUint16(y), 0x0000);
	}
	switch (currBlock) {
		case 0x0002: //normal/breakable block
		case 0x0004:
			return toFIXED(-0.25);
		break;
		case 0x0006: //vertical/horizontal rail block
		case 0x0008:
			return toFIXED(-0.18);
		break;
	}
}

//when I have more block types, this should scan them to make sure they're breakable before breaking them
static void writeBlock(Uint16 x, Uint16 y, Uint16 data) {
	if (MapRead(playfield, x, y) == 0x0004)
		MapWrite(playfield, x, y, data);
}

#define FRICTION toFIXED(0.5)
static void updateSprites(void)
{
	int i;
	FIXED dx, dy;
	for (i = 0; i < MAX_SPRITES; i++) {
		if (sprites[i].state != SPRITE_STATE_NODISP) {
			switch(sprites[i].type) {
			case TYPE_NULL:
				break;
			case TYPE_CIRCLE:
				if (sprites[i].state != SPRITE_STATE_FALL) {
					if (MapRead(playfield, fixedToUint16(sprites[i].pos[X] >> 4), fixedToUint16(sprites[i].pos[Y] >> 4)) == 0x0000) {
						sprites[i].state = SPRITE_STATE_FALL;
						break;
					}
					if (slRandom() > toFIXED(0.5))
						sprites[i].ang += 10;
					else
						sprites[i].ang -= 10;
					sprites[i].dx = slCos(DEGtoANG(sprites[i].ang));
					sprites[i].dy = slSin(DEGtoANG(sprites[i].ang));
					sprites[i].pos[X] += sprites[i].dx;
					sprites[i].pos[Y] += sprites[i].dy;
					
					if (MapRead(playfield, fixedToUint16(sprites[i].pos[X] >> 4), fixedToUint16(sprites[i].pos[Y] >> 4)) == 0x0000) {
						sprites[i].pos[X] -= sprites[i].dx;
						sprites[i].pos[Y] -= sprites[i].dy;
						sprites[i].ang += 90;
					}
				}
				else {
					if (sprites[i].pos[S] > toFIXED(0.05)) //scale down sprite until it disappears
						sprites[i].pos[S] -= toFIXED(0.02);
					else {
						handleSpriteRemoval(i, POINTS[TYPE_CIRCLE]);
					}
				}
			break;
			case TYPE_PUSH:
				if (sprites[i].state != SPRITE_STATE_FALL) {
					if (MapRead(playfield, fixedToUint16(sprites[i].pos[X] >> 4), fixedToUint16(sprites[i].pos[Y] >> 4)) == 0x0000) {
						sprites[i].state = SPRITE_STATE_FALL;
						break;
					}
					if (sprites[i].scratchpad == 1) { //if sprite's been pushed by player
						if (sprites[i].dx > 0)
							sprites[i].dx -= FRICTION;
						else if (sprites[i].dx < 0)
							sprites[i].dx += FRICTION;
						if (sprites[i].dy > 0)
							sprites[i].dy -= FRICTION;
						else if (sprites[i].dy < 0)
							sprites[i].dy += FRICTION;
						sprites[i].pos[X] += sprites[i].dx;
						sprites[i].pos[Y] += sprites[i].dy;
						if ((sprites[i].dx > 0 ? sprites[i].dx : -sprites[i].dx) < toFIXED(0.5) && 
							(sprites[i].dy > 0 ? sprites[i].dy : -sprites[i].dy) < toFIXED(0.5)) {
							sprites[i].scratchpad = 0;
						}
					}
					else {
						if (slRandom() > toFIXED(0.5))
							sprites[i].ang += 10;
						else
							sprites[i].ang -= 10;
						sprites[i].dx = slCos(DEGtoANG(sprites[i].ang));
						sprites[i].dy = slSin(DEGtoANG(sprites[i].ang));
						sprites[i].pos[X] += sprites[i].dx;
						sprites[i].pos[Y] += sprites[i].dy;
						
						if (MapRead(playfield, fixedToUint16(sprites[i].pos[X] >> 4), fixedToUint16(sprites[i].pos[Y] >> 4)) == 0x0000) {
							sprites[i].pos[X] -= sprites[i].dx;
							sprites[i].pos[Y] -= sprites[i].dy;
							sprites[i].ang += 90;
						}
					}
				}
				else {
					if (sprites[i].pos[S] > toFIXED(0.05)) //scale down sprite until it disappears
						sprites[i].pos[S] -= toFIXED(0.02);
					else {
						handleSpriteRemoval(i, POINTS[TYPE_PUSH]);
					}
				}				
			break;
			case TYPE_SHOT:
				sprites[i].pos[X] += sprites[i].dx;
				sprites[i].pos[Y] += sprites[i].dy;
				if (checkShotCollision(i))
					deleteSprite(i);
				else if (abs(sprites[i].pos[X] - screenX) > SCREEN_BOUND_R || //remove shot sprite if it goes offscreen
					abs(sprites[i].pos[Y] - screenY) > SCREEN_BOUND_B) {
						deleteSprite(i);
					}
			break;
			#define DIGIT_DISP_FRAMES 60
			case TYPE_DIGIT:
				sprites[i].pos[Y] -= toFIXED(0.5);
				sprites[i].scratchpad++;
				if (sprites[i].scratchpad > DIGIT_DISP_FRAMES)
					deleteSprite(i);
			break;
			}
		}
	}
}

static FIXED* setShotVelocity(FIXED playerX, FIXED playerY, FIXED spriteX, FIXED spriteY) {	
	ANGLE ang1, ang2, ang3;
	static FIXED pos[6];
	ang1 = slAtan(spriteX - playerX, spriteY - playerY);
	ang2 = ang1 + DEGtoANG(15);
	ang3 = ang1 - DEGtoANG(15);
	pos[0] = slMulFX(slCos(ang1), toFIXED(2));
	pos[1] = slMulFX(slSin(ang1), toFIXED(2));
	pos[2] = slMulFX(slCos(ang2), toFIXED(2));
	pos[3] = slMulFX(slSin(ang2), toFIXED(2));
	pos[4] = slMulFX(slCos(ang3), toFIXED(2));
	pos[5] = slMulFX(slSin(ang3), toFIXED(2));
	return pos;
}

static Uint8 checkShotCollision(int index) 
{
	FIXED x = sprites[index].pos[X];
	FIXED y = sprites[index].pos[Y];
	int i;
	for (i = 0; i < MAX_SPRITES; i++) {
		if (sprites[i].state != SPRITE_STATE_NODISP){
			if (sprites[i].pos[X] - (SPR_SIZE[sprites[i].type] >> 1) < x && sprites[i].pos[X] + (SPR_SIZE[sprites[i].type] >> 1) > x) {
				if (sprites[i].pos[Y] - (SPR_SIZE[sprites[i].type] >> 1) < y && sprites[i].pos[Y] + (SPR_SIZE[sprites[i].type] >> 1) > y) {
					return spriteCollisionBehavior(i, index);
				}
			}

		}
	}
	return 0;

}

static void dispSprites(void)
{
	int i;
	FIXED spritePos[XYZS];
	for (i = 0; i < MAX_SPRITES; i++) {
		if (sprites[i].state != SPRITE_STATE_NODISP && (dispFace || sprites[i].type != TYPE_STATIC)) {
			slPrintHex(i, slLocate(0,6));
			if (sprites[i].type == TYPE_STATIC) {
				spritePos[X] = sprites[i].pos[X];
				spritePos[Y] = sprites[i].pos[Y];
				spritePos[Z] = sprites[i].pos[Z];
			}
			else {
				spritePos[X] = slMulFX(sprites[i].pos[X] - screenX, scale);
				spritePos[Y] = slMulFX(sprites[i].pos[Y] - screenY, scale);
				spritePos[Z] = toFIXED(169);
			}
			if (sprites[i].state == SPRITE_STATE_FALL)
				spritePos[S] = slMulFX(sprites[i].pos[S], scale);
			else if (sprites[i].type == TYPE_NULL || sprites[i].type == TYPE_STATIC)
				spritePos[S] = sprites[i].pos[S];
			else
				spritePos[S] = scale;
			slDispSprite(spritePos, sprites[i].attr, DEGtoANG(sprites[i].ang));
		}
	}
}

static void drawPlayField(void)
{
	#define PLAYFIELD_START_INDEX 20
	int i = 0;
	int x, y;
	Uint16 currVal;
	FIXED spritePos[XYZS];
	SPR_ATTR playfieldSprite = SPR_ATTRIBUTE(0,No_Palet,No_Gouraud,CL32KRGB|SPenb|ECdis,sprNoflip);
	for (y = 0; y < 32; y++) {
		for (x = 0; x < 32; x++) {
			currVal = playfield[i++] >> 1; //works with 2x2 SGL maps, this counteracts the 2x2
			if (currVal) { //keep from having a bunch of blank sprites
				spritePos[X] = slMulFX((x << 20) - (screenX - toFIXED(8)), scale); // << 20 = (x * 16) << 16
				spritePos[Y] = slMulFX((y << 20) - (screenY - toFIXED(8)), scale);
				spritePos[Z] = toFIXED(170);
				spritePos[S] = scale;
				playfieldSprite.texno = currVal + PLAYFIELD_START_INDEX;
				slDispSprite(spritePos, &playfieldSprite, DEGtoANG(0));
			}
		}
	}
}

static void initGame(void)
{	
	screenX = startX;
	screenY = startY;
	scale = toFIXED(1.0);
	scaleSpeed = toFIXED(0);
	playerState = PLAYER_STATE_FALLING;
	frames = 0;
	slTVOff();
	set_sprite(pic_sprites, 48, tex_sprites);
	initVDP2();
	slTVOn();
}

void loadLevel(Uint16 map[])
{
	int i;
	playfieldPtr = map;
	for (i = 0; i < 1024; i++) {
		playfield[i] = map[i];
	}
}



void loadSpritePos(Uint16 posArr[], int size)
{
	int i;
	SPRITE_INFO tmp;
	initSprites();
	tmp = defaultSprite;
	for (i = 0; i < size; i++) {
		tmp.pos[X] = (posArr[i] << 16);
		i++;
		tmp.pos[Y] = (posArr[i] << 16);
		i++,
		tmp.type = posArr[i];
		tmp.attr = SPR_ATTRS[posArr[i]];
		addSprite(tmp);
	}
}

void loadPlayerPos(FIXED x, FIXED y)
{
	startX = x;
	startY = y;
}

static int getNumDigits(int num)
{
	if (num < 10)
		return 1;
	else if (num < 100)
		return 2;
	else if (num < 1000)
		return 3;
	else if (num < 10000)
		return 4;
	else if (num < 100000)
		return 5;
	else if (num < 1000000)
		return 6;
	else if (num < 10000000)
		return 7;
	else if (num < 100000000)
		return 8;
	else if (num < 1000000000)
		return 9;
	else
		return 10;
}

//adds a collection of sprites with the passed number's digits to the playfield
#define NUMBER_WIDTH (9 << 16) //width in pixels of each number sprite
static void dispNum(int number, FIXED x, FIXED y)
{
	int i;
	int digits = getNumDigits(number);
	SPRITE_INFO tmp = defaultSprite;
	tmp.type = TYPE_DIGIT;
	tmp.pos[X] = x;
	tmp.pos[Y] = y;
	while (digits--) {
		tmp.attr = DIGITS[number % 10];
		number /= 10;
		addSprite(tmp);
		tmp.pos[X] -= NUMBER_WIDTH;
	}
}

static inline void updateHud(void)
{
	dispLives();
	dispScore();
}

static void dispLives(void)
{
	int i;
	FIXED lifePos[XYZS];
	lifePos[X] = toFIXED(60);
	lifePos[Y] = toFIXED(-105);
	lifePos[Z] = toFIXED(160);
	lifePos[S] = toFIXED(1);
	for (i = 0; i < lives; i++) {
		slDispSprite(lifePos, &LIFE_ATTR, DEGtoANG(0));
		lifePos[X] += toFIXED(9);
	}
}
static void dispScore(void)
{
	
	int i;
	int tempScore = score;
	SPR_ATTR digitSprite = SPR_ATTRIBUTE(0,No_Palet,No_Gouraud,CL32KRGB|SPenb|ECdis,sprNoflip);
	FIXED digitPos[XYZS];
	digitPos[X] = toFIXED(-60);
	digitPos[Y] = toFIXED(-105);
	digitPos[Z] = toFIXED(160);
	digitPos[S] = toFIXED(1);
	for (i = 0; i < 9; i++) {
		digitSprite.texno = (tempScore % 10) + TYPE_DIGIT;
		tempScore /= 10;
		digitPos[X] -= NUMBER_WIDTH;
		slDispSprite(digitPos, &digitSprite, DEGtoANG(0));
	}
}

static int dispGameOver()
{
	SPRITE_INFO gameOver;
	static int letterIndex = 0;
	int currentIndex; //index of the sprite being animated
	if (letterIndex == 9) {
		if (frames) //has the function been run before in this function?
			return 1; //yes- return
		else
			letterIndex = 0; //otherwise reinit letterIndex, continue execution
	}
	if (!frames) { //do init first time function's run
		gameOver = defaultSprite;
		gameOver.type = TYPE_NULL;
		gameOver.attr = GAMEOVER[letterIndex++];
		gameOver.pos[X] = screenX - toFIXED(25);
		gameOver.pos[Y] = screenY;
		gameOver.pos[Z] = toFIXED(160);
		gameOver.pos[S] = toFIXED(2);
		currentIndex = addSprite(gameOver);
		frames++;
	}
	else if (frames == 1 && letterIndex) { //don't do init when you're adding letters after init
		gameOver.attr = GAMEOVER[letterIndex++];
		currentIndex = addSprite(gameOver);
		frames++;
	}
	else {
		sprites[currentIndex].pos[X] += toFIXED(1);
		frames++;
		if (frames > 8) {
			frames = 1;
			gameOver.pos[X] = sprites[currentIndex].pos[X];
			if (letterIndex == 8) //stop from adding another sprite after last letter animates
				letterIndex = 9;
		}
	}
	return 0;
}

int runLevel(void)
{
	int player; //player sprite for animate up
	gameState = GAME_STATE_START;
	#define NORMAL_COL_RATIO 0x0f
	Uint8 colorRatio = 0x00;
	int i;
	initGame();
	dispFace = 0;
	SPRITE_INFO tmp = defaultSprite;
	tmp.attr = &PLAYER_ATTR;
	tmp.type = TYPE_NULL;
	tmp.pos[X] = screenX;
	tmp.pos[Y] = screenY;
	tmp.pos[S] = toFIXED(6.0);
	player = addSprite(tmp);
	while (1) {
		switch (gameState) {
			case GAME_STATE_START:
				updateBG();
				dispSprites();
				drawPlayField();
				updateHud();
				slSynch();
				if (sprites[player].pos[S] > toFIXED(1.0)) {
					sprites[player].pos[S] -= toFIXED(0.1);
				}
				else {
					deleteSprite(player);
					bgLayers |= NBG3ON;
					slScrAutoDisp(bgLayers);
					dispFace = 1;
					gameState = GAME_STATE_FADEIN;
				}
			break;
			case GAME_STATE_FADEIN:
				if (colorRatio < NORMAL_COL_RATIO) {
					colorRatio++;
					slColRateNbg3(colorRatio);
					updateBG();
					updateSprites();
					dispSprites();
					drawPlayField();
					updateHud();
					slSynch();
				}
				else
					gameState = GAME_STATE_NORMAL;
			break;
			case GAME_STATE_NORMAL:
				handleInput();
				updateBG();
				updateSprites();
				dispSprites();
				drawPlayField();
				updateHud();
				slPrintHex(numSprites, slLocate(0,7));
				slSynch();
				if (numSprites <= NUM_PLAYER_SPRITES) {
					gameState = GAME_STATE_FADEOUT;
				}
			break;
			case GAME_STATE_FADEOUT:
				if (colorRatio > 0) {
					colorRatio--;
					slColRateNbg3(colorRatio);
					updateBG();
					dispSprites();
					drawPlayField();
					updateHud();
					slSynch();
				}
				else {
					bgLayers &= ~NBG3ON;
					slScrAutoDisp(bgLayers);
					dispFace = 0;
					SPRITE_INFO tmp = defaultSprite;
					tmp.attr = &PLAYER_ATTR;
					tmp.type = TYPE_NULL;
					tmp.pos[X] = screenX;
					tmp.pos[Y] = screenY;
					tmp.pos[S] = toFIXED(1.0);
					player = addSprite(tmp);
					gameState = GAME_STATE_COMPLETE;
				}
			break;
			case GAME_STATE_COMPLETE:
				dispSprites();
				updateBG();
				drawPlayField();
				updateHud();
				slSynch();
				if (sprites[player].pos[S] < toFIXED(6.0)) {
					sprites[player].pos[S] += toFIXED(0.1);
					sprites[player].ang += 10;
				}
				else {
					clearSpriteList();
					return 1;
				}
			break;
			case GAME_STATE_CURSOR:
				handleInput();
				dispSprites();
				updateBG();
				drawPlayField();
				slPrint("X:", slLocate(0,4));
				slPrintFX(screenX, slLocate(3,4));
				slPrint("Y:", slLocate(0,5));
				slPrintFX(screenY, slLocate(3,5));
				slSynch();
			break;
			case GAME_STATE_GAMEOVER:
				updateBG();
				updateSprites();
				dispSprites();
				drawPlayField();
				updateHud();
				slSynch();
				if (dispGameOver()) //wait 2 seconds after game over animation finishes
					frames++;
				if (frames > 120) {
					clearSpriteList();
					return 0;
				}
		}
	}
}

