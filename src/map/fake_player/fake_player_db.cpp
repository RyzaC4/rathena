// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL

#include "../fake_player.hpp"

#include "../map.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <common/showmsg.hpp>
#include <common/strlib.hpp>

static void fp_db_trim(char *s)
{
	if (!s)
		return;
	size_t len = strlen(s);
	while (len > 0 && (s[len - 1] == '\r' || s[len - 1] == '\n' || s[len - 1] == ' ' || s[len - 1] == '\t'))
		s[--len] = '\0';
	char *p = s;
	while (*p == ' ' || *p == '\t')
		p++;
	if (p != s)
		memmove(s, p, strlen(p) + 1);
}

static bool fp_db_load_maps(const char *path)
{
	FILE *fp = fopen(path, "r");
	if (!fp) {
		ShowWarning("fake_player: cannot open %s\n", path);
		return false;
	}

	fp_engine.map_zones.clear();
	char line[256];
	while (fgets(line, sizeof(line), fp)) {
		fp_db_trim(line);
		if (!line[0] || line[0] == '/' || line[0] == '#')
			continue;

		s_fp_map_zone z{};
		int x = 0, y = 0;
		char map[MAP_NAME_LENGTH_EXT] = "";
		if (sscanf(line, "%hd,%hd,%23[^,],%d,%d", &z.min_lv, &z.max_lv, map, &x, &y) < 3)
			continue;
		z.map = map;
		z.x = static_cast<int16>(x);
		z.y = static_cast<int16>(y);
		fp_engine.map_zones.push_back(z);
	}
	fclose(fp);
	ShowStatus("fake_player: loaded %zu map zones from %s\n", fp_engine.map_zones.size(), path);
	return true;
}

static bool fp_db_load_skills(const char *path)
{
	FILE *fp = fopen(path, "r");
	if (!fp) {
		ShowWarning("fake_player: cannot open %s\n", path);
		return false;
	}

	fp_engine.skills.clear();
	char line[256];
	while (fgets(line, sizeof(line), fp)) {
		fp_db_trim(line);
		if (!line[0] || line[0] == '/' || line[0] == '#')
			continue;

		s_fp_skill_entry e{};
		int target = 0;
		if (sscanf(line, "%hu,%hhu,%hu,%hu,%hu,%d,%d,%d",
			&e.class_id, &e.state, &e.skill_id, &e.skill_lv, &e.rate,
			&e.casttime, &e.delay, &target) < 4)
			continue;
		e.target = static_cast<uint8>(target);
		fp_engine.skills.push_back(e);
	}
	fclose(fp);
	ShowStatus("fake_player: loaded %zu skill rows from %s\n", fp_engine.skills.size(), path);
	return true;
}

static bool fp_db_load_equips(const char *path)
{
	FILE *fp = fopen(path, "r");
	if (!fp) {
		ShowWarning("fake_player: cannot open %s\n", path);
		return false;
	}

	fp_engine.equips.clear();
	char line[256];
	while (fgets(line, sizeof(line), fp)) {
		fp_db_trim(line);
		if (!line[0] || line[0] == '/' || line[0] == '#')
			continue;

		s_fp_equip_entry e{};
		unsigned int c0 = 0, c1 = 0, c2 = 0, c3 = 0;
		int eq = 0;
		unsigned int nameid = 0;
		unsigned int refine = 0;
		unsigned int class_id = 0;
		if (sscanf(line, "%u,%d,%u,%u,%u,%u,%u,%u",
			&class_id, &eq, &nameid, &refine, &c0, &c1, &c2, &c3) < 3)
			continue;
		e.class_id = static_cast<uint16>(class_id);
		e.equip_index = static_cast<int16>(eq);
		e.nameid = static_cast<t_itemid>(nameid);
		e.refine = static_cast<uint8>(refine);
		e.card[0] = static_cast<t_itemid>(c0);
		e.card[1] = static_cast<t_itemid>(c1);
		e.card[2] = static_cast<t_itemid>(c2);
		e.card[3] = static_cast<t_itemid>(c3);
		fp_engine.equips.push_back(e);
	}
	fclose(fp);
	ShowStatus("fake_player: loaded %zu equip rows from %s\n", fp_engine.equips.size(), path);
	return true;
}

void fake_player_reload_db(void)
{
	fp_db_load_maps("db/fake_player/fake_map_db.txt");
	fp_db_load_skills("db/fake_player/fake_skill_db.txt");
	fp_db_load_equips("db/fake_player/fake_equip_db.txt");
}
