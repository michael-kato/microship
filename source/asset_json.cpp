#include "asset_json.h"
#include "../third_party/json/json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <cmath>
#include <windows.h>
#include "asset_limits.h"

static const int MAX_JSON_FILE_BYTES = 1024*1024;
static const int MAX_JSON_DEPTH = 32;
static const int MAX_JSON_OBJECT_FIELDS = 64;
static const int JSON_ERROR_TEXT_CAPACITY = 160;

JsonDocument::JsonDocument() : root(0),path(0),valid(false) {}
JsonDocument::~JsonDocument() { free(root); }
Json json_root(JsonDocument* d) { return Json{d,d->root}; }
bool json_present(Json j) { return j.value!=0; }

bool json_error(Json j,const char* message)
{
    if (j.document->valid)
    {
        const json_value_ex_s* v=(const json_value_ex_s*)j.value;
        fprintf(stderr,"%s:%u:%u: %s\n",j.document->path,v ? (unsigned)v->line_no : 1,
            v ? (unsigned)v->row_no : 1,message);
    }
    j.document->valid=false;return false;
}

// Check depth before the recursive library parser sees the input.
static bool bounded_text(const char* text,size_t size)
{
    unsigned depth=0;bool string=false,escape=false;
    for (size_t i=0;i<size;++i)
    {
        unsigned char c=(unsigned char)text[i];if (!c) return false;
        if (string)
        {
            if (escape) escape=false;
            else if (c=='\\') escape=true;
            else if (c=='"') string=false;
        }
        else if (c=='"') string=true;
        else if (c=='{' || c=='[') { if (++depth>MAX_JSON_DEPTH) return false; }
        else if (c=='}' || c==']') { if (!depth) return false;--depth; }
    }
    return MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text,(int)size,0,0)>0;
}

static bool check_tree(Json j)
{
    json_value_s* v=(json_value_s*)j.value;
    if (v->type==json_type_string)
    {
        json_string_s* s=(json_string_s*)v->payload;
        if (strlen(s->string)!=s->string_size) return json_error(j,"NUL in JSON string");
    }
    if (v->type==json_type_object)
    {
        json_object_s* o=(json_object_s*)v->payload;
        if (o->length>MAX_JSON_OBJECT_FIELDS) return json_error(j,"object exceeds 64 fields");
        for (json_object_element_s* e=o->start;e;e=e->next)
        {
            if (strlen(e->name->string)!=e->name->string_size) return json_error(j,"NUL in JSON key");
            for (json_object_element_s* p=o->start;p!=e;p=p->next)
                if (!strcmp(p->name->string,e->name->string)) return json_error(Json{j.document,e->value},"duplicate JSON key");
            if (!check_tree(Json{j.document,e->value})) return false;
        }
    }
    if (v->type==json_type_array)
        for (json_array_element_s* e=((json_array_s*)v->payload)->start;e;e=e->next)
            if (!check_tree(Json{j.document,e->value})) return false;
    return true;
}

bool json_load(JsonDocument* d,const char* path)
{
    d->path=path;d->valid=true;
    FILE* f=fopen(path,"rb");if (!f) return json_error(json_root(d),"cannot open JSON asset");
    bool ok=fseek(f,0,SEEK_END)==0;long size=ok ? ftell(f) : -1;
    ok=ok && size>0 && size<=MAX_JSON_FILE_BYTES && fseek(f,0,SEEK_SET)==0;
    char* text=ok ? (char*)malloc((size_t)size) : 0;
    ok=ok && text && fread(text,1,(size_t)size,f)==(size_t)size;
    if (fclose(f)) ok=false;
    if (!ok) { free(text);return json_error(json_root(d),"cannot read JSON asset (limit: 1 MiB)"); }
    if (!bounded_text(text,(size_t)size)) { free(text);return json_error(json_root(d),"invalid UTF-8, NUL byte, or JSON depth exceeds 32"); }
    json_parse_result_s result={};
    d->root=json_parse_ex(text,(size_t)size,json_parse_flags_allow_location_information,0,0,&result);
    free(text);
    if (!d->root)
    {
        fprintf(stderr,"%s:%u:%u: invalid JSON (error %u)\n",path,(unsigned)result.error_line_no,
            (unsigned)result.error_row_no,(unsigned)result.error);
        d->valid=false;return false;
    }
    return check_tree(json_root(d));
}

static bool allowed(const char* keys,const char* key)
{
    size_t length=strlen(key);
    for (const char* p=keys;*p;)
    {
        const char* end=strchr(p,'|');size_t n=end ? (size_t)(end-p) : strlen(p);
        if (n==length && !memcmp(p,key,n)) return true;
        p=end ? end+1 : p+n;
    }
    return false;
}
bool json_object(Json j,const char* keys)
{
    if (!j.document->valid) return false;
    json_value_s* v=(json_value_s*)j.value;
    if (!v || v->type!=json_type_object) return json_error(j,"expected an object");
    for (json_object_element_s* e=((json_object_s*)v->payload)->start;e;e=e->next)
        if (!strcmp(e->name->string,"$schema") && e->value->type!=json_type_string)
            return json_error(Json{j.document,e->value},"$schema must be a string");
        else if (!allowed(keys,e->name->string))
        {
            char error[JSON_ERROR_TEXT_CAPACITY];snprintf(error,sizeof(error),"unknown field: %.120s",e->name->string);
            return json_error(Json{j.document,e->value},error);
        }
    return true;
}
Json json_find(Json j,const char* key)
{
    json_value_s* v=(json_value_s*)j.value;
    if (v && v->type==json_type_object)
        for (json_object_element_s* e=((json_object_s*)v->payload)->start;e;e=e->next)
            if (!strcmp(e->name->string,key)) return Json{j.document,e->value};
    return Json{j.document,0};
}
Json json_get(Json j,const char* key)
{
    Json value=json_find(j,key);
    if (!value.value)
    {
        char error[JSON_ERROR_TEXT_CAPACITY];snprintf(error,sizeof(error),"missing field: %.120s",key);json_error(j,error);
    }
    return value;
}
JsonArray json_array(Json j,uint32_t capacity)
{
    if (!j.document->valid) return JsonArray{j.document,0};
    json_value_s* v=(json_value_s*)j.value;
    if (!v || v->type!=json_type_array) { json_error(j,"expected an array");return JsonArray{j.document,0}; }
    json_array_s* a=(json_array_s*)v->payload;
    if (a->length>capacity) { json_error(j,"array exceeds storage capacity");return JsonArray{j.document,0}; }
    return JsonArray{j.document,a->start};
}
bool json_next(JsonArray* a,Json* j)
{
    if (!a->next || !a->document->valid) return false;
    json_array_element_s* e=(json_array_element_s*)a->next;
    *j=Json{a->document,e->value};a->next=e->next;return true;
}
static double number(Json j,double minimum,double maximum)
{
    if (!j.document->valid) return minimum;
    json_value_s* v=(json_value_s*)j.value;
    if (!v || v->type!=json_type_number) { json_error(j,"expected a number");return minimum; }
    const char* text=((json_number_s*)v->payload)->number;char* end=0;errno=0;double n=strtod(text,&end);
    if (*end || errno==ERANGE || !std::isfinite(n) || n<minimum || n>maximum)
    { json_error(j,"number is outside the permitted range");return minimum; }
    return n;
}
float json_float(Json j,double minimum,double maximum)
{
    double n=number(j,minimum,maximum);float value=(float)n;
    if (!std::isfinite(value) || (n!=0 && value==0)) json_error(j,"number does not fit a float");
    return value;
}
float json_positive(Json j,double maximum)
{
    float value=json_float(j,0,maximum);
    if (value<=0) json_error(j,"number must be positive");
    return value;
}
uint32_t json_uint(Json j,uint32_t minimum,uint32_t maximum)
{
    double n=number(j,minimum,maximum);
    if (n!=std::floor(n)) { json_error(j,"expected an integer");return minimum; }
    return (uint32_t)n;
}
bool json_bool(Json j)
{
    if (!j.document->valid) return false;
    json_value_s* v=(json_value_s*)j.value;
    if (!v || (v->type!=json_type_true && v->type!=json_type_false)) return json_error(j,"expected true or false");
    return v->type==json_type_true;
}
bool json_string(Json j,char* output,size_t capacity)
{
    if (!j.document->valid) return false;
    json_value_s* v=(json_value_s*)j.value;
    if (!v || v->type!=json_type_string) return json_error(j,"expected a string");
    json_string_s* s=(json_string_s*)v->payload;
    if (!s->string_size || s->string_size>=capacity) return json_error(j,"empty string or string exceeds storage capacity");
    memcpy(output,s->string,s->string_size+1);return true;
}
bool json_path(Json j,const char* source,char* output,size_t capacity)
{
    char value[ASSET_PATH_CAPACITY]={};if (!json_string(j,value,sizeof(value))) return false;
    if (value[0]=='/' || value[0]=='\\' || strchr(value,':')) return json_error(j,"asset path must be relative to this file");
    const char* slash=strrchr(source,'/');const char* backslash=strrchr(source,'\\');
    if (backslash && (!slash || backslash>slash)) slash=backslash;
    size_t directory=slash ? (size_t)(slash-source+1) : 0;
    if (directory+strlen(value)>=capacity) return json_error(j,"resolved asset path exceeds storage capacity");
    memcpy(output,source,directory);strcpy(output+directory,value);return true;
}
Vec3 json_vector(Json j,double minimum,double maximum)
{
    JsonArray a=json_array(j,3);float v[3]={};Json item={};uint32_t count=0;
    while (json_next(&a,&item)) v[count++]=json_float(item,minimum,maximum);
    if (count!=3) json_error(j,"expected exactly three vector components");
    return Vec3{v[0],v[1],v[2]};
}
Color json_color(Json j,double maximum)
{
    Vec3 v=json_vector(j,0,maximum);return Color{v.x,v.y,v.z};
}
