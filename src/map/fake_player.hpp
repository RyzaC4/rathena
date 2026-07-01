// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// Fake Player Engine - synthetic population with C++ AI core

#ifndef FAKE_PLAYER_HPP
#define FAKE_PLAYER_HPP

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <common/cbasetypes.hpp>
#include <common/mmo.hpp>
#include <common/timer.hpp>

struct map_session_data;
struct block_list;
struct flooritem_data;

#define FAKE_PLAYER_ACCOUNT_ID_BASE 96000000u
#define FAKE_PLAYER_ACCOUNT_ID_END  97000000u
#define FAKE_PLAYER_CHAR_ID_BASE    96000000u

#define IS_FAKE_PLAYER_ACCOUNT_ID(account_id) \
	((account_id) >= FAKE_PLAYER_ACCOUNT_ID_BASE && (account_id) < FAKE_PLAYER_ACCOUNT_ID_END)

#define IS_FAKE_PLAYER(sd) \
	((sd) != nullptr && IS_FAKE_PLAYER_ACCOUNT_ID((sd)->status.account_id))

enum e_fp_ai_state : uint8 {
	FP_STATE_IDLE = 0,
	FP_STATE_WALK,
	FP_STATE_HUNT,
	FP_STATE_LOOT,
	FP_STATE_RECOVER,
	FP_STATE_SOCIAL,
};

struct s_fp_map_zone {
	int16 min_lv;
	int16 max_lv;
	std::string map;
	int16 x;
	int16 y;
};

struct s_fp_skill_entry {
	uint16 class_id;
	uint8 state;
	uint16 skill_id;
	uint16 skill_lv;
	uint16 rate;
	int32 casttime;
	int32 delay;
	uint8 target;
};

struct s_fp_equip_entry {
	uint16 class_id;
	int16 equip_index;
	t_itemid nameid;
	uint8 refine;
	t_itemid card[4];
};

struct fake_player_runtime {
	uint32 db_id;
	uint32 index;
	e_fp_ai_state ai_state;
	int target_id;
	t_tick last_tick;
	t_tick last_mob_scan;
	t_tick last_save;
	t_tick buff_cooldown;
	t_tick loot_timer;
	uint16 idle_seconds;
	bool overweight_lock;
};

struct fake_player_engine {
	std::vector<s_fp_map_zone> map_zones;
	std::vector<s_fp_skill_entry> skills;
	std::vector<s_fp_equip_entry> equips;
	std::unordered_map<uint32, fake_player_runtime> runtime; // char_id -> runtime
	uint32 active_count;
};

extern fake_player_engine fp_engine;

// Lifecycle
void do_init_fake_player(void);
void do_final_fake_player(void);
void fake_player_reload_db(void);

// Spawn / population
int fake_player_spawn_from_db(void);
int fake_player_maintain_population(void);
bool fake_player_spawn_shell(uint32 index);
void fake_player_despawn(map_session_data *sd, bool save);

// Persistence
bool fake_player_sql_save(map_session_data *sd);
bool fake_player_sql_create(uint32 index, const char *name);

// Hooks (called from core map code)
void fake_player_on_pc_dead(map_session_data *sd, struct block_list *src);
void fake_player_on_levelup(map_session_data *sd);
void fake_player_on_map_quit(map_session_data *sd);
void fake_player_on_whisper(map_session_data *dstsd, map_session_data *srcsd, const char *message);
bool fake_player_block_trade_party(map_session_data *sd, map_session_data *tsd);

// AI
TIMER_FUNC(fake_player_ai_timer);
void fake_player_ai_start(map_session_data *sd);

void fake_player_force_quit(map_session_data *sd);

// Utility
fake_player_runtime *fake_player_runtime_get(map_session_data *sd);
const char *fake_player_random_name(void);
bool fake_player_pick_farm_map(map_session_data *sd, char *mapname, size_t mapname_len, int16 *x, int16 *y);

#endif /* FAKE_PLAYER_HPP */
