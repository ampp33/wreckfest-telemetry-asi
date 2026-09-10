#include "io/Queue.h"

#include <windows.h>

#include <ctime>
#include <fstream>
#include <sstream>

#include "net/SupabaseClient.h"

namespace wreckfest_telemetry {

std::string NowTimestamp() {
    std::time_t t = std::time(nullptr);
    std::tm tmVal{};
    localtime_s(&tmVal, &t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmVal);
    return buf;
}

namespace {

bool AtomicWriteFile(const std::wstring& path, const std::string& content) {
    std::wstring tmp = path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(h, content.data(), static_cast<DWORD>(content.size()), &written, nullptr);
    if (ok) FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok || written != content.size()) return false;
    return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

}  // namespace

std::vector<QueueEntry> ReadQueue(const std::wstring& path) {
    std::vector<QueueEntry> entries;
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) return entries;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        try {
            auto j = nlohmann::json::parse(line);
            if (!j.contains("payload") || !j["payload"].is_object()) continue;
            QueueEntry e;
            e.queued_at = j.value("queued_at", "");
            e.attempts = j.value("attempts", 0);
            e.last_error = j.value("last_error", "");
            e.payload = j["payload"];
            entries.push_back(std::move(e));
        } catch (const nlohmann::json::exception&) {
            continue;
        }
    }
    return entries;
}

void WriteQueue(const std::wstring& path, const std::vector<QueueEntry>& entries) {
    if (entries.empty()) {
        DeleteFileW(path.c_str());
        return;
    }
    std::ostringstream content;
    for (const auto& e : entries) {
        nlohmann::json j = {
            {"queued_at", e.queued_at}, {"attempts", e.attempts}, {"last_error", e.last_error}, {"payload", e.payload}};
        content << j.dump() << "\n";
    }
    AtomicWriteFile(path, content.str());
}

int EnqueuePayload(const std::wstring& path, nlohmann::json payload, const std::string& error) {
    nlohmann::json entry = {
        {"queued_at", NowTimestamp()}, {"attempts", 1}, {"last_error", error}, {"payload", std::move(payload)}};
    std::ofstream out(path.c_str(), std::ios::app | std::ios::binary);
    if (!out) return 0;
    out << entry.dump() << "\n";
    out.close();
    return static_cast<int>(ReadQueue(path).size());
}

void AppendDeadLetter(const std::wstring& path, const QueueEntry& entry, const std::string& error) {
    nlohmann::json j = {{"queued_at", entry.queued_at},
                         {"attempts", entry.attempts},
                         {"last_error", error},
                         {"payload", entry.payload},
                         {"failed_at", NowTimestamp()}};
    std::ofstream out(path.c_str(), std::ios::app | std::ios::binary);
    if (out) out << j.dump() << "\n";
}

FlushResult FlushQueue(const ApiConfig& config, const std::wstring& queuePath, const std::wstring& deadPath,
                        double timeoutSeconds) {
    if (!ApiConfigComplete(config)) return {0, 0, 0};
    auto entries = ReadQueue(queuePath);
    if (entries.empty()) return {0, 0, 0};

    int sent = 0;
    int dead = 0;
    while (!entries.empty()) {
        QueueEntry& entry = entries.front();
        auto result = PostPayload(config, entry.payload, timeoutSeconds);
        if (result.ok) {
            entries.erase(entries.begin());
            WriteQueue(queuePath, entries);
            ++sent;
            continue;
        }
        if (result.retryable) {
            entry.attempts += 1;
            entry.last_error = result.message;
            WriteQueue(queuePath, entries);
            return {sent, static_cast<int>(entries.size()), dead};
        }
        QueueEntry deadEntry = entries.front();
        entries.erase(entries.begin());
        WriteQueue(queuePath, entries);
        AppendDeadLetter(deadPath, deadEntry, result.message);
        ++dead;
    }
    return {sent, 0, dead};
}

void PostOrQueue(const ApiConfig& config, nlohmann::json payload, const std::wstring& queuePath,
                 const std::wstring& deadPath) {
    int remaining = 0;
    if (!ReadQueue(queuePath).empty()) {
        remaining = FlushQueue(config, queuePath, deadPath).remaining;
    }
    if (remaining) {
        EnqueuePayload(queuePath, std::move(payload), "queued behind an unflushed backlog");
        return;
    }

    auto result = PostPayload(config, payload);
    if (result.ok) return;
    if (result.retryable) {
        EnqueuePayload(queuePath, std::move(payload), result.message);
        return;
    }
    QueueEntry entry;
    entry.queued_at = NowTimestamp();
    entry.attempts = 1;
    entry.payload = std::move(payload);
    AppendDeadLetter(deadPath, entry, result.message);
}

}  // namespace wreckfest_telemetry
