#pragma once

#include "codeharness/core/result.h"

#include <git2.h>

#include <array>
#include <string>
#include <string_view>

namespace codeharness
{

struct GitRuntime
{
    GitRuntime()
    {
        git_libgit2_init();
    }

    ~GitRuntime()
    {
        git_libgit2_shutdown();
    }

    GitRuntime(const GitRuntime&) = delete;
    auto operator=(const GitRuntime&) -> GitRuntime& = delete;
};

inline auto git_blob_hash_hex(std::string_view text) -> Result<std::string>
{
    GitRuntime runtime;

    git_oid oid{};
    if (git_odb_hash(&oid, text.data(), text.size(), GIT_OBJECT_BLOB) != 0)
    {
        return fail<std::string>(ErrorKind::Internal, "failed to hash content");
    }

    std::array<char, GIT_OID_SHA1_HEXSIZE + 1> buffer{};
    git_oid_tostr(buffer.data(), buffer.size(), &oid);
    return std::string{buffer.data()};
}

} // namespace codeharness
