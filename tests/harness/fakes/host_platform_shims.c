/* Host-platform services for the SDK's PICO_PLATFORM=host layer.
 *
 * The SDK marks its host platform and timer entry points weak so an application
 * may override them (src/host/pico_platform/platform_base.c and
 * src/host/hardware_timer/timer.c), and its own implementations are POSIX-only:
 * time_us_64() uses clock_gettime() and busy_wait_* fall back to
 * panic_unsupported() outside __unix__/__APPLE__/MSVC. Two consequences for this
 * harness on Windows:
 *
 *   - GCC/MinGW emits the `#pragma weak` definitions as unresolvable weak
 *     symbols, so linking fails on `busy_wait_us` / `tight_loop_contents`;
 *   - there is no usable delay or timestamp implementation to link against even
 *     when the symbols do resolve.
 *
 * These strong definitions give the harness what the platform layer intends:
 * a real, monotonic delay and an empty tight loop. They carry no firmware
 * behaviour and are linked only into the host harness targets.
 */

#include <stdint.h>
#include <time.h>

/* Monotonic microseconds for the host scenarios. */
static uint64_t host_monotonic_us(void)
{
    struct timespec now;
    if (timespec_get(&now, TIME_UTC) != TIME_UTC) {
        return 0ull;
    }
    return (uint64_t)now.tv_sec * 1000000ull + (uint64_t)(now.tv_nsec / 1000);
}

void tight_loop_contents(void)
{
    /* The target spins here waiting for an interrupt; the host scenarios have
     * no interrupt to wait for, so the intent is "yield and return". */
}

uint64_t time_us_64(void)
{
    return host_monotonic_us();
}

uint32_t timer_us_32(void)
{
    return (uint32_t)time_us_64();
}

void busy_wait_us(uint64_t delay_us)
{
    const uint64_t start = time_us_64();
    while ((time_us_64() - start) < delay_us) {
    }
}

void busy_wait_us_32(uint32_t delay_us)
{
    busy_wait_us((uint64_t)delay_us);
}

void busy_wait_ms(uint32_t delay_ms)
{
    busy_wait_us((uint64_t)delay_ms * 1000ull);
}

void busy_wait_at_least_cycles(uint32_t cycles)
{
    /* Same conversion the SDK uses (125 MHz reference). */
    busy_wait_us(((uint64_t)cycles + 124ull) / 125ull);
}

unsigned int get_core_num(void)
{
    /* Single-core host process. */
    return 0u;
}
