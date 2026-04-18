#include "auto/versiondef.h"
#include "config.h"
#include "lfm.h"
#include "log.h"
#include "path.h"
#include "profiling.h"
#include "util.h"

#include <bits/getopt_core.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>

#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

#define USAGE_FMT                                                              \
  "Usage:\n  %s [options] <directory>\n\n"                                     \
  "Options:\n"                                                                 \
  "  -c <cmd>     Execute <cmd> after loading the config\n"                    \
  "  -h           Print this help message\n"                                   \
  "  -l <file>    Write last visited directory to file on exit\n"              \
  "  -L <level>   Set the log level from 0 (Trace) to 5 (FATAL)\n"             \
  "  -s <file>    Write selection to file and quit\n"                          \
  "  -u <config>  Use this config file\n"                                      \
  "  -v           Print version information\n"

#define VERSION_FMT "%s " LFM_VERSION "\n"

static void usage(const char *progname) {
  fprintf(stderr, USAGE_FMT, progname);
}

static void version(const char *progname) {
  fprintf(stderr, VERSION_FMT, progname);
}

// log library needs a log
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
static void lock_mutex(bool lock, void *ud);

static Lfm lfm;

i32 main(i32 argc, char **argv) {
  i32 ret = EXIT_SUCCESS;
  FILE *log = NULL;

  PROFILING_INIT();

  if (!isatty(0) || !isatty(1) || !isatty(2)) {
    fprintf(stderr, "Error: %s must be run in a terminal\n", argv[0]);
    if (!valgrind_active())
      goto err;
  }

  if (!setlocale(LC_ALL, "")) {
    fprintf(stderr, "setlocale\n");
    goto err;
  }

  config_init();

  log = fopen(cstr_str(&cfg.logpath), "w");
  if (!log) {
    perror("fopen");
    goto err;
  }
  log_set_quiet(true);
  i32 log_level = LOG_INFO;

#ifndef NDEBUG
  log_level = LOG_DEBUG;
#endif

  struct lfm_opts opts = {.log = log};

  const char *pwd = getenv("PWD");

  i32 opt;
  while ((opt = getopt(argc, argv, ":c:hl:L:s:u:v")) != -1) {
    switch (opt) {
    case 'c':
      vec_zsview_emplace(&opts.commands, optarg);
      break;
    case 'h':
      usage(argv[0]);
      goto cleanup;
    case 'l':
      opts.lastdir_path = path_normalize_cstr(zsview_from(optarg), pwd);
      break;
    case 'L':
      log_level = atoi(optarg);
      if (log_level < LOG_TRACE || log_level > LOG_FATAL) {
        fprintf(stderr, "Invalid log level: %s\n", optarg);
        usage(argv[0]);
        goto err;
      }
      break;
    case 's':
      opts.selection_path = path_normalize_cstr(zsview_from(optarg), pwd);
      break;
    case 'u':
      // we should print an error if a config is provided here and not found
      opts.config = optarg;
      break;
    case 'v':
      version(argv[0]);
      goto cleanup;
    case '?':
      fprintf(stderr, "Unknown option: %c\n", optopt);
      usage(argv[0]);
      goto err;
    }
  }

  log_add_fp(log, log_level);
  log_set_lock(lock_mutex, &log_mutex);
  log_info("starting lfm " LFM_VERSION);

  // TODO: make it possible to move the cursor to a directory instead
  // of cd'ing into it
  if (optind < argc) {
    cstr path = path_normalize_cstr(zsview_from(argv[optind]), pwd);
    struct stat statbuf;
    if (stat(cstr_str(&path), &statbuf) == -1) {
      // can't print to Ui yet, maybe pass something to init?
      log_perror("stat");
      cstr_drop(&path);
    } else {
      if (!S_ISDIR(statbuf.st_mode)) {
        opts.startfile = cstr_from_zv(basename_cstr(&path));
        dirname_cstr(&path);
        opts.startpath = path;
      } else {
        opts.startpath = path;
      }
    }
  }

  srand(time(NULL));

  PROFILE("lfm_init", { lfm_init(&lfm, &opts); });
  PROFILING_COMPLETE();

  ret = lfm_run(&lfm);

  lfm_deinit(&lfm);
  log_info("fin");

cleanup:
  if (log && fclose(log)) {
    perror("fclose");
    ret = EXIT_FAILURE;
  }
#ifdef NDEBUG
  remove(cstr_str(&cfg.logpath));
#endif
  config_deinit();

  exit(ret);

err:
  ret = EXIT_FAILURE;
  goto cleanup;
}

static void lock_mutex(bool lock, void *ud) {
  pthread_mutex_t *mutex = (pthread_mutex_t *)ud;
  if (lock) {
    pthread_mutex_lock(mutex);
  } else {
    pthread_mutex_unlock(mutex);
  }
}
