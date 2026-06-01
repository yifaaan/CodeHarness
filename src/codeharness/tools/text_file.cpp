#include "codeharness/tools/text_file.h"

#include <fstream>
#include <sstream>
#include <system_error>

namespace codeharness
{

auto read_text_file(const std::filesystem::path& path) -> Result<std::string>
{
    std::ifstream file{path, std::ios::binary};
    if (!file)
    {
        return fail<std::string>(ErrorKind::Io, "failed to open file " + path.string());
    }

    std::ostringstream oss;
    oss << file.rdbuf();

    if (!file.good() && !file.eof())
    {
        return fail<std::string>(ErrorKind::Io, "failed to read file " + path.string());
    }

    return oss.str();
}

auto atomic_write_text_file(const std::filesystem::path& target_path, std::string_view content) -> Result<void>
{
    auto tmp_path = target_path;
    tmp_path += ".tmp";

    std::ofstream file{tmp_path, std::ios::binary};
    if (!file)
    {
        return fail<void>(ErrorKind::Io, "failed to create temp file: " + tmp_path.string());
    }

    file << content;
    file.flush();

    if (!file.good())
    {
        std::error_code ignored;
        std::filesystem::remove(tmp_path, ignored);
        return fail<void>(ErrorKind::Io, "failed to write file content");
    }

    file.close();

    std::error_code error;
    std::filesystem::rename(tmp_path, target_path, error);
    if (error)
    {
        std::error_code ignored;
        std::filesystem::remove(tmp_path, ignored);
        return fail<void>(ErrorKind::Io, "failed to rename temp file: " + error.message());
    }

    return {};
}

} // namespace codeharness
