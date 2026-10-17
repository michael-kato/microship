#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
// Feed the real game's Win32 input queue. No game state is accessed.
int main()
{
    HWND window=FindWindowA("nanovaWindow","Nanova");
    if (!window) { fprintf(stderr,"Nanova window not found.\n");return 1; }
    const LPARAM scan=(LPARAM)MapVirtualKeyA(VK_F1,MAPVK_VK_TO_VSC)<<16;
    if (!PostMessageA(window,WM_KEYDOWN,VK_F1,scan|1)
        || !PostMessageA(window,WM_KEYDOWN,VK_F1,scan|1|((LPARAM)1<<30))
        || !PostMessageA(window,WM_KEYUP,VK_F1,scan|1|((LPARAM)1<<30)|((LPARAM)1<<31))) return 1;
    printf("Queued F1 press, held repeat and release to the game window.\n");return 0;
}
