#define _GNU_SOURCE
#include <errno.h>
#include <lauxlib.h>
#include <libgen.h>
#include <lua.h>
#include <luajit.h>
#include <lualib.h>
#include <ncurses.h>
#include <stdlib.h>
#include <notcurses/notcurses.h>
#include <string.h>
#include <wchar.h>

#include "app.h"
#include "async.h"
#include "config.h"
#include "cmdline.h"
#include "cvector.h"
#include "dir.h"
#include "cache.h"
#include "keys.h"
#include "notify.h"
#include "log.h"
#include "lualfm.h"
#include "tokenize.h"
#include "tpool.h"
#include "util.h"

#include "trie.h"

static app_t *app = NULL;

static trie_node_t *cur;
static trie_node_t *maps;
static trie_node_t *cmaps;

static int *seq = NULL;
static char *seq_str = NULL;

int l_map_key(lua_State *L)
{
	const char *desc = NULL;
	if (lua_type(L, 3) == LUA_TTABLE) {
		lua_getfield(L, 3, "desc");
		if (lua_type(L, -1) == LUA_TSTRING) {
			desc = lua_tostring(L, -1);
		}
		lua_pop(L, 1);
	}
	const char *keys = luaL_checkstring(L, 1);
	if (!(lua_type(L, 2) == LUA_TFUNCTION)) {
		luaL_argerror(L, 2, "expected function");
	}
	int buf[strlen(keys)+1];
	trie_node_t *k = trie_insert(maps, keytrans_inv_str(keys, buf), keys, desc);
	lua_pushlightuserdata(L, (void *)k);
	lua_pushvalue(L, 2);
	lua_settable(L, LUA_REGISTRYINDEX);
	return 0;
}

int l_cmap_key(lua_State *L)
{
	const char *desc = NULL;
	if (lua_type(L, 3) == LUA_TTABLE) {
		lua_getfield(L, 3, "desc");
		if (lua_type(L, -1) == LUA_TSTRING) {
			desc = lua_tostring(L, -1);
		}
		lua_pop(L, 1);
	}
	const char *keys = luaL_checkstring(L, 1);
	if (!(lua_type(L, 2) == LUA_TFUNCTION)) {
		luaL_argerror(L, 2, "expected function");
	}
	int buf[strlen(keys)+1];
	trie_node_t *k = trie_insert(cmaps, keytrans_inv_str(keys, buf), keys, desc);
	lua_pushlightuserdata(L, (void *)k);
	lua_pushvalue(L, 2);
	lua_settable(L, LUA_REGISTRYINDEX);
	return 0;
}

void lua_run_hook(lua_State *L, const char *hook)
{
	lua_getglobal(L, "lfm");
	lua_pushliteral(L, "run_hook");
	lua_gettable(L, -2);
	lua_pushstring(L, hook);
	if (lua_pcall(L, 1, 0, 0)) {
		log_error("run_hook: %s", lua_tostring(L, -1));
	}
}

void lua_exec_expr(lua_State *L, app_t *app, const char *cmd)
{
	log_debug("exec_expr %s", cmd);
	lua_getglobal(L, "lfm");
	lua_pushliteral(L, "exec_expr");
	lua_gettable(L, -2);
	lua_pushstring(L, cmd);
	if (lua_pcall(L, 1, 0, 0)) {
		ui_error(&app->ui, "exec_expr: %s", lua_tostring(L, -1));
	}
}

int l_handle_key(lua_State *L)
{
	const char *keys = luaL_checkstring(L, 1);
	int buf[strlen(keys)];
	keytrans_inv_str(keys, buf);
	for (int *i = buf; *i; i++) {
		struct ncinput in = {
			.id = *i,
		};
		lua_handle_key(L, app, &in);
	}
	return 0;
}

void lua_handle_key(lua_State *L, app_t *app, ncinput *in)
{
	int key = in->id;
	if (in->alt) {
		key = ALT(key);
	}
	if (in->ctrl) {
		key = CTRL(key);
	}
	if (key == CTRL('q')) {
		app_quit(app);
		return;
	}
	const char *prefix = cmdline_prefix_get(&app->ui.cmdline);
	if (!cur) {
		cur = prefix ? cmaps : maps;
		cvector_set_size(seq, 0);
	}
	cur = trie_find_child(cur, key);
	if (prefix) {
		if (!cur) {
			char buf[8] = {key, 0}; /* hope that fits */
			int n = wctomb(buf, key);
			if (n < 0) {
				// invalid character
				n = 0;
			}
			buf[n] = '\0';
			ui_cmd_insert(&app->ui, buf);
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
			if (cur->keys) {
				lua_pushlightuserdata(L, (void *)cur);
				lua_gettable(L, LUA_REGISTRYINDEX);
				cur = NULL;
				if (lua_pcall(L, 0, 0, 0)) {
					error("handle_key: %s", lua_tostring(L, -1));
				}
			} else {
				// ???
			}
		}
	}
	if (!prefix) {
		if (key == 27) {
			cur = NULL;
			ui_cmd_clear(&app->ui);
			fm_selection_visual_stop(&app->fm);
			fm_selection_clear(&app->fm);
			fm_load_clear(&app->fm);
			app->ui.redraw.fm = 1;
			return;
		}
		if (!cur) {
			cvector_push_back(seq, key);
			cvector_set_size(seq_str, 0);
			/* TODO: Use a string builder (on 2021-10-29) */
			for (size_t i = 0; i < cvector_size(seq); i++) {
				const char *s = keytrans(seq[i]);
				while (*s) {
					cvector_push_back(seq_str, *s++);
				}
			}
			cvector_push_back(seq_str, 0);
			error("no such map: %s", seq_str);
			ui_showmenu(&app->ui, NULL);
			return;
		}
		if (cur->keys) {
			ui_showmenu(&app->ui, NULL);
			lua_pushlightuserdata(L, (void *)cur);
			lua_gettable(L, LUA_REGISTRYINDEX);
			cur = NULL;
			if (lua_pcall(L, 0, 0, 0)) {
				error("handle_key: %s", lua_tostring(L, -1));
				if (key == 'q') {
					app_quit(app);
				} else if (key == 'r') {
					lua_load_file(L, app, cfg.configpath);
				}
			}
		} else {
			cvector_push_back(seq, key);
			cvector_vector_type(char*) menu = NULL;
			cvector_push_back(menu, strdup("keys\tcommand"));
			trie_collect_leaves(cur, &menu);
			ui_showmenu(&app->ui, menu);
		}
	}
}

bool lua_load_file(lua_State *L, app_t *app, const char *path)
{
	if (luaL_loadfile(L, path) || lua_pcall(L, 0, 0, 0)) {
		ui_error(&app->ui, "loadfile : %s", lua_tostring(L, -1));
		return false;
	}
	return true;
}

static int l_fm_index(lua_State *L)
{
	fm_t *fm = &app->fm;
	const char *key = luaL_checkstring(L, 2);
	if (streq(key, "height")) {
		lua_pushinteger(L, app->fm.height);
		return 1;
	} else if (streq(key, "selection")) {
		lua_createtable(L, fm->selection.len, 0);
		size_t i, j;
		for (i = 0, j = 1; i < cvector_size(fm->selection.files); i++) {
			if (fm->selection.files[i]) {
				lua_pushstring(L, fm->selection.files[i]);
				lua_rawseti(L, -2, j++);
			}
		}
		return 1;
	} else if (streq(key, "current")) {
		/* TODO: current file or current dir (on 2021-07-21) */
		file_t *file;
		if ((file = fm_current_file(&app->fm))) {
			lua_pushstring(L, file->path);
			return 1;
		} else {
			return 0;
		}
	}
	return 0;
}

static int l_config_index(lua_State *L)
{
	const char *key = luaL_checkstring(L, 2);
	if (streq(key, "truncatechar")) {
		char buf[2];
		buf[0] = cfg.truncatechar;
		buf[1] = 0;
		lua_pushstring(L, buf);
		return 1;
	} else if (streq(key, "hidden")) {
		lua_pushboolean(L, cfg.hidden);
		return 1;
	} else if (streq(key, "ratios")) {
		const size_t l = cvector_size(cfg.ratios);
		lua_createtable(L, l, 0);
		size_t i;
		for (i = 0; i < l; i++) {
			lua_pushinteger(L, cfg.ratios[i]);
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
	} else if (streq(key, "configpath")) {
		lua_pushstring(L, cfg.configpath);
		return 1;
	} else if (streq(key, "dircache_size")) {
		lua_pushinteger(L, app->fm.dirs.cache.capacity);
		return 1;
	} else if (streq(key, "previewcache_size")) {
		lua_pushinteger(L, app->ui.preview.cache.capacity);
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
		wchar_t buf[2];
		const char *val = luaL_checkstring(L, 3);
		const char *p = val;
		mbsrtowcs(buf, &p, 1, NULL);
		cfg.truncatechar = buf[0];
		app->ui.redraw.fm = 1;
	} else if (streq(key, "hidden")) {
		bool hidden = lua_toboolean(L, 3);
		fm_hidden_set(&app->fm, hidden);
		app->ui.redraw.fm = 1;
	} else if (streq(key, "ratios")) {
		const int l = lua_objlen(L, 3);
		log_debug("%d", l);
		if (l == 0) {
			luaL_argerror(L, 3, "no ratios given");
		}
		int *ratios = malloc(sizeof(int) * l);
		int i;
		for (i = 0; i < l; i++) {
			lua_rawgeti(L, 3, i + 1);
			ratios[i] = lua_tointeger(L, -1);
			if (ratios[i] <= 0) {
				free(ratios);
				luaL_error(L, "ratio must be non-negative");
			}
			lua_pop(L, 1);
		}
		config_ratios_set(l, ratios);
		fm_recol(&app->fm);
		ui_recol(&app->ui);
		erase();
		refresh();
		app->ui.redraw.fm = 1;
		free(ratios);
	} else if (streq(key, "scrolloff")) {
		cfg.scrolloff = max(luaL_checkinteger(L, 3), 0);
		return 0;
	} else if (streq(key, "preview")) {
		cfg.preview = lua_toboolean(L, 3);
		fm_recol(&app->fm);
		app->ui.redraw.fm = 1;
		return 0;
	} else if (streq(key, "previewer")) {
		if (lua_isnoneornil(L, 3)) {
			cfg.previewer = NULL;
		} else {
			const char *s = luaL_checkstring(L, 3);
			cfg.previewer = s[0] != 0 ? strdup(s) : NULL;
		}
		/* TODO: purge preview cache (on 2021-08-10) */
		return 0;
	} else if (streq(key, "dircache_size")) {
		int capacity = luaL_checkinteger(L, 3);
		if (capacity < 0) {
			luaL_argerror(L, 3, "size must be non-negative");
		}
		cache_resize(&app->fm.dirs.cache, capacity);
	} else if (streq(key, "previewcache_size")) {
		int capacity = luaL_checkinteger(L, 3);
		if (capacity < 0) {
			luaL_argerror(L, 3, "size must be non-negative");
		}
		cache_resize(&app->ui.preview.cache, capacity);
	} else {
		luaL_error(L, "unexpected key %s", key);
	}
	return 0;
}

static int l_log_debug(lua_State *L)
{
	const char *msg = luaL_checkstring(L, 1);
	log_debug(msg);
	return 0;
}

static int l_log_info(lua_State *L)
{
	const char *msg = luaL_checkstring(L, 1);
	log_info(msg);
	return 0;
}

static int l_log_trace(lua_State *L)
{
	const char *msg = luaL_checkstring(L, 1);
	log_trace(msg);
	return 0;
}

static int l_quit(lua_State *L)
{
	(void) L;
	app_quit(app);
	return 0;
}

static int l_error(lua_State *L)
{
	const char *msg = luaL_checkstring(L, 1);
	ui_error(&app->ui, msg);
	return 0;
}

static int l_ui_clear(lua_State *L)
{
	(void) L;
	ui_clear(&app->ui);
	return 0;
}

static int l_ui_menu(lua_State *L)
{
	const int l = lua_gettop(L);
	const char *s;
	cvector_vector_type(char*) menubuf = NULL;
	cvector_grow(menubuf, l);
	for (int i = 0; i < l; i++) {
		s = luaL_checkstring(L, i + 1);
		cvector_push_back(menubuf, strdup(s));
	}
	ui_showmenu(&app->ui, menubuf);
	return 0;
}

static int l_ui_draw(lua_State *L)
{
	(void) L;
	app->ui.redraw.fm = 1;
	return 0;
}

static unsigned read_channel(lua_State *L, int ind)
{
	switch(lua_type(L, ind))
	{
		case LUA_TSTRING:
			return NCCHANNEL_INITIALIZER_PALINDEX(lua_tointeger(L, ind));
			break;
		case LUA_TNUMBER:
			return NCCHANNEL_INITIALIZER_HEX(lua_tointeger(L, ind));
			break;
		default:
			luaL_typerror(L, ind, "string or number");
			return 0;
	}
}

static unsigned long read_color_pair(lua_State *L, int ind)
{
	unsigned fg, bg;
	ncchannel_set_default(&fg);
	ncchannel_set_default(&bg);
	lua_getfield(L, ind, "fg");
	if (!lua_isnoneornil(L, -1)) {
		fg = read_channel(L, -1);
	}
	lua_pop(L, 1);
	lua_getfield(L, ind, "bg");
	if (!lua_isnoneornil(L, -1)) {
		bg = read_channel(L, -1);
	}
	lua_pop(L, 1);
	return ncchannels_combine(fg, bg);
}

static int l_colors_newindex(lua_State *L)
{
	const char *key = luaL_checkstring(L, 2);
	if (streq(key, "copy")) {
		if (lua_istable(L, 3)) {
			cfg.colors.copy = read_color_pair(L, 3);
		}
	} else if (streq(key, "delete")) {
		if (lua_istable(L, 3)) {
			cfg.colors.delete = read_color_pair(L, 3);
		}
	} else if (streq(key, "dir")) {
		if (lua_istable(L, 3)) {
			cfg.colors.dir = read_color_pair(L, 3);
		} else {
			log_debug("nop");
		}
	} else if (streq(key, "exec")) {
		if (lua_istable(L, 3)) {
			cfg.colors.exec = read_color_pair(L, 3);
		}
	} else if (streq(key, "search")) {
		if (lua_istable(L, 3)) {
			cfg.colors.search = read_color_pair(L, 3);
		}
	} else if (streq(key, "normal")) {
		if (lua_istable(L, 3)) {
			cfg.colors.normal = read_color_pair(L, 3);
		}
	} else if (streq(key, "current")) {
		cfg.colors.current = read_channel(L, 3);
	} else if (streq(key, "patterns")) {
		if (lua_istable(L, 3)) {
			for (lua_pushnil(L); lua_next(L, 3); lua_pop(L, 1)) {
				lua_getfield(L, -1, "color");
				unsigned long ch = read_color_pair(L, -1);
				lua_pop(L, 1);

				lua_getfield(L, -1, "ext");
				for (lua_pushnil(L); lua_next(L, -2); lua_pop(L, 1)) {
					config_ext_channel_add(lua_tostring(L, -1), ch);
				}
				lua_pop(L, 1);
			}
		}
	} else {
		luaL_error(L, "unexpected key %s", key);
	}
	app->ui.redraw.fm = 1;
	return 0;
}

static int l_cmd_line_get(lua_State *L)
{
	const char *line = ui_cmdline_get(&app->ui);
	log_debug("%s", line);
	lua_pushstring(L, line);
	return 1;
}

static int l_cmd_line_set(lua_State *L)
{
	const char *line = lua_tostring(L, 1);
	ui_cmdline_set(&app->ui, line);
	return 0;
}

static int l_cmd_clear(lua_State *L)
{
	(void) L;
	ui_cmd_clear(&app->ui);
	return 0;
}

static int l_cmd_delete(lua_State *L)
{
	(void) L;
	ui_cmd_delete(&app->ui);
	return 0;
}

static int l_cmd_delete_right(lua_State *L)
{
	(void) L;
	ui_cmd_delete_right(&app->ui);
	return 0;
}

static int l_cmd_insert(lua_State *L)
{
	ui_cmd_insert(&app->ui, lua_tostring(L, 1));
	return 0;
}

static int l_cmd_left(lua_State *L)
{
	(void) L;
	ui_cmd_left(&app->ui);
	return 0;
}

static int l_cmd_right(lua_State *L)
{
	(void) L;
	ui_cmd_right(&app->ui);
	return 0;
}

static int l_cmd_home(lua_State *L)
{
	(void) L;
	ui_cmd_home(&app->ui);
	return 0;
}

static int l_cmd_end(lua_State *L)
{
	(void) L;
	ui_cmd_end(&app->ui);
	return 0;
}

static int l_cmd_prefix_set(lua_State *L)
{
	ui_cmd_prefix_set(&app->ui, luaL_optstring(L, 1, ""));
	return 0;
}

static int l_cmd_prefix_get(lua_State *L)
{
	const char *prefix = cmdline_prefix_get(&app->ui.cmdline);
	lua_pushstring(L, prefix ? prefix : "");
	return 1;
}

static int l_fm_up(lua_State *L)
{
	if (fm_up(&app->fm, luaL_optint(L, 1, 1)))
		app->ui.redraw.fm = 1;
	return 0;
}

static int l_fm_down(lua_State *L)
{
	if (fm_down(&app->fm, luaL_optint(L, 1, 1)))
		app->ui.redraw.fm = 1;
	return 0;
}

static int l_fm_top(lua_State *L)
{
	(void) L;
	if (fm_top(&app->fm))
		app->ui.redraw.fm = 1;
	return 0;
}

static int l_fm_bot(lua_State *L)
{
	(void) L;
	if (fm_bot(&app->fm))
		app->ui.redraw.fm = 1;
	return 0;
}

static int l_fm_updir(lua_State *L)
{
	(void) L;
	fm_updir(&app->fm);
	ui_search_nohighlight(&app->ui);
	app->ui.redraw.fm = 1;
	return 0;
}

static int l_fm_open(lua_State *L)
{
	file_t *file;
	fm_t *fm = &app->fm;
	if (!(file = fm_open(&app->fm))) {
		/* changed directory */
		app->ui.redraw.fm = 1;
		ui_search_nohighlight(&app->ui);
		return 0;
	} else {
		if (cfg.selfile) {
			/* lastdir is written in main */
			fm_selection_write(fm, cfg.selfile);
			app_quit(app);
			return 0;
		} else {
			lua_pushstring(L, file->path);
			return 1;
		}
	}
}

static int l_fm_current_dir(lua_State *L)
{
	const dir_t *dir = fm_current_dir(&app->fm);
	lua_newtable(L);
	lua_pushstring(L, dir->path);
	lua_setfield(L, -2, "path");
	lua_pushstring(L, dir->name);
	lua_setfield(L, -2, "name");

	lua_newtable(L);
	int i;
	for (i = 0; i < dir->len; i++) {
		lua_pushstring(L, dir->files[i]->path);
		lua_rawseti(L, -2, i+1);
	}
	lua_setfield(L, -2, "files");

	return 1;
}

static int l_sel_visual_start(lua_State *L)
{
	(void) L;
	fm_selection_visual_start(&app->fm);
	app->ui.redraw.fm = 1;
	return 0;
}

static int l_sel_visual_end(lua_State *L)
{
	(void) L;
	fm_selection_visual_stop(&app->fm);
	app->ui.redraw.fm = 1;
	return 0;
}

static int l_sel_visual_toggle(lua_State *L)
{
	(void) L;
	fm_selection_visual_toggle(&app->fm);
	app->ui.redraw.fm = 1;
	return 0;
}

static int l_shell_pre(lua_State *L)
{
	(void) L;
	kbblocking(true);
	return 0;
}

static int l_shell_post(lua_State *L)
{
	(void) L;
	kbblocking(false);
	ui_clear(&app->ui);
	return 0;
}

static int l_sortby(lua_State *L)
{
	const int l = lua_gettop(L);
	const char *op;
	dir_t *dir = fm_current_dir(&app->fm);
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
	dir_sort(dir);
	app->ui.redraw.fm = 1;
	return 0;
}

static int l_selection_toggle_current(lua_State *L)
{
	(void) L;
	fm_selection_toggle_current(&app->fm);
	return 0;
}

static int l_selection_add(lua_State *L)
{
	fm_selection_add_file(&app->fm, luaL_checkstring(L, 1));
	return 0;
}

static int l_selection_set(lua_State *L)
{
	cvector_vector_type(char*) selection = NULL;
	if (lua_istable(L, -1)) {
		for (lua_pushnil(L); lua_next(L, -2); lua_pop(L, 1)) {
			cvector_push_back(selection, strdup(luaL_checkstring(L, -1)));
		}
	}
	fm_selection_set(&app->fm, selection);
	return 0;
}

static int l_selection_clear(lua_State *L)
{
	(void) L;
	fm_selection_clear(&app->fm);
	app->ui.redraw.fm = 1;
	return 0;
}

static int l_selection_reverse(lua_State *L)
{
	(void) L;
	fm_selection_reverse(&app->fm);
	app->ui.redraw.fm = 1;
	return 0;
}

static int l_fm_chdir(lua_State *L)
{
	const char *path = lua_tostring(L, 1);
	ui_search_nohighlight(&app->ui);
	lua_run_hook(L, "ChdirPre");
	if (fm_chdir(&app->fm, path, true)) {
		lua_run_hook(L, "ChdirPost");
	}
	app->ui.redraw.fm = 1;
	return 0;
}

static int l_fm_get_load(lua_State *L)
{
	size_t i;
	fm_t *fm = &app->fm;
	switch (fm->load.mode) {
		case MODE_MOVE:
			lua_pushstring(L, "move");
			break;
		case MODE_COPY:
			lua_pushstring(L, "copy");
			break;
	}
	lua_createtable(L, cvector_size(fm->load.files), 0);
	for (i = 0; i < cvector_size(fm->load.files); i++) {
		lua_pushstring(L, fm->load.files[i]);
		lua_rawseti(L, -2, i+1);
	}
	return 2;
}

static int l_fm_load_set(lua_State *L)
{
	int i;
	fm_t *fm = &app->fm;
	log_debug("l_fm_load_set %p %p %p", app, &app->ui, fm);
	const char *mode = luaL_checkstring(L, 1);
	fm_load_clear(fm);
	if (streq(mode, "move")) {
		fm->load.mode = MODE_MOVE;
	} else {
		// mode == "copy" is the safe alternative
		fm->load.mode = MODE_COPY;
	}
	const int l = lua_objlen(L, 2);
	for (i = 0; i < l; i++) {
		lua_rawgeti(L, 2, i + 1);
		cvector_push_back(fm->load.files, strdup(lua_tostring(L, -1)));
		lua_pop(L, 1);
	}
	return 0;
}

static int l_fm_load_clear(lua_State *L)
{
	(void) L;
	fm_load_clear(&app->fm);
	return 0;
}

static int l_fm_copy(lua_State *L)
{
	(void) L;
	fm_copy(&app->fm);
	app->ui.redraw.fm = 1;
	return 0;
}

static int l_fm_cut(lua_State *L)
{
	(void) L;
	fm_cut(&app->fm);
	app->ui.redraw.fm = 1;
	return 0;
}

static int l_tokenize(lua_State *L)
{
	const char *string = luaL_optstring(L, 1, "");
	char buf[strlen(string) + 1];
	int pos1 = 0, pos2 = 0;
	const char *tok;
	if ((tok = tokenize(string, buf, &pos1, &pos2))) {
		lua_pushstring(L, tok);
	}
	lua_newtable(L);
	int i = 1;
	while ((tok = tokenize(string, buf, &pos1, &pos2))) {
		lua_pushstring(L, tok);
		lua_rawseti(L, -2, i++);
	}
	return 2;
}

static int l_fm_filter_get(lua_State *L)
{
	lua_pushstring(L, fm_filter_get(&app->fm));
	return 1;
}

static int l_filter(lua_State *L)
{
	const char *filter = lua_tostring(L, 1);
	fm_filter(&app->fm, filter);
	app->ui.redraw.fm = 1;
	return 0;
}

static int l_echo(lua_State *L)
{
	ui_echom(&app->ui, luaL_optstring(L, 1, ""));
	return 0;
}

static int l_fm_mark_load(lua_State *L)
{
	const char *b = lua_tostring(L, 1);
	fm_mark_load(&app->fm, b[0]);
	app->ui.redraw.fm = 1;
	return 0;
}

static int l_history_append(lua_State *L)
{
	const char *line = luaL_checkstring(L, 1);
	ui_history_append(&app->ui, line);
	return 0;
}

static int l_history_prev(lua_State *L)
{
	const char *line = ui_history_prev(&app->ui);
	if (!line) {
		return 0;
	}
	lua_pushstring(L, line);
	return 1;
}

static int l_history_next(lua_State *L)
{
	const char *line;
	if (!(line = ui_history_next(&app->ui))) {
		return 0;
	}
	lua_pushstring(L, line);
	return 1;
}

static int l_ui_messages(lua_State *L)
{
	size_t i;

	ui_t *ui = &app->ui;
	lua_newtable(L);
	for (i = 0; i < cvector_size(ui->messages); i++) {
		lua_pushstring(L, ui->messages[i]);
		lua_rawseti(L, -2, i+1);
	}
	return 1;
}

static int l_crash(lua_State *L)
{
	free(L);
	return 0;
}

static int l_fm_drop_cache(lua_State *L)
{
	(void) L;
	fm_drop_cache(&app->fm);
	ui_drop_cache(&app->ui);
	return 0;
}

static int l_fm_watchers(lua_State *L)
{
	(void) L;
	log_watchers();
	return 0;
}

static int l_fm_check(lua_State *L)
{
	(void) L;
	fm_t *fm = &app->fm;
	dir_t *d = fm_current_dir(fm);
	if (!dir_check(d)) {
		async_dir_load(d->path);
	}
	return 0;
}

static int l_fm_sel(lua_State *L)
{
	fm_move_to(&app->fm, luaL_checkstring(L, 1));
	app->ui.redraw.fm = 1;
	return 0;
}

static int l_search(lua_State *L)
{
	ui_t *ui = &app->ui;
	if (!lua_toboolean(L, 1)) {
		ui_search_nohighlight(ui);
	} else {
		const char *search = luaL_checkstring(L, 1);
		if (search[0] == 0) {
			ui_search_nohighlight(ui);
		} else {
			ui_search_highlight(ui, search, true);
		}
	}
	app->ui.redraw.fm = 1;
	return 0;
}

static int l_search_backwards(lua_State *L)
{
	ui_t *ui = &app->ui;
	if (!lua_toboolean(L, 1)) {
		ui_search_nohighlight(ui);
	} else {
		const char *search = luaL_checkstring(L, 1);
		if (search[0] == 0) {
			ui_search_nohighlight(ui);
		} else {
			ui_search_highlight(ui, search, false);
		}
	}
	app->ui.redraw.fm = 1;
	return 0;
}

static int l_search_next_forward(lua_State *L)
{
	dir_t *dir;
	fm_t *fm = &app->fm;
	ui_t *ui = &app->ui;

	if (!(dir = fm_current_dir(fm)) || !ui->search.string) {
		return 0;
	}
	ui_search_highlight(ui, NULL, true);
	int start = dir->ind;
	if (!lua_toboolean(L, 1)) {
		start++;
	}
	for (int i = 0; i < dir->len; i++) {
		if (strcasestr(dir->files[(start + i) % dir->len]->name,
					ui->search.string)) {
			if ((start + i) % dir->len < dir->ind) {
				fm_up(fm, dir->ind - (start + i) % dir->len);
			} else {
				fm_down(fm,
						(start + i) % dir->len - dir->ind);
			}
			app->ui.redraw.fm = 1;
			break;
		}
	}
	return 0;
}

static int l_search_next_backwards(lua_State *L)
{
	dir_t *dir;
	fm_t *fm = &app->fm;
	ui_t *ui = &app->ui;

	if (!(dir = fm_current_dir(fm)) || !ui->search.string) {
		return 0;
	}
	ui_search_highlight(ui, NULL, false);
	int start = dir->ind;
	if (!lua_toboolean(L, 1)) {
		start--;
	}
	for (int i = 0; i < dir->len; i++) {
		if (strcasestr(
					dir->files[(dir->len + start - i) % dir->len]->name,
					ui->search.string)) {
			if ((dir->len + start - i) % dir->len < dir->ind) {
				fm_up(fm, dir->ind - (dir->len + start - i) %
						dir->len);
			} else {
				fm_down(fm,
						(dir->len + start - i) % dir->len -
						dir->ind);
			}
			app->ui.redraw.fm = 1;
			break;
		}
	}
	return 0;
}

static int l_search_next(lua_State *L)
{
	ui_t *ui = &app->ui;
	if (ui->search.forward) {
		return l_search_next_forward(L);
	} else {
		return l_search_next_backwards(L);
	}
}

static int l_search_prev(lua_State *L)
{
	ui_t *ui = &app->ui;
	if (ui->search.forward) {
		return l_search_next_backwards(L);
	} else {
		return l_search_next_forward(L);
	}
}

static int l_find(lua_State *L)
{
	dir_t *dir;
	fm_t *fm = &app->fm;

	if (!(dir = fm_current_dir(fm))) {
		return 0;
	}
	const char *prefix = luaL_checkstring(L, 1);
	int start = dir->ind;
	int nmatches = 0;
	for (int i = 0; i < dir->len; i++) {
		if (hascaseprefix(dir->files[(start + i) % dir->len]->name,
					prefix)) {
			if (nmatches == 0) {
				if ((start + i) % dir->len < dir->ind) {
					fm_up(fm, dir->ind -
							(start + i) % dir->len);
				} else {
					fm_down(fm, (start + i) % dir->len -
							dir->ind);
				}
				app->ui.redraw.fm = 1;
			}
			nmatches++;
		}
	}
	lua_pushboolean(L, nmatches == 1);
	return 1;
}

static int l_getpid(lua_State *L)
{
	lua_pushinteger(L, getpid());
	return 1;
}

static int l_timeout(lua_State *L)
{
	const int dur = luaL_checkinteger(L, 1);
	if (dur > 0) {
		timeout_set(dur);
	}
	return 0;
}

static const struct luaL_Reg lfm_lib[] = {
	{"map", l_map_key},
	{"cmap", l_cmap_key},
	{"handle_key", l_handle_key},
	{"getpid", l_getpid},
	{"timeout", l_timeout},
	{"find", l_find},
	{"search", l_search},
	{"search_back", l_search_backwards},
	{"search_next", l_search_next},
	{"search_prev", l_search_prev},
	{"echo", l_echo},
	{"crash", l_crash},
	{"echo", l_echo},
	{"error", l_error},
	{"quit", l_quit},
	{"shell_post", l_shell_post},
	{"shell_pre", l_shell_pre},
	{"tokenize", l_tokenize},
	{NULL, NULL}};

static const struct luaL_Reg fm_lib[] = {
	{"bottom", l_fm_bot},
	{"chdir", l_fm_chdir},
	{"down", l_fm_down},
	{"filter", l_filter},
	{"getfilter", l_fm_filter_get},
	{"mark_load", l_fm_mark_load},
	{"open", l_fm_open},
	{"current_dir", l_fm_current_dir},
	{"selection_clear", l_selection_clear},
	{"selection_reverse", l_selection_reverse},
	{"selection_toggle", l_selection_toggle_current},
	{"selection_add", l_selection_add},
	{"selection_set", l_selection_set},
	{"sortby", l_sortby},
	{"top", l_fm_top},
	{"visual_start", l_sel_visual_start},
	{"visual_end", l_sel_visual_end},
	{"visual_toggle", l_sel_visual_toggle},
	{"updir", l_fm_updir},
	{"up", l_fm_up},
	{"load_get", l_fm_get_load},
	{"load_set", l_fm_load_set},
	{"load_clear", l_fm_load_clear},
	{"cut", l_fm_cut},
	{"copy", l_fm_copy},
	{"check", l_fm_check},
	{"drop_cache", l_fm_drop_cache},
	{"sel", l_fm_sel},
	{"debug_watchers", l_fm_watchers},
	{NULL, NULL}};

static const struct luaL_Reg fm_mt[] = {{"__index", l_fm_index}, {NULL, NULL}};

static const struct luaL_Reg cmd_lib[] = {{"clear", l_cmd_clear},
	{"delete", l_cmd_delete},
	{"delete_right", l_cmd_delete_right},
	{"_end", l_cmd_end},
	{"getline", l_cmd_line_get},
	{"getprefix", l_cmd_prefix_get},
	{"home", l_cmd_home},
	{"insert", l_cmd_insert},
	{"left", l_cmd_left},
	{"right", l_cmd_right},
	{"setline", l_cmd_line_set},
	{"setprefix", l_cmd_prefix_set},
	{NULL, NULL}};

static const struct luaL_Reg ui_lib[] = {{"clear", l_ui_clear},
	{"history_append", l_history_append},
	{"history_next", l_history_next},
	{"history_prev", l_history_prev},
	{"menu", l_ui_menu},
	{"draw", l_ui_draw},
	{"messages", l_ui_messages},
	{NULL, NULL}};

static const struct luaL_Reg log_lib[] = {{"debug", l_log_debug},
	{"info", l_log_info},
	{"trace", l_log_trace},
	{NULL, NULL}};

static const struct luaL_Reg config_mt[] = {
	{"__index", l_config_index}, {"__newindex", l_config_newindex}, {NULL, NULL}};

static const struct luaL_Reg colors_mt[] = {
	{"__newindex", l_colors_newindex}, {NULL, NULL}};

int luaopen_lfm(lua_State *L)
{
	log_debug("opening lualfm libs");

	luaL_openlib(L, "lfm", lfm_lib, 1);

	lua_newtable(L);	       /* cfg */

	lua_newtable(L); /* ui.colors */
	luaL_newmetatable(L, "colors_mt"); /* metatable for the config table */
	luaL_register(L, NULL, colors_mt);
	lua_setmetatable(L, -2);
	lua_setfield(L, -2, "colors"); /* lfm.ui = {...} */

	luaL_newmetatable(L, "config_mt"); /* metatable for the config table */
	luaL_register(L, NULL, config_mt);
	lua_setmetatable(L, -2);

	lua_setfield(L, -2, "config"); /* lfm.cfg = {...} */

	lua_newtable(L); /* lfm.log */
	luaL_register(L, NULL, log_lib);
	lua_setfield(L, -2, "log"); /* lfm.log = {...} */

	lua_newtable(L); /* lfm.ui */
	luaL_register(L, NULL, ui_lib);
	lua_setfield(L, -2, "ui"); /* lfm.ui = {...} */

	lua_newtable(L); /* lfm.cmd */
	luaL_register(L, NULL, cmd_lib);
	lua_setfield(L, -2, "cmd"); /* lfm.cmd = {...} */

	lua_newtable(L);	       /* lfm.fm */
	luaL_register(L, NULL, fm_lib);
	luaL_newmetatable(L, "mtNav");
	luaL_register(L, NULL, fm_mt);
	lua_setmetatable(L, -2);
	lua_setfield(L, -2, "fm"); /* lfm.fm = {...} */

	return 1;
}

void lua_init(lua_State *L, app_t *_app)
{
	app = _app;

	maps = trie_new();
	cmaps = trie_new();
	cvector_push_back(seq_str, 0);

	luaL_openlibs(L);
	luaopen_jit(L);
	luaopen_lfm(L);
}

void lua_deinit(lua_State *L)
{
	lua_close(L);
	trie_destroy(maps);
	trie_destroy(cmaps);
	cvector_free(seq_str);
	cvector_free(seq);
}
