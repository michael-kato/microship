#include "pause_menu.h"
#include "window.h"
#include <stdio.h>
#include <string.h>
#include <cmath>

enum { MENU_WIDTH=900,MENU_HEIGHT=700,MENU_ROW_HEIGHT=32,MENU_CONTENT_TOP=122,
    MENU_LABEL_LEFT=214,MENU_VALUE_LEFT=615,MENU_VALUE_WIDTH=240,
    MENU_FIELD_ID=1000,MENU_PAGE_ID=100,MENU_STATUS_ID=30,
    MENU_RESUME_ID=10,MENU_SETTINGS_ID=11,MENU_MAIN_ID=12,MENU_QUIT_ID=13,
    MENU_APPLY_ID=14,MENU_CANCEL_ID=15,MENU_DEFAULTS_ID=16,MENU_CONFIRM_ID=17,
    MENU_ROOT=-1,MENU_CONFIRM_MAIN=-2,MENU_CONFIRM_QUIT=-3 };
static const COLORREF MENU_BACKGROUND=RGB(18,22,27),MENU_TEXT=RGB(230,234,237);
struct PauseMenu
{
    PlayerSettings* draft;
    PlayerSettings original;
    const PlayerSettings* defaults;
    const char* status;
    HWND handle;
    HFONT font,title_font;
    HBRUSH background;
    int page;
    bool open,building;
    float scale;
    PauseAction action;
};
static HWND control(PauseMenu* menu,const char* klass,const char* text,DWORD style,
    int x,int y,int width,int height,int id)
{
    HWND child=CreateWindowExA(0,klass,text,WS_CHILD|WS_VISIBLE|style,(int)(x*menu->scale),(int)(y*menu->scale),(int)(width*menu->scale),(int)(height*menu->scale),
        menu->handle,(HMENU)(INT_PTR)id,GetModuleHandleA(0),0);
    if (!child) { menu->action=PAUSE_ERROR;menu->open=false;return 0; }
    SendMessageA(child,WM_SETFONT,(WPARAM)menu->font,TRUE);return child;
}
static HWND button(PauseMenu* menu,const char* label,int x,int y,int width,int id,bool primary=false)
{ return control(menu,"BUTTON",label,WS_TABSTOP|(primary ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON),x,y,width,38,id); }
static void label(PauseMenu* menu,const char* text,int x,int y,int width,int height,int id=0)
{ control(menu,"STATIC",text,SS_LEFT,x,y,width,height,id); }
static const char* page_title(int page)
{
    static const char* titles[]={"Display","Lighting","Volume","Effects","Audio","Controls"};
    if (page>=0 && page<SETTINGS_PAGE_COUNT) return titles[page];
    if (page==MENU_CONFIRM_MAIN) return "Return to Main Menu?";
    if (page==MENU_CONFIRM_QUIT) return "Quit to Desktop?";
    return "Paused";
}
static void value_label(const SettingField& field,double value,char* text,size_t capacity)
{
    if (field.type==SETTING_BOOL || (field.type==SETTING_UINT && field.maximum==1))
        snprintf(text,capacity,"%s",value ? "On" : "Off");
    else if (field.page==SETTINGS_AUDIO) snprintf(text,capacity,"%.0f%%",value*100);
    else snprintf(text,capacity,"%.4g",value);
}
static void settings_rows(PauseMenu* menu)
{
    for (int page=0;page<SETTINGS_PAGE_COUNT;++page)
        button(menu,page_title(page),28,MENU_CONTENT_TOP+page*48,158,MENU_PAGE_ID+page);
    size_t count;const SettingField* fields=player_setting_fields(&count);int row=0,rows=0;
    for (size_t i=0;i<count;++i) if (fields[i].page==menu->page) ++rows;
    int row_height=rows>12 ? 384/rows : MENU_ROW_HEIGHT;
    for (size_t i=0;i<count;++i)
    {
        const SettingField& field=fields[i];if (field.page!=menu->page) continue;
        int y=MENU_CONTENT_TOP+row++*row_height;
        label(menu,field.label,MENU_LABEL_LEFT,y+3,MENU_VALUE_LEFT-MENU_LABEL_LEFT-12,24);
        HWND combo=control(menu,"COMBOBOX","",WS_TABSTOP|WS_VSCROLL|CBS_DROPDOWNLIST,
            MENU_VALUE_LEFT,y,MENU_VALUE_WIDTH,300,MENU_FIELD_ID+(int)i);
        double current=player_setting_value(menu->draft,field);int selected=-1;
        for (size_t choice=0;choice<field.choice_count;++choice)
        {
            char text[64];value_label(field,field.choices[choice],text,sizeof(text));
            SendMessageA(combo,CB_ADDSTRING,0,(LPARAM)text);
            if (std::fabs(current-field.choices[choice])<.00001) selected=(int)choice;
        }
        if (selected<0)
        {
            char text[64];value_label(field,current,text,sizeof(text));
            selected=(int)SendMessageA(combo,CB_ADDSTRING,0,(LPARAM)text);
        }
        SendMessageA(combo,CB_SETCURSEL,selected,0);
    }
    const char* hint="Changes take effect only after Apply. The match stays paused.";
    if (menu->page==SETTINGS_DISPLAY)
        hint="Window size applies in windowed mode. Borderless uses the monitor's native pixels.\r\nApply changes the display immediately.";
    if (menu->page==SETTINGS_LIGHTING)
        hint="Higher sample and grid counts increase GPU work and memory use.\r\nSubsurface samples must not exceed the film sample count.";
    if (menu->page==SETTINGS_VOLUME)
        hint="Gauss-Legendre needs 2 or 3 steps. Spatial filtering needs 1 volume ray.\r\nThese controls affect current-frame scattering.";
    if (menu->page==SETTINGS_AUDIO)
        hint="The game has sound effects. A volume of 0% mutes that channel.\r\nThe pause menu mutes game sound.";
    if (menu->page==SETTINGS_CONTROLS)
        hint="WASD / arrows: pan   Middle drag: pan   Wheel: zoom   Home: focus\r\n1 / 2 / 3: select groups   F / B: select on screen   Right click: order\r\nSpace: pause   Esc: pause menu   R: restart   F1: statistics   F2: buffers";
    label(menu,hint,MENU_LABEL_LEFT,516,640,72);
    label(menu,menu->status ? menu->status : "",MENU_LABEL_LEFT,590,640,48,MENU_STATUS_ID);
    button(menu,"Restore Defaults",MENU_LABEL_LEFT,646,185,MENU_DEFAULTS_ID);
    button(menu,"Cancel",592,646,120,MENU_CANCEL_ID);
    button(menu,"Apply",726,646,130,MENU_APPLY_ID,true);
}
static void build_page(PauseMenu* menu)
{
    menu->building=true;SendMessageA(menu->handle,WM_SETREDRAW,FALSE,0);
    HWND child=GetWindow(menu->handle,GW_CHILD);
    while (child) { HWND next=GetWindow(child,GW_HWNDNEXT);DestroyWindow(child);child=next; }
    label(menu,"NANOVORE",28,22,500,28);
    HWND title=control(menu,"STATIC",page_title(menu->page),SS_LEFT,28,55,830,52,0);
    SendMessageA(title,WM_SETFONT,(WPARAM)menu->title_font,TRUE);
    if (menu->page>=0) settings_rows(menu);
    else if (menu->page==MENU_ROOT)
    {
        label(menu,"The match is paused.",250,134,500,40);
        button(menu,"Resume",250,210,400,MENU_RESUME_ID,true);
        button(menu,"Settings",250,266,400,MENU_SETTINGS_ID);
        button(menu,"Main Menu",250,322,400,MENU_MAIN_ID);
        button(menu,"Quit to Desktop",250,378,400,MENU_QUIT_ID);
        label(menu,"Esc: resume   Tab: move focus   Enter: select",250,454,520,50);
        SetFocus(GetDlgItem(menu->handle,MENU_RESUME_ID));
    }
    else
    {
        label(menu,"This ends the current match. Match progress will be lost.",180,190,580,70);
        button(menu,"Cancel",230,340,190,MENU_CANCEL_ID,true);
        button(menu,menu->page==MENU_CONFIRM_MAIN ? "Return to Main Menu" : "Quit to Desktop",440,340,240,MENU_CONFIRM_ID);
        SetFocus(GetDlgItem(menu->handle,MENU_CANCEL_ID));
    }
    menu->building=false;SendMessageA(menu->handle,WM_SETREDRAW,TRUE,0);
    RedrawWindow(menu->handle,0,0,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
}
static void choose(PauseMenu* menu,int id)
{
    if (id>=MENU_PAGE_ID && id<MENU_PAGE_ID+SETTINGS_PAGE_COUNT)
    { menu->page=id-MENU_PAGE_ID;build_page(menu);return; }
    switch (id)
    {
        case MENU_SETTINGS_ID:menu->page=SETTINGS_DISPLAY;build_page(menu);break;
        case MENU_MAIN_ID:menu->page=MENU_CONFIRM_MAIN;build_page(menu);break;
        case MENU_QUIT_ID:menu->page=MENU_CONFIRM_QUIT;build_page(menu);break;
        case MENU_RESUME_ID:menu->action=PAUSE_RESUME;menu->open=false;break;
        case MENU_APPLY_ID:
        {
            const char* error=player_settings_error(menu->draft);
            if (error) { SetWindowTextA(GetDlgItem(menu->handle,MENU_STATUS_ID),error);break; }
            menu->action=PAUSE_APPLY;menu->open=false;break;
        }
        case MENU_DEFAULTS_ID:*menu->draft=*menu->defaults;menu->status="Changes not applied.";build_page(menu);break;
        case MENU_CANCEL_ID:
            if (menu->page>=0) { *menu->draft=menu->original;menu->status=""; }
            menu->page=MENU_ROOT;build_page(menu);
            break;
        case MENU_CONFIRM_ID:
            menu->action=menu->page==MENU_CONFIRM_MAIN ? PAUSE_MAIN_MENU : PAUSE_QUIT;menu->open=false;break;
    }
    // GetMessage dispatches sent commands without returning. Wake the posted
    // queue when a command ends the modal loop, including accessibility input.
    if (!menu->open) PostMessageA(menu->handle,WM_NULL,0,0);
}
static LRESULT CALLBACK menu_message(HWND handle,UINT message,WPARAM wparam,LPARAM lparam)
{
    PauseMenu* menu=(PauseMenu*)GetWindowLongPtrA(handle,GWLP_USERDATA);
    if (message==WM_NCCREATE)
    { menu=(PauseMenu*)((CREATESTRUCTA*)lparam)->lpCreateParams;menu->handle=handle;SetWindowLongPtrA(handle,GWLP_USERDATA,(LONG_PTR)menu); }
    if (!menu) return DefWindowProcA(handle,message,wparam,lparam);
    if (message==WM_CLOSE)
    { choose(menu,menu->page==MENU_ROOT ? MENU_RESUME_ID : MENU_CANCEL_ID);return 0; }
    if (message==WM_DESTROY) { menu->open=false;return 0; }
    if (message==WM_ERASEBKGND) return 1;
    if (message==WM_PAINT)
    {
        PAINTSTRUCT paint;HDC dc=BeginPaint(handle,&paint);RECT bounds;GetClientRect(handle,&bounds);
        FillRect(dc,&bounds,menu->background);EndPaint(handle,&paint);return 0;
    }
    if (message==WM_CTLCOLORSTATIC)
    { HDC dc=(HDC)wparam;SetBkColor(dc,MENU_BACKGROUND);SetTextColor(dc,MENU_TEXT);return (LRESULT)menu->background; }
    if (message==WM_COMMAND)
    {
        if (menu->building) return 0;
        int id=LOWORD(wparam);
        if (id>=MENU_FIELD_ID && HIWORD(wparam)==CBN_SELCHANGE)
        {
            size_t count;const SettingField* fields=player_setting_fields(&count);size_t index=(size_t)(id-MENU_FIELD_ID);
            if (index<count)
            {
                int choice=(int)SendMessageA((HWND)lparam,CB_GETCURSEL,0,0);
                if (choice>=0 && (size_t)choice<fields[index].choice_count)
                {
                    player_setting_set(menu->draft,fields[index],fields[index].choices[choice]);
                    menu->status="Changes not applied.";
                    SetWindowTextA(GetDlgItem(handle,MENU_STATUS_ID),menu->status);
                }
            }
        }
        else if (HIWORD(wparam)==BN_CLICKED) choose(menu,id);
        return 0;
    }
    return DefWindowProcA(handle,message,wparam,lparam);
}
static bool show(PauseMenu* menu,Window* owner)
{
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) && GetLastError()!=ERROR_ACCESS_DENIED) return false;
    HINSTANCE instance=GetModuleHandleA(0);WNDCLASSA klass={};klass.hInstance=instance;klass.lpfnWndProc=menu_message;
    klass.hCursor=LoadCursorA(0,IDC_ARROW);klass.lpszClassName="nanovorePauseMenu";
    if (!RegisterClassA(&klass) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) return false;
    RECT area;SystemParametersInfoA(SPI_GETWORKAREA,0,&area,0);
    menu->scale=(float)fmin(1.0,fmin((area.right-area.left)/(double)(MENU_WIDTH+24),
        (area.bottom-area.top)/(double)(MENU_HEIGHT+48)));
    menu->background=CreateSolidBrush(MENU_BACKGROUND);
    menu->font=CreateFontA((int)(19*menu->scale),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,"Tahoma");
    menu->title_font=CreateFontA((int)(36*menu->scale),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,"Tahoma");
    DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU;RECT extent={0,0,(LONG)(MENU_WIDTH*menu->scale),(LONG)(MENU_HEIGHT*menu->scale)};
    if (!AdjustWindowRectExForDpi(&extent,style,FALSE,WS_EX_CONTROLPARENT,GetDpiForSystem())) return false;
    HWND parent=owner ? owner->handle : 0;
    int width=extent.right-extent.left,height=extent.bottom-extent.top;
    int x=area.left+((area.right-area.left)-width)/2,y=area.top+((area.bottom-area.top)-height)/2;
    menu->open=true;
    HWND handle=CreateWindowExA(WS_EX_CONTROLPARENT,klass.lpszClassName,"Nanovore - Pause",style,x,y,width,height,parent,0,instance,menu);
    if (!handle) return false;
    if (owner) { window_suspend_input(owner,true);EnableWindow(parent,FALSE); }
    build_page(menu);ShowWindow(handle,SW_SHOW);UpdateWindow(handle);
    printf("Pause menu ready: %s.\n",page_title(menu->page));fflush(stdout);
    MSG message;bool success=true;
    while (menu->open)
    {
        int result=GetMessageA(&message,0,0,0);
        if (result<=0) { success=false;break; }
        if (message.message==WM_KEYDOWN && message.wParam==VK_ESCAPE)
        {
            HWND focus=GetFocus();
            if (SendMessageA(focus,CB_GETDROPPEDSTATE,0,0)>0)
            { SendMessageA(focus,CB_SHOWDROPDOWN,FALSE,0);continue; }
            if (!(message.lParam&((LPARAM)1<<30)))
            {
                choose(menu,menu->page==MENU_ROOT ? MENU_RESUME_ID : MENU_CANCEL_ID);
            }
            continue;
        }
        if (message.message==WM_KEYDOWN && message.wParam==VK_RETURN)
        {
            HWND focus=GetFocus();char klass_name[32]={};GetClassNameA(focus,klass_name,sizeof(klass_name));
            if (!strcmp(klass_name,"Button")) SendMessageA(focus,BM_CLICK,0,0);
            else if (SendMessageA(focus,CB_GETDROPPEDSTATE,0,0)<=0)
                choose(menu,menu->page>=0 ? MENU_APPLY_ID : menu->page==MENU_ROOT ? MENU_RESUME_ID : MENU_CANCEL_ID);
            else SendMessageA(focus,CB_SHOWDROPDOWN,FALSE,0);
            continue;
        }
        if (!IsDialogMessageA(handle,&message)) { TranslateMessage(&message);DispatchMessageA(&message); }
    }
    DestroyWindow(handle);
    if (owner) { EnableWindow(parent,TRUE);window_suspend_input(owner,false);SetActiveWindow(parent); }
    DeleteObject(menu->font);DeleteObject(menu->title_font);DeleteObject(menu->background);
    UnregisterClassA(klass.lpszClassName,instance);return success;
}
PauseAction pause_menu(Window* owner,PlayerSettings* draft,const PlayerSettings* defaults,int* page,const char* status)
{
    PauseMenu menu={};menu.draft=draft;menu.original=*draft;menu.defaults=defaults;menu.page=*page;menu.status=status;menu.action=PAUSE_RESUME;
    bool success=show(&menu,owner);*page=menu.page>=0 ? menu.page : MENU_ROOT;
    return success ? menu.action : PAUSE_ERROR;
}
