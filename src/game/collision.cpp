/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/system.h>
#include <base/math.h>
#include <base/vmath.h>

#include <math.h>
#include <engine/map.h>
#include <engine/kernel.h>

#include <game/mapitems.h>
#include <game/layers.h>
#include <game/collision.h>

CCollision::CCollision()
{
	m_pTiles = 0;
	m_Width = 0;
	m_Height = 0;
	m_pLayers = 0;
}

void CCollision::Init(class CLayers *pLayers)
{
	m_pLayers = pLayers;
	m_Width = m_pLayers->GameLayer()->m_Width;
	m_Height = m_pLayers->GameLayer()->m_Height;
	m_pTiles = static_cast<CTile *>(m_pLayers->Map()->GetData(m_pLayers->GameLayer()->m_Data));
	
	int tpnum=(TILE_TPORT_LAST-TILE_TPORT_FIRST+1)>>1;
	int *destcount=(int*)malloc(sizeof(int)*tpnum);
	dc=(int*)malloc(sizeof(int)*tpnum);
	dest=(int**)malloc(sizeof(int*)*tpnum);
	for(int z=0;z<tpnum;++z) destcount[z]=dc[z]=0;
	for(int i = 0; i < m_Width*m_Height; i++) //tport first
	{
		int index = m_pTiles[i].m_Index;
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
	
	for(int i = 0; i < m_Width*m_Height; i++)
	{
		int Index = m_pTiles[i].m_Index;
		
		if(Index > TILE_CUSTOM_END)
			continue;
		
		switch(Index)
		{
		case TILE_DEATH:
			m_pTiles[i].m_Index = COLFLAG_DEATH;
			break;
		case TILE_SOLID:
			m_pTiles[i].m_Index = COLFLAG_SOLID;
			break;
		case TILE_NOHOOK:
			m_pTiles[i].m_Index = COLFLAG_SOLID|COLFLAG_NOHOOK;
			break;
		default:
			/*m_pTiles[i].m_Index = 0*/;
		}

		if(Index >= TILE_TPORT_FIRST && Index <= TILE_TPORT_LAST && !(Index&1)) {
			int tind = ((Index-TILE_TPORT_FIRST) >> 1);
			dest[tind][--destcount[tind]]=i;
		} else if (Index >= TILE_CUSTOM_END)
			m_pTiles[i].m_Index = 0;
	}
	free(destcount);
}

int CCollision::GetTile(int X, int Y)
{
	int nx = clamp(X>>5, 0, m_Width-1);
	int ny = clamp(Y>>5, 0, m_Height-1);
	
	return m_pTiles[ny*m_Width+nx].m_Index > TILE_CUSTOM_END ? 0 : m_pTiles[ny*m_Width+nx].m_Index;
}

bool CCollision::IsTileSolid(int X, int Y)
{
	int i = GetTile(X,Y);
	return (i<=5) && (i&COLFLAG_SOLID);
}

vec2 CCollision::GetTeleDest(int tind)
{
	if (dc[tind]) {
		int r = rand() % dc[tind];
		int x = (dest[tind][r] % m_Width) << 5;
		int y = (dest[tind][r] / m_Width) << 5;
		return vec2((float)x + 16.0, (float)y + 16.0);
	} else return vec2(0, 0);
}

vec2 CCollision::boost_accel(int index)
{
	if (index == TILE_BOOST_L) return vec2(-15, 0);
	else if (index == TILE_BOOST_R) return vec2(15, 0);
	else if (index == TILE_BOOST_D) return vec2(0, 15);
	else if (index == TILE_BOOST_U) return vec2(0, -15);

	return vec2(0, 0);
}

// TODO: rewrite this smarter!
int CCollision::IntersectLine(vec2 Pos0, vec2 Pos1, vec2 *pOutCollision, vec2 *pOutBeforeCollision)
{
	float D = distance(Pos0, Pos1);
	int End(D+1);
	vec2 Last = Pos0;
	
	for(int i = 0; i < End; i++)
	{
		float A = i/D;
		vec2 Pos = mix(Pos0, Pos1, A);
		if(CheckPoint(Pos.x, Pos.y))
		{
			if(pOutCollision)
				*pOutCollision = Pos;
			if(pOutBeforeCollision)
				*pOutBeforeCollision = Last;
			return GetCollisionAt(Pos.x, Pos.y);
		}
		Last = Pos;
	}
	if(pOutCollision)
		*pOutCollision = Pos1;
	if(pOutBeforeCollision)
		*pOutBeforeCollision = Pos1;
	return 0;
}

// TODO: OPT: rewrite this smarter!
void CCollision::MovePoint(vec2 *pInoutPos, vec2 *pInoutVel, float Elasticity, int *pBounces)
{
	if(pBounces)
		*pBounces = 0;
	
	vec2 Pos = *pInoutPos;
	vec2 Vel = *pInoutVel;
	if(CheckPoint(Pos + Vel))
	{
		int Affected = 0;
		if(CheckPoint(Pos.x + Vel.x, Pos.y))
		{
			pInoutVel->x *= -Elasticity;
			if(pBounces)
				(*pBounces)++;			
			Affected++;
		}

		if(CheckPoint(Pos.x, Pos.y + Vel.y))
		{
			pInoutVel->y *= -Elasticity;
			if(pBounces)
				(*pBounces)++;			
			Affected++;
		}
		
		if(Affected == 0)
		{
			pInoutVel->x *= -Elasticity;
			pInoutVel->y *= -Elasticity;
		}
	}
	else
	{
		*pInoutPos = Pos + Vel;
	}
}

bool CCollision::TestBox(vec2 Pos, vec2 Size)
{
	Size *= 0.5f;
	if(CheckPoint(Pos.x-Size.x, Pos.y-Size.y))
		return true;
	if(CheckPoint(Pos.x+Size.x, Pos.y-Size.y))
		return true;
	if(CheckPoint(Pos.x-Size.x, Pos.y+Size.y))
		return true;
	if(CheckPoint(Pos.x+Size.x, Pos.y+Size.y))
		return true;
	return false;
}

void CCollision::MoveBox(vec2 *pInoutPos, vec2 *pInoutVel, vec2 Size, float Elasticity)
{
	// do the move
	vec2 Pos = *pInoutPos;
	vec2 Vel = *pInoutVel;
	
	float Distance = length(Vel);
	int Max = (int)Distance;
	
	if(Distance > 0.00001f)
	{
		//vec2 old_pos = pos;
		float Fraction = 1.0f/(float)(Max+1);
		for(int i = 0; i <= Max; i++)
		{
			//float amount = i/(float)max;
			//if(max == 0)
				//amount = 0;
			
			vec2 NewPos = Pos + Vel*Fraction; // TODO: this row is not nice
			
			if(TestBox(vec2(NewPos.x, NewPos.y), Size))
			{
				int Hits = 0;
				
				if(TestBox(vec2(Pos.x, NewPos.y), Size))
				{
					NewPos.y = Pos.y;
					Vel.y *= -Elasticity;
					Hits++;
				}
				
				if(TestBox(vec2(NewPos.x, Pos.y), Size))
				{
					NewPos.x = Pos.x;
					Vel.x *= -Elasticity;
					Hits++;
				}
				
				// neither of the tests got a collision.
				// this is a real _corner case_!
				if(Hits == 0)
				{
					NewPos.y = Pos.y;
					Vel.y *= -Elasticity;
					NewPos.x = Pos.x;
					Vel.x *= -Elasticity;
				}
			}
			
			Pos = NewPos;
		}
	}
	
	*pInoutPos = Pos;
	*pInoutVel = Vel;
}
