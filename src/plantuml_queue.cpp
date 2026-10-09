// PlantUML async render queue implementation. The locking contract lives in
// plantuml_queue.h: one mutex guards the job handoff only, the image cache
// is owner-thread-only, and the worker never mutates the cache.

#include "plantuml_queue.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <system_error>
#include <utility>

#include <windows.h>

namespace plantuml {
namespace {

// Tolerate failures: a missing or briefly locked directory must never abort
// a sweep or a shutdown.
void removeDirTree(const std::wstring& dir) {
    if (dir.empty()) return;
    std::error_code ec;
    std::filesystem::remove_all(std::filesystem::path(dir), ec);
}

bool isAllDigits(const std::wstring& text) {
    if (text.empty()) return false;
    for (wchar_t c : text) {
        if (c < L'0' || c > L'9') return false;
    }
    return true;
}

// Block bookkeeping for a job shared by several layout blocks (key-level
// dedup): attach is idempotent, detach reports whether the block was there.
void attachBlockId(std::vector<size_t>& blockIds, size_t blockId) {
    if (std::find(blockIds.begin(), blockIds.end(), blockId) == blockIds.end()) {
        blockIds.push_back(blockId);
    }
}

bool detachBlockId(std::vector<size_t>& blockIds, size_t blockId) {
    const auto it = std::find(blockIds.begin(), blockIds.end(), blockId);
    if (it == blockIds.end()) return false;
    blockIds.erase(it);
    return true;
}

}  // namespace

std::wstring keyHex(uint64_t key) {
    static const wchar_t kDigits[] = L"0123456789abcdef";
    wchar_t buffer[17];
    for (int i = 15; i >= 0; --i) {
        buffer[i] = kDigits[key & 0xFULL];
        key >>= 4;
    }
    buffer[16] = L'\0';
    return std::wstring(buffer, 16);
}

void sweepStaleTempRoots() {
    wchar_t base[MAX_PATH] = {};
    const DWORD length = GetTempPathW(MAX_PATH, base);
    if (length == 0 || length >= MAX_PATH) return;

    const std::filesystem::path tempRoot(base);
    const std::wstring prefix = L"tinta-plantuml-";
    const auto cutoff =
        std::filesystem::file_time_type::clock::now() - std::chrono::hours(24);
    const unsigned long livePid =
        static_cast<unsigned long>(GetCurrentProcessId());

    std::error_code ec;
    std::filesystem::directory_iterator it(tempRoot, ec);
    const std::filesystem::directory_iterator end;
    while (!ec && it != end) {
        const std::filesystem::directory_entry entry = *it;
        it.increment(ec);

        const std::wstring name = entry.path().filename().wstring();
        if (name.size() <= prefix.size() ||
            name.compare(0, prefix.size(), prefix) != 0) {
            continue;
        }
        const std::wstring suffix = name.substr(prefix.size());
        // The digit-suffix rule is the safety net: tinta-plantuml-tests-*
        // and any other non-numeric name is never a queue work root.
        if (suffix.size() > 10 || !isAllDigits(suffix)) continue;

        unsigned long long pid = 0;
        for (wchar_t c : suffix) {
            pid = pid * 10ULL + static_cast<unsigned long long>(c - L'0');
        }
        if (pid == livePid) continue;

        std::error_code typeEc;
        if (!entry.is_directory(typeEc) || typeEc) continue;

        std::error_code timeEc;
        const std::filesystem::file_time_type written =
            entry.last_write_time(timeEc);
        if (timeEc || written >= cutoff) continue;

        removeDirTree(entry.path().wstring());
    }
}

PlantumlRenderQueue::PlantumlRenderQueue(BackoffConfig backoff,
                                         size_t cacheCap)
    : backoff_(std::move(backoff)), cacheCap_(cacheCap) {
    sweepStaleTempRoots();
}

PlantumlRenderQueue::~PlantumlRenderQueue() { shutdown(); }

void PlantumlRenderQueue::request(size_t blockId, uint64_t key,
                                  std::string finalSource, int format,
                                  const Tool& tool, std::wstring workRoot) {
    // Owner-thread cache read, deliberately off the lock: the cache never
    // lives under the mutex (drainFinished owns every mutation).
    if (cache_.count(key) != 0) return;

    std::unique_lock<std::mutex> lock(mutex_);
    if (shutDown_) return;
    if (permanentKeys_.count(key) != 0) return;

    // Key-level dedup: the same render already scheduled, in flight, or
    // finished but not yet drained must never spawn a second process. The
    // block is attached to the job that owns the key - the block set travels
    // with the job, so per-block coalescing stays meaningful - and the
    // existing schedule (notably a retry backoff window) is kept.
    for (Job& pending : pending_) {
        if (pending.key != key) continue;
        attachBlockId(pending.blockIds, blockId);
        lock.unlock();
        return;
    }
    if (running_ && activeKey_ == key) {
        attachBlockId(activeBlockIds_, blockId);
        lock.unlock();
        return;
    }
    for (const Finished& record : finished_) {
        if (record.key == key) {
            // The worker already produced the outcome; drainFinished() will
            // adopt it. No second spawn.
            lock.unlock();
            return;
        }
    }

    if (!started_) {
        started_ = true;
        worker_ = std::thread(&PlantumlRenderQueue::workerLoop, this);
    }

    // Per-block coalescing: a request with a new key supersedes this block's
    // waiting job. The block may also be attached to a shared job (key-level
    // dedup); detach it there too, dropping that job - and its work
    // directory - only when no block waits on it anymore.
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (!detachBlockId(it->blockIds, blockId)) {
            ++it;
            continue;
        }
        if (!it->blockIds.empty()) {
            ++it;
            continue;
        }
        toDelete_.push_back(std::move(it->workDir));
        it = pending_.erase(it);
    }

    Job job;
    job.blockIds.push_back(blockId);
    job.key = key;
    job.source = std::move(finalSource);
    job.format = format;
    job.tool = tool;
    job.workDir = std::move(workRoot);
    if (!job.workDir.empty() && job.workDir.back() != L'\\' &&
        job.workDir.back() != L'/') {
        job.workDir += L'\\';
    }
    job.workDir += keyHex(key);
    job.notBefore = std::chrono::steady_clock::now();
    job.seq = nextSeq_++;
    pending_.push_back(std::move(job));
    lock.unlock();
    workCv_.notify_all();
}

std::shared_ptr<const Cached> PlantumlRenderQueue::lookup(uint64_t key) const {
    const auto it = cache_.find(key);
    if (it == cache_.end()) return std::shared_ptr<const Cached>();
    return it->second;
}

void PlantumlRenderQueue::setCompletion(
    std::function<void(uint64_t, bool)> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    completion_ = std::move(callback);
}

size_t PlantumlRenderQueue::drainFinished() {
    std::vector<Finished> records;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        records.swap(finished_);
    }

    for (const Finished& record : records) {
        if (!record.ok) continue;  // failures never enter the cache
        Cached cached;
        cached.ok = true;
        cached.filePath = record.filePath;
        cached.spawnCount = record.spawnCount;

        const auto existing = cache_.find(record.key);
        if (existing != cache_.end()) {
            cache_.erase(existing);
            lru_.erase(std::remove(lru_.begin(), lru_.end(), record.key),
                       lru_.end());
        }
        cache_.emplace(record.key,
                       std::make_shared<const Cached>(std::move(cached)));
        lru_.push_back(record.key);

        while (cache_.size() > cacheCap_) {
            const uint64_t victim = lru_.front();
            lru_.pop_front();
            const auto it = cache_.find(victim);
            if (it == cache_.end()) continue;
            // The image lives in <workRoot>\\<hex>\\input.png|svg, so its
            // parent directory is the whole unadopted work directory.
            const std::wstring dir =
                std::filesystem::path(it->second->filePath)
                    .parent_path()
                    .wstring();
            removeDirTree(dir);
            cache_.erase(it);
        }
    }
    return records.size();
}

void PlantumlRenderQueue::waitForIdle(int maxWaitMs) {
    {
        // shutDown_ is guarded by the mutex; read it under the lock even
        // though both call sites currently live on the owner thread.
        std::lock_guard<std::mutex> lock(mutex_);
        if (shutDown_) return;
    }
    const int budget = maxWaitMs < 0 ? 0 : maxWaitMs;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(budget);
    for (;;) {
        drainFinished();
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (shutDown_) return;
            const auto now = std::chrono::steady_clock::now();
            bool due = false;
            for (const Job& entry : pending_) {
                if (entry.notBefore <= now) {
                    due = true;
                    break;
                }
            }
            if (!running_ && !due) return;
            if (now >= deadline) return;
            idleCv_.wait_for(lock, std::chrono::milliseconds(20));
        }
    }
}

void PlantumlRenderQueue::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (shutDown_) return;
        stop_ = true;
    }
    workCv_.notify_all();
    idleCv_.notify_all();
    if (worker_.joinable()) worker_.join();

    std::lock_guard<std::mutex> lock(mutex_);
    for (Job& entry : pending_) removeDirTree(entry.workDir);
    pending_.clear();
    for (const Finished& record : finished_) removeDirTree(record.workDir);
    finished_.clear();
    for (const std::wstring& dir : toDelete_) removeDirTree(dir);
    toDelete_.clear();
    shutDown_ = true;
}

void PlantumlRenderQueue::cleanupToDeleteLocked() {
    for (const std::wstring& dir : toDelete_) removeDirTree(dir);
    toDelete_.clear();
}

void PlantumlRenderQueue::workerLoop() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            for (;;) {
                if (stop_) return;
                cleanupToDeleteLocked();
                const auto now = std::chrono::steady_clock::now();
                auto best = pending_.end();
                for (auto it = pending_.begin(); it != pending_.end(); ++it) {
                    const Job& candidate = *it;
                    if (candidate.notBefore > now) continue;
                    if (best == pending_.end() ||
                        candidate.notBefore < best->notBefore ||
                        (candidate.notBefore == best->notBefore &&
                         candidate.seq < best->seq)) {
                        best = it;
                    }
                }
                if (best != pending_.end()) {
                    job = std::move(*best);
                    pending_.erase(best);
                    break;
                }
                running_ = false;
                idleCv_.notify_all();
                if (pending_.empty()) {
                    workCv_.wait(lock);
                } else {
                    auto next = pending_.front().notBefore;
                    for (const Job& entry : pending_) {
                        next = std::min(next, entry.notBefore);
                    }
                    workCv_.wait_until(lock, next);
                }
            }

            if (permanentKeys_.count(job.key) != 0) {
                // Another attempt proved this key permanently unrenderable
                // while this job waited: drop it without spawning.
                toDelete_.push_back(job.workDir);
                continue;
            }
            running_ = true;
            activeKey_ = job.key;
            activeBlockIds_ = job.blockIds;
        }

        const uint64_t jobKey = job.key;
        std::wstring outFile;
        std::wstring error;
        int exitCode = -1;
        const bool ok = renderSync(job.tool, job.source, job.format, job.workDir,
                                   outFile, 15000, error, &exitCode);
        // The queue intentionally does NOT mutate the cache here: the owner
        // thread adopts this record in drainFinished().
        std::function<void(uint64_t, bool)> callback;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            running_ = false;
            activeKey_ = 0;
            std::vector<size_t> attemptBlocks = std::move(activeBlockIds_);
            activeBlockIds_.clear();
            if (ok) {
                Finished record;
                record.key = jobKey;
                record.ok = true;
                record.filePath = std::move(outFile);
                record.spawnCount =
                    static_cast<uint32_t>(attempts_[jobKey] + 1);
                record.workDir = job.workDir;
                finished_.push_back(std::move(record));
                attempts_.erase(jobKey);
                notBefore_.erase(jobKey);
            } else if (exitCode == 100 || exitCode == 0) {
                // Exit 100 means "no diagram in this source"; exit 0 without
                // a usable artifact (after renderSync's resolution rules)
                // means the same for this key. Both are permanent.
                permanentKeys_.insert(jobKey);
                removeDirTree(job.workDir);
                Finished record;
                record.key = jobKey;
                record.ok = false;
                record.workDir = job.workDir;
                finished_.push_back(std::move(record));
                attempts_.erase(jobKey);
                notBefore_.erase(jobKey);
            } else {
                const int attempt = ++attempts_[jobKey];
                if (static_cast<size_t>(attempt) > backoff_.delaysMs.size()) {
                    // The finite retry schedule is exhausted: permanent.
                    permanentKeys_.insert(jobKey);
                    removeDirTree(job.workDir);
                    Finished record;
                    record.key = jobKey;
                    record.ok = false;
                    record.workDir = job.workDir;
                    finished_.push_back(std::move(record));
                    attempts_.erase(jobKey);
                    notBefore_.erase(jobKey);
                } else {
                    const int delayMs =
                        backoff_.delaysMs[static_cast<size_t>(attempt - 1)];
                    const auto retryAt = std::chrono::steady_clock::now() +
                                         std::chrono::milliseconds(delayMs);
                    notBefore_[jobKey] = retryAt;
                    // Drop every block that received a newer key while this
                    // attempt ran; the retry stays only while a block still
                    // waits on it.
                    std::vector<size_t> keep;
                    for (size_t blockId : attemptBlocks) {
                        bool superseded = false;
                        for (const Job& waiting : pending_) {
                            if (std::find(waiting.blockIds.begin(),
                                          waiting.blockIds.end(),
                                          blockId) != waiting.blockIds.end()) {
                                superseded = true;
                                break;
                            }
                        }
                        if (!superseded) keep.push_back(blockId);
                    }
                    if (keep.empty()) {
                        removeDirTree(job.workDir);
                    } else {
                        Job retry = std::move(job);
                        retry.blockIds = std::move(keep);
                        retry.notBefore = retryAt;
                        pending_.push_back(std::move(retry));
                    }
                }
            }
            callback = completion_;
            idleCv_.notify_all();
        }
        if (callback) callback(jobKey, ok);
    }
}

}  // namespace plantuml