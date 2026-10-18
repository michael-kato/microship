#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
// Feed the real game's Win32 input queue. No game state is accessed.
static bool key(HWND window,WPARAM code,bool down,bool repeat=false)
{
    LPARAM scan=(LPARAM)MapVirtualKeyA((UINT)code,MAPVK_VK_TO_VSC)<<16;
    return PostMessageA(window,down ? WM_KEYDOWN : WM_KEYUP,code,scan|1
        |((!down || repeat) ? (LPARAM)1<<30 : 0)|(!down ? (LPARAM)1<<31 : 0))!=0;
}
static bool press(HWND window,WPARAM code)
{ return key(window,code,true) && key(window,code,true,true) && key(window,code,false); }

int main(int argc,char** argv)
{
    bool render=argc>=2 && !strcmp(argv[1],"--render-keys");
    HWND window=FindWindowA("nanovaWindow","Nanova");
    if (render) for (unsigned attempt=0;!window && attempt<100;++attempt)
    { Sleep(5);window=FindWindowA("nanovaWindow","Nanova"); }
    if (!window) { fprintf(stderr,"Nanova window not found.\n");return 1; }
    if (render)
    {
        for (int i=2;i<argc;++i)
        {
            if (!strcmp(argv[i],"--next"))
            { printf("First input queued.\n");fflush(stdout);if (getchar()!='\n') return 1; }
            else if (!strcmp(argv[i],"pause")) { if (!press(window,VK_SPACE)) return 1;SetCursorPos(0,0); }
            else if (!strcmp(argv[i],"buffers")) { if (!press(window,VK_F2)) return 1; }
            else if (!strcmp(argv[i],"repeat"))
            {
                // Focus loss, a late repeat, then a new press must cancel once.
                for (WPARAM code=VK_F5;code<=VK_F9;++code)
                    if (!key(window,code,true) || !PostMessageA(window,WM_KILLFOCUS,0,0)
                        || !key(window,code,true,true) || !key(window,code,false) || !press(window,code)) return 1;
                if (!press(window,VK_F1) || !press(window,VK_F1)
                    || !press(window,VK_F2) || !press(window,VK_F2)) return 1;
            }
            else if (strlen(argv[i])==1 && argv[i][0]>='5' && argv[i][0]<='9')
            { if (!press(window,VK_F5+argv[i][0]-'5')) return 1; }
            else { fprintf(stderr,"Expected rendering key 5..9, pause, buffers or repeat.\n");return 2; }
        }
        printf("Queued rendering keys, with held repeats, to the game window.\n");return 0;
    }
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
