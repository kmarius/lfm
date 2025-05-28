#include "private.h"

#include "../containers.h"
#include "../fm.h"
#include "../hooks.h"
#include "../lfm.h"
#include "../macros.h"
#include "../memory.h"
#include "../stc/cstr.h"

#include <ev.h>

#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <unistd.h>

struct chdir_data {
  struct result super;
  Async *async;
  char *path;
  char *origin;
  int err;
  bool run_hook;
};

static void chdir_destroy(void *p) {
  struct chdir_data *res = p;
  xfree(res->path);
  xfree(res->origin);
  xfree(res);
}

static void chdir_callback(void *p, Lfm *lfm) {
  struct chdir_data *res = p;
  if (cstr_equals(&lfm->fm.pwd, res->path)) {
    lfm_mode_exit(lfm, c_zv("visual"));
    if (res->err) {
      lfm_error(lfm, "stat: %s", strerror(res->err));
      fm_sync_chdir(&lfm->fm, zsview_from(res->origin), false, false);
    } else if (chdir(res->path) != 0) {
      lfm_error(lfm, "chdir: %s", strerror(errno));
      fm_sync_chdir(&lfm->fm, zsview_from(res->origin), false, false);
    } else {
      setenv("PWD", res->path, true);
      if (res->run_hook) {
        lfm_run_hook(lfm, LFM_HOOK_CHDIRPOST, res->path);
      }
    }
  }
  chdir_destroy(p);
}

static void async_chdir_worker(void *arg) {
  struct chdir_data *work = arg;

  struct stat statbuf;
  if (stat(work->path, &statbuf) == -1) {
    work->err = errno;
  } else {
    work->err = 0;
  }

  enqueue_and_signal(work->async, (struct result *)work);
}

void async_chdir(Async *async, const char *path, bool hook) {
  struct chdir_data *work = xcalloc(1, sizeof *work);
  work->super.callback = &chdir_callback;
  work->super.destroy = &chdir_destroy;

  work->path = strdup(path);
  work->origin = cstr_strdup(&to_lfm(async)->fm.pwd);
  work->async = async;
  work->run_hook = hook;

  tpool_add_work(async->tpool, async_chdir_worker, work, true);
}
