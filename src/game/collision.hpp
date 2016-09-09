/* copyright (c) 2007 magnus auvinen, see licence.txt for more info */
#ifndef GAME_MAPRES_COL_H
#define GAME_MAPRES_COL_H

#include <base/vmath.hpp>

enum
{
	COLFLAG_SOLID=1,
	COLFLAG_DEATH=2,
	COLFLAG_NOHOOK=4,
};

int col_init();
int col_is_solid(int x, int y);
int col_get(int x, int y);
int col_width();
int col_height();
int col_intersect_line(vec2 pos0, vec2 pos1, vec2 *out_collision, vec2 *out_before_collision);


/* ------bbmod---------*/
int col_is_freeze(int x, int y);
int col_is_unfreeze(int x, int y);
int col_is_kick(int x, int y);
int col_is_colfrz(int x, int y);
int col_is_pwrup(int x, int y);
int col_is_1on1(int x, int y);
int col_is_perfrzon(int x, int y);
int col_is_perfrzoff(int x, int y);
int col_is_loadslot(int x, int y);
int col_is_saveslot(int x, int y);
int col_is_teleport(int x, int y);
vec2 get_teleport(int tind);
/* ------eobbmod---------*/
#endif
