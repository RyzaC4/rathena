ACMD_FUNC(reloadfakeplayerdb) {
	nullpo_retr(-1, sd);
	fake_player_reload_db();
	clif_displaymessage(fd, "Fake Player databases reloaded.");
	return 0;
}

ACMD_FUNC(fakeplayer) {
	nullpo_retr(-1, sd);
	int n = fake_player_maintain_population();
	char buf[128];
	snprintf(buf, sizeof(buf), "Fake Player: active=%u, spawned=%d (max %d).",
		fp_engine.active_count, n, battle_config.fake_max_population);
	clif_displaymessage(fd, buf);
	return 0;
}

ACMD_FUNC(fakeplayerstats) {
	nullpo_retr(-1, sd);
	char buf[128];
	snprintf(buf, sizeof(buf), "Fake Players: %u / %d | map zones: %zu | skills: %zu | equips: %zu",
		fp_engine.active_count, battle_config.fake_max_population,
		fp_engine.map_zones.size(), fp_engine.skills.size(), fp_engine.equips.size());
	clif_displaymessage(fd, buf);
	return 0;
}
