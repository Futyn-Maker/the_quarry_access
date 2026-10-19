#pragma once
// Helpers shared by the tests: strings compared as UTF-8 so that a failure prints readably, a
// temporary folder of files that a test writes and that is removed after it, and the lines the
// log stub kept.

#include "core/Strings.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace tests
{
    inline std::string U8(std::wstring_view wide)
    {
        return qa::str::WideToUtf8(wide);
    }

    // A folder of the test's own under the system's temporary folder, removed with everything
    // in it when the object goes.
    class TempDir
    {
    public:
        TempDir();
        ~TempDir();
        TempDir(const TempDir&) = delete;
        TempDir& operator=(const TempDir&) = delete;

        const std::filesystem::path& Path() const
        {
            return m_path;
        }
        // Writes the bytes as they are (no ending is changed) under the folder, creating the
        // folders on the way, and returns the file's path.
        std::filesystem::path Write(std::string_view relative, std::string_view bytes) const;
        std::string Read(std::string_view relative) const;

    private:
        std::filesystem::path m_path;
    };

    // What the modules under test wrote to the log since the last ClearLog, every level.
    std::vector<std::wstring> LogLines();
    void ClearLog();
    // How many of the kept lines contain the text.
    size_t LogLinesWith(std::wstring_view text);
}
