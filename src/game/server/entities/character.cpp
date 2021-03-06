/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <new>

#include <cstdlib>
#include <cstdio>
#include <cstring>

#include <engine/shared/config.h>
#include <game/server/gamecontext.h>
#include <game/mapitems.h>

#include "character.h"
#include "laser.h"
#include "projectile.h"

static char bBuf[128];

//input count
struct CInputCount
{
	int m_Presses;
	int m_Releases;
};

CInputCount CountInput(int Prev, int Cur)
{
	CInputCount c = {0, 0};
	Prev &= INPUT_STATE_MASK;
	Cur &= INPUT_STATE_MASK;
	int i = Prev;

	while(i != Cur)
	{
		i = (i+1)&INPUT_STATE_MASK;
		if(i&1)
			c.m_Presses++;
		else
			c.m_Releases++;
	}

	return c;
}


MACRO_ALLOC_POOL_ID_IMPL(CCharacter, MAX_CLIENTS)

// Character, "physical" player's part
CCharacter::CCharacter(CGameWorld *pWorld)
: CEntity(pWorld, CGameWorld::ENTTYPE_CHARACTER)
{
	m_ProximityRadius = ms_PhysSize;
	m_Health = 0;
	m_Armor = 0;
}

void CCharacter::Reset()
{
	Destroy();
}

bool CCharacter::Spawn(CPlayer *pPlayer, vec2 Pos)
{
	m_EmoteStop = -1;
	m_LastAction = -1;
	m_ActiveWeapon = WEAPON_HAMMER;
	m_LastWeapon = WEAPON_GUN;
	m_QueuedWeapon = -1;

	m_pPlayer = pPlayer;
	m_Pos = Pos;

	m_Core.Reset();
	m_Core.Init(&GameServer()->m_World.m_Core, GameServer()->Collision());
	m_Core.m_Pos = m_Pos;
	GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] = &m_Core;

	m_ReckoningTick = 0;
	mem_zero(&m_SendCore, sizeof(m_SendCore));
	mem_zero(&m_ReckoningCore, sizeof(m_ReckoningCore));

	GameServer()->m_World.InsertEntity(this);
	m_Alive = true;
	CanFire = true;

	m_Core.Skills=m_pPlayer->Skills;
	if (m_pPlayer->is1on1) {
		int *sl = m_pPlayer->slot3;
		if (sl) {
			for (int z = 0; z < NUM_PUPS; ++z)
				m_pPlayer->Skills[z] = sl[z];
		}
		Server()->SetClientName(m_pPlayer->GetCID(), m_pPlayer->oname);
		free(m_pPlayer->oname);
		m_pPlayer->oname = NULL;
		m_pPlayer->is1on1=0;
	}
	lastepicninja=0;
	epicninjaannounced=0;

	GameServer()->m_pController->OnCharacterSpawn(this);

	return true;
}

void CCharacter::Destroy()
{
	GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] = 0;
	m_Alive = false;
}

void CCharacter::SetWeapon(int W)
{
	if(W == m_ActiveWeapon)
		return;

	m_LastWeapon = m_ActiveWeapon;
	m_QueuedWeapon = -1;
	m_ActiveWeapon = W;
	GameServer()->CreateSound(m_Pos, SOUND_WEAPON_SWITCH);

	if(m_ActiveWeapon < 0 || m_ActiveWeapon >= NUM_WEAPONS)
		m_ActiveWeapon = 0;
}

bool CCharacter::IsGrounded()
{
	if(GameServer()->Collision()->CheckPoint(m_Pos.x+m_ProximityRadius/2, m_Pos.y+m_ProximityRadius/2+5))
		return true;
	if(GameServer()->Collision()->CheckPoint(m_Pos.x-m_ProximityRadius/2, m_Pos.y+m_ProximityRadius/2+5))
		return true;
	return false;
}


void CCharacter::HandleNinja()
{
	if(frz_time - Server()->TickSpeed()*0.3 <= 0)
		return;
	if(m_ActiveWeapon != WEAPON_NINJA)
		return;
	vec2 Direction = normalize(vec2(m_LatestInput.m_TargetX, m_LatestInput.m_TargetY));

	if ((Server()->Tick() - m_Ninja.m_ActivationTick) > (g_pData->m_Weapons.m_Ninja.m_Duration * Server()->TickSpeed() / 1000))
	{
		// time's up, return
		m_aWeapons[WEAPON_NINJA].m_Got = false;
		m_ActiveWeapon = m_LastWeapon;

		SetWeapon(m_ActiveWeapon);
		return;
	}

	// force ninja Weapon
	SetWeapon(WEAPON_NINJA);

	m_Ninja.m_CurrentMoveTime--;

	if (m_Ninja.m_CurrentMoveTime == 0)
	{
		// reset velocity
		m_Core.m_Vel.x = 0.0f;
		m_Core.m_Vel.y = 0.0f;
		m_Core.m_Pos = epicninjaoldpos;
		//m_Core.m_Vel = m_Ninja.m_ActivationDir*m_Ninja.m_OldVelAmount;
	}

	if (m_Ninja.m_CurrentMoveTime > 0)
	{
		// Set velocity
		m_Core.m_Vel = m_Ninja.m_ActivationDir * g_pData->m_Weapons.m_Ninja.m_Velocity;
		vec2 OldPos = m_Pos;
		GameServer()->Collision()->MoveBox(&m_Core.m_Pos, &m_Core.m_Vel, vec2(m_ProximityRadius, m_ProximityRadius), 0.f);

		// reset velocity so the client doesn't predict stuff
		m_Core.m_Vel = vec2(0.f, 0.f);

		// check if we Hit anything along the way
		{
			CCharacter *aEnts[MAX_CLIENTS];
			vec2 Dir = m_Pos - OldPos;
			float Radius = m_ProximityRadius * 2.0f;
			vec2 Center = OldPos + Dir * 0.5f;
			int Num = GameServer()->m_World.FindEntities(Center, Radius, (CEntity**)aEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);

			for (int i = 0; i < Num; ++i)
			{
				if (aEnts[i] == this)
					continue;

				// make sure we haven't Hit this object before
				bool bAlreadyHit = false;
				for (int j = 0; j < m_NumObjectsHit; j++)
				{
					if (m_apHitObjects[j] == aEnts[i])
						bAlreadyHit = true;
				}
				if (bAlreadyHit)
					continue;

				// check so we are sufficiently close
				if (distance(aEnts[i]->m_Pos, m_Pos) > (m_ProximityRadius * 2.0f))
					continue;

				// Hit a player, give him damage and stuffs...
				GameServer()->CreateSound(aEnts[i]->m_Pos, SOUND_NINJA_HIT);
				// set his velocity to fast upward (for now)
				if(m_NumObjectsHit < 10)
					m_apHitObjects[m_NumObjectsHit++] = aEnts[i];
					
				aEnts[i]->TakeDamage(vec2(0, 10.0f), 0, m_pPlayer->GetCID(), WEAPON_NINJA);

				aEnts[i]->TakeDamage(vec2(0, -10.0f), g_pData->m_Weapons.m_Ninja.m_pBase->m_Damage, m_pPlayer->GetCID(), WEAPON_NINJA);
			}
		}

		return;
	}

	return;
}


void CCharacter::DoWeaponSwitch()
{
	// make sure we can switch
	if(m_ReloadTimer != 0 || m_QueuedWeapon == -1 || m_aWeapons[WEAPON_NINJA].m_Got)
		return;

	// switch Weapon
	SetWeapon(m_QueuedWeapon);
}

void CCharacter::HandleWeaponSwitch()
{
	int WantedWeapon = m_ActiveWeapon;
	if(m_QueuedWeapon != -1)
		WantedWeapon = m_QueuedWeapon;

	// select Weapon
	int Next = CountInput(m_LatestPrevInput.m_NextWeapon, m_LatestInput.m_NextWeapon).m_Presses;
	int Prev = CountInput(m_LatestPrevInput.m_PrevWeapon, m_LatestInput.m_PrevWeapon).m_Presses;

	if(Next < 128) // make sure we only try sane stuff
	{
		while(Next) // Next Weapon selection
		{
			WantedWeapon = (WantedWeapon+1)%NUM_WEAPONS;
			if(m_aWeapons[WantedWeapon].m_Got)
				Next--;
		}
	}

	if(Prev < 128) // make sure we only try sane stuff
	{
		while(Prev) // Prev Weapon selection
		{
			WantedWeapon = (WantedWeapon-1)<0?NUM_WEAPONS-1:WantedWeapon-1;
			if(m_aWeapons[WantedWeapon].m_Got)
				Prev--;
		}
	}

	// Direct Weapon selection
	if(m_LatestInput.m_WantedWeapon)
		WantedWeapon = m_Input.m_WantedWeapon-1;

	// check for insane values
	if(WantedWeapon >= 0 && WantedWeapon < NUM_WEAPONS && WantedWeapon != m_ActiveWeapon && m_aWeapons[WantedWeapon].m_Got)
		m_QueuedWeapon = WantedWeapon;

	DoWeaponSwitch();
}

void CCharacter::FireWeapon()
{
	if(!CanFire && m_ActiveWeapon != WEAPON_NINJA)
		return;
	if(m_ReloadTimer != 0)
		return;

	DoWeaponSwitch();
	vec2 Direction = normalize(vec2(m_LatestInput.m_TargetX, m_LatestInput.m_TargetY));

	bool FullAuto = false;
	if(m_ActiveWeapon == WEAPON_GRENADE || m_ActiveWeapon == WEAPON_SHOTGUN || m_ActiveWeapon == WEAPON_RIFLE)
		FullAuto = true;


	// check if we gonna fire
	bool WillFire = false;
	if(CountInput(m_LatestPrevInput.m_Fire, m_LatestInput.m_Fire).m_Presses)
		WillFire = true;

	if(FullAuto && (m_LatestInput.m_Fire&1) && m_aWeapons[m_ActiveWeapon].m_Ammo)
		WillFire = true;

	if(!WillFire)
		return;

	// check for ammo
	if(!m_aWeapons[m_ActiveWeapon].m_Ammo)
	{
		// 125ms is a magical limit of how fast a human can click
		m_ReloadTimer = 125 * Server()->TickSpeed() / 1000;
		GameServer()->CreateSound(m_Pos, SOUND_WEAPON_NOAMMO);
		return;
	}

	vec2 ProjStartPos = m_Pos+Direction*m_ProximityRadius*0.75f;

	switch(m_ActiveWeapon)
	{
		case WEAPON_HAMMER:
		{
			// reset objects Hit
			m_NumObjectsHit = 0;
			GameServer()->CreateSound(m_Pos, SOUND_HAMMER_FIRE);

			CCharacter *apEnts[MAX_CLIENTS];
			int Hits = 0;
			int Num = GameServer()->m_World.FindEntities(ProjStartPos, m_ProximityRadius*0.5f, (CEntity**)apEnts,
														MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);

			for (int i = 0; i < Num; ++i)
			{
				CCharacter *pTarget = apEnts[i];
				
				//for race mod or any other mod, which needs hammer hits through the wall remove second condition
				if ((pTarget == this) /* || GameServer()->Collision()->IntersectLine(ProjStartPos, Target->m_Pos, NULL, NULL) */)
					continue;

				// set his velocity to fast upward (for now)
				GameServer()->CreateHammerHit(m_Pos);
				//aEnts[i]->TakeDamage(vec2(0.f, -1.f), g_pData->m_Weapons.m_Hammer.m_pBase->m_Damage, m_pPlayer->GetCID(), m_ActiveWeapon);
				
				apEnts[i]->TakeDamage(vec2(0.f,-1.f),0,m_pPlayer->GetCID(),m_ActiveWeapon);
				apEnts[i]->lasthammeredat = Server()->Tick();
				apEnts[i]->lasthammeredby = m_pPlayer->GetCID();
				
				vec2 Dir;
				if (length(pTarget->m_Pos - m_Pos) > 0.0f)
					Dir = normalize(pTarget->m_Pos - m_Pos);
				else
					Dir = vec2(0.f, -1.f);
				pTarget->m_Core.m_Vel += normalize(Dir + vec2(0.f, -1.1f)) * (10.0f + (m_pPlayer->Skills[PUP_HAMMER] * 3));
				pTarget->Unfreeze();

				Hits++;
			}

			// if we Hit anything, we have to wait for the reload
			if(Hits)
				m_ReloadTimer = Server()->TickSpeed()/3;

		} break;

		case WEAPON_GUN:
		{
			CProjectile *pProj = new CProjectile(GameWorld(), WEAPON_GUN,
				m_pPlayer->GetCID(),
				ProjStartPos,
				Direction,
				(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_GunLifetime),
				0, 0, 0, -1, WEAPON_GUN);
				
			// pack the Projectile and send it to the client Directly
			CNetObj_Projectile p;
			pProj->FillInfo(&p);

			CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
			Msg.AddInt(1);
			for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
				Msg.AddInt(((int *)&p)[i]);

			Server()->SendMsg(&Msg, 0, m_pPlayer->GetCID());

			GameServer()->CreateSound(m_Pos, SOUND_GUN_FIRE);
		} break;

		case WEAPON_SHOTGUN:
		{
			int ShotSpread = 2;

			CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
			Msg.AddInt(ShotSpread*2+1);

			for(int i = -ShotSpread; i <= ShotSpread; ++i)
			{
				float Spreading[] = {-0.185f, -0.070f, 0, 0.070f, 0.185f};
				float a = GetAngle(Direction);
				a += Spreading[i+2];
				float v = 1-(absolute(i)/(float)ShotSpread);
				float Speed = mix((float)GameServer()->Tuning()->m_ShotgunSpeeddiff, 1.0f, v);
				CProjectile *pProj = new CProjectile(GameWorld(), WEAPON_SHOTGUN,
					m_pPlayer->GetCID(),
					ProjStartPos,
					vec2(cosf(a), sinf(a))*Speed,
					(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_ShotgunLifetime),
					1, 0, 0, -1, WEAPON_SHOTGUN);

				// pack the Projectile and send it to the client Directly
				CNetObj_Projectile p;
				pProj->FillInfo(&p);

				for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
					Msg.AddInt(((int *)&p)[i]);
			}

			Server()->SendMsg(&Msg, 0,m_pPlayer->GetCID());

			GameServer()->CreateSound(m_Pos, SOUND_SHOTGUN_FIRE);
		} break;

		case WEAPON_GRENADE:
		{
			CProjectile *pProj = new CProjectile(GameWorld(), WEAPON_GRENADE,
				m_pPlayer->GetCID(),
				ProjStartPos,
				Direction,
				(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_GrenadeLifetime),
				1, true, 0, SOUND_GRENADE_EXPLODE, WEAPON_GRENADE);

			// pack the Projectile and send it to the client Directly
			CNetObj_Projectile p;
			pProj->FillInfo(&p);

			CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
			Msg.AddInt(1);
			for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
				Msg.AddInt(((int *)&p)[i]);
			Server()->SendMsg(&Msg, 0, m_pPlayer->GetCID());

			GameServer()->CreateSound(m_Pos, SOUND_GRENADE_FIRE);
		} break;

		case WEAPON_RIFLE:
		{
			new CLaser(GameWorld(), m_Pos, Direction, GameServer()->Tuning()->m_LaserReach, m_pPlayer->GetCID());
			GameServer()->CreateSound(m_Pos, SOUND_RIFLE_FIRE);
		} break;

		case WEAPON_NINJA:
		{
			if (m_pPlayer->Skills[PUP_EPICNINJA])
			{
				if ((lastepicninja + 10 * Server()->TickSpeed() - m_pPlayer->Skills[PUP_EPICNINJA] * Server()->TickSpeed() / 1.35) <= Server()->Tick()) {
					lastepicninja=Server()->Tick();
					epicninjaoldpos=m_Pos;
					epicninjaannounced=0;
				} else {
					str_format(bBuf, 128, "Freeze attack not ready yet.");
					GameServer()->SendChatTarget(m_pPlayer->GetCID(), bBuf);
                                        return;
                                }
                        } else {
                                return;
                        }
// -----------
			// reset Hit objects
			m_NumObjectsHit = 0;

			m_Ninja.m_ActivationDir = Direction;
			m_Ninja.m_CurrentMoveTime = g_pData->m_Weapons.m_Ninja.m_Movetime * Server()->TickSpeed() / 1000;
			m_Ninja.m_OldVelAmount = length(m_Core.m_Vel);

			GameServer()->CreateSound(m_Pos, SOUND_NINJA_FIRE);
		} break;

	}

	m_AttackTick = Server()->Tick();
	
	if(m_aWeapons[m_ActiveWeapon].m_Ammo > 0) // -1 == unlimited
		m_aWeapons[m_ActiveWeapon].m_Ammo--;
	
	if(!m_ReloadTimer)
		m_ReloadTimer = g_pData->m_Weapons.m_aId[m_ActiveWeapon].m_Firedelay * Server()->TickSpeed() / 1000;
}

void CCharacter::HandleWeapons()
{
	//ninja
	HandleNinja();

	// check reload timer
	if(m_ReloadTimer)
	{
		m_ReloadTimer--;
		return;
	}

	// fire Weapon, if wanted
	FireWeapon();

	// ammo regen
	int AmmoRegenTime = g_pData->m_Weapons.m_aId[m_ActiveWeapon].m_Ammoregentime;
	if(AmmoRegenTime)
	{
		// If equipped and not active, regen ammo?
		if (m_ReloadTimer <= 0)
		{
			if (m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart < 0)
				m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart = Server()->Tick();

			if ((Server()->Tick() - m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart) >= AmmoRegenTime * Server()->TickSpeed() / 1000)
			{
				// Add some ammo
				m_aWeapons[m_ActiveWeapon].m_Ammo = min(m_aWeapons[m_ActiveWeapon].m_Ammo + 1, 10);
				m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart = -1;
			}
		}
		else
		{
			m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart = -1;
		}
	}

	return;
}

bool CCharacter::GiveWeapon(int Weapon, int Ammo)
{
	if(m_aWeapons[Weapon].m_Ammo < g_pData->m_Weapons.m_aId[Weapon].m_Maxammo || !m_aWeapons[Weapon].m_Got)
	{
		m_aWeapons[Weapon].m_Got = true;
		m_aWeapons[Weapon].m_Ammo = min(g_pData->m_Weapons.m_aId[Weapon].m_Maxammo, Ammo);
		return true;
	}
	return false;
}

void CCharacter::GiveNinja()
{
	m_Ninja.m_ActivationTick = Server()->Tick();
	m_aWeapons[WEAPON_NINJA].m_Got = true;
	m_aWeapons[WEAPON_NINJA].m_Ammo = -1;
	if (m_ActiveWeapon != WEAPON_NINJA)
		m_LastWeapon = m_ActiveWeapon;
	m_ActiveWeapon = WEAPON_NINJA;

	GameServer()->CreateSound(m_Pos, SOUND_PICKUP_NINJA);
}

void CCharacter::SetEmote(int Emote, int Tick)
{
	m_EmoteType = Emote;
	m_EmoteStop = Tick;
}

void CCharacter::OnPredictedInput(CNetObj_PlayerInput *pNewInput)
{
	// check for changes
	if(mem_comp(&m_Input, pNewInput, sizeof(CNetObj_PlayerInput)) != 0)
		m_LastAction = Server()->Tick();

	// copy new input
	mem_copy(&m_Input, pNewInput, sizeof(m_Input));
	m_NumInputs++;

	// or are not allowed to aim in the center
	if(m_Input.m_TargetX == 0 && m_Input.m_TargetY == 0)
		m_Input.m_TargetY = -1;
}

void CCharacter::OnDirectInput(CNetObj_PlayerInput *pNewInput)
{
	mem_copy(&m_LatestPrevInput, &m_LatestInput, sizeof(m_LatestInput));
	mem_copy(&m_LatestInput, pNewInput, sizeof(m_LatestInput));

	if(m_NumInputs > 2 && m_pPlayer->GetTeam() != TEAM_SPECTATORS)
	{
		HandleWeaponSwitch();
		FireWeapon();
	}

	mem_copy(&m_LatestPrevInput, &m_LatestInput, sizeof(m_LatestInput));
}

void CCharacter::HandleFreeze()
{
	if (frz_time > 0)
	{
		SetEmote(EMOTE_BLINK, Server()->Tick());
		if (frz_time % (REFREEZE_INTERVAL_TICKS) == 0)
		{
			if(frz_tick < 7*Server()->Tick())
			GameServer()->CreateDamageInd(m_Pos, 0, frz_time / REFREEZE_INTERVAL_TICKS);
                }
		frz_time--;
		m_Input.m_Direction = 0;
		m_Input.m_Jump = 0;
		m_Input.m_Hook = 0;
		CanFire = false;
		if (frz_time - 1 == 0) {
			Unfreeze();
		}
	}
}

void CCharacter::ResetInput()
{
	m_Input.m_Direction = 0;
	m_Input.m_Hook = 0;
	// simulate releasing the fire button
	if((m_Input.m_Fire&1) != 0)
		m_Input.m_Fire++;
	m_Input.m_Fire &= INPUT_STATE_MASK;
	m_Input.m_Jump = 0;
	m_LatestPrevInput = m_LatestInput = m_Input;
}

void CCharacter::Tick()
{
	FreezeTik();
	HandleFreeze();
	CollisonMate = GameServer()->Collision()->GetTile(m_Pos.x, m_Pos.y);
	m_Armor=(frz_time >= 0)?10-(frz_time/15):0;
	if(!strncmp(Server()->ClientName(m_pPlayer->GetCID()), "[bot]", 5))
	{
		int BanID = m_pPlayer->GetCID();
		char aBuf[256];

		//Notify the other players
		str_format(aBuf, sizeof(aBuf), "%s kicked due too cheating. (Reason: [bot])", Server()->ClientName(BanID));
		GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf);
		Server()->Kick(m_pPlayer->GetCID(), aBuf);
		return;
	}
	if(m_pPlayer->Skills[PUP_SFREEZE] > 5)
	{
		int CheaterID = m_pPlayer->GetCID();
		dbg_msg("IMPORTANT","Somone has more than 5 lower freeze times!!! causing them too be able too walk through freeze tile! ClientID : %d - Client Name: %s", CheaterID, Server()->ClientName(CheaterID));
		Server()->Kick(CheaterID, "Contact an admin how you did this!");
		return;
	}
	if(m_MuteInfo + Server()->TickSpeed() * 90 <= Server()->Tick())
	{
		m_pPlayer->m_MuteTimes = 0;
		m_MuteInfo = Server()->Tick();
	}
	if(m_pPlayer->m_ForceBalanced)
	{
		char Buf[128];
		str_format(Buf, sizeof(Buf), "You were moved to %s due to team balancing", GameServer()->m_pController->GetTeamName(m_pPlayer->GetTeam()));
		GameServer()->SendBroadcast(Buf, m_pPlayer->GetCID());

		m_pPlayer->m_ForceBalanced = false;
	}
	if (frz_tick && m_pPlayer->Skills[PUP_EPICNINJA] && (lastepicninja+10*Server()->TickSpeed() - (m_pPlayer->Skills[PUP_EPICNINJA] * Server()->TickSpeed() / 1.35) <= Server()->Tick()) && !epicninjaannounced)
	{
		if(frz_time - Server()->TickSpeed()*0.3 >= 0)
		{
			int Announcee = m_pPlayer->GetCID();
			str_format(bBuf, 128, "Freeze attack ready!");
			GameServer()->SendChatTarget(Announcee, bBuf);
			epicninjaannounced=1;
		}
	}

	m_Core.m_Input = m_Input;
	m_Core.Tick(true);
	

	if (m_Core.m_HookedPlayer >= 0) {
		if (GameServer()->m_apPlayers[m_Core.m_HookedPlayer] && GameServer()->m_apPlayers[m_Core.m_HookedPlayer]->GetCharacter()) {
                       GameServer()->m_apPlayers[m_Core.m_HookedPlayer]->GetCharacter()->lasthookedat = Server()->Tick();
                       GameServer()->m_apPlayers[m_Core.m_HookedPlayer]->GetCharacter()->lasthookedby = m_pPlayer->GetCID();
              }
       }

	if (CollisonMate == TILE_KICK)
	{
		int KickID = m_pPlayer->GetCID();
		Server()->Kick(KickID, "Kicked by evil kick zone");
		return;
	}
	else if (CollisonMate == TILE_FREEZE || (CollisonMate >= TILE_COLFRZ_GREEN && CollisonMate <= TILE_COLFRZ_PINK))
	{
		int CID = m_pPlayer->GetCID();
		if((Server()->Tick() < GameServer()->m_apPlayers[CID]->m_LastActionTick+ (Server()->TickSpeed()*30)))
		{
			if ((wasout || frz_tick == 0) && (((lasthookedat + (Server()->TickSpeed()<<1)) > Server()->Tick()) || ((lasthammeredat + Server()->TickSpeed()) > Server()->Tick())))
			{
				if(by != CID)
				{
					if(GameServer()->m_apPlayers[by] && GameServer()->m_apPlayers[by]->GetCharacter())
					{
						GameServer()->m_apPlayers[CID]->m_Score--;
						GameServer()->m_apPlayers[by]->m_Score++;
					}
				}
			}
		}
		Freeze(ft);
		if ((CollisonMate >= TILE_COLFRZ_GREEN && CollisonMate <= TILE_COLFRZ_PINK) && lastcolfrz + REFREEZE_INTERVAL_TICKS < Server()->Tick())
		{
			lastcolfrz = Server()->Tick();
			int prevfc = m_pPlayer->forcecolor;
			switch (CollisonMate)
			{
				case TILE_COLFRZ_GREEN:
					if(!m_pPlayer->m_NoGreen)
					m_pPlayer->forcecolor = COL_GREEN;
					break;
				case TILE_COLFRZ_BLUE:
					if(!m_pPlayer->m_NoBlue)
					m_pPlayer->forcecolor = COL_BLUE;
					break;
				case TILE_COLFRZ_RED:
					if(!m_pPlayer->m_NoRed)
					m_pPlayer->forcecolor = COL_RED;
					break;
				case TILE_COLFRZ_WHITE:
					if(!m_pPlayer->m_NoWhite)
					m_pPlayer->forcecolor = COL_WHITE;
					break;
				case TILE_COLFRZ_GREY:
					if(!m_pPlayer->m_NoGrey)
					m_pPlayer->forcecolor = COL_GREY;
					break;
				case TILE_COLFRZ_YELLOW:
					if(!m_pPlayer->m_NoYellow)
					m_pPlayer->forcecolor = COL_YELLOW;
					break;
				case TILE_COLFRZ_PINK:
					if(!m_pPlayer->m_NoPink)
					m_pPlayer->forcecolor = COL_PINK;
					break;
			}
			if (m_pPlayer->forcecolor != prevfc)
			{
				m_pPlayer->m_TeeInfos.m_UseCustomColor = (m_pPlayer->forcecolor) ? 1 : m_pPlayer->origusecustcolor;
				m_pPlayer->m_TeeInfos.m_ColorBody = (m_pPlayer->forcecolor) ? m_pPlayer->forcecolor : m_pPlayer->origbodycolor;
				m_pPlayer->m_TeeInfos.m_ColorFeet = (m_pPlayer->forcecolor) ? m_pPlayer->forcecolor : m_pPlayer->origfeetcolor;
				GameServer()->m_pController->OnPlayerInfoChange(m_pPlayer);
			}
		}

	} 

	if ((CollisonMate >= TILE_GREEN && CollisonMate <= TILE_PINK) && lastcolfrz + REFREEZE_INTERVAL_TICKS < Server()->Tick())
		{
			lastcolfrz = Server()->Tick();
			int ColID = m_pPlayer->GetCID();
			switch (CollisonMate)
			{
				case TILE_GREEN:
					m_pPlayer->m_NoGreen = true;
					GameServer()->SendChatTarget(ColID, "Green will no longer effect you!");
					break;
				case TILE_BLUE:
					m_pPlayer->m_NoBlue = true;
					GameServer()->SendChatTarget(ColID, "Blue will no longer effect you!");
					break;
				case TILE_RED:
					m_pPlayer->m_NoRed = true;
					GameServer()->SendChatTarget(ColID, "Red will no longer effect you!");
					break;
				case TILE_WHITE:
					m_pPlayer->m_NoWhite = true;
					GameServer()->SendChatTarget(ColID, "White will no longer effect you!");
					break;
				case TILE_GREY:
					m_pPlayer->m_NoGrey = true;
					GameServer()->SendChatTarget(ColID, "Grey will no longer effect you!");
					break;
				case TILE_YELLOW:
					m_pPlayer->m_NoYellow = true;
					GameServer()->SendChatTarget(ColID, "Yellow will no longer effect you!");
 					break;
                              case TILE_PINK:
					m_pPlayer->m_NoPink = true;
					GameServer()->SendChatTarget(ColID, "Pink will no longer effect you!");
					break;
			}

		}
	else if (CollisonMate == TILE_UNFREEZE)
	{
		Unfreeze();
		wasout=1;

	}
	else if (CollisonMate == TILE_1ON1TOGGLE)
	{
		if ((lastloadsave + Server()->TickSpeed()) < Server()->Tick())
		{
			lastloadsave = Server()->Tick();
			if ((m_pPlayer->is1on1 = 1 - m_pPlayer->is1on1))
			{
				int *sl = m_pPlayer->slot3;
				if (sl)
					free(sl);
				sl = (int*) malloc(sizeof(int) * NUM_PUPS);
				for (int z = 0; z < NUM_PUPS; ++z)
				{
					sl[z] = m_pPlayer->Skills[z];
					m_pPlayer->Skills[z] = 0;
				}
				m_pPlayer->slot3 = sl;
				m_pPlayer->oname = strdup(Server()->ClientName(m_pPlayer->GetCID()));
				char *buf = (char*) malloc(strlen(m_pPlayer->oname) + 8);
				sprintf(buf, "[1on1] %s", m_pPlayer->oname);
				Server()->SetClientName(m_pPlayer->GetCID(), buf);

			}
			else
			{
				int *sl = m_pPlayer->slot3;
				if (sl)
				{
					for (int z = 0; z < NUM_PUPS; ++z)
					m_pPlayer->Skills[z] = sl[z];
				}
				Server()->SetClientName(m_pPlayer->GetCID(), m_pPlayer->oname);
				free(m_pPlayer->oname);
				m_pPlayer->oname = NULL;
			}
			str_format(bBuf, 128, "1on1 mode %s", (m_pPlayer->is1on1) ? "ON" : "OFF");
			GameServer()->SendChatTarget(m_pPlayer->GetCID(), bBuf);
			}
		}
		else if (CollisonMate >= TILE_BOOST_L && CollisonMate <= TILE_BOOST_U)
		{
			m_Core.m_Vel += GameServer()->Collision()->boost_accel(CollisonMate);
		}
		else if (CollisonMate == TILE_COLFRZ_RESET)
		{					
		if (lastcolfrz + REFREEZE_INTERVAL_TICKS < Server()->Tick())
		{
			if (m_pPlayer->forcecolor)
			{
				m_pPlayer->forcecolor = 0;
				m_pPlayer->m_TeeInfos.m_UseCustomColor = (m_pPlayer->forcecolor) ? 1 : m_pPlayer->origusecustcolor;
				m_pPlayer->m_TeeInfos.m_ColorBody = (m_pPlayer->forcecolor) ? m_pPlayer->forcecolor : m_pPlayer->origbodycolor;
				m_pPlayer->m_TeeInfos.m_ColorFeet = (m_pPlayer->forcecolor) ? m_pPlayer->forcecolor : m_pPlayer->origfeetcolor;
				GameServer()->m_pController->OnPlayerInfoChange(m_pPlayer);
			}
		}
	}
	else if (CollisonMate >= TILE_PUP_JUMP && CollisonMate <= TILE_PUP_EPICNINJA) {
		int tmp = CollisonMate - TILE_PUP_JUMP;
		if ((LastUpdate + Server()->TickSpeed()) < Server()->Tick())
		{
			LastUpdate = Server()->Tick();
			if (m_pPlayer->is1on1)
			{
				GameServer()->SendChatTarget(m_pPlayer->GetCID(), "leave 1on1 mode first!");
			}
			if ((lastepicninja +  Server()->TickSpeed()) > Server()->Tick())
			{
				GameServer()->SendChatTarget(m_pPlayer->GetCID(), "bad luck...");
			}
			else
			{
				if (m_pPlayer->Skills[tmp] < g_Config.m_MaxPowerUps)
				{
					m_pPlayer->Skills[tmp]++;
					TellPowerUpInfo(m_pPlayer->GetCID(), tmp);
				}
			}
		}
	}
	else if (CollisonMate == TILE_PUP_RESET)
	{
		if ((LastUpdate + (Server()->TickSpeed() >> 2)) < Server()->Tick())
		{
			LastUpdate = Server()->Tick();
			for (int z = 0; z < NUM_PUPS; ++z)
			{
				m_pPlayer->Skills[z] = 0;
			}
			GameServer()->SendChatTarget(m_pPlayer->GetCID(), "select new powerups!");
		}
	}
	else if (CollisonMate >= TILE_TPORT_FIRST && CollisonMate <= TILE_TPORT_LAST)
	{
		int tmp = CollisonMate-TILE_TPORT_FIRST;
		if (tmp&1)
		{
			m_Core.m_HookedPlayer = -1;
			m_Core.m_HookState = HOOK_RETRACTED;
			m_Core.m_TriggeredEvents |= COREEVENT_HOOK_RETRACT;
			m_Core.m_HookPos = m_Core.m_Pos;
			m_Core.m_Pos = GameServer()->Collision()->GetTeleDest(tmp>>1);
		}
	}
	else
	{
		wasout = 1;
	}

	// handle death-tiles
	int a,b,c,d;

	if(((a=GameServer()->Collision()->GetCollisionAt(m_Pos.x+m_ProximityRadius/3.f, m_Pos.y-m_ProximityRadius/3.f)) <= 5 && (a&CCollision::COLFLAG_DEATH)) ||
		((b=GameServer()->Collision()->GetCollisionAt(m_Pos.x+m_ProximityRadius/3.f, m_Pos.y+m_ProximityRadius/3.f)) <= 5 && (b&CCollision::COLFLAG_DEATH)) ||
		((c=GameServer()->Collision()->GetCollisionAt(m_Pos.x-m_ProximityRadius/3.f, m_Pos.y-m_ProximityRadius/3.f)) <= 5 && (c&CCollision::COLFLAG_DEATH)) ||
		((d=GameServer()->Collision()->GetCollisionAt(m_Pos.x-m_ProximityRadius/3.f, m_Pos.y+m_ProximityRadius/3.f)) <= 5 && (d&CCollision::COLFLAG_DEATH)))
	{
		Die(m_pPlayer->GetCID(), WEAPON_WORLD);
	}

	// kill player when leaving gamelayer
	if((int)m_Pos.x/32 < -200 || (int)m_Pos.x/32 > GameServer()->Collision()->GetWidth()+200 ||
		(int)m_Pos.y/32 < -200 || (int)m_Pos.y/32 > GameServer()->Collision()->GetHeight()+200)
	{
		Die(m_pPlayer->GetCID(), WEAPON_WORLD);
	}

	// handle Weapons
	HandleWeapons();

	// Previnput
	m_PrevInput = m_Input;
	return;
}

void CCharacter::TickDefered()
{
	// advance the dummy
	{
		CWorldCore TempWorld;
		m_ReckoningCore.Init(&TempWorld, GameServer()->Collision());
		m_ReckoningCore.Tick(false);
		m_ReckoningCore.Move();
		m_ReckoningCore.Quantize();
	}

	//lastsentcore
	vec2 StartPos = m_Core.m_Pos;
	vec2 StartVel = m_Core.m_Vel;
	bool StuckBefore = GameServer()->Collision()->TestBox(m_Core.m_Pos, vec2(28.0f, 28.0f));

	m_Core.Move();
	bool StuckAfterMove = GameServer()->Collision()->TestBox(m_Core.m_Pos, vec2(28.0f, 28.0f));
	m_Core.Quantize();
	bool StuckAfterQuant = GameServer()->Collision()->TestBox(m_Core.m_Pos, vec2(28.0f, 28.0f));
	m_Pos = m_Core.m_Pos;

	if(!StuckBefore && (StuckAfterMove || StuckAfterQuant))
	{
		// Hackish solution to get rid of strict-aliasing warning
		union
		{
			float f;
			unsigned u;
		}StartPosX, StartPosY, StartVelX, StartVelY;

		StartPosX.f = StartPos.x;
		StartPosY.f = StartPos.y;
		StartVelX.f = StartVel.x;
		StartVelY.f = StartVel.y;

		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "STUCK!!! %d %d %d %f %f %f %f %x %x %x %x",
			StuckBefore,
			StuckAfterMove,
			StuckAfterQuant,
			StartPos.x, StartPos.y,
			StartVel.x, StartVel.y,
			StartPosX.u, StartPosY.u,
			StartVelX.u, StartVelY.u);
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
	}

	int Events = m_Core.m_TriggeredEvents;
	int Mask = CmaskAllExceptOne(m_pPlayer->GetCID());

	if(Events&COREEVENT_GROUND_JUMP) GameServer()->CreateSound(m_Pos, SOUND_PLAYER_JUMP, Mask);

	if(Events&COREEVENT_HOOK_ATTACH_PLAYER) GameServer()->CreateSound(m_Pos, SOUND_HOOK_ATTACH_PLAYER, CmaskAll());
	if(Events&COREEVENT_HOOK_ATTACH_GROUND) GameServer()->CreateSound(m_Pos, SOUND_HOOK_ATTACH_GROUND, Mask);
	if(Events&COREEVENT_HOOK_HIT_NOHOOK) GameServer()->CreateSound(m_Pos, SOUND_HOOK_NOATTACH, Mask);


	if(m_pPlayer->GetTeam() == TEAM_SPECTATORS)
	{
		m_Pos.x = m_Input.m_TargetX;
		m_Pos.y = m_Input.m_TargetY;
	}

	// update the m_SendCore if needed
	{
		CNetObj_Character Predicted;
		CNetObj_Character Current;
		mem_zero(&Predicted, sizeof(Predicted));
		mem_zero(&Current, sizeof(Current));
		m_ReckoningCore.Write(&Predicted);
		m_Core.Write(&Current);

		// only allow dead reackoning for a top of 3 seconds
		if(m_Core.forceupdate || (m_ReckoningTick+Server()->TickSpeed()*3 < Server()->Tick() || mem_comp(&Predicted, &Current, sizeof(CNetObj_Character)) != 0))
		{
			m_ReckoningTick = Server()->Tick();
			m_SendCore = m_Core;
			m_ReckoningCore = m_Core;
		}
	}
}

bool CCharacter::IncreaseHealth(int Amount)
{
	if(m_Health >= 10)
		return false;
	m_Health = clamp(m_Health+Amount, 0, 10);
	return true;
}

bool CCharacter::IncreaseArmor(int Amount)
{
	if(m_Armor >= 10)
		return false;
	m_Armor = clamp(m_Armor+Amount, 0, 10);
	return true;
}

bool CCharacter::Freeze(int ticks)
{
	if (ticks <= 1)
		return false;
	if (frz_tick > 0)//already frozen
	{
		if (frz_tick + REFREEZE_INTERVAL_TICKS > Server()->Tick())
		return true;
      	}
	else
	{
		frz_start=Server()->Tick();
		epicninjaannounced=0;
		lastepicninja=Server()->Tick()-5*Server()->TickSpeed();
	}
	frz_tick=Server()->Tick();
	frz_time=ticks;
	m_Ninja.m_ActivationTick = Server()->Tick();
	m_aWeapons[WEAPON_NINJA].m_Got = true;
	m_aWeapons[WEAPON_NINJA].m_Ammo = -1;
	if (m_ActiveWeapon != WEAPON_NINJA)
	{
		SetWeapon(WEAPON_NINJA);
	}
	return true;
}

bool CCharacter::Unfreeze()
{
	m_Ninja.m_CurrentMoveTime=-1;//prevent magic teleport when unfreezing while epic ninja
	if (frz_time > 0)
	{
		frz_tick = frz_time = frz_start = 0;
 		m_aWeapons[WEAPON_NINJA].m_Got = false;
		CanFire = true;
		if(m_LastWeapon < 0 || m_LastWeapon >= NUM_WEAPONS || m_LastWeapon  == WEAPON_NINJA || (!m_aWeapons[m_LastWeapon].m_Got)) m_LastWeapon = WEAPON_HAMMER;
		SetWeapon(m_LastWeapon);
		epicninjaannounced=0;
		return true;
	}
	return false;

return true;
}

void CCharacter::TellPowerUpInfo(int ClientID, int Skill)
{
	static char bBuf[512];
	switch(Skill)
	{
		case PUP_JUMP:
			str_format(bBuf, 128, "You got an extra jump");
			break;
		case PUP_HAMMER:
			str_format(bBuf, 128, "Hammer power increased");
			break;
		case PUP_LFREEZE:
			str_format(bBuf, 128, "Enemy freeze time increased");
			break;
		case PUP_SFREEZE:
			str_format(bBuf, 128, "Own freeze time shortened");
			break;
		case PUP_HOOKDUR:
			str_format(bBuf, 128, "Hook duration increased");
			break;
		case PUP_HOOKLEN:
                       str_format(bBuf, 128, "Hook length extended");
			break;
		case PUP_WALKSPD:
                       str_format(bBuf, 128, "Walk speed increased");
			break;
		case PUP_EPICNINJA:
                       str_format(bBuf, 128, "Freeze attack");
			break;
		default:
			str_format(bBuf, 128, "Bug! Contact an admin!");
			break;
	}
	GameServer()->SendChatTarget(ClientID, bBuf);
}

void CCharacter::Die(int Killer, int Weapon)
{
	// we got to wait 0.5 secs before respawning
	m_pPlayer->m_RespawnTick = Server()->Tick()+Server()->TickSpeed()/2;
	int ModeSpecial = GameServer()->m_pController->OnCharacterDeath(this, GameServer()->m_apPlayers[Killer], Weapon);

	// send the kill message
	CNetMsg_Sv_KillMsg Msg;
	Msg.m_Killer = Killer;
	Msg.m_Victim = m_pPlayer->GetCID();
	Msg.m_Weapon = Weapon;
	Msg.m_ModeSpecial = ModeSpecial;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);

	// a nice sound
	GameServer()->CreateSound(m_Pos, SOUND_PLAYER_DIE);

	// this is for auto respawn after 3 secs
//	m_pPlayer->m_DieTick = Server()->Tick();
	m_Alive = false;
	GameServer()->m_World.RemoveEntity(this);
	GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] = 0;
	GameServer()->CreateDeath(m_Pos, m_pPlayer->GetCID());
}

bool CCharacter::TakeDamage(vec2 Force, int Dmg, int From, int Weapon)
{
	m_Core.m_Vel += Force;
	
	/*if(GameServer()->m_pController->IsFriendlyFire(m_pPlayer->GetCID(), From) && !g_Config.m_SvTeamdamage)

	if(GameServer()->m_pController->IsFriendlyFire(m_pPlayer->GetCID(), From) && !g_Config.m_SvTeamdamage)
		return false;

	// m_pPlayer only inflicts half damage on self
	if(From == m_pPlayer->GetCID())
		Dmg = max(1, Dmg/2);

	m_DamageTaken++;

	// create healthmod indicator
	if(Server()->Tick() < m_DamageTakenTick+25)
	{
		// make sure that the damage indicators doesn't group together
		GameServer()->CreateDamageInd(m_Pos, m_DamageTaken*0.25f, Dmg);
	}
	else
	{
		m_DamageTaken = 0;
		GameServer()->CreateDamageInd(m_Pos, 0, Dmg);
	}*/

	if(Weapon == WEAPON_NINJA)
	{
		Freeze(ft + GameServer()->m_apPlayers[From]->Skills[PUP_LFREEZE] * (Server()->TickSpeed()>>1));
	}

	/*if(Dmg)
	{
		if(m_Armor)
		{
			if(Dmg > 1)
			{
				m_Health--;
				Dmg--;
			}

			if(Dmg > m_Armor)
			{
				Dmg -= m_Armor;
				m_Armor = 0;
			}
			else
			{
				m_Armor -= Dmg;
				Dmg = 0;
			}
		}

		m_Health -= Dmg;
	}

	m_DamageTakenTick = Server()->Tick();

	// do damage Hit sound
	if(From >= 0 && From != m_pPlayer->GetCID() && GameServer()->m_apPlayers[From])
		*/GameServer()->CreateSound(GameServer()->m_apPlayers[From]->m_ViewPos, SOUND_HIT, CmaskOne(From));
/*
	{
		int Mask = CmaskOne(From);
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() == TEAM_SPECTATORS && GameServer()->m_apPlayers[i]->m_SpectatorID == From)
				Mask |= CmaskOne(i);
		}
		GameServer()->CreateSound(GameServer()->m_apPlayers[From]->m_ViewPos, SOUND_HIT, Mask);
	}

	// check for death
	if(m_Health <= 0)
	{
		Die(From, Weapon);

		// set attacker's face to happy (taunt!)
		if (From >= 0 && From != m_pPlayer->GetCID() && GameServer()->m_apPlayers[From])
		{
			CCharacter *pChr = GameServer()->m_apPlayers[From]->GetCharacter();
			if (pChr)
			{
				pChr->m_EmoteType = EMOTE_HAPPY;
				pChr->m_EmoteStop = Server()->Tick() + Server()->TickSpeed();
			}
		}

		return false;
	}

	if (Dmg > 2)
		GameServer()->CreateSound(m_Pos, SOUND_PLAYER_PAIN_LONG);
	else
		GameServer()->CreateSound(m_Pos, SOUND_PLAYER_PAIN_SHORT);
*/
	m_EmoteType = EMOTE_PAIN;
	m_EmoteStop = Server()->Tick() + 500 * Server()->TickSpeed() / 1000;
	
	return true;
}

void CCharacter::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	CNetObj_Character *pCharacter = static_cast<CNetObj_Character *>(Server()->SnapNewItem(NETOBJTYPE_CHARACTER, m_pPlayer->GetCID(), sizeof(CNetObj_Character)));
	if(!pCharacter)
		return;

	// write down the m_Core
	if(!m_ReckoningTick || GameServer()->m_World.m_Paused)
	{
		// no dead reckoning when paused because the client doesn't know
		// how far to perform the reckoning
		pCharacter->m_Tick = 0;
		m_Core.Write(pCharacter);
	}
	else
	{
		pCharacter->m_Tick = m_ReckoningTick;
		m_SendCore.Write(pCharacter);
	}

	// set emote
	if (m_EmoteStop < Server()->Tick())
	{
		m_EmoteType = m_DefEmote;
		m_EmoteStop = -1;
	}

	pCharacter->m_Emote = m_EmoteType;

	pCharacter->m_AmmoCount = 0;
	pCharacter->m_Health = 0;
	pCharacter->m_Armor = 0;

	pCharacter->m_Weapon = m_ActiveWeapon;
	pCharacter->m_AttackTick = m_AttackTick;

	pCharacter->m_Direction = m_Input.m_Direction;

	if(m_pPlayer->GetCID() == SnappingClient || SnappingClient == -1 ||
		(!g_Config.m_SvStrictSpectateMode && m_pPlayer->GetCID() == GameServer()->m_apPlayers[SnappingClient]->m_SpectatorID))
	{
		pCharacter->m_Health = m_Health;
		pCharacter->m_Armor = m_Armor;
		if(m_aWeapons[m_ActiveWeapon].m_Ammo > 0)
			pCharacter->m_AmmoCount = m_aWeapons[m_ActiveWeapon].m_Ammo;
	}

	if(pCharacter->m_Emote == EMOTE_NORMAL)
	{
		if(250 - ((Server()->Tick() - m_LastAction)%(250)) < 5)
			pCharacter->m_Emote = EMOTE_BLINK;
	}

	pCharacter->m_PlayerFlags = GetPlayer()->m_PlayerFlags;
}

void CCharacter::FreezeTik()
{
	ft = Server()->TickSpeed() * 3;
	hooked = lasthookedat > lasthammeredat;
	by = hooked ? lasthookedby : lasthammeredby;
	add=0;
	if ((wasout || frz_tick == 0) && (((lasthookedat + (Server()->TickSpeed()<<1)) > Server()->Tick()) || ((lasthammeredat + Server()->TickSpeed()) > Server()->Tick())))
	{
		if (GameServer()->m_apPlayers[by] && GameServer()->m_apPlayers[by]->GetCharacter())
		{
			add = GameServer()->m_apPlayers[by]->Skills[PUP_LFREEZE];
		}
		blockedby=by;
		if (blockedby>=0) blocktime=ft+(add * (Server()->TickSpeed()>>1));
		} else {
			if (frz_tick==0)
			{
				blockedby=-1;
			}
			if (blockedby>=0)
			ft=blocktime;
		}
		add -=m_pPlayer->Skills[PUP_SFREEZE];
		ft += (add * (Server()->TickSpeed()>>1));
		wasout=0;
	}
