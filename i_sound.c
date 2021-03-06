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
//	System interface for sound.
//
//-----------------------------------------------------------------------------

static const char
rcsid[] = "$Id: i_unix.c,v 1.5 1997/02/03 22:45:10 b1 Exp $";

#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include <math.h>

#include <time.h>
#include <sys/types.h>

//#ifndef LINUX
//#include <sys/filio.h>
//#endif

#include <fcntl.h>
#include <io.h>
//#include <unistd.h>
//#include <sys/ioctl.h>

// Linux voxware output.
//#include <linux/soundcard.h>

// Timer stuff. Experimental.
#include <time.h>
#include <signal.h>

#include "z_zone.h"

#include "i_system.h"
#include "i_sound.h"
#include "m_argv.h"
#include "m_misc.h"
#include "w_wad.h"

#include "doomdef.h"


////////////////////////////////////////////////////////////////////////////
// WinDoom - DirectSound
////////////////////////////////////////////////////////////////////////////
#include <dsound.h>

#define NUM_SOUND_FX 128
#define SB_SIZE      20480

dboolean SetupDirectSound();
dboolean CreateSoundBuffer(int Channel, int length, int speed, unsigned char *data);
void I_PlaySoundEffect(int sfxid, int Channel, int volume, int pan);
void DS_Error( HRESULT hresult, char *msg );

#define NUM_DSBUFFERS 256

typedef enum { dsb_perm, dsb_temp } dsb_type;

extern LPDIRECTSOUND        lpDS;
extern LPDIRECTSOUNDBUFFER  lpDSBuffer[NUM_DSBUFFERS];

typedef struct
   {
    void   *origin;
    int     dsb_type;
    int     sfxid;
   }DSBControl_t;

DSBControl_t DSBControl[NUM_DSBUFFERS];

extern int swap_stereo;

void lfprintf(char *message, ... );

////////////////////////////////////////////////////////////////////////////

// UNIX hack, to be removed.
#ifdef SNDSERV
// Separate sound server process.
FILE*	sndserver=0;
char*	sndserver_filename = "./sndserver ";
#elif SNDINTR

// Update all 30 millisecs, approx. 30fps synchronized.
// Linux resolution is allegedly 10 millisecs,
//  scale is microseconds.
#define SOUND_INTERVAL     500

// Get the interrupt. Set duration in millisecs.
int I_SoundSetTimer( int duration_of_tick );
void I_SoundDelTimer( void );
#else
// None?
#endif


// A quick hack to establish a protocol between
// synchronous mix buffer updates and asynchronous
// audio writes. Probably redundant with gametic.
static int flag = 0;

// The number of internal mixing channels,
//  the samples calculated for each mixing step,
//  the size of the 16bit, 2 hardware channel (stereo)
//  mixing buffer, and the samplerate of the raw data.


// Needed for calling the actual sound output.
#define SAMPLECOUNT		512
#define NUM_CHANNELS		16
// It is 2 for 16bit, and 2 for two channels.
#define BUFMUL                  4
#define MIXBUFFERSIZE		(SAMPLECOUNT*BUFMUL)

#define SAMPLERATE		11025	// Hz
#define SAMPLESIZE		2   	// 16bit

// The actual lengths of all sound effects.
int 		lengths[NUMSFX];
int         speeds[NUMSFX];

// The actual output device.
int	audio_fd;

// The global mixing buffer.
// Basically, samples from all active internal channels
//  are modifed and added, and stored in the buffer
//  that is submitted to the audio device.
signed short	mixbuffer[MIXBUFFERSIZE];


// The channel step amount...
unsigned int	channelstep[NUM_CHANNELS];
// ... and a 0.16 bit remainder of last step.
unsigned int	channelstepremainder[NUM_CHANNELS];


// The channel data pointers, start and end.
unsigned char*	channels[NUM_CHANNELS];
unsigned char*	channelsend[NUM_CHANNELS];


// Time/gametic that the channel started playing,
//  used to determine oldest, which automatically
//  has lowest priority.
// In case number of active sounds exceeds
//  available channels.
int		channelstart[NUM_CHANNELS];

// The sound in channel handles,
//  determined on registration,
//  might be used to unregister/stop/modify,
//  currently unused.
int 		channelhandles[NUM_CHANNELS];

// SFX id of the playing sound effect.
// Used to catch duplicates (like chainsaw).
int		channelids[NUM_DSBUFFERS];

// Pitch to stepping lookup, unused.
int		steptable[256];

// Volume lookups.
int		vol_lookup[128*256];

// Hardware left and right channel volume lookup.
int*		channelleftvol_lookup[NUM_CHANNELS];
int*		channelrightvol_lookup[NUM_CHANNELS];

//
// Safe ioctl, convenience.
//
void
myioctl
( int	fd,
  int	command,
  int*	arg )
{   
// FIXME
/*
    int		rc;
    extern int	errno;
    
    rc = ioctl(fd, command, arg);  
    if (rc < 0)
    {
	fprintf(stderr, "ioctl(dsp,%d,arg) failed\n", command);
	fprintf(stderr, "errno=%d\n", errno);
	exit(-1);
    }
*/
}





//
// This function loads the sound data from the WAD lump,
//  for single sound.
//
void *getsfx( char *sfxname, int *len, int *speed )
   {
    unsigned char*      sfx;
    unsigned char*      paddedsfx;
    int                 i;
    int                 size;
    int                 paddedsize;
    char                name[20];
    int                 sfxlump;
    short               s_speed;

   
    sprintf(name, "ds%s", sfxname);

    // Get the sound data from the WAD, allocate lump
    //  in zone memory.
    // Now, there is a severe problem with the
    //  sound handling, in it is not (yet/anymore)
    //  gamemode aware. That means, sounds from
    //  DOOM II will be requested even with DOOM
    //  shareware.
    // The sound list is wired into sounds.c,
    //  which sets the external variable.
    // I do not do runtime patches to that
    //  variable. Instead, we will use a
    //  default sound for replacement.

    if ( W_CheckNumForName(name) == -1 )
      sfxlump = W_GetNumForName("dspistol");
    else
      sfxlump = W_GetNumForName(name);
    
    size = W_LumpLength( sfxlump );

    // Debug.
    // fprintf( stderr, "." );
    //fprintf( stderr, " -loading  %s (lump %d, %d bytes)\n",
    //	     sfxname, sfxlump, size );
    //fflush( stderr );
    
    sfx = (unsigned char*)W_CacheLumpNum( sfxlump, PU_STATIC );

    memcpy((unsigned char *)&s_speed, sfx+2, 2);
    *speed = s_speed;

    // Pads the sound effect out to the mixing buffer size.
    // The original realloc would interfere with zone memory.
    paddedsize = ((size-8 + (SAMPLECOUNT-1)) / SAMPLECOUNT) * SAMPLECOUNT;

    // Allocate from zone memory.
    paddedsfx = (unsigned char*)Z_Malloc( paddedsize+8, PU_STATIC, 0 );
    // ddt: (unsigned char *) realloc(sfx, paddedsize+8);
    // This should interfere with zone memory handling,
    //  which does not kick in in the soundserver.

    // Now copy and pad.
    memcpy(  paddedsfx, sfx, size );
    for (i = size; i < paddedsize+8; i++)
        paddedsfx[i] = 128;

    // Remove the cached lump.
    Z_Free( sfx );
    
    // Preserve padded length.
    *len = paddedsize;

    // Return allocated padded data.
    return (void *) (paddedsfx + 8);
   }



/*

//
// This function adds a sound to the
//  list of currently active sounds,
//  which is maintained as a given number
//  (eight, usually) of internal channels.
// Returns a handle.
//
int addsfx( int sfxid, int volume, int step, int seperation )
   {
    static unsigned short	handlenums = 0;
 
    int		i;
    int		rc = -1;
    
    int		oldest = gametic;
    int		oldestnum = 0;
    int		slot;

    int		rightvol;
    int		leftvol;

    int     iVolume, iPan;

    // Chainsaw troubles.
    // Play these sound effects only one at a time.
    if ( sfxid == sfx_sawup || sfxid == sfx_sawidl || sfxid == sfx_sawful ||
         sfxid == sfx_sawhit || sfxid == sfx_stnmov || sfxid == sfx_pistol )
       {
        // Loop all channels, check.
        for (i = 0; i < NUM_CHANNELS; i++)
           {
            // Active, and using the same SFX?
            if ( (channels[i]) && (channelids[i] == sfxid) )
               {
                // Reset.
                channels[i] = 0;
                // We are sure that iff, there will only be one.
                break;
               }
           }
       }

    // Loop all channels to find oldest SFX.
    for (i = 0; (i<NUM_CHANNELS) && (channels[i]); i++)
        {
         if (channelstart[i] < oldest)
            {
             oldestnum = i;
             oldest = channelstart[i];
            }
        }

    // Tales from the cryptic.
    // If we found a channel, fine.
    // If not, we simply overwrite the first one, 0.
    // Probably only happens at startup.
    if (i == NUM_CHANNELS)
        slot = oldestnum;
    else
        slot = i;

    // Okay, in the less recent channel,
    //  we will handle the new SFX.
    // Set pointer to raw data.
    channels[slot] = (unsigned char *) S_sfx[sfxid].data;
    // Set pointer to end of raw data.
    channelsend[slot] = channels[slot] + lengths[sfxid];

    // Reset current handle number, limited to 0..100.
    if (!handlenums)
        handlenums = 100;

    // Assign current handle number.
    // Preserved so sounds could be stopped (unused).
    channelhandles[slot] = rc = handlenums++;

    // Set stepping???
    // Kinda getting the impression this is never used.
    channelstep[slot] = step;
    // ???
    channelstepremainder[slot] = 0;
    // Should be gametic, I presume.
    channelstart[slot] = gametic;

    iVolume = 0-(128*(15-volume));
    if (iVolume < -10000)
       iVolume == -10000;
    iPan = (seperation-128)*20;
    if (iPan < -10000)
       iPan = -10000;
    if (iPan > 10000)
       iPan = 10000;

    if (swap_stereo == true)
       iPan *= -1;

    // Separation, that is, orientation/stereo.
    //  range is: 1 - 256

    seperation += 1;

    // Per left/right channel.
    //  x^2 seperation,
    //  adjust volume properly.
    leftvol = volume - ((volume*seperation*seperation) >> 16); //(256*256);
    seperation = seperation - 257;
    rightvol = volume - ((volume*seperation*seperation) >> 16);

    // Sanity check, clamp volume.
    if (rightvol < 0 || rightvol > 127)
        I_Error("rightvol out of bounds");
    
    if (leftvol < 0 || leftvol > 127)
        I_Error("leftvol out of bounds");
    
    // Get the proper lookup table piece
    //  for this volume level???
    channelleftvol_lookup[slot] = &vol_lookup[leftvol*256];
    channelrightvol_lookup[slot] = &vol_lookup[rightvol*256];

    // Preserve sound SFX id,
    //  e.g. for avoiding duplicates of chainsaw.
    channelids[slot] = sfxid;

    I_PlaySoundEffect(sfxid, iVolume, iPan);

    // You tell me.
    //return rc;
    return sfxid;
   }

*/
//
// This function adds a sound to the
//  list of currently active sounds,
//  which is maintained as a given number
//  (eight, usually) of internal channels.
// Returns a handle.
//
int addsfx( int sfxid, int volume, int step, int seperation, void *origin )
   {
    static unsigned short	handlenums = 0;
 
    int		i;
    int		rc = -1;
    
    int		oldest = gametic;
    int		oldestnum = 0;
    int		slot;

    int		rightvol;
    int		leftvol;

    int     iVolume, iPan;
    DWORD   dwDSBStatus;
    int     dsbchannel;

    // Chainsaw troubles.
    // Play these sound effects only one at a time.
    if ( sfxid == sfx_sawup  || sfxid == sfx_sawidl || sfxid == sfx_sawful ||
         sfxid == sfx_sawhit || sfxid == sfx_stnmov )
        dsbchannel = sfxid;
    else
       {
        dsbchannel = sfxid;
        if (lpDSBuffer[0] != 0)
            lpDSBuffer[sfxid]->lpVtbl->GetStatus(lpDSBuffer[sfxid], &dwDSBStatus);
        else
            dwDSBStatus = DS_OK;
        if (dwDSBStatus == DSBSTATUS_PLAYING)
           {
            for (i = NUMSFX; i < NUM_DSBUFFERS; i++)
               {
                if ((DSBControl[i].origin == origin) && (DSBControl[i].sfxid == sfxid))
                   {
                    dsbchannel = i;
                    break;
                   }
                if (DSBControl[i].sfxid == -1)
                   {
                    dsbchannel = i;
                    DSBControl[i].origin = origin;
                    break;
                   }
                if (DSBControl[i].sfxid >= 0)
                   {
                    lpDSBuffer[i]->lpVtbl->GetStatus(lpDSBuffer[i], &dwDSBStatus);
                    if (dwDSBStatus != DSBSTATUS_PLAYING)
                       {
                        dsbchannel = i;
                        DSBControl[i].origin = origin;
                        break;
                       }
                   }
                if (DSBControl[i].origin == origin)
                   {
                    dsbchannel = i;
                    break;
                   }
               }
           }
       }

    iVolume = 0-(128*(15-volume));
    if (iVolume < -10000)
       iVolume == -10000;
    iPan = (seperation-128)*20;
    if (iPan < -10000)
       iPan = -10000;
    if (iPan > 10000)
       iPan = 10000;

    if (swap_stereo == true)
       iPan *= -1;

    // Preserve sound SFX id,
    //  e.g. for avoiding duplicates of chainsaw.
    channelids[dsbchannel] = sfxid;

    I_PlaySoundEffect(sfxid, dsbchannel, iVolume, iPan);

    // You tell me.
    //return rc;
    return dsbchannel;
   }

// This function call starts a sound playing in a DirectSound buffer...
void I_PlaySoundEffect(int sfxid, int Channel, int iVolume, int iPan)
   {
    HRESULT hresult;
    DWORD   dwDSBStatus;

    if (Channel > NUM_SOUND_FX)
       {
        return;
       }
    if (lpDSBuffer[Channel] == 0)
       {
        return;
       }

    if ((DSBControl[Channel].dsb_type == dsb_temp) && (DSBControl[Channel].sfxid != sfxid))
       {
        if (DSBControl[Channel].sfxid > 0)
           {
/*
            lpDSBuffer[Channel]->lpVtbl->GetStatus(lpDSBuffer[Channel], &dwDSBStatus);
            if (dwDSBStatus == DSBSTATUS_PLAYING)
               {
                hresult = lpDSBuffer[Channel]->lpVtbl->Stop(lpDSBuffer[Channel]);
                if (hresult != DS_OK)
                    DS_Error(hresult, "lpDSBuffer.Stop");
               }
*/
            lpDSBuffer[Channel]->lpVtbl->Release(lpDSBuffer[Channel]);
            DSBControl[Channel].sfxid = -1;
           }

        if (DSBControl[Channel].sfxid < 0)
           {
            // clone temp buffer here...
            lpDS->lpVtbl->DuplicateSoundBuffer(lpDS, lpDSBuffer[sfxid], &lpDSBuffer[Channel]);
            DSBControl[Channel].sfxid = sfxid;
           }
       }
/*
    else
       {
        lpDSBuffer[Channel]->lpVtbl->GetStatus(lpDSBuffer[Channel], &dwDSBStatus);
        if (dwDSBStatus == DSBSTATUS_PLAYING)
           {
            hresult = lpDSBuffer[Channel]->lpVtbl->Stop(lpDSBuffer[Channel]);
            if (hresult != DS_OK)
                DS_Error(hresult, "lpDSBuffer.Stop");
           }
       }
*/
    hresult = lpDSBuffer[Channel]->lpVtbl->SetCurrentPosition(lpDSBuffer[Channel], 0);
    if (hresult != DS_OK)
        DS_Error(hresult, "lpDSBuffer.SetCurrentPosition");
    hresult = lpDSBuffer[Channel]->lpVtbl->SetVolume(lpDSBuffer[Channel], iVolume );
    if (hresult != DS_OK)
        DS_Error(hresult, "lpDSBuffer.SetVolume");
    hresult = lpDSBuffer[Channel]->lpVtbl->SetPan(lpDSBuffer[Channel], iPan);
    if (hresult != DS_OK)
        DS_Error(hresult, "lpDSBuffer.SetPan");
    hresult = lpDSBuffer[Channel]->lpVtbl->Play(lpDSBuffer[Channel], 0, 0, 0);
    if (hresult != DS_OK)
        DS_Error(hresult, "lpDSBuffer.Play");
   }

//
// SFX API
// Note: this was called by S_Init.
// However, whatever they did in the
// old DPMS based DOS version, this
// were simply dummies in the Linux
// version.
// See soundserver initdata().
//
void I_SetChannels()
{
  // Init internal lookups (raw data, mixing buffer, channels).
  // This function sets up internal lookups used during
  //  the mixing process. 
  int		i;
  int		j;
    
  int*	steptablemid = steptable + 128;
  
  // Okay, reset internal mixing channels to zero.
  for (i=0; i<NUM_CHANNELS; i++)
  {
    channels[i] = 0;
  }

  // This table provides step widths for pitch parameters.
  // I fail to see that this is currently used.
  for (i=-128 ; i<128 ; i++)
    steptablemid[i] = (int)(pow(2.0, (i/64.0))*65536.0);
  
  
  // Generates volume lookup tables
  //  which also turn the unsigned samples
  //  into signed samples.
  for (i=0 ; i<128 ; i++)
    for (j=0 ; j<256 ; j++)
      vol_lookup[i*256+j] = (i*(j-128)*256)/127;
}	

 
void I_SetSfxVolume(int volume)
{
  // Identical to DOS.
  // Basically, this should propagate
  //  the menu/config file setting
  //  to the state variable used in
  //  the mixing.
  snd_SfxVolume = volume;
}

// MUSIC API - dummy. Some code from DOS version.
void I_SetMusicVolume(int volume)
{
  // Internal state variable.
  snd_MusicVolume = volume;
  // Now set volume on output device.
  // Whatever( snd_MusciVolume );
}


//
// Retrieve the raw data lump index
//  for a given SFX name.
//
int I_GetSfxLumpNum(sfxinfo_t* sfx)
{
    char namebuf[9];
    sprintf(namebuf, "ds%s", sfx->name);
    return W_GetNumForName(namebuf);
}

//
// Starting a sound means adding it
//  to the current list of active sounds
//  in the internal channels.
// As the SFX info struct contains
//  e.g. a pointer to the raw data,
//  it is ignored.
// As our sound handling does not handle
//  priority, it is ignored.
// Pitching (that is, increased speed of playback)
//  is set, but currently not used by mixing.
//
int I_StartSound( int id, int vol, int sep, int pitch, int priority, void *origin )
   {
    // UNUSED
    priority = 0;
  
    // Debug.
    
    // Returns a handle (not used).
    id = addsfx( id, vol, steptable[pitch], sep, origin );

    return id;
   }



void I_StopSound (int handle)
   {
    HRESULT hresult;
  // You need the handle returned by StartSound.
  // Would be looping all channels,
  //  tracking down the handle,
  //  an setting the channel to zero.
  
  // UNUSED.
//    handle = 0;

    if (handle > NUM_DSBUFFERS)
       {
        return;
       }
    if (lpDSBuffer[handle] == 0)
       {
        return;
       }

    hresult = lpDSBuffer[handle]->lpVtbl->Stop(lpDSBuffer[handle]);
    if (hresult != DS_OK)
        DS_Error(hresult, "lpDSBuffer.Stop");
/*
    else
    if (DSBControl[handle].dsb_type == dsb_temp)
       {
        lpDSBuffer[handle]->lpVtbl->Release(lpDSBuffer[handle]);
        DSBControl[handle].origin = NULL;
        DSBControl[handle].sfxid = -1;
       }
*/
   }


int I_SoundIsPlaying(int handle)
   {
    DWORD dwStatus;
    HRESULT hresult;

    if (lpDSBuffer[handle] == 0)
       {
        return false;
       }

    hresult = lpDSBuffer[handle]->lpVtbl->GetStatus(lpDSBuffer[handle], &dwStatus);
    if (hresult != DS_OK)
        DS_Error(hresult, "lpDSBuffer.GetStatus");
    return (dwStatus == DSBSTATUS_PLAYING);
    // Ouch.
//    return gametic < handle;
   }




//
// This function loops all active (internal) sound
//  channels, retrieves a given number of samples
//  from the raw sound data, modifies it according
//  to the current (internal) channel parameters,
//  mixes the per channel samples into the global
//  mixbuffer, clamping it to the allowed range,
//  and sets up everything for transferring the
//  contents of the mixbuffer to the (two)
//  hardware channels (left and right, that is).
//
// This function currently supports only 16bit.
//
void I_UpdateSound( void )
   {
#ifdef SNDINTR
  // Debug. Count buffer misses with interrupt.
  static int misses = 0;
#endif

  
    // Mix current sound data.
    // Data, from raw sound, for right and left.
    register unsigned int	sample;
    register int		dl;
    register int		dr;
  
    // Pointers in global mixbuffer, left, right, end.
    signed short*		leftout;
    signed short*		rightout;
    signed short*		leftend;
    // Step in mixbuffer, left and right, thus two.
    int				step;

    // Mixing channel index.
    int				chan;
    
    // Left and right channel
    //  are in global mixbuffer, alternating.
    leftout = mixbuffer;
    rightout = mixbuffer+1;
    step = 2;

    // Determine end, for left channel only
    //  (right channel is implicit).
    leftend = mixbuffer + SAMPLECOUNT*step;

    // Mix sounds into the mixing buffer.
    // Loop over step*SAMPLECOUNT,
    //  that is 512 values for two channels.
    while (leftout != leftend)
    {
	// Reset left/right value. 
	dl = 0;
	dr = 0;

	// Love thy L2 chache - made this a loop.
	// Now more channels could be set at compile time
	//  as well. Thus loop those  channels.
	for ( chan = 0; chan < NUM_CHANNELS; chan++ )
	{
	    // Check channel, if active.
	    if (channels[ chan ])
	    {
		// Get the raw data from the channel. 
		sample = *channels[ chan ];
		// Add left and right part
		//  for this channel (sound)
		//  to the current data.
		// Adjust volume accordingly.
		dl += channelleftvol_lookup[ chan ][sample];
		dr += channelrightvol_lookup[ chan ][sample];
		// Increment index ???
		channelstepremainder[ chan ] += channelstep[ chan ];
		// MSB is next sample???
		channels[ chan ] += channelstepremainder[ chan ] >> 16;
		// Limit to LSB???
		channelstepremainder[ chan ] &= 65536-1;

		// Check whether we are done.
		if (channels[ chan ] >= channelsend[ chan ])
		    channels[ chan ] = 0;
	    }
	}
	
	// Clamp to range. Left hardware channel.
	// Has been char instead of short.
	// if (dl > 127) *leftout = 127;
	// else if (dl < -128) *leftout = -128;
	// else *leftout = dl;

	if (dl > 0x7fff)
	    *leftout = 0x7fff;
	else if (dl < -0x8000)
	    *leftout = -0x8000;
	else
	    *leftout = dl;

	// Same for right hardware channel.
	if (dr > 0x7fff)
	    *rightout = 0x7fff;
	else if (dr < -0x8000)
	    *rightout = -0x8000;
	else
	    *rightout = dr;

	// Increment current pointers in mixbuffer.
	leftout += step;
	rightout += step;
    }

#ifdef SNDINTR
    // Debug check.
    if ( flag )
    {
      misses += flag;
      flag = 0;
    }
    
    if ( misses > 10 )
    {
      fprintf( stderr, "I_SoundUpdate: missed 10 buffer writes\n");
      misses = 0;
    }
    
    // Increment flag for update.
    flag++;
#endif
}


// 
// This would be used to write out the mixbuffer
//  during each game loop update.
// Updates sound buffer and audio device at runtime. 
// It is called during Timer interrupt with SNDINTR.
// Mixing now done synchronous, and
//  only output be done asynchronous?
//
void
I_SubmitSound(void)
{
  // Write it to DSP device.
  write(audio_fd, mixbuffer, SAMPLECOUNT*BUFMUL);
}



void
I_UpdateSoundParams
( int	handle,
  int	vol,
  int	sep,
  int	pitch)
{
  // I fail too see that this is used.
  // Would be using the handle to identify
  //  on which channel the sound might be active,
  //  and resetting the channel parameters.
 int iVolume, iPan;
 HRESULT hresult;

 if (lpDSBuffer[handle] == 0)
    return;

    iVolume = 0-(128*(15-vol));
    if (iVolume < -10000)
       iVolume == -10000;
    iPan = (sep-128)*20;
    if (iPan < -10000)
       iPan = -10000;
    if (iPan > 10000)
       iPan = 10000;

    if (swap_stereo == true)
       iPan *= -1;

    hresult = lpDSBuffer[handle]->lpVtbl->SetVolume(lpDSBuffer[handle], iVolume );
    if (hresult != DS_OK)
        DS_Error(hresult, "lpDSBuffer.SetVolume");
    hresult = lpDSBuffer[handle]->lpVtbl->SetPan(lpDSBuffer[handle], iPan);
    if (hresult != DS_OK)
        DS_Error(hresult, "lpDSBuffer.SetPan");
  // UNUSED.
  //handle = vol = sep = pitch = 0;
}




void I_ShutdownSound(void)
{    
#ifdef SNDSERV
  if (sndserver)
  {
    // Send a "quit" command.
    fprintf(sndserver, "q\n");
    fflush(sndserver);
  }
#else
  // Wait till all pending sounds are finished.
  int done = 0;
  int i;
  

  // FIXME (below).
  fprintf( stderr, "I_ShutdownSound: NOT finishing pending sounds\n");
  fflush( stderr );
  
  while ( !done )
  {
    for( i=0 ; i<8 && !channels[i] ; i++);
    
    // FIXME. No proper channel output.
    //if (i==8)
    done=1;
  }
#ifdef SNDINTR
  I_SoundDelTimer();
#endif
  
  // Cleaning up -releasing the DSP device.
  close ( audio_fd );
#endif

  // Done.
  return;
}

void I_InitSound()
   { 
    int i;

    SetupDirectSound();

    for (i = 1; i < NUMSFX; i++)
       { 
        // Alias? Example is the chaingun sound linked to pistol.
        if (!S_sfx[i].link)
           {
            // Load data from WAD file.
            S_sfx[i].data = getsfx( S_sfx[i].name, &lengths[i], &speeds[i] );
           }	
        else
           {
            // Previously loaded already? - substitution
            S_sfx[i].data = S_sfx[i].link->data;
            lengths[i] = lengths[(S_sfx[i].link - S_sfx)];
            speeds[i] = speeds[(S_sfx[i].link - S_sfx)];
           }
        if (CreateSoundBuffer(i, lengths[i], speeds[i], S_sfx[i].data) == false)
           {
            lfprintf("Could not create sound buffer for %s - %d %d\n", S_sfx[i].name, lengths[i], speeds[i]);
           }
        DSBControl[i].origin = NULL;
        DSBControl[i].sfxid = i;
        DSBControl[i].dsb_type = dsb_perm;
       }
    for (; i < NUM_DSBUFFERS; i++)
       {
        DSBControl[i].origin = NULL;
        DSBControl[i].sfxid = -1;
        DSBControl[i].dsb_type = dsb_temp;
       }
   }




//
// MUSIC API.
// Still no music done.
// Remains. Dummies.
//

#include "m_music.h"

MidiConvert( const char *mid, int nodisplay, int div, int size, int nocomp, dboolean overwrite, void *MusBuff, int bufflength );

unsigned char *musicdata;

void I_InitMusic(void)
   {
   }

void I_ShutdownMusic(void)
   {
   }

static int	looping=0;
static int	musicdies=-1;

void I_PlaySong(int handle, int looping)
   {
    PlayMidiFile("DOOMSONG.MID");
    //handle = looping = 0;
    //musicdies = gametic + TICRATE*30;
   }

void I_PauseSong (int handle)
   {
    PauseResumeMusic();
    //handle = 0;
   }

void I_ResumeSong (int handle)
   {
    PauseResumeMusic();
    //handle = 0;
   }

void I_StopSong(int handle)
   {
    StopMusic();
    //handle = 0;
  
    //looping = 0;
    //musicdies = 0;
   }

void I_UnRegisterSong(int handle)
  {
   unlink("DOOMSONG.MID");
   musicdata = 0;
  }

int I_RegisterSong(void* data, int buffsize)
   {
    musicdata = (unsigned char *)data;
    MidiConvert( "DOOMSONG.MID", true, 0, 128, false, true, musicdata, buffsize );
    return 1;
   }

// Is the song playing?
int I_QrySongPlaying(int handle)
{
  // UNUSED.
  handle = 0;
  return looping || musicdies > gametic;
}



//
// Experimental stuff.
// A Linux timer interrupt, for asynchronous
//  sound output.
// I ripped this out of the Timer class in
//  our Difference Engine, including a few
//  SUN remains...
//  
#ifdef sun
    typedef     sigset_t        tSigSet;
#else    
    typedef     int             tSigSet;
#endif


// We might use SIGVTALRM and ITIMER_VIRTUAL, if the process
//  time independend timer happens to get lost due to heavy load.
// SIGALRM and ITIMER_REAL doesn't really work well.
// There are issues with profiling as well.

// FIXME
#define ITIMER_REAL 0
static int /*__itimer_which*/  itimer = ITIMER_REAL;

// FIXME
#define SIGALRM 0
static int sig = SIGALRM;

// Interrupt handler.
void I_HandleSoundTimer( int ignore )
{
  // Debug.
  //fprintf( stderr, "%c", '+' ); fflush( stderr );
  
  // Feed sound device if necesary.
  if ( flag )
  {
    // See I_SubmitSound().
    // Write it to DSP device.
    write(audio_fd, mixbuffer, SAMPLECOUNT*BUFMUL);

    // Reset flag counter.
    flag = 0;
  }
  else
    return;
  
  // UNUSED, but required.
  ignore = 0;
  return;
}

// Get the interrupt. Set duration in millisecs.
int I_SoundSetTimer( int duration_of_tick )
{
/*
  // Needed for gametick clockwork.
  struct itimerval    value;
  struct itimerval    ovalue;
  struct sigaction    act;
  struct sigaction    oact;

  int res;
  
  // This sets to SA_ONESHOT and SA_NOMASK, thus we can not use it.
  //     signal( _sig, handle_SIG_TICK );
  
  // Now we have to change this attribute for repeated calls.
  act.sa_handler = I_HandleSoundTimer;
#ifndef sun    
  //ac	t.sa_mask = _sig;
#endif
  act.sa_flags = SA_RESTART;
  
  sigaction( sig, &act, &oact );

  value.it_interval.tv_sec    = 0;
  value.it_interval.tv_usec   = duration_of_tick;
  value.it_value.tv_sec       = 0;
  value.it_value.tv_usec      = duration_of_tick;

  // Error is -1.
  res = setitimer( itimer, &value, &ovalue );

  // Debug.
  if ( res == -1 )
    fprintf( stderr, "I_SoundSetTimer: interrupt n.a.\n");
  
  return res;
*/
 return 0;
}


// Remove the interrupt. Set duration to zero.
void I_SoundDelTimer()
{
  // Debug.
  if ( I_SoundSetTimer( 0 ) == -1)
    fprintf( stderr, "I_SoundDelTimer: failed to remove interrupt. Doh!\n");
}
