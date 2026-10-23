#!/usr/bin/env python3
"""report-lost.py <mesa tree> - make a lost device say why.

The vendor driver marks a queue lost with vk_queue_set_lost(), which only records the reason; a
release build prints nothing, so a game just gets VK_ERROR_DEVICE_LOST. The kernel's own reason
(a CSF fatal notification: exception type, sideband, fault trace) sits unread on the kbase fd
when the loss is found by a failed submit instead of a completion wait.

This edits the patched tree so that every vk_queue_set_lost() in the CSF queue code and in
async binds first prints "droiddeck: queue lost at file:line: reason" to stderr and then reads
and logs every CSF notification still pending on the kbase fd. It inserts code at anchors and
fails loudly if one is missing, instead of carrying a diff against a vendor tree that moves.
"""
import sys
from pathlib import Path

HELPER = r'''
/* droiddeck: report device loss (tools/panvk/report-lost.py) */
#include <stdarg.h>
#include <stdio.h>
#ifdef HAVE_PAN_KMOD_KBASE
void kbase_kmod_csf_drain_events(struct pan_kmod_dev *dev);
#endif

static VkResult
droiddeck_report_lost(struct vk_queue *q, const char *file, int line,
                      const char *fmt, ...)
{
   char msg[512];
   va_list ap;
   va_start(ap, fmt);
   vsnprintf(msg, sizeof(msg), fmt, ap);
   va_end(ap);
   if (!vk_queue_is_lost(q)) {
      fprintf(stderr, "MESA: error: droiddeck: queue lost at %s:%d: %s\n",
              file, line, msg);
      fflush(stderr);
#ifdef HAVE_PAN_KMOD_KBASE
      kbase_kmod_csf_drain_events(to_panvk_device(q->base.device)->kmod.dev);
#endif
      fprintf(stderr, "MESA: error: droiddeck: end of pending CSF notifications\n");
      fflush(stderr);
   }
   return _vk_queue_set_lost(q, file, line, "%s", msg);
}
#undef vk_queue_set_lost
#define vk_queue_set_lost(q, ...) \
   droiddeck_report_lost((q), __FILE__, __LINE__, __VA_ARGS__)
'''

DRAIN = r'''

/* droiddeck: log every CSF notification still pending (tools/panvk/report-lost.py) */
void kbase_kmod_csf_drain_events(struct pan_kmod_dev *dev);
void
kbase_kmod_csf_drain_events(struct pan_kmod_dev *dev)
{
   for (int i = 0; i < 64; i++) {
      struct pollfd pfd = {
         .fd = dev->fd,
         .events = POLLIN,
      };
      if (poll(&pfd, 1, 0) <= 0 || !(pfd.revents & POLLIN))
         return;
      struct base_csf_notification event;
      ssize_t rd = read(dev->fd, &event, sizeof(event));
      if (rd != (ssize_t)sizeof(event))
         return;
      fprintf(stderr, "MESA: error: droiddeck: pending CSF notification type %u\n",
              (unsigned)event.type);
      kbase_log_csf_notification(dev, &event);
   }
}
'''


def insert_after(path, anchor, text, after_next=None):
    src = path.read_text()
    at = src.find(anchor)
    if at < 0:
        sys.exit("report-lost: anchor %r not found in %s" % (anchor, path))
    at += len(anchor)
    if after_next:
        nxt = src.find(after_next, at)
        if nxt < 0:
            sys.exit("report-lost: %r not found after %r in %s" % (after_next, anchor, path))
        at = nxt + len(after_next)
    end = src.find("\n", at)
    path.write_text(src[:end + 1] + text + src[end + 1:])
    print("report-lost: edited %s" % path)


def main():
    mesa = Path(sys.argv[1])
    queue = mesa / "src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c"
    insert_after(queue, '#include "kmod/kbase_kmod.h"', HELPER, after_next="#endif")
    bind = mesa / "src/panfrost/vulkan/panvk_async_bind.c"
    if bind.exists() and "vk_queue_set_lost" in bind.read_text():
        insert_after(bind, '#include "vk_queue.h"', HELPER)
    kmod = mesa / "src/panfrost/lib/kmod/kbase_kmod.c"
    src = kmod.read_text()
    for need in ("kbase_log_csf_notification(struct pan_kmod_dev *dev", "<poll.h>"):
        if need not in src:
            sys.exit("report-lost: %r not found in %s" % (need, kmod))
    if "<stdio.h>" not in src:
        src = "#include <stdio.h>\n" + src
    kmod.write_text(src.rstrip("\n") + "\n" + DRAIN)
    print("report-lost: edited %s" % kmod)


if __name__ == "__main__":
    main()
