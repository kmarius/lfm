#include "auto/versiondef.h"
#include "config.h"
#include "lfm.h"
#include "log.h"
#include "path.h"
#include "profiling.h"
#include "util.h"

#include <ev.h>

#include <errno.h>
#include <libgen.h>
#include <locale.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
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

int main(int argc, char **argv) {
  int ret = EXIT_SUCCESS;

  PROFILING_INIT();

  if (!isatty(0) || !isatty(1) || !isatty(2)) {
    fprintf(stderr, "Error: %s must be run in a terminal\n", argv[0]);
    if (!valgrind_active()) {
      exit(EXIT_FAILURE);
    }
  }

  config_init();

  FILE *log = fopen(cstr_str(&cfg.logpath), "w");
  log_set_quiet(true);
  int log_level = LOG_INFO;

#ifndef NDEBUG
  log_level = LOG_DEBUG;
#endif

  struct lfm_opts opts = {.log = log};

  int opt;
  while ((opt = getopt(argc, argv, ":c:hl:L:s:u:v")) != -1) {
    switch (opt) {
    case 'c':
      vec_zsview_emplace(&opts.commands, optarg);
      break;
    case 'h':
      usage(argv[0]);
      goto cleanup;
    case 'l':
      opts.lastdir_path = optarg;
      break;
    case 'L':
      log_level = atoi(optarg);
      if (log_level < LOG_TRACE || log_level > LOG_FATAL) {
        fprintf(stderr, "Invalid log level: %s\n", optarg);
        usage(argv[0]);
        ret = EXIT_FAILURE;
        goto cleanup;
      }
      break;
    case 's':
      opts.selection_path = optarg;
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
      ret = EXIT_FAILURE;
      goto cleanup;
    }
  }

  log_add_fp(log, log_level);
  log_set_lock(lock_mutex, &log_mutex);
  log_info("starting lfm " LFM_VERSION);

  // TODO: make it possible to move the cursor to a directory instead
  // of cd'ing into it
  if (optind < argc) {
    cstr path = path_normalize_cstr(zsview_from(argv[optind]), NULL);
    struct stat statbuf;
    if (stat(cstr_str(&path), &statbuf) == -1) {
      // can't print to Ui yet, maybe pass something to init?
      log_error("%s: %s", strerror(errno), opts.startpath);
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

  setlocale(LC_ALL, "");

  srand(time(NULL));

  PROFILE("lfm_init", { lfm_init(&lfm, &opts); });
  PROFILING_COMPLETE();

  ret = lfm_run(&lfm);

  lfm_deinit(&lfm);

cleanup:

  log_info("fin");
  fclose(log);

#ifdef NDEBUG
  remove(cstr_str(&cfg.logpath));
#endif

  config_deinit();

  exit(ret);
}

static void lock_mutex(bool lock, void *ud) {
  pthread_mutex_t *mutex = (pthread_mutex_t *)ud;
  if (lock) {
    pthread_mutex_lock(mutex);
  } else {
    pthread_mutex_unlock(mutex);
  }
}
