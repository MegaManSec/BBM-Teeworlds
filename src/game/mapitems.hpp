/* copyright (c) 2007 magnus auvinen, see licence.txt for more info */
#ifndef GAME_MAPITEMS_H
#define GAME_MAPITEMS_H

// layer types
enum
{
	LAYERTYPE_INVALID=0,
	LAYERTYPE_GAME, // not used
	LAYERTYPE_TILES,
	LAYERTYPE_QUADS,
	
	MAPITEMTYPE_VERSION=0,
	MAPITEMTYPE_INFO,
	MAPITEMTYPE_IMAGE,
	MAPITEMTYPE_ENVELOPE,
	MAPITEMTYPE_GROUP,
	MAPITEMTYPE_LAYER,
	MAPITEMTYPE_ENVPOINTS,
	

	CURVETYPE_STEP=0,
	CURVETYPE_LINEAR,
	CURVETYPE_SLOW,
	CURVETYPE_FAST,
	CURVETYPE_SMOOTH,
	NUM_CURVETYPES,
	
	// game layer tiles
	ENTITY_NULL=0,
	ENTITY_SPAWN,
	ENTITY_SPAWN_RED,
	ENTITY_SPAWN_BLUE,
	ENTITY_FLAGSTAND_RED,
	ENTITY_FLAGSTAND_BLUE,
	ENTITY_ARMOR_1,
	ENTITY_HEALTH_1,
	ENTITY_WEAPON_SHOTGUN,
	ENTITY_WEAPON_GRENADE,
	ENTITY_POWERUP_NINJA,
	ENTITY_WEAPON_RIFLE,
	NUM_ENTITIES,
	
	TILE_AIR=0,
	TILE_SOLID,
	TILE_DEATH,
	TILE_NOHOOK,
	
	TILE_ACCEL_L,
	TILE_ACCEL_R,
	TILE_ACCEL_D,
	TILE_ACCEL_U,

	TILE_UNUSED,

	TILE_FREEZE,
	TILE_KICK,
	TILE_UNFREEZE,

	TILE_CFREEZE_GREEN,
	TILE_CFREEZE_BLUE,
	TILE_CFREEZE_RED,
	TILE_CFREEZE_WHITE,
	TILE_CFREEZE_GREY,
	TILE_CFREEZE_YELLOW,
	TILE_CFREEZE_PINK,
	TILE_CFREEZE_RESET,

	TILE_PUP_JUMP,
	TILE_PUP_HAMMER,
	TILE_PUP_LFREEZE,
	TILE_PUP_SFREEZE,
	TILE_PUP_HOOKDUR,
	TILE_PUP_HOOKLEN,
	TILE_PUP_WALKSPD,
	TILE_PUP_EPICNINJA,
	TILE_PUP_TPORT,
	TILE_PUP_MAGNET,
	TILE_PUP_KOD,
	TILE_PUP_LASER,

	TILE_PUP_RES01,
	TILE_PUP_RES02,
	TILE_PUP_RES03,
	TILE_PUP_RES04,
	TILE_PUP_RES05,
	TILE_PUP_RES06,
	TILE_PUP_RES07,
	TILE_PUP_RES08,
	TILE_PUP_RES09,
	TILE_PUP_RES10,
	TILE_PUP_RES11,
	TILE_PUP_RES12,
	TILE_PUP_RES13,
	TILE_PUP_RES14,
	TILE_PUP_RES15,
	TILE_PUP_RES16,

	TILE_PUP_RESET,


	TILE_1ON1TOGGLE,
	TILE_PERFRZOFF,
	TILE_PERFRZON,
	TILE_LOADSLOT1,
	TILE_LOADSLOT2,
	TILE_SAVESLOT1,
	TILE_SAVESLOT2,

	TILE_TPORT_FIRST = 112,
	TILE_TPORT_LAST = 191,
	TILE_CUSTOM_END,

	TILEFLAG_VFLIP=1,
	TILEFLAG_HFLIP=2,
	TILEFLAG_OPAQUE=4,
	
	LAYERFLAG_DETAIL=1,
	
	ENTITY_OFFSET=255-16*4,
};

typedef struct
{
	int x, y; // 22.10 fixed point
} POINT;

typedef struct
{
	int r, g, b, a;
} COLOR;

typedef struct
{
	POINT points[5];
	COLOR colors[4];
	POINT texcoords[4];
	
	int pos_env;
	int pos_env_offset;
	
	int color_env;
	int color_env_offset;
} QUAD;

typedef struct
{
	unsigned char index;
	unsigned char flags;
	unsigned char skip;
	unsigned char reserved;
} TILE;

typedef struct 
{
	int version;
	int width;
	int height;
	int external;
	int image_name;
	int image_data;
} MAPITEM_IMAGE;

struct MAPITEM_GROUP_v1
{
	int version;
	int offset_x;
	int offset_y;
	int parallax_x;
	int parallax_y;

	int start_layer;
	int num_layers;
} ;


struct MAPITEM_GROUP : public MAPITEM_GROUP_v1
{
	enum { CURRENT_VERSION=2 };
	
	int use_clipping;
	int clip_x;
	int clip_y;
	int clip_w;
	int clip_h;
} ;

typedef struct
{
	int version;
	int type;
	int flags;
} MAPITEM_LAYER;

typedef struct
{
	MAPITEM_LAYER layer;
	int version;
	
	int width;
	int height;
	int flags;
	
	COLOR color;
	int color_env;
	int color_env_offset;
	
	int image;
	int data;
} MAPITEM_LAYER_TILEMAP;

typedef struct
{
	MAPITEM_LAYER layer;
	int version;
	
	int num_quads;
	int data;
	int image;
} MAPITEM_LAYER_QUADS;

typedef struct
{
	int version;
} MAPITEM_VERSION;

typedef struct
{
	int time; // in ms
	int curvetype;
	int values[4]; // 1-4 depending on envelope (22.10 fixed point)
} ENVPOINT;

typedef struct
{
	int version;
	int channels;
	int start_point;
	int num_points;
	int name;
} MAPITEM_ENVELOPE;

#endif
