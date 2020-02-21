// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// $Log:$
//
// DESCRIPTION:
//	Do all the WAD I/O, get map description,
//	set up initial state and misc. LUTs.
//
//-----------------------------------------------------------------------------

static const char
rcsid[] = "$Id: p_setup.c,v 1.5 1997/02/03 22:45:12 b1 Exp $";

#include <windows.h>
#include <gl/gl.h>
#include <gl/glu.h>
#include <malloc.h>
#include <math.h>

#include "z_zone.h"

#include "m_swap.h"
#include "m_bbox.h"

#include "g_game.h"

#include "i_system.h"
#include "w_wad.h"

#include "doomdef.h"
#include "p_local.h"

#include "s_sound.h"

#include "doomstat.h"
#include "sys_win.h"
#include "gldefs.h"

#include "info.h"

#include "gconsole.h"
#include "doomlib.h"
#include "mathlib.h"

void lfprintf(char *message, ... );

void	P_SpawnMapThing (mapthing_t*	mthing);

//
// MAP related Lookup tables.
// Store VERTEXES, LINEDEFS, SIDEDEFS, etc.
//
int		numvertexes;
vertex_t*	vertexes;

int		numsegs;
seg_t*		segs;

int		numsectors;
sector_t*	sectors;
int     lastsectors = 0;

int		numsubsectors;
subsector_t*	subsectors;

int		numnodes;
node_t*		nodes;

int		numlines;
line_t*		lines;

int		numsides;
side_t*		sides;


// BLOCKMAP
// Created from axis aligned bounding box
// of the map, a rectangular array of
// blocks of size ...
// Used to speed up collision detection
// by spatial subdivision in 2D.
//
// Blockmap size.
int		bmapwidth;
int		bmapheight;	// size in mapblocks
short*		blockmap;	// int for larger maps
// offsets in blockmap are from here
short*		blockmaplump;		
// origin of block map
fixed_t		bmaporgx;
fixed_t		bmaporgy;
// for thing chains
mobj_t**	blocklinks;		


// REJECT
// For fast sight rejection.
// Speeds up enemy AI by skipping detailed
//  LineOf Sight calculation.
// Without special effect, this could be
//  used as a PVS lookup as well.
//
byte*		rejectmatrix;


// Maintain single and multi player starting spots.
#define MAX_DEATHMATCH_STARTS	10

mapthing_t	deathmatchstarts[MAX_DEATHMATCH_STARTS];
mapthing_t*	deathmatch_p;
mapthing_t	playerstarts[MAXPLAYERS];


dboolean SpritePresent[NUMMOBJTYPES];

//
// P_LoadVertexes
//
void P_LoadVertexes (int lump)
{
    byte*		data;
    int			i;
    mapvertex_t*	ml;
    vertex_t*		li;

    // Determine number of lumps:
    //  total lump length / vertex record length.
    numvertexes = W_LumpLength (lump) / sizeof(mapvertex_t);

    // Allocate zone memory for buffer.
    vertexes = Z_Malloc (numvertexes*sizeof(vertex_t),PU_LEVEL,0);	

    // Load data into cache.
    data = W_CacheLumpNum (lump,PU_STATIC);
	
    ml = (mapvertex_t *)data;
    li = vertexes;

    // Copy and convert vertex coordinates,
    // internal representation as fixed.
    for (i=0 ; i<numvertexes ; i++, li++, ml++)
    {
	li->x = SHORT(ml->x)<<FRACBITS;
	li->y = SHORT(ml->y)<<FRACBITS;
    }

    // Free buffer memory.
    Z_Free (data);
}



//
// P_LoadSegs
//
void P_LoadSegs (int lump)
{
    byte*		data;
    int			i;
    mapseg_t*		ml;
    seg_t*		li;
    line_t*		ldef;
    int			linedef;
    int			side;
	
    numsegs = W_LumpLength (lump) / sizeof(mapseg_t);
    segs = Z_Malloc (numsegs*sizeof(seg_t),PU_LEVEL,0);	
    memset (segs, 0, numsegs*sizeof(seg_t));
    data = W_CacheLumpNum (lump,PU_STATIC);
	
    ml = (mapseg_t *)data;
    li = segs;
    for (i=0 ; i<numsegs ; i++, li++, ml++)
    {
	li->v1 = &vertexes[SHORT(ml->v1)];
	li->v2 = &vertexes[SHORT(ml->v2)];
					
	li->angle = (SHORT(ml->angle))<<16;
	li->offset = (SHORT(ml->offset))<<16;
	linedef = SHORT(ml->linedef);
	ldef = &lines[linedef];
	li->linedef = ldef;
	side = SHORT(ml->side);
	li->sidedef = &sides[ldef->sidenum[side]];
	li->frontsector = sides[ldef->sidenum[side]].sector;
	if (ldef-> flags & ML_TWOSIDED)
	    li->backsector = sides[ldef->sidenum[side^1]].sector;
	else
	    li->backsector = 0;
    }
	
    Z_Free (data);
}


//
// P_LoadSubsectors
//
void P_LoadSubsectors (int lump)
{
    byte*		data;
    int			i;
    mapsubsector_t*	ms;
    subsector_t*	ss;
	
    numsubsectors = W_LumpLength (lump) / sizeof(mapsubsector_t);
    subsectors = Z_Malloc (numsubsectors*sizeof(subsector_t),PU_LEVEL,0);	
    data = W_CacheLumpNum (lump,PU_STATIC);
	
    ms = (mapsubsector_t *)data;
    memset (subsectors,0, numsubsectors*sizeof(subsector_t));
    ss = subsectors;
    
    for (i=0 ; i<numsubsectors ; i++, ss++, ms++)
    {
	ss->numlines = SHORT(ms->numsegs);
	ss->firstline = SHORT(ms->firstseg);
    }
	
    Z_Free (data);
}



//
// P_LoadSectors
//
void P_LoadSectors (int lump)
{
    byte*		data;
    int			i;
    mapsector_t*	ms;
    sector_t*		ss;
	
    numsectors = W_LumpLength (lump) / sizeof(mapsector_t);
    sectors = Z_Malloc (numsectors*sizeof(sector_t),PU_LEVEL,0);	
    memset (sectors, 0, numsectors*sizeof(sector_t));
    data = W_CacheLumpNum (lump,PU_STATIC);
	
    ms = (mapsector_t *)data;
    ss = sectors;
    for (i=0 ; i<numsectors ; i++, ss++, ms++)
    {
	ss->floorheight = SHORT(ms->floorheight)<<FRACBITS;
	ss->ceilingheight = SHORT(ms->ceilingheight)<<FRACBITS;
	ss->floorpic = R_FlatNumForName(ms->floorpic);
	ss->ceilingpic = R_FlatNumForName(ms->ceilingpic);
	ss->lightlevel = SHORT(ms->lightlevel);
	ss->special = SHORT(ms->special);
	ss->tag = SHORT(ms->tag);
	ss->thinglist = NULL;
    }
	
    Z_Free (data);
}


//
// P_LoadNodes
//
void P_LoadNodes (int lump)
{
    byte*	data;
    int		i;
    int		j;
    int		k;
    mapnode_t*	mn;
    node_t*	no;
	
    numnodes = W_LumpLength (lump) / sizeof(mapnode_t);
    nodes = Z_Malloc (numnodes*sizeof(node_t),PU_LEVEL,0);	
    data = W_CacheLumpNum (lump,PU_STATIC);
	
    mn = (mapnode_t *)data;
    no = nodes;
    
    for (i=0 ; i<numnodes ; i++, no++, mn++)
    {
	no->x = SHORT(mn->x)<<FRACBITS;
	no->y = SHORT(mn->y)<<FRACBITS;
	no->dx = SHORT(mn->dx)<<FRACBITS;
	no->dy = SHORT(mn->dy)<<FRACBITS;
	for (j=0 ; j<2 ; j++)
	{
	    no->children[j] = SHORT(mn->children[j]);
	    for (k=0 ; k<4 ; k++)
		no->bbox[j][k] = SHORT(mn->bbox[j][k])<<FRACBITS;
	}
    }
	
    Z_Free (data);
}

//
// P_LoadThings
//
void P_LoadThings (int lump)
   {
    byte*		data;
    int			i;
    mapthing_t*		mt;
    int			numthings;
    dboolean		spawn;

    for (i = 0; i < NUMMOBJTYPES; i++)
        SpritePresent[i] = 0;

    data = W_CacheLumpNum (lump,PU_STATIC);
    numthings = W_LumpLength (lump) / sizeof(mapthing_t);
	
//    lfprintf( "Level contains %d things.\n", numthings);

    mt = (mapthing_t *)data;
    for (i=0 ; i<numthings ; i++, mt++)
       {
        spawn = true;

        // Do not spawn cool, new monsters if !commercial
        if ( gamemode != commercial)
           {
            switch(mt->type)
               {
                case 68:	// Arachnotron
                case 64:	// Archvile
                case 88:	// Boss Brain
                case 89:	// Boss Shooter
                case 69:	// Hell Knight
                case 67:	// Mancubus
                case 71:	// Pain Elemental
                case 65:	// Former Human Commando
                case 66:	// Revenant
                case 84:	// Wolf SS
                     spawn = false;
                     break;
               }
           }
        if (spawn == false)
            break;

        // Do spawn all other stuff. 
        mt->x = SHORT(mt->x);
        mt->y = SHORT(mt->y);
        mt->angle = SHORT(mt->angle);
        mt->type = SHORT(mt->type);
        mt->options = SHORT(mt->options);
	
        P_SpawnMapThing(mt);
       }
	
/*
    for (i = 0; i < NUMMOBJTYPES; i++)
       {
        if (SpritePresent[i] > 0)
           {
            lfprintf( "Object present : %d -> %d\n", i, SpritePresent[i]);
           }
       }
*/
    Z_Free (data);
   }


//
// P_LoadLineDefs
// Also counts secret lines for intermissions.
//
void P_LoadLineDefs (int lump)
{
    byte*		data;
    int			i;
    maplinedef_t*	mld;
    line_t*		ld;
    vertex_t*		v1;
    vertex_t*		v2;
	
    numlines = W_LumpLength (lump) / sizeof(maplinedef_t);
    lines = Z_Malloc (numlines*sizeof(line_t),PU_LEVEL,0);
    memset (lines, 0, numlines*sizeof(line_t));
    data = W_CacheLumpNum (lump,PU_STATIC);
	
    mld = (maplinedef_t *)data;
    ld = lines;
    for (i=0 ; i<numlines ; i++, mld++, ld++)
    {
	ld->flags = SHORT(mld->flags);
	ld->special = SHORT(mld->special);
	ld->tag = SHORT(mld->tag);
	v1 = ld->v1 = &vertexes[SHORT(mld->v1)];
	v2 = ld->v2 = &vertexes[SHORT(mld->v2)];
	ld->dx = v2->x - v1->x;
	ld->dy = v2->y - v1->y;
	
	if (!ld->dx)
	    ld->slopetype = ST_VERTICAL;
	else if (!ld->dy)
	    ld->slopetype = ST_HORIZONTAL;
	else
	{
	    if (FixedDiv (ld->dy , ld->dx) > 0)
		ld->slopetype = ST_POSITIVE;
	    else
		ld->slopetype = ST_NEGATIVE;
	}
		
	if (v1->x < v2->x)
	{
	    ld->bbox[BOXLEFT] = v1->x;
	    ld->bbox[BOXRIGHT] = v2->x;
	}
	else
	{
	    ld->bbox[BOXLEFT] = v2->x;
	    ld->bbox[BOXRIGHT] = v1->x;
	}

	if (v1->y < v2->y)
	{
	    ld->bbox[BOXBOTTOM] = v1->y;
	    ld->bbox[BOXTOP] = v2->y;
	}
	else
	{
	    ld->bbox[BOXBOTTOM] = v2->y;
	    ld->bbox[BOXTOP] = v1->y;
	}

	ld->sidenum[0] = SHORT(mld->sidenum[0]);
	ld->sidenum[1] = SHORT(mld->sidenum[1]);

	if (ld->sidenum[0] != -1)
	    ld->frontsector = sides[ld->sidenum[0]].sector;
	else
	    ld->frontsector = 0;

	if (ld->sidenum[1] != -1)
	    ld->backsector = sides[ld->sidenum[1]].sector;
	else
	    ld->backsector = 0;
    }
	
    Z_Free (data);
}

//
// P_LoadSideDefs
//
void P_LoadSideDefs (int lump)
{
    byte*		data;
    int			i;
    mapsidedef_t*	msd;
    side_t*		sd;
	
    numsides = W_LumpLength (lump) / sizeof(mapsidedef_t);
    sides = Z_Malloc (numsides*sizeof(side_t),PU_LEVEL,0);	
    memset (sides, 0, numsides*sizeof(side_t));
    data = W_CacheLumpNum (lump,PU_STATIC);
	
    msd = (mapsidedef_t *)data;
    sd = sides;
    for (i=0 ; i<numsides ; i++, msd++, sd++)
    {
	sd->textureoffset = SHORT(msd->textureoffset)<<FRACBITS;
	sd->rowoffset = SHORT(msd->rowoffset)<<FRACBITS;
	sd->toptexture = R_TextureNumForName(msd->toptexture);
	sd->bottomtexture = R_TextureNumForName(msd->bottomtexture);
	sd->midtexture = R_TextureNumForName(msd->midtexture);
	sd->sector = &sectors[SHORT(msd->sector)];
    sd->sectornumb = msd->sector;
    }

    Z_Free (data);
}


//
// P_LoadBlockMap
//
void P_LoadBlockMap (int lump)
{
    int		i;
    int		count;
	
    blockmaplump = W_CacheLumpNum (lump,PU_LEVEL);
    blockmap = blockmaplump+4;
    count = W_LumpLength (lump)/2;

    for (i=0 ; i<count ; i++)
       blockmaplump[i] = SHORT(blockmaplump[i]);

    bmaporgx = blockmaplump[0]<<FRACBITS;
    bmaporgy = blockmaplump[1]<<FRACBITS;
    bmapwidth = blockmaplump[2];
    bmapheight = blockmaplump[3];
	
    // clear out mobj chains
    count = sizeof(*blocklinks)* bmapwidth*bmapheight;
    blocklinks = Z_Malloc (count,PU_LEVEL, 0);
    memset (blocklinks, 0, count);
}



//
// P_GroupLines
// Builds sector line lists and subsector sector numbers.
// Finds block bounding boxes for sectors.
//
void P_GroupLines (void)
{
    line_t**		linebuffer;
    int			i;
    int			j;
    int			total;
    line_t*		li;
    sector_t*		sector;
    subsector_t*	ss;
    seg_t*		seg;
    fixed_t		bbox[4];
    int			block;
	
    // look up sector number for each subsector
    ss = subsectors;
    for (i=0 ; i<numsubsectors ; i++, ss++)
    {
	seg = &segs[ss->firstline];
	ss->sector = seg->sidedef->sector;
    }

    // count number of lines in each sector
    li = lines;
    total = 0;
    for (i=0 ; i<numlines ; i++, li++)
    {
	total++;
	li->frontsector->linecount++;

	if (li->backsector && li->backsector != li->frontsector)
	{
	    li->backsector->linecount++;
	    total++;
	}
    }
	
    // build line tables for each sector	
    linebuffer = Z_Malloc (total*4, PU_LEVEL, 0);
    sector = sectors;
    for (i=0 ; i<numsectors ; i++, sector++)
    {
	M_ClearBox (bbox);
	sector->lines = linebuffer;
	li = lines;
	for (j=0 ; j<numlines ; j++, li++)
	{
	    if (li->frontsector == sector || li->backsector == sector)
	    {
		*linebuffer++ = li;
		M_AddToBox (bbox, li->v1->x, li->v1->y);
		M_AddToBox (bbox, li->v2->x, li->v2->y);
	    }
	}
	if (linebuffer - sector->lines != sector->linecount)
	    I_Error ("P_GroupLines: miscounted");
			
	// set the degenmobj_t to the middle of the bounding box
	sector->soundorg.x = (bbox[BOXRIGHT]+bbox[BOXLEFT])/2;
	sector->soundorg.y = (bbox[BOXTOP]+bbox[BOXBOTTOM])/2;
		
	// adjust bounding box to map blocks
	block = (bbox[BOXTOP]-bmaporgy+MAXRADIUS)>>MAPBLOCKSHIFT;
	block = block >= bmapheight ? bmapheight-1 : block;
	sector->blockbox[BOXTOP]=block;

	block = (bbox[BOXBOTTOM]-bmaporgy-MAXRADIUS)>>MAPBLOCKSHIFT;
	block = block < 0 ? 0 : block;
	sector->blockbox[BOXBOTTOM]=block;

	block = (bbox[BOXRIGHT]-bmaporgx+MAXRADIUS)>>MAPBLOCKSHIFT;
	block = block >= bmapwidth ? bmapwidth-1 : block;
	sector->blockbox[BOXRIGHT]=block;

	block = (bbox[BOXLEFT]-bmaporgx-MAXRADIUS)>>MAPBLOCKSHIFT;
	block = block < 0 ? 0 : block;
	sector->blockbox[BOXLEFT]=block;
    }
	
}

void  Build3DLevel(void);
void  BuildThingList(void);

//
// P_SetupLevel
//
void
P_SetupLevel
( int		episode,
  int		map,
  int		playermask,
  skill_t	skill)
{
    int		i;
    char	lumpname[9];
    int		lumpnum;
	
    totalkills = totalitems = totalsecret = wminfo.maxfrags = 0;
    wminfo.partime = 180;
    for (i=0 ; i<MAXPLAYERS ; i++)
    {
	players[i].killcount = players[i].secretcount 
	    = players[i].itemcount = 0;
    }

    // Initial height of PointOfView
    // will be set by player think.
    players[consoleplayer].viewz = 1; 

    // Make sure all sounds are stopped before Z_FreeTags.
    S_Start ();			

    
#if 0 // UNUSED
    if (debugfile)
    {
	Z_FreeTags (PU_LEVEL, MAXINT);
	Z_FileDumpHeap (debugfile);
    }
    else
#endif
       {
	Z_FreeTags (PU_LEVEL, PU_PURGELEVEL-1);
       }


    // UNUSED W_Profile ();
    P_InitThinkers ();

    // if working with a devlopment map, reload it
    W_Reload ();			
	   
    // find map name
    if ( gamemode == commercial)
    {
	if (map<10)
	    sprintf (lumpname,"map0%i", map);
	else
	    sprintf (lumpname,"map%i", map);
    }
    else
    {
	lumpname[0] = 'E';
	lumpname[1] = '0' + episode;
	lumpname[2] = 'M';
	lumpname[3] = '0' + map;
	lumpname[4] = 0;
    }

    lumpnum = W_GetNumForName (lumpname);
	
    leveltime = 0;
	
    // note: most of this ordering is important
    P_LoadBlockMap (lumpnum+ML_BLOCKMAP);
    P_LoadVertexes (lumpnum+ML_VERTEXES);
    P_LoadSectors (lumpnum+ML_SECTORS);
    P_LoadSideDefs (lumpnum+ML_SIDEDEFS);

    P_LoadLineDefs (lumpnum+ML_LINEDEFS);
    P_LoadSubsectors (lumpnum+ML_SSECTORS);
    P_LoadNodes (lumpnum+ML_NODES);
    P_LoadSegs (lumpnum+ML_SEGS);
	
    rejectmatrix = W_CacheLumpNum (lumpnum+ML_REJECT,PU_LEVEL);
    P_GroupLines ();

    bodyqueslot = 0;
    deathmatch_p = deathmatchstarts;
    P_LoadThings (lumpnum+ML_THINGS);
    
    // if deathmatch, randomly spawn the active players
    if (deathmatch)
    {
	for (i=0 ; i<MAXPLAYERS ; i++)
	    if (playeringame[i])
	    {
		players[i].mo = NULL;
		G_DeathMatchSpawnPlayer (i);
	    }
			
    }

    // clear special respawning que
    iquehead = iquetail = 0;		
	
    // set up world state
    P_SpawnSpecials ();
	
    // build subsector connect matrix
    //	UNUSED P_ConnectSubsectors ();

    // preload graphics
    if (precache)
       {
	    R_PrecacheLevel ();
       }

    //BuildThingList();

    Build3DLevel();

    //printf ("free memory: 0x%x\n", Z_FreeMemory());

}



//
// P_Init
//
void P_Init (void)
{
    P_InitSwitchList ();
    P_InitPicAnims ();
    R_InitSprites (sprnames);
}



////////////////////////////////////////////////////////////////////
// Polygon Creation
////////////////////////////////////////////////////////////////////

void  NormalVector(float, float, float, float, int);
float InnerProduct(float *f, float *m, float *e);

RECT              *SectorBBox = 0;

drawside_t        *DrawSide = 0;
dboolean              *DrawFlat = 0;

DW_Vertex3Dv      *Normal = 0;
DW_Polygon        *PolyList = 0;
DW_FloorCeil      *FloorList = 0;
DW_FloorCeil      *CeilList = 0;
DW_TexList         TexList[1024];
int                TexCount;

flats_t           *flats = 0;

float InnerProduct(float *f, float *m, float *e)
   {
    float v1[3], v2[3], v3[3], d, n[3];

    v1[0] = e[0] - f[0];
    v1[1] = e[1] - f[1];
    v1[2] = e[2] - f[2];

    v2[0] = e[0] - e[0];
    v2[1] = e[1] - e[1];
    v2[2] = e[2] - 128.0f;

    n[0] = v1[1]*v2[2] - v2[1]*v1[2];
    n[1] = v1[2]*v2[0] - v2[2]*v1[0];
    n[2] = v1[0]*v2[1] - v2[0]*v1[1];

    d = (n[0]*n[0])+(n[1]*n[1])+(n[2]*n[2]);

    if (d == 0)
       d = 1.0f;
    else
       d = 1.0f / (float)sqrt(d);

    n[0] *= d;
    n[1] *= d;
    n[2] *= d;

    v3[0] = f[0] - m[0];
    v3[1] = f[1] - m[1];
    v3[2] = f[2] - m[2];

    d = (v3[0]*v3[0])+(v3[1]*v3[1])+(v3[2]*v3[2]);

    if (d == 0)
       d = 1.0f;
    else
       d = 1.0f / (float)sqrt(d);

    v3[0] *= d;
    v3[1] *= d;
    v3[2] *= d;

	 return (n[0]*v3[0])+(n[1]*v3[1])+(n[2]*v3[2]);
   }

void NormalVector(float x1, float y1, float x2, float y2, int side)
   {
    float v1[3], v2[3], d, n[3];

    // horizontal vector
    //v1[0] = x2 - x1;
    //v1[1] = 0.0f;
    //v1[2] = y2 - y1;

    // vertical vector - walls all vertical
    //v2[0] = 0.0f;
    //v2[1] = -1.0f;
    //v2[2] = 0.0f;

    // cross product...
    //n[0] = (v1[1] * v2[2]) - (v1[2] * v2[1]);
    //n[1] = (v1[2] * v2[0]) - (v1[0] * v2[2]);
    //n[2] = (v1[0] * v2[1]) - (v1[1] * v2[0]);

    // or we can use the "short form..." :o)
    n[0] = y2 - y1;
    n[2] = 0.0f - (x2 - x1);

    d = (n[0]*n[0])+(n[2]*n[2]);

    if (d == 0)
       d = 1.0f;
    else
       d = 1.0f / (float)sqrt(d);

    Normal[side].v[0] = n[0] * d;
    Normal[side].v[1] = 0.0f;
    Normal[side].v[2] = n[2] * d;
   }

// Original Doom geometry data...

typedef struct
{
    dboolean	istexture;	// if false, it is a flat
    char	endname[9];
    char	startname[9];
    int		speed;
} animdef_t;


// A single patch from a texture definition,
//  basically a rectangular area within
//  the texture rectangle.
typedef struct
{
    // Block origin (allways UL),
    // which has allready accounted
    // for the internal origin of the patch.
    int		originx;	
    int		originy;
    int		patch;
} texpatch_t;


// A maptexturedef_t describes a rectangular texture,
//  which is composed of one or more mappatch_t structures
//  that arrange graphic patches.
typedef struct
{
    // Keep name for switch changing, etc.
    char	name[8];		
    short	width;
    short	height;
    
    // All the patches[patchcount]
    //  are drawn back to front into the cached texture.
    short	patchcount;
    texpatch_t	patches[1];		
    
} texture_t;

int    translate[1024];
int    ftranslate[1024];
extern animdef_t    animdefs[];
extern texture_t**	textures;
extern switchlist_t alphSwitchList[];
extern int numswitches;
extern int switchlist[];
extern dboolean  TexTransparent;

extern int skytexture;
int        GL_SkyTexture[4], GL_SkyParts = 0, GL_SkyTop;

void CalcTexCoords(void);

void BuildWallTexList()
   {
    int   line, side, texture;
    dboolean  TexDupe;

    for (side = 0; side < 2; side++)
       {
        for (line = 0; line < numlines; line++)
           {
            if (lines[line].sidenum[side] >= 0)
               {
                TexDupe = false;
                if (sides[lines[line].sidenum[side]].toptexture > 0)
                   {
                    for (texture = 0; texture < TexCount; texture++)
                       {
                        if (TexList[texture].Number == sides[lines[line].sidenum[side]].toptexture)
                           {
                            TexDupe = true;
                            break;
                           }
                       }
                    if (TexDupe == false)
                       {
                        translate[sides[lines[line].sidenum[side]].toptexture] = TexCount;
                        TexList[TexCount].Number = sides[lines[line].sidenum[side]].toptexture;
                        TexList[TexCount].Type = 'W';
                        TexList[TexCount].DWide = textures[TexList[TexCount].Number]->width;
                        TexList[TexCount].DHigh = textures[TexList[TexCount].Number]->height;
                        strncpy(TexList[TexCount].szName, textures[TexList[TexCount].Number]->name, 8);
                        TexList[TexCount].szName[8] = '\0';
                        TexCount++;
                       }
                   }
                TexDupe = false;
                if (sides[lines[line].sidenum[side]].bottomtexture > 0)
                   {
                    for (texture = 0; texture < TexCount; texture++)
                       {
                        if (TexList[texture].Number == sides[lines[line].sidenum[side]].bottomtexture)
                           {
                            TexDupe = true;
                            break;
                           }
                       }
                    if (TexDupe == false)
                       {
                        translate[sides[lines[line].sidenum[side]].bottomtexture] = TexCount;
                        TexList[TexCount].Number = sides[lines[line].sidenum[side]].bottomtexture;
                        TexList[TexCount].Type = 'W';
                        TexList[TexCount].DWide = textures[TexList[TexCount].Number]->width;
                        TexList[TexCount].DHigh = textures[TexList[TexCount].Number]->height;
                        strncpy(TexList[TexCount].szName, textures[TexList[TexCount].Number]->name, 8);
                        TexList[TexCount].szName[8] = '\0';
                        TexCount++;
                       }
                   }
                TexDupe = false;
                if (sides[lines[line].sidenum[side]].midtexture > 0)
                   {
                    for (texture = 0; texture < TexCount; texture++)
                       {
                        if (TexList[texture].Number == sides[lines[line].sidenum[side]].midtexture)
                           {
                            TexDupe = true;
                            break;
                           }
                       }
                    if (TexDupe == false)
                       {
                        translate[sides[lines[line].sidenum[side]].midtexture] = TexCount;
                        TexList[TexCount].Number = sides[lines[line].sidenum[side]].midtexture;
                        TexList[TexCount].Type = 'W';
                        TexList[TexCount].DWide = textures[TexList[TexCount].Number]->width;
                        TexList[TexCount].DHigh = textures[TexList[TexCount].Number]->height;
                        strncpy(TexList[TexCount].szName, textures[TexList[TexCount].Number]->name, 8);
                        TexList[TexCount].szName[8] = '\0';
                        TexCount++;
                       }
                   }
               }
           }
       }
/*
   for (texture = 0; texture < TexCount; texture++)
      {
       lfprintf( "Texture %3d -> %5d, %s\n", texture, TexList[texture].Number, TexList[texture].szName);
      }
*/
   }

dboolean CheckTexture(char *name)
   {
    int t;

    for (t = 0; t < TexCount; t++)
       {
        if (D_strcasecmp(name, TexList[t].szName) == 0)
           {
            return true;
           }
       }

    return false;
   }

void AddSwitchTextures()
   {
    int  a, b, c;
    char tname[10];

    b = TexCount;
    for (a = 0; a < b; a++)
       {
        strncpy(tname, textures[TexList[a].Number]->name, 8);
        tname[8] = '\0';
        for (c = 0; c < numswitches; c++)
           {
            if (D_strcasecmp(tname, alphSwitchList[c].name1) == 0)
               {
                if (CheckTexture(alphSwitchList[c].name2) == false)
                   {
                    translate[switchlist[(c*2)+1]] = TexCount;
                    strncpy(TexList[TexCount].szName, alphSwitchList[c].name2, 8);
                    TexList[TexCount].szName[8] = '\0';
                    TexList[TexCount].Number = switchlist[(c*2)+1];
                    TexList[TexCount].Type = 'W';
                    TexList[TexCount].DWide = textures[TexList[TexCount].Number]->width;
                    TexList[TexCount].DHigh = textures[TexList[TexCount].Number]->height;
                    strncpy(TexList[TexCount].szName, textures[TexList[TexCount].Number]->name, 8);
                    TexCount++;
                   }
                break;
               }
            else
            if (D_strcasecmp(tname, alphSwitchList[c].name2) == 0)
               {
                if (CheckTexture(alphSwitchList[c].name1) == false)
                   {
                    translate[switchlist[c*2]] = TexCount;
                    strncpy(TexList[TexCount].szName, alphSwitchList[c].name1, 8);
                    TexList[TexCount].szName[8] = '\0';
                    TexList[TexCount].Number = switchlist[c*2];
                    TexList[TexCount].Type = 'W';
                    TexList[TexCount].DWide = textures[TexList[TexCount].Number]->width;
                    TexList[TexCount].DHigh = textures[TexList[TexCount].Number]->height;
                    strncpy(TexList[TexCount].szName, textures[TexList[TexCount].Number]->name, 8);
                    TexCount++;
                   }
                break;
               }
           }
       }
   }

void AddWallAnimations()
   {
    int a, b, c, bpic, epic, anims, anime;

    b = TexCount;
    for (a = 0; a < b; a++)
       {
        if (TexList[a].Type != 'W')
           continue;

        //lfprintf( "Matching wall texture : %s\n", textures[TexList[a].Number]->name);
        for (c = 0; animdefs[c].istexture != -1; c++)
           {
            if (animdefs[c].istexture == true)
               {
                bpic = R_CheckTextureNumForName(animdefs[c].startname);
                epic = R_CheckTextureNumForName(animdefs[c].endname);
                if ((bpic != -1) && (epic != -1))
                   {
                    //lfprintf( "Wall Animation : %s to %s\n", animdefs[c].startname, animdefs[c].endname);
                    anims = R_TextureNumForName (animdefs[c].startname);
                    anime = R_TextureNumForName (animdefs[c].endname);
                    if ((TexList[a].Number >= anims) && (TexList[a].Number <= anime))
                       {
                        //lfprintf( "Wall Animation : %s to %s\n", animdefs[c].startname, animdefs[c].endname);
                        for (; anims <= anime; anims++)
                           {
                            if (TexList[a].Number == anims)
                               continue;
                            translate[anims] = TexCount;
                            TexList[TexCount].Number = anims;
                            TexList[TexCount].Type = 'W';
                            TexList[TexCount].DWide = textures[TexList[TexCount].Number]->width;
                            TexList[TexCount].DHigh = textures[TexList[TexCount].Number]->height;
                            strncpy(TexList[TexCount].szName, textures[TexList[TexCount].Number]->name, 8);
                            TexCount++;
                           }
                        break;
                       }
                   }
               }
           }
       }
   }

void AddNewFlatTextures()
   {
    int  sector, t, TexFlat;
    dboolean TexDupe;

    TexFlat = TexCount;
    for (sector = 0; sector < numsectors; sector++)
       {
        TexDupe = false;
        for (t = TexFlat; t < TexCount; t++)
           {
            if (TexList[t].Number  == sectors[sector].floorpic)
               {
                TexDupe = true;
                break;
               }
           }
        if (TexDupe == false)
           {
            ftranslate[sectors[sector].floorpic] = TexCount;
            TexList[TexCount].Number = sectors[sector].floorpic;
            TexList[TexCount].DWide = 64;
            TexList[TexCount].DHigh = 64;
            TexList[TexCount].Type = 'F';
            TexCount++;
           }
        TexDupe = false;
        for (t = TexFlat; t < TexCount; t++)
           {
            if (TexList[t].Number == sectors[sector].ceilingpic)
               {
                TexDupe = true;
                break;
               }
           }
        if (TexDupe == false)
           {
            ftranslate[sectors[sector].ceilingpic] = TexCount;
            TexList[TexCount].Number = sectors[sector].ceilingpic;
            TexList[TexCount].DWide = 64;
            TexList[TexCount].DHigh = 64;
            TexList[TexCount].Type = 'F';
            TexCount++;
           }
       }
/*
   for (t = 0; t < TexCount; t++)
      {
       if (TexList[t].Type != 'F')
          {
           continue;
          }
       lfprintf( "Flat %3d -> %5d, %s\n", t, TexList[t].Number, TexList[t].szName);
      }
*/
   }

void AddFlatAnimations(int TexFlat)
   {
    int  a, b, c, bpic, epic, anims, anime;

    b = TexCount;
    for (a = TexFlat; a < b; a++)
       {
        if (TexList[a].Type != 'F')  // "shouldn't" be necessary
           continue;

        //lfprintf( "Checking flat %d\n", TexList[a].Number);
        for (c = 0; animdefs[c].istexture != -1; c++)
           {
            if (animdefs[c].istexture == false)
               {
                bpic = W_CheckNumForName(animdefs[c].startname);
                epic = W_CheckNumForName(animdefs[c].endname);
                if ((bpic != -1) && (epic != -1))
                   {
                    anims = R_FlatNumForName (animdefs[c].startname);
                    anime = R_FlatNumForName (animdefs[c].endname);
                    //if (((firstflat+TexList[a].Number) >= anims) && ((firstflat+TexList[a].Number) <= anime))
                    if ((TexList[a].Number >= anims) && (TexList[a].Number <= anime))
                       {
                        //lfprintf( "Flat Animation : %s to %s\n", animdefs[c].startname, animdefs[c].endname);
                        for (; anims <= anime; anims++)
                           {
                            ftranslate[anims] = TexCount;
                            TexList[TexCount].Number = anims;
                            TexList[TexCount].DWide = 64;
                            TexList[TexCount].DHigh = 64;
                            TexList[TexCount].Type = 'F';
                            TexCount++;
                          }
                        break;
                       }
                   }
               }
           }
       }
   }

void CreateSkyEntry()
   {
    TexList[TexCount].Number = skytexture;
    TexList[TexCount].DWide = textures[TexList[TexCount].Number]->width;
    TexList[TexCount].DHigh = textures[TexList[TexCount].Number]->height;
    TexList[TexCount].Type = 'S';
    TexCount++;
   }

int GL_LoadTexture(int texture);
int GL_LoadSkyTexture(int texture, int *tlist);
int GL_LoadFlatTexture(int texture);
int GL_LoadSkyTop(char *filename);

void CreateNewTextures()
   {
    int texture;

    for (texture = 0; texture < TexCount; texture++)
       {
        if (TexList[texture].Type == 'W')
           {
            TexList[texture].glTexture = GL_LoadTexture(texture);
            TexList[texture].Transparent = TexTransparent;
//            lfprintf( "Texture %-8s : %dx%d -> %dx%d\n", TexList[texture].szName,
//                    TexList[texture].DWide, TexList[texture].DHigh,
//                    TexList[texture].GLWide, TexList[texture].GLHigh);
           }
        else
        if (TexList[texture].Type == 'S')
           {
//            lfprintf("Creating sky texture...\n");
            GL_SkyParts = GL_LoadSkyTexture(texture, GL_SkyTexture);
            TexList[texture].Transparent = false;
//            lfprintf( "Texture %-8s : %dx%d -> %dx%d - parts %d\n", TexList[texture].szName,
//                    TexList[texture].DWide, TexList[texture].DHigh,
//                    TexList[texture].GLWide, TexList[texture].GLHigh, GL_SkyParts);
           }
        else
           {
            TexList[texture].glTexture = GL_LoadFlatTexture(texture);
            TexList[texture].Transparent = TexTransparent;
//            lfprintf( "Flat - %d - %d\n", texture, TexList[texture].Number);
           }
       }

//    GL_SkyTop = GL_LoadSkyTop("sky1top.bmp");
   }

void LoadNewTextures()
   {
    int       texture, TexFlat;
    GLdouble *v[1024];

    // clear texture translation table
    for (texture = 0; texture < 1024; texture++)
       {
        v[texture] = (GLdouble *)malloc(sizeof(GLdouble)*3);
        translate[texture] = texture;
       }

    TexCount = 0;

    for (texture = 0; texture < 1024; texture++)
       {
        TexList[texture].szName[0] = '\0';
        TexList[texture].DWide = 0;
        TexList[texture].DHigh = 0;
        TexList[texture].GLWide = 0;
        TexList[texture].GLHigh = 0;
        TexList[texture].Type = 'W';
        TexList[texture].glTexture = 0;
       }
    BuildWallTexList();
    AddSwitchTextures();
    AddWallAnimations();
    TexFlat = TexCount;
    AddNewFlatTextures();
    AddFlatAnimations(TexFlat);
    CreateSkyEntry();
    CreateNewTextures();
   }

int TextureSearchSector(int sector, int top)
   {
    int side;

    for (side = 0; side < numsides; side++)
       {
        if (top)
           {
            if ((sides[side].sectornumb == sector) && (sides[side].toptexture != 0))
               {
                return sides[side].toptexture;
               }
           }
        else
           {
            if ((sides[side].sectornumb == sector) && (sides[side].bottomtexture != 0))
               {
                return sides[side].bottomtexture;
               }
           }
       }
    return 0;
   }

void CreateNewWalls()
   {
    int         line, side, PolyCount;
    int         Sector1, Sector2, texture;
    float       Ceil1, Ceil2, Floor1, Floor2, MiddleTop, MiddleBottom;
    DW_Polygon *TempPoly;
    char        szCeil[2][10], szFloor[2][10];
    dboolean        SkyCeil, SkyFloor;

    //lfprintf( "Number of linedefs : %d\n", numlines);

    TempPoly = PolyList;
    while (TempPoly != 0)
       {
        PolyList = TempPoly->Next;
        free(TempPoly);
        TempPoly = PolyList;
       }
    PolyCount = 0;

    if (Normal != 0)
	{
       free(Normal);
	}
    Normal = (DW_Vertex3Dv *)malloc(sizeof(DW_Vertex3Dv)*numsides);

    if (DrawSide != 0)
        free(DrawSide);
    DrawSide = (drawside_t *)malloc(sizeof(drawside_t)*numsides);
    for (side = 0; side < numsides; side++)
        DrawSide[side] = ds_unknown;


    for (line = 0; line < numlines; line++)
       {
        // Generate the normal for this linedef
        NormalVector((float)(lines[line].v1->x>>FRACBITS), (float)(lines[line].v1->y>>FRACBITS)*-1.0f,
                     (float)(lines[line].v2->x>>FRACBITS), (float)(lines[line].v2->y>>FRACBITS)*-1.0f, lines[line].sidenum[0]);

        Sector1 = sides[lines[line].sidenum[0]].sectornumb;
        Ceil1 =  (float)(sides[lines[line].sidenum[0]].sector->ceilingheight>>FRACBITS);
        Floor1 = (float)(sides[lines[line].sidenum[0]].sector->floorheight>>FRACBITS);

        if (lines[line].sidenum[1] >= 0) // We have a side 2
           {
            // If two sided, then invert the normal for the other side...
            Normal[lines[line].sidenum[1]].v[0] = Normal[lines[line].sidenum[0]].v[0] * -1;
            Normal[lines[line].sidenum[1]].v[1] = Normal[lines[line].sidenum[0]].v[1] * -1;
            Normal[lines[line].sidenum[1]].v[2] = Normal[lines[line].sidenum[0]].v[2] * -1;

            Sector2 = sides[lines[line].sidenum[1]].sectornumb;
            Ceil2   = (float)(sides[lines[line].sidenum[1]].sector->ceilingheight>>FRACBITS);
            Floor2  = (float)(sides[lines[line].sidenum[1]].sector->floorheight>>FRACBITS);

            // Check for "SKY" as the floor or ceiling and don't generate the upper or middle wall if it is "sky" on both sides
            strncpy(szCeil[0], lumpinfo[sides[lines[line].sidenum[0]].sector->ceilingpic+firstflat].name, 8);
            szCeil[0][8] = '\0';
            strncpy(szCeil[1], lumpinfo[sides[lines[line].sidenum[1]].sector->ceilingpic+firstflat].name, 8);
            szCeil[1][8] = '\0';
            if ((D_strcasecmp(szCeil[0], "F_SKY1") == 0) && (D_strcasecmp(szCeil[1], "F_SKY1") == 0))
               {
                SkyCeil = true;
               }
            else
               {
                SkyCeil = false;
               }

            strncpy(szFloor[0], lumpinfo[sides[lines[line].sidenum[0]].sector->floorpic+firstflat].name, 8);
            szFloor[0][8] = '\0';
            strncpy(szFloor[1], lumpinfo[sides[lines[line].sidenum[1]].sector->floorpic+firstflat].name, 8);
            szFloor[1][8] = '\0';
            if ((D_strcasecmp(szFloor[0], "F_SKY1") == 0) && (D_strcasecmp(szFloor[1], "F_SKY1") == 0))
               {
                SkyFloor = true;
               }
            else
               {
                SkyFloor = false;
               }

            // Check here for upper and lower textures left out...
            // Check uppers first...
            if ((Ceil1 > Ceil2) && (sides[lines[line].sidenum[0]].toptexture == 0) && (SkyCeil == false))
               {
                sides[lines[line].sidenum[0]].toptexture = TextureSearchSector(Sector1, 1);
                if (sides[lines[line].sidenum[0]].toptexture == 0)
                   {
                    if (sides[lines[line].sidenum[1]].toptexture != 0)
                       {
                        sides[lines[line].sidenum[0]].toptexture = sides[lines[line].sidenum[1]].toptexture;
                       }
                    else
                    if (sides[lines[line].sidenum[0]].bottomtexture != 0)
                       {
                        sides[lines[line].sidenum[0]].toptexture = sides[lines[line].sidenum[0]].bottomtexture;
                       }
                    else
                       {
                        sides[lines[line].sidenum[0]].toptexture = 1;
                       }
                   }
               }
            else
            if ((Ceil1 < Ceil2) && (sides[lines[line].sidenum[1]].toptexture == 0) && (SkyCeil == false))
               {
                sides[lines[line].sidenum[1]].toptexture = TextureSearchSector(Sector2, 1);
                if (sides[lines[line].sidenum[1]].toptexture == 0)
                   {
                    if (sides[lines[line].sidenum[0]].toptexture != 0)
                       {
                        sides[lines[line].sidenum[1]].toptexture = sides[lines[line].sidenum[0]].toptexture;
                       }
                    else
                    if (sides[lines[line].sidenum[1]].bottomtexture != 0)
                       {
                        sides[lines[line].sidenum[1]].toptexture = sides[lines[line].sidenum[1]].bottomtexture;
                       }
                    else
                       {
                        sides[lines[line].sidenum[1]].toptexture = 1;
                       }
                   }
               }
            // Then check the lowers...
            if ((Floor1 < Floor2) && (sides[lines[line].sidenum[0]].bottomtexture == 0) && (SkyFloor == false))
               {
                sides[lines[line].sidenum[0]].bottomtexture = TextureSearchSector(Sector1, 0);
                if (sides[lines[line].sidenum[0]].bottomtexture == 0)
                   {
                    if (sides[lines[line].sidenum[1]].bottomtexture != 0)
                       {
                        sides[lines[line].sidenum[0]].bottomtexture = sides[lines[line].sidenum[1]].bottomtexture;
                       }
                    else
                    if (sides[lines[line].sidenum[0]].toptexture != 0)
                       {
                        sides[lines[line].sidenum[0]].bottomtexture = sides[lines[line].sidenum[0]].toptexture;
                       }
                    else
                       {
                        sides[lines[line].sidenum[0]].bottomtexture = 1;
                       }
                   }
               }
            else
            if ((Floor2 < Floor1) && (sides[lines[line].sidenum[1]].bottomtexture == 0) && (SkyFloor == false))
               {
                sides[lines[line].sidenum[1]].bottomtexture = TextureSearchSector(Sector2, 0);
                if (sides[lines[line].sidenum[1]].bottomtexture == 0)
                   {
                    if (sides[lines[line].sidenum[0]].bottomtexture != 0)
                       {
                        sides[lines[line].sidenum[1]].bottomtexture = sides[lines[line].sidenum[0]].bottomtexture;
                       }
                    else
                    if (sides[lines[line].sidenum[1]].toptexture != 0)
                       {
                        sides[lines[line].sidenum[1]].bottomtexture = sides[lines[line].sidenum[1]].toptexture;
                       }
                    else
                       {
                        sides[lines[line].sidenum[1]].bottomtexture = 1;
                       }
                   }
               }
           }
        else
           {
            SkyCeil = false;
            SkyFloor = false;
           }

        if (lines[line].sidenum[1] >= 0) // We have a side 2
           {
            // Generate Polygons for the "sector1" (first) side of the "portal"
            if ((sides[lines[line].sidenum[0]].bottomtexture > 0) && (SkyFloor == false))
               {
                TempPoly = (DW_Polygon *)malloc(sizeof(DW_Polygon));
                TempPoly->SideDef = lines[line].sidenum[0];
                TempPoly->LineDef = line;
                TempPoly->Position = DW_LOWER;
                TempPoly->Texture[0] = 0;
                TempPoly->LSector = Sector1;
                TempPoly->Lighting[0] = (float)sectors[Sector1].lightlevel;
                TempPoly->Lighting[1] = TempPoly->Lighting[0];
                TempPoly->Lighting[2] = TempPoly->Lighting[0];
                TempPoly->Lighting[3] = 1.0f;
                TempPoly->coloff = sides[TempPoly->SideDef].textureoffset;

                TempPoly->Sector = Sector1;
                TempPoly->BackSector = Sector2;

                for (texture = 0; texture < TexCount; texture++)
                   {
                    if (TexList[texture].Number == sides[lines[line].sidenum[0]].bottomtexture)
                       {
                        TempPoly->Texture[0] = texture;
                        break;
                       }
                   }

                // Top left first...
                TempPoly->Point[0].v[0] = (float)(lines[line].v1->x>>FRACBITS);
                TempPoly->Point[0].v[1] = (float)Floor2;
                TempPoly->Point[0].v[2] = (float)(lines[line].v1->y>>FRACBITS)*-1;
                // Bottom left next...
                TempPoly->Point[1].v[0] = (float)(lines[line].v1->x>>FRACBITS);
                TempPoly->Point[1].v[1] = (float)Floor1;
                TempPoly->Point[1].v[2] = (float)(lines[line].v1->y>>FRACBITS)*-1;
                // Bottom right next...
                TempPoly->Point[2].v[0] = (float)(lines[line].v2->x>>FRACBITS);
                TempPoly->Point[2].v[1] = (float)Floor1;
                TempPoly->Point[2].v[2] = (float)(lines[line].v2->y>>FRACBITS)*-1;
                // Top right next...
                TempPoly->Point[3].v[0] = (float)(lines[line].v2->x>>FRACBITS);
                TempPoly->Point[3].v[1] = (float)Floor2;
                TempPoly->Point[3].v[2] = (float)(lines[line].v2->y>>FRACBITS)*-1;
                TempPoly->Next = PolyList;
                PolyList = TempPoly;
                PolyCount++;
               }

            if (sides[lines[line].sidenum[0]].midtexture > 0)
               {
                TempPoly = (DW_Polygon *)malloc(sizeof(DW_Polygon));
                TempPoly->SideDef = lines[line].sidenum[0];
                TempPoly->LineDef = line;
                TempPoly->Position = DW_MIDDLE;
                TempPoly->Texture[0] = 0;
                TempPoly->LSector = Sector1;
                TempPoly->Lighting[0] = (float)sectors[Sector1].lightlevel;
                TempPoly->Lighting[1] = TempPoly->Lighting[0];
                TempPoly->Lighting[2] = TempPoly->Lighting[0];
                TempPoly->Lighting[3] = 1.0f;
                TempPoly->coloff = sides[TempPoly->SideDef].textureoffset;

                TempPoly->Sector = Sector1;
                TempPoly->BackSector = Sector2;

                for (texture = 0; texture < TexCount; texture++)
                   {
                    if (TexList[texture].Number == sides[lines[line].sidenum[0]].midtexture)
                       {
                        TempPoly->Texture[0] = texture;
                        break;
                       }
                   }

                // Figure out the top and bottom of this quad...
                if (Ceil1 < Ceil2)
                   MiddleTop = Ceil1;
                else
                   MiddleTop = Ceil2;
                if (Floor1 > Floor2)
                   MiddleBottom = Floor1;
                else
                   MiddleBottom = Floor2;
                // Top left first...
                TempPoly->Point[0].v[0] = (float)(lines[line].v1->x>>FRACBITS);
                TempPoly->Point[0].v[1] = (float)MiddleTop;
                TempPoly->Point[0].v[2] = (float)(lines[line].v1->y>>FRACBITS)*-1;
                // Bottom left next...
                TempPoly->Point[1].v[0] = (float)(lines[line].v1->x>>FRACBITS);
                TempPoly->Point[1].v[1] = (float)MiddleBottom;
                TempPoly->Point[1].v[2] = (float)(lines[line].v1->y>>FRACBITS)*-1;
                // Bottom right next...
                TempPoly->Point[2].v[0] = (float)(lines[line].v2->x>>FRACBITS);
                TempPoly->Point[2].v[1] = (float)MiddleBottom;
                TempPoly->Point[2].v[2] = (float)(lines[line].v2->y>>FRACBITS)*-1;
                // Top right next...
                TempPoly->Point[3].v[0] = (float)(lines[line].v2->x>>FRACBITS);
                TempPoly->Point[3].v[1] = (float)MiddleTop;
                TempPoly->Point[3].v[2] = (float)(lines[line].v2->y>>FRACBITS)*-1;
                TempPoly->Next = PolyList;
                PolyList = TempPoly;
                PolyCount++;
               }

            if ((sides[lines[line].sidenum[0]].toptexture > 0) && (SkyCeil == false))
               {
                TempPoly = (DW_Polygon *)malloc(sizeof(DW_Polygon));
                TempPoly->SideDef = lines[line].sidenum[0];
                TempPoly->LineDef = line;
                TempPoly->Position = DW_UPPER;
                TempPoly->Texture[0] = 0;
                TempPoly->LSector = Sector1;
                TempPoly->Lighting[0] = (float)sectors[Sector1].lightlevel;
                TempPoly->Lighting[1] = TempPoly->Lighting[0];
                TempPoly->Lighting[2] = TempPoly->Lighting[0];
                TempPoly->Lighting[3] = 1.0f;
                TempPoly->coloff = sides[TempPoly->SideDef].textureoffset;

                TempPoly->Sector = Sector1;
                TempPoly->BackSector = Sector2;

                for (texture = 0; texture < TexCount; texture++)
                   {
                    if (TexList[texture].Number == sides[lines[line].sidenum[0]].toptexture)
                       {
                        TempPoly->Texture[0] = texture;
                        break;
                       }
                   }

                // Top left first...
                TempPoly->Point[0].v[0] = (float)(lines[line].v1->x>>FRACBITS);
                TempPoly->Point[0].v[1] = (float)Ceil1;
                TempPoly->Point[0].v[2] = (float)(lines[line].v1->y>>FRACBITS)*-1;
                // Bottom left next...
                TempPoly->Point[1].v[0] = (float)(lines[line].v1->x>>FRACBITS);
                TempPoly->Point[1].v[1] = (float)Ceil2;
                TempPoly->Point[1].v[2] = (float)(lines[line].v1->y>>FRACBITS)*-1;
                // Bottom right next...
                TempPoly->Point[2].v[0] = (float)(lines[line].v2->x>>FRACBITS);
                TempPoly->Point[2].v[1] = (float)Ceil2;
                TempPoly->Point[2].v[2] = (float)(lines[line].v2->y>>FRACBITS)*-1;
                // Top right next...
                TempPoly->Point[3].v[0] = (float)(lines[line].v2->x>>FRACBITS);
                TempPoly->Point[3].v[1] = (float)Ceil1;
                TempPoly->Point[3].v[2] = (float)(lines[line].v2->y>>FRACBITS)*-1;
                TempPoly->Next = PolyList;
                PolyList = TempPoly;
                PolyCount++;
               }

            // Generate Polygons for the "sector2" (second) side of the "portal"
            if ((sides[lines[line].sidenum[1]].bottomtexture > 0) && (SkyFloor == false))
               {
                TempPoly = (DW_Polygon *)malloc(sizeof(DW_Polygon));
                TempPoly->SideDef = lines[line].sidenum[1];
                TempPoly->LineDef = line;
                TempPoly->Position = DW_LOWER;
                TempPoly->Texture[0] = 0;
                TempPoly->LSector = Sector2;
                TempPoly->Lighting[0] = (float)sectors[Sector2].lightlevel;
                TempPoly->Lighting[1] = TempPoly->Lighting[0];
                TempPoly->Lighting[2] = TempPoly->Lighting[0];
                TempPoly->Lighting[3] = 1.0f;
                TempPoly->coloff = sides[TempPoly->SideDef].textureoffset;

                TempPoly->Sector = Sector2;
                TempPoly->BackSector = Sector1;

                for (texture = 0; texture < TexCount; texture++)
                   {
                    if (TexList[texture].Number == sides[lines[line].sidenum[1]].bottomtexture)
                       {
                        TempPoly->Texture[0] = texture;
                        break;
                       }
                   }

                // Top left first...
                TempPoly->Point[0].v[0] = (float)(lines[line].v2->x>>FRACBITS);
                TempPoly->Point[0].v[1] = (float)Floor1;
                TempPoly->Point[0].v[2] = (float)(lines[line].v2->y>>FRACBITS)*-1;
                // Bottom left next...
                TempPoly->Point[1].v[0] = (float)(lines[line].v2->x>>FRACBITS);
                TempPoly->Point[1].v[1] = (float)Floor2;
                TempPoly->Point[1].v[2] = (float)(lines[line].v2->y>>FRACBITS)*-1;
                // Bottom right next...
                TempPoly->Point[2].v[0] = (float)(lines[line].v1->x>>FRACBITS);
                TempPoly->Point[2].v[1] = (float)Floor2;
                TempPoly->Point[2].v[2] = (float)(lines[line].v1->y>>FRACBITS)*-1;
                // Top right next...
                TempPoly->Point[3].v[0] = (float)(lines[line].v1->x>>FRACBITS);
                TempPoly->Point[3].v[1] = (float)Floor1;
                TempPoly->Point[3].v[2] = (float)(lines[line].v1->y>>FRACBITS)*-1;
                TempPoly->Next = PolyList;
                PolyList = TempPoly;
                PolyCount++;
               }

            if (sides[lines[line].sidenum[1]].midtexture > 0)
               {
                TempPoly = (DW_Polygon *)malloc(sizeof(DW_Polygon));
                TempPoly->SideDef = lines[line].sidenum[1];
                TempPoly->LineDef = line;
                TempPoly->Position = DW_MIDDLE;
                TempPoly->Texture[0] = 0;
                TempPoly->LSector = Sector2;
                TempPoly->Lighting[0] = (float)sectors[Sector2].lightlevel;
                TempPoly->Lighting[1] = TempPoly->Lighting[0];
                TempPoly->Lighting[2] = TempPoly->Lighting[0];
                TempPoly->Lighting[3] = 1.0f;
                TempPoly->coloff = sides[TempPoly->SideDef].textureoffset;

                TempPoly->Sector = Sector2;
                TempPoly->BackSector = Sector1;

                for (texture = 0; texture < TexCount; texture++)
                   {
                    if (TexList[texture].Number == sides[lines[line].sidenum[1]].midtexture)
                       {
                        TempPoly->Texture[0] = texture;
                        break;
                       }
                   }

                // Figure out the top and bottom of this quad...
                if (Ceil1 < Ceil2)
                   MiddleTop = Ceil1;
                else
                   MiddleTop = Ceil2;
                if (Floor1 > Floor2)
                   MiddleBottom = Floor1;
                else
                   MiddleBottom = Floor2;
                // Top left first...
                TempPoly->Point[0].v[0] = (float)(lines[line].v2->x>>FRACBITS);
                TempPoly->Point[0].v[1] = (float)MiddleTop;
                TempPoly->Point[0].v[2] = (float)(lines[line].v2->y>>FRACBITS)*-1;
                // Bottom left next...
                TempPoly->Point[1].v[0] = (float)(lines[line].v2->x>>FRACBITS);
                TempPoly->Point[1].v[1] = (float)MiddleBottom;
                TempPoly->Point[1].v[2] = (float)(lines[line].v2->y>>FRACBITS)*-1;
                // Bottom right next...
                TempPoly->Point[2].v[0] = (float)(lines[line].v1->x>>FRACBITS);
                TempPoly->Point[2].v[1] = (float)MiddleBottom;
                TempPoly->Point[2].v[2] = (float)(lines[line].v1->y>>FRACBITS)*-1;
                // Top right next...
                TempPoly->Point[3].v[0] = (float)(lines[line].v1->x>>FRACBITS);
                TempPoly->Point[3].v[1] = (float)MiddleTop;
                TempPoly->Point[3].v[2] = (float)(lines[line].v1->y>>FRACBITS)*-1;
                TempPoly->Next = PolyList;
                PolyList = TempPoly;
                PolyCount++;
               }

            if ((sides[lines[line].sidenum[1]].toptexture > 0) && (SkyCeil == false))
               {
                TempPoly = (DW_Polygon *)malloc(sizeof(DW_Polygon));
                TempPoly->SideDef = lines[line].sidenum[1];
                TempPoly->LineDef = line;
                TempPoly->Position = DW_UPPER;
                TempPoly->Texture[0] = 0;
                TempPoly->LSector = Sector2;
                TempPoly->Lighting[0] = (float)sectors[Sector2].lightlevel;
                TempPoly->Lighting[1] = TempPoly->Lighting[0];
                TempPoly->Lighting[2] = TempPoly->Lighting[0];
                TempPoly->Lighting[3] = 1.0f;
                TempPoly->coloff = sides[TempPoly->SideDef].textureoffset;

                TempPoly->Sector = Sector2;
                TempPoly->BackSector = Sector1;

                for (texture = 0; texture < TexCount; texture++)
                   {
                    if (TexList[texture].Number == sides[lines[line].sidenum[1]].toptexture)
                       {
                        TempPoly->Texture[0] = texture;
                        break;
                       }
                   }

                // Top left first...
                TempPoly->Point[0].v[0] = (float)(lines[line].v2->x>>FRACBITS);
                TempPoly->Point[0].v[1] = (float)Ceil2;
                TempPoly->Point[0].v[2] = (float)(lines[line].v2->y>>FRACBITS)*-1;
                // Bottom left next...
                TempPoly->Point[1].v[0] = (float)(lines[line].v2->x>>FRACBITS);
                TempPoly->Point[1].v[1] = (float)Ceil1;
                TempPoly->Point[1].v[2] = (float)(lines[line].v2->y>>FRACBITS)*-1;
                // Bottom right next...
                TempPoly->Point[2].v[0] = (float)(lines[line].v1->x>>FRACBITS);
                TempPoly->Point[2].v[1] = (float)Ceil1;
                TempPoly->Point[2].v[2] = (float)(lines[line].v1->y>>FRACBITS)*-1;
                // Top right next...
                TempPoly->Point[3].v[0] = (float)(lines[line].v1->x>>FRACBITS);
                TempPoly->Point[3].v[1] = (float)Ceil2;
                TempPoly->Point[3].v[2] = (float)(lines[line].v1->y>>FRACBITS)*-1;
                TempPoly->Next = PolyList;
                PolyList = TempPoly;
                PolyCount++;
               }
           }
        else
           {
            if (sides[lines[line].sidenum[0]].midtexture > 0)
               {
                Sector1 = sides[lines[line].sidenum[0]].sectornumb;

                TempPoly = (DW_Polygon *)malloc(sizeof(DW_Polygon));
                TempPoly->SideDef = lines[line].sidenum[0];
                TempPoly->LineDef = line;
                TempPoly->Position = DW_MIDDLE;
                TempPoly->Texture[0] = 0;
                TempPoly->LSector = Sector1;
                TempPoly->Lighting[0] = (float)sides[lines[line].sidenum[0]].sector->lightlevel;
                TempPoly->Lighting[1] = TempPoly->Lighting[0];
                TempPoly->Lighting[2] = TempPoly->Lighting[0];
                TempPoly->Lighting[3] = 1.0f;
                TempPoly->coloff = sides[TempPoly->SideDef].textureoffset;

                for (texture = 0; texture < TexCount; texture++)
                   {
                    if (TexList[texture].Number == sides[lines[line].sidenum[0]].midtexture)
                       {
                        TempPoly->Texture[0] = texture;
                        break;
                       }
                   }

                TempPoly->Sector = Sector1;
                TempPoly->BackSector = -1;

                Floor1 = (float)(sides[lines[line].sidenum[0]].sector->floorheight>>FRACBITS);
                Ceil1  = (float)(sides[lines[line].sidenum[0]].sector->ceilingheight>>FRACBITS);

                // Top left first...
                TempPoly->Point[0].v[0] = (float)(lines[line].v1->x>>FRACBITS);
                TempPoly->Point[0].v[1] = (float)Ceil1;
                TempPoly->Point[0].v[2] = (float)(lines[line].v1->y>>FRACBITS)*-1;
                // Bottom left next...
                TempPoly->Point[1].v[0] = (float)(lines[line].v1->x>>FRACBITS);
                TempPoly->Point[1].v[1] = (float)Floor1;
                TempPoly->Point[1].v[2] = (float)(lines[line].v1->y>>FRACBITS)*-1;
                // Bottom right next...
                TempPoly->Point[2].v[0] = (float)(lines[line].v2->x>>FRACBITS);
                TempPoly->Point[2].v[1] = (float)Floor1;
                TempPoly->Point[2].v[2] = (float)(lines[line].v2->y>>FRACBITS)*-1;
                // Top right next...
                TempPoly->Point[3].v[0] = (float)(lines[line].v2->x>>FRACBITS);
                TempPoly->Point[3].v[1] = (float)Ceil1;
                TempPoly->Point[3].v[2] = (float)(lines[line].v2->y>>FRACBITS)*-1;
                TempPoly->Next = PolyList;
                PolyList = TempPoly;
                PolyCount++;
               }
           }
       }
    //lfprintf( "Total generated \"wall\" polygon count: %d\n", PolyCount);
   }

typedef enum { onleft, onright, intersected, newleft, newright, colinear, revlinear } linetype_t;

typedef struct
   {
    double   x;
    double   y;
   }bspvert_t;

typedef struct
   {
    bspvert_t v[2];
    int       sector;
    int       next;
    int       prev;
   }linelist_t;

typedef struct
   {
    bspvert_t  v;
    linetype_t type;
    dboolean       forward;
    float      length;
    dboolean       vertex;
   }intersect_t;

typedef struct
   {
    bspvert_t  v[2];
    bspvert_t  n;
    bspvert_t  nu;
    int        vert[2];
    int        sector;
    int        section;
    dboolean       used;
    int        prev;
    int        next;
    dboolean       colinear;
    int        line;
   }bspline_t;

typedef struct
   {
    int    lines;
    int    first;
   }section_t;

#define EPSILON 0.0125
#define BVMAX      4096

#define FLOAT_EQ(x,v) (((v - EPSILON) <= x) && (x <= ( v + EPSILON)))

bspvert_t     bverts[4096];
int           bvcount;

dboolean          newsection = false;
bspline_t     bsplines[2048];
intersect_t   iplist[256];
int           icount = 0, lcount = 0, scount = 1;
section_t     slines[1024];
//int           slines[1024];

//#define WDEBUG 1

int FindVertex(float x, float y)
   {
    int i;

    for (i = 0; i < bvcount; i++)
       {
        if (FLOAT_EQ(bverts[i].x, x) && FLOAT_EQ(bverts[i].y,  y))
           {
            return i;
           }
       }
    return -1;
   }

int AddVertex( float x, float y)
   {
    if (bvcount < BVMAX)
       {
        bverts[bvcount].x = x;
        bverts[bvcount].y = y;
        bvcount++;
        return (bvcount - 1);
       }
    else
       {
        return 0;
       }
   }

void AddNewLine(bspvert_t *v0, bspvert_t *v1, int bv0, int bv1, int sector)
   {
    bspvert_t  n;
    double     l;

    if (FLOAT_EQ(v0->x, v1->x) && FLOAT_EQ(v0->y, v1->y))
       {
        return;
       }

    // calculate the normal vector
    n.x = v0->y - v1->y;
    n.y = -(v0->x - v1->x);
    // calculate the normal vector length
    l = sqrt((n.x * n.x) + (n.y * n.y));
    // divide each coordinate of the normal by the length to make it a unit vector
    n.x = n.x / l;
    n.y = n.y / l;

    bsplines[lcount].v[0].x = v0->x;
    bsplines[lcount].v[0].y = v0->y;
    bsplines[lcount].v[1].x = v1->x;
    bsplines[lcount].v[1].y = v1->y;
    bsplines[lcount].vert[0] = bv0;
    bsplines[lcount].vert[1] = bv1;
    bsplines[lcount].n.x = v0->y - v1->y;
    bsplines[lcount].n.y = -(v0->x - v1->x);
    bsplines[lcount].nu.x = n.x;
    bsplines[lcount].nu.y = n.y;
    bsplines[lcount].sector = sector;
    bsplines[lcount].section = scount-1;
    bsplines[lcount].used = false;
    bsplines[lcount].prev = -1;
    bsplines[lcount].next = -1;
    bsplines[lcount].colinear = false;
    lcount++;
   }

dboolean IsColinear(bspline_t *r, bspline_t *l)
   {
    double     nx, ny;
    double     denom, numer;

    nx = bverts[r->vert[0]].y - bverts[r->vert[1]].y;
    ny = -(bverts[r->vert[0]].x - bverts[r->vert[1]].x);

    // Calculate the dot products we'll need for line intersection and spatial relationship
    numer = (nx * (bverts[l->vert[0]].x - bverts[r->vert[0]].x)) + (ny * (bverts[l->vert[0]].y - bverts[r->vert[0]].y));
    denom = ((-nx) * (bverts[l->vert[1]].x - bverts[l->vert[0]].x)) + (-(ny) * (bverts[l->vert[1]].y - bverts[l->vert[0]].y));

    // Figure out if the infinite lines of the current line and
    // the root are colinear
    if (denom == 0.0)
       {
        if (numer == 0.0)
           {
            if ((r->nu.x == l->nu.x) && (r->nu.y == l->nu.y))  // normals are identical == lines are parallel
               {
                return true;
               }
            else // lines are opposite
               {
                return false;
               }
           }
       }
    else
       {
        return false;
       }
/*
    nx = r->v[0].y - r->v[1].y;
    ny = -(r->v[0].x - r->v[1].x);

    // Calculate the dot products we'll need for line intersection and spatial relationship
    numer = (nx * (l->v[0].x - r->v[0].x)) + (ny * (l->v[0].y - r->v[0].y));
    denom = ((-nx) * (l->v[1].x - l->v[0].x)) + (-(ny) * (l->v[1].y - l->v[0].y));

    // Figure out if the infinite lines of the current line and
    // the root are colinear
    if (denom == 0.0)
       {
        if (numer == 0.0f)
           {
            if ((r->n.x == l->n.x) && (r->n.y == l->n.y))  // normals are identical lines are parallel
               {
                return true;
               }
            else // lines are opposite
               {
                return false;
               }
           }
       }
    else
       {
        return false;
       }
*/
	return true;
   }

dboolean CheckVerts( bspvert_t *p1, bspvert_t *p2 )
   {
    if (FLOAT_EQ(p1->x, p2->x) && FLOAT_EQ(p1->y, p2->y))
       {
        return true;
       }
    
    return false;
   }

linetype_t LineTest(bspline_t *r, bspline_t *l, intersect_t *ip)
   {
    linetype_t lt;
    double     nx, ny, t, flen, blen;
    double     denom, numer;
    dboolean       Done;
    int        ipx, ipy;

    lt = onleft;
    //return lt;

    //nx = bverts[r->vert[0]].y - bverts[r->vert[1]].y;
    //ny = -(bverts[r->vert[0]].x - bverts[r->vert[1]].x);

    //nx = r->v[0].y - r->v[1].y;
    //ny = -(r->v[0].x - r->v[1].x);

    if ((r->v[0].x != bverts[r->vert[0]].x) || (r->v[0].y != bverts[r->vert[0]].y) ||
        (r->v[1].x != bverts[r->vert[1]].x) || (r->v[1].y != bverts[r->vert[1]].y))
       {
        lfprintf("diff : %lf,%lf to %lf,%lf : stored %lf,%lf to %lf,%lf\n",
                    r->v[0].x, r->v[0].y, r->v[1].x, r->v[1].y,
                    bverts[r->vert[0]].x, bverts[r->vert[0]].y, bverts[r->vert[1]].x, bverts[r->vert[1]].y);
       }

    //if ((nx != r->n.x) || (ny != r->n.y))
    //   {
    //    lfprintf("Calc n : %lf,%lf : stored %lf,%lf\n", nx, ny, r->n.x, r->n.y);
    //   }

    nx = r->n.x;
    ny = r->n.y;

    // Calculate the dot products we'll need for line intersection and spatial relationship
    numer = (nx * (bverts[l->vert[0]].x - bverts[r->vert[0]].x)) + (ny * (bverts[l->vert[0]].y - bverts[r->vert[0]].y));
    denom = ((-nx) * (bverts[l->vert[1]].x - bverts[l->vert[0]].x)) + (-(ny) * (bverts[l->vert[1]].y - bverts[l->vert[0]].y));

    //numer = (nx * (l->v[0].x - r->v[0].x)) + (ny * (l->v[0].y - r->v[0].y));
    //denom = ((-nx) * (l->v[1].x - l->v[0].x)) + (-(ny) * (l->v[1].y - l->v[0].y));

    // Figure out if the infinite lines of the current line and
    // the root intersect; if so, figure out if the current line
    // segment is actually split, split if so, and add front/back
    // polygons as appropriate
    if (denom == 0.0)
       {
        // No intersection, because lines are parallel; just add to appropriate list
        if (numer < 0.0)
           {
            // Current line is in front of root line; link into
            // front list
            ip->vertex = false;
            lt = onright;
           }
        else
        if (numer > 0.0)
           {
            // Current line behind root line; link into back list
            ip->vertex = false;
            lt = onleft;
           }
        else // lines are not only parallel but are colinear - check direction
           {
            if ((r->nu.x == l->nu.x) && (r->nu.y == l->nu.y))  // normals are identical lines are parallel
               {
                lt = colinear;
               }
            else // lines are opposite
               {
                lt = revlinear;
               }
           }
       }
    else
       {
        t = numer / denom;
        if (( t > 0.001 ) && (t < 0.999))
           {
            lt = intersected;
            ip->v.x = l->v[0].x + (l->v[1].x - l->v[0].x) * t;
            ip->v.y = l->v[0].y + (l->v[1].y - l->v[0].y) * t;

            ipx = ip->v.x;
            if ((ipx >= (ip->v.x - 0.5)) && (ipx <= (ip->v.x + 0.5)))
               {
                ip->v.x = ipx;
               }
            else
            if (((ipx+1) >= (ip->v.x - 0.5)) && ((ipx+1) <= (ip->v.x + 0.5)))
               {
                ip->v.x = ipx+1;
               }
            else
            if (((ipx-1) >= (ip->v.x - 0.5)) && ((ipx-1) <= (ip->v.x + 0.5)))
               {
                ip->v.x = ipx-1;
               }

            ipy = ip->v.y;
            if ((ipy >= (ip->v.y - 0.5)) && (ipy <= (ip->v.y + 0.5)))
               {
                ip->v.y = ipy;
               }
            else
            if (((ipy+1) >= (ip->v.y - 0.5)) && ((ipy+1) <= (ip->v.y + 0.5)))
               {
                ip->v.y = ipy+1;
               }
            else
            if (((ipy-1) >= (ip->v.y - 0.5)) && ((ipy-1) <= (ip->v.y + 0.5)))
               {
                ip->v.y = ipy-1;
               }

            blen = ((ip->v.x-bverts[r->vert[0]].x)*(ip->v.x-bverts[r->vert[0]].x))+((ip->v.y-bverts[r->vert[0]].y)*(ip->v.y-bverts[r->vert[0]].y));
            flen = ((ip->v.x-bverts[r->vert[1]].x)*(ip->v.x-bverts[r->vert[1]].x))+((ip->v.y-bverts[r->vert[1]].y)*(ip->v.y-bverts[r->vert[1]].y));
            //blen = ((ip->v.x-r->v[0].x)*(ip->v.x-r->v[0].x))+((ip->v.y-r->v[0].y)*(ip->v.y-r->v[0].y));
            //flen = ((ip->v.x-r->v[1].x)*(ip->v.x-r->v[1].x))+((ip->v.y-r->v[1].y)*(ip->v.y-r->v[1].y));
            if (blen < flen)
               {
                ip->forward = false;
                ip->length = blen;
               }
            else
               {
                ip->forward = true;
                ip->length = flen;
               }
            if (CheckVerts(&bverts[r->vert[0]], &bverts[l->vert[0]]) || CheckVerts(&bverts[r->vert[0]], &bverts[l->vert[1]]))
            //if (CheckVerts(&r->v[0], &l->v[0]) || CheckVerts(&r->v[0], &l->v[1]))
               {
#ifdef WDEBUG
                lfprintf("IP snapped to startpoint.\n");
#endif
                ip->v.x = bverts[r->vert[0]].x;
                ip->v.y = bverts[r->vert[0]].y;
                //ip->v.x = r->v[0].x;
                //ip->v.y = r->v[0].y;
               }
            if (CheckVerts(&bverts[r->vert[1]], &bverts[l->vert[0]]) || CheckVerts(&bverts[r->vert[1]], &bverts[l->vert[1]]))
            //if (CheckVerts(&r->v[1], &l->v[0]) || CheckVerts(&r->v[1], &l->v[1]))
               {
#ifdef WDEBUG
                lfprintf("IP snapped to endpoint.\n");
#endif
                ip->v.x = bverts[r->vert[1]].x;
                ip->v.y = bverts[r->vert[1]].y;
                //ip->v.x = r->v[1].x;
                //ip->v.y = r->v[1].y;
               }
            else
            //icount++;
            if (numer < 0.0)
               {
                // Current line is in front of root line -- link into front list
                //lfprintf("Old right, new left...\n");
                lt = newleft;
               }
            else
            if (numer > 0.0)
               {
                // Current line is behind root line -- link into back list
                //lfprintf("New right, old left...\n");
                lt = newright;
               }
            else
               {
                // denom != 0.0f but numer == 0.0f
#ifdef WDEBUG
                lfprintf("Intersect at end point?\n");
#endif
               }
           }
        else
           {
            ip->v.x = bverts[l->vert[0]].x + (bverts[l->vert[1]].x - bverts[l->vert[0]].x) * t;
            ip->v.y = bverts[l->vert[0]].y + (bverts[l->vert[1]].y - bverts[l->vert[0]].y) * t;
            //ip->v.x = l->v[0].x + (l->v[1].x - l->v[0].x) * t;
            //ip->v.y = l->v[0].y + (l->v[1].y - l->v[0].y) * t;

            ipx = ip->v.x;
            if ((ipx >= (ip->v.x - 0.5)) && (ipx <= (ip->v.x + 0.5)))
               {
                ip->v.x = ipx;
               }
            else
            if (((ipx+1) >= (ip->v.x - 0.5)) && ((ipx+1) <= (ip->v.x + 0.5)))
               {
                ip->v.x = ipx+1;
               }
            else
            if (((ipx-1) >= (ip->v.x - 0.5)) && ((ipx-1) <= (ip->v.x + 0.5)))
               {
                ip->v.x = ipx-1;
               }

            ipy = ip->v.y;
            if ((ipy >= (ip->v.y - 0.5)) && (ipy <= (ip->v.y + 0.5)))
               {
                ip->v.y = ipy;
               }
            else
            if (((ipy+1) >= (ip->v.y - 0.5)) && ((ipy+1) <= (ip->v.y + 0.5)))
               {
                ip->v.y = ipy+1;
               }
            else
            if (((ipy-1) >= (ip->v.y - 0.5)) && ((ipy-1) <= (ip->v.y + 0.5)))
               {
                ip->v.y = ipy-1;
               }

            if ((FLOAT_EQ(ip->v.x, bverts[l->vert[0]].x) && FLOAT_EQ(ip->v.y, bverts[l->vert[0]].y)) ||
                (FLOAT_EQ(ip->v.x, bverts[l->vert[1]].x) && FLOAT_EQ(ip->v.y, bverts[l->vert[1]].y)))
            //if ((FLOAT_EQ(ip->v.x, l->v[0].x) && FLOAT_EQ(ip->v.y, l->v[0].y)) ||
            //    (FLOAT_EQ(ip->v.x, l->v[1].x) && FLOAT_EQ(ip->v.y, l->v[1].y)))
               {
                ip->vertex = true;
               }
            else
               {
                ip->vertex = false;
               }
            blen = ((ip->v.x-bverts[r->vert[0]].x)*(ip->v.x-bverts[r->vert[0]].x))+((ip->v.y-bverts[r->vert[0]].y)*(ip->v.y-bverts[r->vert[0]].y));
            flen = ((ip->v.x-bverts[r->vert[1]].x)*(ip->v.x-bverts[r->vert[1]].x))+((ip->v.y-bverts[r->vert[1]].y)*(ip->v.y-bverts[r->vert[1]].y));
            //blen = ((ip->v.x-r->v[0].x)*(ip->v.x-r->v[0].x))+((ip->v.y-r->v[0].y)*(ip->v.y-r->v[0].y));
            //flen = ((ip->v.x-r->v[1].x)*(ip->v.x-r->v[1].x))+((ip->v.y-r->v[1].y)*(ip->v.y-r->v[1].y));
            if (blen < flen)
               {
                ip->forward = false;
                ip->length = blen;
               }
            else
               {
                ip->forward = true;
                ip->length = flen;
               }
            Done = false;
#ifdef WDEBUG
            lfprintf( "numer = %lf, nx = %lf, ny = %lf\n", numer, nx, ny);
#endif
            if ((numer < 0.0) && (numer > -5.0))
               {
                numer = 0.0;
               }
            while (!Done)
               {
#ifdef WDEBUG
                lfprintf( "numer = %lf\n", numer);
#endif
                if (numer < 0.0)
                   {
                    // Current line is in front of root line -- link into front list
                    lt = onright;
                    Done = true;
                   }
                else
                if (numer > 0.0)
                   {
                    // Current line is behind root line -- link into back list
                    lt = onleft;
                    Done = true;
                   }
                else
                if (numer == 0.0)
                   {
                    numer = (nx*(bverts[l->vert[1]].x - bverts[r->vert[0]].x))+(ny*(bverts[l->vert[1]].y - bverts[r->vert[0]].y));
                    //numer = (nx*(l->v[1].x - r->v[0].x))+(ny*(l->v[1].y - r->v[0].y));
#ifdef WDEBUG
                    lfprintf( "Tiebreak: numer = %lf\n", numer);
#endif
                    if (numer == 0.0)
                       {
                        Done = true;
                       }
                   }
               }
           }
       }

    ip->type = lt;
    return lt;
   }

intersect_t ipl[256];

int FindIntersection(dboolean bForward, int imax)
   {
    int   ipoint, i;
    float fShorter;

    ipoint = -1;

    fShorter = 0.0f;
    for (i = 0; i < imax; i++)
       {
        if ((ipl[i].forward == bForward) && (ipl[i].length > fShorter))
           {
            fShorter = ipl[i].length;
            ipoint = i;
           }
       }
    return ipoint;
   }

dboolean CheckInsideBBox(int sector, double x, double y)
   {
    if ((SectorBBox[sector].left > x) || (SectorBBox[sector].right < x) ||
        (SectorBBox[sector].top < y) || (SectorBBox[sector].bottom > y))
       {
        lfprintf("Sector %d BBOX %d,%d to %d,%d : point %lf,%lf\n",
                 SectorBBox[sector].left, SectorBBox[sector].top,
                 SectorBBox[sector].right, SectorBBox[sector].bottom,
                 x, y);
        return false;
       }
    else
       {
        return true;
       }
   }

void PartitionSection(int sector, int section)
   {
    int          rline, cline, c, i, ipc, p, ix, iy, nv1, nv2;
    double       dx, dy;
    linetype_t   lt;
    intersect_t  ip;

    c = 0;
    for (i = 0; i < lcount; i++)
       {
        if (bsplines[i].section == section)
           {
            c++;
           }
       }
#ifdef WDEBUG
    lfprintf( "Sector %d, section %d, has %d line segments.\n", sector, section, c);
#endif

    c = 0;
    //icount = 0;
    for (rline = 0; ((rline < lcount) && (rline < 2048)); rline++)
       {
        newsection = false;
        i = 0;
        if (bsplines[rline].section != section)
           {
            continue;
#ifdef WDEBUG
            lfprintf("Wrong section...\n");
#endif
           }
#ifdef WDEBUG
//        lfprintf( "Processing line %d %.5f,%.5f to %.5f,%.5f\n",
//                rline, bsplines[rline].v[0].x,bsplines[rline].v[0].y,
//                bsplines[rline].v[1].x,bsplines[rline].v[1].y);
        lfprintf( "Processing line %d %.5f,%.5f to %.5f,%.5f\n",
                rline, bverts[bsplines[rline].vert[0]].x,bverts[bsplines[rline].vert[0]].y,
                bverts[bsplines[rline].vert[1]].x,bverts[bsplines[rline].vert[1]].y);
#endif
        if (bsplines[rline].used == true)
           {
#ifdef WDEBUG
            lfprintf("Line already used...\n");
#endif
            continue;
           }
        ipc = 0;
        for (cline = 0; ((cline < lcount) && (cline < 2048)); cline++)
           {
            if (rline == cline)
               {
                continue;
               }

            if (bsplines[rline].section != bsplines[cline].section)
                continue;

            bsplines[cline].colinear = false;

#ifdef WDEBUG
//            lfprintf( "\nAgainst line %d %.5f,%.5f to %.5f,%.5f\n",
//                    cline, bsplines[cline].v[0].x,bsplines[cline].v[0].y,bsplines[cline].v[1].x,bsplines[cline].v[1].y);
            lfprintf( "\nAgainst line %d %.5f,%.5f to %.5f,%.5f\n",
                    cline, bverts[bsplines[cline].vert[0]].x,bverts[bsplines[cline].vert[0]].y,
                           bverts[bsplines[cline].vert[1]].x,bverts[bsplines[cline].vert[1]].y);
#endif
            lt = LineTest(&bsplines[rline], &bsplines[cline], &ip);
            //inumb = icount-1;
            switch(lt)
               {
                case colinear:
#ifdef WDEBUG
                     lfprintf("Lines colinear - drop line.\n");
#endif
//                     bsplines[cline].section = -1;
/*
                     if (newsection == true)
                        {
#ifdef WDEBUG
                         lfprintf("Add copy of line (reversed) to new section...\n");
#endif
                         AddNewLine( &bsplines[cline].v[1], &bsplines[cline].v[0], sector );
                         bsplines[lcount].section = scount-1;
                         bsplines[lcount].used = true;
                        }
*/
                     bsplines[cline].used = true;  // this line has already been tested...
                     bsplines[cline].colinear = true;
                     break;
                case revlinear:
#ifdef WDEBUG
                     lfprintf("Lines (reverse)colinear.\n");
#endif
                     if (newsection == false)
                        {
#ifdef WDEBUG
                         lfprintf( "Adding new section %d...\n", scount);
#endif
                         newsection = true;
                         scount++;
                        }
                     bsplines[cline].section = scount-1;
#ifdef WDEBUG
                     lfprintf("Current line moved to new section...\n");
#endif
/*
                     if (newsection == true)
                        {
#ifdef WDEBUG
                         lfprintf("Add copy of line (reversed) to current section...\n");
#endif
                         // add a copy of the line to the list
                         AddNewLine( &bsplines[cline].v[1], &bsplines[cline].v[0], sector );
                         // put new line in the current section
                         bsplines[lcount].section = section;
                         // put original line in new section
                         bsplines[cline].section = scount-1;
                        }
*/
                     break;
                case intersected:
#ifdef WDEBUG
                     lfprintf("Lines intersect - do nothing.\n");
#endif
                     // do nothing
                     break;
                case onleft:
#ifdef WDEBUG
                     lfprintf("Line on left - do nothing.\n");
#endif
                     if (ip.vertex == true)
                        {
                         //if ((!FLOAT_EQ(ip.v.x, bsplines[rline].v[0].x) ||
                         //     !FLOAT_EQ(ip.v.y, bsplines[rline].v[0].y)) &&
                         //    (!FLOAT_EQ(ip.v.x, bsplines[rline].v[1].x) ||
                         //     !FLOAT_EQ(ip.v.y, bsplines[rline].v[1].y)))
                         if ((!FLOAT_EQ(ip.v.x, bverts[bsplines[rline].vert[0]].x) ||
                              !FLOAT_EQ(ip.v.y, bverts[bsplines[rline].vert[0]].y)) &&
                             (!FLOAT_EQ(ip.v.x, bverts[bsplines[rline].vert[1]].x) ||
                              !FLOAT_EQ(ip.v.y, bverts[bsplines[rline].vert[1]].y)))
                            {
                             for (i = 0; i < ipc; i++)
                                {
                                 if (FLOAT_EQ(ip.v.x, ipl[i].v.x) && FLOAT_EQ(ip.v.y, ipl[i].v.y))
                                    {
                                     ip.v.x = ipl[i].v.x;
                                     ip.v.y = ipl[i].v.y;
                                     break;
                                    }
                                }
                             if (i == ipc)
                                {
#ifdef WDEBUG
                                 lfprintf( "Vertex intersected at %.2f,%.2f\n", ip.v.x, ip.v.y);
#endif
                                 ipl[ipc].v.x = ip.v.x;
                                 ipl[ipc].v.y = ip.v.y;
                                 ipl[ipc].type = ip.type;
                                 ipl[ipc].forward = ip.forward;
                                 ipl[ipc].length = ip.length;
                                 ipc++;
                                }
                            }
                        }
                     // do nothing
                     break;
                case onright:
#ifdef WDEBUG
                     lfprintf("onright\n");
#endif
                     if (newsection == false)
                        {
#ifdef WDEBUG
                         lfprintf( "Adding new section %d...\n", scount);
#endif
                         newsection = true;
                         scount++;
                        }
                     if (ip.vertex == true)
                        {
                         //if ((!FLOAT_EQ(ip.v.x, bsplines[rline].v[0].x) ||
                         //     !FLOAT_EQ(ip.v.y, bsplines[rline].v[0].y)) &&
                         //    (!FLOAT_EQ(ip.v.x, bsplines[rline].v[1].x) ||
                         //     !FLOAT_EQ(ip.v.y, bsplines[rline].v[1].y)))
                         if ((!FLOAT_EQ(ip.v.x, bverts[bsplines[rline].vert[0]].x) ||
                              !FLOAT_EQ(ip.v.y, bverts[bsplines[rline].vert[0]].y)) &&
                             (!FLOAT_EQ(ip.v.x, bverts[bsplines[rline].vert[1]].x) ||
                              !FLOAT_EQ(ip.v.y, bverts[bsplines[rline].vert[1]].y)))
                            {
                             for (i = 0; i < ipc; i++)
                                {
                                 if (FLOAT_EQ(ip.v.x, ipl[i].v.x) && FLOAT_EQ(ip.v.y, ipl[i].v.y))
                                    {
                                     ip.v.x = ipl[i].v.x;
                                     ip.v.y = ipl[i].v.y;
                                     break;
                                    }
                                }
                             if (i == ipc)
                                {
#ifdef WDEBUG
                                 lfprintf( "Vertex intersected at %.2f,%.2f\n", ip.v.x, ip.v.y);
#endif
                                 ipl[ipc].v.x = ip.v.x;
                                 ipl[ipc].v.y = ip.v.y;
                                 ipl[ipc].type = ip.type;
                                 ipl[ipc].forward = ip.forward;
                                 ipl[ipc].length = ip.length;
                                 ipc++;
                                }
                            }
                        }
#ifdef WDEBUG
                     lfprintf("Line on right - added to new section.\n");
#endif
                     bsplines[cline].section = scount-1;
                     break;
                case newright:
                     if (!CheckInsideBBox(sector, ip.v.x, ip.v.y))
                        {
                         lt = onleft;
                         break;
                        }
#ifdef WDEBUG
                     lfprintf("newright\n");
#endif
                     for (i = 0; i < ipc; i++)
                        {
                         if (FLOAT_EQ(ip.v.x, ipl[i].v.x) && FLOAT_EQ(ip.v.y, ipl[i].v.y))
                            {
                             ip.v.x = ipl[i].v.x;
                             ip.v.y = ipl[i].v.y;
                             break;
                            }
                        }
                     if (i == ipc)
                        {
#ifdef WDEBUG
                         lfprintf( "Vertex intersected at %.2f,%.2f\n", ip.v.x, ip.v.y);
#endif
                         ipl[ipc].v.x = ip.v.x;
                         ipl[ipc].v.y = ip.v.y;
                         ipl[ipc].type = ip.type;
                         ipl[ipc].forward = ip.forward;
                         ipl[ipc].length = ip.length;
                         ipc++;
                        }
                     if (newsection == false)
                        {
#ifdef WDEBUG
                         lfprintf( "Adding new section %d...line split to right\n", scount);
#endif
                         newsection = true;
                         scount++;
                        }
                     if (FLOAT_EQ(ip.v.x, bverts[bsplines[cline].vert[0]].x) && FLOAT_EQ(ip.v.y, bverts[bsplines[cline].vert[0]].y))
                     //if (FLOAT_EQ(ip.v.x, bsplines[cline].v[0].x) && FLOAT_EQ(ip.v.y, bsplines[cline].v[0].y))
                        {
#ifdef WDEBUG
                         lfprintf("Intersection is at an beginning of line...\n");
#endif
                         ipl[ipc-1].v.x = bverts[bsplines[cline].vert[0]].x;
                         ipl[ipc-1].v.y = bverts[bsplines[cline].vert[0]].y;
                         //ipl[ipc-1].v.x = bsplines[cline].v[0].x;
                         //ipl[ipc-1].v.y = bsplines[cline].v[0].y;
                        }
                     else
                     if (FLOAT_EQ(ip.v.x, bverts[bsplines[cline].vert[1]].x) && FLOAT_EQ(ip.v.y, bverts[bsplines[cline].vert[1]].y))
                     //if (FLOAT_EQ(ip.v.x, bsplines[cline].v[1].x) && FLOAT_EQ(ip.v.y, bsplines[cline].v[1].y))
                        {
#ifdef WDEBUG
                         lfprintf("Intersection is at an end of line...\n");
#endif
                         ipl[ipc-1].v.x = bverts[bsplines[cline].vert[1]].x;
                         ipl[ipc-1].v.y = bverts[bsplines[cline].vert[1]].y;
                         //ipl[ipc-1].v.x = bsplines[cline].v[1].x;
                         //ipl[ipc-1].v.y = bsplines[cline].v[1].y;
                        }
                     else
                        {
#ifdef WDEBUG
                         lfprintf( "New line added  %d %f,%f to %f,%f\n", lcount,
                                 ip.v.x, ip.v.y, bsplines[cline].v[1].x,bsplines[cline].v[1].y );
#endif
                         // Add a new line using the end of the old line
                         if ((nv1 = FindVertex(ip.v.x, ip.v.y)) == -1)
                            {
                             nv1 = AddVertex(ip.v.x, ip.v.y);
                            }
                         AddNewLine( &ip.v,  &bverts[bsplines[cline].vert[1]], nv1, bsplines[cline].vert[1], sector );
                         //AddNewLine( &ip.v,  &bsplines[cline].v[1], nv1, bsplines[cline].vert[1], sector );
                         // Shift ending point of old line to intersection point
                         bsplines[cline].v[1].x = ip.v.x;
                         bsplines[cline].v[1].y = ip.v.y;
                         bsplines[cline].vert[1] = nv1;
                         // Adjust the line "normal" (not a 'unit' normal...)
                         bsplines[cline].n.x = bverts[bsplines[cline].vert[0]].y - bverts[bsplines[cline].vert[1]].y;
                         bsplines[cline].n.y = -(bverts[bsplines[cline].vert[0]].x - bverts[bsplines[cline].vert[1]].x);
                         //bsplines[cline].n.x = bsplines[cline].v[0].y - bsplines[cline].v[1].y;
                         //bsplines[cline].n.y = -(bsplines[cline].v[0].x - bsplines[cline].v[1].x);
#ifdef WDEBUG
                         //lfprintf( "Old line changed to %f,%f to %f,%f\n",
                         //        bsplines[cline].v[0].x,bsplines[cline].v[0].y, bsplines[cline].v[1].x,bsplines[cline].v[1].y);
                         lfprintf( "Old line changed to %f,%f to %f,%f\n",
                                 bverts[bsplines[cline].vert[0]].x, bverts[bsplines[cline].vert[0]].y, bverts[bsplines[cline].vert[1]].x, bverts[bsplines[cline].vert[1]].y);
#endif
                        }
#ifdef WDEBUG
                     lfprintf("\n");
#endif
                     break;
                case newleft:
#ifdef WDEBUG
                     lfprintf("newleft\n");
#endif
                     if (!CheckInsideBBox(sector, ip.v.x, ip.v.y))
                        {
                         lt = onright;
                         if (newsection == false)
                            {
#ifdef WDEBUG
                             lfprintf( "Adding new section %d...\n", scount);
#endif
                             newsection = true;
                             scount++;
                            }
                         bsplines[cline].section = scount-1;
                         break;
                        }
                     for (i = 0; i < ipc; i++)
                        {
                         if (FLOAT_EQ(ip.v.x, ipl[i].v.x) && FLOAT_EQ(ip.v.y, ipl[i].v.y))
                            {
                             ip.v.x = ipl[i].v.x;
                             ip.v.y = ipl[i].v.y;
                             break;
                            }
                        }
                     if (i == ipc)
                        {
#ifdef WDEBUG
                         lfprintf( "Vertex intersected at %.2f,%.2f\n", ip.v.x, ip.v.y);
#endif
                         ipl[ipc].v.x = ip.v.x;
                         ipl[ipc].v.y = ip.v.y;
                         ipl[ipc].type = ip.type;
                         ipl[ipc].forward = ip.forward;
                         ipl[ipc].length = ip.length;
                         ipc++;
                        }
                     if (newsection == false)
                        {
#ifdef WDEBUG
                         lfprintf( "Adding new section %d...line split to left\n", scount);
#endif
                         newsection = true;
                         scount++;
                        }
                     if (FLOAT_EQ(ip.v.x, bverts[bsplines[cline].vert[0]].x) && FLOAT_EQ(ip.v.y, bverts[bsplines[cline].vert[0]].y))
                     //if (FLOAT_EQ(ip.v.x, bsplines[cline].v[0].x) && FLOAT_EQ(ip.v.y, bsplines[cline].v[0].y))
                        {
#ifdef WDEBUG
                         lfprintf("Intersection is at an beginning of line...\n");
#endif
                         ipl[ipc-1].v.x = bsplines[cline].v[0].x;
                         ipl[ipc-1].v.y = bsplines[cline].v[0].y;
                        }
                     else
                     if (FLOAT_EQ(ip.v.x, bverts[bsplines[cline].vert[1]].x) && FLOAT_EQ(ip.v.y, bverts[bsplines[cline].vert[1]].y))
                     //if (FLOAT_EQ(ip.v.x, bsplines[cline].v[1].x) && FLOAT_EQ(ip.v.y, bsplines[cline].v[1].y))
                        {
#ifdef WDEBUG
                         lfprintf("Intersection is at an end of line...\n");
#endif
                         ipl[ipc-1].v.x = bsplines[cline].v[1].x;
                         ipl[ipc-1].v.y = bsplines[cline].v[1].y;
                        }
                     else
                        {
#ifdef WDEBUG
                         lfprintf( "New line added  %d %f,%f to %f,%f\n", lcount, 
                                 bsplines[cline].v[0].x,bsplines[cline].v[0].y, ip.v.x, ip.v.y);
#endif
                         // Add a new line using the beginning of the old line
                         if ((nv2 = FindVertex(ip.v.x, ip.v.y)) == -1)
                            {
                             nv2 = AddVertex(ip.v.x, ip.v.y);
                            }
                         AddNewLine( &bverts[bsplines[cline].vert[0]], &ip.v, bsplines[cline].vert[0], nv2, sector );
                         //AddNewLine( &bsplines[cline].v[0], &ip.v, bsplines[cline].vert[0], nv2, sector );
                         // Shift starting point of old line to intersection point
                         bsplines[cline].v[0].x = ip.v.x;
                         bsplines[cline].v[0].y = ip.v.y;
                         bsplines[cline].vert[0] = nv2;
                         bsplines[cline].n.x = bverts[bsplines[cline].vert[0]].y - bverts[bsplines[cline].vert[1]].y;
                         bsplines[cline].n.y = -(bverts[bsplines[cline].vert[0]].x - bverts[bsplines[cline].vert[1]].x);
                         //bsplines[cline].n.x = bsplines[cline].v[0].y - bsplines[cline].v[1].y;
                         //bsplines[cline].n.y = -(bsplines[cline].v[0].x - bsplines[cline].v[1].x);
#ifdef WDEBUG
                         //lfprintf( "Old line changed to %f,%f to %f,%f\n",
                         //        bsplines[cline].v[0].x,bsplines[cline].v[0].y, bsplines[cline].v[1].x,bsplines[cline].v[1].y);
                         lfprintf( "Old line changed to %f,%f to %f,%f\n",
                                 bverts[bsplines[cline].vert[0]].x, bverts[bsplines[cline].vert[0]].y, bverts[bsplines[cline].vert[1]].x, bverts[bsplines[cline].vert[1]].y);
#endif
                        }
#ifdef WDEBUG
                     lfprintf("\n");
#endif
                     break;
                default:
                     lfprintf( "Unknown state 0x%4X returned.\n", lt);
                     break;
               }
            i++;
           }
        bsplines[rline].used = true;
        //icount = 0;
        if ((ipc > 0) && (newsection == true))
           {
#ifdef WDEBUG
            lfprintf( "\nFound %d intersections\n", ipc);
#endif
            // process all the forward intersections

            if ((p = FindIntersection(true, ipc)) != -1)
               {
                // use farthest intersect to extend current line segment p1 forward
#ifdef WDEBUG
                if ((ipl[p].v.x == bsplines[rline].v[1].x) && (ipl[p].v.y == bsplines[rline].v[1].y))
                   {
                    lfprintf("Intersected at vertices - no new lines...\n");
                   }
                else
#endif
                if ((ipl[p].v.x != bsplines[rline].v[1].x) || (ipl[p].v.y != bsplines[rline].v[1].y))
                   {
                    // and create a new line segment (reversed) in the new section
#ifdef WDEBUG
                    lfprintf("Add new line to \"back\" sector nl...\n");
                    lfprintf( "%d From %f,%f to %f,%f\n", lcount,
                            ipl[p].v.x,ipl[p].v.y, bsplines[rline].v[1].x,bsplines[rline].v[1].y);
#endif
                    if ((nv1 = FindVertex(ipl[p].v.x, ipl[p].v.y)) == -1)
                       {
                        nv1 = AddVertex(ipl[p].v.x, ipl[p].v.y);
                       }
                    AddNewLine( &ipl[p].v, &bverts[bsplines[rline].vert[1]], nv1, bsplines[rline].vert[1], sector );
                    //AddNewLine( &ipl[p].v, &bsplines[rline].v[1], nv1, bsplines[rline].vert[1], sector );
                    bsplines[lcount-1].used = true;
#ifdef WDEBUG
                    //lfprintf( "End point moved from %f,%f to %f,%f\n",
                    //        bsplines[rline].v[1].x,bsplines[rline].v[1].y, ipl[p].v.x,ipl[p].v.y);
                    lfprintf( "End point moved from %f,%f to %f,%f\n",
                            bverts[bsplines[rline].vert[1]].x, bverts[bsplines[rline].vert[1]].y, ipl[p].v.x,ipl[p].v.y);
#endif
                    bsplines[rline].v[1].x = ipl[p].v.x;
                    bsplines[rline].v[1].y = ipl[p].v.y;
                    bsplines[rline].vert[1] = nv1;
                    bsplines[rline].n.x = bverts[bsplines[rline].vert[0]].y - bverts[bsplines[rline].vert[1]].y;
                    bsplines[rline].n.y = -(bverts[bsplines[rline].vert[0]].x - bverts[bsplines[rline].vert[1]].x);
                    //bsplines[rline].n.x = bsplines[rline].v[0].y - bsplines[rline].v[1].y;
                    //bsplines[rline].n.y = -(bsplines[rline].v[0].x - bsplines[rline].v[1].x);
                   }
               }

            // process all the backward intersections
            if ((p = FindIntersection(false, ipc)) != -1)
               {
                // use closest intersect to extend current line segment p1 forward
#ifdef WDEBUG
                if ((ipl[p].v.x == bsplines[rline].v[0].x) && (ipl[p].v.y == bsplines[rline].v[0].y))
                   {
                    lfprintf("Intersected at vertices - no new lines...\n");
                   }
                else
#endif
                if ((ipl[p].v.x != bsplines[rline].v[0].x) || (ipl[p].v.y != bsplines[rline].v[0].y))
                   {
#ifdef WDEBUG
                    lfprintf("Add new line to \"back\" sector nl...\n");
                    lfprintf( "%d From %f,%f to %f,%f\n", lcount,
                            bsplines[rline].v[0].x,bsplines[rline].v[0].y,
                            ipl[p].v.x,ipl[p].v.y);
#endif
                    if ((nv2 = FindVertex(ipl[p].v.x, ipl[p].v.y)) == -1)
                       {
                        nv2 = AddVertex(ipl[p].v.x, ipl[p].v.y);
                       }
                    AddNewLine( &bverts[bsplines[rline].vert[0]], &ipl[p].v, bsplines[rline].vert[0], nv2, sector );
                    //AddNewLine( &bsplines[rline].v[0], &ipl[p].v, bsplines[rline].vert[0], nv2, sector );
                    bsplines[lcount-1].used = true;
#ifdef WDEBUG
                    //lfprintf( "Start point moved from %f,%f to %f,%f\n",
                    //        bsplines[rline].v[0].x,bsplines[rline].v[0].y, ipl[p].v.x,ipl[p].v.y);
                    lfprintf( "Start point moved from %f,%f to %f,%f\n",
                            bverts[bsplines[rline].vert[0]].x, bverts[bsplines[rline].vert[0]].y, ipl[p].v.x,ipl[p].v.y);
#endif
                    bsplines[rline].v[0].x = ipl[p].v.x;
                    bsplines[rline].v[0].y = ipl[p].v.y;
                    bsplines[rline].vert[0] = nv2;
                    bsplines[rline].n.x = bverts[bsplines[rline].vert[0]].y - bverts[bsplines[rline].vert[1]].y;
                    bsplines[rline].n.y = -(bverts[bsplines[rline].vert[0]].x - bverts[bsplines[rline].vert[1]].x);
                    //bsplines[rline].n.x = bsplines[rline].v[0].y - bsplines[rline].v[1].y;
                    //bsplines[rline].n.y = -(bsplines[rline].v[0].x - bsplines[rline].v[1].x);
                   }
               }
           }
       }
   }


int CullColinearSegs(int sector, int section)
   {
    int          rline, cline, i, c;

    c = 0;
    for (rline = 0; ((rline < lcount) && (rline < 2048)); rline++)
       {
        newsection = false;
        i = 0;
        if (bsplines[rline].section != section)
           {
            continue;
           }
#ifdef WDEBUG
        //lfprintf( "Processing line %d %.2f,%.2f to %.2f,%.2f\n",
        //        rline, bsplines[rline].v[0].x,bsplines[rline].v[0].y,
        //        bsplines[rline].v[1].x,bsplines[rline].v[1].y);
        lfprintf( "Processing line %d %.2f,%.2f to %.2f,%.2f\n",
                rline, bverts[bsplines[rline].vert[0]].x,bverts[bsplines[rline].vert[0]].y,
                bverts[bsplines[rline].vert[1]].x,bverts[bsplines[rline].vert[1]].y);
#endif
        for (cline = 0; ((cline < lcount) && (cline < 2048)); cline++)
           {
            if (rline == cline)
               {
                continue;
               }

            if (bsplines[rline].section != bsplines[cline].section)
               {
                continue;
               }

            if (IsColinear(&bsplines[rline], &bsplines[cline]) == true)
               {
#ifdef WDEBUG
                //lfprintf( "Against line %d %.2f,%.2f to %.2f,%.2f\n",
                //    cline, bsplines[cline].v[0].x,bsplines[cline].v[0].y,bsplines[cline].v[1].x,bsplines[cline].v[1].y);
                lfprintf( "Against line %d %.2f,%.2f to %.2f,%.2f\n",
                    cline, bverts[bsplines[cline].vert[0]].x,bverts[bsplines[cline].vert[0]].y,bverts[bsplines[cline].vert[1]].x,bverts[bsplines[cline].vert[1]].y);
#endif
                //if (((bsplines[rline].v[0].x) == (bsplines[cline].v[1].x)) &&
                //    ((bsplines[rline].v[0].y) == (bsplines[cline].v[1].y)))
                if (bsplines[rline].vert[0] == bsplines[cline].vert[1])
                //if (FLOAT_EQ(bverts[bsplines[rline].vert[0]].x, bverts[bsplines[cline].vert[1]].x) &&
                //    FLOAT_EQ(bverts[bsplines[rline].vert[0]].y, bverts[bsplines[cline].vert[1]].y))
                   {
                    // these two lines adjoin at the beginning of rline
#ifdef WDEBUG
                    lfprintf("Lines colinear and adjoining.\n");
#endif
                    // move the beginning of rline to the beginning of cline
                    bsplines[rline].v[0].x = bsplines[cline].v[0].x;
                    bsplines[rline].v[0].y = bsplines[cline].v[0].y;
                    bsplines[rline].vert[0] = bsplines[cline].vert[0];
                    bsplines[rline].n.x = bverts[bsplines[rline].vert[0]].y - bverts[bsplines[rline].vert[1]].y;
                    bsplines[rline].n.y = -(bverts[bsplines[rline].vert[0]].x - bverts[bsplines[rline].vert[1]].x);
                    //bsplines[rline].n.x = bsplines[rline].v[0].y - bsplines[rline].v[1].y;
                    //bsplines[rline].n.y = -(bsplines[rline].v[0].x - bsplines[rline].v[1].x);
                    // drop cline from the list
#ifdef WDEBUG
                    lfprintf("Line dropped.\n");
#endif
                    bsplines[cline].section = -1;
                   }
                else
                //if (((bsplines[rline].v[1].x) == (bsplines[cline].v[0].x)) &&
                //    ((bsplines[rline].v[1].y) == (bsplines[cline].v[0].y)))
                if (bsplines[rline].vert[1] == bsplines[cline].vert[0])
                //if (FLOAT_EQ(bverts[bsplines[rline].vert[1]].x, bverts[bsplines[cline].vert[0]].x) &&
                //    FLOAT_EQ(bverts[bsplines[rline].vert[1]].y, bverts[bsplines[cline].vert[0]].y))
                   {
                    // these two lines adjoin at the end of rline
#ifdef WDEBUG
                    lfprintf("Lines colinear and adjoining.\n");
#endif
                    // move the end of rline to the end of cline
                    bsplines[rline].v[1].x = bsplines[cline].v[1].x;
                    bsplines[rline].v[1].y = bsplines[cline].v[1].y;
                    bsplines[rline].vert[1] = bsplines[cline].vert[1];
                    bsplines[rline].n.x = bverts[bsplines[rline].vert[0]].y - bverts[bsplines[rline].vert[1]].y;
                    bsplines[rline].n.y = -(bverts[bsplines[rline].vert[0]].x - bverts[bsplines[rline].vert[1]].x);
                    //bsplines[rline].n.x = bsplines[rline].v[0].y - bsplines[rline].v[1].y;
                    //bsplines[rline].n.y = -(bsplines[rline].v[0].x - bsplines[rline].v[1].x);
                    // drop cline from the list
#ifdef WDEBUG
                    lfprintf("Line dropped.\n");
#endif
                    bsplines[cline].section = -1;
                   }
                if (bsplines[cline].section == -1)
                   {
#ifdef WDEBUG
                    //lfprintf( "New Coordinates for %d %.2f,%.2f to %.2f,%.2f\n", rline,
                    //        bsplines[rline].v[0].x,bsplines[rline].v[0].y,
                    //        bsplines[rline].v[1].x,bsplines[rline].v[1].y);
                    lfprintf( "New Coordinates for %d %.2f,%.2f to %.2f,%.2f\n", rline,
                            bverts[bsplines[rline].vert[0]].x,bverts[bsplines[rline].vert[0]].y,
                            bverts[bsplines[rline].vert[1]].x,bverts[bsplines[rline].vert[1]].y);
#endif
                    c++;
                   }
               }
           }
        bsplines[rline].used = false;
       }
    return c;
   }

int JoinColinearSegs(int sector, int section)
   {
    int          rline, cline, culled;
    float        rlen, l1, l2;

    culled = 0;
    for (rline = 0; ((rline < lcount) && (rline < 2048)); rline++)
       {
        newsection = false;
        if (bsplines[rline].section != section)
           {
            continue;
           }
#ifdef WDEBUG
        //lfprintf( "Processing line %d %.2f,%.2f to %.2f,%.2f\n",
        //        rline, bsplines[rline].v[0].x,bsplines[rline].v[0].y,
        //        bsplines[rline].v[1].x,bsplines[rline].v[1].y);
        lfprintf( "Processing line %d %.2f,%.2f to %.2f,%.2f\n",
                rline, bverts[bsplines[rline].vert[0]].x,bverts[bsplines[rline].vert[0]].y,
                bverts[bsplines[rline].vert[1]].x,bverts[bsplines[rline].vert[1]].y);
#endif
        for (cline = 0; ((cline < lcount) && (cline < 2048)); cline++)
           {
            if (rline == cline)
               {
                continue;
               }

            if (bsplines[rline].section != bsplines[cline].section)
               {
                continue;
               }

#ifdef WDEBUG
            //lfprintf( "Against line %d %.2f,%.2f to %.2f,%.2f\n",
            //        cline, bsplines[cline].v[0].x,bsplines[cline].v[0].y,bsplines[cline].v[1].x,bsplines[cline].v[1].y);
            lfprintf( "Against line %d %.2f,%.2f to %.2f,%.2f\n",
                    cline, bverts[bsplines[cline].vert[0]].x, bverts[bsplines[cline].vert[0]].y, bverts[bsplines[cline].vert[1]].x, bverts[bsplines[cline].vert[1]].y);
#endif

            if (IsColinear(&bsplines[rline], &bsplines[cline]) == true)
               {
#ifdef WDEBUG
                lfprintf("Lines colinear.\n");
#endif
//                // get the length of the current line segment
//                rlen = (((bsplines[rline].v[0].x-bsplines[rline].v[1].x)*
//                         (bsplines[rline].v[0].x-bsplines[rline].v[1].x)) +
//                        ((bsplines[rline].v[0].y-bsplines[rline].v[1].y)*
//                         (bsplines[rline].v[0].y-bsplines[rline].v[1].y)));
//                // find the distance from the beginning of the current line and the test line
//                l1 = (((bsplines[rline].v[0].x-bsplines[cline].v[0].x)*
//                       (bsplines[rline].v[0].x-bsplines[cline].v[0].x)) +
//                      ((bsplines[rline].v[0].y-bsplines[cline].v[0].y)*
//                       (bsplines[rline].v[0].y-bsplines[cline].v[0].y)));
//                // find the distance from the end of the current line and the test line
//                l2 = (((bsplines[rline].v[1].x-bsplines[cline].v[0].x)*
//                       (bsplines[rline].v[1].x-bsplines[cline].v[0].x)) +
//                      ((bsplines[rline].v[1].y-bsplines[cline].v[0].y)*
//                       (bsplines[rline].v[1].y-bsplines[cline].v[0].y)));
                // get the length of the current line segment
                rlen = (((bverts[bsplines[rline].vert[0]].x-bverts[bsplines[rline].vert[1]].x)*
                         (bverts[bsplines[rline].vert[0]].x-bverts[bsplines[rline].vert[1]].x)) +
                        ((bverts[bsplines[rline].vert[0]].y-bverts[bsplines[rline].vert[1]].y)*
                         (bverts[bsplines[rline].vert[0]].y-bverts[bsplines[rline].vert[1]].y)));
                // find the distance from the beginning of the current line and the test line
                l1 = (((bverts[bsplines[rline].vert[0]].x-bverts[bsplines[cline].vert[0]].x)*
                       (bverts[bsplines[rline].vert[0]].x-bverts[bsplines[cline].vert[0]].x)) +
                      ((bverts[bsplines[rline].vert[0]].y-bverts[bsplines[cline].vert[0]].y)*
                       (bverts[bsplines[rline].vert[0]].y-bverts[bsplines[cline].vert[0]].y)));
                // find the distance from the end of the current line and the test line
                l2 = (((bverts[bsplines[rline].vert[1]].x-bverts[bsplines[cline].vert[0]].x)*
                       (bverts[bsplines[rline].vert[1]].x-bverts[bsplines[cline].vert[0]].x)) +
                      ((bverts[bsplines[rline].vert[1]].y-bverts[bsplines[cline].vert[0]].y)*
                       (bverts[bsplines[rline].vert[1]].y-bverts[bsplines[cline].vert[0]].y)));
                if ((l1 > rlen) || (l2 > rlen))
                   {
                    if (l2 > l1) // test line p0 is farther from current line p0
                       {
#ifdef WDEBUG
                        lfprintf("Moving p0 of current line to p0 of test line\n");
#endif
                        bsplines[rline].v[0].x = bsplines[cline].v[0].x;
                        bsplines[rline].v[0].y = bsplines[cline].v[0].y;
                        bsplines[rline].vert[0] = bsplines[cline].vert[0];
                        bsplines[rline].n.x = bverts[bsplines[rline].vert[0]].y - bverts[bsplines[rline].vert[1]].y;
                        bsplines[rline].n.y = -(bverts[bsplines[rline].vert[0]].x - bverts[bsplines[rline].vert[1]].x);
                        //bsplines[rline].n.x = bsplines[rline].v[0].y - bsplines[rline].v[1].y;
                        //bsplines[rline].n.y = -(bsplines[rline].v[0].x - bsplines[rline].v[1].x);
                       }
                   }
#ifdef WDEBUG
                else
                   {
                    // this point is WITHIN the current line segment - ignore it
                    lfprintf("Point 0 is within the line - ignored.\n");
                   }
#endif
//                // get the length of the current line segment
//                rlen = (((bsplines[rline].v[0].x-bsplines[rline].v[1].x)*
//                         (bsplines[rline].v[0].x-bsplines[rline].v[1].x)) +
//                        ((bsplines[rline].v[0].y-bsplines[rline].v[1].y)*
//                         (bsplines[rline].v[0].y-bsplines[rline].v[1].y)));
//                // find the distance from the beginning of the current line and the test line
//                l1 = (((bsplines[rline].v[0].x-bsplines[cline].v[1].x)*
//                       (bsplines[rline].v[0].x-bsplines[cline].v[1].x)) +
//                      ((bsplines[rline].v[0].y-bsplines[cline].v[1].y)*
//                       (bsplines[rline].v[0].y-bsplines[cline].v[1].y)));
//                // find the distance from the end of the current line and the test line
//                l2 = (((bsplines[rline].v[1].x-bsplines[cline].v[1].x)*
//                       (bsplines[rline].v[1].x-bsplines[cline].v[1].x)) +
//                      ((bsplines[rline].v[1].y-bsplines[cline].v[1].y)*
//                       (bsplines[rline].v[1].y-bsplines[cline].v[1].y)));
                // get the length of the current line segment
                rlen = (((bverts[bsplines[rline].vert[0]].x-bverts[bsplines[rline].vert[1]].x)*
                         (bverts[bsplines[rline].vert[0]].x-bverts[bsplines[rline].vert[1]].x)) +
                        ((bverts[bsplines[rline].vert[0]].y-bverts[bsplines[rline].vert[1]].y)*
                         (bverts[bsplines[rline].vert[0]].y-bverts[bsplines[rline].vert[1]].y)));
                // find the distance from the beginning of the current line and the test line
                l1 = (((bverts[bsplines[rline].vert[0]].x-bverts[bsplines[cline].vert[1]].x)*
                       (bverts[bsplines[rline].vert[0]].x-bverts[bsplines[cline].vert[1]].x)) +
                      ((bverts[bsplines[rline].vert[0]].y-bverts[bsplines[cline].vert[1]].y)*
                       (bverts[bsplines[rline].vert[0]].y-bverts[bsplines[cline].vert[1]].y)));
                // find the distance from the end of the current line and the test line
                l2 = (((bverts[bsplines[rline].vert[1]].x-bverts[bsplines[cline].vert[1]].x)*
                       (bverts[bsplines[rline].vert[1]].x-bverts[bsplines[cline].vert[1]].x)) +
                      ((bverts[bsplines[rline].vert[1]].y-bverts[bsplines[cline].vert[1]].y)*
                       (bverts[bsplines[rline].vert[1]].y-bverts[bsplines[cline].vert[1]].y)));
                if ((l1 > rlen) || (l2 > rlen))
                   {
                    if (l1 > l2) // test line p0 is farther from current line p1
                       {
                        //lfprintf("Moving p1 of current line to p1 of test line\n");
                        bsplines[rline].v[1].x = bsplines[cline].v[1].x;
                        bsplines[rline].v[1].y = bsplines[cline].v[1].y;
                        bsplines[rline].vert[1] = bsplines[cline].vert[1];
                        bsplines[rline].n.x = bverts[bsplines[rline].vert[0]].y - bverts[bsplines[rline].vert[1]].y;
                        bsplines[rline].n.y = -(bverts[bsplines[rline].vert[0]].x - bverts[bsplines[rline].vert[1]].x);
                        //bsplines[rline].n.x = bsplines[rline].v[0].y - bsplines[rline].v[1].y;
                        //bsplines[rline].n.y = -(bsplines[rline].v[0].x - bsplines[rline].v[1].x);
                       }
                   }
#ifdef WDEBUG
                else
                   {
                    // this point is WITHIN the current line segment - ignore it
                    lfprintf("Point 1 is within the line - ignored.\n");
                   }
                //lfprintf( "New Coordinates for %d %.2f,%.2f to %.2f,%.2f\n", rline,
                //        bsplines[rline].v[0].x,bsplines[rline].v[0].y,
                //        bsplines[rline].v[1].x,bsplines[rline].v[1].y);
                lfprintf( "New Coordinates for %d %.2f,%.2f to %.2f,%.2f\n", rline,
                        bverts[bsplines[rline].vert[0]].x,bverts[bsplines[rline].vert[0]].y,
                        bverts[bsplines[rline].vert[1]].x,bverts[bsplines[rline].vert[1]].y);
#endif
                bsplines[cline].section = -1;
                culled++;
               }
           }
       }
    return culled;
   }

void ChainSectionForward(int sector, int section)
   {
    int i, j, n, p, nonext, noprev;

    for (i = 0; i < lcount; i++)
       {
        if (bsplines[i].section != section)
           {
            continue;
           }
        if (FLOAT_EQ(bverts[bsplines[i].vert[0]].x, bverts[bsplines[i].vert[1]].x) &&
            FLOAT_EQ(bverts[bsplines[i].vert[0]].y, bverts[bsplines[i].vert[1]].y))
           {
            //this is a null line...
            bsplines[i].section = -1;
            continue;
           }
        for (j = 0; j < lcount; j++)
           {
            if (i == j)
               {
                continue;
               }
            if (bsplines[j].section != section)
               {
                continue;
               }
            if (FLOAT_EQ(bverts[bsplines[j].vert[0]].x, bverts[bsplines[j].vert[1]].x) &&
                FLOAT_EQ(bverts[bsplines[j].vert[0]].y, bverts[bsplines[j].vert[1]].y))
               {
                //this is a null line...
                bsplines[j].section = -1;
                continue;
               }
            //if (FLOAT_EQ(bsplines[i].v[0].x, bsplines[j].v[1].x) &&
            //    FLOAT_EQ(bsplines[i].v[0].y, bsplines[j].v[1].y))
            if (bsplines[i].vert[0] == bsplines[j].vert[1])
               {
                bsplines[j].next = i;
                bsplines[i].prev = j;
               }
           }
       }
    nonext = noprev = 0;
    for (i = 0; i < lcount; i++)
       {
        if (bsplines[i].section != section)
           {
            continue;
           }
        if (bsplines[i].next == -1)
           {
            nonext++;
            n = i;
           }
        if (bsplines[i].prev == -1)
           {
            noprev++;
            p = i;
           }
       }

// This code tries to join segments that have a missing line
// can't do this because of the possiblity of creating invalid polygons
// from pieces of segments clipped off by other segments
/*
    if ((nonext == 1) && (noprev == 1))
       {
        AddNewLine( &bsplines[n].v[1],  &bsplines[p].v[0], sector );
        lfprintf( "Adding new line %d - %.2f,%.2f to %.2f,%.2f\n", lcount-1,
                bsplines[n].v[1].x, bsplines[n].v[1].y, bsplines[p].v[0].x, bsplines[p].v[0].y);
        bsplines[n].next = lcount-1;
        bsplines[p].prev = lcount-1;
        bsplines[lcount-1].next = p;
        bsplines[lcount-1].prev = n;
       }
*/
#ifdef WDEBUG
    lfprintf( "Section %d - Connections: nonext %d noprev %d\n", section, nonext, noprev);
    for (i = 0; i < lcount; i++)
       {
        if (bsplines[i].section == section)
           {
            lfprintf( "Line %3d next %3d prev %3d\n", i, bsplines[i].next, bsplines[i].prev);
           }
       }
#endif
   }

dboolean CheckSectionLines(int sector, int section)
   {
    int           i, cline = -1, lines = 0;

    // count the lines in this section
    for (i = 0; i < lcount; i++)
       {
        if (bsplines[i].section == section)
           {
            if ((bsplines[i].next != -1) && (bsplines[i].prev != -1))
               {
                lines++;
                if (cline == -1)
                   {
                    cline = i;
                   }
               }
            bsplines[i].used = false;
           }
       }

    slines[section].first = cline;
    if ((lines < 3) || (cline == -1))
       {
        return false;
       }

    i = 0;
    do
       {
        bsplines[cline].used = true;
        cline = bsplines[cline].prev;
        if (cline == -1)
           {
            break;
           }
        i++;
        if (bsplines[cline].used == true)
           {
            break;
           }
       }
    while (i < lines);
    if ((i > 2) && (cline != -1))
       {
        slines[section].lines = i;
        return true;
       }
    else
       {
        return false;
       }
   }

void MakeSectionFloor(int sector, int section)
   {
    int           i, cline = -1, lines = 0;
    float         fh;

    fh = (float)(sectors[sector].floorheight >> FRACBITS);

    flats[sector].Floor[flats[sector].fcount].PCount = slines[section].lines; // Set the point count
    flats[sector].Floor[flats[sector].fcount].Point = (DW_Vertex3Dv *)malloc(sizeof(DW_Vertex3Dv)*slines[section].lines);
    memset(flats[sector].Floor[flats[sector].fcount].Point, 0, sizeof(DW_Vertex3Dv)*slines[section].lines);
    flats[sector].Floor[flats[sector].fcount].Sector = sector;

    i = 0;
    cline = slines[section].first;

    flats[sector].Floor[flats[sector].fcount].Point[i].v[0] = bsplines[cline].v[0].x;
    flats[sector].Floor[flats[sector].fcount].Point[i].v[1] = fh;
    flats[sector].Floor[flats[sector].fcount].Point[i].v[2] = bsplines[cline].v[0].y;
    i++;
    do
       {
        cline = bsplines[cline].prev;
        if (cline == -1)
           {
            break;
           }
        if (i >= slines[section].lines)
           {
            lfprintf("Oops...\n");
           }
        flats[sector].Floor[flats[sector].fcount].Point[i].v[0] = bsplines[cline].v[0].x;
        flats[sector].Floor[flats[sector].fcount].Point[i].v[1] = fh;
        flats[sector].Floor[flats[sector].fcount].Point[i].v[2] = bsplines[cline].v[0].y;
        i++;
       }
    while (i < slines[section].lines);

    if (cline == -1) // abort this
       {
        free(flats[sector].Floor[flats[sector].fcount].Point);
        return;
       }
   }

void MakeSectionCeiling(int sector, int section)
   {
    int           i, cline = -1, lines = 0;
    float         ch;

    ch = (float)(sectors[sector].ceilingheight >> FRACBITS);

    flats[sector].Ceiling[flats[sector].ccount].PCount = slines[section].lines;                 // Set the point count
    flats[sector].Ceiling[flats[sector].ccount].Point = (DW_Vertex3Dv *)malloc(sizeof(DW_Vertex3Dv)*slines[section].lines);
    memset(flats[sector].Ceiling[flats[sector].ccount].Point, 0, sizeof(DW_Vertex3Dv)*slines[section].lines);
    flats[sector].Ceiling[flats[sector].ccount].Sector = sector;

    cline = slines[section].first;
    i = 0;

    flats[sector].Ceiling[flats[sector].ccount].Point[i].v[0] = bsplines[cline].v[0].x;
    flats[sector].Ceiling[flats[sector].ccount].Point[i].v[1] = ch;
    flats[sector].Ceiling[flats[sector].ccount].Point[i].v[2] = bsplines[cline].v[0].y;
    i++;
    do
       {
        cline = bsplines[cline].next;
        if (cline == -1)
           {
            break;
           }
        if (i >= slines[section].lines)
           {
            lfprintf("Oops...\n");
           }
        flats[sector].Ceiling[flats[sector].ccount].Point[i].v[0] = bsplines[cline].v[0].x;
        flats[sector].Ceiling[flats[sector].ccount].Point[i].v[1] = ch;
        flats[sector].Ceiling[flats[sector].ccount].Point[i].v[2] = bsplines[cline].v[0].y;
        i++;
       }
    while (i < slines[section].lines);

    if (cline == -1) // abort this
       {
        free(flats[sector].Ceiling[flats[sector].ccount].Point);
        return;
       }
   }

void AlignVertices()
   {
    int i, itvx1, itvy1, itvx2, itvy2;

    for (i = 0; i < lcount; i++)
       {
        lfprintf("A Vertices : %f,%f to %f,%f\n", bsplines[i].v[0].x, bsplines[i].v[0].y,
                                                  bsplines[i].v[1].x, bsplines[i].v[1].y);
        itvx1 = bsplines[i].v[0].x;
        if (((float)itvx1 >= (bsplines[i].v[0].x - 0.5)) &&
            ((float)itvx1 <= (bsplines[i].v[0].x + 0.5)))
           {
            bsplines[i].v[0].x = itvx1;
           }
        else
           {
            itvx1++;
            if (((float)itvx1 >= (bsplines[i].v[0].x - 0.5)) &&
                ((float)itvx1 <= (bsplines[i].v[0].x + 0.5)))
               {
                bsplines[i].v[0].x = itvx1;
               }
           }
        itvy1 = bsplines[i].v[0].y;
        if (((float)itvy1 >= (bsplines[i].v[0].y - 0.5)) &&
            ((float)itvy1 <= (bsplines[i].v[0].y + 0.5)))
           {
            bsplines[i].v[0].y = itvy1;
           }
        else
           {
            itvy1++;
            if (((float)itvy1 >= (bsplines[i].v[0].y - 0.5)) &&
                ((float)itvy1 <= (bsplines[i].v[0].y + 0.5)))
               {
                bsplines[i].v[0].y = itvy1;
               }
           }
        itvx2 = bsplines[i].v[1].x;
        if (((float)itvx2 >= (bsplines[i].v[1].x - 0.5)) &&
            ((float)itvx2 <= (bsplines[i].v[1].x + 0.5)))
           {
            bsplines[i].v[1].x = itvx2;
           }
        else
           {
            itvx2++;
            if (((float)itvx2 >= (bsplines[i].v[1].x - 0.5)) &&
                ((float)itvx2 <= (bsplines[i].v[1].x + 0.5)))
               {
                bsplines[i].v[1].x = itvx2;
               }
           }
        itvy2 = bsplines[i].v[1].y;
        if (((float)itvy2 >= (bsplines[i].v[1].y - 0.5)) &&
            ((float)itvy2 <= (bsplines[i].v[1].y + 0.5)))
           {
            bsplines[i].v[1].y = itvy2;
           }
        else
           {
            itvy2++;
            if (((float)itvy2 >= (bsplines[i].v[1].y - 0.5)) &&
                ((float)itvy2 <= (bsplines[i].v[1].y + 0.5)))
               {
                bsplines[i].v[1].y = itvy2;
               }
           }
        lfprintf("B Vertices : %f,%f to %f,%f - %d,%d to %d,%d\n", bsplines[i].v[0].x, bsplines[i].v[0].y,
                                                  bsplines[i].v[1].x, bsplines[i].v[1].y, itvx1, itvy1, itvx2, itvy2);
       }
   }

void CreateNewFlats()
   {
    static int          sector, line, side1, side2, section, side, l1, l2, lc, fc, cc;
    static int          seg;
    static bspvert_t    v[2];
    static linelist_t  *linelist;
    int                 sections, v1x, v2x, v1y, v2y, v1, v2;

    // Free up what we allocated for the previous level...
    if (flats != 0)
       {
        for (sector = 0; sector < lastsectors; sector++)
           {
            for (fc = 0; fc < flats[sector].fcount; fc++)
               {
                if (flats[sector].Floor != 0)
                   {
                    if (flats[sector].Floor[fc].Point != 0)
                       {
                        free(flats[sector].Floor[fc].Point);
                       }
                   }
               }
            for (cc = 0; cc < flats[sector].ccount; cc++)
               {
                if (flats[sector].Ceiling != 0)
                   {
                    if (flats[sector].Ceiling[cc].Point != 0)
                       {
                        free(flats[sector].Ceiling[cc].Point);
                       }
                   }
               }
            if (flats[sector].Floor != 0)
               {
                free(flats[sector].Floor);
                flats[sector].Floor = 0;
               }
            if (flats[sector].Ceiling != 0)
               {
                free(flats[sector].Ceiling);
                flats[sector].Ceiling = 0;
               }
           }
         free(flats);
         flats = 0;
        }

    flats = (flats_t *)malloc(sizeof(flats_t)*numsectors);

#ifdef WDEBUG
    lfprintf( "Number of sectors = %d\n", numsectors);
#endif
    //for (sector = 125; sector < 126; sector++) // For each sector...
    for (sector = 0; sector < numsectors; sector++) // For each sector...
       {
        scount = 1;
        sections = 0;
        if ((sectors[sector].floorpic == skyflatnum) && (sectors[sector].ceilingpic == skyflatnum))
           continue;

#ifdef WDEBUG
        lfprintf( "Processing sector %d flats\n", sector);
#endif

        SectorBBox[sector].left = SectorBBox[sector].bottom =  32767;
        SectorBBox[sector].top = SectorBBox[sector].right = -32767;

        lcount = 0;
        bvcount = 0;
        // Build a line list for this sector...
        for (line = 0; line < numlines; line++)
           {
            side1 = lines[line].sidenum[0];
            side2 = lines[line].sidenum[1];
            // ignore trip lines...
            if (sides[side1].sectornumb == sides[side2].sectornumb)
                continue;

            // If the sector of sidedef 1 or  sidedef 2 of this line equals this sector, we'll use it...
            if ((sides[side1].sectornumb == sector) ||
                (sides[side2].sectornumb == sector))
               {
                v1x = (lines[line].v1->x >> FRACBITS);
                v1y = (lines[line].v1->y >> FRACBITS)*-1;
                v2x = (lines[line].v2->x >> FRACBITS);
                v2y = (lines[line].v2->y >> FRACBITS)*-1;
                if (SectorBBox[sector].left > v1x)
                    SectorBBox[sector].left = v1x;
                if (SectorBBox[sector].bottom > v1y)
                    SectorBBox[sector].bottom = v1y;
                if (SectorBBox[sector].left > v2x)
                    SectorBBox[sector].left = v2x;
                if (SectorBBox[sector].bottom > v2y)
                    SectorBBox[sector].bottom = v2y;
                if (SectorBBox[sector].right < v1x)
                    SectorBBox[sector].right = v1x;
                if (SectorBBox[sector].top < v1y)
                    SectorBBox[sector].top = v1y;
                if (SectorBBox[sector].right < v2x)
                    SectorBBox[sector].right = v2x;
                if (SectorBBox[sector].top < v2y)
                    SectorBBox[sector].top = v2y;

                // We render "anti-clockwise"...
                if (sides[side1].sectornumb == sector)
                   {
                    v[0].x = (float)v1x;
                    v[0].y = (float)v1y;
                    v[1].x = (float)v2x;
                    v[1].y = (float)v2y;
                   }
                else
                   {
                    v[0].x = (float)v2x;
                    v[0].y = (float)v2y;
                    v[1].x = (float)v1x;
                    v[1].y = (float)v1y;
                   }
                if ((v1 = FindVertex(v[0].x, v[0].y)) == -1)
                   {
                    v1 = AddVertex(v[0].x, v[0].y);
                   }
                if ((v2 = FindVertex(v[1].x, v[1].y)) == -1)
                   {
                    v2 = AddVertex(v[1].x, v[1].y);
                   }
                AddNewLine( &v[0], &v[1], v1, v2, sector );
                bsplines[lcount-1].line = line;
               }
           }
        // BSP the sector and create the sections...
        //scount = 1;
//        lfprintf("Sector line list:\n");

//        lfprintf("Sector %d BBox %d,%d to %d,%d\n", sector,
//                  SectorBBox[sector].left, SectorBBox[sector].top,
//                  SectorBBox[sector].right, SectorBBox[sector].bottom);
//        for (l1 = 0; l1 < lcount; l1++)
//           {
//            lfprintf( "Line %d %.2f,%.2f to %.2f,%.2f\n", l1,
//                    bsplines[l1].v[0].x,bsplines[l1].v[0].y, bsplines[l1].v[1].x,bsplines[l1].v[1].y);
//           }
#ifdef WDEBUG
        lfprintf("\nPartitioning sector\n\n");
#endif
        l2 = 0;
        do {
#ifdef WDEBUG
            lfprintf("\nCull Colinear Segments\n\n");
#endif
            l1 = CullColinearSegs(sector, 0);
            l2 += l1;
           }
        while ( l1 > 0 );
#ifdef WDEBUG
        lfprintf( "Dropped %d line segments.\n", l2);
#endif
        for (section = 0; section < scount; section++)
           {
#ifdef WDEBUG
            lfprintf( "Processing section %d.\n", section);
#endif
            PartitionSection(sector, section);
            // remove any duplicate lines...
            for (l1 = 0; l1 < lcount; l1++)
               {
                if (bsplines[l1].section != section)
                   {
                    continue;
                   }
                for (l2 = 0; l2 < lcount; l2++)
                   {
                    if (l1 == l2)
                       {
                        continue;
                       }
                    if (bsplines[l1].section != bsplines[l2].section)
                       {
                        continue;
                       }
                    if (FLOAT_EQ(bsplines[l1].v[0].x, bsplines[l2].v[0].x) &&
                        FLOAT_EQ(bsplines[l1].v[0].y, bsplines[l2].v[0].y) &&
                        FLOAT_EQ(bsplines[l1].v[1].x, bsplines[l2].v[1].x) &&
                        FLOAT_EQ(bsplines[l1].v[1].y, bsplines[l2].v[1].y) )
                       {
#ifdef WDEBUG
                        lfprintf("Duplicate line dropped...\n");
#endif
                        bsplines[l2].section = -1;
                       }
                   }
               }
           }
#ifdef WDEBUG
        lfprintf("\nSector line segment info\n\n");
        lfprintf( "Sector %4d: %3d linedefs %3d sections\n", sector, lcount, scount);
#endif
#ifdef WDEBUG
        for (section = 0; section < scount; section++)
           {
            lfprintf( "Section %d lines:\n", section);
            for (line = 0; line < lcount; line++)
               {
                if (bsplines[line].section == section)
                   {
                    lfprintf( "Line %d - %f,%f to %f,%f\n",line,
                            bsplines[line].v[0].x, bsplines[line].v[0].y,
                            bsplines[line].v[1].x, bsplines[line].v[1].y );
                   }
               }
           }
#endif
        //AlignVertices();

        flats[sector].Floor = 0;
        flats[sector].Ceiling = 0;
        flats[sector].fcount = 0;
        flats[sector].ccount = 0;
        flats[sector].Floor = (DW_FloorCeil *)malloc(sizeof(DW_FloorCeil)*scount);
        memset(flats[sector].Floor, 0, sizeof(DW_FloorCeil)*scount);
        flats[sector].Ceiling = (DW_FloorCeil *)malloc(sizeof(DW_FloorCeil)*scount);
        memset(flats[sector].Ceiling, 0, sizeof(DW_FloorCeil)*scount);
        sections = 0;
        for (section = 0; section < scount; section++)
           {
//            lfprintf("\nJoin Colinear Segments\n\n");
            JoinColinearSegs(sector, section);
            slines[section].lines = 0;
//            lfprintf("\nFind forward chains for section\n\n");
            ChainSectionForward(sector, section);
            if (CheckSectionLines(sector, section) == true)
               {
                sections++;
                if ((sectors[sector].floorpic != skyflatnum) && (slines[section].lines > 2))
                   {
//                    lfprintf("\nMake section floor\n\n");
                    MakeSectionFloor(sector, section);
                    flats[sector].fcount++;
                   }
                if ((sectors[sector].ceilingpic != skyflatnum) && (slines[section].lines > 2))
                   {
//                    lfprintf("\nMake section ceiling\n\n");
                    MakeSectionCeiling(sector, section);
                    flats[sector].ccount++;
                   }
               }
           }
        //lfprintf("Sector %d number of bverts: %d\n", sector, bvcount);
       }
    //free(linelist);
    //free(flats);
    if (DrawFlat != 0)
        free(DrawFlat);
    DrawFlat = (dboolean *)malloc(sizeof(dboolean)*numsectors);
    for (sector = 0; sector < numsectors; sector++)
       {
        DrawFlat[sector] = false;
       }
    lastsectors = numsectors;
   }


void Build3DLevel()
   {
    LoadNewTextures();

    CreateNewWalls();
   
    SectorBBox = (RECT *)alloca(sizeof(RECT)*numsectors);

    CreateNewFlats();

    CalcTexCoords();

    SectorBBox = 0;

    // find place to drop these:
    //
    // flats
    // floorlist
    // ceillist
   }



void CalcTexCoords()
   {
    DW_Polygon   *TempPoly;
    float         fLength, fHigh, fVertOff, fHorzOff, GLXOff, GLYOff, UnPegOff, xGrid, yGrid;
    float         GLHigh, DHigh, YRatio, YPos, YOffset;
    int           p, sector, section;
    float         SectorX, SectorY;

    TempPoly = PolyList;
    while (TempPoly != 0)
       {
        GLHigh = (float)TexList[TempPoly->Texture[0]].GLHigh;
        DHigh  = (float)TexList[TempPoly->Texture[0]].DHigh;
        YRatio = DHigh / GLHigh;

        // tu = horizontal texture coordinate
        // tv = vertical texture coordinate
        fLength = (float)sqrt(((TempPoly->Point[0].v[0] - TempPoly->Point[3].v[0])*(TempPoly->Point[0].v[0] - TempPoly->Point[3].v[0]))+
                              ((TempPoly->Point[0].v[2] - TempPoly->Point[3].v[2])*(TempPoly->Point[0].v[2] - TempPoly->Point[3].v[2])));
        fHigh = (TempPoly->Point[0].v[1] - TempPoly->Point[1].v[1]);
        GLXOff = (float)(TexList[TempPoly->Texture[0]].GLWide - TexList[TempPoly->Texture[0]].DWide) / fLength;
        GLYOff = (float)(GLHigh - DHigh) / GLHigh;
        YOffset = (float)(sides[TempPoly->SideDef].rowoffset >> FRACBITS);
        if (((sides[TempPoly->SideDef].rowoffset >> FRACBITS) == 0)||(DHigh == 0))
           fVertOff = 0.0f;
        else
           {
            fVertOff = ((float)(sides[TempPoly->SideDef].rowoffset >> FRACBITS) / DHigh)*-1.0f;
           }
        while (fVertOff < 0.0f)
           fVertOff += 1.0f;
        if (((sides[TempPoly->SideDef].textureoffset >> FRACBITS) == 0)||(TexList[TempPoly->Texture[0]].DWide == 0))
           fHorzOff = 0.0f;
        else
           {
            fHorzOff = ((float)(sides[TempPoly->SideDef].textureoffset >> FRACBITS) / (float)TexList[TempPoly->Texture[0]].DWide);
           }
        while (fHorzOff < 0.0f)
           fHorzOff += 1.0f;

        TempPoly->Point[0].tu = 0.0f + fHorzOff;
        switch(TempPoly->Position)
           { // line_t
            case DW_UPPER:
                 if ((lines[TempPoly->LineDef].flags & DW_UPUNPEG) == DW_UPUNPEG)
                     TempPoly->Point[0].tv = 1.0f + fVertOff;
                 else
                     TempPoly->Point[0].tv = (((fHigh / DHigh) + fVertOff) * YRatio) + GLYOff;
                 break;

            case DW_MIDDLE:
                 if ((lines[TempPoly->LineDef].flags & DW_LWUNPEG) == DW_LWUNPEG)
                     TempPoly->Point[0].tv = (fHigh/DHigh) + fVertOff;
                 else
                    {
                     YPos = YOffset;
                     TempPoly->Point[0].tv = 1.0f - (YPos / GLHigh);
                    }
                 break;

            case DW_LOWER:
                 if ((lines[TempPoly->LineDef].flags & DW_LWUNPEG) == DW_LWUNPEG)
                    {
                     UnPegOff = (float)((sectors[sides[TempPoly->SideDef].sectornumb].ceilingheight >> FRACBITS)- TempPoly->Point[0].v[1])/GLHigh;
                     while (UnPegOff > 1.0f)
                        UnPegOff -= 1.0f;
                     UnPegOff = 1.0f - UnPegOff;
                     TempPoly->Point[0].tv = UnPegOff + fVertOff;
                    }
                 else
                    TempPoly->Point[0].tv = 1.0f + fVertOff;
                 break;
           }

        TempPoly->Point[1].tu = TempPoly->Point[0].tu;
        switch(TempPoly->Position)
           {
            case DW_UPPER:
                 if ((lines[TempPoly->LineDef].flags & DW_UPUNPEG) == DW_UPUNPEG)
                     TempPoly->Point[1].tv = (1.0f - (fHigh / GLHigh)) + fVertOff;
                 else
                     TempPoly->Point[1].tv = 0.0f + fVertOff + GLYOff;
                 break;

            case DW_MIDDLE:
                 if ((lines[TempPoly->LineDef].flags & DW_LWUNPEG) == DW_LWUNPEG)
                     TempPoly->Point[1].tv = ((GLHigh-DHigh)/GLHigh) + fVertOff;
                 else
                    {
                     YPos = fHigh + YOffset;
                     TempPoly->Point[1].tv = (1.0f - (YPos / GLHigh));
                    }
                 break;

            case DW_LOWER:
                 if ((lines[TempPoly->LineDef].flags & DW_LWUNPEG) == DW_LWUNPEG)
                    {
                     UnPegOff = TempPoly->Point[0].tv - ((TempPoly->Point[0].v[1] - TempPoly->Point[1].v[1])/GLHigh);
                     TempPoly->Point[1].tv = UnPegOff;
                    }
                 else
                    TempPoly->Point[1].tv = (1.0f - (fHigh / GLHigh)) + fVertOff;
           }

        TempPoly->Point[2].tu = (fLength / (float)TexList[TempPoly->Texture[0]].DWide) + fHorzOff;
        TempPoly->Point[2].tv = TempPoly->Point[1].tv;

        TempPoly->Point[3].tu = TempPoly->Point[2].tu;
        TempPoly->Point[3].tv = TempPoly->Point[0].tv;
        TempPoly = TempPoly->Next;
       }

    for (sector = 0; sector < numsectors; sector++)
       {
        xGrid = 64.0f;
        yGrid = 64.0f;
        SectorX = ((float)(SectorBBox[sector].left % 64) / 64.0f);
        if (SectorX < 0.0f)
           SectorX += 1.0f;
        SectorY = ((float)(SectorBBox[sector].bottom % 64) / 64.0f);
        if (SectorY < 0.0f)
           SectorY += 1.0f;
        for (section = 0; section < flats[sector].fcount; section++)
           {
            for (p = 0; p < flats[sector].Floor[section].PCount; p++)
               {
                flats[sector].Floor[section].Point[p].tu = ((float)(flats[sector].Floor[section].Point[p].v[0] - SectorBBox[sector].left)/xGrid)+SectorX;
                flats[sector].Floor[section].Point[p].tv = ((float)(flats[sector].Floor[section].Point[p].v[2] - SectorBBox[sector].bottom)/yGrid)+SectorY;
               }
           }
        for (section = 0; section < flats[sector].ccount; section++)
           {
            for (p = 0; p < flats[sector].Ceiling[section].PCount; p++)
               {
                flats[sector].Ceiling[section].Point[p].tu = ((float)(flats[sector].Ceiling[section].Point[p].v[0] - SectorBBox[sector].left)/xGrid)+SectorX;
                flats[sector].Ceiling[section].Point[p].tv = ((float)(flats[sector].Ceiling[section].Point[p].v[2] - SectorBBox[sector].bottom)/yGrid)+SectorY;
               }
           }
       }
   }


/*
   Figuring out the sprites:

   Trying to figure out which sprites to load for each level is rather slow and it's also quite easy to
   miss sprites if certain states have not been accounted for.  It might just be better to load every one
   of the sprites by it's sprite number and be done with it.  This will result in upwards of three megabytes
   of memory used for the sprites at load time and will cause the initial program load to be quite long.
   It will have the benefit, though of making sure that ALL the sprites get loaded and are available.

   Another possibility is to load the sprites on an as-needed basis.  Which might make for some slow spots
   when monsters are first encountered or a new weapon is picked up and fired. Also, explosions might make
   for stutters as well.

   Try loading all the sprites as textures first then just using them.  For the alpha, treat memory as if
   it is unlimited.

   Optimization can be done once the brute force methods work.
*/


extern int        NumSpriteLumps;
extern int       *SpriteLumps;
extern GLTexData *SprData;

extern spritedef_t*	sprites;
int GL_MakeSpriteTexture(patch_t *Sprite, GLTexData *Tex, dboolean smooth);

void LoadAllSprites()
   {
    int    i, lump, dotchk, dotmod;
    char   tstr[64];

    dotmod = numspritelumps/32;
    dotchk = numspritelumps/dotmod;
    
    con_printf("[%*s]\b", dotchk, " ");
    for (i = 0; i < dotchk; i++)
       {
        tstr[i] = '\b';
       }
    tstr[i] = '\0';
    con_printf(tstr);
    for (lump = 0, dotchk = 0; lump < numspritelumps; lump++)
       {
        dotchk++;
        if ((dotchk % dotmod) == 0)
           {
            con_printf(".");
           }
        SprData[lump].TexName = GL_MakeSpriteTexture(W_CacheLumpNum(firstspritelump+lump,PU_CACHE), &SprData[lump], true);
       }
    con_printf("\n");
   }

void BuildThingList()
   {
    int i, frame, angle, lump;
    int bstate, state, pstate;

    // additional player sprites (weapons etc.)
    SpritePresent[MT_PUFF] = 1;
    SpritePresent[MT_ROCKET] = 1;
    if (gamemode != shareware)
       {
        SpritePresent[MT_PLASMA] = 1;
        SpritePresent[MT_BFG] = 1;
        SpritePresent[MT_TFOG] = 1;
       }

    // additional "monster" sprites (weapons and attacks)
    if (gamemode == commercial)
       {
        SpritePresent[MT_FIRE] = 1;
        SpritePresent[MT_BRUISERSHOT] = 1;
        SpritePresent[MT_SPAWNSHOT] = 1;
        SpritePresent[MT_SPAWNFIRE] = 1;
       }
    SpritePresent[MT_IFOG] = 1;
    SpritePresent[MT_TROOPSHOT] = 1;
    SpritePresent[MT_HEADSHOT] = 1;
    SpritePresent[MT_TELEPORTMAN] = 1;

    for (i = 0; i < NUMMOBJTYPES; i++)
       {
        if (SpritePresent[i] > 0)
           {
            state = bstate = mobjinfo[i].spawnstate;
            do
               {
                pstate = state;
                frame = states[state].frame & 0x7FFF;
                for (angle = 0; angle < 8; angle++)
                   {
//                    lfprintf( "Object type %d uses sprite %s frame %d lump %d - %d present.\n", i, sprnames[states[state].sprite],
//                       frame, sprites[states[state].sprite].spriteframes[frame].lump[angle], SpritePresent[i]);
                    SpriteLumps[sprites[states[state].sprite].spriteframes[frame].lump[angle]]++;
                    if (sprites[states[state].sprite].spriteframes[frame].rotate == false)
                        break;
                   }
                state = states[state].nextstate;
               }
            while ((state != S_NULL) && (state != bstate) && (state > pstate));
            if (mobjinfo[i].seestate != S_NULL)
               {
                state = bstate = mobjinfo[i].seestate;
                do
                   {
                    pstate = state;
                    frame = states[state].frame & 0x7FFF;
                    for (angle = 0; angle < 8; angle++)
                       {
//                        lfprintf( "Object type %d uses sprite %s frame %d lump %d - %d present.\n", i, sprnames[states[state].sprite],
//                          frame, sprites[states[state].sprite].spriteframes[frame].lump[angle], SpritePresent[i]);
                        SpriteLumps[sprites[states[state].sprite].spriteframes[frame].lump[angle]]++;
                        if (sprites[states[state].sprite].spriteframes[frame].rotate == false)
                            break;
                       }
                    state = states[state].nextstate;
                   }
                while ((state != S_NULL) && (state != bstate) && (state > pstate));
               }
            if (mobjinfo[i].painstate != S_NULL)
               {
                state = bstate = mobjinfo[i].painstate;
                do
                   {
                    pstate = state;
                    frame = states[state].frame & 0x7FFF;
                    for (angle = 0; angle < 8; angle++)
                       {
//                        lfprintf( "Object type %d uses sprite %s frame %d lump %d - %d present.\n", i, sprnames[states[state].sprite],
//                          frame, sprites[states[state].sprite].spriteframes[frame].lump[angle], SpritePresent[i]);
                        SpriteLumps[sprites[states[state].sprite].spriteframes[frame].lump[angle]]++;
                        if (sprites[states[state].sprite].spriteframes[frame].rotate == false)
                            break;
                       }
                    state = states[state].nextstate;
                   }
                while ((state != S_NULL) && (state != bstate) && (state > pstate));
               }
            if (mobjinfo[i].meleestate != S_NULL)
               {
                state = bstate = mobjinfo[i].meleestate;
                do
                   {
                    pstate = state;
                    frame = states[state].frame & 0x7FFF;
                    for (angle = 0; angle < 8; angle++)
                       {
//                        lfprintf( "Object type %d uses sprite %s frame %d lump %d - %d present.\n", i, sprnames[states[state].sprite],
//                          frame, sprites[states[state].sprite].spriteframes[frame].lump[angle], SpritePresent[i]);
                        SpriteLumps[sprites[states[state].sprite].spriteframes[frame].lump[angle]]++;
                        if (sprites[states[state].sprite].spriteframes[frame].rotate == false)
                            break;
                       }
                    state = states[state].nextstate;
                   }
                while ((state != S_NULL) && (state != bstate) && (state > pstate));
               }
            if (mobjinfo[i].missilestate != S_NULL)
               {
                state = bstate = mobjinfo[i].missilestate;
                do
                   {
                    pstate = state;
                    frame = states[state].frame & 0x7FFF;
                    for (angle = 0; angle < 8; angle++)
                       {
//                        lfprintf( "Object type %d uses sprite %s frame %d lump %d - %d present.\n", i, sprnames[states[state].sprite],
//                          frame, sprites[states[state].sprite].spriteframes[frame].lump[angle], SpritePresent[i]);
                        SpriteLumps[sprites[states[state].sprite].spriteframes[frame].lump[angle]]++;
                        if (sprites[states[state].sprite].spriteframes[frame].rotate == false)
                            break;
                       }
                    state = states[state].nextstate;
                   }
                while ((state != S_NULL) && (state != bstate) && (state > pstate));
               }
            if (mobjinfo[i].deathstate != S_NULL)
               {
                state = bstate = mobjinfo[i].deathstate;
                do
                   {
                    pstate = state;
                    frame = states[state].frame & 0x7FFF;
                    for (angle = 0; angle < 8; angle++)
                       {
//                        lfprintf( "Object type %d uses sprite %s frame %d lump %d - %d present.\n", i, sprnames[states[state].sprite],
//                          frame, sprites[states[state].sprite].spriteframes[frame].lump[angle], SpritePresent[i]);
                        SpriteLumps[sprites[states[state].sprite].spriteframes[frame].lump[angle]]++;
                        if (sprites[states[state].sprite].spriteframes[frame].rotate == false)
                            break;
                       }
                    state = states[state].nextstate;
                   }
                while ((state != S_NULL) && (state != bstate) && (state > pstate));
               }
            if (mobjinfo[i].xdeathstate != S_NULL)
               {
                state = bstate = mobjinfo[i].xdeathstate;
                do
                   {
                    pstate = state;
                    frame = states[state].frame & 0x7FFF;
                    for (angle = 0; angle < 8; angle++)
                       {
//                        lfprintf( "Object type %d uses sprite %s frame %d lump %d - %d present.\n", i, sprnames[states[state].sprite],
//                          frame, sprites[states[state].sprite].spriteframes[frame].lump[angle], SpritePresent[i]);
                        SpriteLumps[sprites[states[state].sprite].spriteframes[frame].lump[angle]]++;
                        if (sprites[states[state].sprite].spriteframes[frame].rotate == false)
                            break;
                       }
                    state = states[state].nextstate;
                   }
                while ((state != S_NULL) && (state != bstate) && (state > pstate));
               }
           }
       }
    for (lump = 0; lump < NumSpriteLumps; lump++)
       {
        if ((SpriteLumps[lump] > 0) && (SprData[lump].Permanent == false))
           {
//            if (modifiedgame)
//                patched = W_GetNumForName(lumpinfo[lump].name);
//            else
//                patched = l;
            //lfprintf( "GL Sprite Lump %d\n", firstspritelump+lump);
            SprData[lump].TexName = GL_MakeSpriteTexture(W_CacheLumpNum(firstspritelump+lump,PU_CACHE), &SprData[lump], true);
//            lfprintf( "World Sprite lump %d used - texture %d - height %f, top %f\n", lump, SprData[lump].TexName, SprData[lump].Height, SprData[lump].TopOff);
           }
       }
    //wglMakeCurrent(NULL, NULL);
   }

void WS_Init(void) // Setup Weapon Sprites...
   {
    int i, frame, lump;
    int bstate, state, wmax;

    if (gamemode != shareware)
       {
        if (gamemode == commercial)
            wmax = NUMWEAPONS;
        else
            wmax = NUMWEAPONS - 1;
       }
    else
       {
        wmax = 5;
       }


    for (i = 0; i < wmax; i++)
       {
//        lfprintf( "Weapon type %d\n", i);
//        lfprintf("Up state\n");
        if (weaponinfo[i].upstate != S_NULL)
           {
            state = bstate = weaponinfo[i].upstate;
            do
               {
                frame = states[state].frame&0x7FFF;
//                lfprintf( "Weapon type %d upstate sprite %s frame %d lump %d.\n", i, sprnames[states[state].sprite],
//                   frame, sprites[states[state].sprite].spriteframes[frame].lump[0]);
                SpriteLumps[sprites[states[state].sprite].spriteframes[frame].lump[0]]++;
                state = states[state].nextstate;
               }
            while ((state != S_NULL) && (state != bstate) && (state != S_LIGHTDONE) && (state != weaponinfo[i].readystate));
           }
        
//        lfprintf("Down state\n");
        if (weaponinfo[i].downstate != S_NULL)
           {
            state = bstate = weaponinfo[i].downstate;
            do
               {
                frame = states[state].frame&0x7FFF;
//                lfprintf( "Weapon type %d downstate sprite %s frame %d lump %d.\n", i, sprnames[states[state].sprite],
//                   frame, sprites[states[state].sprite].spriteframes[frame].lump[0]);
                SpriteLumps[sprites[states[state].sprite].spriteframes[frame].lump[0]]++;
                state = states[state].nextstate;
               }
            while ((state != S_NULL) && (state != bstate) && (state != S_LIGHTDONE) && (state != weaponinfo[i].readystate));
           }
        
//        lfprintf("Ready state\n");
        if (weaponinfo[i].readystate != S_NULL)
           {
            state = bstate = weaponinfo[i].readystate;
            do
               {
                frame = states[state].frame&0x7FFF;
//                lfprintf( "Weapon type %d readystate sprite %s frame %d lump %d.\n", i, sprnames[states[state].sprite],
//                   frame, sprites[states[state].sprite].spriteframes[frame].lump[0]);
                SpriteLumps[sprites[states[state].sprite].spriteframes[frame].lump[0]]++;
                state = states[state].nextstate;
               }
            while ((state != S_NULL) && (state != bstate) && (state != S_LIGHTDONE));
           }
        
//        lfprintf("Attack state\n");
        if (weaponinfo[i].atkstate != S_NULL)
           {
            state = bstate = weaponinfo[i].atkstate;
            do
               {
                frame = states[state].frame&0x7FFF;
//                lfprintf( "Weapon type %d atkstate sprite %s frame %d lump %d.\n", i, sprnames[states[state].sprite],
//                   frame, sprites[states[state].sprite].spriteframes[frame].lump[0]);
                SpriteLumps[sprites[states[state].sprite].spriteframes[frame].lump[0]]++;
                state = states[state].nextstate;
               }
            while ((state != S_NULL) && (state != bstate) && (state != S_LIGHTDONE) && (state != weaponinfo[i].readystate));
           }
        
//        lfprintf("Flash state\n");
        if (weaponinfo[i].flashstate != S_NULL)
           {
            state = bstate = weaponinfo[i].flashstate;
            do
               {
                frame = states[state].frame&0x7FFF;
//                lfprintf( "Weapon type %d flashstate sprite %s frame %d lump %d.\n", i, sprnames[states[state].sprite],
//                   frame, sprites[states[state].sprite].spriteframes[frame].lump[0]);
                SpriteLumps[sprites[states[state].sprite].spriteframes[frame].lump[0]]++;
                SprData[sprites[states[state].sprite].spriteframes[frame].lump[0]].Translucent = 153;
                state = states[state].nextstate;
               }
            while ((state != S_NULL) && (state != bstate) && (state != S_LIGHTDONE) && (state != weaponinfo[i].readystate));
           }
       }
    for (lump = 0; lump < NumSpriteLumps; lump++)
       {
        SprData[lump].TexName = 0;
        if (SpriteLumps[lump] > 0)
           {
            if (SprData[lump].Translucent != 153)
                SprData[lump].Translucent = 64;
            SprData[lump].TexName = GL_MakeSpriteTexture(W_CacheLumpNum(firstspritelump+lump,PU_CACHE), &SprData[lump], true);
            //lfprintf( "Weapon Sprite lump %d used - texture %d\n", lump, SprData[lump].TexName);
            SprData[lump].Permanent = true;
           }
       }
   }

