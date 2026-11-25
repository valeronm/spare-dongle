#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include "indicator.h"

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static atomic_t mode = ATOMIC_INIT(IND_IDLE);

void indicator_set(enum indicator_mode m)
{
	atomic_set(&mode, m);
}

static void indicator_thread(void *a, void *b, void *c)
{
	gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	for (uint32_t step = 0;; step++) {
		gpio_pin_set_dt(&led, blink_lit(atomic_get(&mode), step));
		k_msleep(BLINK_STEP_MS);
	}
}

K_THREAD_DEFINE(indicator_tid, 512, indicator_thread, NULL, NULL, NULL, 10, 0, 0);
