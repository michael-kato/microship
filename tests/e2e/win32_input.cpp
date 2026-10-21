#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
// Feed the real game's Win32 input queue. No game state is accessed.
static bool key(HWND window,WPARAM code,bool down,bool repeat=false)
{
    LPARAM scan=(LPARAM)MapVirtualKeyA((UINT)code,MAPVK_VK_TO_VSC)<<16;
    return PostMessageA(window,code==VK_F10 ? (down ? WM_SYSKEYDOWN : WM_SYSKEYUP) : (down ? WM_KEYDOWN : WM_KEYUP),code,scan|1
        |((!down || repeat) ? (LPARAM)1<<30 : 0)|(!down ? (LPARAM)1<<31 : 0))!=0;
}
static bool press(HWND window,WPARAM code)
{ return key(window,code,true) && key(window,code,true,true) && key(window,code,false); }

static int menu_input(int argc,char** argv)
{
    bool close=argc==2 && !strcmp(argv[1],"--menu-close");
    if (!close && (argc!=4 || strcmp(argv[1],"--menu-click"))) return 2;
    HWND window=0;
    for (unsigned attempt=0;!window && attempt<200;++attempt)
    { window=FindWindowA("nanovaStartMenu","nanova - Choose faction");if (!window) Sleep(5); }
    if (!window) { fprintf(stderr,"Nanova menu not found.\n");return 1; }
    if (close) return PostMessageA(window,WM_CLOSE,0,0) ? 0 : 1;
    char* end=0;long x=strtol(argv[2],&end,10);if (end==argv[2] || *end || x<0 || x>8191) return 2;
    long y=strtol(argv[3],&end,10);if (end==argv[3] || *end || y<0 || y>8191) return 2;
    RECT client;if (!GetClientRect(window,&client) || x>=client.right || y>=client.bottom) return 2;
    LPARAM point=MAKELPARAM(x,y);
    if (!PostMessageA(window,WM_LBUTTONDOWN,MK_LBUTTON,point)
        || !PostMessageA(window,WM_LBUTTONUP,0,point)) return 1;
    printf("Queued menu click at %ld %ld.\n",x,y);return 0;
}


struct UiTarget { DWORD process;const char* klass;HWND window; };
static BOOL CALLBACK find_ui(HWND window,LPARAM context)
{
    UiTarget* target=(UiTarget*)context;DWORD process=0;GetWindowThreadProcessId(window,&process);
    char klass[80]={};GetClassNameA(window,klass,sizeof(klass));
    if (process==target->process && !strcmp(klass,target->klass) && IsWindowVisible(window))
    { target->window=window;return FALSE; }
    return TRUE;
}
static HWND ui_window(DWORD process,const char* klass)
{
    UiTarget target={process,klass,0};
    for (unsigned attempt=0;!target.window && attempt<400;++attempt)
    { EnumWindows(find_ui,(LPARAM)&target);if (!target.window) Sleep(5); }
    return target.window;
}
static HWND ui_control(DWORD process,const char* klass,const char* text,HWND* parent)
{
    for (unsigned attempt=0;attempt<400;++attempt)
    {
        UiTarget target={process,"nanovaPauseMenu",0};EnumWindows(find_ui,(LPARAM)&target);
        HWND child=target.window ? FindWindowExA(target.window,0,klass,text) : 0;
        if (child) { *parent=target.window;return child; }
        Sleep(5);
    }
    fprintf(stderr,"UI control not found: %s / %s.\n",klass,text);return 0;
}
static int ui_input(DWORD process)
{
    char line[512];
    while (fgets(line,sizeof(line),stdin))
    {
        char* end=strpbrk(line,"\r\n");if (end) *end=0;
        char* command=strtok(line,"|"),*label=strtok(0,"|"),*value=strtok(0,"|");
        if (!command) return 2;
        const char* klass=!strcmp(command,"game_escape") || !strcmp(command,"game_dirty") ? "nanovaWindow"
            : !strcmp(command,"faction") ? "nanovaStartMenu" : "nanovaPauseMenu";
        HWND window=ui_window(process,klass);if (!window) { fprintf(stderr,"UI window not found: %s.\n",klass);return 1; }
        if (!strcmp(command,"game_escape") || !strcmp(command,"escape"))
        { if (!press(window,VK_ESCAPE)) return 1; }
        else if (!strcmp(command,"game_dirty"))
        {
            if (!key(window,'W',true) || !PostMessageA(window,WM_MBUTTONDOWN,MK_MBUTTON,MAKELPARAM(900,500))
                || !PostMessageA(window,WM_MOUSEMOVE,MK_MBUTTON,MAKELPARAM(990,550))
                || !PostMessageA(window,WM_MBUTTONUP,0,MAKELPARAM(990,550))
                || !PostMessageA(window,WM_RBUTTONUP,0,MAKELPARAM(1400,500)) || !key(window,'W',false)) return 1;
        }
        else if (!strcmp(command,"close"))
        { if (!PostMessageA(window,WM_CLOSE,0,0)) return 1; }
        else if (!strcmp(command,"faction"))
        { if (!label || label[0]<'1' || label[0]>'3' || label[1] || !press(window,label[0])) return 2; }
        else if (!strcmp(command,"click"))
        {
            HWND child=label ? ui_control(process,"BUTTON",label,&window) : 0;if (!child) return 1;
            SendMessageA(window,WM_COMMAND,MAKEWPARAM(GetDlgCtrlID(child),BN_CLICKED),(LPARAM)child);
        }
        else if (!strcmp(command,"set") || !strcmp(command,"get"))
        {
            HWND child=label ? ui_control(process,"STATIC",label,&window) : 0;if (!child) return 1;
            HWND combo=GetWindow(child,GW_HWNDNEXT);char name[32]={};GetClassNameA(combo,name,sizeof(name));
            if (strcmp(name,"ComboBox")) return 1;
            if (!strcmp(command,"set"))
            {
                if (!value) return 2;
                int index=(int)SendMessageA(combo,CB_FINDSTRINGEXACT,(WPARAM)-1,(LPARAM)value);
                if (index<0) { fprintf(stderr,"UI choice not found: %s = %s.\n",label,value);return 1; }
                SendMessageA(combo,CB_SETCURSEL,index,0);
                SendMessageA(window,WM_COMMAND,MAKEWPARAM(GetDlgCtrlID(combo),CBN_SELCHANGE),(LPARAM)combo);
            }
            else
            {
                int index=(int)SendMessageA(combo,CB_GETCURSEL,0,0);char text[128]={};
                if (index<0 || SendMessageA(combo,CB_GETLBTEXTLEN,index,0)>=(LRESULT)sizeof(text)) return 1;
                SendMessageA(combo,WM_GETTEXT,sizeof(text),(LPARAM)text);printf("VALUE|%s\n",text);
            }
        }
        else if (!strcmp(command,"wait_text"))
        {
            bool found=false;
            for (unsigned attempt=0;!found && attempt<400;++attempt)
            {
                UiTarget target={process,"nanovaPauseMenu",0};EnumWindows(find_ui,(LPARAM)&target);
                found=target.window && label && FindWindowExA(target.window,0,"STATIC",label);
                if (!found) Sleep(5);
            }
            if (!found) return 1;
        }
        else if (strcmp(command,"wait")) return 2;
        printf("OK\n");fflush(stdout);
    }
    return 0;
}

int main(int argc,char** argv)
{
    if (argc==3 && !strcmp(argv[1],"--ui"))
    { char* end=0;unsigned long process=strtoul(argv[2],&end,10);if (*end || !process) return 2;return ui_input((DWORD)process); }
    if (argc>=2 && (!strcmp(argv[1],"--menu-click") || !strcmp(argv[1],"--menu-close"))) return menu_input(argc,argv);
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
                for (WPARAM code=VK_F5;code<=VK_F10;++code)
                    if (!key(window,code,true) || !PostMessageA(window,WM_KILLFOCUS,0,0)
                        || !key(window,code,true,true) || !key(window,code,false) || !press(window,code)) return 1;
                if (!press(window,VK_F1) || !press(window,VK_F1)
                    || !press(window,VK_F2) || !press(window,VK_F2)) return 1;
            }
            else if (!strcmp(argv[i],"10")) { if (!press(window,VK_F10)) return 1; }
            else if (strlen(argv[i])==1 && argv[i][0]>='5' && argv[i][0]<='9')
            { if (!press(window,VK_F5+argv[i][0]-'5')) return 1; }
            else { fprintf(stderr,"Expected rendering key 5..10, pause, buffers or repeat.\n");return 2; }
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
