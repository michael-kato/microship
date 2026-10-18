#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
// Feed the real game's Win32 input queue. No game state is accessed.
int main(int argc,char** argv)
{
    HWND window=FindWindowA("nanovaWindow","Nanova");
    if (!window) { fprintf(stderr,"Nanova window not found.\n");return 1; }
    if (argc==2 && !strcmp(argv[1],"--pan"))
    {
        if (!PostMessageA(window,WM_MBUTTONDOWN,MK_MBUTTON,MAKELPARAM(900,500))
            || !PostMessageA(window,WM_MOUSEMOVE,MK_MBUTTON,MAKELPARAM(980,480))
            || !PostMessageA(window,WM_MBUTTONUP,0,MAKELPARAM(980,480))) return 1;
        printf("Queued middle-button pan to the game window.\n");return 0;
    }
    if (argc!=1) { fprintf(stderr,"Expected no arguments or --pan.\n");return 2; }
    const LPARAM scan=(LPARAM)MapVirtualKeyA(VK_F1,MAPVK_VK_TO_VSC)<<16;
    if (!PostMessageA(window,WM_KEYDOWN,VK_F1,scan|1)
        || !PostMessageA(window,WM_KEYDOWN,VK_F1,scan|1|((LPARAM)1<<30))
        || !PostMessageA(window,WM_KEYUP,VK_F1,scan|1|((LPARAM)1<<30)|((LPARAM)1<<31))) return 1;
    printf("Queued F1 press, held repeat and release to the game window.\n");return 0;
}
