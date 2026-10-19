#include "Support.hpp"

#include <atomic>
#include <chrono>
#include <fstream>
#include <iterator>

namespace tests
{
    namespace
    {
        std::atomic<int> g_folders{0};
    }

    TempDir::TempDir()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = std::filesystem::temp_directory_path() / ("QuarryAccessTests-" + std::to_string(stamp) + "-" + std::to_string(g_folders++));
        std::filesystem::create_directories(m_path);
    }

    TempDir::~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(m_path, ec);
    }

    std::filesystem::path TempDir::Write(std::string_view relative, std::string_view bytes) const
    {
        const auto path = m_path / std::filesystem::path(std::string(relative));
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return path;
    }

    std::string TempDir::Read(std::string_view relative) const
    {
        std::ifstream in(m_path / std::filesystem::path(std::string(relative)), std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
}
