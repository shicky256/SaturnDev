#ifndef SPRITELIST_H
#define SPRITELIST_H

typedef struct SpriteInfo {
	FIXED pos[XYZS];
	Uint16 ang;
	SPR_ATTR* attr;
	FIXED dx, dy;
	Uint8 type;
	Uint8 state;
} SPRITE_INFO;

#define TYPE_NULL 0
#define TYPE_CIRCLE 1
#define TYPE_SHOT 2
#define TYPE_FACE 3 //what makes up char's face
#define TYPE_PUSH 4

#define SPRITE_STATE_NODISP 0
#define SPRITE_STATE_NORM 1
#define SPRITE_STATE_FALL 2

#define fixedToUint16(fixed) (fixed >> 16)
int addSprite(SPRITE_INFO data);
void deleteSprite(int index);
void clearSpriteList(void);

#endif
