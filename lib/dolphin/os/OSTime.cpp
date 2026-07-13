#include <chrono>
#include <ctime>
#include <atomic>
#include <limits>
#include <mutex>

#include "internal.hpp"
#include <dolphin/os.h>

static const int YearDays[MONTH_MAX] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};

static const int LeapYearDays[MONTH_MAX] = {0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335};

namespace chrono = std::chrono;
using SystemDuration = chrono::system_clock::duration;
using SystemTime = chrono::time_point<chrono::system_clock>;
using LocalTime = chrono::local_time<SystemDuration>;
using TickDuration = chrono::duration<s64, std::ratio<1, OS_TIMER_CLOCK>>;

static const SystemTime startupTime = chrono::system_clock::now();
static const chrono::time_point<chrono::steady_clock> startupSteadyTime = chrono::steady_clock::now();

static LocalTime SystemTimeToLocalTime(SystemTime time) {
#if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L
    return chrono::zoned_time(chrono::current_zone(), time).get_local_time();
#else
    // Apple libc++ currently ships <chrono> with the C++20 timezone database API disabled
    // (_LIBCPP_HAS_TIME_ZONE_DATABASE == 0), so zoned_time/current_zone are unavailable there.
    const auto wholeSeconds = chrono::floor<chrono::seconds>(time);
    const auto fractionalSeconds = chrono::duration_cast<SystemDuration>(time - wholeSeconds);
    std::time_t wallClock = chrono::system_clock::to_time_t(wholeSeconds);
    std::tm localTm{};

#if defined(_WIN32)
    ASSERT(localtime_s(&localTm, &wallClock) == 0);
#else
    ASSERT(localtime_r(&wallClock, &localTm) != nullptr);
#endif

    const auto localDate = chrono::local_days{
        chrono::year{localTm.tm_year + 1900} / chrono::month{static_cast<unsigned>(localTm.tm_mon + 1)} /
        chrono::day{static_cast<unsigned>(localTm.tm_mday)}};
    const auto localTimeOfDay =
        chrono::hours{localTm.tm_hour} + chrono::minutes{localTm.tm_min} + chrono::seconds{localTm.tm_sec};
    return LocalTime{
        chrono::duration_cast<SystemDuration>(localDate.time_since_epoch()) +
        chrono::duration_cast<SystemDuration>(localTimeOfDay) +
        fractionalSeconds};
#endif
}

static const LocalTime startupLocalTime = SystemTimeToLocalTime(startupTime);

namespace {

struct DeterministicClock {
    std::atomic<bool> enabled{false};
    std::atomic<OSTime> ticks{0};
    std::mutex writerMutex;
    u64 stepTickNumerator = 0;
    u32 stepTickDenominator = 1;
    u64 remainder = 0;
};

DeterministicClock g_deterministicClock;

bool CheckedMultiply(const u64 lhs, const u64 rhs, u64& result) {
    if (lhs != 0 && rhs > std::numeric_limits<u64>::max() / lhs) {
        return false;
    }
    result = lhs * rhs;
    return true;
}

bool CheckedAdd(const u64 lhs, const u64 rhs, u64& result) {
    if (rhs > std::numeric_limits<u64>::max() - lhs) {
        return false;
    }
    result = lhs + rhs;
    return true;
}

bool AddUnsignedTicks(const OSTime current, const u64 delta, OSTime& result) {
    if (delta == 0) {
        result = current;
        return true;
    }
    if (current >= 0) {
        const u64 available = static_cast<u64>(std::numeric_limits<OSTime>::max() - current);
        if (delta > available) {
            return false;
        }
        result = current + static_cast<OSTime>(delta);
        return true;
    }

    const u64 distanceToZero = static_cast<u64>(-(current + 1)) + 1;
    if (delta < distanceToZero) {
        const u64 magnitude = distanceToZero - delta;
        result = -static_cast<OSTime>(magnitude);
        return true;
    }
    const u64 positive = delta - distanceToZero;
    if (positive > static_cast<u64>(std::numeric_limits<OSTime>::max())) {
        return false;
    }
    result = static_cast<OSTime>(positive);
    return true;
}

OSTime GetRealtimeOSTime() {
    // System time is provided in the number of timer ticks since 2000-01-01 00:00:00
    // Use time_t arithmetic to avoid chrono duration_cast overflow issues on some platforms.

    // GCN epoch: 2000-01-01 00:00:00 UTC = 946684800 seconds after Unix epoch
    static constexpr s64 gcnEpochUnix = 946684800LL;

    // Get current wall-clock time
    auto elapsed = chrono::steady_clock::now() - startupSteadyTime;
    auto currentTime = startupTime + chrono::duration_cast<chrono::system_clock::duration>(elapsed);

    // Convert to seconds since Unix epoch, then offset to GCN epoch
    auto sinceUnix = chrono::duration_cast<chrono::microseconds>(currentTime.time_since_epoch());
    s64 totalMicros = sinceUnix.count();

    // Apply local timezone offset
    std::time_t wallClock = chrono::system_clock::to_time_t(currentTime);
    std::tm localTm{};
    std::tm gmTm{};
#if defined(_WIN32)
    localtime_s(&localTm, &wallClock);
    gmtime_s(&gmTm, &wallClock);
#else
    localtime_r(&wallClock, &localTm);
    gmtime_r(&wallClock, &gmTm);
#endif

    // Fix time with daylight savings
    localTm.tm_isdst = -1;
    gmTm.tm_isdst = -1;

    // Compute UTC offset in seconds
    s64 utcOffsetSec = static_cast<s64>(mktime(&localTm)) - static_cast<s64>(mktime(&gmTm));

    s64 secondsSinceGcnEpoch = (totalMicros / 1000000LL) - gcnEpochUnix + utcOffsetSec;
    s64 remainderMicros = totalMicros % 1000000LL;

    s64 ticksFromSeconds = secondsSinceGcnEpoch * static_cast<s64>(OS_TIMER_CLOCK);
    s64 ticksFromRemainder = remainderMicros * static_cast<s64>(OS_TIMER_CLOCK) / 1000000LL;

    return ticksFromSeconds + ticksFromRemainder;
}

} // namespace

OSTick OSGetTick() {
    return OSGetTime() & 0xFFFFFFFF;
}

OSTime OSGetTime() {
    if (g_deterministicClock.enabled.load(std::memory_order_acquire)) {
        return g_deterministicClock.ticks.load(std::memory_order_acquire);
    }
    return GetRealtimeOSTime();
}

BOOL AuroraEnableDeterministicTime(const OSTime initialTicks, const u32 rateNumerator,
                                   const u32 rateDenominator) {
    if (rateNumerator == 0 || rateDenominator == 0) {
        return FALSE;
    }
    const u64 stepNumerator = static_cast<u64>(OS_TIMER_CLOCK) * rateDenominator;

    std::lock_guard lock(g_deterministicClock.writerMutex);
    g_deterministicClock.stepTickNumerator = stepNumerator;
    g_deterministicClock.stepTickDenominator = rateNumerator;
    g_deterministicClock.remainder = 0;
    g_deterministicClock.ticks.store(initialTicks, std::memory_order_release);
    g_deterministicClock.enabled.store(true, std::memory_order_release);
    return TRUE;
}

void AuroraDisableDeterministicTime() {
    std::lock_guard lock(g_deterministicClock.writerMutex);
    g_deterministicClock.enabled.store(false, std::memory_order_release);
}

BOOL AuroraIsDeterministicTimeEnabled() {
    return g_deterministicClock.enabled.load(std::memory_order_acquire) ? TRUE : FALSE;
}

BOOL AuroraResetDeterministicTime(const OSTime ticks) {
    std::lock_guard lock(g_deterministicClock.writerMutex);
    if (!g_deterministicClock.enabled.load(std::memory_order_acquire)) {
        return FALSE;
    }
    g_deterministicClock.remainder = 0;
    g_deterministicClock.ticks.store(ticks, std::memory_order_release);
    return TRUE;
}

BOOL AuroraAdvanceDeterministicTime(const u64 logicalTicks) {
    std::lock_guard lock(g_deterministicClock.writerMutex);
    if (!g_deterministicClock.enabled.load(std::memory_order_acquire)) {
        return FALSE;
    }

    const u64 divisor = g_deterministicClock.stepTickDenominator;
    const u64 wholeTicks = g_deterministicClock.stepTickNumerator / divisor;
    const u64 fractionalTicks = g_deterministicClock.stepTickNumerator % divisor;
    const u64 cycles = logicalTicks / divisor;
    const u64 tailSteps = logicalTicks % divisor;

    u64 wholeDelta;
    u64 cycleDelta;
    if (!CheckedMultiply(wholeTicks, logicalTicks, wholeDelta) ||
        !CheckedMultiply(fractionalTicks, cycles, cycleDelta)) {
        return FALSE;
    }
    const u64 tailNumerator = g_deterministicClock.remainder + fractionalTicks * tailSteps;
    const u64 tailDelta = tailNumerator / divisor;
    const u64 newRemainder = tailNumerator % divisor;

    u64 delta;
    if (!CheckedAdd(wholeDelta, cycleDelta, delta) || !CheckedAdd(delta, tailDelta, delta)) {
        return FALSE;
    }

    const OSTime current = g_deterministicClock.ticks.load(std::memory_order_relaxed);
    OSTime advanced;
    if (!AddUnsignedTicks(current, delta, advanced)) {
        return FALSE;
    }
    g_deterministicClock.remainder = newRemainder;
    g_deterministicClock.ticks.store(advanced, std::memory_order_release);
    return TRUE;
}

void AuroraInitClock() {
  if (OSBaseAddress == 0) {
    return;
  }

  __OSBusClock = OS_TIMER_CLOCK * OS_TIMER_CLOCK_DIVIDER;
}


static int IsLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static int GetYearDays(int year, int mon) {
    const int* md = (IsLeapYear(year)) ? LeapYearDays : YearDays;

    return md[mon];
}

static int GetLeapDays(int year) {
    ASSERT(0 <= year);
    
    if (year < 1) {
        return 0;
    }
    return (year + 3) / 4 - (year - 1) / 100 + (year - 1) / 400;
}

static void GetDates(int days, OSCalendarTime* td) {
    int year;
    int n;
    int month;
    const int* md;

    ASSERT(0 <= days);

    td->wday = (days + 6) % WEEK_DAY_MAX;

    for (year = days / YEAR_DAY_MAX;
         days < (n = year * YEAR_DAY_MAX + GetLeapDays(year)); year--) {
        ;
    }

    days -= n;
    td->year = year;
    td->yday = days;

    md = IsLeapYear(year) ? LeapYearDays : YearDays;
    for (month = MONTH_MAX; days < md[--month];) {
        ;
    }
    td->mon = month;
    td->mday = days - md[month] + 1;
}

void OSTicksToCalendarTime(OSTime ticks, OSCalendarTime* td) {
    int days;
    int secs;
    OSTime d;

    d = ticks % OS_SEC_TO_TICKS(1);    
    if (d < 0) {
        d += OS_SEC_TO_TICKS(1);
        ASSERT(0 <= d);
    }

    td->usec = OS_TICKS_TO_USEC(d) % USEC_MAX;
    td->msec = OS_TICKS_TO_MSEC(d) % MSEC_MAX;

    ASSERT(0 <= td->usec);
    ASSERT(0 <= td->msec);

    ticks -= d;

    ASSERT(ticks % OSSecondsToTicks(1) == 0);
    ASSERT(0 <= OSTicksToSeconds(ticks) / 86400 + BIAS && OSTicksToSeconds(ticks) / 86400 + BIAS <= INT_MAX);

    days = (OS_TICKS_TO_SEC(ticks) / SECS_IN_DAY) + BIAS;    
    secs = OS_TICKS_TO_SEC(ticks) % SECS_IN_DAY;
    if (secs < 0) {
        days -= 1;
        secs += SECS_IN_DAY;
        ASSERT(0 <= secs);
    }

    GetDates(days, td);
    td->hour = secs / 60 / 60;
    td->min = secs / 60 % 60;
    td->sec = secs % 60;
}

OSTime OSCalendarTimeToTicks(OSCalendarTime* td) {
    OSTime secs;
    int ov_mon;
    int mon;
    int year;

    ov_mon = td->mon / MONTH_MAX;
    mon = td->mon - (ov_mon * MONTH_MAX);

    if (mon < 0) {
        mon += MONTH_MAX;
        ov_mon--;
    }

    ASSERT((ov_mon <= 0 && 0 <= td->year + ov_mon) || (0 < ov_mon && td->year <= INT_MAX - ov_mon));
    
    year = td->year + ov_mon;

    secs = (OSTime)SECS_IN_YEAR * year +
           (OSTime)SECS_IN_DAY * (GetLeapDays(year) + GetYearDays(year, mon) + td->mday - 1) +
           (OSTime)SECS_IN_HOUR * td->hour +
           (OSTime)SECS_IN_MIN * td->min +
           td->sec -
           (OSTime)0xEB1E1BF80ULL;

    return OS_SEC_TO_TICKS(secs) + OS_MSEC_TO_TICKS((OSTime)td->msec) +
           OS_USEC_TO_TICKS((OSTime)td->usec);
}
