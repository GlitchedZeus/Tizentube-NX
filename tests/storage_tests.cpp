#include "app_storage.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>

int main() {
    namespace fs = std::filesystem;
    char temporary[] = "/tmp/ttnx-storage-XXXXXX";
    const char* root = mkdtemp(temporary);
    if (!root) return 1;
    const auto original = fs::current_path();
    fs::current_path(root);
    fs::create_directory("sdmc:");
    int failures = 0;
    auto expect = [&](bool condition, const char* message) {
        if (!condition) { std::cerr << message << '\n'; ++failures; }
    };
    expect(ttnx::initialize_storage(), "create application directories");
    expect(ttnx::initialize_storage(), "existing directories supported");
    expect(!ttnx::load_settings().show_fps, "missing file uses defaults");
    expect(ttnx::save_settings({true}), "first save");
    expect(ttnx::load_settings().show_fps, "first save reloads");
    expect(ttnx::save_settings({false}), "replacement save");
    expect(!ttnx::load_settings().show_fps, "replacement reloads");
    const auto primary = fs::path(ttnx::kAppDirectory) / "settings.ini";
    { std::ofstream file(primary); file << "version=1\nshow_fps="; }
    expect(ttnx::load_settings().show_fps, "damaged primary recovers last valid backup");
    expect(ttnx::save_settings({false}), "save after recovery");
    fs::remove(primary);
    expect(ttnx::load_settings().show_fps, "recovered backup remains valid after save");
    // A stale or incomplete temporary file must never displace the backup.
    { std::ofstream file(primary.string() + ".tmp"); file << "invalid"; }
    expect(ttnx::load_settings().show_fps, "temporary file ignored on boot");
    fs::remove(primary.string() + ".tmp");
    fs::create_directory(primary.string() + ".tmp");
    expect(!ttnx::save_settings({false}), "temporary write failure reported");
    expect(ttnx::load_settings().show_fps, "write failure preserves saved preference");
    fs::current_path(original);
    fs::remove_all(root);
    return failures ? 1 : 0;
}
