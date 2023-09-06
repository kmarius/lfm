#include <lauxlib.h>
#include <linux/limits.h>
#include <lua.h>
#include <lualib.h>
#include <pcre.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../cvector.h"
#include "../log.h"
#include "../util.h"
#include "rifle.h"

#define RIFLE_META "rifle_meta"

// arbitrary, max binary name length for `has`
#define EXECUTABLE_MAX 256

#define BUFSIZE 4096
#define DELIM_CONDITION ","
#define DELIM_COMMAND " = "

struct fileinfo_s;

struct condition_s;

typedef bool(check_fun)(struct condition_s *, const struct fileinfo_s *);

typedef struct condition_s {
  bool negate;
  union {
    char *arg;
    struct {
      pcre *pcre;
      pcre_extra *pcre_extra;
    };
  };
  check_fun *check;
} Condition;

typedef struct rule_s {
  Condition **conditions;
  char *command;
  char *label;
  int number;
  bool has_mime;
  bool flag_fork;
  bool flag_term;
  bool flag_esc;
} Rule;

typedef struct fileinfo_s {
  const char *file;
  const char *path;
  const char *mime;
} FileInfo;

typedef struct rifle_s {
  char *config_file;
  Rule **rules;
} Rifle;

static inline Condition *condition_create(check_fun *f, const char *arg,
                                          bool negate) {
  Condition *cd = xcalloc(1, sizeof *cd);
  cd->check = f;
  cd->arg = arg ? strdup(arg) : NULL;
  cd->negate = negate;
  return cd;
}

static inline void condition_destroy(Condition *cd) {
  if (!cd) {
    return;
  }
  if (cd->pcre_extra) {
    pcre_free(cd->pcre);
    pcre_free(cd->pcre_extra);
  } else {
    xfree(cd->arg);
  }
  xfree(cd);
}

static inline Rule *rule_create(const char *command) {
  Rule *rl = xcalloc(1, sizeof *rl);
  rl->command = command ? strdup(command) : NULL;
  rl->number = -1;
  return rl;
}

static inline void rule_destroy(Rule *rl) {
  if (!rl) {
    return;
  }
  cvector_ffree(rl->conditions, condition_destroy);
  xfree(rl->label);
  xfree(rl->command);
  xfree(rl);
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
    }
  }
}

static inline bool re_match(pcre *re, pcre_extra *re_extra,
                            const char *string) {
  int substr_vec[32];
  return pcre_exec(re, re_extra, string, strlen(string), 0, 0, substr_vec,
                   30) >= 0;
}

static bool check_fun_file(Condition *cd, const FileInfo *info) {
  struct stat path_stat;
  if (stat(info->file, &path_stat) == -1) {
    return cd->negate;
  }
  return S_ISREG(path_stat.st_mode) != cd->negate;
}

static bool check_fun_dir(Condition *cd, const FileInfo *info) {
  struct stat statbuf;
  if (stat(info->file, &statbuf) != 0) {
    return cd->negate;
  }
  return S_ISDIR(statbuf.st_mode) != cd->negate;
}

// not sure if this even works from within lfm
static bool check_fun_term(Condition *cd, const FileInfo *info) {
  (void)info;
  return (isatty(0) && isatty(1) && isatty(2)) != cd->negate;
}

static bool check_fun_env(Condition *cd, const FileInfo *info) {
  (void)info;
  const char *val = getenv(cd->arg);
  return (val && *val) != cd->negate;
}

static bool check_fun_else(Condition *cd, const FileInfo *info) {
  (void)info;
  return !cd->negate;
}

/* TODO: log errors (on 2022-10-15) */
static inline Condition *condition_create_re(check_fun *f, const char *re,
                                             bool negate) {
  const char *pcre_error_str;
  int pcre_error_offset;
  Condition *c = condition_create(f, NULL, negate);
  c->pcre = pcre_compile(re, 0, &pcre_error_str, &pcre_error_offset, NULL);
  if (!c->pcre) {
    xfree(c);
    return NULL;
  }
  c->pcre_extra = pcre_study(c->pcre, 0, &pcre_error_str);
  if (pcre_error_str) {
    pcre_free(c->pcre);
    xfree(c);
    return NULL;
  }
  return c;
}

static bool check_fun_path(Condition *cd, const FileInfo *info) {
  return re_match(cd->pcre, cd->pcre_extra, info->path) != cd->negate;
}

static bool check_fun_mime(Condition *cd, const FileInfo *info) {
  return re_match(cd->pcre, cd->pcre_extra, info->mime) != cd->negate;
}

static bool check_fun_name(Condition *cd, const FileInfo *info) {
  const char *ptr = strrchr(info->file, '/');
  return re_match(cd->pcre, cd->pcre_extra, ptr ? ptr : info->file) !=
         cd->negate;
}

static bool check_fun_match(Condition *cd, const FileInfo *info) {
  return re_match(cd->pcre, cd->pcre_extra, info->file) != cd->negate;
}

static bool check_fun_has(Condition *cd, const FileInfo *info) {
  (void)info;
  char cmd[EXECUTABLE_MAX];
  snprintf(cmd, sizeof cmd, "command -v \"%s\" >/dev/null 2>&1", cd->arg);
  return !system(cmd) != cd->negate;
}

static inline Condition *condition_create_re_name(const char *arg,
                                                  bool negate) {
  return condition_create_re(check_fun_name, arg, negate);
}

static inline Condition *condition_create_re_ext(const char *arg, bool negate) {
  char *regex_str = xmalloc(strlen(arg) + 8);
  sprintf(regex_str, "\\.(%s)$", arg);
  Condition *c = condition_create_re(check_fun_name, regex_str, negate);
  c->check = check_fun_name;
  xfree(regex_str);
  return c;
}

static inline Condition *condition_create_re_mime(const char *arg,
                                                  bool negate) {
  return condition_create_re(check_fun_mime, arg, negate);
}

static inline Condition *condition_create_re_match(const char *arg,
                                                   bool negate) {
  return condition_create_re(check_fun_match, arg, negate);
}

static inline char *split_command(char *s) {
  if ((s = strstr(s, DELIM_COMMAND)) == NULL) {
    return NULL;
  }
  *s = '\0';
  return trim(s + 3);
}

static inline bool is_comment_or_whitespace(char *s) {
  s--;
  while (isspace(*++s)) {
  }
  return *s == '#' || *s == '\0';
}

static inline bool rule_add_condition(Rule *r, char *cond_str) {
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

  Condition *c = NULL;

  if (streq(func, "file")) {
    c = condition_create(check_fun_file, NULL, negate);
  } else if (streq(func, "directory")) {
    c = condition_create(check_fun_dir, NULL, negate);
  } else if (streq(func, "terminal")) {
    c = condition_create(check_fun_term, NULL, negate);
  } else if (streq(func, "X")) {
    c = condition_create(check_fun_env, "DISPLAY", negate);
  } else if (streq(func, "W")) {
    c = condition_create(check_fun_env, "WAYLAND_DISPLAY", negate);
  } else if (streq(func, "else")) {
    c = condition_create(check_fun_else, NULL, negate);
  } else {
    if ((arg = strtok_r(cond_str, "\0", &cond_str)) == NULL) {
      return false;
    }

    if (streq(func, "label")) {
      r->label = strdup(arg);
    } else if (streq(func, "number")) {
      /* TODO: cant distringuish between 0 and invalid number
       * (on 2021-07-27) */
      r->number = atoi(arg);
    } else if (streq(func, "flag")) {
      rule_set_flags(r, arg);
    } else if (streq(func, "ext")) {
      c = condition_create_re_ext(arg, negate);
    } else if (streq(func, "path")) {
      c = condition_create(check_fun_path, arg, negate);
    } else if (streq(func, "mime")) {
      c = condition_create_re_mime(arg, negate);
      if (!negate) {
        r->has_mime = true;
      }
    } else if (streq(func, "name")) {
      c = condition_create_re_name(arg, negate);
    } else if (streq(func, "match")) {
      c = condition_create_re_match(arg, negate);
    } else if (streq(func, "env")) {
      c = condition_create(check_fun_env, arg, negate);
    } else if (streq(func, "has")) {
      /* could be checked here? some others too */
      c = condition_create(check_fun_has, arg, negate);
    } else {
      return false;
    }
  }

  if (c) {
    cvector_push_back(r->conditions, c);
  }

  return true;
}

static inline Rule *parse_rule(char *rule, const char *command) {
  Rule *r = rule_create(command);

  char *cond;
  while ((cond = strtok_r(rule, DELIM_CONDITION, &rule))) {
    if (!rule_add_condition(r, cond)) {
      rule_destroy(r);
      return NULL;
    }
  }

  return r;
}

static inline bool rule_check(Rule *r, const FileInfo *info) {
  cvector_foreach(Condition * c, r->conditions) {
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

  lua_newtable(L);

  lua_pushstring(L, file);
  lua_setfield(L, -2, "file");

  lua_pushstring(L, mime);
  lua_setfield(L, -2, "mime");

  lua_pushstring(L, path);
  lua_setfield(L, -2, "path");

  return 1;
}

static inline int llua_push_rule(lua_State *L, const Rule *r, int num) {
  lua_newtable(L);

  lua_pushstring(L, r->command);
  lua_setfield(L, -2, "command");

  lua_pushboolean(L, r->flag_fork);
  lua_setfield(L, -2, "fork");

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
  const char *pick = NULL;

  if (lua_istable(L, 2)) {
    lua_getfield(L, 2, "limit");
    limit = luaL_optinteger(L, -1, 0);
    lua_pop(L, 1);

    lua_getfield(L, 2, "pick");
    if (!lua_isnoneornil(L, -1)) {
      pick = luaL_optstring(L, -1, NULL);
    }
    lua_pop(L, 1);
  }

  const FileInfo info = {.file = "", .path = "", .mime = mime};

  lua_newtable(L);

  int i = 1;
  int ct_match = 0;

  cvector_foreach(Rule * r, rifle->rules) {
    if (r->has_mime && rule_check(r, &info)) {
      if (r->number > 0) {
        ct_match = r->number;
      }
      ct_match++;

      if (pick && *pick) {
        const int ind = atoi(pick);
        const bool ok = (ind != 0 || pick[0] == '0');
        if ((ok && ind != ct_match - 1) ||
            (!ok && ((r)->label == NULL || strcmp(pick, r->label) != 0))) {
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
  const char *file = luaL_checkstring(L, 1);

  int limit = 0;
  const char *pick = NULL;

  if (lua_istable(L, 2)) {
    lua_getfield(L, 2, "limit");
    limit = luaL_optinteger(L, -1, 0);
    lua_pop(L, 1);

    lua_getfield(L, 2, "pick");
    if (!lua_isnoneornil(L, -1)) {
      pick = luaL_optstring(L, -1, NULL);
    }
    lua_pop(L, 1);
  }

  char path[PATH_MAX + 1];
  if (realpath(file, path) == NULL) {
    path[0] = 0;
  }

  char mime[256];
  get_mimetype(path, mime, sizeof mime);

  const FileInfo info = {.file = file, .path = path, .mime = mime};

  lua_newtable(L); /* {} */

  int i = 1;
  int ct_match = 0;

  cvector_foreach(Rule * r, rifle->rules) {
    if (rule_check(r, &info)) {
      if (r->number > 0) {
        ct_match = r->number;
      }
      ct_match++;

      if (pick != NULL && pick[0] != '\0') {
        const int ind = atoi(pick);
        const bool ok = (ind != 0 || pick[0] == '0');
        if ((ok && ind != ct_match - 1) ||
            (!ok && (r->label == NULL || strcmp(pick, r->label) != 0))) {
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
  if (!rifle->config_file) {
    return;
  }

  FILE *fp = fopen(rifle->config_file, "r");
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

    Rule *r = parse_rule(buf, command);
    if (r) {
      cvector_push_back(rifle->rules, r);
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

    Rule *r = parse_rule(buf, command);
    if (!r) {
      log_error("malformed rule: %s", str);
      continue;
    }
    cvector_push_back(rifle->rules, r);
  }
  return 0;
}

static int l_rifle_setup(lua_State *L) {
  Rifle *rifle = lua_touserdata(L, lua_upvalueindex(1));

  cvector_fclear(rifle->rules, rule_destroy);

  if (lua_istable(L, 1)) {
    lua_getfield(L, 1, "rules");
    if (!lua_isnoneornil(L, -1)) {
      llua_parse_rules(L, -1, rifle);
    }
    lua_pop(L, 1);

    lua_getfield(L, 1, "config");
    if (!lua_isnoneornil(L, -1)) {
      xfree(rifle->config_file);
      rifle->config_file = path_replace_tilde(lua_tostring(L, -1));
    }
    lua_pop(L, 1);
  }

  load_rules(rifle);

  return 0;
}

static int l_rifle_nrules(lua_State *L) {
  Rifle *rifle = lua_touserdata(L, lua_upvalueindex(1));
  lua_pushinteger(L, cvector_size(rifle->rules));
  return 1;
}

static int l_rifle_gc(lua_State *L) {
  Rifle *r = luaL_checkudata(L, 1, RIFLE_META);
  cvector_ffree(r->rules, rule_destroy);
  xfree(r->config_file);
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
