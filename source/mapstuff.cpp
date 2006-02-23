#include "uox3.h"
#include "regions.h"
#include "fileio.h"
#include "cServerDefinitions.h"
#include "ssection.h"
#include "scriptc.h"

#undef DBGFILE
#define DBGFILE "mapstuff.cpp"

namespace UOX
{

cMapStuff *Map					= NULL;

const SI32 VERFILE_MAP			= 0x00;
const SI32 VERFILE_STAIDX		= 0x01;
const SI32 VERFILE_STATICS		= 0x02;
const SI32 VERFILE_ARTIDX		= 0x03;
const SI32 VERFILE_ART			= 0x04;
const SI32 VERFILE_ANIMIDX		= 0x05;
const SI32 VERFILE_ANIM			= 0x06;
const SI32 VERFILE_SOUNDIDX		= 0x07;
const SI32 VERFILE_SOUND		= 0x08;
const SI32 VERFILE_TEXIDX		= 0x09;
const SI32 VERFILE_TEXMAPS		= 0x0A;
const SI32 VERFILE_GUMPIDX		= 0x0B;
const SI32 VERFILE_GUMPART		= 0x0C;
const SI32 VERFILE_MULTIIDX		= 0x0D;
const SI32 VERFILE_MULTI		= 0x0E;
const SI32 VERFILE_SKILLSIDX	= 0x0F;
const SI32 VERFILE_SKILLS		= 0x10;
const SI32 VERFILE_TILEDATA		= 0x1E;
const SI32 VERFILE_ANIMDATA		= 0x1F;
const UI16 TILEDATA_SIZE		= 512 * 32;


//! these are the fixed record lengths as determined by the .mul files from OSI
//! i made them longs because they are used to calculate offsets into the files
const UI32 VersionRecordSize	= 20L;
const UI32 MultiRecordSize		= 12L;
const UI32 LandRecordSize		= 26L;
const UI32 TileRecordSize		= 37L;
const UI32 MapRecordSize		= 3L;
const UI32 MultiIndexRecordSize = 12L;
const UI32 StaticRecordSize		= 7L;

/*!
** Hey, its me fur again. I'm going to tear through this file next
** so I can get a better understanding of the map/tile stuff and
** especially the z values so we can nail down the walking bugs once
** and for all.  Gosh, I feel like a high-tech version of Steve the
** Crocodile Hunter these days..  C'mon mate, lets go squish some bugs!
*/

/*! New Notes: 11/3/1999
** I changed all of the places where z was handled as a signed char and
** upped them to an int. While the basic terrain never goes above 127, 
** calculations sure can. For example, if you are at a piece of land at 100
** and put a 50 high static object, its going to roll over. - fur
*/

/*!
** Tile Flags Bit Definitions - Please fill in if you know what something does!!
** I updated some of these with what LB knew 11/17/1999
**
** (tile.flag1&0x01)		1 = At Floor Level(?)
** (tile.flag1&0x02)		1 = Wearable/Holdable(?)
** (tile.flag1&0x04)		1 = Signs, Guilds, banners, (?)
** (tile.flag1&0x08)		1 = Web, dirt, blood, footsteps(?)
** (tile.flag1&0x10)		1 = Wall/Vertical Tile(?)
** (tile.flag1&0x20)		1 = Causes Damage(?)
** (tile.flag1&0x40)		1 = Blocking, cannot travel over
** (tile.flag1&0x80)		1 = IsWet, Maybe Water or Blood
**
** (tile.flag2&0x01)		Nothing Uses This Bit
** (tile.flag2&0x02)		Standable, Mostly#ifdef UOX_PLATFORM != PLATFORM_WIN32
#  include <sys/types.h> // open, stat, mmap, munmap
#  include <sys/stat.h>  // stat
#  include <fcntl.h>     // open
#  include <unistd.h>    // stat, mmap, munmap
#  include <sys/mman.h>  // mmap, mmunmap
#endif
 Flat Surfaces(?)
** (tile.flag2&0x04)		Climbable, eg. Stairs/Ladders(?)
** (tile.flag2&0x08)		Pileable
** (tile.flag2&0x10)		Windows/Doors(?)
** (tile.flag2&0x20)		Cannot shoot thru
** (tile.flag2&0x40)		Display as 'a xxxx'
** (tile.flag2&0x80)		Display as 'an xxxx;
**
** (tile.flag3&0x01)		Internal Only Tile(?)
** (tile.flag3&0x02)		Fades with CoT(?)
** (tile.flag3&0x04)		(?)
** (tile.flag3&0x08)		Never used(?)
** (tile.flag3&0x10)		Map(?)
** (tile.flag3&0x20)		Container(?)
** (tile.flag3&0x40)		Wearable(?)
** (tile.flag3&0x80)		Lightsource(?)
**
**
** (tile.flag4&0x01)		Animated Tile(?)
** (tile.flag4&0x02)		(?)
** (tile.flag4&0x04)		Walk(?)
** (tile.flag4&0x08)		Whole bodyitem/Items you cannot remove from paper doll(?)
** (tile.flag4&0x10)		Wall/Roof? Weapon(?)
** (tile.flag4&0x20)		Door(?)
** (tile.flag4&0x40)		Climbable direction (2-bits)
** (tile.flag4&0x80)		Climbable direction(?)
**
** Other things, one bit somewhere is probably whether items decay or not, whether
** something is magical or not, one bit for editible or not, bit for dyeable, can be poisioned,
** wearable and/or wear it can be worn, regeants bit? required str? damage for weapons?
** readable? nameable?
*/

/*
** 11/17/1999 Rewrite plans
**
** In reality most of the 'caching' code found within is really 'shadowing' code. That is,
** its all just slurped up into memory once at startup (very slow)  I plan on rewriting all
** of this so that its actually cached, that is if a piece is already in memory it will be
** used, otherwise gotten from disk.  There's no need to load them all at start up, in fact
** this is wasteful, because you wait at startup loading a lot of pieces you will never use.
** Also the map cache is retarded because it is alway shifting the entire buffer on a miss,
** rather than using a rolling queue. Finally, the verdata.mul stuff is not cached at all
** and has to be a huge performance hit constantly sifting through the index for that file.
** -fur
*/

/*!
** 12/08/1999 New Info
**
** OK, Zippy had taken a shot at the circular cache for the map0. He was close, but I had to
** fix a few bugs.  Also, I implemented the caching of the verdata.mul. Its important to note
** that the verdata.mul has lots of patches for all sorts of .mul files, I only kept the ones
** we were actively using.
**
** New rewrite plans, its way confusing having every function in here using 'Height' to mean
** elevation or actual height of a tile.  Here's the new renaming plan:
** Elevation - the z value of the bottom edge of an item
** Height - the height of the item, it is independant of the elevation, like a person is 6ft tall
** Top - the z value of the top edge of an item, is always Elevation + Height of item
** -fur
*/

/*!
** I thought it might be helpful to others to include a quick guide to what things
** meant in this code.
** Tile - a single item in the game, maybe static or dynamic
** Land - a single square from the map, the ground/water
** Static - a single item that comes as part of the basic map. NOTE: Land and Statics are both
**			saved in the same .mul file!  A lot of statics look like land as well.
** Dynamic - any item added that doesn't come as part of the basic map
** Single - an item in the game that will fit in a single cell
** Multi - any item in the game that is composed of multiple single items across multiple cells
**			maybe static or dynamic, may have multiple singles within same cell
**
*/

MapData_st::~MapData_st()
{
	if( mapObj != NULL )
	{
		delete mapObj;
		mapObj = NULL;
	}
	if( staticsObj != NULL )
	{
		delete staticsObj;
		staticsObj = NULL;
	}
	if( staidxObj != NULL )
	{
		delete staidxObj;
		staidxObj = NULL;
	}
	if( mapDiffObj != NULL )
	{
		delete mapDiffObj;
		mapDiffObj = NULL;
	}
	if( staticsDiffObj != NULL )
	{
		delete staticsDiffObj;
		staticsDiffObj = NULL;
	}
}

void cMapStuff::MultiItemsIndex::Include(SI16 x, SI16 y, SI16 z)
{
	// compute new bounding box, by include the new point
	if( x < lx ) 
		lx = x;
	if( x > hx ) 
		hx = x;
	if( y < ly ) 
		ly = y;
	if( y > hy )
		hy = y;
	if( z < lz )
		lz = z;
	if( z > hz )
		hz = z;
}


/*! Use Memory-Mapped file to do caching instead
** 'verdata.mul', 'tiledata.mul', 'multis.mul' and 'multi.idx' is changed so
**  it will reads into main memory and access directly.  'map*.mul', 'statics*.mul' and
**  'staidx.mul' is being managed by memory-mapped files, it faster than manual caching 
**  and less susceptible to bugs.
** -lingo
*/
cMapStuff::cMapStuff() : TileMem( 0 ), MultisMem( 0 ), landTile( 0 ), staticTile( 0 ), multiItems( 0 ), multiIndex( 0 ), multiIndexSize( 0 )
{
	UString tag, data, UTag;
	UI08 NumberOfWorlds = static_cast<UI08>(FileLookup->CountOfEntries( maps_def ));
	MapList.reserve( NumberOfWorlds );
	for( UI08 i = 0; i < NumberOfWorlds; ++i )
	{
		ScriptSection *toFind = FileLookup->FindEntry( "MAP " + UString::number( i ), maps_def );
		if( toFind == NULL )
			break;
		
		MapData_st toAdd;
		for( tag = toFind->First(); !toFind->AtEnd(); tag = toFind->Next() )
		{
			UTag = tag.upper();
			data = toFind->GrabData();
			switch( (tag.data()[0]) )
			{
			case 'M':
				if( UTag == "MAP" )
					toAdd.mapFile = data;
				else if( UTag == "MAPDIFF" )
					toAdd.mapDiffFile = data;
				else if( UTag == "MAPDIFFLIST" )
					toAdd.mapDiffListFile = data;
				break;
			case 'S':
				if( UTag == "STATICS" )
					toAdd.staticsFile = data;
				else if( UTag == "STAIDX" )
					toAdd.staidxFile = data;
				else if( UTag == "STATICSDIFF" )
					toAdd.staticsDiffFile = data;
				else if( UTag == "STATICSDIFFLIST" )
					toAdd.staticsDiffListFile = data;
				else if( UTag == "STATICSDIFFINDEX" )
					toAdd.staticsDiffIndexFile = data;
				break;
			case 'X':
				if( UTag == "X" )
					toAdd.xBlock = data.toUShort();
				break;
			case 'Y':
				if( UTag == "Y" )
					toAdd.yBlock = data.toUShort();
				break;
			}
		}
		MapList.push_back( toAdd );
	}
}

cMapStuff::~cMapStuff()
{
	if ( landTile )
		delete[] landTile;
	if ( staticTile )   
		delete[] staticTile;
	if ( multiItems )  
		delete[] multiItems;
	if ( multiIndex )
		delete[] multiIndex;
}


//!--------------------------------------------------------------------------o
//|	Function/Class	-	void cMapStuff::Load( void )
//|	Date			-	03/12/2002
//|	Developer(s)	-	Unknown / EviLDeD 
//|	Company/Team	-	UOX3 DevTeam
//|	Status			-	
//o--------------------------------------------------------------------------o
//|	Description		-	Prepare access streamultidxms to the server required mul files. 
//|									This function basicaly just opens the streams and validates
//|									that the file is open and available
//o--------------------------------------------------------------------------o
//|	Returns				-	N/A
//o--------------------------------------------------------------------------o
UOXFile * loadFile( const std::string fullName )
{
	UOXFile *toLoad = new UOXFile( fullName.c_str(), "rb" );
	Console << "\t" << fullName << "\t\t";

	if( toLoad != NULL && toLoad->ready() )
		Console.PrintDone();
	else
		Console.PrintSpecial( CBLUE, "not found" );

	return toLoad;
}

void cMapStuff::Load( void )
{
	Console.PrintSectionBegin();
	Console << "Preparing to open *.mul files..." << myendl << "(If they don't open, fix your paths in uox.ini or filenames in maps.dfn)" << myendl;

	UString lName;
	UI16 i;

	UString basePath = cwmWorldState->ServerData()->Directory( CSDDP_DATA );

	UI08 totalMaps = 0;
	for( MAPLIST_ITERATOR mlIter = MapList.begin(); mlIter != MapList.end(); ++mlIter )
	{
		MapData_st& mMap	= (*mlIter);

		lName				= basePath + mMap.mapFile;
		mMap.mapObj			= new UOXFile( lName.c_str(), "rb" );
		Console << "\t" << lName << "\t\t";

		if( mMap.mapObj != NULL && mMap.mapObj->ready() )
		{
			mMap.fileSize = mMap.mapObj->getLength();

			SI32 checkSize = (mMap.fileSize / 196);
			if( checkSize / (mMap.xBlock/8) == (mMap.yBlock/8) )
			{
				++totalMaps;
				Console.PrintDone();
			}
			else
				Console.PrintFailed();
		}
		else
			Console.PrintSpecial( CRED, "not found" );

		mMap.staticsObj				= loadFile( basePath + mMap.staticsFile );
		mMap.staidxObj				= loadFile( basePath + mMap.staidxFile );

		if( !mMap.mapDiffFile.empty() )
			mMap.mapDiffObj			= loadFile( basePath + mMap.mapDiffFile );

		if( !mMap.staticsDiffFile.empty() )
			mMap.staticsDiffObj		= loadFile( basePath + mMap.staticsDiffFile );

		if( !mMap.mapDiffListFile.empty() )
		{
			lName = basePath + mMap.mapDiffListFile;
			Console << "\t" << lName << "\t\t";

			std::ifstream mdlFile ( lName.c_str(), std::ios::in | std::ios::binary );
			if( mdlFile.is_open() )
			{
				size_t offset = 0;
				size_t blockID = 0;
				mdlFile.seekg( 0, std::ios::beg );
				while( !mdlFile.fail() )
				{
					mdlFile.read( (char *)&blockID, 4 );
					if( !mdlFile.fail() )
						mMap.mapDiffList[blockID] = offset+4;
					offset += 196;
				}
				mdlFile.close();
				Console.PrintDone();
			}
			else
				Console.PrintSpecial( CBLUE, "not found" );
		}

		if( !mMap.staticsDiffIndexFile.empty() && !mMap.staticsDiffListFile.empty() )
		{
			lName = basePath + mMap.staticsDiffIndexFile;
			Console << "\t" << lName << "\t\t";

			std::ifstream sdiFile ( lName.c_str(), std::ios::in | std::ios::binary );
			std::ifstream sdlFile ( lName.c_str(), std::ios::in | std::ios::binary );
			if( sdiFile.is_open() && sdlFile.is_open() )
			{
				size_t blockID = 0;
				sdiFile.seekg( 0, std::ios::beg );
				sdlFile.seekg( 0, std::ios::beg );
				while( !sdiFile.fail() && !sdlFile.fail() )
				{
					sdlFile.read( (char *)&blockID, 4 );
					StaticsIndex_st &sdiRecord = mMap.staticsDiffIndex[blockID];
					sdiFile.read( (char *)&sdiRecord.offset, 4 );
					sdiFile.read( (char *)&sdiRecord.length, 4 );
					sdiFile.read( (char *)&sdiRecord.unknown, 4 );
				}
				sdiFile.close();
				sdlFile.close();
				Console.PrintDone();
			}
			else
				Console.PrintSpecial( CBLUE, "not found" );

			lName = basePath + mMap.staticsDiffListFile;
			Console << "\t" << lName << "\t\t";

			if( FileExists( lName ) )
				Console.PrintDone();
			else
				Console.PrintSpecial( CBLUE, "not found" );
		}
	}

	if( totalMaps == 0 )
	{
		// Hmm, no maps were valid, so not much sense in continuing
		Console.Error( 1, " Fatal Error: No maps found" );
		Console.Error( 1, " Check the settings for DATADIRECTORY in uox.ini" );
		Shutdown( FATAL_UOX3_MAP_NOT_FOUND );
	}

	// tiledata.mul is to be cached.
	lName = basePath + "tiledata.mul";
	Console << "\t" << lName << "\t";
	UOXFile tilefile( lName.c_str(), "rb" );
	if( !tilefile.ready() )
	{
		Console.PrintFailed();
		Shutdown( FATAL_UOX3_TILEDATA_NOT_FOUND );
	}
	// first we have 512*32 pieces of land tile
	TileMem			= TILEDATA_SIZE * sizeof(CLand);
	landTile		= new CLand[TILEDATA_SIZE];
	CLand *landPtr	= landTile;
	for( i = 0; i < 512; ++i )	
	{
		tilefile.seek(4, SEEK_CUR);			// skip the dummy header
		tilefile.get_land_st(landPtr, 32);
		landPtr += 32;
	}
	// now get the 512*32 static tile pieces,
	TileMem			+= TILEDATA_SIZE * sizeof( CTile );
	staticTile		= new CTile[TILEDATA_SIZE];
	CTile *tilePtr	= staticTile;
	for( i = 0; i < 512; ++i )
	{
		tilefile.seek(4, SEEK_CUR);			// skip the dummy header
		tilefile.get_tile_st(tilePtr, 32);
		tilePtr += 32;
	}
	Console.PrintDone();

	LoadMultis( basePath );
	LoadDFNOverrides();

	FileLookup->Dispose( maps_def );
	Console.PrintSectionBegin();
}


void cMapStuff::LoadMultis(const UString &basePath)
{
	// now main memory multiItems
	Console << "Caching Multis....  "; 

	// only turn it off for now because we are trying to fill the cache.
	UString lName		= basePath + "multi.idx";

	UOXFile multiIDX( lName.c_str(), "rb" );
	if( !multiIDX.ready() )
	{
		Console.PrintFailed();
		Console.Error( 1, "Can't cache %s!  File cannot be opened", lName.c_str() );
		return;
	}
	lName			= basePath + "multi.mul";
	UOXFile multis( lName.c_str(), "rb" );
	if( !multis.ready() )
	{
		Console.PrintFailed();
		Console.Error( 1, "Can't cache %s!  File cannot be opened", lName.c_str() );
		return;
	}

	//! first reads in st_multi completely
	size_t multiSize	= multis.getLength() / MultiRecordSize;
	multiItems			= new st_multi[multiSize];
	multis.get_st_multi( multiItems, multiSize );
	MultisMem			= multis.getLength();

	multiIndexSize	= multiIDX.getLength() / MultiIndexRecordSize;
	multiIndex		= new MultiItemsIndex[multiIndexSize];
	MultisMem		= multiIndexSize * sizeof(MultiItemsIndex);

	// now rejig the multiIDX to point to the cache directly, and calculate the size
	for( MultiItemsIndex *ptr = multiIndex; ptr != (multiIndex+multiIndexSize); ++ptr )
	{
		st_multiidx multiidx;
		multiIDX.get_st_multiidx( &multiidx );
		ptr->size = multiidx.length;
		if( ptr->size != -1 )
		{
			ptr->size /= MultiRecordSize;		// convert byte size to record size
			ptr->items = (st_multi*)((char*)multiItems + multiidx.start);
			for( st_multi* items = ptr->items; items != (ptr->items+ptr->size); ++items )
			{	
				ptr->Include( items->x, items->y, items->z );
			}
		}
	}
	// reenable the caching now that its filled
	Console.PrintDone();
}


// this stuff is rather buggy thats why I separted it from uox3.cpp
// feel free to correct it, but be carefull
// bugfixing this stuff often has a domino-effect with walking etc.
// LB 24/7/99
// oh yah, well that's encouraging.. NOT! at least LB was kind enough to
// move this out into a separate file. he gets kudos for that!
SI08 cMapStuff::TileHeight( UI16 tilenum )
{
	CTile tile;
	SeekTile( tilenum, &tile );
	
	// For Stairs+Ladders
	if( tile.Climbable() ) 
		return (SI08)(tile.Height()/2);	// hey, lets just RETURN half!
	return tile.Height();
}


//o-------------------------------------------------------------o
//|   Function    :  SI08 StaticTop( SI16, SI16 y, SI08 oldz);
//|   Date        :  Unknown     Touched: Dec 21, 1998
//|   Programmer  :  Unknown
//o-------------------------------------------------------------o
//|   Purpose     :  Top of statics at/above given coordinates
//o-------------------------------------------------------------o
SI08 cMapStuff::StaticTop( SI16 x, SI16 y, SI08 oldz, UI08 worldNumber, SI08 maxZ )
{
	SI08 top = ILLEGAL_Z;

	MapStaticIterator msi( x, y, worldNumber );
	staticrecord *stat;
	while( stat = msi.Next() )
	{
		SI08 tempTop = (SI08)(stat->zoff + TileHeight(stat->itemid));
		if( ( tempTop <= oldz + maxZ ) && ( tempTop > top ) )
			top = tempTop;
	}
	return top;
}	


// i guess this function returns where the tile is a 'blocker' meaning you can't pass through it
bool cMapStuff::DoesTileBlock( UI16 tilenum )
{
	CTile tile;
	SeekTile( tilenum, &tile );
	return tile.Blocking();
}


//o--------------------------------------------------------------------------
//|	Function		-	void SeekMultiSizes( UI16 multiNum, SI16& x1, SI16& x2, SI16& y1, SI16& y2 )
//|	Date			-	26th September, 2001
//|	Programmer		-	Abaddon
//|	Modified		-
//o--------------------------------------------------------------------------
//|	Purpose			-	Updates the box coordinates based on cached information
//o--------------------------------------------------------------------------
void cMapStuff::SeekMultiSizes( UI16 multiNum, SI16& x1, SI16& x2, SI16& y1, SI16& y2 )
{
	x1 = multiIndex[multiNum].lx;		// originally lx 
	x2 = multiIndex[multiNum].hx;		// originally ly presumablely bugs?
	y1 = multiIndex[multiNum].ly;		// originally ly 
	y2 = multiIndex[multiNum].hy;		// originally hy presumablely bugs?
}


// multinum is also the index into the array!!!  Makes our life MUCH easier, doesn't it?
// TODO: Make it verseek compliant!!!!!!!!!
// Currently, there is no hit on either of the multi files, so we're fairly safe
// But we probably want to not rely on this being always true
// especially with the extra land patch coming up!
void cMapStuff::SeekMulti( UI16 multinum, SI32 *length )
{
	if( multinum >= multiIndexSize )
		*length = -1;
	else
		*length = multiIndex[multinum].size;
}


st_multi *cMapStuff::SeekIntoMulti( UI16 multinum, SI32 number )
{
	return (multiIndex[multinum].items + number);
}


//o--------------------------------------------------------------------------
//|	Function		-	void MultiArea( CMultiObj *i, SI16 &x1, SI16 &y1, SI16 &x2, SI16 &y2 )
//|	Date			-	6th September, 1999
//|	Programmer		-	Crackerjack
//|	Modified		-	Abaddon, 26th September, 2001 - fetches from cache
//o--------------------------------------------------------------------------
//|	Purpose			-	Finds the corners of a multi object
//o--------------------------------------------------------------------------
void cMapStuff::MultiArea( CMultiObj *i, SI16 &x1, SI16 &y1, SI16 &x2, SI16 &y2 )
{
	x1 = y1 = x2 = y2 = 0;
	if( !ValidateObject( i ) )
		return;
	const SI16 xAdd = i->GetX();
	const SI16 yAdd = i->GetY();
	SeekMultiSizes( static_cast<UI16>(i->GetID() - 0x4000), x1, x2, y1, y2 );

	x1 += xAdd;
	x2 += xAdd;
	y1 += yAdd;
	y2 += yAdd;
}


// return the height of a multi item at the given x,y. this seems to actually return a height
SI08 cMapStuff::MultiHeight( CItem *i, SI16 x, SI16 y, SI08 oldz, SI08 maxZ )
{                                                                                                                                  	
	SI32 length = 0;
	UI16 multiID = static_cast<UI16>( i->GetID() - 0x4000);
	SeekMulti( multiID, &length );
	st_multi *multi = NULL;

	if( length == -1 || length >= 17000000 ) //Too big... bug fix hopefully (Abaddon 13 Sept 1999)                                                                                                                          
		length = 0;
	
	for( SI32 j = 0; j < length; ++j )
	{
		multi = SeekIntoMulti( multiID, j );
		if( multi->visible && ( i->GetX() + multi->x == x) && ( i->GetY() + multi->y == y ) )
		{
			int tmpTop = i->GetZ() + multi->z;
			if( ( tmpTop <= oldz + maxZ ) && ( tmpTop >= oldz - 1 ) )
				return multi->z;
			else if( ( tmpTop >= oldz - maxZ ) && ( tmpTop < oldz - 1 ) )
				return multi->z;
		}                                                                                                                 
	}
	return 0;                                                                                                                     
} 


// This was fixed to actually return the *elevation* of dynamic items at/above given coordinates
SI08 cMapStuff::DynamicElevation( SI16 x, SI16 y, SI08 oldz, UI08 worldNumber, SI08 maxZ )
{
	SI08 z = ILLEGAL_Z;
	
	CMapRegion *MapArea = MapRegion->GetMapRegion( MapRegion->GetGridX( x ), MapRegion->GetGridY( y ), worldNumber );
	if( MapArea == NULL )	// no valid region
		return z;
	CDataList< CItem * > *regItems = MapArea->GetItemList();
	regItems->Push();
	for( CItem *tempItem = regItems->First(); !regItems->Finished(); tempItem = regItems->Next() )
	{
		if( !ValidateObject( tempItem ) )
			continue;
		if( tempItem->GetID( 1 ) >= 0x40 )
		{
			z = MultiHeight( tempItem, x, y, oldz, maxZ );
			z += tempItem->GetZ() + 1;
		}
		if( tempItem->GetX() == x && tempItem->GetY() == y && tempItem->GetID( 1 ) < 0x40 )
		{
			SI08 ztemp = (SI08)(tempItem->GetZ() + TileHeight( tempItem->GetID() ));
			if( ( ztemp <= oldz + maxZ ) && ztemp > z )
				z = ztemp;
		}
	}
	regItems->Pop();
	return z;
}


UI16 cMapStuff::MultiTile( CItem *i, SI16 x, SI16 y, SI08 oldz )
{
	if( !i->CanBeObjType( OT_MULTI ) )
		return 0;
	SI32 length = 0;
	UI16 multiID = static_cast<UI16>(i->GetID() - 0x4000);
	SeekMulti( multiID, &length );
	if( length == -1 || length >= 17000000 )//Too big... bug fix hopefully (Abaddon 13 Sept 1999)
	{
		Console << "cMapStuff::MultiTile->Bad length in multi file. Avoiding stall." << myendl;
		const map_st map1 = Map->SeekMap( i->GetX(), i->GetY(), i->WorldNumber() );
		CLand land;
		Map->SeekLand( map1.id, &land );
		if( land.LiquidWet() ) // is it water?
			i->SetID( 0x4001 );
		else
			i->SetID( 0x4064 );
		length = 0;
	}

	st_multi *multi = NULL;
	for( SI32 j = 0; j < length; ++j )
	{
		multi = SeekIntoMulti( multiID, j );
		if( ( multi->visible && ( i->GetX() + multi->x == x) && (i->GetY() + multi->y == y)
			&& ( abs( i->GetZ() + multi->z - oldz ) <= 1 ) ) )
		{
			return multi->tile;
		}
		
	}
	return 0;
}


// returns which dynamic tile is present at (x,y) or -1 if no tile exists
UI16 cMapStuff::DynTile( SI16 x, SI16 y, SI08 oldz, UI08 worldNumber )
{
	CMapRegion *MapArea = MapRegion->GetMapRegion( MapRegion->GetGridX( x ), MapRegion->GetGridY( y ), worldNumber );
	if( MapArea == NULL )	// no valid region
		return INVALIDID;

	CDataList< CItem * > *regItems = MapArea->GetItemList();
	regItems->Push();
	for( CItem *tempItem = regItems->First(); !regItems->Finished(); tempItem = regItems->Next() )
	{
		if( !ValidateObject( tempItem ) )
			continue;
		if( tempItem->GetID( 1 ) >= 0x40 )
		{
			regItems->Pop();
			return MultiTile( tempItem, x, y, oldz );
		}
		else if( tempItem->GetX() == x && tempItem->GetY() == y )
		{
			regItems->Pop();
			return tempItem->GetID();
		}
	}
	regItems->Pop();
	return INVALIDID;
}


// return the elevation of MAP0.MUL at given coordinates, we'll assume since its land
// the height is inherently 0
SI08 cMapStuff::MapElevation( SI16 x, SI16 y, UI08 worldNumber )
{
	const map_st map = SeekMap( x, y, worldNumber );
	// make sure nothing can move into black areas
	if( 430 == map.id || 475 == map.id || 580 == map.id || 610 == map.id ||
		611 == map.id || 612 == map.id || 613 == map.id)
		return ILLEGAL_Z;
	return map.z;
}


// compute the 'average' map height by looking at three adjacent cells
SI08 cMapStuff::AverageMapElevation( SI16 x, SI16 y, UI16 &id, UI08 worldNumber )
{
	// first thing is to get the map where we are standing
	const map_st map1 = Map->SeekMap( x, y, worldNumber );
	id = map1.id;
	// if this appears to be a valid land id, <= 2 is invalid
	if( map1.id > 2 && ILLEGAL_Z != MapElevation( x, y, worldNumber ) )
	{
		// get three other nearby maps to decide on an average z?
		const SI08 map2z = MapElevation( x + 1, y, worldNumber );
		const SI08 map3z = MapElevation( x, y + 1, worldNumber );
		const SI08 map4z = MapElevation( x + 1, y + 1, worldNumber );
		
		SI08 testz = 0;
		if( abs( map1.z - map4z ) <= abs( map2z - map3z ) )
		{
			if( ILLEGAL_Z == map4z )
				testz = map1.z;
			else
			{
				testz = (SI08)((map1.z + map4z) >> 1);
				if( testz%2 < 0 ) 
					--testz;
				// ^^^ Fix to make it round DOWN, not just in the direction of zero
			}
		}
		else
		{
			if( ILLEGAL_Z == map2z || ILLEGAL_Z == map3z )
				testz = map1.z;
			else
			{
				testz = (SI08)((map2z + map3z) >> 1);
				if( testz%2<0 )
					--testz;
				// ^^^ Fix to make it round DOWN, not just in the direction of zero
			}
		}
		return testz;
	}
	return ILLEGAL_Z;
}


bool cMapStuff::SeekTile( UI16 tilenum, CTile *tile)
{
	if( tilenum == INVALIDID || tilenum >= TILEDATA_SIZE )
	{	// report the invalid access, but keep running
		Console << "invalid tile access the offending tile num is " << tilenum << myendl;
		const static CTile emptyTile;
		*tile = emptyTile;			// arbitrary choice, default constructor
		return false;
	}
	else
		*tile = staticTile[tilenum];
	return true;
}


void cMapStuff::SeekLand( UI16 landnum, CLand *land)
{

	if( landnum == INVALIDID || landnum >= TILEDATA_SIZE )
	{	// report the invalid access, but keep running
		Console << "invalid Land tile access the offending land num is " << landnum << myendl;
		const static CLand emptyTile;
		*land = emptyTile;			// arbitray empty choice
	}
	else
		*land = landTile[landnum];
}


bool cMapStuff::InsideValidWorld( SI16 x, SI16 y, UI08 worldNumber )
{
	if( worldNumber >= MapList.size() )
		return false;

	return ( ( x >= 0 && x < (MapList[worldNumber].xBlock/8) ) && ( y >= 0 && y < (MapList[worldNumber].yBlock/8) ) );
}


/*!
** Use this iterator class anywhere you would have used SeekInit() and SeekStatic()
** Unlike those functions however, it will only return the location of tiles that match your
** (x,y) EXACTLY.  They also should be significantly faster since the iterator saves
** a lot of info that was recomputed over and over and it returns a pointer instead 
** of an entire structure on the stack.  You can call First() if you need to reset it.
** This iterator hides all of the map implementation from other parts of the program.
** If you supply the exact variable, it tells it whether to iterate over those items
** with your exact (x,y) otherwise it will loop over all items in the same cell as you.
** (Thats normally only used for filling the cache)
** 
** Usage:
**		MapStaticIterator msi( x, y, worldNumber );
**
**      staticrecord *stat = NULL;
**      CTile tile;
**      while( stat = msi.Next() )
**      {
**          msi.GetTile( &tile );
**  		    ... your code here...
**	  	}
*/
MapStaticIterator::MapStaticIterator( UI32 x, UI32 y, UI08 world, bool exact ) : baseX( static_cast<SI32>(x / 8) ), 
baseY( static_cast<SI32>(y / 8) ), pos( 0 ), remainX( static_cast<UI08>(x % 8 )), remainY( static_cast<UI08>(y % 8 )), 
index( 0 ), length( 0 ), tileid( 0 ), exactCoords( exact ), worldNumber( world ), useDiffs( false )
{
	if( !Map->InsideValidWorld( static_cast<SI16>(baseX), static_cast<SI16>(baseY), world ) )
	{
		Console.Error( 3, "ASSERT: MapStaticIterator(); Not inside a valid world" );
		return;
	}

	MapData_st& mMap = Map->GetMapData( world );

	const SI16 TileHeight = (mMap.yBlock/8);
	const SI32 indexPos = (( baseX * TileHeight * 12L ) + ( baseY * 12L ));

	std::map< UI32, StaticsIndex_st >::const_iterator diffIter = mMap.staticsDiffIndex.find( indexPos/12 );
	if( diffIter != mMap.staticsDiffIndex.end() )
	{
		const StaticsIndex_st &sdiRecord = diffIter->second;
		
		if( sdiRecord.offset == 0xFFFFFFFF )
		{
			length = 0;
			return;
		}

		useDiffs = true;
		length = sdiRecord.length;
	}
	else
	{
		UOXFile *toRead = mMap.staidxObj;
		if( toRead == NULL )
		{
			length = 0;
			return;
		}

		toRead->seek( indexPos, SEEK_SET );
		if( !toRead->eof() )
		{
			toRead->getLong( &pos );
			if( pos != -1 )
			{
				toRead->getULong( &length );
				length /= StaticRecordSize;
				useDiffs = false;
			}
		}
	}
}

staticrecord *MapStaticIterator::First( void )
{
	index = 0;
	return Next();
}

staticrecord *MapStaticIterator::Next( void )
{
	tileid = 0;
	if( index >= length )
		return NULL;

	MapData_st& mMap = Map->GetMapData( worldNumber );
	UOXFile *mFile = NULL;
	
	if( useDiffs )
		mFile = mMap.staticsDiffObj;
	else
		mFile = mMap.staticsObj;

	if( mFile == NULL )
		return NULL;
	
	// this was sizeof(OldStaRec) which SHOULD be 7, but on some systems which don't know how to pack, 
	const SI32 pos2 = pos + (StaticRecordSize * index);	// skip over all the ones we've read so far
	mFile->seek( pos2, SEEK_SET );
	do
	{
		if( mFile->eof( ) )
			return NULL;
		
		mFile->get_staticrecord( &staticArray );
		++index;
		// if these are ever larger than 7 we've gotten out of sync
		assert( staticArray.xoff < 0x08 );
		assert( staticArray.yoff < 0x08 );
		if( !exactCoords || ( staticArray.xoff == remainX && staticArray.yoff == remainY ) )
		{
#ifdef DEBUG_MAP_STUFF
			Console << "Found static at index: " << index << ", Length: " << length << ", indepos: " << pos2 << myendl;
			Console << "item is " << (int)staticArray.itemid << ", x" << (int)staticArray.xoff << ", y: " << (int) staticArray.yoff << myendl;
#endif
			tileid = staticArray.itemid;
			return &staticArray;
		}
	} while( index < length );
	
	return NULL;
}

// since 99% of the time we want the tile at the requested location, here's a
// helper function.  pass in the pointer to a struct you want filled.
void MapStaticIterator::GetTile( CTile *tile ) const
{
	assert( tile );
	Map->SeekTile( tileid, tile );
}


map_st cMapStuff::SeekMap( SI16 x, SI16 y, UI08 worldNumber )
{
	const SI16 x1 = x / 8, y1 = y / 8;
	const UI08 x2 = (UI08)(x % 8), y2 = (UI08)(y % 8);
	map_st map = {0, 0};
	if( worldNumber >= MapList.size() )
		return map;

	MapData_st& mMap = MapList[worldNumber];
	// index to the world
	const size_t blockID = x / 8 * (mMap.yBlock/8) + y / 8;
	std::map< UI32, UI32 >::const_iterator diffIter = mMap.mapDiffList.find( blockID );
	if( diffIter != mMap.mapDiffList.end() )
	{
		mMap.mapDiffObj->seek( diffIter->second, SEEK_SET );
		mMap.mapDiffObj->get_map_st( &map );
	}
	else
	{
		const SI32 pos = ( x1 * (mMap.yBlock/8) * 196 ) + ( y1 * 196 ) + ( y2 * 24 ) + ( x2 * 3 ) + 4;
		mMap.mapObj->seek( pos, SEEK_SET );
		mMap.mapObj->get_map_st( &map );
	}
	return map;
}


// these function don't look like they are actually used by anything
// anymore, at least we know which bit means wet
bool cMapStuff::IsTileWet( UI16 tilenum )   // lord binary
{
	CTile tile;
	SeekTile( tilenum, &tile );
	return tile.LiquidWet();
}


// Blocking statics at/above given coordinates?
bool cMapStuff::DoesStaticBlock( SI16 x, SI16 y, SI08 oldz, UI08 worldNumber, bool checkWater )
{
	MapStaticIterator msi( x, y, worldNumber );
	
	staticrecord *stat = NULL;
	while( stat = msi.Next() )
	{
		const SI08 elev = static_cast<SI08>(stat->zoff + TileHeight( stat->itemid ));
		if( elev > oldz && stat->zoff <= oldz && DoesTileBlock( stat->itemid ) || ( checkWater && IsTileWet( stat->itemid ) ) )
			return true;
	}
	return false;
}


// Return new height of player who walked to X/Y but from OLDZ
SI08 cMapStuff::Height( SI16 x, SI16 y, SI08 oldz, UI08 worldNumber )
{
	// let's check in this order.. dynamic, static, then the map
	const SI08 dynz = DynamicElevation( x, y, oldz, worldNumber, MAX_Z_STEP );
	if( ILLEGAL_Z != dynz )
		return dynz;

	const SI08 staticz = StaticTop( x, y, oldz, worldNumber, MAX_Z_STEP );
	if( ILLEGAL_Z != staticz )
		return staticz;

	return MapElevation( x, y, worldNumber );
}

// Return whether give coordinates are inside a building by checking if there is a multi or static above them
bool cMapStuff::inBuilding( SI16 x, SI16 y, SI08 z, UI08 worldNumber )
{
		const SI08 dynz = Map->DynamicElevation( x, y, z, worldNumber, (SI08)127 );
		if( dynz > ( z + 10))
			return true;
		else
		{
			const SI08 staticz = Map->StaticTop( x, y, z, worldNumber, (SI08)127 );
			if( staticz > ( z + 10))
				return true;
		}
		return false;
}

// can the monster move here from an adjacent cell at elevation 'oldz'
// use illegal_z if they are teleporting from an unknown z
bool cMapStuff::CanMonsterMoveHere( SI16 x, SI16 y, SI08 oldz, UI08 worldNumber, bool checkWater )
{
	if( worldNumber >= MapList.size() )
		return false;

	MapData_st& mMap = MapList[worldNumber];
	if( x < 0 || y < 0 || x >= mMap.xBlock || y >= mMap.yBlock  )
		return false;
    const SI08 elev = Height( x, y, oldz, worldNumber );
	if( ILLEGAL_Z == elev )
		return false;

	// is it too great of a difference z-value wise?
	if( oldz != ILLEGAL_Z )
	{
		// you can climb MaxZstep, but fall up to 15
		if( elev - oldz > MAX_Z_STEP )
			return false;
		else if( oldz - elev > 15 )
			return false;
	}

    // get the tile id of any dynamic tiles at this spot
    const UI16 dt = DynTile( x, y, elev, worldNumber );
	
    // if there is a dynamic tile at this spot, check to see if its a blocker
    // if it does block, might as well short-circuit and return right away
    if( dt != INVALIDID && ( DoesTileBlock( dt ) || ( checkWater && IsTileWet( dt ) ) ) )
		return false;

    // if there's a static block here in our way, return false
    if( DoesStaticBlock( x, y, elev, worldNumber, checkWater ) )
		return false;
	else if( checkWater )
	{
		CLand land;
		const map_st map = Map->SeekMap( x, y, worldNumber );
		Map->SeekLand( map.id, &land );
		if( land.LiquidWet() )
			return false;
	}
    return true;
}

//o--------------------------------------------------------------------------
//|	Function		-	bool MapExists( UI08 worldNumber )
//|	Date			-	27th September, 2001
//|	Programmer		-	Abaddon
//|	Modified		-
//o--------------------------------------------------------------------------
//|	Purpose			-	Returns true if the server has that map in memory
//o--------------------------------------------------------------------------
bool cMapStuff::MapExists( UI08 worldNumber )
{
	return ( (worldNumber < MapList.size()) && (MapList[worldNumber].mapObj != NULL) );
}

MapData_st& cMapStuff::GetMapData( UI08 worldNumber )
{
	return MapList[worldNumber];
}

UI08 cMapStuff::MapCount( void ) const
{
	return static_cast<UI08>(MapList.size());
}

void cMapStuff::LoadDFNOverrides( void )
{
	UString data, UTag, entryName, titlePart;
	size_t entryNum;

	for( Script *mapScp = FileLookup->FirstScript( maps_def ); !FileLookup->FinishedScripts( maps_def ); mapScp = FileLookup->NextScript( maps_def ) )
	{
		if( mapScp == NULL )
			continue;
		for( ScriptSection *toScan = mapScp->FirstEntry(); toScan != NULL; toScan = mapScp->NextEntry() )
		{
			if( toScan == NULL )
				continue;
			entryName	= mapScp->EntryName();
			entryNum	= entryName.section( " ", 1, 1 ).toULong();
			titlePart	= entryName.section( " ", 0, 0 ).upper();
			// have we got an entry starting with TILE ?
			if( titlePart == "TILE" && entryNum )
			{
				if( entryNum == INVALIDID || entryNum >= TILEDATA_SIZE )
					continue;
				CTile *tile = &staticTile[entryNum];
				if( tile != NULL )
				{
					for( UString tag = toScan->First(); !toScan->AtEnd(); tag = toScan->Next() )
					{
						data	= toScan->GrabData();
						UTag	= tag.upper();

						// CTile properties
						if( UTag == "WEIGHT" )
							tile->Weight( data.toUByte() );
						else if( UTag == "HEIGHT" )
							tile->Height( data.toByte() );
						else if( UTag == "LAYER" )
							tile->Layer( data.toByte() );
						else if( UTag == "ANIMATION" )
							tile->Animation( data.toInt() );
						else if( UTag == "NAME" )
							tile->Name( data.c_str() );

						// BaseTile Flag 1
						else if( UTag == "ATFLOORLEVEL" )
							tile->AtFloorLevel( (data.toInt() != 0) );
						else if( UTag == "HOLDABLE" )
							tile->Holdable( (data.toInt() != 0) );
						else if( UTag == "SIGNGUILDBANNER" )
							tile->SignGuildBanner( (data.toInt() != 0) );
						else if( UTag == "WEBDIRTBLOOD" )
							tile->WebDirtBlood( (data.toInt() != 0) );
						else if( UTag == "WALLVERTTILE" )
							tile->WallVertTile( (data.toInt() != 0) );
						else if( UTag == "DAMAGING" )
							tile->Damaging( (data.toInt() != 0) );
						else if( UTag == "BLOCKING" )
							tile->Blocking( (data.toInt() != 0) );
						else if( UTag == "LIQUIDWET" )
							tile->LiquidWet( (data.toInt() != 0) );

						// BaseTile Flag 2
						else if( UTag == "UNKNOWN1" )
							tile->Unknown1( (data.toInt() != 0) );
						else if( UTag == "STANDABLE" )
							tile->Standable( (data.toInt() != 0) );
						else if( UTag == "CLIMBABLE" )
							tile->Climbable( (data.toInt() != 0) );
						else if( UTag == "STACKABLE" )
							tile->Stackable( (data.toInt() != 0) );
						else if( UTag == "WINDOWARCHDOOR" )
							tile->WindowArchDoor( (data.toInt() != 0) );
						else if( UTag == "CANNOTSHOOTTHRU" )
							tile->CannotShootThru( (data.toInt() != 0) );
						else if( UTag == "DISPLAYASA" )
							tile->DisplayAsA( (data.toInt() != 0) );
						else if( UTag == "DISPLAYASAN" )
							tile->DisplayAsAn( (data.toInt() != 0) );

						// BaseTile Flag 3
						else if( UTag == "DESCRIPTIONTILE" )
							tile->DescriptionTile( (data.toInt() != 0) );
						else if( UTag == "FADEWITHTRANS" )
							tile->FadeWithTrans( (data.toInt() != 0) );
						else if( UTag == "UNKNOWN2" )
							tile->Unknown2( (data.toInt() != 0) );
						else if( UTag == "UNKNOWN3" )
							tile->Unknown3( (data.toInt() != 0) );
						else if( UTag == "MAP" )
							tile->Map( (data.toInt() != 0) );
						else if( UTag == "CONTAINER" )
							tile->Container( (data.toInt() != 0) );
						else if( UTag == "EQUIPABLE" )
							tile->Equipable( (data.toInt() != 0) );
						else if( UTag == "LIGHTSOURCE" )
							tile->LightSource( (data.toInt() != 0) );

						// BaseTile Flag 4
						else if( UTag == "ANIMATED" )
							tile->Animated( (data.toInt() != 0) );
						else if( UTag == "UNKNOWN4" )
							tile->Unknown4( (data.toInt() != 0) );
						else if( UTag == "WALK" )
							tile->Walk( (data.toInt() != 0) );
						else if( UTag == "WHOLEBODYITEM" )
							tile->WholeBodyItem( (data.toInt() != 0) );
						else if( UTag == "WALLROOFWEAP" )
							tile->WallRoofWeap( (data.toInt() != 0) );
						else if( UTag == "DOOR" )
							tile->Door( (data.toInt() != 0) );
						else if( UTag == "CLIMBABLEBIT1" )
							tile->ClimbableBit1( (data.toInt() != 0) );
						else if( UTag == "CLIMBABLEBIT2" )
							tile->ClimbableBit2( (data.toInt() != 0) );
					}
				}
			}
			else if( titlePart == "LAND" && entryNum )
			{	// let's not deal with this just yet
			}
		}
	}
}

void CTile::Read( UOXFile *toRead )
{
//	DWORD Flags (see below)
//	BYTE Weight (weight of the item, 255 means not movable)
//	BYTE Quality (If Wearable, this is a Layer. If Light Source, this is Light ID)
//	UWORD Unknown
//	BYTE Unknown1
//	BYTE Quantity (if Weapon, this is Weapon Class. If Armor, Armor Class)
//	UWORD Anim ID (The Body ID the animatation. Add 50,000 and 60,000 respectivefully to get the two gump indicies assocaited with this tile)
//	BYTE Unknown2
//	BYTE Hue (perhaps colored light?)
//	UWORD Unknown3
//	BYTE Height (If Conatainer, this is how much the container can hold)
//	CHAR[20] Tile Name
	toRead->getUChar( &flag1 );
	toRead->getUChar( &flag2 );
	toRead->getUChar( &flag3 );
	toRead->getUChar( &flag4 );
	toRead->getUChar( &weight );
	toRead->getChar(  &layer );
	toRead->getLong(  &unknown1 );
	toRead->getLong(  &animation );
	toRead->getChar(  &unknown2 );
	toRead->getChar(  &unknown3 );
	toRead->getChar(  &height );
	toRead->getChar(  name, 20 );

}

void CLand::Read( UOXFile *toRead )
{
	toRead->getUChar( &flag1 );
	toRead->getUChar( &flag2 );
	toRead->getUChar( &flag3 );
	toRead->getUChar( &flag4 );
	toRead->getUShort( &textureID );
	toRead->getChar( name, 20 );
}


}
