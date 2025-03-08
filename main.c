#include <stdio.h>
#include <uv.h>

void on_timer(uv_timer_t *timer)
{
  printf("Timer expired!\n");
  uv_stop(timer->loop);
}

int main()
{
  uv_loop_t *loop = uv_default_loop();
  uv_timer_t timer;

  uv_timer_init(loop, &timer);
  uv_timer_start(&timer, on_timer, 1000, 0);

  uv_run(loop, UV_RUN_DEFAULT);

  return 0;
}