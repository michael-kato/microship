#ifndef NANOVORE_ASSET_JSON_H
#define NANOVORE_ASSET_JSON_H
#include <stdint.h>
#include <stddef.h>
#include "types.h"
#include "asset_limits.h"

// Startup-only document ownership. No JSON nodes survive loading.
struct JsonDocument
{
    void* root;
    const char* path;
    bool valid;
    JsonDocument();
    ~JsonDocument();
private:
    JsonDocument(const JsonDocument&);
    JsonDocument& operator=(const JsonDocument&);
};
struct Json { JsonDocument* document; void* value; };
struct JsonArray { JsonDocument* document; void* next; };
bool json_load(JsonDocument*,const char* path);
Json json_root(JsonDocument*);
Json json_get(Json,const char* key);
Json json_find(Json,const char* key);
bool json_present(Json);
bool json_object(Json,const char* keys);
JsonArray json_array(Json,uint32_t capacity);
bool json_next(JsonArray*,Json*);
bool json_error(Json,const char* message);
float json_float(Json,double minimum,double maximum);
float json_positive(Json,double maximum);
uint32_t json_uint(Json,uint32_t minimum,uint32_t maximum);
bool json_bool(Json);
bool json_string(Json,char* output,size_t capacity);
bool json_path(Json,const char* source,char* output,size_t capacity=ASSET_PATH_CAPACITY);
Vec3 json_vector(Json,double minimum,double maximum);
Color json_color(Json,double maximum=1);
#endif
