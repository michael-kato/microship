#include "communications.h"
#include "simulation.h"
#include "vector_math.h"
#include <assert.h>
#include <limits.h>

static bool near_units(const Simulation* s, Vec3 point, float radius,
                       uint32_t factions)
{
	uint16_t nearby[MAX_UNITS];
	uint32_t count =
	    factions == 1u << s->world.player_faction
	        ? simulation_query_player_units(s, point, radius, nearby)
	        : simulation_query_units(s, point, radius, factions, nearby);
	for (uint32_t i = 0; i < count; ++i)
	{
		const Unit& u = s->units[nearby[i]];
		Vec3 delta = subtract(u.position, point);
		if (u.health > 0 && (factions & (1u << u.faction)) &&
		    dot(delta, delta) <= radius * radius)
			return true;
	}
	return false;
}
static bool sees_player(const Simulation* s, Faction faction, bool cells)
{
	float reach = s->world.communications.encounter_radius;
	Faction player = s->world.player_faction;
	for (uint32_t i = 0; i < s->world.body_count; ++i)
	{
		const Colony& c = s->colonies[i];
		if (!c.active || c.faction != faction)
			continue;
		const Body& b = s->world.bodies[i];
		float radius = reach + b.radius;
		if (!cells && near_units(s, b.position, radius, 1u << player))
			return true;
		if (cells)
			for (uint32_t j = 0; j < s->world.body_count; ++j)
			{
				if (!s->colonies[j].active || s->colonies[j].faction != player)
					continue;
				Vec3 delta = subtract(b.position, s->world.bodies[j].position);
				float limit = radius + s->world.bodies[j].radius;
				if (dot(delta, delta) <= limit * limit)
					return true;
			}
	}
	if (cells)
	{
		for (uint32_t i = 0; i < s->world.body_count; ++i)
			if (s->colonies[i].active && s->colonies[i].faction == player &&
			    near_units(s, s->world.bodies[i].position,
			               reach + s->world.bodies[i].radius, 1u << faction))
				return true;
	}
	else
		for (uint32_t i = 0; i < MAX_UNITS; ++i)
		{
			const Unit& u = s->units[i];
			if (u.health <= 0 || u.faction != faction)
				continue;
			if (near_units(s, u.position, reach, 1u << player))
				return true;
		}
	return false;
}
static void enqueue(Simulation* s, Faction faction, uint32_t trigger,
                    uint32_t cells)
{
	CommunicationState& n = s->communications;
	// Each faction can add at most two contacts and nine milestones in a match.
	assert(n.dialogue_count < DIALOGUE_HISTORY_CAPACITY);
	if (n.dialogue_count == DIALOGUE_HISTORY_CAPACITY)
		return;
	n.dialogue[n.dialogue_count++] =
	    DialogueEvent{faction, trigger, cells, s->tick_count};
}
static void update_alerts(Simulation* s)
{
	CommunicationState& n = s->communications;
	Faction player = s->world.player_faction;
	for (uint32_t p = 0; p < s->world.body_count; ++p)
	{
		Faction owner = s->colonies[p].active ? s->colonies[p].faction : 0;
		if (owner != n.owners[p])
		{
			n.owners[p] = owner;
			n.seen[p] = 0;
		}
		if (owner != player)
			continue;
		const Body& b = s->world.bodies[p];
		float radius = b.radius + s->world.communications.territory_clearance;
		uint16_t nearby[MAX_UNITS];
		uint32_t count = simulation_query_units(s, b.position, radius,
		                                        ~(1u << player), nearby),
		         contacts = 0;
		for (uint32_t i = 0; i < count; ++i)
		{
			uint32_t slot = nearby[i];
			const Unit& u = s->units[slot];
			Vec3 delta = subtract(u.position, b.position);
			if (u.health > 0 && u.faction != player && s->visible_units[slot] &&
			    dot(delta, delta) <= radius * radius)
				contacts |= 1u << u.faction;
		}
		uint32_t fresh = contacts & ~n.seen[p];
		if (!fresh)
			continue;
		n.seen[p] |= fresh;
		TerritoryAlert& a = n.alerts[p];
		a.factions = contacts;
		a.sequence = ++n.alert_sequence;
		a.tick = s->tick_count;
		a.pending = true;
		simulation_emit_sound(s, SOUND_ALERT, b.position, player);
	}
}
void communications_update(Simulation* s)
{
	const CommunicationSettings& settings = s->world.communications;
	if (!settings.enabled)
		return;
	CommunicationState& n = s->communications;
	update_alerts(s);
	uint32_t controlled[MAX_FACTIONS] = {};
	for (uint32_t p = 0; p < s->world.body_count; ++p)
		if (s->colonies[p].active)
			++controlled[s->colonies[p].faction];
	for (uint32_t f = 1; f < MAX_FACTIONS; ++f)
	{
		if (f == s->world.player_faction ||
		    !(s->world.active_factions & (1u << f)) ||
		    !s->world.content.factions[f].dialogue.enabled)
			continue;
		if (!n.ships[f] && sees_player(s, f, false))
		{
			n.ships[f] = true;
			enqueue(s, f, DIALOGUE_SHIPS, 0);
		}
		if (!n.cells[f] && sees_player(s, f, true))
		{
			n.cells[f] = true;
			enqueue(s, f, DIALOGUE_CELLS, 0);
		}
		for (uint32_t k = 0; k < FACTION_MILESTONES; ++k)
			if (controlled[f] >= 10 * (k + 1) && !(n.milestones[f] & (1u << k)))
			{
				n.milestones[f] |= 1u << k;
				enqueue(s, f, DIALOGUE_TERRITORY, 10 * (k + 1));
			}
	}
	if (n.dialogue_until > 0 && s->time_seconds >= n.dialogue_until)
	{
		++n.dialogue_cursor;
		n.dialogue_until = 0;
		n.dialogue_ready = s->time_seconds + settings.dialogue_gap;
	}
	if (n.dialogue_cursor < n.dialogue_count && n.dialogue_until == 0 &&
	    s->time_seconds >= n.dialogue_ready)
	{
		n.dialogue_until = s->time_seconds + settings.dialogue_seconds;
		simulation_emit_sound(s, SOUND_TRANSMISSION, Vec3{},
		                      n.dialogue[n.dialogue_cursor].faction);
	}
}
int32_t communications_alert(const Simulation* s, uint32_t row)
{
	uint32_t after = 0;
	for (uint32_t n = 0; n <= row; ++n)
	{
		uint32_t first = UINT32_MAX;
		int32_t cell = -1;
		for (uint32_t p = 0; p < s->world.body_count; ++p)
		{
			const TerritoryAlert& a = s->communications.alerts[p];
			if (a.pending && a.sequence > after && a.sequence < first)
			{
				cell = (int32_t)p;
				first = a.sequence;
			}
		}
		if (cell < 0 || n == row)
			return cell;
		after = first;
	}
	return -1;
}
bool communications_acknowledge(Simulation* s, uint32_t cell)
{
	if (cell >= s->world.body_count ||
	    !s->communications.alerts[cell].pending ||
	    !simulation_body_known(s, cell))
		return false;
	s->communications.alerts[cell].pending = false;
	return true;
}
const DialogueEvent* communications_speaking(const Simulation* s)
{
	const CommunicationState& n = s->communications;
	return n.dialogue_cursor < n.dialogue_count &&
	               n.dialogue_until > s->time_seconds
	           ? &n.dialogue[n.dialogue_cursor]
			   : 0;
}
const char* communications_line(const Simulation* s, const DialogueEvent& e)
{
	const FactionDialogue& d = s->world.content.factions[e.faction].dialogue;
	return e.trigger == DIALOGUE_SHIPS   ? d.ships
	       : e.trigger == DIALOGUE_CELLS ? d.cells
	                                     : d.milestones[e.cells / 10 - 1];
}
static void text(FILE* file, const char* line)
{
	fputc('"', file);
	for (const unsigned char* p = (const unsigned char*)line; *p; ++p)
	{
		if (*p == '"' || *p == '\\')
			fputc('\\', file);
		if (*p < 32)
			fprintf(file, "\\u%04x", *p);
		else
			fputc(*p, file);
	}
	fputc('"', file);
}
void communications_write_state(const Simulation* s, FILE* file)
{
	const CommunicationState& n = s->communications;
	fprintf(file, ",\"communications\":{\"alerts_emitted\":%u,\"alerts\":[",
	        n.alert_sequence);
	bool first = true;
	for (uint32_t p = 0; p < s->world.body_count; ++p)
	{
		const TerritoryAlert& a = n.alerts[p];
		if (!a.sequence)
			continue;
		fprintf(file,
		        "%s{\"cell\":%u,\"factions\":%u,\"sequence\":%u,\"tick\":%u,"
		        "\"pending\":%s}",
		        first ? "" : ",", p, a.factions, a.sequence, a.tick,
		        a.pending ? "true" : "false");
		first = false;
	}
	fprintf(file, "],\"dialogue\":[");
	for (uint32_t i = 0; i < n.dialogue_count; ++i)
	{
		const DialogueEvent& e = n.dialogue[i];
		fprintf(file,
		        "%s{\"faction\":%u,\"trigger\":%u,\"cells\":%u,\"tick\":%u,"
		        "\"line\":",
		        i ? "," : "", e.faction, e.trigger, e.cells, e.tick);
		text(file, communications_line(s, e));
		fputc('}', file);
	}
	fprintf(file, "],\"current\":%d}",
	        communications_speaking(s) ? (int)n.dialogue_cursor : -1);
}
