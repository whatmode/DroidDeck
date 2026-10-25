/* Built as "steamwebhelper": ends the way the argument says, with a second thread running. */
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static void *idle(void *a) { (void)a; for (;;) pause(); return NULL; }
int main(int c, char **v) {
  pthread_t t; pthread_create(&t, NULL, idle, NULL); usleep(100000);
  const char *how = c > 1 ? v[1] : "exit";
  if (!strcmp(how, "segv")) { volatile int *p = (int *)16; *p = 1; }
  if (!strcmp(how, "abort")) abort();
  if (!strcmp(how, "trap")) __builtin_trap();
  if (!strcmp(how, "term")) { kill(getpid(), SIGTERM); sleep(2); }
  if (!strcmp(how, "_exit")) _exit(3);
  exit(2);
}
