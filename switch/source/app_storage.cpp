#include "app_storage.hpp"

#include <cerrno>
#include <cstdio>
#include <string>
#include <sys/stat.h>

namespace ttnx {
namespace {
const std::string settings_path = std::string(kAppDirectory) + "/settings.ini";
const std::string backup_path = settings_path + ".bak";
const std::string temporary_path = settings_path + ".tmp";
const std::string log_path = std::string(kAppDirectory) + "/logs/boot.log";

bool directory(const std::string& path) {
    if (mkdir(path.c_str(), 0777) == 0) return true;
    struct stat info{};
    return errno == EEXIST && stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

std::optional<core::Settings> read_settings(const std::string& path) {
    FILE* file = fopen(path.c_str(), "rb");
    if (!file) return std::nullopt;
    char buffer[513];
    const auto count = fread(buffer, 1, sizeof(buffer), file);
    const bool valid = !ferror(file) && count <= 512;
    fclose(file);
    if (!valid) return std::nullopt;
    return core::parse_settings(std::string_view(buffer, count));
}
} // namespace

bool initialize_storage() {
    if (!directory("sdmc:/switch") || !directory(kAppDirectory)) return false;
    return directory(std::string(kAppDirectory) + "/logs");
}

core::Settings load_settings() {
    if (const auto settings = read_settings(settings_path)) return *settings;
    if (const auto backup = read_settings(backup_path)) return *backup;
    return {};
}

bool save_settings(const core::Settings& settings) {
    const auto serialized = core::serialize_settings(settings);
    FILE* file = fopen(temporary_path.c_str(), "wb");
    if (!file) return false;
    const bool written = fwrite(serialized.data(), 1, serialized.size(), file) == serialized.size();
    const bool flushed = fflush(file) == 0;
    const bool closed = fclose(file) == 0;
    if (!written || !flushed || !closed) return false;
    const auto verified = read_settings(temporary_path);
    if (!verified || verified->show_fps != settings.show_fps) return false;

    // libnx SD rename need not overwrite an existing destination. Keep a last
    // valid backup, including when boot recovered from a damaged primary file.
    if (read_settings(settings_path)) {
        if (remove(backup_path.c_str()) != 0 && errno != ENOENT) return false;
        if (rename(settings_path.c_str(), backup_path.c_str()) != 0) return false;
    } else if (remove(settings_path.c_str()) != 0 && errno != ENOENT) {
        return false;
    }
    if (rename(temporary_path.c_str(), settings_path.c_str()) != 0) return false;
    return true;
}

void record_boot_event(const char* event) {
    // Truncate at launch; no unbounded logs and no redirect of library logging.
    FILE* file = fopen(log_path.c_str(), "w");
    if (!file) return;
    fprintf(file, "TizenTube NX 0.1.0-m1\n%s\n", event);
    fclose(file);
}
} // namespace ttnx
