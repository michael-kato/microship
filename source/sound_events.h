#ifndef NANOVA_SOUND_EVENTS_H
#define NANOVA_SOUND_EVENTS_H
#include "types.h"
#include <stdint.h>
enum SoundKind { SOUND_ORDER,SOUND_GROW,SOUND_MATURE,SOUND_BIRTH,SOUND_SHOT,SOUND_HIT,SOUND_DEATH,SOUND_COLONIZE,SOUND_HEAL,SOUND_WIN,SOUND_LOSE,SOUND_BREAK,SOUND_ALERT,SOUND_TRANSMISSION,SOUND_COUNT };
enum { MAX_SOUND_EVENTS=64 };
struct SoundEvent { Vec3 position;SoundKind kind;uint32_t faction; };
struct SoundEvents { SoundEvent pending[MAX_SOUND_EVENTS];unsigned count,dropped,totals[SOUND_COUNT]; };
inline const char* sound_name(unsigned i)
{
    static const char* names[SOUND_COUNT]={"order","growth","mature","birth","shot","hit","death","colonize","heal","win","lose","break","alert","transmission"};
    return i<SOUND_COUNT ? names[i] : "invalid";
}
#endif
