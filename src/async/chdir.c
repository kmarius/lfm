#include "private.h"

#include "fm.h"
#include "getpwd.h"
#include "hooks.h"
#include "lfm.h"
#include "defs.h"
#include "memory.h"
#include "stc/cstr.h"

#include <ev.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <unistd.h>

struct chdir_data {
  struct result super;
  Async *async;
  char *destination;
  char *origin;
  int fd;
  int err;
  bool run_hook;
};

static void chdir_destroy(void *p) {
  struct chdir_data *res = p;
  if (res->fd > 0)
    close(res->fd);
  xfree(res->destination);
  xfree(res->origin);
  xfree(res);
}

static void chdir_callback(void *p, Lfm *lfm) {
  struct chdir_data *res = p;
  // check that we have not moved in the meantime
  // TODO: there's better ways to do this
  if (cstr_equals(&lfm->fm.pwd, res->destination)) {
    lfm_mode_exit(lfm, c_zv("visual"));
    if (res->err) {
      // back to the where we cd'ed from
      lfm_errorf(lfm, "open: %s", strerror(res->err));
      fm_sync_chdir(&lfm->fm, zsview_from(res->origin), false, false);
    } else if (fchdir(res->fd) != 0) {
      lfm_errorf(lfm, "fchdir: %s", strerror(errno));
      fm_sync_chdir(&lfm->fm, zsview_from(res->origin), false, false);
    } else {
      setpwd(res->destination);
      if (res->run_hook) {
        lfm_run_hook(lfm, LFM_HOOK_CHDIRPOST, res->destination);
      }
    }
  }
  chdir_destroy(p);
}

static void async_chdir_worker(void *arg) {
  struct chdir_data *work = arg;

  // open the directory, use fchdir in the callback
  work->fd = open(work->destination, O_RDONLY);
  if (work->fd <= 0)
    work->err = errno;

  enqueue_and_signal(work->async, (struct result *)work);
}

void async_chdir(Async *async, const char *path, bool hook) {
  struct chdir_data *work = xcalloc(1, sizeof *work);
  work->super.callback = &chdir_callback;
  work->super.destroy = &chdir_destroy;

  work->destination = strdup(path);
  work->origin = cstr_strdup(&to_lfm(async)->fm.pwd);
  work->async = async;
  work->run_hook = hook;

  tpool_add_work(async->tpool, async_chdir_worker, work, true);
}
