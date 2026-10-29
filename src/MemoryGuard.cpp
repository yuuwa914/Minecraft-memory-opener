/*
 * MemoryGuard - low-memory relief mod for Minecraft Bedrock (LeviLauncher).
 *
 * Does NOT hook or patch the game. A background thread watches the system's
 * MemAvailable; when it drops below a threshold it asks the C allocator
 * (Scudo on Android) to hand cached free pages back to the OS via
 * mallopt(M_PURGE_ALL / M_PURGE). Version independent: no signatures.
 *
 * Limits: this only returns *free* memory held by the allocator. It cannot
 * shrink memory the game is actively using (chunks, textures, entities).
 */
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

#include <android/log.h>
#include <dlfcn.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <pl/Mod.hpp>

namespace {

using namespace std::chrono_literals;

constexpr const char *kTag = "MemoryGuard";

/* bionic malloc.h option values (resolved via dlsym so no API-level needed) */
constexpr int kMalloptPurge = -101;    /* M_PURGE      */
constexpr int kMalloptPurgeAll = -104; /* M_PURGE_ALL  (Android 14+) */

/* Tunables */
constexpr auto kStartupDelay = 30s;     /* let the world finish loading     */
constexpr auto kPollInterval = 5s;      /* how often MemAvailable is read   */
constexpr auto kPurgeCooldown = 30s;    /* minimum gap between purges       */
constexpr auto kFallbackInterval = 90s; /* if /proc/meminfo is unreadable   */
constexpr long kPercentOfTotal = 15;    /* threshold = 15% of MemTotal ...  */
constexpr long kMinThresholdKb = 600L * 1024;  /* ... but at least 600 MB   */
constexpr long kMaxThresholdKb = 1536L * 1024; /* ... and at most 1.5 GB    */

struct MemInfo {
    long totalKb = 0;
    long availKb = 0;
    bool ok = false;
};

MemInfo readMemInfo() {
    MemInfo info;
    FILE *f = std::fopen("/proc/meminfo", "r");
    if (!f) return info;
    char line[128];
    while (std::fgets(line, sizeof(line), f)) {
        long v = 0;
        if (std::sscanf(line, "MemTotal: %ld kB", &v) == 1) info.totalKb = v;
        else if (std::sscanf(line, "MemAvailable: %ld kB", &v) == 1)
            info.availKb = v;
    }
    std::fclose(f);
    info.ok = info.totalKb > 0 && info.availKb > 0;
    return info;
}

long thresholdKb(long totalKb) {
    const long t = totalKb * kPercentOfTotal / 100;
    return std::clamp(t, kMinThresholdKb, kMaxThresholdKb);
}

bool purgeAllocator() {
    using MalloptFn = int (*)(int, int);
    auto fn = reinterpret_cast<MalloptFn>(dlsym(RTLD_DEFAULT, "mallopt"));
    if (!fn) return false;
    if (fn(kMalloptPurgeAll, 0) == 1) return true;
    return fn(kMalloptPurge, 0) == 1;
}

class Guard {
public:
    void start() {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mRunning) return;
        mStop = false;
        mRunning = true;
        mThread = std::thread([this] { run(); });
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(mMutex);
            if (!mRunning) return;
            mStop = true;
        }
        mCv.notify_all();
        if (mThread.joinable()) mThread.join();
        std::lock_guard<std::mutex> lock(mMutex);
        mRunning = false;
    }

private:
    bool sleepFor(std::chrono::seconds d) {
        std::unique_lock<std::mutex> lock(mMutex);
        return !mCv.wait_for(lock, d, [this] { return mStop; });
    }

    void run() {
        /* Background work must never compete with the render thread. */
        setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), 10);

        if (!sleepFor(std::chrono::duration_cast<std::chrono::seconds>(
                kStartupDelay)))
            return;

        auto lastPurge = std::chrono::steady_clock::now() - kPurgeCooldown;
        auto lastFallback = std::chrono::steady_clock::now();

        for (;;) {
            const auto now = std::chrono::steady_clock::now();
            const MemInfo info = readMemInfo();
            bool want = false;
            if (info.ok) {
                want = info.availKb < thresholdKb(info.totalKb);
            } else if (now - lastFallback >= kFallbackInterval) {
                want = true;
                lastFallback = now;
            }

            if (want && now - lastPurge >= kPurgeCooldown) {
                const bool ok = purgeAllocator();
                lastPurge = std::chrono::steady_clock::now();
                const MemInfo after = readMemInfo();
                __android_log_print(
                    ANDROID_LOG_INFO, kTag,
                    "purge %s: MemAvailable %ld MB -> %ld MB (threshold %ld MB)",
                    ok ? "ok" : "failed", info.availKb / 1024,
                    after.availKb / 1024,
                    info.ok ? thresholdKb(info.totalKb) / 1024 : 0L);
            }

            if (!sleepFor(std::chrono::duration_cast<std::chrono::seconds>(
                    kPollInterval)))
                return;
        }
    }

    std::mutex mMutex;
    std::condition_variable mCv;
    std::thread mThread;
    bool mStop = false;
    bool mRunning = false;
};

Guard gGuard;

} // namespace

class MemoryGuardMod {
public:
    bool load() { return true; }
    bool enable() {
        gGuard.start();
        __android_log_print(ANDROID_LOG_INFO, kTag, "enabled");
        return true;
    }
    bool disable() {
        gGuard.stop();
        return true;
    }
    bool unload() {
        gGuard.stop();
        return true;
    }
};

static MemoryGuardMod mod;
PL_REGISTER_MOD(MemoryGuardMod, mod)
