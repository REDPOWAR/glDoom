// win_kbrd.c
//
// This module handles the DirectInput startup code and initializes
// the keyboard interface as well as handling the keyboard input
// and shutting down the keyboard interface and DirectInput.

#include <windows.h>
#include <dinput.h>
#include <stdio.h>

#include "doomtype.h"
#include "d_main.h"
#include "d_event.h"
#include "dxerr.h"
#include "sys_win.h"

#undef RELEASE
#ifdef __cplusplus
#define RELEASE(x) if (x != NULL) {x->Release(); x = NULL;}
#else
#define RELEASE(x) if (x != NULL) {x->lpVtbl->Release(x); x = NULL;}
#endif

LPDIRECTINPUT        lpDirectInput = 0;
LPDIRECTINPUTDEVICE  lpKeyboard    = 0;

#define KS_KEYUP        0
#define KS_KEYDOWN    255

unsigned char        diKeyState[256];
short                si_Kbd[256];

extern int           keylink;

extern windata_t     WinData;

void lfprintf(char *message, ... );

void I_ReleaseKeyboard()
   {
    if (lpKeyboard != 0)
       {
        lpKeyboard->lpVtbl->Unacquire(lpKeyboard);
        lpKeyboard->lpVtbl->Release(lpKeyboard);
        lpKeyboard = 0;
       }
    RELEASE(lpDirectInput);
   }

dboolean I_SetupKeyboard()
   {
    int     k;
    HRESULT hresult;

    hresult = DirectInput8Create(WinData.hInstance, DIRECTINPUT_VERSION, &IID_IDirectInput8, &lpDirectInput, NULL );
    if (hresult != DI_OK)
       {
        DI_Error( hresult, "DirectInputCreate");
        return false;
       }
    hresult = lpDirectInput->lpVtbl->CreateDevice(lpDirectInput, &GUID_SysKeyboard, &lpKeyboard, NULL );
    if (hresult != DI_OK)
       {
        DI_Error( hresult, "CreateDevice (keyboard)");
        I_ReleaseKeyboard();
        return false;
       }
    hresult = lpKeyboard->lpVtbl->SetDataFormat(lpKeyboard, &c_dfDIKeyboard);
    if (hresult != DI_OK)
       {
        DI_Error( hresult, "SetDataFormat (keyboard)");
        I_ReleaseKeyboard();
        return false;
       }
    hresult = lpKeyboard->lpVtbl->SetCooperativeLevel(lpKeyboard, WinData.hWnd, DISCL_NONEXCLUSIVE | DISCL_FOREGROUND);
    if (hresult != DI_OK)
       {
        DI_Error( hresult, "SetCooperativeLevel (keyboard)");
        I_ReleaseKeyboard();
        return false;
       }
    hresult = lpKeyboard->lpVtbl->Acquire(lpKeyboard);
    if (hresult != DI_OK)
       {
        DI_Error( hresult, "Acquire (keyboard)");
        I_ReleaseKeyboard();
        return false;
       }

    // Set the keyboard buffer to "all keys up"
    for (k = 0; k < 256; k++)
        si_Kbd[k] = KS_KEYUP;

    return true;
   }

char t_text[2048];

void I_CheckKeyboard()
   {
    HRESULT          hresult;
    static  event_t  event[256];
    unsigned short   lm, rm, mk;
    int     i;

    if (lpKeyboard == 0)
       {
        return;
       }

    RetryKeyboard:;

    hresult = lpKeyboard->lpVtbl->GetDeviceState(lpKeyboard, sizeof(diKeyState), &diKeyState);
    if ((hresult == DIERR_INPUTLOST) || (hresult == DIERR_NOTACQUIRED))
       {
        hresult = lpKeyboard->lpVtbl->Acquire(lpKeyboard);
        if (SUCCEEDED(hresult))
            goto RetryKeyboard;
       }
    else
    if (hresult != DI_OK)
       {
        DI_Error(hresult, "GetDeviceState (keyboard)");
        sprintf(t_text, "hresult = %08X\n", hresult);
        lm = 0;
       }
    else
       {
        for (i = 1; i < 256; i++)
           {
/*
            if (keylink == false || (i != DIK_LMENU && i != DIK_RMENU &&
                                    i != DIK_LSHIFT && i != DIK_RSHIFT &&
                                    i != DIK_LCONTROL && i != DIK_RCONTROL))
               {
*/
                if (((diKeyState[i] & 0x80) == 0) && (si_Kbd[i] == KS_KEYDOWN))
                   {
                    event[i].type = ev_keyup;
                    event[i].data1 = i;
                    D_PostEvent(&event[i]);
                    si_Kbd[i] = KS_KEYUP;
                   }

                if ((diKeyState[i] & 0x80) && (si_Kbd[i] == KS_KEYUP))
                   {
                    if ((i != DIK_TAB) || ((diKeyState[DIK_LMENU] == KS_KEYUP) && (diKeyState[DIK_RMENU] == KS_KEYUP)))
                       {
                        event[i].type = ev_keydown;
                        event[i].data1 = i;
                        D_PostEvent(&event[i]);
                        si_Kbd[i] = KS_KEYDOWN;
                       }
                   }
/*
               }
*/
           }
       }
   }
