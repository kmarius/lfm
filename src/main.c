#include <errno.h>
#include <ev.h>
#include <libgen.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "app.h"
#include "auto/versiondef.h"
#include "config.h"
#include "cvector.h"
#include "fm.h"
#include "log.h"
#include "lualfm.h"
#include "ui.h"
#include "util.h"

#define USAGE_FMT                                                          \
  "Usage:\n  %s [options] <directory>\n\n"                               \
  "Options:\n"                                                           \
  "  -c <cmd>     Execute <cmd> after loading the config\n"              \
  "  -h           Print this help message\n"                             \
  "  -l <file>    Write last visited directory to file on exit\n"        \
  "  -s <file>    Write selection to file and quit\n"                    \
  "  -u <config>  Use this config file\n"                                \
  "  -v           Print version information\n"

#define VERSION_FMT "%s " LFM_VERSION "\n"


static void usage(const char *progname)
{
  fprintf(stderr, USAGE_FMT, progname);
}


static void version(const char *progname)
{
  fprintf(stderr, VERSION_FMT, progname);
}


int main(int argc, char **argv)
{
  App app;
  int ret = EXIT_SUCCESS;

  const uint64_t t0 = current_micros();

  config_init();

  FILE *log_fp = fopen(cfg.logpath, "w");

  log_add_fp(log_fp, LOG_TRACE);
  log_set_quiet(true);
  log_debug("startup");

  int opt;
  while ((opt = getopt(argc, argv, ":c:hl:s:u:v")) != -1) {
    switch (opt) {
      case 'c':
        cvector_push_back(cfg.commands, optarg);
        break;
      case 'h':
        usage(argv[0]);
        goto cleanup;
      case 'l':
        cfg.lastdir = optarg;
        break;
      case 's':
        cfg.selfile = optarg;
        break;
      case 'u':
        free(cfg.configpath);
        cfg.configpath = strdup(optarg);
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

  // TODO: make it possible to move the cursor to a directory instead
  // of cd'ing into it
  if (optind < argc) {
    cfg.startpath = realpath_a(argv[optind]);
    struct stat statbuf;
    if (stat(cfg.startpath, &statbuf) == -1) {
      error("%s: %s", strerror(errno), cfg.startpath);
      free(cfg.startpath);
      cfg.startpath = NULL;
    } else {
      if (!S_ISDIR(statbuf.st_mode)) {
        char *f = cfg.startpath;
        cfg.startfile = basename_a(cfg.startpath);
        cfg.startpath = dirname_a(cfg.startpath);
        free(f);
      }
    }
  }

  setlocale(LC_ALL, "");

  srand(time(NULL));

  app_init(&app);

  log_debug("starting main loop after %.2f ms", (current_micros() - t0)/1000.0);
  app_run(&app);

  app_deinit(&app);

  /* selection is written in lualfm.c */
  if (cfg.lastdir) {
    FILE *fp = fopen(cfg.lastdir, "w");
    if (!fp) {
      log_error("lastdir: %s", strerror(errno));
    } else {
      fputs(getenv("PWD"), fp);
      fclose(fp);
    }
  }

cleanup:

  log_info("fin");
  fclose(log_fp);

#ifndef DEBUG
  remove(cfg.logpath);
#endif

  config_deinit();

  exit(ret);
}
