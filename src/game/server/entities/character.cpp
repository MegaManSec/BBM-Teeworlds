#include <new>
#include <cstring>
#include <cstdio>
#include <engine/e_server_interface.h>
#include <engine/e_config.h>
#include <game/server/gamecontext.hpp>
#include <game/mapitems.hpp>

#include "character.hpp"
#include "laser.hpp"
#include "projectile.hpp"

struct INPUT_COUNT
{
	int presses;
	int releases;
};

static INPUT_COUNT count_input(int prev, int cur)
{
	INPUT_COUNT c = {0,0};
	prev &= INPUT_STATE_MASK;
	cur &= INPUT_STATE_MASK;
	int i = prev;
	while(i != cur)
	{
		i = (i+1)&INPUT_STATE_MASK;
		if(i&1)
			c.presses++;
		else
			c.releases++;
	}

	return c;
}
/*----bbmod-----*/
char bbuf[512];
/*----eobbmod-----*/


MACRO_ALLOC_POOL_ID_IMPL(CHARACTER, MAX_CLIENTS)

// player
CHARACTER::CHARACTER()
: ENTITY(NETOBJTYPE_CHARACTER)
{
	proximity_radius = phys_size;
}

void CHARACTER::reset()
{
	destroy();
}

bool CHARACTER::spawn(PLAYER *player, vec2 pos, int team)
{
	player_state = PLAYERSTATE_UNKNOWN;
	emote_stop = -1;
	last_action = -1;
	active_weapon = WEAPON_GUN;
	last_weapon = WEAPON_HAMMER;
	queued_weapon = -1;
	
	//clear();
	this->player = player;
	this->pos = pos;
	this->team = team;
	
	core.reset();
	core.world = &game.world.core;
	core.pos = pos;
	game.world.core.characters[player->client_id] = &core;

	reckoning_tick = 0;
	mem_zero(&sendcore, sizeof(sendcore));
	mem_zero(&reckoningcore, sizeof(reckoningcore));
	
	game.world.insert_entity(this);
	alive = true;
	player->force_balanced = false;
	core.skills=player->skills;
	if (player->is1on1) {
		int *sl = player->slot3;
		if (sl)
		{
			for (int z = 0; z < NUM_PUPS; ++z)
				player->skills[z] = sl[z];
		}
		server_setclientname(player->client_id, player->oname);
		free(player->oname);
		player->oname = NULL;
		player->is1on1=0;
	}
	player->isperfrz=0;
	lastperfrz = 0;
	lastepicninja=0;
	epicninjaannounced=0;
	game.controller->on_character_spawn(this);

	return true;
}

void CHARACTER::destroy()
{
	game.world.core.characters[player->client_id] = 0;
	alive = false;
}

void CHARACTER::set_weapon(int w)
{
	if(w == active_weapon)
		return;
		
	last_weapon = active_weapon;
	queued_weapon = -1;
	active_weapon = w;
	if(active_weapon < 0 || active_weapon >= NUM_WEAPONS)
		active_weapon = 0;
	
	game.create_sound(pos, SOUND_WEAPON_SWITCH);
}

bool CHARACTER::is_grounded()
{
	if(col_check_point((int)(pos.x+phys_size/2), (int)(pos.y+phys_size/2+5)))
		return true;
	if(col_check_point((int)(pos.x-phys_size/2), (int)(pos.y+phys_size/2+5)))
		return true;
	return false;
}


int CHARACTER::handle_ninja()
{
	if(active_weapon != WEAPON_NINJA)
		return 0;
	
	vec2 direction = normalize(vec2(latest_input.target_x, latest_input.target_y));

	if ((server_tick() - ninja.activationtick) > (data->weapons.ninja.duration * server_tickspeed() / 1000))
	{
		// time's up, return
		weapons[WEAPON_NINJA].got = false;
		active_weapon = last_weapon;
		if(active_weapon == WEAPON_NINJA)
			active_weapon = WEAPON_GUN;
		set_weapon(active_weapon);
		return 0;
	}
	
	// force ninja weapon
	set_weapon(WEAPON_NINJA);

	ninja.currentmovetime--;

	if (ninja.currentmovetime == 0)
	{
		// reset player velocity
//		core.vel *= 0.2f;
		//return MODIFIER_RETURNFLAGS_OVERRIDEWEAPON;
		core.vel.x=0.0;
		core.vel.y=0.0;
		core.pos=epicninjaoldpos;
	}

	if (ninja.currentmovetime > 0)
	{
		// Set player velocity
		core.vel = ninja.activationdir * data->weapons.ninja.velocity;
		vec2 oldpos = pos;
		move_box(&core.pos, &core.vel, vec2(phys_size, phys_size), 0.0f);
		// reset velocity so the client doesn't predict stuff
		core.vel = vec2(0.0f,0.0f);
		if ((ninja.currentmovetime % 2) == 0)
		{
			//create_smoke(pos);
		}

		// check if we hit anything along the way
		{
			CHARACTER *ents[64];
			vec2 dir = pos - oldpos;
			float radius = phys_size * 2.0f; //length(dir * 0.5f);
			vec2 center = oldpos + dir * 0.5f;
			int num = game.world.find_entities(center, radius, (ENTITY**)ents, 64, NETOBJTYPE_CHARACTER);

			for (int i = 0; i < num; i++)
			{
				// Check if entity is a player
				if (ents[i] == this)
					continue;
				// make sure we haven't hit this object before
				bool balreadyhit = false;
				for (int j = 0; j < numobjectshit; j++)
				{
					if (hitobjects[j] == ents[i])
						balreadyhit = true;
				}
				if (balreadyhit)
					continue;

				// check so we are sufficiently close
				if (distance(ents[i]->pos, pos) > (phys_size * 2.0f))
					continue;

				// hit a player, give him damage and stuffs...
				game.create_sound(ents[i]->pos, SOUND_NINJA_HIT);
				// set his velocity to fast upward (for now)
				if(numobjectshit < 10)
					hitobjects[numobjectshit++] = ents[i];
					
				ents[i]->take_damage(vec2(0,10.0f), 0, player->client_id,WEAPON_NINJA);
			}
		}
		return 0;
	}

	return 0;
}


void CHARACTER::do_weaponswitch()
{
	if(reload_timer != 0) // make sure we have reloaded
		return;
		
	if(queued_weapon == -1) // check for a queued weapon
		return;

	if(weapons[WEAPON_NINJA].got) // if we have ninja, no weapon selection is possible
		return;

	// switch weapon
	set_weapon(queued_weapon);
}

void CHARACTER::handle_weaponswitch()
{
	int wanted_weapon = active_weapon;
	if(queued_weapon != -1)
		wanted_weapon = queued_weapon;
	
	// select weapon
	int next = count_input(latest_previnput.next_weapon, latest_input.next_weapon).presses;
	int prev = count_input(latest_previnput.prev_weapon, latest_input.prev_weapon).presses;

	if(next < 128) // make sure we only try sane stuff
	{
		while(next) // next weapon selection
		{
			wanted_weapon = (wanted_weapon+1)%NUM_WEAPONS;
			if(weapons[wanted_weapon].got)
				next--;
		}
	}

	if(prev < 128) // make sure we only try sane stuff
	{
		while(prev) // prev weapon selection
		{
			wanted_weapon = (wanted_weapon-1)<0?NUM_WEAPONS-1:wanted_weapon-1;
			if(weapons[wanted_weapon].got)
				prev--;
		}
	}

	// direct weapon selection
	if(latest_input.wanted_weapon)
		wanted_weapon = input.wanted_weapon-1;

	// check for insane values
	if(wanted_weapon >= 0 && wanted_weapon < NUM_WEAPONS && wanted_weapon != active_weapon && weapons[wanted_weapon].got)
		queued_weapon = wanted_weapon;
	
	do_weaponswitch();
}

void CHARACTER::fire_weapon()
{
	if(reload_timer != 0)
		return;
		
	do_weaponswitch();
	
	vec2 direction = normalize(vec2(latest_input.target_x, latest_input.target_y));
	
	bool fullauto = false;
	if(active_weapon == WEAPON_GRENADE || active_weapon == WEAPON_SHOTGUN || active_weapon == WEAPON_RIFLE)
		fullauto = true;


	// check if we gonna fire
	bool will_fire = false;
	if(count_input(latest_previnput.fire, latest_input.fire).presses) will_fire = true;
	if(fullauto && (latest_input.fire&1) && weapons[active_weapon].ammo) will_fire = true;
	if(!will_fire)
		return;
		
	// check for ammo
	if(!weapons[active_weapon].ammo)
	{
		// 125ms is a magical limit of how fast a human can click
		reload_timer = 125 * server_tickspeed() / 1000;;
		game.create_sound(pos, SOUND_WEAPON_NOAMMO);
		return;
	}
	
	vec2 projectile_startpos = pos+direction*phys_size*0.75f;
	
	switch(active_weapon)
	{
		case WEAPON_HAMMER:
		{
			// reset objects hit
			numobjectshit = 0;
			game.create_sound(pos, SOUND_HAMMER_FIRE);
			
			CHARACTER *ents[64];
			int hits = 0;
			int num = game.world.find_entities(pos+direction*phys_size*0.75f, phys_size*0.5f, (ENTITY**)ents, 64, NETOBJTYPE_CHARACTER);

			for (int i = 0; i < num; i++)
			{
				CHARACTER *target = ents[i];
				if (target == this)
					continue;
					
				// hit a player, give him damage and stuffs...
				vec2 fdir = normalize(ents[i]->pos - pos);

				// set his velocity to fast upward (for now)
				game.create_hammerhit(pos);
//				ents[i]->take_damage(vec2(0,-1.0f), data->weapons.hammer.base->damage, player->client_id, active_weapon);
				ents[i]->take_damage(vec2(0,-1.0f), 0, player->client_id, active_weapon);
				ents[i]->lasthammeredat = server_tick();
				ents[i]->lasthammeredby = player->client_id;
				ents[i]->freeze(3*server_tickspeed());
				vec2 dir;
				if (length(target->pos - pos) > 0.0f)
					dir = normalize(target->pos - pos);
				else
					dir = vec2(0,-1);
					
				target->core.vel += normalize(dir + vec2(0, -1.1f)) * (10.0f + (player->skills[PUP_HAMMER] * 3));
				target->unfreeze();
				hits++;
			}
			
			// if we hit anything, we have to wait for the reload
			if(hits)
				reload_timer = server_tickspeed()/3;
			
		} break;

		case WEAPON_GUN:
		{
			PROJECTILE *proj = new PROJECTILE(WEAPON_GUN,
				player->client_id,
				projectile_startpos,
				direction,
				(int)(server_tickspeed()*tuning.gun_lifetime),
				0, 0, 0, -1, WEAPON_GUN);
				
			// pack the projectile and send it to the client directly
			NETOBJ_PROJECTILE p;
			proj->fill_info(&p);
			
			msg_pack_start(NETMSGTYPE_SV_EXTRAPROJECTILE, 0);
			msg_pack_int(1);
			for(unsigned i = 0; i < sizeof(NETOBJ_PROJECTILE)/sizeof(int); i++)
				msg_pack_int(((int *)&p)[i]);
			msg_pack_end();
			server_send_msg(player->client_id);
							
			game.create_sound(pos, SOUND_GUN_FIRE);
		} break;
		
		case WEAPON_SHOTGUN:
		{
			int shotspread = 2;

			msg_pack_start(NETMSGTYPE_SV_EXTRAPROJECTILE, 0);
			msg_pack_int(shotspread*2+1);
			
			for(int i = -shotspread; i <= shotspread; i++)
			{
				float spreading[] = {-0.185f, -0.070f, 0, 0.070f, 0.185f};
				float a = get_angle(direction);
				a += spreading[i+2];
				float v = 1-(abs(i)/(float)shotspread);
				float speed = mix((float)tuning.shotgun_speeddiff, 1.0f, v);
				PROJECTILE *proj = new PROJECTILE(WEAPON_SHOTGUN,
					player->client_id,
					projectile_startpos,
					vec2(cosf(a), sinf(a))*speed,
					(int)(server_tickspeed()*tuning.shotgun_lifetime),
					1, 0, 0, -1, WEAPON_SHOTGUN);
					
				// pack the projectile and send it to the client directly
				NETOBJ_PROJECTILE p;
				proj->fill_info(&p);
				
				for(unsigned i = 0; i < sizeof(NETOBJ_PROJECTILE)/sizeof(int); i++)
					msg_pack_int(((int *)&p)[i]);
			}

			msg_pack_end();
			server_send_msg(player->client_id);					
			
			game.create_sound(pos, SOUND_SHOTGUN_FIRE);
		} break;

		case WEAPON_GRENADE:
		{
			PROJECTILE *proj = new PROJECTILE(WEAPON_GRENADE,
				player->client_id,
				projectile_startpos,
				direction,
				(int)(server_tickspeed()*tuning.grenade_lifetime),
				1, PROJECTILE::PROJECTILE_FLAGS_EXPLODE, 0, SOUND_GRENADE_EXPLODE, WEAPON_GRENADE);

			// pack the projectile and send it to the client directly
			NETOBJ_PROJECTILE p;
			proj->fill_info(&p);
			
			msg_pack_start(NETMSGTYPE_SV_EXTRAPROJECTILE, 0);
			msg_pack_int(1);
			for(unsigned i = 0; i < sizeof(NETOBJ_PROJECTILE)/sizeof(int); i++)
				msg_pack_int(((int *)&p)[i]);
			msg_pack_end();
			server_send_msg(player->client_id);

			game.create_sound(pos, SOUND_GRENADE_FIRE);
		} break;
		
		case WEAPON_RIFLE:
		{
			new LASER(pos, direction, tuning.laser_reach, player->client_id);
			game.create_sound(pos, SOUND_RIFLE_FIRE);
		} break;
		
		case WEAPON_NINJA:
		{
			if (player->skills[PUP_EPICNINJA]) {
				if ((lastepicninja + 10 * server_tickspeed()) < server_tick()) {
					lastepicninja=server_tick();
					epicninjaoldpos=pos;
					epicninjaannounced=0;
				} else {
					str_format(bbuf, 128, "epic ninja not yet ready.");
					game.send_chat_target(player->client_id, bbuf);
					return;
				}
			} else {
				return;
			}
			attack_tick = server_tick();
			ninja.activationdir = direction;
			ninja.currentmovetime = data->weapons.ninja.movetime * server_tickspeed() / 1000;

			//reload_timer = data->weapons.ninja.base->firedelay * server_tickspeed() / 1000 + server_tick();

			// reset hit objects
			numobjectshit = 0;

			game.create_sound(pos, SOUND_NINJA_FIRE);
			
		} break;
		
	}

	if(weapons[active_weapon].ammo > 0) // -1 == unlimited
		weapons[active_weapon].ammo--;
	attack_tick = server_tick();
	if(!reload_timer)
		reload_timer = data->weapons.id[active_weapon].firedelay * server_tickspeed() / 1000;
}

int CHARACTER::handle_weapons()
{
	vec2 direction = normalize(vec2(latest_input.target_x, latest_input.target_y));

	/*
	if(config.dbg_stress)
	{
		for(int i = 0; i < NUM_WEAPONS; i++)
		{
			weapons[i].got = true;
			weapons[i].ammo = 10;
		}

		if(reload_timer) // twice as fast reload
			reload_timer--;
	} */

	//if(active_weapon == WEAPON_NINJA)
	handle_ninja();


	// check reload timer
	if(reload_timer)
	{
		reload_timer--;
		return 0;
	}
	
	/*
	if (active_weapon == WEAPON_NINJA)
	{
		// don't update other weapons while ninja is active
		return handle_ninja();
	}*/

	// fire weapon, if wanted
	fire_weapon();

	// ammo regen
	int ammoregentime = data->weapons.id[active_weapon].ammoregentime;
	if(ammoregentime)
	{
		// If equipped and not active, regen ammo?
		if (reload_timer <= 0)
		{
			if (weapons[active_weapon].ammoregenstart < 0)
				weapons[active_weapon].ammoregenstart = server_tick();

			if ((server_tick() - weapons[active_weapon].ammoregenstart) >= ammoregentime * server_tickspeed() / 1000)
			{
				// Add some ammo
				weapons[active_weapon].ammo = min(weapons[active_weapon].ammo + 1, 10);
				weapons[active_weapon].ammoregenstart = -1;
			}
		}
		else
		{
			weapons[active_weapon].ammoregenstart = -1;
		}
	}
	
	return 0;
}

void CHARACTER::on_predicted_input(NETOBJ_PLAYER_INPUT *new_input)
{
	// check for changes
	if(mem_comp(&input, new_input, sizeof(NETOBJ_PLAYER_INPUT)) != 0)
		last_action = server_tick();
		
	// copy new input
	mem_copy(&input, new_input, sizeof(input));
	num_inputs++;
	
	// or are not allowed to aim in the center
	if(input.target_x == 0 && input.target_y == 0)
		input.target_y = -1;	
}

void CHARACTER::on_direct_input(NETOBJ_PLAYER_INPUT *new_input)
{
	mem_copy(&latest_previnput, &latest_input, sizeof(latest_input));
	mem_copy(&latest_input, new_input, sizeof(latest_input));
	
	if(num_inputs > 2 && team != -1)
	{
		handle_weaponswitch();
		fire_weapon();
	}
	
	mem_copy(&latest_previnput, &latest_input, sizeof(latest_input));
}

void CHARACTER::tick()
{
	if(player->force_balanced)
	{
		char buf[128];
		str_format(buf, sizeof(buf), "You were moved to %s due to team balancing", game.controller->get_team_name(team));
		game.send_broadcast(buf, player->client_id);
		
		player->force_balanced = false;
	}

	if (frz_time > 0) {
		if (frz_time % (REFREEZE_INTERVAL_TICKS) == 0) {
			game.create_damageind(pos, 0, frz_time / REFREEZE_INTERVAL_TICKS);
//			dbg_msg("char","%i,%i",frz_time,frz_time / REFREEZE_INTERVAL_TICKS);
		}
		frz_time--;
		input.direction = 0;
		input.jump = 0;
		input.hook = 0;
		input.fire = 0;
		if (frz_time - 1 == 0) {
			unfreeze();
		}
	}
	if (player->isperfrz) {
		if ((lastperfrz + 25 * server_tickspeed()) < server_tick()) {
			freeze(15 * server_tickspeed());
			lastperfrz = server_tick();
		}
	}
	if (frz_tick && player->skills[PUP_EPICNINJA] && ((lastepicninja+10*server_tickspeed()) < server_tick()) && !epicninjaannounced) {
		str_format(bbuf, 128, "epic ninja ready!");
		game.send_chat_target(player->client_id, bbuf);
		epicninjaannounced=1;
	}

	//player_core core;
	//core.pos = pos;
	//core.jumped = jumped;
	core.input = input;
	core.tick(true);
	
	if (core.hooked_player >= 0) {
		if (game.players[core.hooked_player] && game.players[core.hooked_player]->get_character()) {
			game.players[core.hooked_player]->get_character()->lasthookedat = server_tick();
			game.players[core.hooked_player]->get_character()->lasthookedby = player->client_id;
		}
	}

	int tmp;
	if (col_is_kick(pos.x, pos.y))
	{
		//		dbg_msg("char","ki");
		server_kick(player->client_id, "You were kicked by evil kick zone");
	}
	else if (col_is_freeze(pos.x, pos.y))
	{
		//		dbg_msg("char","fr");
		int ft = server_tickspeed() * 3;
		int add=0;
//		dbg_msg("char","char collision with freeze: wasout: %i, frz_tick: %i",wasout,frz_tick);
		if ((wasout || frz_tick == 0) && (((lasthookedat + (server_tickspeed()<<1)) > server_tick()) || ((lasthammeredat + server_tickspeed()) > server_tick())))
		{
			int hooked = lasthookedat > lasthammeredat;
			int by = hooked ? lasthookedby : lasthammeredby;
			dbg_msg("char","got %s by player %i",hooked?"hooked":"hammered",by);
			if (game.players[by] && game.players[by]->get_character()) {
				add = game.players[by]->skills[PUP_LFREEZE];
			}
			blockedby=by;
			if (blockedby>=0) blocktime=ft+(add * (server_tickspeed()>>1));
			//dbg_msg("char","saving blocker (%i, time %i)",blockedby,blocktime);
		} else {
			if (frz_tick==0) {
//				dbg_msg("char","selfblocked");
				blockedby=-1;
			}
			if (blockedby>=0)
				ft=blocktime;
		}

		add -=player->skills[PUP_SFREEZE];
		ft += (add * (server_tickspeed()>>1));
		wasout=0;
		freeze(ft);
	}
	else if (col_is_unfreeze(pos.x, pos.y))
	{
		//		dbg_msg("char","unf");
		unfreeze();
		wasout=1;
	}
	else if ((tmp = col_is_colfrz(pos.x, pos.y)))
	{
		//		dbg_msg("char","colfrz %i! (rst is %i)",tmp,TILE_CFREEZE_RESET);
		if (lastcolfrz + REFREEZE_INTERVAL_TICKS < server_tick())
		{
			lastcolfrz = server_tick();
			if (tmp == TILE_CFREEZE_RESET)
			{
				player->forcecolor = 0;
			}
			else
			{
				switch (tmp)
				{
					case TILE_CFREEZE_GREEN:
						player->forcecolor = COL_GREEN;
						break;
					case TILE_CFREEZE_BLUE:
						player->forcecolor = COL_BLUE;
						break;
					case TILE_CFREEZE_RED:
						player->forcecolor = COL_RED;
						break;
					case TILE_CFREEZE_WHITE:
						player->forcecolor = COL_WHITE;
						break;
					case TILE_CFREEZE_GREY:
						player->forcecolor = COL_GREY;
						break;
					case TILE_CFREEZE_YELLOW:
						player->forcecolor = COL_YELLOW;
						break;
					case TILE_CFREEZE_PINK:
						player->forcecolor = COL_PINK;
						break;
				}
			}
			player->use_custom_color = (player->forcecolor) ? 1 : player->origusecustcolor;
			player->color_body = (player->forcecolor) ? player->forcecolor : player->origbodycolor;
			player->color_feet = (player->forcecolor) ? player->forcecolor : player->origfeetcolor;
			game.controller->on_player_info_change(player);
		}
	}
	else if ((tmp = col_is_pwrup(pos.x, pos.y)))
	{
		if ((lastup + ((tmp == TILE_PUP_RESET) ? server_tickspeed() >> 2 : server_tickspeed())) < server_tick())
		{
			lastup = server_tick();
			if (player->is1on1)
			{
				game.send_chat_target(player->client_id, "leave 1on1 mode first!");
			}
			if ((lastepicninja +  server_tickspeed()) > server_tick())
			{
				game.send_chat_target(player->client_id, "bad luck...");
			}
			else
			{
				if (tmp == TILE_PUP_RESET)
				{
					for (int z = 0; z < NUM_PUPS; ++z)
					{
						player->skills[z] = 0;
					}
					game.send_chat_target(player->client_id, "select new powerups!");
				}
				else
				{		
if (player->skills[tmp - TILE_PUP_JUMP] < 10) {
    player->skills[tmp - TILE_PUP_JUMP]++;
    tell_powerup_info(player->client_id, tmp - TILE_PUP_JUMP);
}
				}
			}
		}
	}
	else if ((tmp = col_is_loadslot(pos.x, pos.y)))
	{
		if ((lastloadsave + server_tickspeed()) < server_tick())
		{
			lastloadsave = server_tick();
			if (player->is1on1)
			{
				game.send_chat_target(player->client_id, "leave 1on1 mode first!");
			}
			else
			{
				int *sl = ((tmp == TILE_LOADSLOT1) ? player->slot1 : player->slot2);
				if (sl)
				{
					for (int z = 0; z < NUM_PUPS; ++z)
					{
						player->skills[z] = sl[z];
					}
					game.send_chat_target(player->client_id, "loaded and equipped powerups!");
				}
				else
				{
					game.send_chat_target(player->client_id, "nothing saved for this slot yet!");
				}
			}
		}
	}
	else if ((tmp = col_is_saveslot(pos.x, pos.y)))
	{
		if ((lastloadsave + server_tickspeed()) < server_tick())
		{
			lastloadsave = server_tick();
			if (player->is1on1)
			{
				game.send_chat_target(player->client_id, "leave 1on1 mode first!");
			}
			else
			{
				int *sl = ((tmp == TILE_SAVESLOT1) ? player->slot1 : player->slot2);
				if (sl)
					free(sl);
				sl = (int*) malloc(sizeof(int) * NUM_PUPS);
				for (int z = 0; z < NUM_PUPS; ++z)
				{
					sl[z] = player->skills[z];
				}
				((tmp == TILE_SAVESLOT1) ? player->slot1 : player->slot2) = sl;
				game.send_chat_target(player->client_id, "powerups saved!");
			}
		}
	}
	else if ((tmp = col_is_1on1(pos.x, pos.y)))
	{
		if ((lastloadsave + server_tickspeed()) < server_tick())
		{
			lastloadsave = server_tick();
			if ((player->is1on1 = 1 - player->is1on1))
			{
				int *sl = player->slot3;
				if (sl)
					free(sl);
				sl = (int*) malloc(sizeof(int) * NUM_PUPS);
				for (int z = 0; z < NUM_PUPS; ++z)
				{
					sl[z] = player->skills[z];
					player->skills[z] = 0;
				}
				player->slot3 = sl;
				player->oname = strdup(server_clientname(player->client_id));
				char *buf = (char*) malloc(strlen(player->oname) + 8);
				sprintf(buf, "[1on1] %s", player->oname);
				server_setclientname(player->client_id, buf);
			}
			else
			{
				int *sl = player->slot3;
				if (sl)
				{
					for (int z = 0; z < NUM_PUPS; ++z)
						player->skills[z] = sl[z];
				}
				server_setclientname(player->client_id, player->oname);
				free(player->oname);
				player->oname = NULL;
			}

			str_format(bbuf, 128, "1on1 mode %s", (player->is1on1) ? "ON" : "OFF");
			game.send_chat_target(player->client_id, bbuf);
		}
	}
	else if (col_is_perfrzon(pos.x, pos.y))
	{
		player->isperfrz=1;
		lastperfrz=server_tick()-(10*server_tickspeed());
	}
	else if (col_is_perfrzoff(pos.x, pos.y))
	{
		player->isperfrz=0;
	}
	else if ((tmp=col_is_teleport(pos.x, pos.y)))
	{
		if ((tmp-TILE_TPORT_FIRST)&1) {
			core.hooked_player = -1;
			core.hook_state = HOOK_RETRACTED;
			core.triggered_events |= COREEVENT_HOOK_RETRACT;
			core.hook_state = HOOK_RETRACTED;
			core.hook_pos = core.pos;
			core.pos = get_teleport((tmp-TILE_TPORT_FIRST)>>1);
		}
	}
	else
	{
		wasout=1;
	}
	float phys_size = 28.0f;
	// handle death-tiles
	int a,b,c,d;

	if (    ((a=col_get((int)(pos.x+phys_size/2), (int)(pos.y-phys_size/2)))<=5 && (a&COLFLAG_DEATH)) ||
			((b=col_get((int)(pos.x+phys_size/2), (int)(pos.y+phys_size/2)))<=5 && (b&COLFLAG_DEATH)) ||
			((c=col_get((int)(pos.x-phys_size/2), (int)(pos.y-phys_size/2)))<=5 && (c&COLFLAG_DEATH)) ||
			((d=col_get((int)(pos.x-phys_size/2), (int)(pos.y+phys_size/2)))<=5 && (d&COLFLAG_DEATH))   )
	{
		die(player->client_id, WEAPON_WORLD);
	}

	// handle weapons
	handle_weapons();

	player_state = input.player_state;

	// Previnput
	previnput = input;
	return;
}

void CHARACTER::tick_defered()
{
	// advance the dummy
	{
		WORLD_CORE tempworld;
		reckoningcore.world = &tempworld;
		reckoningcore.tick(false);
		reckoningcore.move();
		reckoningcore.quantize();
	}
	
	//lastsentcore;
	/*if(!dead)
	{*/
		vec2 start_pos = core.pos;
		vec2 start_vel = core.vel;
		bool stuck_before = test_box(core.pos, vec2(28.0f, 28.0f));
		
		core.move();
		bool stuck_after_move = test_box(core.pos, vec2(28.0f, 28.0f));
		core.quantize();
		bool stuck_after_quant = test_box(core.pos, vec2(28.0f, 28.0f));
		pos = core.pos;
		
		if(!stuck_before && (stuck_after_move || stuck_after_quant))
		{
			dbg_msg("player", "STUCK!!! %d %d %d %f %f %f %f %x %x %x %x", 
				stuck_before,
				stuck_after_move,
				stuck_after_quant,
				start_pos.x, start_pos.y,
				start_vel.x, start_vel.y,
				*((unsigned *)&start_pos.x), *((unsigned *)&start_pos.y),
				*((unsigned *)&start_vel.x), *((unsigned *)&start_vel.y));
		}

		int events = core.triggered_events;
		int mask = cmask_all_except_one(player->client_id);
		
		if(events&COREEVENT_GROUND_JUMP) game.create_sound(pos, SOUND_PLAYER_JUMP, mask);
		
		//if(events&COREEVENT_HOOK_LAUNCH) snd_play_random(CHN_WORLD, SOUND_HOOK_LOOP, 1.0f, pos);
		if(events&COREEVENT_HOOK_ATTACH_PLAYER) game.create_sound(pos, SOUND_HOOK_ATTACH_PLAYER, cmask_all());
		if(events&COREEVENT_HOOK_ATTACH_GROUND) game.create_sound(pos, SOUND_HOOK_ATTACH_GROUND, mask);
		if(events&COREEVENT_HOOK_HIT_NOHOOK) game.create_sound(pos, SOUND_HOOK_NOATTACH, mask);
		//if(events&COREEVENT_HOOK_RETRACT) snd_play_random(CHN_WORLD, SOUND_PLAYER_JUMP, 1.0f, pos);
	//}
	
	if(team == -1)
	{
		pos.x = input.target_x;
		pos.y = input.target_y;
	}
	
	// update the sendcore if needed
	{
		NETOBJ_CHARACTER predicted;
		NETOBJ_CHARACTER current;
		mem_zero(&predicted, sizeof(predicted));
		mem_zero(&current, sizeof(current));
		reckoningcore.write(&predicted);
		core.write(&current);

		// only allow dead reackoning for a top of 3 seconds
		if (core.forceupdate || (reckoning_tick + server_tickspeed() * 3 < server_tick() || mem_comp(&predicted, &current, sizeof(NETOBJ_CHARACTER)) != 0))
		{
			reckoning_tick = server_tick();
			sendcore = core;
			reckoningcore = core;
		}
	}
}

bool CHARACTER::increase_health(int amount)
{
	if(health >= 10)
		return false;
	health = clamp(health+amount, 0, 10);
	return true;
}

bool CHARACTER::increase_armor(int amount)
{
	if(armor >= 10)
		return false;
	armor = clamp(armor+amount, 0, 10);
	return true;
}

void CHARACTER::die(int killer, int weapon)
{
	/*if (dead || team == -1)
		return;*/
	int mode_special = game.controller->on_character_death(this, game.players[killer], weapon);

	dbg_msg("game", "kill killer='%d:%s' victim='%d:%s' weapon=%d special=%d",
		killer, server_clientname(killer),
		player->client_id, server_clientname(player->client_id), weapon, mode_special);

	// send the kill message
	NETMSG_SV_KILLMSG msg;
	msg.killer = killer;
	msg.victim = player->client_id;
	msg.weapon = weapon;
	msg.mode_special = mode_special;
	msg.pack(MSGFLAG_VITAL);
	server_send_msg(-1);

	// a nice sound
	game.create_sound(pos, SOUND_PLAYER_DIE);

	// set dead state
	// TODO: do stuff here
	/*
	die_pos = pos;
	dead = true;
	*/
	
	// this is for auto respawn after 3 secs
	player->die_tick = server_tick();
	
	alive = false;
	game.world.remove_entity(this);
	game.world.core.characters[player->client_id] = 0;
	game.create_death(pos, player->client_id);
	
	// we got to wait 0.5 secs before respawning
	player->respawn_tick = server_tick()+server_tickspeed()/2;
}

bool CHARACTER::take_damage(vec2 force, int dmg, int from, int weapon)
{
	core.vel += force;
	
	if(game.controller->is_friendly_fire(player->client_id, from) && !config.sv_teamdamage)
		return false;

	// player only inflicts half damage on self
	if(from == player->client_id)
		dmg = max(1, dmg/2);

	damage_taken++;

	// create healthmod indicator
	if(server_tick() < damage_taken_tick+25)
	{
		// make sure that the damage indicators doesn't group together
		game.create_damageind(pos, damage_taken*0.25f, dmg);
	}
	else
	{
		damage_taken = 0;
		game.create_damageind(pos, 0, dmg);
	}
	if (weapon == WEAPON_NINJA) freeze(3 * server_tickspeed());
	if(dmg)
	{
		if(armor)
		{
			if(dmg > 1)
			{
				health--;
				dmg--;
			}
			
			if(dmg > armor)
			{
				dmg -= armor;
				armor = 0;
			}
			else
			{
				armor -= dmg;
				dmg = 0;
			}
		}
		
		health -= dmg;
	}

	damage_taken_tick = server_tick();

	// do damage hit sound
	if(from >= 0 && from != player->client_id && game.players[from])
		game.create_sound(game.players[from]->view_pos, SOUND_HIT, cmask_one(from));

	// check for death
	if(health <= 0)
	{
		die(from, weapon);
		
		// set attacker's face to happy (taunt!)
		if (from >= 0 && from != player->client_id && game.players[from])
		{
			CHARACTER *chr = game.players[from]->get_character();
			if (chr)
			{
				chr->emote_type = EMOTE_HAPPY;
				chr->emote_stop = server_tick() + server_tickspeed();
			}
		}
	
		return false;
	}

	if (dmg > 2)
		game.create_sound(pos, SOUND_PLAYER_PAIN_LONG);
	else
		game.create_sound(pos, SOUND_PLAYER_PAIN_SHORT);

	emote_type = EMOTE_PAIN;
	emote_stop = server_tick() + 500 * server_tickspeed() / 1000;

	// spawn blood?
	return true;
}

void CHARACTER::snap(int snapping_client)
{
	if(networkclipped(snapping_client))
		return;
	
	NETOBJ_CHARACTER *character = (NETOBJ_CHARACTER *)snap_new_item(NETOBJTYPE_CHARACTER, player->client_id, sizeof(NETOBJ_CHARACTER));
	
	// write down the core
	if(game.world.paused)
	{
		// no dead reckoning when paused because the client doesn't know
		// how far to perform the reckoning
		character->tick = 0;
		core.write(character);
	}
	else
	{
		character->tick = reckoning_tick;
		sendcore.write(character);
	}
	
	// set emote
	if (emote_stop < server_tick())
	{
		emote_type = EMOTE_NORMAL;
		emote_stop = -1;
	}

	character->emote = emote_type;

	character->ammocount = 0;
	character->health = 0;
	character->armor = 0;
	
	character->weapon = active_weapon;
	character->attacktick = attack_tick;

	character->direction = input.direction;

	if(player->client_id == snapping_client)
	{
		character->health = health;
		character->armor = armor;
		if(weapons[active_weapon].ammo > 0)
			character->ammocount = weapons[active_weapon].ammo;
	}

	if (character->emote == EMOTE_NORMAL)
	{
		if(250 - ((server_tick() - last_action)%(250)) < 5)
			character->emote = EMOTE_BLINK;
	}

	character->player_state = player_state;
}
/*----------bbmod------------*/
bool CHARACTER::freeze(int ticks)
{
	if (ticks <= 1) return false;
	if (frz_tick > 0) { //already frozen
		if (frz_tick + REFREEZE_INTERVAL_TICKS > server_tick()) return true;
	} else {
		frz_start=server_tick();
		epicninjaannounced=0;
		lastepicninja=server_tick()-5*server_tickspeed();
	}
	frz_tick=server_tick();
	frz_time=ticks;
	ninja.activationtick = server_tick();
	weapons[WEAPON_NINJA].got = true;
	weapons[WEAPON_NINJA].ammo = -1;
	if (active_weapon != WEAPON_NINJA) {
		set_weapon(WEAPON_NINJA);
	}
	return true;
}
bool CHARACTER::unfreeze(){
	if (frz_time > 0) {
		frz_tick = frz_time = frz_start = 0;
		ninja.currentmovetime=-1;//prevent magic teleport when unfreezing while epic ninja
		weapons[WEAPON_NINJA].got = false;
		if (last_weapon <0 || last_weapon >= NUM_WEAPONS || last_weapon == WEAPON_NINJA || (!weapons[last_weapon].got)) last_weapon = WEAPON_HAMMER;
		set_weapon(last_weapon);
		epicninjaannounced=0;
		return true;
	}
	return false;

return true;
}
void tell_powerup_info(int client_id, int skill){
	static char bbuf[256];
	switch(skill) {
		case PUP_JUMP:
			str_format(bbuf, 128, "you got an extra air jump!");break;
		case PUP_HAMMER:
			str_format(bbuf, 128, "hammer powered!");break;
		case PUP_LFREEZE:
			str_format(bbuf, 128, "enemy freeze time increased!");break;
		case PUP_SFREEZE:
			str_format(bbuf, 128, "own freeze time shortened!");break;
		case PUP_HOOKDUR:
			str_format(bbuf, 128, "hook duration increased (you wont see it but it works!)!");break;
		case PUP_HOOKLEN:
			str_format(bbuf, 128, "hook length extended (its not smooth, however)");break;
		case PUP_WALKSPD:
			str_format(bbuf, 128, "walk speed increased");break;
		case PUP_EPICNINJA:
			str_format(bbuf, 128, "eeeeeeeeeeeeeeeeepic ninja! (freeze attack)");break;
		case PUP_TPORT:
			str_format(bbuf, 128, "short distance teleport!");break;
		case PUP_MAGNET:
			str_format(bbuf, 128, "magnet!");break;
		case PUP_KOD:
			str_format(bbuf, 128, "kiss of death");break;
		case PUP_LASER:
			str_format(bbuf, 128, "freeze laser");break;
		default:
			str_format(bbuf, 128, "wtf");break;
	}
	game.send_chat_target(client_id, bbuf);
}
/*---------eobbmod------------*/

