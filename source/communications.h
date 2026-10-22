#ifndef NANOVA_COMMUNICATIONS_H
#define NANOVA_COMMUNICATIONS_H
#include "content.h"
#include <stdio.h>
struct Simulation;
enum { DIALOGUE_HISTORY_CAPACITY=MAX_FACTIONS*(FACTION_MILESTONES+2) };
enum DialogueTrigger { DIALOGUE_SHIPS=1,DIALOGUE_CELLS,DIALOGUE_TERRITORY };
struct CommunicationSettings
{
    bool enabled;
    float territory_clearance,encounter_radius,dialogue_seconds,dialogue_gap,focus_distance;
};
struct TerritoryAlert { uint32_t factions,sequence,tick;bool pending; };
struct DialogueEvent { Faction faction;uint32_t trigger,cells,tick; };
struct CommunicationState
{
    TerritoryAlert alerts[CELL_STORAGE_CAPACITY];
    Faction owners[CELL_STORAGE_CAPACITY];uint32_t seen[CELL_STORAGE_CAPACITY];
    uint32_t milestones[MAX_FACTIONS];bool ships[MAX_FACTIONS],cells[MAX_FACTIONS];
    DialogueEvent dialogue[DIALOGUE_HISTORY_CAPACITY];
    uint32_t dialogue_count,dialogue_cursor,alert_sequence;
    double dialogue_until,dialogue_ready;
};
void communications_update(Simulation*);
int32_t communications_alert(const Simulation*,uint32_t row);
bool communications_acknowledge(Simulation*,uint32_t cell);
const DialogueEvent* communications_speaking(const Simulation*);
const char* communications_line(const Simulation*,const DialogueEvent&);
void communications_write_state(const Simulation*,FILE*);
#endif
