#ifndef COMPONENT_ESM_FORMAT_H
#define COMPONENT_ESM_FORMAT_H

#include "components/esm/fourcc.hpp"

#include <cstdint>
#include <iosfwd>
#include <string_view>

namespace ESM
{
    enum class Format : std::uint32_t
    {
        Tes3 = fourCC("TES3"),
        Tes4 = fourCC("TES4"),
    };

    Format readFormat(std::istream& stream);
    Format readFormat(std::istream& stream, const std::filesystem::path& filepath);
    Format parseFormat(std::string_view value);
}

#endif
