// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL

#include "../fake_player.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include <common/malloc.hpp>
#include <common/nullpo.hpp>
#include <common/random.hpp>
#include <common/showmsg.hpp>
#include <common/sql.hpp>
#include <common/strlib.hpp>
#include <common/timer.hpp>

#include "../battle.hpp"
#include "../chrif.hpp"
#include "../clif.hpp"
#include "../log.hpp"
#include "../map.hpp"
#include "../pc.hpp"
#include "../skill.hpp"
#include "../status.hpp"
#include "../unit.hpp"

extern Sql* mmysql_handle;

fake_player_engine fp_engine;

static const char *fp_name_prefix[] = {
	"Rune", "Luna", "Kai", "Mira", "Nova", "Zen", "Aki", "Yuri", "Nara", "Bolt",
	"Sora", "Hana", "Vex", "Rin", "Ash", "Kira", "Fox", "Ray", "Lyn", "Ace"
};

const char *fake_player_random_name(void)
{
	static char buf[NAME_LENGTH];
	snprintf(buf, sizeof(buf), "%s_%04d", fp_name_prefix[rnd() % 20], rnd() % 10000);
	return buf;
}

fake_player_runtime *fake_player_runtime_get(map_session_data *sd)
{
	if (!sd)
		return nullptr;
	auto it = fp_engine.runtime.find(sd->status.char_id);
	if (it == fp_engine.runtime.end())
		return nullptr;
	return &it->second;
}

bool fake_player_pick_farm_map(map_session_data *sd, char *mapname, size_t mapname_len, int16 *x, int16 *y)
{
	nullpo_retr(false, sd);
	nullpo_retr(false, mapname);
	nullpo_retr(false, x);
	nullpo_retr(false, y);

	std::vector<const s_fp_map_zone *> pool;
	for (const auto &z : fp_engine.map_zones) {
		if (sd->status.base_level >= z.min_lv && sd->status.base_level <= z.max_lv)
			pool.push_back(&z);
	}
	if (pool.empty())
		return false;

	const s_fp_map_zone *z = pool[rnd() % pool.size()];
	safestrncpy(mapname, z->map.c_str(), mapname_len);
	*x = z->x;
	*y = z->y;
	return true;
}

static void fp_apply_equipment(map_session_data *sd)
{
	nullpo_retv(sd);

	for (const auto &e : fp_engine.equips) {
		if (e.class_id != sd->status.class_ && e.class_id != 0)
			continue;
		if (e.equip_index < 0 || e.equip_index >= EQI_MAX)
			continue;

		struct item it = {};
		it.nameid = e.nameid;
		it.identify = 1;
		it.refine = e.refine;
		it.card[0] = e.card[0];
		it.card[1] = e.card[1];
		it.card[2] = e.card[2];
		it.card[3] = e.card[3];

		pc_additem(sd, &it, 1, LOG_TYPE_SCRIPT);
		pc_equipitem(sd, sd->last_addeditem_index, pc_equippoint(sd, sd->last_addeditem_index));
	}
}

static void fp_grant_class_skills(map_session_data *sd)
{
	nullpo_retv(sd);

	for (const auto &sk : fp_engine.skills) {
		if (sk.class_id != sd->status.class_ && sk.class_id != 0)
			continue;
		pc_skill(sd, sk.skill_id, sk.skill_lv, ADDSKILL_PERMANENT);
	}
}

static bool fp_place_on_map(map_session_data *sd, const char *mapname, int16 x, int16 y)
{
	nullpo_retr(false, sd);

	uint16 mapindex = mapindex_name2id(mapname);
	if (!mapindex)
		mapindex = mapindex_name2id(battle_config.fake_town_map);

	if (!mapindex)
		return false;

	if (x <= 0 || y <= 0) {
		int16 m = map_mapindex2mapid(mapindex);
		if (m < 0 || !map_search_freecell(nullptr, m, &x, &y, 10, 10, 1))
			x = y = 0;
	}

	if (pc_setpos(sd, mapindex, x, y, CLR_OUTSIGHT) != SETPOS_OK)
		return false;

	status_set_viewdata(&sd->bl, sd->status.class_);
	pc_set_costume_view(sd);

	struct map_data *mapdata = map_getmapdata(sd->bl.m);
	if (mapdata) {
		if (mapdata->users++ == 0 && battle_config.dynamic_mobs)
			map_spawnmobs(sd->bl.m);
		if (!pc_isinvisible(sd))
			mapdata->users_pvp++;
	}

	if (map_addblock(&sd->bl))
		return false;

	clif_spawn(&sd->bl);
	return true;
}

bool fake_player_sql_save(map_session_data *sd)
{
	nullpo_retr(false, sd);

	fake_player_runtime *rt = fake_player_runtime_get(sd);
	if (!rt)
		return false;

	const char *mapname = mapindex_id2name(sd->mapindex);
	if (!mapname)
		mapname = battle_config.fake_town_map;

	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"UPDATE `fake_player` SET `class`='%d', `base_level`='%d', `job_level`='%d', "
		"`str`='%d', `agi`='%d', `vit`='%d', `int`='%d', `dex`='%d', `luk`='%d', "
		"`hair`='%d', `hair_color`='%d', `clothes_color`='%d', "
		"`map`='%s', `x`='%d', `y`='%d', `hp`='%u', `max_hp`='%u', `sp`='%u', `max_sp`='%u' "
		"WHERE `char_id`='%u'",
		sd->status.class_, sd->status.base_level, sd->status.job_level,
		sd->status.str, sd->status.agi, sd->status.vit, sd->status.int_, sd->status.dex, sd->status.luk,
		sd->status.hair, sd->status.hair_color, sd->status.clothes_color,
		mapname, sd->bl.x, sd->bl.y,
		sd->battle_status.hp, sd->battle_status.max_hp, sd->battle_status.sp, sd->battle_status.max_sp,
		sd->status.char_id)) {
		Sql_ShowDebug(mmysql_handle);
		return false;
	}
	return true;
}

bool fake_player_sql_create(uint32 index, const char *name)
{
	uint32 account_id = FAKE_PLAYER_ACCOUNT_ID_BASE + index;
	uint32 char_id = FAKE_PLAYER_CHAR_ID_BASE + index;
	char sex = rnd() % 2 ? 'M' : 'F';

	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"INSERT INTO `fake_player` (`char_name`,`account_id`,`char_id`,`sex`,`class`,`base_level`,`job_level`,"
		"`str`,`agi`,`vit`,`int`,`dex`,`luk`,`hair`,`hair_color`,`clothes_color`,`map`,`x`,`y`,`active`) "
		"VALUES ('%s','%u','%u','%c','0','1','1','9','9','9','9','9','9','%d','%d','%d','%s','%d','%d','1')",
		name, account_id, char_id, sex, rnd() % 24 + 1, rnd() % 8, rnd() % 8,
		battle_config.fake_town_map, battle_config.fake_town_x, battle_config.fake_town_y)) {
		Sql_ShowDebug(mmysql_handle);
		return false;
	}
	return true;
}

bool fake_player_spawn_shell(uint32 index)
{
	uint32 account_id = FAKE_PLAYER_ACCOUNT_ID_BASE + index;
	uint32 char_id = FAKE_PLAYER_CHAR_ID_BASE + index;

	if (map_id2sd(account_id))
		return false;

	map_session_data *sd = nullptr;
	CREATE(sd, map_session_data, 1);
	pc_setnewpc(sd, account_id, char_id, 0, gettick(), rnd() % 2 ? SEX_MALE : SEX_FEMALE, 0);
	sd->fd = 0;

	// Load from SQL if exists
	char name[NAME_LENGTH] = "";
	char mapname[MAP_NAME_LENGTH] = "";
	int16 x = 0, y = 0;
	uint32 db_id = 0;

	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT `id`,`char_name`,`sex`,`class`,`base_level`,`job_level`,"
		"`str`,`agi`,`vit`,`int`,`dex`,`luk`,`hair`,`hair_color`,`clothes_color`,"
		"`map`,`x`,`y` FROM `fake_player` WHERE `char_id`='%u' AND `active`=1 LIMIT 1", char_id)) {
		Sql_ShowDebug(mmysql_handle);
		aFree(sd);
		return false;
	}

	if (Sql_NumRows(mmysql_handle) > 0 && SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
		char *data;
		Sql_GetData(mmysql_handle, 0, &data, nullptr); db_id = atoi(data);
		Sql_GetData(mmysql_handle, 1, &data, nullptr); safestrncpy(name, data, sizeof(name));
		Sql_GetData(mmysql_handle, 2, &data, nullptr); sd->status.sex = (data[0] == 'F') ? SEX_FEMALE : SEX_MALE;
		Sql_GetData(mmysql_handle, 3, &data, nullptr); sd->status.class_ = atoi(data);
		Sql_GetData(mmysql_handle, 4, &data, nullptr); sd->status.base_level = atoi(data);
		Sql_GetData(mmysql_handle, 5, &data, nullptr); sd->status.job_level = atoi(data);
		Sql_GetData(mmysql_handle, 6, &data, nullptr); sd->status.str = atoi(data);
		Sql_GetData(mmysql_handle, 7, &data, nullptr); sd->status.agi = atoi(data);
		Sql_GetData(mmysql_handle, 8, &data, nullptr); sd->status.vit = atoi(data);
		Sql_GetData(mmysql_handle, 9, &data, nullptr); sd->status.int_ = atoi(data);
		Sql_GetData(mmysql_handle, 10, &data, nullptr); sd->status.dex = atoi(data);
		Sql_GetData(mmysql_handle, 11, &data, nullptr); sd->status.luk = atoi(data);
		Sql_GetData(mmysql_handle, 12, &data, nullptr); sd->status.hair = atoi(data);
		Sql_GetData(mmysql_handle, 13, &data, nullptr); sd->status.hair_color = atoi(data);
		Sql_GetData(mmysql_handle, 14, &data, nullptr); sd->status.clothes_color = atoi(data);
		Sql_GetData(mmysql_handle, 15, &data, nullptr); safestrncpy(mapname, data, sizeof(mapname));
		Sql_GetData(mmysql_handle, 16, &data, nullptr); x = atoi(data);
		Sql_GetData(mmysql_handle, 17, &data, nullptr); y = atoi(data);
	}
	Sql_FreeResult(mmysql_handle);

	if (!name[0]) {
		safestrncpy(name, fake_player_random_name(), sizeof(name));
		fake_player_sql_create(index, name);
		safestrncpy(mapname, battle_config.fake_town_map, sizeof(mapname));
		x = battle_config.fake_town_x;
		y = battle_config.fake_town_y;
	}

	safestrncpy(sd->status.name, name, NAME_LENGTH);
	sd->status.account_id = account_id;
	sd->status.char_id = char_id;
	sd->group_id = 0;
	sd->state.active = 1;

	status_change_init(&sd->bl);
	map_addiddb(&sd->bl);

	if (!fp_place_on_map(sd, mapname[0] ? mapname : battle_config.fake_town_map, x, y)) {
		map_deliddb(&sd->bl);
		aFree(sd);
		return false;
	}

	status_calc_pc(sd, SCO_FIRST);
	pc_inventoryblank(sd);

	sd->max_weight = 2000000; // allow virtual equipment load
	fp_apply_equipment(sd);
	fp_grant_class_skills(sd);
	sd->weight = 0;
	sd->max_weight = 0; // visual spoof: no real inventory weight

	fake_player_runtime rt = {};
	rt.db_id = db_id;
	rt.index = index;
	rt.ai_state = FP_STATE_SOCIAL;
	rt.last_tick = gettick();
	rt.last_save = gettick();
	rt.last_mob_scan = gettick();
	fp_engine.runtime[char_id] = rt;

	fake_player_ai_start(sd);
	fp_engine.active_count++;
	ShowInfo("fake_player: spawned [%s] at %s (%d,%d)\n", sd->status.name, mapname, sd->bl.x, sd->bl.y);
	return true;
}

int fake_player_spawn_from_db(void)
{
	int spawned = 0;
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT (`char_id` - %u) AS idx FROM `fake_player` WHERE `active`=1 ORDER BY `id` ASC",
		FAKE_PLAYER_CHAR_ID_BASE)) {
		Sql_ShowDebug(mmysql_handle);
		return 0;
	}

	while (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
		char *data;
		Sql_GetData(mmysql_handle, 0, &data, nullptr);
		uint32 index = atoi(data);
		if (fake_player_spawn_shell(index))
			spawned++;
	}
	Sql_FreeResult(mmysql_handle);
	return spawned;
}

int fake_player_maintain_population(void)
{
	if (!battle_config.fake_player_enable)
		return 0;

	int need = battle_config.fake_max_population - static_cast<int>(fp_engine.active_count);
	int created = 0;

	for (int i = 0; i < need; i++) {
		for (uint32 idx = 1; idx < (FAKE_PLAYER_ACCOUNT_ID_END - FAKE_PLAYER_ACCOUNT_ID_BASE); idx++) {
			uint32 char_id = FAKE_PLAYER_CHAR_ID_BASE + idx;
			if (fp_engine.runtime.find(char_id) != fp_engine.runtime.end())
				continue;
			if (map_id2sd(FAKE_PLAYER_ACCOUNT_ID_BASE + idx))
				continue;
			if (fake_player_spawn_shell(idx)) {
				created++;
				break;
			}
		}
	}
	return created;
}

static TIMER_FUNC(fake_player_population_timer)
{
	fake_player_maintain_population();
	add_timer(gettick() + 60000, fake_player_population_timer, 0, 0);
	return 0;
}

void fake_player_force_quit(map_session_data *sd)
{
	if (!sd || !IS_FAKE_PLAYER(sd))
		return;

	fake_player_sql_save(sd);
	fp_engine.runtime.erase(sd->status.char_id);
	if (fp_engine.active_count > 0)
		fp_engine.active_count--;

	sd->state.active = 0;
	if (sd->bl.prev != nullptr)
		unit_remove_map_pc(sd, CLR_OUTSIGHT);
	status_change_clear(&sd->bl, 3);
	skill_blockpc_clear(sd);
	if (sd->ud.walktimer != INVALID_TIMER)
		unit_stop_walking(&sd->bl, 1);
	map_deliddb(&sd->bl);
	aFree(sd);
}

void fake_player_despawn(map_session_data *sd, bool save)
{
	if (!sd || !IS_FAKE_PLAYER(sd))
		return;

	if (save)
		fake_player_sql_save(sd);

	fake_player_force_quit(sd);
}

static uint16 fp_first_to_second(uint16 job)
{
	switch (job) {
		case JOB_SWORDMAN: return JOB_KNIGHT;
		case JOB_MAGE: return JOB_WIZARD;
		case JOB_ARCHER: return JOB_HUNTER;
		case JOB_ACOLYTE: return JOB_PRIEST;
		case JOB_MERCHANT: return JOB_BLACKSMITH;
		case JOB_THIEF: return JOB_ASSASSIN;
		default: return job;
	}
}

static void fp_auto_jobchange(map_session_data *sd)
{
	nullpo_retv(sd);

	if (sd->status.class_ == JOB_NOVICE && sd->status.base_level >= 10) {
		uint16 jobs[] = { JOB_SWORDMAN, JOB_MAGE, JOB_ARCHER, JOB_ACOLYTE, JOB_MERCHANT, JOB_THIEF };
		pc_jobchange(sd, jobs[rnd() % 6], 0);
		clif_misceffect(&sd->bl, 3);
		fp_grant_class_skills(sd);
		fp_apply_equipment(sd);
		return;
	}

	if (sd->status.base_level >= 53 && sd->status.class_ >= JOB_SWORDMAN && sd->status.class_ <= JOB_THIEF) {
		pc_jobchange(sd, fp_first_to_second(sd->status.class_), 0);
		clif_misceffect(&sd->bl, 3);
		fp_grant_class_skills(sd);
		fp_apply_equipment(sd);
	}
}

static void fp_rebirth_cycle(map_session_data *sd)
{
	nullpo_retv(sd);

	if (sd->status.base_level < 99)
		return;

	map_foreachinallrange([](block_list *bl, va_list ap) -> int {
		map_session_data *tsd = va_arg(ap, map_session_data *);
		if (bl->type == BL_PC && bl->id != tsd->bl.id)
			clif_displaymessage(BL_CAST(BL_PC, bl)->fd, "[FakePlayer] A wanderer has been reborn and begins anew.");
		return 0;
	}, &sd->bl, AREA_SIZE, BL_PC, sd);

	pc_jobchange(sd, JOB_NOVICE, 0);
	sd->status.base_level = 1;
	sd->status.job_level = 1;
	sd->status.skill_point = 0;
	sd->status.status_point = 0;
	pc_resetstate(sd);
	pc_resetskill(sd, 1);

	uint16 mapindex = mapindex_name2id(battle_config.fake_town_map);
	pc_setpos(sd, mapindex, battle_config.fake_town_x, battle_config.fake_town_y, CLR_TELEPORT);
	fake_player_sql_save(sd);
}

void fake_player_on_pc_dead(map_session_data *sd, struct block_list *src)
{
	nullpo_retv(sd);
	if (!IS_FAKE_PLAYER(sd))
		return;

	status_revive(&sd->bl, 100, 100);
	uint16 mapindex = mapindex_name2id(battle_config.fake_town_map);
	pc_setpos(sd, mapindex, battle_config.fake_town_x, battle_config.fake_town_y, CLR_TELEPORT);
	fake_player_sql_save(sd);
}

void fake_player_on_map_quit(map_session_data *sd)
{
	if (!sd || !IS_FAKE_PLAYER(sd))
		return;
	fake_player_sql_save(sd);
}

static bool fp_is_support_keyword(const char *msg)
{
	if (!msg)
		return false;
	return strstr(msg, "ab") || strstr(msg, "AB") ||
		strstr(msg, "เอบี") || strstr(msg, "บัฟ") || strstr(msg, "buff");
}

void fake_player_on_whisper(map_session_data *dstsd, map_session_data *srcsd, const char *message)
{
	nullpo_retv(dstsd);
	nullpo_retv(srcsd);
	if (!IS_FAKE_PLAYER(dstsd))
		return;
	if (!fp_is_support_keyword(message))
		return;

	uint16 support_jobs[] = { JOB_ACOLYTE, JOB_PRIEST, JOB_MONK, JOB_ARCH_BISHOP, JOB_SURA };
	bool is_support = false;
	for (uint16 j : support_jobs) {
		if (dstsd->status.class_ == j) {
			is_support = true;
			break;
		}
	}
	if (!is_support)
		return;

	fake_player_runtime *rt = fake_player_runtime_get(dstsd);
	if (!rt)
		return;

	t_tick now = gettick();
	if (rt->buff_cooldown && DIFF_TICK(now, rt->buff_cooldown) < battle_config.fake_support_buff_cooldown)
		return;

	if (dstsd->bl.m != srcsd->bl.m || distance_bl(&dstsd->bl, &srcsd->bl) > AREA_SIZE)
		return;

	unit_setdir(&dstsd->bl, map_calc_dir(&dstsd->bl, srcsd->bl.x, srcsd->bl.y), true);
	clif_changed_dir(&dstsd->bl, AREA);

	if (pc_checkskill(dstsd, AL_BLESSING) > 0)
		unit_skilluse_id(&dstsd->bl, srcsd->bl.id, AL_BLESSING, 10);
	if (pc_checkskill(dstsd, AL_INCAGI) > 0)
		unit_skilluse_id(&dstsd->bl, srcsd->bl.id, AL_INCAGI, 10);

	rt->buff_cooldown = now;
	clif_displaymessage(srcsd->fd, "The adventurer casts Blessing and Increase AGI on you.");
}

bool fake_player_block_trade_party(map_session_data *sd, map_session_data *tsd)
{
	if (IS_FAKE_PLAYER(sd) || (tsd && IS_FAKE_PLAYER(tsd)))
		return true;
	return false;
}

void fake_player_on_levelup(map_session_data *sd)
{
	if (!IS_FAKE_PLAYER(sd))
		return;
	fp_auto_jobchange(sd);
	if (sd->status.base_level >= 99)
		fp_rebirth_cycle(sd);
	else {
		char mapname[MAP_NAME_LENGTH];
		int16 x = 0, y = 0;
		if (fake_player_pick_farm_map(sd, mapname, sizeof(mapname), &x, &y))
			pc_setpos(sd, mapindex_name2id(mapname), x, y, CLR_TELEPORT);
	}
	fake_player_sql_save(sd);
}

void fake_player_reload_db(void);

static void fp_config_strings_default(void)
{
	safestrncpy(battle_config.fake_town_map, "prontera", sizeof(battle_config.fake_town_map));
	safestrncpy(battle_config.fake_nlp_host, "127.0.0.1", sizeof(battle_config.fake_nlp_host));
}

static void fp_config_strings_read(void)
{
	FILE *fp = fopen("conf/battle/fake_player.conf", "r");
	if (!fp)
		return;

	char line[256];
	while (fgets(line, sizeof(line), fp)) {
		char w1[64], w2[128];
		if (sscanf(line, "%63[^:]:%127s", w1, w2) < 2)
			continue;
		// trim trailing comment/whitespace from w2
		char *cmt = strchr(w2, '/');
		if (cmt)
			*cmt = '\0';
		size_t len = strlen(w2);
		while (len > 0 && (w2[len - 1] == ' ' || w2[len - 1] == '\t' || w2[len - 1] == '\r' || w2[len - 1] == '\n'))
			w2[--len] = '\0';

		if (!strcmpi(w1, "fake_town_map"))
			safestrncpy(battle_config.fake_town_map, w2, sizeof(battle_config.fake_town_map));
		else if (!strcmpi(w1, "fake_nlp_host"))
			safestrncpy(battle_config.fake_nlp_host, w2, sizeof(battle_config.fake_nlp_host));
	}
	fclose(fp);
}

void do_init_fake_player(void)
{
	fp_config_strings_default();
	fp_config_strings_read();

	if (!battle_config.fake_player_enable) {
		ShowStatus("fake_player: disabled (fake_player_enable)\n");
		return;
	}

	fake_player_reload_db();

	add_timer_func_list(fake_player_ai_timer, "fake_player_ai_timer");
	add_timer_func_list(fake_player_population_timer, "fake_player_population_timer");

	int n = fake_player_spawn_from_db();
	ShowStatus("fake_player: restored %d bots from SQL\n", n);
	fake_player_maintain_population();
	add_timer(gettick() + 60000, fake_player_population_timer, 0, 0);
	ShowStatus("fake_player: engine ready (max population %d)\n", battle_config.fake_max_population);
}

void do_final_fake_player(void)
{
	std::vector<uint32> char_ids;
	char_ids.reserve(fp_engine.runtime.size());
	for (const auto &p : fp_engine.runtime)
		char_ids.push_back(p.first);

	for (uint32 cid : char_ids) {
		map_session_data *sd = map_charid2sd(cid);
		if (sd)
			fake_player_despawn(sd, true);
	}
	fp_engine.runtime.clear();
	fp_engine.active_count = 0;
}
