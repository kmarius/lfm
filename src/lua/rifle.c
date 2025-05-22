#include "../log.h"
#include "../path.h"
#include "../util.h"

#include "../stc/cstr.h"
#include "../stc/zsview.h"
#include "util.h"

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <pcre.h>

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/limits.h>
#include <sys/stat.h>
#include <unistd.h>

#define RIFLE_META "Lfm.Rifle.Meta"

// arbitrary, max binary name length for `has`
#define EXECUTABLE_MAX 256

#define BUFSIZE 4096
#define DELIM_CONDITION ","
#define DELIM_COMMAND " = "

struct FileInfo;
struct Condition;

typedef bool(check_fn)(struct Condition *, const struct FileInfo *);

typedef struct Condition {
  bool negate;
  cstr arg;
  pcre *pcre;
  pcre_extra *pcre_extra;
  check_fn *check;
} Condition;

static inline void condition_drop(Condition *self) {
  if (self->pcre_extra) {
    pcre_free(self->pcre);
    pcre_free(self->pcre_extra);
  } else {
    cstr_drop(&self->arg);
  }
}

#define i_TYPE conditions, Condition
#define i_keydrop condition_drop
#define i_no_clone
#include "../stc/vec.h"

typedef struct Rule {
  conditions conditions;
  cstr command;
  cstr label;
  int number;
  bool has_mime;
  bool flag_fork;
  bool flag_term;
  bool flag_esc;
  bool flag_lfm;
} Rule;

static inline void rule_drop(Rule *self) {
  conditions_drop(&self->conditions);
  cstr_drop(&self->label);
  cstr_drop(&self->command);
}

#define i_TYPE rules, Rule
#define i_keydrop rule_drop
#define i_no_clone
#include "../stc/vec.h"

typedef struct FileInfo {
  zsview file;
  zsview path;
  zsview mime;
} FileInfo;

typedef struct {
  cstr config_file;
  rules rules;
} Rifle;

static inline Condition condition_create(check_fn *f, const char *arg,
                                         bool negate) {
  Condition cond = {0};
  cond.check = f;
  cond.arg = arg ? cstr_from(arg) : cstr_init();
  cond.negate = negate;
  return cond;
}

static inline void rule_set_flags(Rule *r, const char *flags) {
  for (const char *f = flags; *f; f++) {
    switch (*f) {
    case 'f':
      r->flag_fork = true;
      break;
    case 't':
      r->flag_term = true;
      break;
    case 'e':
      r->flag_esc = true;
      break;
    case 'l':
      r->flag_lfm = true;
      break;
    }
  }
  for (const char *f = flags; *f; f++) {
    switch (*f) {
    case 'F':
      r->flag_fork = false;
      break;
    case 'T':
      r->flag_term = false;
      break;
    case 'E':
      r->flag_esc = false;
      break;
    case 'L':
      r->flag_lfm = false;
      break;
    }
  }
}

static inline bool re_match(pcre *re, pcre_extra *re_extra, zsview string) {
  int substr_vec[32];
  return pcre_exec(re, re_extra, string.str, string.size, 0, 0, substr_vec,
                   30) >= 0;
}

static bool check_fn_file(Condition *cond, const FileInfo *info) {
  struct stat path_stat;
  if (stat(info->file.str, &path_stat) == -1) {
    return cond->negate;
  }
  return S_ISREG(path_stat.st_mode) != cond->negate;
}

static bool check_fn_dir(Condition *cond, const FileInfo *info) {
  struct stat statbuf;
  if (stat(info->file.str, &statbuf) != 0) {
    return cond->negate;
  }
  return S_ISDIR(statbuf.st_mode) != cond->negate;
}

// not sure if this even works from within lfm
static bool check_fn_term(Condition *cond, const FileInfo *info) {
  (void)info;
  return (isatty(0) && isatty(1) && isatty(2)) != cond->negate;
}

static bool check_fn_env(Condition *cond, const FileInfo *info) {
  (void)info;
  const char *val = getenv(cstr_str(&cond->arg));
  return (val && *val) != cond->negate;
}

static bool check_fn_else(Condition *cond, const FileInfo *info) {
  (void)info;
  return !cond->negate;
}

/* TODO: log errors (on 2022-10-15) */
static inline Condition condition_create_re(check_fn *f, const char *re,
                                            bool negate) {
  const char *pcre_error_str;
  int pcre_error_offset;
  Condition c = condition_create(f, NULL, negate);
  c.pcre = pcre_compile(re, 0, &pcre_error_str, &pcre_error_offset, NULL);
  if (!c.pcre) {
    return (Condition){0};
  }
  c.pcre_extra = pcre_study(c.pcre, 0, &pcre_error_str);
  if (pcre_error_str) {
    pcre_free(c.pcre);
    return (Condition){0};
  }
  return c;
}

static bool check_fn_path(Condition *cond, const FileInfo *info) {
  return re_match(cond->pcre, cond->pcre_extra, info->path) != cond->negate;
}

static bool check_fn_mime(Condition *cond, const FileInfo *info) {
  return re_match(cond->pcre, cond->pcre_extra, info->mime) != cond->negate;
}

static bool check_fn_name(Condition *cond, const FileInfo *info) {
  const char *ptr = strrchr(info->file.str, '/');
  if (ptr) {
    int pos = ptr - info->file.str;
    return re_match(cond->pcre, cond->pcre_extra,
                    zsview_from_pos(info->file, pos)) != cond->negate;
  } else {
    return re_match(cond->pcre, cond->pcre_extra, info->file) != cond->negate;
  }
}

static bool check_fn_match(Condition *cond, const FileInfo *info) {
  return re_match(cond->pcre, cond->pcre_extra, info->file) != cond->negate;
}

static bool check_fn_has(Condition *cond, const FileInfo *info) {
  (void)info;
  char cmd[EXECUTABLE_MAX];
  snprintf(cmd, sizeof cmd, "command -v \"%s\" >/dev/null 2>&1",
           cstr_str(&cond->arg));
  return !system(cmd) != cond->negate;
}

static inline Condition condition_create_re_name(const char *arg, bool negate) {
  return condition_create_re(check_fn_name, arg, negate);
}

static inline Condition condition_create_re_path(const char *arg, bool negate) {
  return condition_create_re(check_fn_path, arg, negate);
}

static inline Condition condition_create_re_ext(const char *arg, bool negate) {
  char buf[512];
  snprintf(buf, sizeof buf - 1, "\\.(%s)$", arg);
  Condition cond = condition_create_re(check_fn_name, buf, negate);
  return cond;
}

static inline Condition condition_create_re_mime(const char *arg, bool negate) {
  return condition_create_re(check_fn_mime, arg, negate);
}

static inline Condition condition_create_re_match(const char *arg,
                                                  bool negate) {
  return condition_create_re(check_fn_match, arg, negate);
}

static inline char *split_command(char *s) {
  if ((s = strstr(s, DELIM_COMMAND)) == NULL) {
    return NULL;
  }
  *s = '\0';
  return trim(s + 3);
}

static inline bool is_comment_or_whitespace(char *s) {
  while (isspace(*s)) {
    s++;
  }
  return *s == '#' || *s == '\0';
}

static inline bool rule_add_condition(Rule *self, char *cond_str) {
  if (*cond_str == 0) {
    return true;
  }

  cond_str = rtrim(cond_str);

  bool negate = false;
  char *arg, *func = strtok_r(cond_str, " \t", &cond_str);
  if (func[0] == '!') {
    negate = true;
    func++;
  }

  Condition cond = {0};

  if (streq(func, "file")) {
    cond = condition_create(check_fn_file, NULL, negate);
  } else if (streq(func, "directory")) {
    cond = condition_create(check_fn_dir, NULL, negate);
  } else if (streq(func, "terminal")) {
    cond = condition_create(check_fn_term, NULL, negate);
  } else if (streq(func, "X")) {
    cond = condition_create(check_fn_env, "DISPLAY", negate);
  } else if (streq(func, "W")) {
    cond = condition_create(check_fn_env, "WAYLAND_DISPLAY", negate);
  } else if (streq(func, "else")) {
    cond = condition_create(check_fn_else, NULL, negate);
  } else {
    if ((arg = strtok_r(cond_str, "\0", &cond_str)) == NULL) {
      return false;
    }

    if (streq(func, "label")) {
      self->label = cstr_from(arg);
    } else if (streq(func, "number")) {
      /* TODO: cant distringuish between 0 and invalid number
       * (on 2021-07-27) */
      self->number = atoi(arg);
    } else if (streq(func, "flag")) {
      rule_set_flags(self, arg);
    } else if (streq(func, "ext")) {
      cond = condition_create_re_ext(arg, negate);
    } else if (streq(func, "path")) {
      cond = condition_create_re_path(arg, negate);
    } else if (streq(func, "mime")) {
      cond = condition_create_re_mime(arg, negate);
      if (!negate) {
        self->has_mime = true;
      }
    } else if (streq(func, "name")) {
      cond = condition_create_re_name(arg, negate);
    } else if (streq(func, "match")) {
      cond = condition_create_re_match(arg, negate);
    } else if (streq(func, "env")) {
      cond = condition_create(check_fn_env, arg, negate);
    } else if (streq(func, "has")) {
      /* could be checked here? some others too */
      cond = condition_create(check_fn_has, arg, negate);
    } else {
      return false;
    }
  }

  if (cond.check) {
    conditions_push(&self->conditions, cond);
  }

  return true;
}

static inline int rule_init(Rule *self, char *str, const char *command) {
  if (command == NULL) {
    rule_drop(self);
    return -1;
  }
  memset(self, 0, sizeof *self);
  self->command = cstr_from(command);
  self->number = -1;

  char *cond;
  while ((cond = strtok_r(str, DELIM_CONDITION, &str))) {
    if (!rule_add_condition(self, cond)) {
      rule_drop(self);
      return -1;
    }
  }

  return 0;
}

static inline bool rule_check(Rule *self, const FileInfo *info) {
  c_foreach(it, conditions, self->conditions) {
    Condition *c = it.ref;
    if (!c->check(c, info)) {
      return false;
    }
  }
  return true;
}

static int l_rifle_fileinfo(lua_State *L) {
  const char *file = luaL_checkstring(L, 1);

  char path[PATH_MAX + 1];
  if (realpath(file, path) == NULL) {
    path[0] = 0;
  }

  char mime[256];
  get_mimetype(path, mime, sizeof mime);

  lua_createtable(L, 0, 3);

  lua_pushstring(L, file);
  lua_setfield(L, -2, "file");

  lua_pushstring(L, mime);
  lua_setfield(L, -2, "mime");

  lua_pushstring(L, path);
  lua_setfield(L, -2, "path");

  return 1;
}

static inline int llua_push_rule(lua_State *L, const Rule *r, int num) {
  lua_createtable(L, 0, 5);

  lua_pushcstr(L, &r->command);
  lua_setfield(L, -2, "command");

  lua_pushboolean(L, r->flag_fork);
  lua_setfield(L, -2, "fork");

  lua_pushboolean(L, r->flag_lfm);
  lua_setfield(L, -2, "lfm");

  lua_pushboolean(L, r->flag_term);
  lua_setfield(L, -2, "term");

  lua_pushboolean(L, r->flag_esc);
  lua_setfield(L, -2, "esc");

  lua_pushinteger(L, num);
  lua_setfield(L, -2, "number");

  return 1;
}

static int l_rifle_query_mime(lua_State *L) {
  Rifle *rifle = lua_touserdata(L, lua_upvalueindex(1));
  const char *mime = luaL_checkstring(L, 1);

  int limit = 0;
  zsview pick = zsview_init();

  if (lua_istable(L, 2)) {
    lua_getfield(L, 2, "limit");
    limit = luaL_optinteger(L, -1, 0);
    lua_pop(L, 1);

    lua_getfield(L, 2, "pick");
    if (!lua_isnoneornil(L, -1)) {
      pick = lua_tozsview(L, -1);
    }
    lua_pop(L, 1);
  }

  const FileInfo info = {
      .file = c_zv(""), .path = c_zv(""), .mime = zsview_from(mime)};

  lua_newtable(L);

  int i = 1;
  int ct_match = 0;

  c_foreach(it, rules, rifle->rules) {
    Rule *r = it.ref;
    if (r->has_mime && rule_check(r, &info)) {
      if (r->number > 0) {
        ct_match = r->number;
      }
      ct_match++;

      if (!zsview_is_empty(pick)) {
        const int ind = atoi(pick.str);
        const bool ok = (ind != 0 || pick.str[0] == '0');
        if ((ok && ind != ct_match - 1) ||
            (!ok && (cstr_equals_zv(&r->label, &pick) != 0))) {
          continue;
        }
      }

      llua_push_rule(L, r, ct_match - 1);
      lua_rawseti(L, -2, i++);

      if (limit > 0 && i > limit) {
        break;
      }
    }
  }

  return 1;
}

static int l_rifle_query(lua_State *L) {
  Rifle *rifle = lua_touserdata(L, lua_upvalueindex(1));

  luaL_checktype(L, 1, LUA_TSTRING);
  zsview file = lua_tozsview(L, 1);

  int limit = 0;
  zsview pick = zsview_init();

  if (lua_istable(L, 2)) {
    lua_getfield(L, 2, "limit");
    limit = luaL_optinteger(L, -1, 0);
    lua_pop(L, 1);

    lua_getfield(L, 2, "pick");
    if (!lua_isnoneornil(L, -1)) {
      pick = lua_tozsview(L, -1);
    }
    lua_pop(L, 1);
  }

  char path[PATH_MAX + 1];
  if (realpath(file.str, path) == NULL) {
    path[0] = 0;
  }

  char mime[256];
  get_mimetype(path, mime, sizeof mime);

  const FileInfo info = {
      .file = file, .path = zsview_from(path), .mime = zsview_from(mime)};

  lua_newtable(L); /* {} */

  int i = 1;
  int ct_match = 0;

  c_foreach(it, rules, rifle->rules) {
    Rule *r = it.ref;
    if (rule_check(r, &info)) {
      if (r->number > 0) {
        ct_match = r->number;
      }
      ct_match++;

      if (!zsview_is_empty(pick)) {
        const int ind = atoi(pick.str);
        const bool ok = (ind != 0 || pick.str[0] == '0');
        if ((ok && ind != ct_match - 1) ||
            (!ok && (cstr_equals_zv(&r->label, &pick) != 0))) {
          continue;
        }
      }

      llua_push_rule(L, r, ct_match - 1);
      lua_rawseti(L, -2, i++);

      if (limit > 0 && i > limit) {
        break;
      }
    }
  }

  return 1;
}

// loads rules from the configuration file
static void load_rules(Rifle *rifle) {
  if (cstr_is_empty(&rifle->config_file)) {
    return;
  }

  FILE *fp = fopen(cstr_str(&rifle->config_file), "r");
  if (!fp) {
    return;
  }

  char buf[BUFSIZE];
  while (fgets(buf, BUFSIZE, fp) != NULL) {
    if (is_comment_or_whitespace(buf)) {
      continue;
    }

    char *command = split_command(buf);
    if (!command) {
      continue;
    }

    Rule r;
    if (rule_init(&r, buf, command) == 0) {
      rules_push(&rifle->rules, r);
    }
  }

  fclose(fp);
}

// loads rules from the table at the stack position idx
static inline int llua_parse_rules(lua_State *L, int idx, Rifle *rifle) {
  char buf[BUFSIZE];
  for (lua_pushnil(L); lua_next(L, idx - 1); lua_pop(L, 1)) {
    const char *str = lua_tostring(L, -1);
    log_debug("parsing: %s", str);
    strncpy(buf, str, sizeof buf - 1);
    buf[sizeof buf - 1] = 0;

    char *command = split_command(buf);
    if (!command) {
      log_error("malformed rule: %s", str);
    }

    Rule r;
    if (rule_init(&r, buf, command) != 0) {
      log_error("malformed rule: %s", str);
      continue;
    }
    rules_push(&rifle->rules, r);
  }
  return 0;
}

static int l_rifle_setup(lua_State *L) {
  Rifle *rifle = lua_touserdata(L, lua_upvalueindex(1));

  rules_clear(&rifle->rules);

  if (lua_istable(L, 1)) {
    lua_getfield(L, 1, "rules");
    if (!lua_isnoneornil(L, -1)) {
      llua_parse_rules(L, -1, rifle);
    }
    lua_pop(L, 1);

    lua_getfield(L, 1, "config");
    if (!lua_isnoneornil(L, -1)) {
      cstr_drop(&rifle->config_file);
      rifle->config_file = path_replace_tilde(lua_tozsview(L, -1));
    }
    lua_pop(L, 1);
  }

  load_rules(rifle);

  return 0;
}

static int l_rifle_nrules(lua_State *L) {
  Rifle *rifle = lua_touserdata(L, lua_upvalueindex(1));
  lua_pushinteger(L, rules_size(&rifle->rules));
  return 1;
}

static int l_rifle_gc(lua_State *L) {
  Rifle *rifle = luaL_checkudata(L, 1, RIFLE_META);
  rules_drop(&rifle->rules);
  cstr_drop(&rifle->config_file);
  return 0;
}

static const luaL_Reg rifle_lib[] = {
    {"fileinfo", l_rifle_fileinfo}, {"nrules", l_rifle_nrules},
    {"query", l_rifle_query},       {"query_mime", l_rifle_query_mime},
    {"setup", l_rifle_setup},       {NULL, NULL}};

int luaopen_rifle(lua_State *L) {
  lua_newtable(L);

  Rifle *r = lua_newuserdata(L, sizeof *r);
  memset(r, 0, sizeof *r);
  luaL_newmetatable(L, RIFLE_META);
  lua_pushcfunction(L, l_rifle_gc);
  lua_setfield(L, -2, "__gc");
  lua_setmetatable(L, -2);

  luaL_setfuncs(L, rifle_lib, 1);

  return 1;
}
