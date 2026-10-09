#ifndef TINTA_PLANTUML_QUEUE_H
#define TINTA_PLANTUML_QUEUE_H

// Asynchronous PlantUML render queue: the layout path asks for renders and
// collects finished ones, while a single worker thread runs the synchronous
// renderer off the UI thread. App-free like plantuml.h: standard C++ plus
// the Win32 process/file APIs, no UI, no theme types and no network.
//
// Ownership and locking (the whole concurrency contract):
//   - ONE std::mutex + two condition variables guard ONLY the pending-job
//     list, the running job's key and block set, the finished handoff, the
//     permanentKeys set, the toDelete list and the running/stop/started
//     flags. Every other member has an owner.
//   - The rendered-image cache (key -> Cached + LRU order) is owner-thread
//     ONLY: read in lookup(), mutated ONLY in drainFinished(). The worker
//     never touches it and it is never under the mutex.
//   - attempts_/notBefore_ backoff bookkeeping is worker-thread ONLY.
//   - request() never blocks on rendering and never spawns from the
//     caller's thread: it publishes a job and wakes the worker.
//
// Scheduling semantics: a job waiting out its retry backoff window is
// SCHEDULED, not busy - it does not slow waitForIdle() down (so a 60 s
// backoff never blocks a caller), and its retry fires automatically. The
// schedule is FINITE: once BackoffConfig::delaysMs is exhausted, and also
// when the tool exits 0 without a usable artifact, the key is permanent.

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "plantuml.h"

namespace plantuml {

// Retry delays for a failed render, indexed by attempt number: the first
// retry waits delaysMs[0], the second delaysMs[1], and so on. The schedule
// is finite: after delaysMs.size() failed retries the key becomes permanent.
struct BackoffConfig {
    std::vector<int> delaysMs{5000, 15000, 60000};
};

// One cached render. `filePath` points at the rendered image inside a
// private work directory; `spawnCount` counts every tool spawn spent on
// the key (retries included).
struct Cached {
    bool ok = false;
    std::wstring filePath;
    uint32_t spawnCount = 0;
};

// Lowercase hex of a 64-bit render key (16 digits, no prefix). The queue
// names work directories with it; tests use it to identify spawns in the
// fake tool's log.
std::wstring keyHex(uint64_t key);

class PlantumlRenderQueue {
public:
    explicit PlantumlRenderQueue(BackoffConfig backoff = {},
                                 size_t cacheCap = 32);
    ~PlantumlRenderQueue();

    PlantumlRenderQueue(const PlantumlRenderQueue&) = delete;
    PlantumlRenderQueue& operator=(const PlantumlRenderQueue&) = delete;

    // Publishes a render request for `blockId` (the layout block that
    // wants the image). Owner thread only. No-op when the queue is shut
    // down, when `key` is already cached, or when `key` is permanently
    // unrenderable. When the key is already pending, in flight, or finished
    // but not yet drained, the block attaches to that job instead of
    // scheduling a second spawn; the existing schedule - notably a retry
    // backoff window - is kept, so duplicates never reset it. Replacing a
    // pending request for the same block with a new key detaches the block
    // and drops the superseded job's work directory.
    void request(size_t blockId, uint64_t key, std::string finalSource,
                 int format, const Tool& tool, std::wstring workRoot);

    // Cache read only, NO locks: owner thread only. Null when `key` is
    // not cached (never rendered, in flight, failed, or evicted).
    std::shared_ptr<const Cached> lookup(uint64_t key) const;

    // Completion notification, invoked FROM THE WORKER THREAD once per
    // render attempt; `ok` is that attempt's outcome (a retryable failure
    // notifies false too). Notification only: the callback must not touch
    // the cache - it belongs to the owner thread.
    void setCompletion(std::function<void(uint64_t key, bool ok)> callback);

    // Owner thread: adopts every finished render into the cache (the only
    // cache mutation point) and returns how many records were adopted.
    size_t drainFinished();

    // Owner thread: drains, then waits until no render is RUNNING and
    // none is DUE. Returns anyway when `maxWaitMs` elapses. Jobs inside a
    // backoff window are scheduled, not busy, so they never delay it.
    void waitForIdle(int maxWaitMs = 30000);

    // Idempotent: stops and joins the worker, then deletes the work
    // directories of everything unadopted (pending jobs, leftover
    // finished records, queued deletions). Later request() calls are
    // no-ops; lookup() still reads the cache and waitForIdle() returns
    // immediately.
    void shutdown();

private:
    struct Job {
        // Every layout block waiting on this render, first requester first;
        // key-level dedup attaches further blocks instead of enqueueing.
        std::vector<size_t> blockIds;
        uint64_t key = 0;
        std::string source;
        int format = 0;
        Tool tool;
        std::wstring workDir;
        std::chrono::steady_clock::time_point notBefore;
        uint64_t seq = 0;  // tie-break: equal deadlines pop in request order
    };

    struct Finished {
        uint64_t key = 0;
        bool ok = false;
        std::wstring filePath;
        uint32_t spawnCount = 0;
        std::wstring workDir;
    };

    void workerLoop();
    void cleanupToDeleteLocked();

    BackoffConfig backoff_;
    size_t cacheCap_;

    // Owner-thread-only cache: read in lookup(), mutated in drainFinished().
    std::unordered_map<uint64_t, std::shared_ptr<const Cached>> cache_;
    std::deque<uint64_t> lru_;

    mutable std::mutex mutex_;
    std::condition_variable workCv_;
    std::condition_variable idleCv_;

    // Guarded by mutex_.
    std::vector<Job> pending_;  // scheduled renders, one entry per cache key
    std::vector<Finished> finished_;
    std::unordered_set<uint64_t> permanentKeys_;
    std::vector<std::wstring> toDelete_;
    std::function<void(uint64_t, bool)> completion_;
    uint64_t nextSeq_ = 0;
    bool running_ = false;
    uint64_t activeKey_ = 0;              // key of the running render
    std::vector<size_t> activeBlockIds_;  // its blocks (attachments included)
    bool stop_ = false;
    bool started_ = false;
    bool shutDown_ = false;
    std::thread worker_;

    // Worker-thread-only backoff bookkeeping.
    std::unordered_map<uint64_t, int> attempts_;
    std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> notBefore_;
};

// Removes leftover `%TEMP%\tinta-plantuml-<digits>` work roots older than
// 24 hours, skipping the live process's own root. Names whose suffix is not
// a pure digit run (for example the test suite's
// `tinta-plantuml-tests-<pid>`) are never touched; failures are tolerated.
void sweepStaleTempRoots();

}  // namespace plantuml

#endif  // TINTA_PLANTUML_QUEUE_H