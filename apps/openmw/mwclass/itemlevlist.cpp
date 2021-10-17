#include "itemlevlist.hpp"

#include <components/esm3/loadlevlist.hpp>

namespace MWClass
{
    ItemLevList::ItemLevList()
        : MWWorld::RegisteredClass<ItemLevList>(ESM::ItemLevList::sRecordId)
    {
    }

    std::string_view ItemLevList::getName(const MWWorld::ConstPtr& ptr) const
    {
        return {};
    }

    std::string_view ItemLevList::getSearchTags(const MWWorld::ConstPtr& ptr) const
    {
        return " item level list ";
    }


    bool ItemLevList::hasToolTip(const MWWorld::ConstPtr& ptr) const
    {
        return false;
    }
}
