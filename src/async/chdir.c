#include "private.h"

#include "defs.h"
#include "fm.h"
#include "getpwd.h"
#include "hooks.h"
#include "lfm.h"

#include <ev.h>
#include <stc/cstr.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

struct chdir_work {
  struct result super;
  struct async_ctx *async;
  char *dest;
  char *origin;
  int fd;
  int err;
  bool run_hook;
};

static void destroy(void *p) {
  struct chdir_work *work = p;
  if (likely(work->fd > 0))
    close(work->fd);
  xfree(work->dest);
  xfree(work->origin);
  xfree(work);
}

static void callback(void *p, Lfm *lfm) {
  struct chdir_work *work = p;

  if (lfm->async.in_progress.chdir == p)
    lfm->async.in_progress.chdir = NULL;

  lfm_mode_exit(lfm, c_zv("visual"));

  if (work->fd < 0) {
    // back to the where we cd'ed from
    lfm_errorf(lfm, "open: %s", strerror(work->err));
    fm_sync_chdir(&lfm->fm, zsview_from(work->origin), false, false);
  } else if (fchdir(work->fd) != 0) {
    lfm_perror(lfm, "fchdir");
    fm_sync_chdir(&lfm->fm, zsview_from(work->origin), false, false);
  } else {
    setpwd(work->dest);
    if (work->run_hook)
      LFM_RUN_HOOK(lfm, LFM_HOOK_CHDIRPOST, work->dest);
  }
}

static void worker(void *arg) {
  struct chdir_work *work = arg;

  work->fd = open(work->dest, O_RDONLY);
  if (work->fd < 0)
    work->err = errno;

  submit_async_result(work->async, (struct result *)work);
}

void async_chdir(struct async_ctx *async, const char *path, bool run_hook) {
  struct chdir_work *work = xcalloc(1, sizeof *work);
  work->super.callback = &callback;
  work->super.destroy = &destroy;

  work->dest = strdup(path);
  work->origin = cstr_strdup(&to_lfm(async)->fm.pwd);
  work->async = async;
  work->run_hook = run_hook;

  cancel(async->in_progress.chdir);
  async->in_progress.chdir = &work->super;

  tpool_add_work(async->tpool, worker, work, true);
}
