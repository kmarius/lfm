#include <errno.h>
#include <lauxlib.h>
#include <libgen.h>
#include <luajit.h>
#include <lualib.h>
#include <notcurses/notcurses.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

#include "async.h"
#include "cache.h"
#include "cmdline.h"
#include "config.h"
#include "cvector.h"
#include "dir.h"
#include "find.h"
#include "fm.h"
#include "log.h"
#include "lualfm.h"
#include "notify.h"
#include "search.h"
#include "tokenize.h"
#include "tpool.h"
#include "trie.h"
#include "ui.h"
#include "util.h"

#define TABLE_CALLBACKS "callbacks"

#define luaL_optbool(L, i, d) \
	lua_isnoneornil(L, i) ? d : lua_toboolean(L, i)

static App *app = NULL;
static Ui *ui = NULL;
static Fm *fm = NULL;

static struct {
	Trie *normal;
	Trie *cmd;
	Trie *cur;
	input_t *seq;
	char *str;
} maps;

/* lfm lib {{{ */

static int l_handle_key(lua_State *L)
{
	const char *keys = luaL_checkstring(L, 1);
	input_t *buf = malloc((strlen(keys) + 1) * sizeof(input_t));
	key_names_to_input(keys, buf);
	for (input_t *u = buf; *u; u++)
		lua_handle_key(L, *u);
	free(buf);
	return 0;
}


static int l_timeout(lua_State *L)
{
	const int16_t dur = luaL_checkinteger(L, 1);
	if (dur > 0)
		app_timeout_set(app, dur);
	return 0;
}


static int l_search(lua_State *L)
{
	search(ui, luaL_optstring(L, 1, NULL), true);
	return 0;
}


static int l_search_backwards(lua_State *L)
{
	search(ui, luaL_optstring(L, 1, NULL), false);
	return 0;
}


static int l_nohighlight(lua_State *L)
{
	(void) L;
	nohighlight(ui);
	return 0;
}


static int l_search_next(lua_State *L)
{
	(void) L;
	search_next(ui, fm, luaL_optbool(L, 1, false));
	return 0;
}


static int l_search_prev(lua_State *L)
{
	(void) L;
	search_prev(ui, fm, luaL_optbool(L, 1, false));
	return 0;
}


static int l_find(lua_State *L)
{
	lua_pushboolean(L, find(fm, ui, luaL_checkstring(L, 1)));
	return 1;
}


static int l_find_clear(lua_State *L)
{
	(void) L;
	find_clear(fm, ui);
	return 0;
}


static int l_find_next(lua_State *L)
{
	(void) L;
	find_next(fm, ui);
	return 0;
}


static int l_find_prev(lua_State *L)
{
	(void) L;
	find_prev(fm, ui);
	return 0;
}


static int l_crash(lua_State *L)
{
	free(L);
	return 0;
}


static int l_quit(lua_State *L)
{
	(void) L;
	app_quit(app);
	return 0;
}


static int l_echo(lua_State *L)
{
	ui_echom(ui, luaL_optstring(L, 1, ""));
	return 0;
}


static int l_error(lua_State *L)
{
	ui_error(ui, luaL_checkstring(L, 1));
	return 0;
}


static int l_execute(lua_State *L)
{
	bool out = true;
	bool err = true;
	bool fork = false;
	int cb_index = 0;

	luaL_checktype(L, 1, LUA_TTABLE);

	const uint16_t n = lua_objlen(L, 1);
	if (n == 0)
		luaL_error(L, "no command given");

	char **args = malloc((n + 1) * sizeof(char*));
	for (uint16_t i = 1; i <= n; i++) {
		lua_rawgeti(L, 1, i);
		args[i-1] = strdup(lua_tostring(L, -1));
		lua_pop(L, 1);
	}
	args[n] = NULL;
	if (lua_gettop(L) >= 2) {
		luaL_checktype(L, 2, LUA_TTABLE);

		lua_getfield(L, 2, "out");
		if (!lua_isnoneornil(L, -1))
			out = lua_toboolean(L, -1);
		lua_pop(L, 1);

		lua_getfield(L, 2, "err");
		if (!lua_isnoneornil(L, -1))
			err = lua_toboolean(L, -1);
		lua_pop(L, 1);

		lua_getfield(L, 2, "fork");
		if (!lua_isnoneornil(L, -1))
			fork = lua_toboolean(L, -1);
		lua_pop(L, 1);

		lua_getfield(L, 2, "callback");
		if (!lua_isnoneornil(L, -1)) {
			lua_getfield(L, LUA_REGISTRYINDEX, TABLE_CALLBACKS);
			cb_index = lua_objlen(L, -1) + 1;
			lua_pushvalue(L, -2);
			lua_rawseti(L, -2, cb_index);
			lua_pop(L, 1);
		}
		lua_pop(L, 1);
	}
	bool ret = app_execute(app, args[0], args, fork, out, err, cb_index);
	for (uint16_t i = 0; i < n+1; i++)
		free(args[i]);
	free(args);

	if (ret) {
		lua_pushboolean(L, true);
		return 1;
	} else {
		lua_pushnil(L);
		lua_pushstring(L, strerror(errno)); // not sure if something even sets errno
		return 2;
	}
}


void lua_run_callback(lua_State *L, int cb_index, int rstatus)
{
	lua_getfield(L, LUA_REGISTRYINDEX, TABLE_CALLBACKS);
	lua_rawgeti(L, -1, cb_index);
	lua_pushnumber(L, rstatus);
	lua_call(L, 1, 0);
	lua_pushnil(L);
	lua_rawseti(L, -2, cb_index);
	lua_pop(L, 1);
}


static int l_map_key(lua_State *L)
{
	const char *desc = NULL;

	if (lua_type(L, 3) == LUA_TTABLE) {
		lua_getfield(L, 3, "desc");
		if (!lua_isnoneornil(L, -1))
			desc = lua_tostring(L, -1);
		lua_pop(L, 1);
	}
	const char *keys = luaL_checkstring(L, 1);

	if (!(lua_type(L, 2) == LUA_TFUNCTION))
		luaL_argerror(L, 2, "expected function");

	input_t *buf = malloc((strlen(keys) + 1) * sizeof(input_t));
	Trie *k = trie_insert(maps.normal, key_names_to_input(keys, buf), keys, desc);
	lua_pushlightuserdata(L, (void *)k);
	lua_pushvalue(L, 2);
	lua_settable(L, LUA_REGISTRYINDEX);
	return 0;
}


static int l_cmap_key(lua_State *L)
{
	const char *desc = NULL;
	if (lua_type(L, 3) == LUA_TTABLE) {
		lua_getfield(L, 3, "desc");
		if (!lua_isnoneornil(L, -1))
			desc = lua_tostring(L, -1);
		lua_pop(L, 1);
	}
	const char *keys = luaL_checkstring(L, 1);

	if (!(lua_type(L, 2) == LUA_TFUNCTION))
		luaL_argerror(L, 2, "expected function");

	input_t *buf = malloc((strlen(keys)+1) * sizeof(input_t));
	Trie *k = trie_insert(maps.cmd, key_names_to_input(keys, buf), keys, desc);
	lua_pushlightuserdata(L, (void *)k);
	lua_pushvalue(L, 2);
	lua_settable(L, LUA_REGISTRYINDEX);
	free(buf);
	return 0;
}


static const struct luaL_Reg lfm_lib[] = {
	{"execute", l_execute},
	{"map", l_map_key},
	{"cmap", l_cmap_key},
	{"handle_key", l_handle_key},
	{"timeout", l_timeout},
	{"find", l_find},
	{"find_clear", l_find_clear},
	{"find_next", l_find_next},
	{"find_prev", l_find_prev},
	{"nohighlight", l_nohighlight},
	{"search", l_search},
	{"search_back", l_search_backwards},
	{"search_next", l_search_next},
	{"search_prev", l_search_prev},
	{"crash", l_crash},
	{"echo", l_echo},
	{"error", l_error},
	{"quit", l_quit},
	{NULL, NULL}};

/* }}} */

/* config lib {{{ */

static int l_config_index(lua_State *L)
{
	const char *key = luaL_checkstring(L, 2);
	if (streq(key, "truncatechar")) {
		char buf[MB_LEN_MAX + 1];
		int l = wctomb(buf, cfg.truncatechar);
		if (l == -1) {
			log_error("converting truncatechar to mbs");
			l = 0;
		}
		buf[l] = 0;
		lua_pushstring(L, buf);
		return 1;
	} else if (streq(key, "hidden")) {
		lua_pushboolean(L, cfg.hidden);
		return 1;
	} else if (streq(key, "ratios")) {
		const size_t l = cvector_size(cfg.ratios);
		lua_createtable(L, l, 0);
		for (size_t i = 0; i < l; i++) {
			lua_pushinteger(L, cfg.ratios[i]);
			lua_rawseti(L, -2, i + 1);
		}
		return 1;
	} else if (streq(key, "inotify_blacklist")) {
		const size_t l = cvector_size(cfg.inotify_blacklist);
		lua_createtable(L, l, 0);
		for (size_t i = 0; i < l; i++) {
			lua_pushstring(L, cfg.inotify_blacklist[i]);
			lua_rawseti(L, -2, i + 1);
		}
		return 1;
	} else if (streq(key, "scrolloff")) {
		lua_pushinteger(L, cfg.scrolloff);
		return 1;
	} else if (streq(key, "preview")) {
		lua_pushboolean(L, cfg.preview);
		return 1;
	} else if (streq(key, "previewer")) {
		lua_pushstring(L, cfg.previewer ? cfg.previewer : "");
		return 1;
	} else if (streq(key, "fifopath")) {
		lua_pushstring(L, cfg.fifopath);
		return 1;
	} else if (streq(key, "logpath")) {
		lua_pushstring(L, cfg.logpath);
		return 1;
	} else if (streq(key, "configpath")) {
		lua_pushstring(L, cfg.configpath);
		return 1;
	} else if (streq(key, "dircache_size")) {
		lua_pushinteger(L, fm->dirs.cache.capacity);
		return 1;
	} else if (streq(key, "previewcache_size")) {
		lua_pushinteger(L, ui->preview.cache.capacity);
		return 1;
	} else {
		luaL_error(L, "unexpected key %s", key);
	}
	return 0;
}


static int l_config_newindex(lua_State *L)
{
	const char *key = luaL_checkstring(L, 2);
	if (streq(key, "truncatechar")) {
		wchar_t w;
		const char *val = luaL_checkstring(L, 3);
		int l = mbtowc(&w, val, MB_LEN_MAX);
		if (l == -1) {
			log_error("converting truncatechar to wchar_t");
			return 0;
		}
		cfg.truncatechar = w;
		ui_redraw(ui, REDRAW_FM);
	} else if (streq(key, "hidden")) {
		bool hidden = lua_toboolean(L, 3);
		fm_hidden_set(fm, hidden);
		ui_redraw(ui, REDRAW_FM);
	} else if (streq(key, "ratios")) {
		const int l = lua_objlen(L, 3);
		if (l == 0)
			luaL_argerror(L, 3, "no ratios given");
		uint16_t *ratios = NULL;
		for (uint16_t i = 1; i <= l; i++) {
			lua_rawgeti(L, 3, i);
			cvector_push_back(ratios, lua_tointeger(L, -1));
			if (ratios[i-1] <= 0) {
				luaL_error(L, "ratio must be non-negative");
				cvector_free(ratios);
				return 0;
			}
			lua_pop(L, 1);
		}
		config_ratios_set(ratios);
		fm_recol(fm);
		ui_recol(ui);
		ui_redraw(ui, REDRAW_FM);
	} else if (streq(key, "inotify_blacklist")) {
		const size_t l = lua_objlen(L, 3);
		cvector_ffree(cfg.inotify_blacklist, free);
		cfg.inotify_blacklist = NULL;
		for (size_t i = 1; i <= l; i++) {
			lua_rawgeti(L, 3, i);
			cvector_push_back(cfg.inotify_blacklist, strdup(lua_tostring(L, -1)));
			lua_pop(L, 1);
		}
		return 0;
	} else if (streq(key, "scrolloff")) {
		cfg.scrolloff = max(luaL_checkinteger(L, 3), 0);
		return 0;
	} else if (streq(key, "preview")) {
		cfg.preview = lua_toboolean(L, 3);
		fm_recol(fm);
		ui_redraw(ui, REDRAW_FM);
		return 0;
	} else if (streq(key, "previewer")) {
		if (lua_isnoneornil(L, 3)) {
			cfg.previewer = NULL;
		} else {
			const char *str = luaL_checkstring(L, 3);
			cfg.previewer = str[0] != 0 ? strdup(str) : NULL;
		}
		/* TODO: purge preview cache (on 2021-08-10) */
		return 0;
	} else if (streq(key, "dircache_size")) {
		int capacity = luaL_checkinteger(L, 3);
		if (capacity < 0)
			luaL_argerror(L, 3, "size must be non-negative");
		cache_resize(&fm->dirs.cache, capacity);
	} else if (streq(key, "previewcache_size")) {
		int capacity = luaL_checkinteger(L, 3);
		if (capacity < 0)
			luaL_argerror(L, 3, "size must be non-negative");
		cache_resize(&ui->preview.cache, capacity);
	} else {
		luaL_error(L, "unexpected key %s", key);
	}
	return 0;
}


static const struct luaL_Reg config_mt[] = {
	{"__index", l_config_index},
	{"__newindex", l_config_newindex},
	{NULL, NULL}};

/* }}} */

/* log lib {{{ */

static int l_log_trace(lua_State *L)
{
	log_trace("%s", luaL_checkstring(L, 1));
	return 0;
}


static int l_log_debug(lua_State *L)
{
	log_debug("%s", luaL_checkstring(L, 1));
	return 0;
}


static int l_log_info(lua_State *L)
{
	log_info("%s", luaL_checkstring(L, 1));
	return 0;
}


static int l_log_warn(lua_State *L)
{
	log_warn("%s", luaL_checkstring(L, 1));
	return 0;
}


static int l_log_error(lua_State *L)
{
	log_error("%s", luaL_checkstring(L, 1));
	return 0;
}


static int l_log_fatal(lua_State *L)
{
	log_fatal("%s", luaL_checkstring(L, 1));
	return 0;
}


static const struct luaL_Reg log_lib[] = {
	{"trace", l_log_trace},
	{"debug", l_log_debug},
	{"info", l_log_info},
	{"warn", l_log_warn},
	{"error", l_log_error},
	{"fatal", l_log_fatal},
	{NULL, NULL}};

/* }}} */

/* ui lib {{{ */

static int l_ui_history_append(lua_State *L)
{
	history_append(&ui->history, luaL_checkstring(L, 1));
	return 0;
}


static int l_ui_history_prev(lua_State *L)
{
	const char *line = history_prev(&ui->history);
	if (!line)
		return 0;
	lua_pushstring(L, line);
	return 1;
}


static int l_ui_history_next(lua_State *L)
{
	const char *line = history_next(&ui->history);
	if (!line)
		return 0;
	lua_pushstring(L, line);
	return 1;
}


static int l_ui_messages(lua_State *L)
{
	lua_newtable(L);
	for (size_t i = 0; i < cvector_size(ui->messages); i++) {
		lua_pushstring(L, ui->messages[i]);
		lua_rawseti(L, -2, i+1);
	}
	return 1;
}


static int l_ui_clear(lua_State *L)
{
	(void) L;
	ui_clear(ui);
	return 0;
}


static int l_ui_get_width(lua_State *L)
{
	lua_pushnumber(L, ui->ncol);
	return 1;
}


static int l_ui_get_height(lua_State *L)
{
	lua_pushnumber(L, ui->nrow);
	return 1;
}


static int l_ui_menu(lua_State *L)
{
	cvector_vector_type(char*) menubuf = NULL;
	if (lua_type(L, -1) == LUA_TTABLE) {
		cvector_grow(menubuf, lua_objlen(L, -1));
		for (lua_pushnil(L); lua_next(L, -2); lua_pop(L, 1))
			cvector_push_back(menubuf, strdup(luaL_checkstring(L, -1)));
	}
	ui_showmenu(ui, menubuf);
	return 0;
}


static int l_ui_draw(lua_State *L)
{
	(void) L;
	ui_redraw(ui, REDRAW_FM);
	return 0;
}


static const struct luaL_Reg ui_lib[] = {
	{"get_width", l_ui_get_width},
	{"get_height", l_ui_get_height},
	{"clear", l_ui_clear},
	{"draw", l_ui_draw},
	{"history_append", l_ui_history_append},
	{"history_next", l_ui_history_next},
	{"history_prev", l_ui_history_prev},
	{"menu", l_ui_menu},
	{"messages", l_ui_messages},
	{NULL, NULL}};

/* }}} */

/* color lib {{{ */

static uint32_t read_channel(lua_State *L, int idx)
{
	switch(lua_type(L, idx))
	{
		case LUA_TSTRING:
			return NCCHANNEL_INITIALIZER_PALINDEX(lua_tointeger(L, idx));
		case LUA_TNUMBER:
			return NCCHANNEL_INITIALIZER_HEX(lua_tointeger(L, idx));
		default:
			luaL_typerror(L, idx, "string or number");
			return 0;
	}
}


static uint64_t read_color_pair(lua_State *L, int ind)
{
	uint32_t fg, bg;
	ncchannel_set_default(&fg);
	ncchannel_set_default(&bg);

	lua_getfield(L, ind, "fg");
	if (!lua_isnoneornil(L, -1))
		fg = read_channel(L, -1);
	lua_pop(L, 1);

	lua_getfield(L, ind, "bg");
	if (!lua_isnoneornil(L, -1))
		bg = read_channel(L, -1);
	lua_pop(L, 1);

	return ncchannels_combine(fg, bg);
}


static int l_colors_newindex(lua_State *L)
{
	const char *key = luaL_checkstring(L, 2);
	if (streq(key, "copy")) {
		if (lua_istable(L, 3))
			cfg.colors.copy = read_color_pair(L, 3);
	} else if (streq(key, "delete")) {
		if (lua_istable(L, 3))
			cfg.colors.delete = read_color_pair(L, 3);
	} else if (streq(key, "dir")) {
		if (lua_istable(L, 3))
			cfg.colors.dir = read_color_pair(L, 3);
	} else if (streq(key, "broken")) {
		if (lua_istable(L, 3))
			cfg.colors.broken = read_color_pair(L, 3);
	} else if (streq(key, "exec")) {
		if (lua_istable(L, 3))
			cfg.colors.exec = read_color_pair(L, 3);
	} else if (streq(key, "search")) {
		if (lua_istable(L, 3))
			cfg.colors.search = read_color_pair(L, 3);
	} else if (streq(key, "normal")) {
		if (lua_istable(L, 3))
			cfg.colors.normal = read_color_pair(L, 3);
	} else if (streq(key, "current")) {
		cfg.colors.current = read_channel(L, 3);
	} else if (streq(key, "patterns")) {
		if (lua_istable(L, 3)) {
			for (lua_pushnil(L); lua_next(L, 3); lua_pop(L, 1)) {
				lua_getfield(L, -1, "color");
				const uint64_t ch = read_color_pair(L, -1);
				lua_pop(L, 1);

				lua_getfield(L, -1, "ext");
				for (lua_pushnil(L); lua_next(L, -2); lua_pop(L, 1))
					config_ext_channel_add(lua_tostring(L, -1), ch);
				lua_pop(L, 1);
			}
		}
	} else {
		luaL_error(L, "unexpected key %s", key);
	}
	ui_redraw(ui, REDRAW_FM);
	return 0;
}


static const struct luaL_Reg colors_mt[] = {
	{"__newindex", l_colors_newindex},
	{NULL, NULL}};

/* }}} */

/* cmd lib {{{ */

static int l_cmd_line_get(lua_State *L)
{
	const char *line = cmdline_get(&ui->cmdline);
	const char *prefix = cmdline_prefix_get(&ui->cmdline);
	log_debug("%s%s", prefix, line);
	lua_pushstring(L, line);
	return 1;
}


static int l_cmd_line_set(lua_State *L)
{
	switch (lua_gettop(L)) {
		case 1:
			{
				if (cmdline_set(&ui->cmdline, lua_tostring(L, 1)))
					ui_redraw(ui, REDRAW_CMDLINE);
			}
			break;
		case 3:
			{
				if(cmdline_set_whole(&ui->cmdline, lua_tostring(L, 1),
							lua_tostring(L, 2), lua_tostring(L, 3)))
					ui_redraw(ui, REDRAW_CMDLINE);

			}
	}
	return 0;
}


static int l_cmd_clear(lua_State *L)
{
	(void) L;
	ui_cmd_clear(ui);
	return 0;
}


static int l_cmd_delete(lua_State *L)
{
	(void) L;
	if (cmdline_delete(&ui->cmdline))
		ui_redraw(ui, REDRAW_CMDLINE);
	return 0;
}


static int l_cmd_delete_right(lua_State *L)
{
	(void) L;
	if (cmdline_delete_right(&ui->cmdline))
		ui_redraw(ui, REDRAW_CMDLINE);
	return 0;
}


static int l_cmd_delete_word(lua_State *L)
{
	(void) L;
	if (cmdline_delete_word(&ui->cmdline))
		ui_redraw(ui, REDRAW_CMDLINE);
	return 0;
}


static int l_cmd_insert(lua_State *L)
{
	if (cmdline_insert(&ui->cmdline, lua_tostring(L, 1)))
		ui_redraw(ui, REDRAW_CMDLINE);
	return 0;
}


static int l_cmd_left(lua_State *L)
{
	(void) L;
	if (cmdline_left(&ui->cmdline))
		ui_redraw(ui, REDRAW_CMDLINE);
	return 0;
}


static int l_cmd_right(lua_State *L)
{
	(void) L;
	if (cmdline_right(&ui->cmdline))
		ui_redraw(ui, REDRAW_CMDLINE);
	return 0;
}


static int l_cmd_word_left(lua_State *L)
{
	(void) L;
	if (cmdline_word_left(&ui->cmdline))
		ui_redraw(ui, REDRAW_CMDLINE);
	return 0;
}


static int l_cmd_word_right(lua_State *L)
{
	(void) L;
	if (cmdline_word_right(&ui->cmdline))
		ui_redraw(ui, REDRAW_CMDLINE);
	return 0;
}


static int l_cmd_delete_line_left(lua_State *L)
{
	(void) L;
	if (cmdline_delete_line_left(&ui->cmdline))
		ui_redraw(ui, REDRAW_CMDLINE);
	return 0;
}


static int l_cmd_home(lua_State *L)
{
	(void) L;
	if (cmdline_home(&ui->cmdline))
		ui_redraw(ui, REDRAW_CMDLINE);
	return 0;
}


static int l_cmd_end(lua_State *L)
{
	(void) L;
	if (cmdline_end(&ui->cmdline))
		ui_redraw(ui, REDRAW_CMDLINE);
	return 0;
}


static int l_cmd_prefix_set(lua_State *L)
{
	ui_cmd_prefix_set(ui, luaL_optstring(L, 1, ""));
	return 0;
}


static int l_cmd_prefix_get(lua_State *L)
{
	const char *prefix = cmdline_prefix_get(&ui->cmdline);
	lua_pushstring(L, prefix ? prefix : "");
	return 1;
}


static int l_fm_up(lua_State *L)
{
	if (fm_up(fm, luaL_optint(L, 1, 1)))
		ui_redraw(ui, REDRAW_FM);
	return 0;
}


static int l_fm_down(lua_State *L)
{
	if (fm_down(fm, luaL_optint(L, 1, 1)))
		ui_redraw(ui, REDRAW_FM);
	return 0;
}


static const struct luaL_Reg cmd_lib[] = {
	{"clear", l_cmd_clear},
	{"delete", l_cmd_delete},
	{"delete_right", l_cmd_delete_right},
	{"delete_word", l_cmd_delete_word},
	{"_end", l_cmd_end},
	{"getline", l_cmd_line_get},
	{"getprefix", l_cmd_prefix_get},
	{"home", l_cmd_home},
	{"insert", l_cmd_insert},
	{"left", l_cmd_left},
	{"word_left", l_cmd_word_left},
	{"word_right", l_cmd_word_right},
	{"delete_line_left", l_cmd_delete_line_left},
	{"right", l_cmd_right},
	{"setline", l_cmd_line_set},
	{"setprefix", l_cmd_prefix_set},
	{NULL, NULL}};

/* }}} */

/* fm lib {{{ */

static int l_fm_get_height(lua_State *L)
{
	lua_pushnumber(L, fm->height);
	return 1;
}


static int l_fm_drop_cache(lua_State *L)
{
	(void) L;
	fm_drop_cache(fm);
	ui_drop_cache(ui);
	return 0;
}


static int l_fm_reload(lua_State *L)
{
	(void) L;
	fm_reload(fm);
	return 0;
}


static int l_fm_check(lua_State *L)
{
	(void) L;
	Dir *d = fm_current_dir(fm);
	if (!dir_check(d))
		async_dir_load(d, true);
	return 0;
}


static int l_fm_sel(lua_State *L)
{
	fm_move_cursor_to(fm, luaL_checkstring(L, 1));
	ui_redraw(ui, REDRAW_FM);
	return 0;
}


static int l_fm_top(lua_State *L)
{
	(void) L;
	if (fm_top(fm))
		ui_redraw(ui, REDRAW_FM);
	return 0;
}


static int l_fm_bot(lua_State *L)
{
	(void) L;
	if (fm_bot(fm))
		ui_redraw(ui, REDRAW_FM);
	return 0;
}


static int l_fm_updir(lua_State *L)
{
	(void) L;
	fm_updir(fm);
	nohighlight(ui);
	ui_redraw(ui, REDRAW_FM);
	return 0;
}


static int l_fm_open(lua_State *L)
{
	File *file = fm_open(fm);
	if (!file) {
		/* changed directory */
		ui_redraw(ui, REDRAW_FM);
		nohighlight(ui);
		return 0;
	} else {
		if (cfg.selfile) {
			/* lastdir is written in main */
			fm_selection_write(fm, cfg.selfile);
			app_quit(app);
			return 0;
		}

		lua_pushstring(L, file_path(file));
		return 1;
	}
}


static int l_fm_current_file(lua_State *L)
{
	File *file = fm_current_file(fm);
	if (file) {
		lua_pushstring(L, file_path(file));
		return 1;
	}
	return 0;
}


static int l_fm_current_dir(lua_State *L)
{
	const Dir *dir = fm_current_dir(fm);
	lua_newtable(L);
	lua_pushstring(L, dir->path);
	lua_setfield(L, -2, "path");
	lua_pushstring(L, dir->name);
	lua_setfield(L, -2, "name");

	lua_newtable(L);
	for (uint16_t i = 0; i < dir->length; i++) {
		lua_pushstring(L, file_path(dir->files[i]));
		lua_rawseti(L, -2, i+1);
	}
	lua_setfield(L, -2, "files");

	return 1;
}


static int l_fm_visual_start(lua_State *L)
{
	(void) L;
	fm_selection_visual_start(fm);
	ui_redraw(ui, REDRAW_FM);
	return 0;
}


static int l_fm_visual_end(lua_State *L)
{
	(void) L;
	fm_selection_visual_stop(fm);
	ui_redraw(ui, REDRAW_FM);
	return 0;
}


static int l_fm_visual_toggle(lua_State *L)
{
	(void) L;
	fm_selection_visual_toggle(fm);
	ui_redraw(ui, REDRAW_FM);
	return 0;
}


static int l_fm_sortby(lua_State *L)
{
	const int l = lua_gettop(L);
	const char *op;
	Dir *dir = fm_current_dir(fm);
	for (int i = 0; i < l; i++) {
		op = luaL_checkstring(L, i + 1);
		if (streq(op, "name")) {
			dir->sorttype = SORT_NAME;
		} else if (streq(op, "natural")) {
			dir->sorttype = SORT_NATURAL;
		} else if (streq(op, "ctime")) {
			dir->sorttype = SORT_CTIME;
		} else if (streq(op, "size")) {
			dir->sorttype = SORT_SIZE;
		} else if (streq(op, "random")) {
			dir->sorttype = SORT_RAND;
		} else if (streq(op, "dirfirst")) {
			dir->dirfirst = true;
		} else if (streq(op, "nodirfirst")) {
			dir->dirfirst = false;
		} else if (streq(op, "reverse")) {
			dir->reverse = true;
		} else if (streq(op, "noreverse")) {
			dir->reverse = false;
		} else {
			luaL_error(L, "sortby: unrecognized option: %s", op);
			// not reached
		}
	}
	dir->sorted = false;
	const File *file = dir_current_file(dir);
	dir_sort(dir);
	if (file)
		fm_move_cursor_to(fm, file_name(file));
	ui_redraw(ui, REDRAW_FM);
	return 0;
}


static int l_fm_selection_toggle_current(lua_State *L)
{
	(void) L;
	fm_selection_toggle_current(fm);
	ui_redraw(ui, REDRAW_FM);
	return 0;
}


static int l_fm_selection_add(lua_State *L)
{
	fm_selection_add_file(fm, luaL_checkstring(L, 1));
	return 0;
}


static int l_fm_selection_set(lua_State *L)
{
	cvector_vector_type(char*) selection = NULL;
	if (lua_istable(L, -1)) {
		for (lua_pushnil(L); lua_next(L, -2); lua_pop(L, 1))
			cvector_push_back(selection, strdup(luaL_checkstring(L, -1)));
	}
	fm_selection_set(fm, selection);
	return 0;
}


static int l_fm_selection_get(lua_State *L)
{
	lua_createtable(L, fm->selection.length, 0);
	size_t j = 1;
	for (size_t i = 0; i < cvector_size(fm->selection.files); i++) {
		if (fm->selection.files[i]) {
			lua_pushstring(L, fm->selection.files[i]);
			lua_rawseti(L, -2, j++);
		}
	}
	return 1;
}


static int l_fm_selection_clear(lua_State *L)
{
	(void) L;
	fm_selection_clear(fm);
	ui_redraw(ui, REDRAW_FM);
	return 0;
}


static int l_fm_selection_reverse(lua_State *L)
{
	(void) L;
	fm_selection_reverse(fm);
	ui_redraw(ui, REDRAW_FM);
	return 0;
}


static int l_fm_chdir(lua_State *L)
{
	const char *path = lua_tostring(L, 1);
	nohighlight(ui);
	lua_run_hook(L, "ChdirPre");
	if (fm_chdir(fm, path, true))
		lua_run_hook(L, "ChdirPost");
	ui_redraw(ui, REDRAW_FM);
	return 0;
}


static int l_fm_get_load(lua_State *L)
{
	switch (fm->load.mode) {
		case MODE_MOVE:
			lua_pushstring(L, "move");
			break;
		case MODE_COPY:
			lua_pushstring(L, "copy");
			break;
	}
	lua_createtable(L, cvector_size(fm->load.files), 0);
	for (size_t i = 0; i < cvector_size(fm->load.files); i++) {
		lua_pushstring(L, fm->load.files[i]);
		lua_rawseti(L, -2, i+1);
	}
	return 2;
}


static int l_fm_load_set(lua_State *L)
{
	const char *mode = luaL_checkstring(L, 1);
	fm_load_clear(fm);
	if (streq(mode, "move"))
		fm->load.mode = MODE_MOVE;
	else
		fm->load.mode = MODE_COPY;

	const size_t l = lua_objlen(L, 2);
	for (size_t i = 0; i < l; i++) {
		lua_rawgeti(L, 2, i + 1);
		cvector_push_back(fm->load.files, strdup(lua_tostring(L, -1)));
		lua_pop(L, 1);
	}
	ui_redraw(ui, REDRAW_FM);
	return 0;
}


static int l_fm_load_clear(lua_State *L)
{
	(void) L;
	fm_load_clear(fm);
	ui_redraw(ui, REDRAW_FM);
	return 0;
}


static int l_fm_copy(lua_State *L)
{
	(void) L;
	fm_copy(fm);
	ui_redraw(ui, REDRAW_FM);
	return 0;
}


static int l_fm_cut(lua_State *L)
{
	(void) L;
	fm_cut(fm);
	ui_redraw(ui, REDRAW_FM);
	return 0;
}


static int l_fm_filter_get(lua_State *L)
{
	lua_pushstring(L, fm_filter_get(fm));
	return 1;
}


static int l_fm_filter(lua_State *L)
{
	const char *filter = lua_tostring(L, 1);
	fm_filter(fm, filter);
	ui_redraw(ui, REDRAW_FM);
	return 0;
}


static int l_fm_mark_load(lua_State *L)
{
	/* TODO: what about umlauts (on 2022-01-14) */
	const char *b = lua_tostring(L, 1);
	fm_mark_load(fm, b[0]);
	ui_redraw(ui, REDRAW_FM);
	return 0;
}


static int l_fm_flatten_level(lua_State *L)
{
	log_debug("flatten_level %d", fm_current_dir(fm)->flatten_level);
	lua_pushinteger(L, fm_current_dir(fm)->flatten_level);
	return 1;
}

static int l_fm_flatten(lua_State *L)
{
	(void) L;
	int level = luaL_optinteger(L, 1, 0);
	if (level < 0)
		level = 0;
	fm_flatten(fm, level);
	ui_redraw(ui, REDRAW_FM);
	return 0;
}

static const struct luaL_Reg fm_lib[] = {
	{"flatten", l_fm_flatten},
	{"flatten_level", l_fm_flatten_level},
	{"bottom", l_fm_bot},
	{"chdir", l_fm_chdir},
	{"down", l_fm_down},
	{"filter", l_fm_filter},
	{"getfilter", l_fm_filter_get},
	{"mark_load", l_fm_mark_load},
	{"open", l_fm_open},
	{"current_dir", l_fm_current_dir},
	{"current_file", l_fm_current_file},
	{"selection_clear", l_fm_selection_clear},
	{"selection_reverse", l_fm_selection_reverse},
	{"selection_toggle", l_fm_selection_toggle_current},
	{"selection_add", l_fm_selection_add},
	{"selection_set", l_fm_selection_set},
	{"selection_get", l_fm_selection_get},
	{"sortby", l_fm_sortby},
	{"top", l_fm_top},
	{"visual_start", l_fm_visual_start},
	{"visual_end", l_fm_visual_end},
	{"visual_toggle", l_fm_visual_toggle},
	{"updir", l_fm_updir},
	{"up", l_fm_up},
	{"load_get", l_fm_get_load},
	{"load_set", l_fm_load_set},
	{"load_clear", l_fm_load_clear},
	{"cut", l_fm_cut},
	{"copy", l_fm_copy},
	{"check", l_fm_check},
	{"drop_cache", l_fm_drop_cache},
	{"reload", l_fm_reload},
	{"sel", l_fm_sel},
	{"get_height", l_fm_get_height},
	{NULL, NULL}};

/* }}} */

/* fn lib {{{ */

static int l_fn_tokenize(lua_State *L)
{
	const char *string = luaL_optstring(L, 1, "");
	char *buf = malloc((strlen(string) + 1) * sizeof(char));
	uint16_t pos1 = 0, pos2 = 0;
	const char *tok;
	if ((tok = tokenize(string, buf, &pos1, &pos2)))
		lua_pushstring(L, tok);
	lua_newtable(L);
	int i = 1;
	while ((tok = tokenize(string, buf, &pos1, &pos2))) {
		lua_pushstring(L, tok);
		lua_rawseti(L, -2, i++);
	}
	free(buf);
	return 2;
}


static int l_fn_split_last(lua_State *L)
{
	const char *s, *string = luaL_checkstring(L, 1);
	const char *last = string; /* beginning of last token */
	bool esc = false;
	for (s = string; *s != 0; s++) {
		if (*s == '\\') {
			esc = !esc;
		} else {
			if (*s == ' ' && !esc) {
				last = s + 1;
			}
			esc = false;
		}
	}
	lua_pushlstring(L, string, last - string);
	lua_pushstring(L, last);
	return 2;
}


static int l_fn_unquote_space(lua_State *L)
{
	const char *string = luaL_checkstring(L, 1);
	char *buf = malloc((strlen(string) + 1) * sizeof(char));
	char *t = buf;
	for (const char *s = string; *s != 0; s++) {
		if (*s != '\\' || *(s+1) != ' ') {
			*t++ = *s;
		}
	}
	lua_pushlstring(L, buf, t-buf);
	free(buf);
	return 1;
}


static int l_fn_quote_space(lua_State *L)
{
	const char *string = luaL_checkstring(L, 1);
	char *buf = malloc((strlen(string) * 2 + 1) * sizeof(char));
	char *t = buf;
	for (const char *s = string; *s; s++) {
		if (*s == ' ') {
			*t++ = '\\';
		}
		*t++ = *s;
	}
	lua_pushlstring(L, buf, t-buf);
	free(buf);
	return 1;
}


static int l_fn_getpid(lua_State *L)
{
	lua_pushinteger(L, getpid());
	return 1;
}


static int l_fn_getcwd(lua_State *L)
{
	const char *cwd = getcwd(NULL, 0);
	lua_pushstring(L, cwd ? cwd : "");
	return 1;
}


static int l_fn_getpwd(lua_State *L)
{
	const char *pwd = getenv("PWD");
	lua_pushstring(L, pwd ? pwd : "");
	return 1;
}


static const struct luaL_Reg fn_lib[] = {
	{"split_last", l_fn_split_last},
	{"quote_space", l_fn_quote_space},
	{"unquote_space", l_fn_unquote_space},
	{"tokenize", l_fn_tokenize},
	{"getpid", l_fn_getpid},
	{"getcwd", l_fn_getcwd},
	{"getpwd", l_fn_getpwd},
	{NULL, NULL}};

/* }}} */

void lua_run_hook(lua_State *L, const char *hook)
{
	lua_getglobal(L, "lfm");
	lua_pushliteral(L, "run_hook");
	lua_gettable(L, -2);
	lua_pushstring(L, hook);
	if (lua_pcall(L, 1, 0, 0))
		ui_error(ui, "run_hook: %s", lua_tostring(L, -1));
}


void lua_eval(lua_State *L, const char *expr)
{
	log_debug("eval %s", expr);
	lua_getglobal(L, "lfm");
	lua_pushliteral(L, "eval");
	lua_gettable(L, -2);
	lua_pushstring(L, expr);
	if (lua_pcall(L, 1, 0, 0))
		ui_error(ui, "eval: %s", lua_tostring(L, -1));
}


void lua_handle_key(lua_State *L, input_t in)
{
	if (in == CTRL('q')) {
		app_quit(app);
		return;
	}
	const char *prefix = cmdline_prefix_get(&ui->cmdline);
	if (!maps.cur) {
		maps.cur = prefix ? maps.cmd : maps.normal;
		cvector_set_size(maps.seq, 0);
	}
	maps.cur = trie_find_child(maps.cur, in);
	if (prefix) {
		if (!maps.cur) {
			if (iswprint(in)) {
				char buf[8];
				int n = wctomb(buf, in);
				if (n < 0)
					n = 0; // invalid character or borked shift/ctrl/alt
				buf[n] = '\0';
				if (cmdline_insert(&ui->cmdline, buf))
					ui_redraw(ui, REDRAW_CMDLINE);
			}
			lua_getglobal(L, "lfm");
			if (lua_type(L, -1) == LUA_TTABLE) {
				lua_getfield(L, -1, "modes");
				if (lua_type(L, -1) == LUA_TTABLE) {
					lua_getfield(L, -1, prefix);
					if (lua_type(L, -1) == LUA_TTABLE) {
						lua_getfield(L, -1, "change");
						if (lua_type(L, -1) == LUA_TFUNCTION) {
							lua_pcall(L, 0, 0, 0);
						}
					}
				}
			}
		} else {
			if (maps.cur->keys) {
				lua_pushlightuserdata(L, (void *)maps.cur);
				lua_gettable(L, LUA_REGISTRYINDEX);
				maps.cur = NULL;
				if (lua_pcall(L, 0, 0, 0))
					ui_error(ui, "handle_key: %s", lua_tostring(L, -1));
			} else {
				// ???
			}
		}
	} else {
		// prefix == NULL
		if (in == 27) {
			maps.cur = NULL;
			ui->message = false;
			ui_cmd_clear(ui);
			nohighlight(ui);
			fm_selection_visual_stop(fm);
			fm_selection_clear(fm);
			fm_load_clear(fm);
			ui_redraw(ui, REDRAW_FM);
			return;
		}
		if (!maps.cur) {
			cvector_push_back(maps.seq, in);
			cvector_set_size(maps.str, 0);
			for (size_t i = 0; i < cvector_size(maps.seq); i++) {
				for (const char *s = input_to_key_name(maps.seq[i]); *s; s++)
					cvector_push_back(maps.str, *s);
			}
			cvector_push_back(maps.str, 0);
			ui_error(ui, "no such map: %s", maps.str);
			log_debug("key: %d, id: %d, shift: %d, ctrl: %d alt %d", in, ID(in), ISSHIFT(in), ISCTRL(in), ISALT(in));
			ui_showmenu(ui, NULL);
			return;
		}
		if (maps.cur->keys) {
			ui_showmenu(ui, NULL);
			lua_pushlightuserdata(L, (void *)maps.cur);
			lua_gettable(L, LUA_REGISTRYINDEX);
			maps.cur = NULL;
			if (lua_pcall(L, 0, 0, 0)) {
				ui_error(ui, "handle_key: %s", lua_tostring(L, -1));
				if (in == 'q')
					app_quit(app);
				else if (in == 'r')
					lua_load_file(L, cfg.configpath);
			}
		} else {
			cvector_push_back(maps.seq, in);
			cvector_vector_type(char*) menu = NULL;
			cvector_push_back(menu, strdup("keys\tcommand"));
			trie_collect_leaves(maps.cur, &menu);
			ui_showmenu(ui, menu);
		}
	}
}


bool lua_load_file(lua_State *L, const char *path)
{
	if (luaL_loadfile(L, path) || lua_pcall(L, 0, 0, 0)) {
		ui_error(ui, "loadfile : %s", lua_tostring(L, -1));
		return false;
	}
	return true;
}


int luaopen_lfm(lua_State *L)
{
	log_debug("opening lualfm libs");

	luaL_openlib(L, "lfm", lfm_lib, 1);

	lua_newtable(L); /* lfm.cfg */

	lua_newtable(L); /* lfm.cfg.colors */
	luaL_newmetatable(L, "colors_mt");
	luaL_register(L, NULL, colors_mt);
	lua_setmetatable(L, -2);
	lua_setfield(L, -2, "colors"); /* lfm.cfg.colors = {...} */

	luaL_newmetatable(L, "config_mt");
	luaL_register(L, NULL, config_mt);
	lua_setmetatable(L, -2);

	lua_setfield(L, -2, "config"); /* lfm.config = {...} */

	lua_newtable(L); /* lfm.log */
	luaL_register(L, NULL, log_lib);
	lua_setfield(L, -2, "log"); /* lfm.log = {...} */

	lua_newtable(L); /* lfm.ui */
	luaL_register(L, NULL, ui_lib);
	lua_setfield(L, -2, "ui"); /* lfm.ui = {...} */

	lua_newtable(L); /* lfm.cmd */
	luaL_register(L, NULL, cmd_lib);
	lua_setfield(L, -2, "cmd"); /* lfm.cmd = {...} */

	lua_newtable(L); /* lfm.fm */
	luaL_register(L, NULL, fm_lib);
	lua_setfield(L, -2, "fm"); /* lfm.fm = {...} */

	lua_newtable(L); /* lfm.fn */
	luaL_register(L, NULL, fn_lib);
	lua_setfield(L, -2, "fn"); /* lfm.fn = {...} */

	return 1;
}


void lua_init(lua_State *L, App *_app)
{
	app = _app;
	ui = &_app->ui;
	fm = &_app->fm;

	maps.normal = trie_create();
	maps.cmd = trie_create();
	maps.cur = NULL;
	maps.str = NULL;
	maps.seq = NULL;
	cvector_push_back(maps.str, 0);

	luaL_openlibs(L);
	luaopen_jit(L);
	luaopen_lfm(L);

	lua_newtable(L);
	lua_setfield(L, LUA_REGISTRYINDEX, TABLE_CALLBACKS);
}


void lua_deinit(lua_State *L)
{
	lua_close(L);
	trie_destroy(maps.normal);
	trie_destroy(maps.cmd);
	cvector_free(maps.str);
	cvector_free(maps.seq);
}
