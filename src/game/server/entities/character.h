/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_CHARACTER_H
#define GAME_SERVER_ENTITIES_CHARACTER_H

#include <game/server/entity.h>
#include <game/generated/server_data.h>
#include <game/generated/protocol.h>

#include <game/gamecore.h>

#define REFREEZE_INTERVAL_TICKS (Server()->TickSpeed()>>1)

#define COL_BLUE 9502541
#define COL_GREEN 5373773
#define COL_WHITE 16777215
#define COL_GREY 1
#define COL_RED 65280
#define COL_YELLOW 2883328
#define COL_PINK 14090075

enum
{
	WEAPON_GAME = -3, // team switching etc
	WEAPON_SELF = -2, // console kill command
	WEAPON_WORLD = -1, // death tiles etc
};

class CCharacter : public CEntity
{
	MACRO_ALLOC_POOL_ID()
	
public:
	//character's size
	static const int ms_PhysSize = 28;

	CCharacter(CGameWorld *pWorld);

	virtual void Reset();
	virtual void Destroy();
	virtual void Tick();
	virtual void TickDefered();
	virtual void Snap(int SnappingClient);
		
	bool IsGrounded();
	
	void SetWeapon(int W);
	void HandleWeaponSwitch();
	void DoWeaponSwitch();
	
	void HandleWeapons();
	void HandleNinja();

	void OnPredictedInput(CNetObj_PlayerInput *pNewInput);
	void OnDirectInput(CNetObj_PlayerInput *pNewInput);
	void FireWeapon();

	void Die(int Killer, int Weapon);
	bool TakeDamage(vec2 Force, int Dmg, int From, int Weapon);	

	bool Spawn(class CPlayer *pPlayer, vec2 Pos);
	bool Remove();
	
	bool IncreaseHealth(int Amount);
	bool IncreaseArmor(int Amount);
	
	bool GiveWeapon(int Weapon, int Ammo);
	void GiveNinja();
	
	void SetEmote(int Emote, int Tick);
	
	bool IsAlive() const { return m_Alive; }
	class CPlayer *GetPlayer() { return m_pPlayer; }
	void SetEmoteStop(int EmoteStop) { m_EmoteStop = EmoteStop; };
	
	bool Freeze(int time);
	bool Unfreeze();
	void SetEmoteType(int EmoteType) { m_EmoteType = EmoteType; };
	int m_EmoteStop;
	int m_DefEmote;
	int m_DefEmoteReset;
	int ft;
	int m_MuteInfo;
	int frz_tick;//will get updated on every REFREEZE_INTERVAL ticks
	int by;
private:
	void TellPowerUpInfo(int ClientID, int Skill);

	// player controlling this character
	class CPlayer *m_pPlayer;
	
	bool m_Alive;
	int m_EmoteType;
	// weapon info
	CEntity *m_apHitObjects[10];
	int m_NumObjectsHit;
	
	struct WeaponStat
	{
		int m_AmmoRegenStart;
		int m_Ammo;
		int m_Ammocost;
		bool m_Got;

	} m_aWeapons[NUM_WEAPONS];
	
	int m_ActiveWeapon;
	int m_LastWeapon;
	int m_QueuedWeapon;
	
	int m_ReloadTimer;
	int m_AttackTick;
	
	int m_DamageTaken;

	
	// last tick that the player took any action ie some input
	int m_LastAction;

	// these are non-heldback inputs
	CNetObj_PlayerInput m_LatestPrevInput;
	CNetObj_PlayerInput m_LatestInput;

	// input	
	CNetObj_PlayerInput m_PrevInput;
	CNetObj_PlayerInput m_Input;
	int m_NumInputs;
	int m_Jumped;
	
	int m_DamageTakenTick;

	int m_Health;
	int m_Armor;

	int frz_time;//will be higher when blocker has lfreeze, for instance
	int frz_start;//will be set on the first freeze

	int lastcolfrz;
	int lastloadsave;
	int lasthammeredby, lasthammeredat;
	int lasthookedby, lasthookedat;
	int LastUpdate;
	int wasout;
	int lastepicninja;
	int epicninjaannounced;
	int blockedby;
	int blocktime;
	int add;
	int hooked;
	vec2 epicninjaoldpos;

	// ninja
	struct
	{
		vec2 m_ActivationDir;
		int m_ActivationTick;
		int m_CurrentMoveTime;
		
	} m_Ninja;

	int m_PlayerState;// if the client is chatting, accessing a menu or so

	// the player core for the physics	
	CCharacterCore m_Core;
	
	// info for dead reckoning
	int m_ReckoningTick; // tick that we are performing dead reckoning From
	CCharacterCore m_SendCore; // core that we should send
	CCharacterCore m_ReckoningCore; // the dead reckoning core

};

#endif
