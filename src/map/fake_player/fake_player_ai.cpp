// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL

#include "../fake_player.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include <common/nullpo.hpp>
#include <common/random.hpp>
#include <common/showmsg.hpp>
#include <common/timer.hpp>

#include "../battle.hpp"
#include "../clif.hpp"
#include "../map.hpp"
#include "../mob.hpp"
#include "../pc.hpp"
#include "../skill.hpp"
#include "../status.hpp"
#include "../unit.hpp"

static bool fp_mob_is_ks_locked(map_session_data *sd, struct block_list *mob_bl)
{
	nullpo_retr(false, sd);
	nullpo_retr(false, mob_bl);

	if (!battle_config.fake_anti_ks)
		return false;

	struct mob_data *md = BL_CAST(BL_MOB, mob_bl);
	if (!md)
		return false;

	if (md->target_id > 0) {
		struct block_list *t = map_id2bl(md->target_id);
		if (t && t->type == BL_PC && t->id != sd->bl.id)
			return true;
	}

	// Damage log: another player hit recently
	for (int i = 0; i < DAMAGELOG_SIZE; i++) {
		int32 id = md->dmglog[i].id;
		if (id > 0 && id != sd->bl.id) {
			struct block_list *t = map_id2bl(id);
			if (t && t->type == BL_PC)
				return true;
		}
	}
	return false;
}

static int fp_ai_sub_mob(struct block_list *bl, va_list ap)
{
	map_session_data *sd = va_arg(ap, map_session_data *);
	int *best_dist = va_arg(ap, int *);
	int *best_id = va_arg(ap, int *);

	if (!sd || bl->type != BL_MOB)
		return 0;

	if (status_isdead(bl))
		return 0;

	if (fp_mob_is_ks_locked(sd, bl))
		return 0;

	int dist = distance_bl(&sd->bl, bl);
	if (dist > battle_config.fake_scan_mob_distance)
		return 0;

	if (*best_id == 0 || dist < *best_dist) {
		*best_dist = dist;
		*best_id = bl->id;
	}
	return 0;
}

static int fp_ai_sub_item(struct block_list *bl, va_list ap)
{
	map_session_data *sd = va_arg(ap, map_session_data *);
	int *best_dist = va_arg(ap, int *);
	int *best_id = va_arg(ap, int *);

	if (!sd || bl->type != BL_ITEM)
		return 0;

	int dist = distance_bl(&sd->bl, bl);
	if (dist > battle_config.fake_scan_item_distance)
		return 0;

	if (*best_id == 0 || dist < *best_dist) {
		*best_dist = dist;
		*best_id = bl->id;
	}
	return 0;
}

static void fp_visual_loot(map_session_data *sd, struct flooritem_data *fitem)
{
	nullpo_retv(sd);
	nullpo_retv(fitem);
	clif_takeitem(&sd->bl, &fitem->bl);
	map_clearflooritem(&fitem->bl);
}

static void fp_try_potion(map_session_data *sd)
{
	nullpo_retv(sd);

	int hp_pct = sd->battle_status.max_hp ? (sd->battle_status.hp * 100 / sd->battle_status.max_hp) : 100;
	int sp_pct = sd->battle_status.max_sp ? (sd->battle_status.sp * 100 / sd->battle_status.max_sp) : 100;

	if (hp_pct <= battle_config.fake_potion_hp_percent)
		status_heal(&sd->bl, battle_config.fake_potion_heal_amount, 0, 1);
	if (sp_pct <= battle_config.fake_potion_sp_percent)
		status_heal(&sd->bl, 0, battle_config.fake_potion_sp_amount, 1);
}

static void fp_try_sit(map_session_data *sd)
{
	nullpo_retv(sd);

	int hp_pct = sd->battle_status.max_hp ? (sd->battle_status.hp * 100 / sd->battle_status.max_hp) : 100;
	int sp_pct = sd->battle_status.max_sp ? (sd->battle_status.sp * 100 / sd->battle_status.max_sp) : 100;

	if (hp_pct <= battle_config.fake_sit_hp_percent || sp_pct <= battle_config.fake_sit_sp_percent) {
		if (!pc_issit(sd)) {
			pc_setsit(sd);
			clif_sitting(&sd->bl);
		}
		return;
	}

	if (pc_issit(sd) && hp_pct >= 80 && sp_pct >= 50) {
		if (pc_setstand(sd, false))
			clif_standing(&sd->bl);
	}
}

static void fp_use_class_skill(map_session_data *sd, int target_id, uint8 state)
{
	nullpo_retv(sd);

	std::vector<const s_fp_skill_entry *> pool;
	for (const auto &sk : fp_engine.skills) {
		if (sk.class_id != sd->status.class_ && sk.class_id != 0)
			continue;
		if (sk.state != state)
			continue;
		if (rnd() % 100 >= sk.rate)
			continue;
		pool.push_back(&sk);
	}
	if (pool.empty())
		return;

	const s_fp_skill_entry *sk = pool[rnd() % pool.size()];
	int tid = target_id;
	if (sk->target == 0)
		tid = sd->bl.id;
	unit_skilluse_id(&sd->bl, tid, sk->skill_id, sk->skill_lv);
}

static void fp_random_walk(map_session_data *sd)
{
	nullpo_retv(sd);

	int dx = rnd_value(battle_config.fake_walk_distance_min, battle_config.fake_walk_distance_max);
	int dy = rnd_value(battle_config.fake_walk_distance_min, battle_config.fake_walk_distance_max);
	if (rnd() % 2)
		dx = -dx;
	if (rnd() % 2)
		dy = -dy;

	int nx = sd->bl.x + dx;
	int ny = sd->bl.y + dy;
	if (map_getcell(sd->bl.m, nx, ny, CELL_CHKPASS))
		unit_walktoxy(&sd->bl, nx, ny, 4);
}

static void fp_smart_teleport(map_session_data *sd)
{
	nullpo_retv(sd);

	char mapname[MAP_NAME_LENGTH];
	int16 x = 0, y = 0;
	if (!fake_player_pick_farm_map(sd, mapname, sizeof(mapname), &x, &y))
		return;

	uint16 mapindex = mapindex_name2id(mapname);
	if (!mapindex)
		return;

	if (pc_checkskill(sd, AL_TELEPORT) > 0)
		unit_skilluse_id(&sd->bl, sd->bl.id, AL_TELEPORT, 1);
	else
		pc_setpos(sd, mapindex, x, y, CLR_TELEPORT);
}

static void fp_ai_tick(map_session_data *sd, fake_player_runtime *rt)
{
	nullpo_retv(sd);
	nullpo_retv(rt);

	t_tick now = gettick();
	fp_try_potion(sd);
	fp_try_sit(sd);

	if (pc_issit(sd))
		return;

	// Loot priority
	if (battle_config.fake_prioritize_loot) {
		int item_dist = 9999, item_id = 0;
		map_foreachinrange(fp_ai_sub_item, &sd->bl, battle_config.fake_scan_item_distance, BL_ITEM, sd, &item_dist, &item_id);
		if (item_id > 0) {
			struct block_list *ibl = map_id2bl(item_id);
			struct flooritem_data *fitem = BL_CAST(BL_ITEM, ibl);
			if (fitem) {
				if (distance_bl(&sd->bl, ibl) <= 1) {
					if (rt->loot_timer == 0 || DIFF_TICK(now, rt->loot_timer) >= battle_config.fake_loot_speed) {
						fp_visual_loot(sd, fitem);
						rt->loot_timer = now;
					}
					rt->ai_state = FP_STATE_LOOT;
					return;
				}
				unit_walktoxy(&sd->bl, ibl->x, ibl->y, 4);
				rt->ai_state = FP_STATE_LOOT;
				return;
			}
		}
	}

	int mob_dist = 9999, mob_id = 0;
	map_foreachinrange(fp_ai_sub_mob, &sd->bl, battle_config.fake_scan_mob_distance, BL_MOB, sd, &mob_dist, &mob_id);

	if (mob_id > 0) {
		rt->last_mob_scan = now;
		rt->idle_seconds = 0;
		rt->target_id = mob_id;
		rt->ai_state = FP_STATE_HUNT;
		if (mob_dist > 1)
			unit_walktoxy(&sd->bl, map_id2bl(mob_id)->x, map_id2bl(mob_id)->y, 4);
		else
			fp_use_class_skill(sd, mob_id, 1);
		return;
	}

	// No mobs — idle counter for teleport
	if (rt->last_mob_scan == 0)
		rt->last_mob_scan = now;
	if (DIFF_TICK(now, rt->last_mob_scan) >= battle_config.fake_teleport_on_idle_seconds * 1000) {
		fp_smart_teleport(sd);
		rt->last_mob_scan = now;
		rt->ai_state = FP_STATE_HUNT;
		return;
	}

	// Natural idle / walk
	if (rt->ai_state == FP_STATE_IDLE || rt->ai_state == FP_STATE_WALK) {
		int idle_ms = rnd_value(battle_config.fake_idle_time_min, battle_config.fake_idle_time_max);
		if (DIFF_TICK(now, rt->last_tick) < idle_ms) {
			rt->ai_state = FP_STATE_IDLE;
			return;
		}
		fp_random_walk(sd);
		fp_use_class_skill(sd, sd->bl.id, 2);
		rt->ai_state = FP_STATE_WALK;
		rt->last_tick = now;
		return;
	}

	fp_random_walk(sd);
	rt->ai_state = FP_STATE_WALK;
	rt->last_tick = now;
}

TIMER_FUNC(fake_player_ai_timer)
{
	map_session_data *sd = map_id2sd(id);
	if (!sd || !IS_FAKE_PLAYER(sd))
		return 0;

	fake_player_runtime *rt = fake_player_runtime_get(sd);
	if (!rt)
		return 0;

	fp_ai_tick(sd, rt);

	if (battle_config.fake_sql_save_interval > 0 &&
		DIFF_TICK(gettick(), rt->last_save) >= battle_config.fake_sql_save_interval) {
		fake_player_sql_save(sd);
		rt->last_save = gettick();
	}

	add_timer(gettick() + battle_config.fake_ai_tick_rate, fake_player_ai_timer, sd->bl.id, 0);
	return 0;
}

void fake_player_ai_start(map_session_data *sd)
{
	nullpo_retv(sd);
	add_timer(gettick() + battle_config.fake_ai_tick_rate, fake_player_ai_timer, sd->bl.id, 0);
}
