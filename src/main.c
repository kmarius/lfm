#include <errno.h>
#include <locale.h>
#include <ev.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include "app.h"
#include "config.h"
#include "cvector.h"
#include "log.h"
#include "lualfm.h"
#include "nav.h"
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
#define VERSION_FMT "%s v0.epsilon-debug\n"
#else
#define VERSION_FMT "%s v0.epsilon\n"
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
	/* TODO: get user id (on 2021-08-13) */

	char rundir[48];
	snprintf(rundir, sizeof(rundir), "/var/run/user/%d/lfm", getuid());
	if (mkdir(rundir, 0700) == -1 && errno != EEXIST) {
		fprintf(stderr, "mkdir: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}
	cfg.rundir = rundir;

	char fifopath[64];
#ifdef DEBUG
	snprintf(fifopath, sizeof(fifopath), "%s/debug.fifo", rundir);

	const char *logpath = "/tmp/lfm.debug.log";
#else
	snprintf(fifopath, sizeof(fifopath), "%s/%d.fifo", rundir, getpid());

	char logpath[64];
	snprintf(logpath, sizeof(logpath), "/tmp/lfm.%d.log", getpid());
#endif
	cfg.fifopath = fifopath;

	FILE *log_fp = fopen(logpath, "w");

	log_add_fp(log_fp, LOG_TRACE);
	log_set_quiet(true);
	log_debug("startup");

	config_defaults();

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
		cfg.startpath = argv[optind];
	}

	setlocale(LC_ALL, "");

	app_init(&app);

	ui_draw(&app.ui, &app.nav);

	log_debug("starting main loop after %.2f ms", (current_micros() - t0)/1000.0);
	run(&app);

	app_destroy(&app);

	/* selection is written in lualfm.c */
	if (cfg.lastdir) {
		FILE *fp;
		if ((fp = fopen(cfg.lastdir, "w"))) {
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
	remove(logpath);
#endif

	exit(0);
}
