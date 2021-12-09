#ifndef CONTENTLOADER_HPP
#define CONTENTLOADER_HPP

#include <filesystem>
#include <MyGUI_TextIterator.h>


#include <components/debug/debuglog.hpp>
#include "components/loadinglistener/loadinglistener.hpp"

namespace MWWorld
{


    struct ContentLoader
    {
        virtual ~ContentLoader() = default;

        virtual void load(const std::filesystem::path& filepath, int& index, Loading::Listener* listener) = 0;
};


} /* namespace MWWorld */

#endif /* CONTENTLOADER_HPP */

