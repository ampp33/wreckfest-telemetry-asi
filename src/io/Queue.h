#pragma once

#include <string>
#include <vector>

#include "io/Config.h"
#include "json.hpp"

namespace wreckfest_telemetry {

std::string NowTimestamp();  // "%Y-%m-%d %H:%M:%S", local time

struct QueueEntry {
    std::string queued_at;
    int attempts = 0;
    std::string last_error;
    nlohmann::json payload;  // never includes api_key -- injected only at send time
};

std::vector<QueueEntry> ReadQueue(const std::wstring& path);

// Atomic rewrite (temp file + FlushFileBuffers + MoveFileEx) so a crash
// mid-write can never leave a truncated queue. An empty list deletes the
// file rather than leaving a zero-byte one behind.
void WriteQueue(const std::wstring& path, const std::vector<QueueEntry>& entries);

// Appends one payload. Returns the resulting queue length (0 if it
// couldn't be written at all).
int EnqueuePayload(const std::wstring& path, nlohmann::json payload, const std::string& error);

void AppendDeadLetter(const std::wstring& path, const QueueEntry& entry, const std::string& error);

struct FlushResult {
    int sent;
    int remaining;
    int dead;
};

// Replays the queue oldest-first, rewriting the queue file after every
// individual successful send (a crash mid-flush can't double-post). Stops
// at the first retryable failure rather than burning a timeout on every
// remaining entry; dead-letters and continues past permanent failures.
FlushResult FlushQueue(const ApiConfig& config, const std::wstring& queuePath, const std::wstring& deadPath,
                        double timeoutSeconds = 5.0);

// The send path for a freshly captured race: drains any existing backlog
// first (oldest has to land first), then posts this one -- or queues/
// dead-letters it on failure, per the same rules as FlushQueue.
void PostOrQueue(const ApiConfig& config, nlohmann::json payload, const std::wstring& queuePath,
                 const std::wstring& deadPath);

}  // namespace wreckfest_telemetry
