#include <errno.h>
#include <ev.h>
#include <libgen.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "app.h"
#include "config.h"
#include "cvector.h"
#include "fm.h"
#include "log.h"
#include "lualfm.h"
#include "ui.h"
#include "util.h"

#define DEFAULT_PROGNAME "lfm"

#define USAGE_FMT                                                          \
	"Usage:\n  %s [options] <directory>\n\n"                               \
	"Options:\n"                                                           \
	"  -c <cmd>     Execute <cmd> after loading the config\n"              \
	"  -h           Print this help message\n"                             \
	"  -l <file>    Write last visited directory to file on exit\n"        \
	"  -s <file>    Write selection to file and quit\n"                    \
	"  -u <config>  Use this config file\n"                                \
	"  -v           Print version information\n"

#ifdef DEBUG
#ifndef VERSION
#define VERSION "?"
#endif
#define VERSION_FMT "%s v0." VERSION "-debug\n"
#else
#ifndef VERSION
#define VERSION "release"
#endif
#define VERSION_FMT "%s v0." VERSION "\n"
#endif

static void usage(const char *progname, int opt)
{
	fprintf(stderr, USAGE_FMT, progname ? progname : DEFAULT_PROGNAME);
	exit(opt);
}

static void version()
{
	fprintf(stderr, VERSION_FMT, DEFAULT_PROGNAME);
	exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	app_t app;

	const unsigned long t0 = current_micros();

	config_defaults();

	FILE *log_fp = fopen(cfg.logpath, "w");

	log_add_fp(log_fp, LOG_TRACE);
	log_set_quiet(true);
	log_debug("startup");

	int opt;
	while ((opt = getopt(argc, argv, ":c:hl:s:u:v")) != -1) {
		switch (opt) {
			case 'c':
				log_debug("command: %s\n", optarg);
				cvector_push_back(cfg.commands, optarg);
				break;
			case 'h':
				usage(argv[0], EXIT_SUCCESS);
				/* not reached */
				break;
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
				version();
				/* not reached */
				break;
			case '?':
				log_error("Unknown option: %c\n", optopt);
				fprintf(stderr, "Unknown option: %c\n", optopt);
				usage(argv[0], EXIT_FAILURE);
				/* not reached */
				break;
		}
	}

	if (optind < argc) {
		log_debug("setting start dir %s", argv[optind]);
		cfg.startpath = arealpath(argv[optind]);
		struct stat statbuf;
		if (stat(cfg.startpath, &statbuf) == -1) {
			log_error(strerror(errno));
			free(cfg.startpath);
			cfg.startpath = NULL;
		} else {
			if (!S_ISDIR(statbuf.st_mode)) {
				char *f = cfg.startpath;
				cfg.startfile = abasename(cfg.startpath);
				cfg.startpath = adirname(cfg.startpath);
				free(f);
				log_debug("set start file to %s", cfg.startfile);
			}
			log_debug("set start dir %s", cfg.startpath);
		}
	}

	setlocale(LC_ALL, "");

	app_init(&app);

	log_debug("starting main loop after %.2f ms", (current_micros() - t0)/1000.0);
	app_run(&app);

	app_deinit(&app);

	/* selection is written in lualfm.c */
	if (cfg.lastdir != NULL) {
		FILE *fp;
		if ((fp = fopen(cfg.lastdir, "w")) != NULL) {
			fputs(getenv("PWD"), fp);
			fclose(fp);
		} else {
			log_error("lastdir: %s", strerror(errno));
		}
	}

	config_clear();

	log_info("fin");
	fclose(log_fp);

#ifndef DEBUG
	remove(cfg.logpath);
#endif

	exit(0);
}
