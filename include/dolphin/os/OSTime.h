#ifndef _DOLPHIN_OSTIME_H_
#define _DOLPHIN_OSTIME_H_

#include <dolphin/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef s64 OSTime;
typedef u32 OSTick;

#ifdef TARGET_PC
/**
 * Opt-in logical clock used by deterministic simulation.
 *
 * rateNumerator/rateDenominator is the logical update rate in hertz. For a
 * 30 Hz simulation pass 30/1. Advances retain their fractional timer-tick
 * remainder, so rational rates do not accumulate truncation drift.
 *
 * This controls OSGetTime and therefore OSGetTick. It does not advance or
 * dispatch OSAlarm callbacks, redirect the SDK's separate __OSGetSystemTime
 * implementation, or replace host steady/system clocks used by profiling and
 * worker infrastructure. Code that busy-waits on OSGetTime will remain paused
 * until the simulation driver explicitly advances this clock.
 */
BOOL AuroraEnableDeterministicTime(OSTime initialTicks, u32 rateNumerator,
                                   u32 rateDenominator);
void AuroraDisableDeterministicTime(void);
BOOL AuroraIsDeterministicTimeEnabled(void);
BOOL AuroraResetDeterministicTime(OSTime ticks);
BOOL AuroraAdvanceDeterministicTime(u64 logicalTicks);
#endif

#define OSDiffTick(tick1, tick0) ((s32)(tick1) - (s32)(tick0))

// Time base frequency = 1/4 bus clock
#define OS_TIME_SPEED (OS_BUS_CLOCK / 4)

// OS time -> Real time
#define OS_TICKS_TO_SEC(x) ((x) / (OS_TIME_SPEED))
#define OS_TICKS_TO_MSEC(x) ((x) / (OS_TIME_SPEED / 1000))
#define OS_TICKS_TO_USEC(x) (((x) * 8) / (OS_TIME_SPEED / 125000))
#define OS_TICKS_TO_NSEC(x) (((x) * 8000) / (OS_TIME_SPEED / 125000))

// Real time -> OS time
#define OS_SEC_TO_TICKS(x) ((x) * (OS_TIME_SPEED))
#define OS_MSEC_TO_TICKS(x) ((x) * (OS_TIME_SPEED / 1000))
#define OS_USEC_TO_TICKS(x) ((x) * (OS_TIME_SPEED / 125000) / 8)
#define OS_NSEC_TO_TICKS(x) ((x) * (OS_TIME_SPEED / 125000) / 8000)

#define USEC_MAX 1000
#define MSEC_MAX 1000
#define MONTH_MAX 12
#define WEEK_DAY_MAX 7
#define YEAR_DAY_MAX 365

#define SECS_IN_MIN 60
#define SECS_IN_HOUR (SECS_IN_MIN * 60)
#define SECS_IN_DAY (SECS_IN_HOUR * 24)
#define SECS_IN_YEAR (SECS_IN_DAY * 365)

#define BIAS 0xB2575

#define __OSSystemTime (OSTime*)0x800030D8

#ifdef __cplusplus
}
#endif

#endif // _DOLPHIN_OSTIME_H_
