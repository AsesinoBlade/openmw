#ifndef CONTENTLOADER_HPP
#define CONTENTLOADER_HPP

#include <MyGUI_TextIterator.h>
#include <filesystem>

#include "components/loadinglistener/loadinglistener.hpp"
#include <components/debug/debuglog.hpp>

namespace MWWorld
{

    struct ContentLoader
    {
        virtual ~ContentLoader() = default;

        virtual void load(const std::filesystem::path& filepath, int& index, Loading::Listener* listener) = 0;
    };

} /* namespace MWWorld */

#endif /* CONTENTLOADER_HPP */
