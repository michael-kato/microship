#include "start_menu.h"
#include "asset_json.h"
#include "window.h"
#include <stdio.h>
#include <string.h>

struct MenuChoice { char label[64],description[160],faction[ASSET_NAME_CAPACITY];Color color;RECT bounds; };
struct StartMenu
{
    char title[80],prompt[160];
    uint32_t width,height,count,selected;
    Color background,text;
    MenuChoice choices[8];
    bool open;
};
static COLORREF menu_color(Color c) { return RGB((BYTE)(255*c.r),(BYTE)(255*c.g),(BYTE)(255*c.b)); }
static void menu_text(HDC dc,const char* text,RECT bounds,int height,Color color)
{
    HFONT font=CreateFontA(height,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,"Segoe UI");
    HGDIOBJ old=SelectObject(dc,font);SetTextColor(dc,menu_color(color));
    DrawTextA(dc,text,-1,&bounds,DT_CENTER|DT_VCENTER|DT_WORDBREAK);
    SelectObject(dc,old);DeleteObject(font);
}
static void menu_paint(HWND handle,const StartMenu* menu)
{
    PAINTSTRUCT paint;HDC dc=BeginPaint(handle,&paint);RECT client;GetClientRect(handle,&client);
    HBRUSH brush=CreateSolidBrush(menu_color(menu->background));FillRect(dc,&client,brush);DeleteObject(brush);SetBkMode(dc,TRANSPARENT);
    RECT title={0,100,(LONG)menu->width,230},prompt={0,235,(LONG)menu->width,300};
    menu_text(dc,menu->title,title,64,menu->text);menu_text(dc,menu->prompt,prompt,26,menu->text);
    for (uint32_t i=0;i<menu->count;++i)
    {
        const MenuChoice& choice=menu->choices[i];RECT card=choice.bounds;
        brush=CreateSolidBrush(menu_color(choice.color));FrameRect(dc,&card,brush);DeleteObject(brush);
        RECT label=card;label.top+=45;label.bottom=label.top+60;
        menu_text(dc,choice.label,label,36,choice.color);
        RECT description=card;description.left+=24;description.right-=24;description.top+=140;description.bottom-=45;
        menu_text(dc,choice.description,description,24,menu->text);
        char key[16];snprintf(key,sizeof(key),"%u",i+1);RECT number=card;number.top=number.bottom-55;number.bottom-=15;
        menu_text(dc,key,number,24,choice.color);
    }
    EndPaint(handle,&paint);
}
static void menu_select(StartMenu* menu,uint32_t choice)
{
    if (choice>=menu->count) return;
    menu->selected=choice+1;menu->open=false;
    printf("Start menu selection: %s.\n",menu->choices[choice].label);
}
static LRESULT CALLBACK menu_message(HWND handle,UINT message,WPARAM wparam,LPARAM lparam)
{
    StartMenu* menu=(StartMenu*)GetWindowLongPtrA(handle,GWLP_USERDATA);
    if (message==WM_NCCREATE)
    { menu=(StartMenu*)((CREATESTRUCTA*)lparam)->lpCreateParams;SetWindowLongPtrA(handle,GWLP_USERDATA,(LONG_PTR)menu); }
    if (menu && message==WM_PAINT) { menu_paint(handle,menu);return 0; }
    if (menu && (message==WM_CLOSE || (message==WM_KEYDOWN && wparam==VK_ESCAPE))) { menu->open=false;return 0; }
    if (menu && message==WM_KEYDOWN && wparam>='1' && wparam<'1'+menu->count)
    { menu_select(menu,(uint32_t)(wparam-'1'));return 0; }
    if (menu && message==WM_LBUTTONUP)
    {
        POINT point={(short)LOWORD(lparam),(short)HIWORD(lparam)};
        for (uint32_t i=0;i<menu->count;++i) if (PtInRect(&menu->choices[i].bounds,point)) menu_select(menu,i);
        return 0;
    }
    return DefWindowProcA(handle,message,wparam,lparam);
}
static bool menu_load(StartMenu* menu,const char* path)
{
    JsonDocument document;if (!json_load(&document,path)) return false;
    Json root=json_root(&document);json_object(root,"$schema|title|prompt|width|height|background|text|choices");
    json_string(json_get(root,"title"),menu->title,sizeof(menu->title));json_string(json_get(root,"prompt"),menu->prompt,sizeof(menu->prompt));
    menu->width=json_uint(json_get(root,"width"),1920,3840);menu->height=json_uint(json_get(root,"height"),1080,2160);
    menu->background=json_color(json_get(root,"background"));menu->text=json_color(json_get(root,"text"));
    JsonArray array=json_array(json_get(root,"choices"),8);Json item;
    while (json_next(&array,&item))
    {
        MenuChoice* choice=&menu->choices[menu->count++];json_object(item,"label|description|faction|color");
        json_string(json_get(item,"label"),choice->label,sizeof(choice->label));json_string(json_get(item,"description"),choice->description,sizeof(choice->description));
        json_string(json_get(item,"faction"),choice->faction,sizeof(choice->faction));choice->color=json_color(json_get(item,"color"));
    }
    if (!menu->count) json_error(root,"menu needs at least one choice");
    return document.valid;
}
int start_menu(const char* path,char* faction,size_t capacity,uint32_t selection)
{
    StartMenu menu={};if (!menu_load(&menu,path)) return -1;
    if (selection)
    {
        if (selection>menu.count) { fprintf(stderr,"Start menu selection is out of range.\n");return -1; }
        menu_select(&menu,selection-1);
    }
    else
    {
        if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) && GetLastError()!=ERROR_ACCESS_DENIED) return -1;
        HINSTANCE instance=GetModuleHandleA(0);WNDCLASSA klass={};klass.lpfnWndProc=menu_message;klass.hInstance=instance;
        klass.hCursor=LoadCursorA(0,IDC_ARROW);klass.lpszClassName="nanovoreStartMenu";
        if (!RegisterClassA(&klass) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) return -1;
        DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;RECT extent={0,0,(LONG)menu.width,(LONG)menu.height};
        if (!AdjustWindowRectExForDpi(&extent,style,FALSE,0,GetDpiForSystem())) return -1;
        int margin=60,gap=24,width=((int)menu.width-margin*2-gap*((int)menu.count-1))/(int)menu.count;
        for (uint32_t i=0;i<menu.count;++i) menu.choices[i].bounds=RECT{margin+(int)i*(width+gap),380,margin+(int)i*(width+gap)+width,760};
        menu.open=true;
        HWND handle=CreateWindowExA(0,klass.lpszClassName,"nanovore - Choose faction",style,CW_USEDEFAULT,CW_USEDEFAULT,
            extent.right-extent.left,extent.bottom-extent.top,0,0,instance,&menu);
        if (!handle) return -1;
        ShowWindow(handle,SW_SHOW);UpdateWindow(handle);printf("Start menu ready.\n");fflush(stdout);
        MSG message;
        while (menu.open)
        {
            int result=GetMessageA(&message,0,0,0);if (result<=0) { DestroyWindow(handle);return result<0 ? -1 : 0; }
            TranslateMessage(&message);DispatchMessageA(&message);
        }
        DestroyWindow(handle);
    }
    if (!menu.selected) return 0;
    const char* selected=menu.choices[menu.selected-1].faction;
    if (strlen(selected)>=capacity) return -1;
    strcpy(faction,selected);return 1;
}
