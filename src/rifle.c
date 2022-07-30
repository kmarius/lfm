#include <lauxlib.h>
#include <linux/limits.h>
#include <lua.h>
#include <lualib.h>
#include <magic.h>
#include <pcre.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "config.h"
#include "cvector.h"
#include "rifle.h"
#include "util.h"

// arbitrary, max binary name length for `has`
#define EXECUTABLE_MAX 256

#define BUFSIZE 4096
#define DELIM_CONDITION ","
#define DELIM_COMMAND " = "

struct FileInfo;

struct Condition;

typedef bool(check_fun)(struct Condition *, const struct FileInfo *);

typedef struct Condition {
  bool negate;
  char *arg;
  check_fun *check;
} Condition;

typedef struct Rule {
  Condition **conditions;
  char *command;
  char *label;
  int number;
  bool flag_fork;
  bool flag_term;
  bool flag_esc;
} Rule;

typedef struct FileInfo {
  const char *file;
  const char *path;
  const char *mime;
} FileInfo;

static struct {
  char *config_file;
} config = { NULL };

static Rule **rules = NULL;

static Condition *condition_create(check_fun *f, const char *arg, bool negate)
{
  Condition *cd = malloc(sizeof *cd);
  cd->check = f;
  cd->arg = arg ? strdup(arg) : NULL;
  cd->negate = negate;
  return cd;
}


static void condition_destroy(Condition *cd)
{
  if (!cd) {
    return;
  }
  free(cd->arg);
  free(cd);
}


static Rule *rule_create(const char *command)
{
  Rule *rl = malloc(sizeof *rl);
  rl->command = command ? strdup(command) : NULL;
  rl->conditions = NULL;
  rl->label = NULL;
  rl->number = -1;
  rl->flag_fork = false;
  rl->flag_term = false;
  rl->flag_esc = false;
  return rl;
}


static void rule_destroy(Rule *rl)
{
  if (!rl) {
    return;
  }

  cvector_ffree(rl->conditions, condition_destroy);
  free(rl->label);
  free(rl->command);
  free(rl);
}


static void rule_set_flags(Rule *r, const char *flags)
{
  for (const char *f = flags; *f; f++) {
    switch (*f) {
      case 'f': r->flag_fork = true; break;
      case 't': r->flag_term = true; break;
      case 'e': r->flag_esc = true;  break;
    }
  }
  for (const char *f = flags; *f; f++) {
    switch (*f) {
      case 'F': r->flag_fork = false; break;
      case 'T': r->flag_term = false; break;
      case 'E': r->flag_esc = false;  break;
    }
  }
}


// see https://www.mitchr.me/SS/exampleCode/AUPG/pcre_example.c.html
static bool regex_match(const char *regex, const char *string)
{
  int substr_vec[32];
  const char *pcre_error_str;
  int pcre_error_offset;

  pcre *re_compiled = pcre_compile(regex, 0, &pcre_error_str, &pcre_error_offset, NULL);
  if (!re_compiled) {
    return false;
  }

  pcre_extra *pcre_extra = pcre_study(re_compiled, 0, &pcre_error_str);
  if (pcre_error_str) {
    pcre_free(re_compiled);
    return false;
  }

  int pcre_exec_ret = pcre_exec(re_compiled, pcre_extra, string,
      strlen(string), 0, 0, substr_vec, 30);

  pcre_free(re_compiled);
  pcre_free_study(pcre_extra);

  if (pcre_exec_ret < 0) {
    return false;
  }

  return true;
}


// https://stackoverflow.com/questions/9152978/include-unix-utility-file-in-c-program
bool get_mimetype(const char *path, char *dest)
{
  bool ret = true;
  magic_t magic = magic_open(MAGIC_MIME_TYPE);
  magic_load(magic, NULL);
  const char *mime = magic_file(magic, path);
  if (!mime || hasprefix(mime, "cannot open")) {
    ret = false;
    *dest = 0;
  } else {
    strncpy(dest, mime, MIME_MAX);
  }
  magic_close(magic);
  return ret;
}


static bool check_fun_file(Condition *cd, const FileInfo *info)
{
  struct stat path_stat;
  stat(info->file, &path_stat);
  return S_ISREG(path_stat.st_mode) != cd->negate;
}


static bool check_fun_dir(Condition *cd, const FileInfo *info)
{
  struct stat statbuf;
  if (stat(info->file, &statbuf) != 0) {
    return 0;
  }
  return S_ISDIR(statbuf.st_mode) != cd->negate;
}


static bool check_fun_term(Condition *cd, const FileInfo *info)
{
  (void) info;
  return (isatty(0) && isatty(1) && isatty(2)) != cd->negate;
}


static bool check_fun_env(Condition *cd, const FileInfo *info)
{
  (void) info;
  const char *val = getenv(cd->arg);
  return (val != NULL && val[0] != '\0') != cd->negate;
}


static bool check_fun_else(Condition *cd, const FileInfo *info)
{
  (void) info;
  return !cd->negate;
}


static bool check_fun_ext(Condition *cd, const FileInfo *info)
{
  char *regex_str = malloc(strlen(cd->arg) + 8);
  sprintf(regex_str, "\\.(%s)$", cd->arg);
  bool ret = regex_match(regex_str, info->file) != cd->negate;
  free(regex_str);
  return ret;
}


static bool check_fun_path(Condition *cd, const FileInfo *info)
{
  return regex_match(cd->arg, info->path) != cd->negate;
}


static bool check_fun_mime(Condition *cd, const FileInfo *info)
{
  return regex_match(cd->arg, info->mime) != cd->negate;
}


static bool check_fun_name(Condition *cd, const FileInfo *info)
{
  const char *ptr = info->file + strlen(info->file);
  while (ptr > info->file && *(ptr - 1) != '/') {
    ptr--;
  }
  return regex_match(cd->arg, ptr) != cd->negate;
}


static bool check_fun_match(Condition *cd, const FileInfo *info)
{
  return regex_match(cd->arg, info->file) != cd->negate;
}


static bool check_fun_has(Condition *cd, const FileInfo *info)
{
  (void) info;
  char cmd[EXECUTABLE_MAX]; // arbitrary
  snprintf(cmd, sizeof cmd, "command -v \"%s\" >/dev/null 2>&1", cd->arg);
  return !system(cmd) != cd->negate;
}


static char *split_command(char *s)
{
  if ((s = strstr(s, DELIM_COMMAND)) == NULL) {
    return NULL;
  }
  *s = '\0';
  return trim(s + 3);
}


static bool is_comment_or_whitespace(char* s)
{
  s--;
  while (isspace(*++s)) {}
  return *s == '#' || *s == '\0';
}



static bool rule_add_condition(Rule *r, char *cond_str)
{
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
    c = condition_create(
        check_fun_env, "WAYLAND_DISPLAY", negate);
  } else if (streq(func, "else")) {
    c = condition_create(check_fun_else, NULL, negate);
  } else {
    if ((arg = strtok_r(cond_str, "\0", &cond_str)) == NULL)
      return false;

    if (streq(func, "label")) {
      r->label = strdup(arg);
    } else if (streq(func, "number")) {
      /* TODO: cant distringuish between 0 and invalid number
       * (on 2021-07-27) */
      r->number = atoi(arg);
    } else if (streq(func, "flag")) {
      rule_set_flags(r, arg);
    } else if (streq(func, "ext")) {
      c = condition_create(check_fun_ext, arg, negate);
    } else if (streq(func, "path")) {
      c = condition_create(check_fun_path, arg, negate);
    } else if (streq(func, "mime")) {
      c = condition_create(check_fun_mime, arg, negate);
    } else if (streq(func, "name")) {
      c = condition_create(check_fun_name, arg, negate);
    } else if (streq(func, "match")) {
      c = condition_create(check_fun_match, arg, negate);
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


static Rule *parse_rule(char *rule, const char *command)
{
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


static bool check_rule(Rule *r, const FileInfo *info)
{
  for (size_t i = 0; i < cvector_size(r->conditions); i++) {
    if (!r->conditions[i]->check(r->conditions[i], info)) {
      return false;
    }
  }
  return true;
}


static int l_rifle_fileinfo(lua_State *L)
{
  const char *file = luaL_checkstring(L, 1);

  char path[PATH_MAX + 1];
  realpath(file, path);

  char mime[MIME_MAX + 1];
  get_mimetype(path, mime);

  lua_newtable(L);

  lua_pushstring(L, file);
  lua_setfield(L, -2, "file");

  lua_pushstring(L, mime);
  lua_setfield(L, -2, "mime");

  lua_pushstring(L, path);
  lua_setfield(L, -2, "path");

  return 1;
}


static int l_rifle_query(lua_State *L)
{
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
  realpath(file, path);

  char mime[MIME_MAX + 1];
  get_mimetype(path, mime);

  const FileInfo info = {.file = file, .path = path, .mime = mime};

  lua_newtable(L); /* {} */

  int i = 1;
  int ct_match = 0;

  for (size_t j = 0; j < cvector_size(rules); j++) {
    if (check_rule(rules[j], &info)) {
      if (rules[j]->number > 0) {
        ct_match = rules[j]->number;
      }
      ct_match++;

      if (pick != NULL && pick[0] != '\0') {
        const int ind = atoi(pick);
        const bool ok = (ind != 0 || pick[0] == '0');
        if ((ok && ind != ct_match-1) ||
            (!ok && ((rules[j])->label == NULL
                 || strcmp(pick, rules[j]->label) != 0))) {
          continue;
        }
      }

      lua_newtable(L); /* t = {} */

      lua_pushstring(L, rules[j]->command);
      lua_setfield(L, -2, "command"); /* t.command = ... */

      lua_pushboolean(L, rules[j]->flag_fork);
      lua_setfield(L, -2, "fork"); /* t.fork = ... */

      lua_pushboolean(L, rules[j]->flag_term);
      lua_setfield(L, -2, "term");

      lua_pushboolean(L, rules[j]->flag_esc);
      lua_setfield(L, -2, "esc");

      lua_pushinteger(L, ct_match-1);
      lua_setfield(L, -2, "number");

      lua_rawseti(L, -2, i++); /* m[i] = t */

      if (limit > 0 && i > limit) {
        break;
      }
    }
  }

  return 1;
}


static void load_rules(lua_State *L, const char *config)
{
  (void) L;
  char buf[BUFSIZE];

  FILE *fp = fopen(config, "r");
  if (!fp) {
    return;
  }

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
      cvector_push_back(rules, r);
    }
  }

  fclose(fp);
}


static int l_rifle_setup(lua_State *L)
{
  if (lua_istable(L, 1)) {
    lua_getfield(L, 1, "config");
    if (!lua_isnoneornil(L, -1)) {
      free(config.config_file);
      config.config_file = path_replace_tilde(lua_tostring(L, -1));
    }
    lua_pop(L, 1);
  }
  if (!config.config_file) {
    asprintf(&config.config_file, "%s/rifle.conf", cfg.configdir);
  }

  cvector_fclear(rules, rule_destroy);

  load_rules(L, config.config_file);

  return 0;
}


static int l_rifle_nrules(lua_State *L)
{
  lua_pushinteger(L, cvector_size(rules));
  return 1;
}


/* static int l_rifle_gc(lua_State *L) { */
/*  (void) L; */
/*  cvector_ffree(rules, rule_destroy); */
/*  rules = NULL; */
/**/
/*  free(cfg.config); */
/*  cfg.config = NULL; */
/**/
/*  return 0; */
/* } */
/**/
/* static struct luaL_Reg rifle_meta[] = { */
/*  {"__gc", l_rifle_gc}, */
/*  {NULL, NULL}}; */


static const luaL_Reg rifle_lib[] = {
  {"fileinfo", l_rifle_fileinfo},
  {"nrules", l_rifle_nrules},
  {"query", l_rifle_query},
  {"setup", l_rifle_setup},
  {NULL, NULL}};


int lua_register_rifle(lua_State *L)
{
  luaL_register(L, NULL, rifle_lib);

  /* lua_newuserdata(L, 0); */
  /*  */
  /* luaL_newmetatable(L, "rifle_meta"); */
  /* luaL_register(L, NULL, rifle_meta); */
  /* lua_setmetatable(L, -2); */
  /*  */
  /* lua_setfield(L, -2, "dummy"); */

  return 1;
}


int lua_rifle_clear(lua_State *L)
{
  (void) L;
  cvector_ffree(rules, rule_destroy);
  rules = NULL;

  free(config.config_file);
  config.config_file = NULL;
  return 0;
}
