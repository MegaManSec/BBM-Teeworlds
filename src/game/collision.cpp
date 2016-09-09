/* copyright (c) 2007 magnus auvinen, see licence.txt for more info */
#include <base/system.h>
#include <base/math.hpp>
#include <base/vmath.hpp>

#include <math.h>
#include <engine/e_common_interface.h>
#include <game/mapitems.hpp>
#include <game/layers.hpp>
#include <game/collision.hpp>

static TILE *tiles;
static int width = 0;
static int height = 0;

int col_width() { return width; }
int col_height() { return height; }

int *dc;
int **dest;


int col_init()
{
	width = layers_game_layer()->width;
	height = layers_game_layer()->height;
	tiles = (TILE *)map_get_data(layers_game_layer()->data);
	dbg_msg("collision","start loop (w: %i, h: %i",width,height);
	int tpnum=(TILE_TPORT_LAST-TILE_TPORT_FIRST+1)>>1;
	int *destcount=(int*)malloc(sizeof(int)*tpnum);
	dc=(int*)malloc(sizeof(int)*tpnum);
	dest=(int**)malloc(sizeof(int*)*tpnum);
	for(int z=0;z<tpnum;++z) destcount[z]=dc[z]=0;

	for(int i = 0; i < width*height; i++) //tport first
	{
		int index = tiles[i].index;
		if(index >= TILE_TPORT_FIRST && index <= TILE_TPORT_LAST && !(index&1)) {
			int tind = ((index-TILE_TPORT_FIRST) >> 1);
			destcount[tind]++;dc[tind]++;
		}
	}
	for(int z=0;z<tpnum;++z) {
		if (destcount[z]) {
			dest[z]=(int*)malloc(sizeof(int)*destcount[z]);
		}

	}

	for(int i = 0; i < width*height; i++)
	{
		int index = tiles[i].index;
		if(index >= TILE_CUSTOM_END) {
			continue;
		}
		
		if(index == TILE_DEATH)
			tiles[i].index = COLFLAG_DEATH;
		else if(index == TILE_SOLID)
			tiles[i].index = COLFLAG_SOLID;
		else if(index == TILE_NOHOOK)
			tiles[i].index = COLFLAG_SOLID|COLFLAG_NOHOOK;
		else if(index >= TILE_TPORT_FIRST && index <= TILE_TPORT_LAST && !(index&1)) {
			int tind = ((index-TILE_TPORT_FIRST) >> 1);
			dest[tind][--destcount[tind]]=i;
//			dbg_msg("coll","dest[%i][%i]=%i",tind,destcount[tind],i);
		}
		else if (index >= TILE_CUSTOM_END)
			tiles[i].index = 0;

//		if (index != tiles[i].index)
//			dbg_msg("collision","new index: %i",tiles[i].index);
	}
	free(destcount);
	dbg_msg("collision","end of loop");
				
	return 1;
}



int col_get(int x, int y)
{
	int nx = clamp(x/32, 0, width-1);
	int ny = clamp(y/32, 0, height-1);
	if (tiles[ny*width+nx].index)
//	dbg_msg("coll","col_get(%i,%i) -> nxy: (%i,%i) ---> index: %i =====> %i",x,y,nx,ny,ny*width+nx,(tiles[ny*width+nx].index > 128)?0:(tiles[ny*width+nx].index));

	if(tiles[ny*width+nx].index >= TILE_CUSTOM_END)
		return 0;
	return tiles[ny*width+nx].index;
}

int col_is_solid(int x, int y)
{
	int i = col_get(x,y);
	return (i<=5) && (i&COLFLAG_SOLID);
}

// TODO: rewrite this smarter!
int col_intersect_line(vec2 pos0, vec2 pos1, vec2 *out_collision, vec2 *out_before_collision)
{
	float d = distance(pos0, pos1);
	vec2 last = pos0;
	
	for(float f = 0; f < d; f++)
	{
		float a = f/d;
		vec2 pos = mix(pos0, pos1, a);
		if(col_is_solid(round(pos.x), round(pos.y)))
		{
			if(out_collision)
				*out_collision = pos;
			if(out_before_collision)
				*out_before_collision = last;
			return col_get(round(pos.x), round(pos.y));
		}
		last = pos;
	}
	if(out_collision)
		*out_collision = pos1;
	if(out_before_collision)
		*out_before_collision = pos1;
	return 0;
}

/* -------- bbmod ---------*/
//int col_get_a(int x, int y)
//{
//	int nx = clamp(x/32, 0, width-1);
//	int ny = clamp(y/32, 0, height-1);
//	if (tiles[ny*width+nx].index)
//	dbg_msg("coll","geta(%i,%i)-->%i (arrayindex %i) ",nx,ny,tiles[ny*width+nx].index,ny*width+nx);
//	return tiles[ny*width+nx].index;
//}
int col_is_freeze(int x, int y)
{
	return (col_get(x,y))==TILE_FREEZE;
}
int col_is_unfreeze(int x, int y)
{
	return (col_get(x,y))==TILE_UNFREEZE;
}
int col_is_kick(int x, int y)
{
	return (col_get(x,y))==TILE_KICK;
}
int col_is_colfrz(int x, int y)
{
	int i = col_get(x,y);
	return (i>=TILE_CFREEZE_GREEN && i <= TILE_CFREEZE_RESET)?i:0;
}
int col_is_pwrup(int x, int y)
{
	int i = col_get(x,y);
	return (i>=TILE_PUP_JUMP && i <= TILE_PUP_RESET)?i:0;
}
int col_is_1on1(int x, int y)
{
	return (col_get(x,y))==TILE_1ON1TOGGLE;
}
int col_is_perfrzon(int x, int y)
{
	return (col_get(x,y))==TILE_PERFRZON;
}
int col_is_perfrzoff(int x, int y)
{
	return (col_get(x,y))==TILE_PERFRZOFF;
}
int col_is_teleport(int x, int y)
{
	int i = col_get(x,y);
	return (i>=TILE_TPORT_FIRST && i <= TILE_TPORT_LAST)?i:0;
}
int col_is_loadslot(int x, int y)

{
	int i = col_get(x,y);
	return (i==TILE_LOADSLOT1 || i==TILE_LOADSLOT2)?i:0;
}
int col_is_saveslot(int x, int y)
{
	int i = col_get(x,y);
	return (i==TILE_SAVESLOT1 || i==TILE_SAVESLOT2)?i:0;
}

vec2 get_teleport(int tind)
{
	if (dc[tind]) {
		int r = rand() % dc[tind];
		int x = (dest[tind][r] % width) << 5;
		int y = (dest[tind][r] / width) << 5;
		return vec2((float)x + 16.0, (float)y + 16.0);
	} else return vec2(0, 0);
}


/* -------- eobbmod ---------*/
