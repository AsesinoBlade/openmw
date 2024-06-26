#ifndef GAME_MWCLASS_POTION_H
#define GAME_MWCLASS_POTION_H

#include "../mwworld/registeredclass.hpp"

namespace MWClass
{
    class Potion : public MWWorld::RegisteredClass<Potion>
    {
        friend MWWorld::RegisteredClass<Potion>;

        Potion();

        MWWorld::Ptr copyToCellImpl(const MWWorld::ConstPtr& ptr, MWWorld::CellStore& cell) const override;

        bool isPoison(const MWWorld::ConstPtr& ptr) const;

    public:
        void insertObjectRendering(const MWWorld::Ptr& ptr, const std::string& model,
            MWRender::RenderingInterface& renderingInterface) const override;
        ///< Add reference into a cell for rendering

        std::string_view getName(const MWWorld::ConstPtr& ptr) const override;
        ///< \return name or ID; can return an empty string.

        bool isItem(const MWWorld::ConstPtr&) const override { return true; }

        std::unique_ptr<MWWorld::Action> activate(const MWWorld::Ptr& ptr, const MWWorld::Ptr& actor) const override;
        ///< Generate action for activation

        std::string_view getSearchTags(const MWWorld::ConstPtr& ptr) const override;
        /// <\ return meta data relevant for searches

        MWGui::ToolTipInfo getToolTipInfo(const MWWorld::ConstPtr& ptr, int count) const override;
        ///< @return the content of the tool tip to be displayed. raises exception if the object has no tooltip.

        ESM::RefId getScript(const MWWorld::ConstPtr& ptr) const override;
        ///< Return name of the script attached to ptr

        ESM::RefId getPoison(const MWWorld::ConstPtr& ptr) const override;

        int getValue(const MWWorld::ConstPtr& ptr) const override;
        ///< Return trade value of the object. Throws an exception, if the object can't be traded.

        std::unique_ptr<MWWorld::Action> use(const MWWorld::Ptr& ptr, bool force = false) const override;
        ///< Generate action for using via inventory menu

        const ESM::RefId& getUpSoundId(const MWWorld::ConstPtr& ptr) const override;
        ///< Return the pick up sound Id

        const ESM::RefId& getDownSoundId(const MWWorld::ConstPtr& ptr) const override;
        ///< Return the put down sound Id

        const std::string& getInventoryIcon(const MWWorld::ConstPtr& ptr) const override;
        ///< Return name of inventory icon.

        std::string_view getModel(const MWWorld::ConstPtr& ptr) const override;

        float getWeight(const MWWorld::ConstPtr& ptr) const override;

        bool canSell(const MWWorld::ConstPtr& item, int npcServices) const override;
    };
}

#endif
