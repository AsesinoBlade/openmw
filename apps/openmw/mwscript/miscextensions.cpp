#include "miscextensions.hpp"

#include <chrono>
#include <cstdlib>
#include <iomanip>

#include <sstream>
#include <components/misc/rng.hpp>

#include "../Boost/boost/property_tree/ptree.hpp"
#include "../Boost/boost/property_tree/xml_parser.hpp"
#include "../Boost/boost/foreach.hpp"
#include "../Boost/boost/filesystem.hpp"

#include <components/compiler/extensions.hpp>
#include <components/compiler/locals.hpp>
#include <components/compiler/opcodes.hpp>

#include <components/debug/debuglog.hpp>

#include <components/interpreter/interpreter.hpp>
#include <components/interpreter/opcodes.hpp>
#include <components/interpreter/runtime.hpp>

#include <components/misc/resourcehelpers.hpp>
#include <components/resource/resourcesystem.hpp>

#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadcrea.hpp>

#include "../openmw//apps/esmtool/labels.hpp"

#include <components/vfs/manager.hpp>

#include <components/misc/rng.hpp>

#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>

#include <components/sceneutil/positionattitudetransform.hpp>

#include <components/esm3/loadacti.hpp>
#include <components/esm3/loadalch.hpp>
#include <components/esm3/loadappa.hpp>
#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadbody.hpp>
#include <components/esm3/loadbook.hpp>
#include <components/esm3/loadclot.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loaddoor.hpp>
#include <components/esm3/loadingr.hpp>
#include <components/esm3/loadlevlist.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/esm3/loadlock.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadmisc.hpp>
#include <components/esm3/loadprob.hpp>
#include <components/esm3/loadrepa.hpp>
#include <components/esm3/loadscpt.hpp>
#include <components/esm3/loadstat.hpp>
#include <components/esm3/loadweap.hpp>

#include <components/files/conversion.hpp>
#include <components/misc/strings/conversion.hpp>
#include <components/sceneutil/util.hpp>
#include <components/vfs/manager.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/luamanager.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/scriptmanager.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwclass/actor.hpp"
#include "../mwclass/npc.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/player.hpp"
#include "../mwworld/containerstore.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/containerstore.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwworld/manualref.hpp"
#include "../mwworld/player.hpp"

#include "../mwmechanics/actorutil.hpp"
#include "../mwmechanics/aicast.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/npcstats.hpp"
#include "../mwmechanics/spellcasting.hpp"

#include "../mwrender/animation.hpp"

#include "interpretercontext.hpp"
#include "ref.hpp"

#include "apps/openmw/mwbase/journal.hpp"
#include "apps/openmw/mwmechanics/aifollow.hpp"
#include "apps/openmw/mwmechanics/aiwander.hpp"
#include "apps/openmw/mwworld/worldimp.hpp"
#include "apps/openmw/mwworld/worldmodel.hpp"

#include <boost/iostreams/filter/zlib.hpp>
#include <components/files/configurationmanager.hpp>



namespace
{
    struct TextureFetchVisitor : osg::NodeVisitor
    {
        std::vector<std::pair<std::string, std::string>> mTextures;

        TextureFetchVisitor(osg::NodeVisitor::TraversalMode mode = TRAVERSE_ALL_CHILDREN)
            : osg::NodeVisitor(mode)
        {
        }

        void apply(osg::Node& node) override
        {
            const osg::StateSet* stateset = node.getStateSet();
            if (stateset)
            {
                const osg::StateSet::TextureAttributeList& texAttributes = stateset->getTextureAttributeList();
                for (unsigned i = 0; i < static_cast<unsigned>(texAttributes.size()); i++)
                {
                    const osg::StateAttribute* attr = stateset->getTextureAttribute(i, osg::StateAttribute::TEXTURE);
                    if (!attr)
                        continue;
                    const osg::Texture* texture = attr->asTexture();
                    if (!texture)
                        continue;
                    const osg::Image* image = texture->getImage(0);
                    std::string fileName;
                    if (image)
                        fileName = image->getFileName();
                    mTextures.emplace_back(SceneUtil::getTextureType(*stateset, *texture, i), fileName);
                }
            }

            traverse(node);
        }
    };

    void addToLevList(ESM::LevelledListBase* list, const ESM::RefId& itemId, uint16_t level)
    {
        for (auto& levelItem : list->mList)
        {
            if (levelItem.mLevel == level && itemId == levelItem.mId)
                return;
        }

        ESM::LevelledListBase::LevelItem item;
        item.mId = itemId;
        item.mLevel = level;
        list->mList.push_back(item);
    }

    void removeFromLevList(ESM::LevelledListBase* list, const ESM::RefId& itemId, int level)
    {
        // level of -1 removes all items with that itemId
        for (std::vector<ESM::LevelledListBase::LevelItem>::iterator it = list->mList.begin(); it != list->mList.end();)
        {
            if (level != -1 && it->mLevel != level)
            {
                ++it;
                continue;
            }
            if (itemId == it->mId)
                it = list->mList.erase(it);
            else
                ++it;
        }
    }

}

namespace MWScript
{
    namespace Misc
    {        
        class OpMenuMode : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                runtime.push(MWBase::Environment::get().getWindowManager()->isGuiMode());
            }
        };

        class OpRandom : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                Interpreter::Type_Integer limit = runtime[0].mInteger;
                runtime.pop();

                if (limit < 0)
                    throw std::runtime_error("random: argument out of range (Don't be so negative!)");

                auto& prng = MWBase::Environment::get().getWorld()->getPrng();
                runtime.push(static_cast<Interpreter::Type_Float>(::Misc::Rng::rollDice(limit, prng))); // [o, limit)
            }
        };

        template <class R>
        class OpStartScript : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                MWWorld::Ptr target = R()(runtime, false);
                ESM::RefId name = ESM::RefId::stringRefId(runtime.getStringLiteral(runtime[0].mInteger));
                runtime.pop();

                if (!MWBase::Environment::get().getESMStore()->get<ESM::Script>().search(name))
                {
                    runtime.getContext().report(
                        "Failed to start global script '" + name.getRefIdString() + "': script record not found");
                    return;
                }

                MWBase::Environment::get().getScriptManager()->getGlobalScripts().addScript(name, target);
            }
        };

        class OpScriptRunning : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                const ESM::RefId& name = ESM::RefId::stringRefId(runtime.getStringLiteral(runtime[0].mInteger));
                runtime.pop();
                runtime.push(MWBase::Environment::get().getScriptManager()->getGlobalScripts().isRunning(name));
            }
        };

        class OpStopScript : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                const ESM::RefId& name = ESM::RefId::stringRefId(runtime.getStringLiteral(runtime[0].mInteger));
                runtime.pop();

                if (!MWBase::Environment::get().getESMStore()->get<ESM::Script>().search(name))
                {
                    runtime.getContext().report(
                        "Failed to stop global script '" + name.getRefIdString() + "': script record not found");
                    return;
                }

                MWBase::Environment::get().getScriptManager()->getGlobalScripts().removeScript(name);
            }
        };

        class OpGetSecondsPassed : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                runtime.push(MWBase::Environment::get().getFrameDuration());
            }
        };

        template <class R>
        class OpEnable : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                MWWorld::Ptr ptr = R()(runtime);
                MWBase::Environment::get().getWorld()->enable(ptr);
            }
        };

        template <class R>
        class OpDisable : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                MWWorld::Ptr ptr;
                if (!R::implicit)
                {
                    ESM::RefId name = ESM::RefId::stringRefId(runtime.getStringLiteral(runtime[0].mInteger));
                    runtime.pop();

                    ptr = MWBase::Environment::get().getWorld()->searchPtr(name, false);
                    // We don't normally want to let this go, but some mods insist on trying this
                    if (ptr.isEmpty())
                    {
                        const std::string error = "Failed to find an instance of object " + name.toDebugString();
                        runtime.getContext().report(error);
                        Log(Debug::Error) << error;
                        return;
                    }
                }
                else
                {
                    ptr = R()(runtime);
                }
                MWBase::Environment::get().getWorld()->disable(ptr);
            }
        };

        template <class R>
        class OpGetDisabled : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                MWWorld::Ptr ptr = R()(runtime);
                runtime.push(!ptr.getRefData().isEnabled());
            }
        };

        class OpPlayBink : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                std::string_view name = runtime.getStringLiteral(runtime[0].mInteger);
                runtime.pop();

                bool allowSkipping = runtime[0].mInteger != 0;
                runtime.pop();

                MWBase::Environment::get().getWindowManager()->playVideo(name, allowSkipping);
            }
        };

        class OpGetPcSleep : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                runtime.push(MWBase::Environment::get().getWindowManager()->getPlayerSleeping());
            }
        };

        class OpGetPcJumping : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                MWBase::World* world = MWBase::Environment::get().getWorld();
                runtime.push(world->getPlayer().getJumping());
            }
        };

        class OpWakeUpPc : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                MWBase::Environment::get().getWindowManager()->wakeUpPlayer();
            }
        };

        class OpXBox : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override { runtime.push(0); }
        };

        template <class R>
        class OpOnActivate : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                MWWorld::Ptr ptr = R()(runtime);

                runtime.push(ptr.getRefData().onActivate());
            }
        };

        template <class R>
        class OpActivate : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                InterpreterContext& context = static_cast<InterpreterContext&>(runtime.getContext());

                MWWorld::Ptr ptr = R()(runtime);

                if (ptr.getRefData().activateByScript() || ptr.getContainerStore())
                    context.executeActivation(ptr, MWMechanics::getPlayer());
            }
        };

        template <class R>
        class OpLock : public Interpreter::Opcode1
        {
        public:
            void execute(Interpreter::Runtime& runtime, unsigned int arg0) override
            {
                MWWorld::Ptr ptr = R()(runtime);

                Interpreter::Type_Integer lockLevel = ptr.getCellRef().getLockLevel();
                if (lockLevel == 0)
                { // no lock level was ever set, set to 100 as default
                    lockLevel = 100;
                }

                if (arg0 == 1)
                {
                    lockLevel = runtime[0].mInteger;
                    runtime.pop();
                }

                ptr.getCellRef().lock(lockLevel);

                // Instantly reset door to closed state
                // This is done when using Lock in scripts, but not when using Lock spells.
                if (ptr.getType() == ESM::Door::sRecordId && !ptr.getCellRef().getTeleport())
                {
                    MWBase::Environment::get().getWorld()->activateDoor(ptr, MWWorld::DoorState::Idle);
                }
            }
        };

        template <class R>
        class OpUnlock : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                MWWorld::Ptr ptr = R()(runtime);
                if (ptr.getCellRef().isLocked())
                    ptr.getCellRef().unlock();
            }
        };

        class OpToggleCollisionDebug : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                bool enabled = MWBase::Environment::get().getWorld()->toggleRenderMode(MWRender::Render_CollisionDebug);

                runtime.getContext().report(
                    enabled ? "Collision Mesh Rendering -> On" : "Collision Mesh Rendering -> Off");
            }
        };

        class OpToggleCollisionBoxes : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                bool enabled = MWBase::Environment::get().getWorld()->toggleRenderMode(MWRender::Render_CollisionDebug);

                runtime.getContext().report(
                    enabled ? "Collision Mesh Rendering -> On" : "Collision Mesh Rendering -> Off");
            }
        };

        class OpToggleWireframe : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                bool enabled = MWBase::Environment::get().getWorld()->toggleRenderMode(MWRender::Render_Wireframe);

                runtime.getContext().report(enabled ? "Wireframe Rendering -> On" : "Wireframe Rendering -> Off");
            }
        };

        class OpToggleBorders : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                bool enabled = MWBase::Environment::get().getWorld()->toggleBorders();

                runtime.getContext().report(enabled ? "Border Rendering -> On" : "Border Rendering -> Off");
            }
        };

        class OpTogglePathgrid : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                bool enabled = MWBase::Environment::get().getWorld()->toggleRenderMode(MWRender::Render_Pathgrid);

                runtime.getContext().report(enabled ? "Path Grid rendering -> On" : "Path Grid Rendering -> Off");
            }
        };

        class OpFadeIn : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                Interpreter::Type_Float time = runtime[0].mFloat;
                runtime.pop();

                MWBase::Environment::get().getWindowManager()->fadeScreenIn(time, false);
            }
        };

        class OpFadeOut : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                Interpreter::Type_Float time = runtime[0].mFloat;
                runtime.pop();

                MWBase::Environment::get().getWindowManager()->fadeScreenOut(time, false);
            }
        };

        class OpFadeTo : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                Interpreter::Type_Float alpha = runtime[0].mFloat;
                runtime.pop();

                Interpreter::Type_Float time = runtime[0].mFloat;
                runtime.pop();

                MWBase::Environment::get().getWindowManager()->fadeScreenTo(static_cast<int>(alpha), time, false);
            }
        };

        class OpToggleWater : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                runtime.getContext().report(
                    MWBase::Environment::get().getWorld()->toggleWater() ? "Water -> On" : "Water -> Off");
            }
        };

        class OpToggleWorld : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                runtime.getContext().report(
                    MWBase::Environment::get().getWorld()->toggleWorld() ? "World -> On" : "World -> Off");
            }
        };

        class OpDontSaveObject : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                // We are ignoring the DontSaveObject statement for now. Probably not worth
                // bothering with. The incompatibility we are creating should be marginal at most.
            }
        };

        class OpPcForce1stPerson : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                if (!MWBase::Environment::get().getWorld()->isFirstPerson())
                    MWBase::Environment::get().getWorld()->togglePOV(true);
            }
        };

        class OpPcForce3rdPerson : public Interpreter::Opcode0
        {
            void execute(Interpreter::Runtime& runtime) override
            {
                if (MWBase::Environment::get().getWorld()->isFirstPerson())
                    MWBase::Environment::get().getWorld()->togglePOV(true);
            }
        };

        class OpPcGet3rdPerson : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                runtime.push(!MWBase::Environment::get().getWorld()->isFirstPerson());
            }
        };

        class OpToggleVanityMode : public Interpreter::Opcode0
        {
            static bool sActivate;

        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                MWBase::World* world = MWBase::Environment::get().getWorld();

                if (world->toggleVanityMode(sActivate))
                {
                    runtime.getContext().report(sActivate ? "Vanity Mode -> On" : "Vanity Mode -> Off");
                    sActivate = !sActivate;
                }
                else
                {
                    runtime.getContext().report("Vanity Mode -> No");
                }
            }
        };
        bool OpToggleVanityMode::sActivate = true;

        template <class R>
        class OpGetLocked : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                MWWorld::Ptr ptr = R()(runtime);

                runtime.push(ptr.getCellRef().isLocked());
            }
        };

        template <class R>
        class OpGetEffect : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                MWWorld::Ptr ptr = R()(runtime);

                const std::string_view effectName = runtime.getStringLiteral(runtime[0].mInteger);
                runtime.pop();

                if (!ptr.getClass().isActor())
                {
                    runtime.push(0);
                    return;
                }

                ESM::RefId key;

                if (const auto k = ::Misc::StringUtils::toNumeric<long>(effectName);
                    k.has_value() && *k >= 0 && *k <= 32767)
                    key = ESM::MagicEffect::indexToRefId(*k);
                else
                    key = ESM::MagicEffect::effectGmstIdToRefId(effectName);

                const MWMechanics::CreatureStats& stats = ptr.getClass().getCreatureStats(ptr);
                for (const auto& spell : stats.getActiveSpells())
                {
                    for (const auto& effect : spell.getEffects())
                    {
                        if (effect.mFlags & ESM::ActiveEffect::Flag_Remove && effect.mEffectId == key)
                        {
                            runtime.push(1);
                            return;
                        }
                    }
                }
                runtime.push(0);
            }
        };

        template <class R>
        class OpAddSoulGem : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                MWWorld::Ptr ptr = R()(runtime);

                ESM::RefId creature = ESM::RefId::stringRefId(runtime.getStringLiteral(runtime[0].mInteger));
                runtime.pop();

                ESM::RefId gem = ESM::RefId::stringRefId(runtime.getStringLiteral(runtime[0].mInteger));
                runtime.pop();

                if (!ptr.getClass().isActor())
                    return;

                const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();
                store.get<ESM::Creature>().find(
                    creature); // This line throws an exception if it can't find the creature

                MWWorld::Ptr item = *ptr.getClass().getContainerStore(ptr).add(gem, 1);

                // Set the soul on just one of the gems, not the whole stack
                item.getContainerStore()->unstack(item);
                item.getCellRef().setSoul(creature);

                // Restack the gem with other gems with the same soul
                item.getContainerStore()->restack(item);
            }
        };

        template <class R>
        class OpRemoveSoulGem : public Interpreter::Opcode1
        {
        public:
            void execute(Interpreter::Runtime& runtime, unsigned int arg0) override
            {
                MWWorld::Ptr ptr = R()(runtime);

                ESM::RefId soul = ESM::RefId::stringRefId(runtime.getStringLiteral(runtime[0].mInteger));
                runtime.pop();

                // throw away additional arguments
                for (unsigned int i = 0; i < arg0; ++i)
                    runtime.pop();

                if (!ptr.getClass().isActor())
                    return;

                MWWorld::ContainerStore& store = ptr.getClass().getContainerStore(ptr);
                for (MWWorld::ContainerStoreIterator it = store.begin(); it != store.end(); ++it)
                {
                    if (it->getCellRef().getSoul() == soul)
                    {
                        store.remove(*it, 1);
                        return;
                    }
                }
            }
        };

        template <class R>
        class OpDrop : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {

                MWWorld::Ptr ptr = R()(runtime);

                ESM::RefId item = ESM::RefId::stringRefId(runtime.getStringLiteral(runtime[0].mInteger));
                runtime.pop();

                Interpreter::Type_Integer amount = runtime[0].mInteger;
                runtime.pop();

                if (amount < 0)
                    throw std::runtime_error("amount must be non-negative");

                // no-op
                if (amount == 0)
                    return;

                if (!ptr.getClass().isActor())
                    return;

                MWWorld::InventoryStore* invStorePtr = nullptr;
                if (ptr.getClass().hasInventoryStore(ptr))
                {
                    invStorePtr = &ptr.getClass().getInventoryStore(ptr);
                    // Prefer dropping unequipped items first; re-stack if possible by unequipping items before dropping
                    // them.
                    int numNotEquipped = invStorePtr->count(item);
                    for (int slot = 0; slot < MWWorld::InventoryStore::Slots; ++slot)
                    {
                        MWWorld::ConstContainerStoreIterator it = invStorePtr->getSlot(slot);
                        if (it != invStorePtr->end() && it->getCellRef().getRefId() == item)
                        {
                            numNotEquipped -= it->getCellRef().getCount();
                        }
                    }

                    for (int slot = 0; slot < MWWorld::InventoryStore::Slots && amount > numNotEquipped; ++slot)
                    {
                        MWWorld::ContainerStoreIterator it = invStorePtr->getSlot(slot);
                        if (it != invStorePtr->end() && it->getCellRef().getRefId() == item)
                        {
                            int numToRemove = std::min(amount - numNotEquipped, it->getCellRef().getCount());
                            invStorePtr->unequipItemQuantity(*it, numToRemove);
                            numNotEquipped += numToRemove;
                        }
                    }
                }

                MWWorld::ContainerStore& store = ptr.getClass().getContainerStore(ptr);
                for (MWWorld::ContainerStoreIterator iter(store.begin()); iter != store.end(); ++iter)
                {
                    if (iter->getCellRef().getRefId() == item && (!invStorePtr || !invStorePtr->isEquipped(*iter)))
                    {
                        int removed = store.remove(*iter, amount);
                        MWWorld::Ptr dropped
                            = MWBase::Environment::get().getWorld()->dropObjectOnGround(ptr, *iter, removed);
                        dropped.getCellRef().setOwner(ESM::RefId());

                        amount -= removed;

                        if (amount <= 0)
                            break;
                    }
                }

                MWWorld::ManualRef ref(*MWBase::Environment::get().getESMStore(), item, 1);
                MWWorld::Ptr itemPtr(ref.getPtr());
                if (amount > 0)
                {
                    if (itemPtr.getClass().getScript(itemPtr).empty())
                    {
                        MWBase::Environment::get().getWorld()->dropObjectOnGround(ptr, itemPtr, amount);
                    }
                    else
                    {
                        // Dropping one item per time to prevent making stacks of scripted items
                        for (int i = 0; i < amount; i++)
                            MWBase::Environment::get().getWorld()->dropObjectOnGround(ptr, itemPtr, 1);
                    }
                }

                MWBase::Environment::get().getSoundManager()->playSound3D(
                    ptr, itemPtr.getClass().getDownSoundId(itemPtr), 1.f, 1.f);
            }
        };

        template <class R>
        class OpDropSoulGem : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {

                MWWorld::Ptr ptr = R()(runtime);

                ESM::RefId soul = ESM::RefId::stringRefId(runtime.getStringLiteral(runtime[0].mInteger));
                runtime.pop();

                if (!ptr.getClass().isActor())
                    return;

                MWWorld::ContainerStore& store = ptr.getClass().getContainerStore(ptr);

                for (MWWorld::ContainerStoreIterator iter(store.begin()); iter != store.end(); ++iter)
                {
                    if (iter->getCellRef().getSoul() == soul)
                    {
                        MWBase::Environment::get().getWorld()->dropObjectOnGround(ptr, *iter, 1);
                        store.remove(*iter, 1);
                        break;
                    }
                }
            }
        };

        template <class R>
        class OpGetAttacked : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                MWWorld::Ptr ptr = R()(runtime);

                runtime.push(ptr.getClass().getCreatureStats(ptr).getAttacked());
            }
        };

        template <class R>
        class OpGetWeaponDrawn : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                MWWorld::Ptr ptr = R()(runtime);
                auto& cls = ptr.getClass();
                if (!cls.hasInventoryStore(ptr) && !cls.isBipedal(ptr))
                {
                    runtime.push(0);
                    return;
                }

                if (cls.getCreatureStats(ptr).getDrawState() != MWMechanics::DrawState::Weapon)
                {
                    runtime.push(0);
                    return;
                }

                MWRender::Animation* anim = MWBase::Environment::get().getWorld()->getAnimation(ptr);
                runtime.push(anim && anim->getWeaponsShown());
            }
        };

        template <class R>
        class OpGetSpellReadied : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                MWWorld::Ptr ptr = R()(runtime);

                runtime.push(ptr.getClass().getCreatureStats(ptr).getDrawState() == MWMechanics::DrawState::Spell);
            }
        };


        template <class R>
        class OpListSpells : public Interpreter::Opcode0
        {
        public:

            void execute(Interpreter::Runtime& runtime) override
            {
                MWWorld::Ptr ptr = R()(runtime);
                std::string str = "";
                auto spells = ptr.getClass().getCreatureStats(ptr).getSpells();
                auto wm = MWBase::Environment::get().getWindowManager();
                const MWWorld::ESMStore& store = MWBase::Environment::get().getWorld()->getStore();

                for (auto iter = spells.begin(); iter != spells.end(); ++iter)
                {
                    const ESM::Spell* spell = *iter;
                    str = spell->mName + "   [" + spell->mId.toString() + "]";
                    for (auto effectIt = spell->mEffects.mList.begin(); effectIt != spell->mEffects.mList.end(); ++effectIt)
                    {

                         auto &effect = *effectIt;
                         auto effectIDStr = ESM::MagicEffect::indexToGmstString(effect.mData.mEffectID);
                         const ESM::MagicEffect* magicEffect = store.get<ESM::MagicEffect>().search(effect.mData.mEffectID);

                         std::string range = (effect.mData.mRange == 0 ? "Self" : effect.mData.mRange == 1 ? "Touch" : "Target");
                         std::string magnitude = "";
                         std::string area = "";
                         if (effect.mData.mMagnMin > 0)
                             magnitude += std::to_string(effect.mData.mMagnMin);
                         std::string duration = "";
                         if (effect.mData.mMagnMax > effect.mData.mMagnMin && effect.mData.mMagnMin > 0)
                             magnitude += " to " + std::to_string(effect.mData.mMagnMax);

                         if (effect.mData.mMagnMin > 0 )
                             if (effect.mData.mMagnMin > 1 || effect.mData.mMagnMax > 1)
                                 magnitude += " pts";
                             else
                                 magnitude += " pt";

                         if (effect.mData.mDuration > 0)
                             if (effect.mData.mDuration > 1)
                                duration = "for " + std::to_string(effect.mData.mDuration) + " secs";
                             else
                                duration = "for " + std::to_string(effect.mData.mDuration) + " sec";

                         if (effect.mData.mArea > 0)
                             area = " in " + std::to_string(effect.mData.mArea) + " ft";

                         effectIDStr += " " + magnitude + " " + duration + area + " on " + range;
                         str += "\n ---- " + effectIDStr;


                    }
                    runtime.getContext().report(str + "\n");
                }
            }
        };

        template <class R>
        class OpGetSpellEffects : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                MWWorld::Ptr ptr = R()(runtime);
                ESM::RefId id = ESM::RefId::stringRefId(runtime.getStringLiteral(runtime[0].mInteger));
                runtime.pop();

                if (!ptr.getClass().isActor())
                {
                    runtime.push(0);
                    return;
                }

                const auto& activeSpells = ptr.getClass().getCreatureStats(ptr).getActiveSpells();
                runtime.push(activeSpells.isSpellActive(id) || activeSpells.isEnchantmentActive(id));
            }
        };

        class OpGetCurrentTime : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                runtime.push(MWBase::Environment::get().getWorld()->getTimeStamp().getHour());
            }
        };

        template <class R>
        class OpSetDelete : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                MWWorld::Ptr ptr = R()(runtime);
                int parameter = runtime[0].mInteger;
                runtime.pop();

                if (parameter == 1)
                    MWBase::Environment::get().getWorld()->deleteObject(ptr);
                else if (parameter == 0)
                    MWBase::Environment::get().getWorld()->undeleteObject(ptr);
                else
                    throw std::runtime_error("SetDelete: unexpected parameter");
            }
        };

        class OpGetSquareRoot : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                float param = runtime[0].mFloat;
                runtime.pop();

                if (param < 0)
                    throw std::runtime_error("square root of negative number (we aren't that imaginary)");

                runtime.push(std::sqrt(param));
            }
        };

        template <class R>
        class OpFall : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override {}
        };

        template <class R>
        class OpGetStandingPc : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                MWWorld::Ptr ptr = R()(runtime);
                runtime.push(MWBase::Environment::get().getWorld()->getPlayerStandingOn(ptr));
            }
        };

        template <class R>
        class OpGetStandingActor : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                MWWorld::Ptr ptr = R()(runtime);
                runtime.push(MWBase::Environment::get().getWorld()->getActorStandingOn(ptr));
            }
        };

        template <class R>
        class OpGetCollidingPc : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                MWWorld::Ptr ptr = R()(runtime);
                runtime.push(MWBase::Environment::get().getWorld()->getPlayerCollidingWith(ptr));
            }
        };

        template <class R>
        class OpGetCollidingActor : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                MWWorld::Ptr ptr = R()(runtime);
                runtime.push(MWBase::Environment::get().getWorld()->getActorCollidingWith(ptr));
            }
        };

        template <class R>
        class OpHurtStandingActor : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                MWWorld::Ptr ptr = R()(runtime);
                float healthDiffPerSecond = runtime[0].mFloat;
                runtime.pop();

                MWBase::Environment::get().getWorld()->hurtStandingActors(ptr, healthDiffPerSecond);
            }
        };

        template <class R>
        class OpHurtCollidingActor : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                MWWorld::Ptr ptr = R()(runtime);
                float healthDiffPerSecond = runtime[0].mFloat;
                runtime.pop();

                MWBase::Environment::get().getWorld()->hurtCollidingActors(ptr, healthDiffPerSecond);
            }
        };

        class OpGetWindSpeed : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                runtime.push(MWBase::Environment::get().getWorld()->getWindSpeed());
            }
        };

        template <class R>
        class OpHitOnMe : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                MWWorld::Ptr ptr = R()(runtime);

                ESM::RefId objectID = ESM::RefId::stringRefId(runtime.getStringLiteral(runtime[0].mInteger));
                runtime.pop();

                MWMechanics::CreatureStats& stats = ptr.getClass().getCreatureStats(ptr);
                bool hit = objectID == stats.getLastHitObject();
                runtime.push(hit);
                if (hit)
                    stats.clearLastHitObject();
            }
        };

        template <class R>
        class OpHitAttemptOnMe : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                MWWorld::Ptr ptr = R()(runtime);

                ESM::RefId objectID = ESM::RefId::stringRefId(runtime.getStringLiteral(runtime[0].mInteger));
                runtime.pop();

                MWMechanics::CreatureStats& stats = ptr.getClass().getCreatureStats(ptr);
                bool hit = objectID == stats.getLastHitAttemptObject();
                runtime.push(hit);
                if (hit)
                    stats.clearLastHitAttemptObject();
            }
        };

        template <bool Enable>
        class OpEnableTeleporting : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                MWBase::World* world = MWBase::Environment::get().getWorld();
                world->enableTeleporting(Enable);
            }
        };

        template <bool Enable>
        class OpEnableLevitation : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                MWBase::World* world = MWBase::Environment::get().getWorld();
                world->enableLevitation(Enable);
            }
        };

        template <class R>
        class OpShow : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                MWWorld::Ptr ptr = R()(runtime, false);
                std::string_view var = runtime.getStringLiteral(runtime[0].mInteger);
                runtime.pop();

                std::stringstream output;

                if (!ptr.isEmpty())
                {
                    ESM::RefId script = ESM::RefId::stringRefId(runtime.getStringLiteral(runtime[0].mInteger));
                    if (!script.empty())
                    {
                        const Compiler::Locals& locals
                            = MWBase::Environment::get().getScriptManager()->getLocals(script);
                        char type = locals.getType(var);
                        std::string refId = ptr.getCellRef().getRefId().getRefIdString();
                        if (refId.find(' ') != std::string::npos)
                            refId = '"' + refId + '"';
                        switch (type)
                        {
                            case 'l':
                            case 's':
                                output << refId << "." << var << " = "
                                       << ptr.getRefData().getLocals().getIntVar(script, var);
                                break;
                            case 'f':
                                output << refId << "." << var << " = "
                                       << ptr.getRefData().getLocals().getFloatVar(script, var);
                                break;
                        }
                    }
                }
                if (output.rdbuf()->in_avail() == 0)
                {
                    MWBase::World* world = MWBase::Environment::get().getWorld();
                    char type = world->getGlobalVariableType(var);

                    switch (type)
                    {
                        case 's':
                            output << var << " = " << runtime.getContext().getGlobalShort(var);
                            break;
                        case 'l':
                            output << var << " = " << runtime.getContext().getGlobalLong(var);
                            break;
                        case 'f':
                            output << var << " = " << runtime.getContext().getGlobalFloat(var);
                            break;
                        default:
                            output << "unknown variable";
                    }
                }
                runtime.getContext().report(output.str());
            }
        };

        template <class R>
        class OpShowVars : public Interpreter::Opcode0
        {
            void printLocalVars(Interpreter::Runtime& runtime, const MWWorld::Ptr& ptr)
            {
                std::ostringstream str;

                const ESM::RefId& script = ptr.getClass().getScript(ptr);

                auto printVariables = [&str](const auto& names, const auto& values, std::string_view type) {
                    size_t size = std::min(names.size(), values.size());
                    for (size_t i = 0; i < size; ++i)
                    {
                        str << "\n  " << names[i] << " = " << values[i] << " (" << type << ")";
                    }
                };

                if (script.empty())
                    str << ptr.getCellRef().getRefId() << " does not have a script.";
                else
                {
                    str << "Local variables for " << ptr.getCellRef().getRefId();

                    const Locals& locals = ptr.getRefData().getLocals();
                    const Compiler::Locals& complocals
                        = MWBase::Environment::get().getScriptManager()->getLocals(script);

                    printVariables(complocals.get('s'), locals.mShorts, "short");
                    printVariables(complocals.get('l'), locals.mLongs, "long");
                    printVariables(complocals.get('f'), locals.mFloats, "float");
                }

                runtime.getContext().report(str.str());
            }

            void printGlobalVars(Interpreter::Runtime& runtime)
            {
                std::ostringstream str;
                str << "Global Variables:";

                MWBase::World* world = MWBase::Environment::get().getWorld();
                auto& context = runtime.getContext();
                std::vector<std::string> names = context.getGlobals();

                std::sort(names.begin(), names.end(), ::Misc::StringUtils::ciLess);

                auto printVariable = [&str, &context](const std::string& name, char type) {
                    str << "\n " << name << " = ";
                    switch (type)
                    {
                        case 's':
                            str << context.getGlobalShort(name) << " (short)";
                            break;
                        case 'l':
                            str << context.getGlobalLong(name) << " (long)";
                            break;
                        case 'f':
                            str << context.getGlobalFloat(name) << " (float)";
                            break;
                        default:
                            str << "<unknown type>";
                    }
                };

                for (const auto& name : names)
                    printVariable(name, world->getGlobalVariableType(name));

                context.report(str.str());
            }

            void printGlobalScriptsVars(Interpreter::Runtime& runtime)
            {
                std::ostringstream str;
                str << "\nGlobal Scripts:";

                const auto& scripts = MWBase::Environment::get().getScriptManager()->getGlobalScripts().getScripts();

                // sort for user convenience
                std::map<ESM::RefId, std::shared_ptr<GlobalScriptDesc>> globalScripts(scripts.begin(), scripts.end());

                auto printVariables
                    = [&str](std::string_view scptName, const auto& names, const auto& values, std::string_view type) {
                          size_t size = std::min(names.size(), values.size());
                          for (size_t i = 0; i < size; ++i)
                          {
                              str << "\n " << scptName << "->" << names[i] << " = " << values[i] << " (" << type << ")";
                          }
                      };

                for (const auto& [refId, script] : globalScripts)
                {
                    // Skip dormant global scripts
                    if (!script->mRunning)
                        continue;

                    std::string_view scptName = refId.getRefIdString();

                    const Compiler::Locals& complocals
                        = MWBase::Environment::get().getScriptManager()->getLocals(refId);
                    const Locals& locals
                        = MWBase::Environment::get().getScriptManager()->getGlobalScripts().getLocals(refId);

                    if (locals.isEmpty())
                        str << "\n No variables in script " << scptName;
                    else
                    {
                        printVariables(scptName, complocals.get('s'), locals.mShorts, "short");
                        printVariables(scptName, complocals.get('l'), locals.mLongs, "long");
                        printVariables(scptName, complocals.get('f'), locals.mFloats, "float");
                    }
                }

                runtime.getContext().report(str.str());
            }

        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                MWWorld::Ptr ptr = R()(runtime, false);
                if (!ptr.isEmpty())
                    printLocalVars(runtime, ptr);
                else
                {
                    // No reference, no problem.
                    printGlobalVars(runtime);
                    printGlobalScriptsVars(runtime);
                }
            }
        };

        class OpToggleScripts : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                bool enabled = MWBase::Environment::get().getWorld()->toggleScripts();

                runtime.getContext().report(enabled ? "Scripts -> On" : "Scripts -> Off");
            }
        };

        class OpToggleGodMode : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                bool enabled = MWBase::Environment::get().getWorld()->toggleGodMode();

                runtime.getContext().report(enabled ? "God Mode -> On" : "God Mode -> Off");
            }
        };

        class OpGetFollowers : public Interpreter::Opcode0
        {
        public:

            void execute(Interpreter::Runtime& runtime) override
            {
                std::set<MWWorld::Ptr> followers;
                MWBase::Environment::get().getMechanicsManager()->getActorsFollowing(MWMechanics::getPlayer(), followers);
                std::string str = "";
                for (auto& f : followers)
                {
                    str += "\n" + f.getCellRef().getRefId().toString();
                }
                runtime.getContext().report(str);
            }
        };


#include <iostream>
#include <fstream>
#include <set>
#include <string>
#include <sstream>

class OpSetFollowers : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                std::string fileStr = "Saved";


                Files::ConfigurationManager cfgMgr;
                std::filesystem::path logPath = cfgMgr.getLogPath();
                std::filesystem::path gdsOpenMW = logPath / "gdsOpenMW.xml";


                if (std::filesystem::exists("gdsOpenMW.xml"))
                {
                    fileStr = "Replaced";
                    std::filesystem::remove("gdsOpenMW.xml");
                }

                std::set<MWWorld::Ptr> followers;
                MWBase::Environment::get().getMechanicsManager()->getActorsFollowing(
                    MWMechanics::getPlayer(), followers);

                std::ostringstream xmlStream;
                xmlStream << "<?xml version=\"1.0\" encoding=\"utf-8\"?>" << std::endl;
                xmlStream << "<Followers>" << std::endl;

                size_t n = 1;
                for (const auto& f : followers)
                {
                    xmlStream << "  <F" << n << ">" << f.getCellRef().getRefId() << "</F" << n << ">" << std::endl;
                    n++;
                }


                xmlStream << "</Followers>" << std::endl;

                std::ofstream outputFile("gdsOpenMW.xml");
                if (outputFile.is_open())
                {
                    outputFile << xmlStream.str();
                    outputFile.close();
                }


                runtime.getContext().report(fileStr);
            }
        };

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

        class OpRecoverFollowers : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                std::string fileStr = "gdsOpenMW.xml";

                Files::ConfigurationManager cfgMgr;
                std::filesystem::path logPath = cfgMgr.getLogPath();
                std::filesystem::path gdsOpenMW = logPath / "gdsOpenMW.xml";
                if (!std::filesystem::exists(gdsOpenMW))

                {
                    std::string str = fileStr + " file not found; no recovery made. " + fileStr
                        + " created by calling SaveFollowers (sf)";
                    Log(Debug::Info) << str;
                    runtime.getContext().report(str);

                    return;
                }

                std::map<std::string, std::string> data;


                std::ifstream inputFile(fileStr);
                if (inputFile.is_open())
                {
                    std::ostringstream fileContents;
                    fileContents << inputFile.rdbuf();
                    inputFile.close();

                    // Parse the XML content using basic C++ Standard Library features
                    std::string xmlContent = fileContents.str();
                    std::istringstream xmlStream(xmlContent);
                    std::string line;
                    while (std::getline(xmlStream, line))
                    {
                        // Process each line of XML as needed
                        // You may need to extract data from XML elements and populate the 'data' map here
                        // For simplicity, we assume the XML structure is well-formed and processable
                    }
                }

                std::string str = "";
                for (const auto& entry : data)
                {
                    auto refid = ESM::RefId::stringRefId(entry.second);
                    auto ptr = MWBase::Environment::get().getWorld()->searchPtr(refid, false, true);

                    if (ptr.isEmpty())
                    {
                        str = refid.toString() + " not found, skipping recovery for actor.";
                        Log(Debug::Info) << str;
                        runtime.getContext().report(str);
                    }
                    else
                    {
                        //move to player
                        MWWorld::CellStore* store = nullptr;
                        try
                        {
                            store = &MWBase::Environment::get().getWorldModel()->getInterior(MWMechanics::getPlayer().getCell()->getCell()->getDisplayName());
                        }
                        catch (std::exception&)
                        {
                            auto pos = MWMechanics::getPlayer().getCellRef().getPosition().asVec3();

                            store = &MWBase::Environment::get().getWorldModel()->getExterior(
                                ESM::ExteriorCellLocation(pos[0], pos[1],ESM::Cell::sDefaultWorldspaceId ));
                            if (pos.isNaN())
                            {
                                std::string error = "Warning: PositionCell: unknown interior cell (" + 
                                    std::string(MWMechanics::getPlayer().getCell()->getCell()->getDisplayName()) + "), moving to exterior instead";
                                runtime.getContext().report(error);
                                Log(Debug::Warning) << error;
                            }
                        }

                        MWWorld::Ptr companion = ptr;
                        auto player = MWMechanics::getPlayer();
                        auto position = player.getRefData().getPosition().asVec3();
                        MWWorld::Ptr baseCompanion = companion;


                        position[0] += (::Misc::Rng::rollDice(81) - 40 + 1);
                        position[1] += (::Misc::Rng::rollDice(81) - 40 + 1);

                        str = "recover " + ptr.getCellRef().getRefId().toString() + " to " + 
                            std::string(MWMechanics::getPlayer().getCell()->getCell()->getDisplayName()) + ":" +
                            std::to_string(position[0]) + ":" + std::to_string(position[1]) + ":" + std::to_string(position[2]);
                        Log(Debug::Info) << str;
                        runtime.getContext().report("report: " + str);


                        companion = MWBase::Environment::get().getWorld()->moveObject(companion, MWMechanics::getPlayer().getCell(), osg::Vec3f(position));
                        dynamic_cast<MWScript::InterpreterContext&>(runtime.getContext()).updatePtr(baseCompanion, companion);

                        auto rot = companion.getRefData().getPosition().asRotationVec3();
                        rot.z() = osg::DegreesToRadians(0.0f);
                        MWBase::Environment::get().getWorld()->rotateObject(companion, rot);

                        companion.getClass().adjustPosition(companion, false);

                        //set wander / follow AI packages -- wander packages must come first so that follow is the latest and most current package
                        MWMechanics::AiWander wanderPackage(120, 60, 40, std::vector<unsigned char>{30, 20, 0, 0, 0, 0, 0, 0, 0}, false);
                        companion.getClass().getCreatureStats(companion).getAiSequence().stack(wanderPackage, companion);

 //                       MWMechanics::AiFollow followPackage(MWMechanics::getPlayer().getCellRef().getRefId(), 0, 0, 0, 0, true);

//                        companion.getClass().getCreatureStats(companion).getAiSequence().stack(followPackage, companion);

                        //set disposition and health
                        if (companion.getClass().isNpc())
                        {
                            companion.getClass().getNpcStats(companion).setBaseDisposition(100);

                            //get player health
                            float playerHealthValue;
                            if (player.getClass().hasItemHealth(player))
                                playerHealthValue = static_cast<Interpreter::Type_Float>(player.getClass().getItemMaxHealth(player));
                            else
                                playerHealthValue = player.getClass().getCreatureStats(player).getDynamic(0).getCurrent();

                            float oldValue;

                            if (companion.getClass().hasItemHealth(companion))
                                oldValue = static_cast<Interpreter::Type_Float>(companion.getClass().getItemMaxHealth(companion));
                            else
                                oldValue = companion.getClass().getCreatureStats(companion).getDynamic(0).getCurrent();

                            if (oldValue < playerHealthValue * 3)
                            {
                                auto newValue = oldValue * 10;

                                MWMechanics::DynamicStat<float> stat(companion.getClass().getCreatureStats(companion)
                                    .getDynamic(0));

                                stat.setModifier(newValue);
                                stat.setCurrent(newValue);

                                companion.getClass().getCreatureStats(companion).setDynamic(0, stat);
                            }
                        }
                    }
                }
                runtime.getContext().report(str);
            }
        };




        template <class R>
        class OpCast : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                MWWorld::Ptr ptr = R()(runtime);

                ESM::RefId spellId = ESM::RefId::stringRefId(runtime.getStringLiteral(runtime[0].mInteger));
                runtime.pop();

                ESM::RefId targetId = ESM::RefId::stringRefId(runtime.getStringLiteral(runtime[0].mInteger));
                runtime.pop();

                const ESM::Spell* spell = MWBase::Environment::get().getESMStore()->get<ESM::Spell>().search(spellId);
                if (!spell)
                {
                    runtime.getContext().report(
                        "spellcasting failed: cannot find spell \"" + spellId.getRefIdString() + "\"");
                    return;
                }

                if (ptr == MWMechanics::getPlayer())
                {
                    MWBase::Environment::get().getWorld()->getPlayer().setSelectedSpell(spell->mId);
                    return;
                }

                if (ptr.getClass().isActor())
                {
                    if (!MWBase::Environment::get().getMechanicsManager()->isCastingSpell(ptr))
                    {
                        MWMechanics::AiCast castPackage(targetId, spell->mId, true);
                        ptr.getClass().getCreatureStats(ptr).getAiSequence().stack(castPackage, ptr);
                    }
                    return;
                }

                MWWorld::Ptr target = MWBase::Environment::get().getWorld()->searchPtr(targetId, false, false);
                if (target.isEmpty())
                    return;

                MWMechanics::CastSpell cast(ptr, target, false, true);
                cast.playSpellCastingEffects(spell);
                cast.mHitPosition = target.getRefData().getPosition().asVec3();
                cast.mAlwaysSucceed = true;
                cast.cast(spell);
            }
        };

        template <class R>
        class OpExplodeSpell : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                MWWorld::Ptr ptr = R()(runtime);

                ESM::RefId spellId = ESM::RefId::stringRefId(runtime.getStringLiteral(runtime[0].mInteger));
                runtime.pop();

                const ESM::Spell* spell = MWBase::Environment::get().getESMStore()->get<ESM::Spell>().search(spellId);
                if (!spell)
                {
                    runtime.getContext().report(
                        "spellcasting failed: cannot find spell \"" + spellId.getRefIdString() + "\"");
                    return;
                }

                if (ptr == MWMechanics::getPlayer())
                {
                    MWBase::Environment::get().getWorld()->getPlayer().setSelectedSpell(spell->mId);
                    return;
                }

                if (ptr.getClass().isActor())
                {
                    if (!MWBase::Environment::get().getMechanicsManager()->isCastingSpell(ptr))
                    {
                        MWMechanics::AiCast castPackage(ptr.getCellRef().getRefId(), spell->mId, true);
                        ptr.getClass().getCreatureStats(ptr).getAiSequence().stack(castPackage, ptr);
                    }
                    return;
                }

                MWMechanics::CastSpell cast(ptr, ptr, false, true);
                cast.mHitPosition = ptr.getRefData().getPosition().asVec3();
                cast.mAlwaysSucceed = true;
                cast.cast(spell);
            }
        };

        class OpGoToJail : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                MWBase::World* world = MWBase::Environment::get().getWorld();
                world->goToJail();
            }
        };

        class OpPayFine : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                MWWorld::Ptr player = MWMechanics::getPlayer();
                player.getClass().getNpcStats(player).setBounty(0);
                MWBase::World* world = MWBase::Environment::get().getWorld();
                world->confiscateStolenItems(player);
                world->getPlayer().recordCrimeId();
                world->getPlayer().setDrawState(MWMechanics::DrawState::Nothing);
            }
        };

        class OpPayFineThief : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                MWWorld::Ptr player = MWMechanics::getPlayer();
                player.getClass().getNpcStats(player).setBounty(0);
                MWBase::Environment::get().getWorld()->getPlayer().recordCrimeId();
            }
        };

        class OpGetPcInJail : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                runtime.push(MWBase::Environment::get().getWorld()->isPlayerInJail());
            }
        };

        class OpGetPcTraveling : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                runtime.push(MWBase::Environment::get().getWorld()->isPlayerTraveling());
            }
        };

        template <class R>
        class OpBetaComment : public Interpreter::Opcode1
        {
        public:

            std::string processInventory(MWWorld::ConstPtr ptr, int slot )
            {
                std::stringstream msg;

                const std::vector<std::string> slotNames{ "Helmet","Cuirass","Greaves","LeftPauldron","RightPauldron","LeftGauntlet","RightGauntlet", "Boots", "Shirt",
            "Pants", "Skirt","Robe","LeftRing","RightRing", "Amulet","Belt","CarriedRight", "CarriedLeft", "Ammunition" };

                if (ptr.isEmpty())
                    return "";

                if (slot < 0 || slot >> 18)
                    return "";

                msg << "Slot: " << std::to_string(slot) << " : " << slotNames[slot] << std::endl;
                msg << "RefNum: " << ptr.getCellRef().getRefNum().mIndex << std::endl;

                if (ptr.getRefData().isDeletedByContentFile())
                    msg << "[Deleted by content file]" << std::endl;
                auto &refD = ptr.getRefData();
                if (!refD.getBaseNode())
                    msg << "[Deleted]" << std::endl;

                msg << "Name: " << ptr.getClass().getName(ptr) << std::endl;
                msg << "Class: " << ptr.getClass().getSearchTags(ptr) << std::endl;
                msg << "RefID: " << ptr.getCellRef().getRefId() << std::endl;
                msg << "Ref Type: " << ptr.getType() << std::endl;
                msg << "Memory address: " << ptr.getBase() << std::endl;

                if (ptr.getType() == ESM::Weapon::sRecordId)
                {
                    const MWWorld::LiveCellRef<ESM::Weapon>* ref = ptr.get<ESM::Weapon>();
                    msg << "Enchant: " << ref->mBase->mEnchant << std::endl;
                }

                if (ptr.getType() == ESM::Armor::sRecordId)
                {
                    const MWWorld::LiveCellRef<ESM::Armor>* ref = ptr.get<ESM::Armor>();
                    msg << "Enchant: " << ref->mBase->mEnchant << std::endl;
                }

                if (ptr.getType() == ESM::Clothing::sRecordId)
                {
                    const auto* ref = ptr.get<ESM::Clothing>();
                    msg << "Enchant: " << ref->mBase->mEnchant << std::endl;
                }

                auto vfs = MWBase::Environment::get().getResourceSystem()->getVFS();
                std::string model = ::Misc::ResourceHelpers::correctActorModelPath(std::string(ptr.getClass().getModel(ptr)), vfs);
                msg << "Model: " << model << std::endl;
                if (!model.empty())
                {
                    const std::string archive = vfs->getArchive(model);
                    if (!archive.empty())
                        msg << "(" << archive << ")" << std::endl;
                }
                if (::Misc::ResourceHelpers::correctIconPath(ptr.getClass().getInventoryIcon(ptr), vfs) != "icons\\")
                {
                    std::string icon = ::Misc::ResourceHelpers::correctIconPath(ptr.getClass().getInventoryIcon(ptr), vfs);
                    msg << "Icon: " << icon << std::endl;
                    if (!icon.empty())
                    {
                        const std::string archive = vfs->getArchive(icon);
                        if (!archive.empty())
                            msg << "(" << archive << ")" << std::endl;
                    }
                }
                if (!ptr.getClass().getScript(ptr).empty())
                    msg << "Script: " << ptr.getClass().getScript(ptr) << std::endl;

                return msg.str();
            };

            void execute(Interpreter::Runtime &runtime, unsigned int arg0) override

            {
                MWWorld::Ptr ptr = R()(runtime);

                std::stringstream msg;

                msg << "Report time: ";

                std::time_t currentTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                tm timeinfo{};
#ifdef _WIN32
                gmtime_s(&timeinfo, &currentTime);
#else
                gmtime_r(&currentTime, &timeinfo);
#endif
                msg << std::put_time(&timeinfo, "%Y.%m.%d %T UTC") << std::endl;

                msg << "Content file: " << ptr.getCellRef().getRefNum().mContentFile;

                if (!ptr.getCellRef().hasContentFile())
                    msg << " [None]" << std::endl;
                else
                {
                    std::vector<std::string> contentFiles = MWBase::Environment::get().getWorld()->getContentFiles();

                    msg << " [" << contentFiles.at(ptr.getCellRef().getRefNum().mContentFile) << "]" << std::endl;
                }

                msg << "RefNum: " << ptr.getCellRef().getRefNum().mIndex << std::endl;

                if (ptr.getRefData().isDeletedByContentFile())
                    msg << "[Deleted by content file]" << std::endl;
                if (!ptr.getCellRef().getCount())
                    msg << "[Deleted]" << std::endl;

                msg << "Name: " << ptr.getClass().getName(ptr) << std::endl;
                msg << "Class: " << ptr.getClass().getSearchTags(ptr) << std::endl;
                msg << "RefID: " << ptr.getCellRef().getRefId() << std::endl;
                msg << "Ref Type: " << ptr.getTypeDescription() << std::endl;
                msg << "Memory address: " << ptr.getBase() << std::endl;

                if (ptr.isInCell())
                {
                    MWWorld::CellStore* cell = ptr.getCell();
                    msg << "Cell: " << MWBase::Environment::get().getWorld()->getCellName(cell) << std::endl;
                    if (cell->getCell()->isExterior())
                        msg << "Grid: " << cell->getCell()->getGridX() << " " << cell->getCell()->getGridY()
                            << std::endl;
                    osg::Vec3f pos(ptr.getRefData().getPosition().asVec3());
                    msg << "Coordinates: " << pos.x() << " " << pos.y() << " " << pos.z() << std::endl;

                    if (ptr.getType() == ESM::Weapon::sRecordId)
                    {
                        const MWWorld::LiveCellRef<ESM::Weapon>* ref = ptr.get<ESM::Weapon>();
                        msg << "Enchant: " << ref->mBase->mEnchant << std::endl;
                    }

                    if (ptr.getType() == ESM::Armor::sRecordId)
                    {
                        const MWWorld::LiveCellRef<ESM::Armor>* ref = ptr.get<ESM::Armor>();
                        msg << "Enchant: " << ref->mBase->mEnchant << std::endl;
                    }

                    if (ptr.getType() == ESM::Clothing::sRecordId)
                    {
                        const auto* ref = ptr.get<ESM::Clothing>();
                        msg << "Enchant: " << ref->mBase->mEnchant << std::endl;
                    }

                    auto vfs = MWBase::Environment::get().getResourceSystem()->getVFS();
                    const VFS::Path::Normalized model
                        = ::Misc::ResourceHelpers::correctActorModelPath(ptr.getClass().getCorrectedModel(ptr), vfs);
                    msg << "Model: " << model.value() << std::endl;
                    if (!model.empty())
                    {
                        const std::string archive = vfs->getArchive(model);
                        if (!archive.empty())
                            msg << "(" << archive << ")" << std::endl;
                        TextureFetchVisitor visitor;
                        SceneUtil::PositionAttitudeTransform* baseNode = ptr.getRefData().getBaseNode();
                        if (baseNode)
                            baseNode->accept(visitor);
                        // The instance might not have a physical model due to paging or scripting.
                        // If this is the case, fall back to the template
                        if (visitor.mTextures.empty())
                        {
                            Resource::SceneManager* sceneManager
                                = MWBase::Environment::get().getResourceSystem()->getSceneManager();
                            const_cast<osg::Node*>(sceneManager->getTemplate(model).get())->accept(visitor);
                            msg << "Bound textures: [None]" << std::endl;
                            if (!visitor.mTextures.empty())
                                msg << "Model textures: ";
                        }
                        else
                        {
                            msg << "Bound textures: ";
                        }
                        if (!visitor.mTextures.empty())
                        {
                            msg << std::endl;
                            std::string lastTextureSrc;
                            for (auto& [textureName, fileName] : visitor.mTextures)
                            {
                                std::string textureSrc;
                                if (!fileName.empty())
                                    textureSrc = vfs->getArchive(fileName);

                                if (lastTextureSrc.empty() || textureSrc != lastTextureSrc)
                                {
                                    lastTextureSrc = std::move(textureSrc);
                                    if (lastTextureSrc.empty())
                                        lastTextureSrc = "[No Source]";

                                    msg << "  " << lastTextureSrc << std::endl;
                                }
                                msg << "    ";
                                msg << (textureName.empty() ? "[Anonymous]: " : textureName) << ": ";
                                msg << (fileName.empty() ? "[No File]" : fileName) << std::endl;
                            }
                        }
                        else
                        {
                            msg << "[None]" << std::endl;
                        }
                    }
                    try
                    {
                        if (::Misc::ResourceHelpers::correctIconPath(
                                VFS::Path::toNormalized(ptr.getClass().getInventoryIcon(ptr)), *vfs)
                            != "icons\\")
                        {
                            std::string icon = ::Misc::ResourceHelpers::correctIconPath(
                                VFS::Path::toNormalized(ptr.getClass().getInventoryIcon(ptr)), *vfs);
                            msg << "Icon: " << icon << std::endl;
                            if (!icon.empty())
                            {
                                const std::string archive = vfs->getArchive(icon);
                                if (!archive.empty())
                                    msg << "(" << archive << ")" << std::endl;
                            }
                        }
                        if (!ptr.getClass().getScript(ptr).empty())
                            msg << "Script: " << ptr.getClass().getScript(ptr) << std::endl;
                    }
                    catch (const std::exception& e)
                    {
                        
                    }
                }


                while (arg0 > 0)
                {
                    std::string_view notes = runtime.getStringLiteral(runtime[0].mInteger);
                    runtime.pop();
                    if (!notes.empty())
                        msg << "Notes: " << notes << std::endl;
                    --arg0;
                }

                if (ptr.getClass().isNpc())
                {
                    msg << std::endl << " Report on Inventory for " << ptr.getClass().getName(ptr) << std::endl << std::endl;
                    const MWWorld::InventoryStore& invStore = ptr.getClass().getInventoryStore(ptr);
                    for (int slot = 0; slot < MWWorld::InventoryStore::Slots; ++slot)
                    {
                        MWWorld::ConstContainerStoreIterator equipped = invStore.getSlot(slot);
                        if (equipped != invStore.end())
                        {
                            std::string v = "";
                            v = processInventory((*equipped), slot);
                            msg << v << std::endl;
                        }
                    }
                }
                Log(Debug::Warning) << "\n" << msg.str();
                runtime.getContext().report(msg.str());
            }
        };

        class OpAddToLevCreature : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                const ESM::RefId& levId = ESM::RefId::stringRefId(runtime.getStringLiteral(runtime[0].mInteger));
                runtime.pop();
                const ESM::RefId& creatureId = ESM::RefId::stringRefId(runtime.getStringLiteral(runtime[0].mInteger));
                runtime.pop();
                int level = runtime[0].mInteger;
                runtime.pop();

                ESM::CreatureLevList listCopy
                    = *MWBase::Environment::get().getESMStore()->get<ESM::CreatureLevList>().find(levId);
                addToLevList(&listCopy, creatureId, static_cast<uint16_t>(level));
                MWBase::Environment::get().getESMStore()->overrideRecord(listCopy);
            }
        };

        class OpGetGlobal : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                const std::string& global = std::string(runtime.getStringLiteral(runtime[0].mInteger));
                runtime.pop();
                
                MWBase::World* world = MWBase::Environment::get().getWorld();

                std::string str = "";

                char type = world->getGlobalVariableType(global);
                str = "\n" + global + " = ";

                switch (type)
                {
                case 's':

                    str += std::to_string(runtime.getContext().getGlobalShort(global)) + " (short)";
                    break;

                case 'l':

                    str += std::to_string(runtime.getContext().getGlobalLong(global)) + " (long)";
                    break;

                case 'f':

                    str += std::to_string(runtime.getContext().getGlobalFloat(global)) + " (float)";
                    break;

                default:

                    str += "<unknown type>";
                }

                runtime.getContext().report(str);
            }
        };

        class OpRemoveFromLevCreature : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                const ESM::RefId& levId = ESM::RefId::stringRefId(runtime.getStringLiteral(runtime[0].mInteger));
                runtime.pop();
                const ESM::RefId& creatureId = ESM::RefId::stringRefId(runtime.getStringLiteral(runtime[0].mInteger));
                runtime.pop();
                int level = runtime[0].mInteger;
                runtime.pop();

                ESM::CreatureLevList listCopy
                    = *MWBase::Environment::get().getESMStore()->get<ESM::CreatureLevList>().find(levId);
                removeFromLevList(&listCopy, creatureId, level);
                MWBase::Environment::get().getESMStore()->overrideRecord(listCopy);
            }
        };

        class OpAddToLevItem : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                const ESM::RefId& levId = ESM::RefId::stringRefId(runtime.getStringLiteral(runtime[0].mInteger));
                runtime.pop();
                const ESM::RefId& itemId = ESM::RefId::stringRefId(runtime.getStringLiteral(runtime[0].mInteger));
                runtime.pop();
                int level = runtime[0].mInteger;
                runtime.pop();

                ESM::ItemLevList listCopy
                    = *MWBase::Environment::get().getESMStore()->get<ESM::ItemLevList>().find(levId);
                addToLevList(&listCopy, itemId, static_cast<uint16_t>(level));
                MWBase::Environment::get().getESMStore()->overrideRecord(listCopy);
            }
        };

        class OpRemoveFromLevItem : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                const ESM::RefId& levId = ESM::RefId::stringRefId(runtime.getStringLiteral(runtime[0].mInteger));
                runtime.pop();
                const ESM::RefId& itemId = ESM::RefId::stringRefId(runtime.getStringLiteral(runtime[0].mInteger));
                runtime.pop();
                int level = runtime[0].mInteger;
                runtime.pop();

                ESM::ItemLevList listCopy
                    = *MWBase::Environment::get().getESMStore()->get<ESM::ItemLevList>().find(levId);
                removeFromLevList(&listCopy, itemId, level);
                MWBase::Environment::get().getESMStore()->overrideRecord(listCopy);
            }
        };

        template <class R>
        class OpShowSceneGraph : public Interpreter::Opcode1
        {
        public:
            void execute(Interpreter::Runtime& runtime, unsigned int arg0) override
            {
                MWWorld::Ptr ptr = R()(runtime, false);

                int confirmed = 0;
                if (arg0 == 1)
                {
                    confirmed = runtime[0].mInteger;
                    runtime.pop();
                }

                if (ptr.isEmpty() && !confirmed)
                    runtime.getContext().report(
                        "Exporting the entire scene graph will result in a large file. Confirm this action using "
                        "'showscenegraph 1' or select an object instead.");
                else
                {
                    const auto filename = MWBase::Environment::get().getWorld()->exportSceneGraph(ptr);
                    runtime.getContext().report("Wrote '" + Files::pathToUnicodeString(filename) + "'");
                }
            }
        };

        class OpToggleNavMesh : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                bool enabled = MWBase::Environment::get().getWorld()->toggleRenderMode(MWRender::Render_NavMesh);

                runtime.getContext().report(
                    enabled ? "Navigation Mesh Rendering -> On" : "Navigation Mesh Rendering -> Off");
            }
        };

        class OpReportActiveQuests : public Interpreter::Opcode0
        {
            public:

                void execute (Interpreter::Runtime& runtime) override
                {
                    MWBase::Journal* journal = MWBase::Environment::get().getJournal(); 
                    std::string str = "";

                    for (MWBase::Journal::TQuestIter i = journal->questBegin(); i != journal->questEnd(); ++i)
                        if (!(i->second.isFinished()))
                            str += " Name: " + std::string(i->second.getName()) + "\n  ID : " + i->second.getTopic().toString() +
                               "\n  Index : " + std::to_string(i->second.getIndex()) + "\n";
                    runtime.getContext().report (str);
                }
        };

        class OpToggleActorsPaths : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                bool enabled = MWBase::Environment::get().getWorld()->toggleRenderMode(MWRender::Render_ActorsPaths);

                runtime.getContext().report(enabled ? "Agents Paths Rendering -> On" : "Agents Paths Rendering -> Off");
            }
        };

        class OpSetNavMeshNumberToRender : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                const auto navMeshNumber = runtime[0].mInteger;
                runtime.pop();

                if (navMeshNumber < 0)
                {
                    runtime.getContext().report("Invalid navmesh number: use not less than zero values");
                    return;
                }

                MWBase::Environment::get().getWorld()->setNavMeshNumberToRender(
                    static_cast<std::size_t>(navMeshNumber));            }
        };

        template <class R>
        class OpRepairedOnMe : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                // Broken in vanilla and deliberately no-op.
                runtime.push(0);
            }
        };

        class OpToggleRecastMesh : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                bool enabled = MWBase::Environment::get().getWorld()->toggleRenderMode(MWRender::Render_RecastMesh);

                runtime.getContext().report(enabled ? "Recast Mesh Rendering -> On" : "Recast Mesh Rendering -> Off");
            }
        };

        class OpHelp : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                std::stringstream message;
                message << MWBase::Environment::get().getWindowManager()->getVersionDescription() << "\n\n";
                std::vector<std::string> commands;
                MWBase::Environment::get().getScriptManager()->getExtensions().listKeywords(commands);
                for (const auto& command : commands)
                    message << command << "\n";
                runtime.getContext().report(message.str());
            }
        };

        class OpReloadLua : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                MWBase::Environment::get().getLuaManager()->reloadAllScripts();
                runtime.getContext().report("All Lua scripts are reloaded");
            }
        };

        class OpTestModels : public Interpreter::Opcode0
        {
            template <class T>
            void test(int& count) const
            {
                Resource::SceneManager* sceneManager
                    = MWBase::Environment::get().getResourceSystem()->getSceneManager();
                const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();
                for (const T& record : store.get<T>())
                {
                    MWWorld::ManualRef ref(store, record.mId);
                    VFS::Path::Normalized model(ref.getPtr().getClass().getCorrectedModel(ref.getPtr()));
                    if (!model.empty())
                    {
                        sceneManager->getTemplate(model);
                        ++count;
                    }
                }
            }

        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                Resource::SceneManager* sceneManager
                    = MWBase::Environment::get().getResourceSystem()->getSceneManager();
                double delay = sceneManager->getExpiryDelay();
                sceneManager->setExpiryDelay(0.0);
                int count = 0;

                test<ESM::Activator>(count);
                test<ESM::Apparatus>(count);
                test<ESM::Armor>(count);
                test<ESM::Potion>(count);
                test<ESM::BodyPart>(count);
                test<ESM::Book>(count);
                test<ESM::Clothing>(count);
                test<ESM::Container>(count);
                test<ESM::Creature>(count);
                test<ESM::Door>(count);
                test<ESM::Ingredient>(count);
                test<ESM::Light>(count);
                test<ESM::Lockpick>(count);
                test<ESM::Miscellaneous>(count);
                test<ESM::Probe>(count);
                test<ESM::Repair>(count);
                test<ESM::Static>(count);
                test<ESM::Weapon>(count);

                sceneManager->setExpiryDelay(delay);
                std::stringstream message;
                message << "Attempted to load models for " << count << " objects. Check the log for details.";
                runtime.getContext().report(message.str());
            }
        };

        void installOpcodes(Interpreter::Interpreter& interpreter)
        {
            interpreter.installSegment5<OpMenuMode>(Compiler::Misc::opcodeMenuMode);
            interpreter.installSegment5<OpRandom>(Compiler::Misc::opcodeRandom);
            interpreter.installSegment5<OpScriptRunning>(Compiler::Misc::opcodeScriptRunning);
            interpreter.installSegment5<OpStartScript<ImplicitRef>>(Compiler::Misc::opcodeStartScript);
            interpreter.installSegment5<OpStartScript<ExplicitRef>>(Compiler::Misc::opcodeStartScriptExplicit);
            interpreter.installSegment5<OpStopScript>(Compiler::Misc::opcodeStopScript);
            interpreter.installSegment5<OpGetSecondsPassed>(Compiler::Misc::opcodeGetSecondsPassed);
            interpreter.installSegment5<OpEnable<ImplicitRef>>(Compiler::Misc::opcodeEnable);
            interpreter.installSegment5<OpEnable<ExplicitRef>>(Compiler::Misc::opcodeEnableExplicit);
            interpreter.installSegment5<OpDisable<ImplicitRef>>(Compiler::Misc::opcodeDisable);
            interpreter.installSegment5<OpDisable<ExplicitRef>>(Compiler::Misc::opcodeDisableExplicit);
            interpreter.installSegment5<OpGetDisabled<ImplicitRef>>(Compiler::Misc::opcodeGetDisabled);
            interpreter.installSegment5<OpGetDisabled<ExplicitRef>>(Compiler::Misc::opcodeGetDisabledExplicit);
            interpreter.installSegment5<OpXBox>(Compiler::Misc::opcodeXBox);
            interpreter.installSegment5<OpOnActivate<ImplicitRef>>(Compiler::Misc::opcodeOnActivate);
            interpreter.installSegment5<OpOnActivate<ExplicitRef>>(Compiler::Misc::opcodeOnActivateExplicit);
            interpreter.installSegment5<OpActivate<ImplicitRef>>(Compiler::Misc::opcodeActivate);
            interpreter.installSegment5<OpActivate<ExplicitRef>>(Compiler::Misc::opcodeActivateExplicit);
            interpreter.installSegment3<OpLock<ImplicitRef>>(Compiler::Misc::opcodeLock);
            interpreter.installSegment3<OpLock<ExplicitRef>>(Compiler::Misc::opcodeLockExplicit);
            interpreter.installSegment5<OpUnlock<ImplicitRef>>(Compiler::Misc::opcodeUnlock);
            interpreter.installSegment5<OpUnlock<ExplicitRef>>(Compiler::Misc::opcodeUnlockExplicit);
            interpreter.installSegment5<OpToggleCollisionDebug>(Compiler::Misc::opcodeToggleCollisionDebug);
            interpreter.installSegment5<OpToggleCollisionBoxes>(Compiler::Misc::opcodeToggleCollisionBoxes);
            interpreter.installSegment5<OpToggleWireframe>(Compiler::Misc::opcodeToggleWireframe);
            interpreter.installSegment5<OpFadeIn>(Compiler::Misc::opcodeFadeIn);
            interpreter.installSegment5<OpFadeOut>(Compiler::Misc::opcodeFadeOut);
            interpreter.installSegment5<OpFadeTo>(Compiler::Misc::opcodeFadeTo);
            interpreter.installSegment5<OpTogglePathgrid>(Compiler::Misc::opcodeTogglePathgrid);
            interpreter.installSegment5<OpToggleWater>(Compiler::Misc::opcodeToggleWater);
            interpreter.installSegment5<OpToggleWorld>(Compiler::Misc::opcodeToggleWorld);
            interpreter.installSegment5<OpDontSaveObject>(Compiler::Misc::opcodeDontSaveObject);
            interpreter.installSegment5<OpPcForce1stPerson>(Compiler::Misc::opcodePcForce1stPerson);
            interpreter.installSegment5<OpPcForce3rdPerson>(Compiler::Misc::opcodePcForce3rdPerson);
            interpreter.installSegment5<OpPcGet3rdPerson>(Compiler::Misc::opcodePcGet3rdPerson);
            interpreter.installSegment5<OpToggleVanityMode>(Compiler::Misc::opcodeToggleVanityMode);
            interpreter.installSegment5<OpGetPcSleep>(Compiler::Misc::opcodeGetPcSleep);
            interpreter.installSegment5<OpGetPcJumping>(Compiler::Misc::opcodeGetPcJumping);
            interpreter.installSegment5<OpWakeUpPc>(Compiler::Misc::opcodeWakeUpPc);
            interpreter.installSegment5<OpPlayBink>(Compiler::Misc::opcodePlayBink);
            interpreter.installSegment5<OpPayFine>(Compiler::Misc::opcodePayFine);
            interpreter.installSegment5<OpPayFineThief>(Compiler::Misc::opcodePayFineThief);
            interpreter.installSegment5<OpGoToJail>(Compiler::Misc::opcodeGoToJail);
            interpreter.installSegment5<OpGetLocked<ImplicitRef>>(Compiler::Misc::opcodeGetLocked);
            interpreter.installSegment5<OpGetLocked<ExplicitRef>>(Compiler::Misc::opcodeGetLockedExplicit);
            interpreter.installSegment5<OpGetEffect<ImplicitRef>>(Compiler::Misc::opcodeGetEffect);
            interpreter.installSegment5<OpGetEffect<ExplicitRef>>(Compiler::Misc::opcodeGetEffectExplicit);
            interpreter.installSegment5<OpAddSoulGem<ImplicitRef>>(Compiler::Misc::opcodeAddSoulGem);
            interpreter.installSegment5<OpAddSoulGem<ExplicitRef>>(Compiler::Misc::opcodeAddSoulGemExplicit);
            interpreter.installSegment3<OpRemoveSoulGem<ImplicitRef>>(Compiler::Misc::opcodeRemoveSoulGem);
            interpreter.installSegment3<OpRemoveSoulGem<ExplicitRef>>(Compiler::Misc::opcodeRemoveSoulGemExplicit);
            interpreter.installSegment5<OpDrop<ImplicitRef>>(Compiler::Misc::opcodeDrop);
            interpreter.installSegment5<OpDrop<ExplicitRef>>(Compiler::Misc::opcodeDropExplicit);
            interpreter.installSegment5<OpDropSoulGem<ImplicitRef>>(Compiler::Misc::opcodeDropSoulGem);
            interpreter.installSegment5<OpDropSoulGem<ExplicitRef>>(Compiler::Misc::opcodeDropSoulGemExplicit);
            interpreter.installSegment5<OpGetAttacked<ImplicitRef>>(Compiler::Misc::opcodeGetAttacked);
            interpreter.installSegment5<OpGetAttacked<ExplicitRef>>(Compiler::Misc::opcodeGetAttackedExplicit);
            interpreter.installSegment5<OpGetWeaponDrawn<ImplicitRef>>(Compiler::Misc::opcodeGetWeaponDrawn);
            interpreter.installSegment5<OpGetWeaponDrawn<ExplicitRef>>(Compiler::Misc::opcodeGetWeaponDrawnExplicit);
            interpreter.installSegment5<OpGetSpellReadied<ImplicitRef>>(Compiler::Misc::opcodeGetSpellReadied);
            interpreter.installSegment5<OpGetSpellReadied<ExplicitRef>>(Compiler::Misc::opcodeGetSpellReadiedExplicit);
            interpreter.installSegment5<OpGetSpellEffects<ImplicitRef>>(Compiler::Misc::opcodeGetSpellEffects);
            interpreter.installSegment5<OpGetSpellEffects<ExplicitRef>>(Compiler::Misc::opcodeGetSpellEffectsExplicit);
            interpreter.installSegment5<OpGetCurrentTime>(Compiler::Misc::opcodeGetCurrentTime);
            interpreter.installSegment5<OpSetDelete<ImplicitRef>>(Compiler::Misc::opcodeSetDelete);
            interpreter.installSegment5<OpSetDelete<ExplicitRef>>(Compiler::Misc::opcodeSetDeleteExplicit);
            interpreter.installSegment5<OpGetSquareRoot>(Compiler::Misc::opcodeGetSquareRoot);
            interpreter.installSegment5<OpFall<ImplicitRef>>(Compiler::Misc::opcodeFall);
            interpreter.installSegment5<OpFall<ExplicitRef>>(Compiler::Misc::opcodeFallExplicit);
            interpreter.installSegment5<OpGetStandingPc<ImplicitRef>>(Compiler::Misc::opcodeGetStandingPc);
            interpreter.installSegment5<OpGetStandingPc<ExplicitRef>>(Compiler::Misc::opcodeGetStandingPcExplicit);
            interpreter.installSegment5<OpGetStandingActor<ImplicitRef>>(Compiler::Misc::opcodeGetStandingActor);
            interpreter.installSegment5<OpGetStandingActor<ExplicitRef>>(
                Compiler::Misc::opcodeGetStandingActorExplicit);
            interpreter.installSegment5<OpGetCollidingPc<ImplicitRef>>(Compiler::Misc::opcodeGetCollidingPc);
            interpreter.installSegment5<OpGetCollidingPc<ExplicitRef>>(Compiler::Misc::opcodeGetCollidingPcExplicit);
            interpreter.installSegment5<OpGetCollidingActor<ImplicitRef>>(Compiler::Misc::opcodeGetCollidingActor);
            interpreter.installSegment5<OpGetCollidingActor<ExplicitRef>>(
                Compiler::Misc::opcodeGetCollidingActorExplicit);
            interpreter.installSegment5<OpHurtStandingActor<ImplicitRef>>(Compiler::Misc::opcodeHurtStandingActor);
            interpreter.installSegment5<OpHurtStandingActor<ExplicitRef>>(
                Compiler::Misc::opcodeHurtStandingActorExplicit);
            interpreter.installSegment5<OpHurtCollidingActor<ImplicitRef>>(Compiler::Misc::opcodeHurtCollidingActor);
            interpreter.installSegment5<OpHurtCollidingActor<ExplicitRef>>(
                Compiler::Misc::opcodeHurtCollidingActorExplicit);
            interpreter.installSegment5<OpGetWindSpeed>(Compiler::Misc::opcodeGetWindSpeed);
            interpreter.installSegment5<OpHitOnMe<ImplicitRef>>(Compiler::Misc::opcodeHitOnMe);
            interpreter.installSegment5<OpHitOnMe<ExplicitRef>>(Compiler::Misc::opcodeHitOnMeExplicit);
            interpreter.installSegment5<OpHitAttemptOnMe<ImplicitRef>>(Compiler::Misc::opcodeHitAttemptOnMe);
            interpreter.installSegment5<OpHitAttemptOnMe<ExplicitRef>>(Compiler::Misc::opcodeHitAttemptOnMeExplicit);
            interpreter.installSegment5<OpEnableTeleporting<false>>(Compiler::Misc::opcodeDisableTeleporting);
            interpreter.installSegment5<OpEnableTeleporting<true>>(Compiler::Misc::opcodeEnableTeleporting);
            interpreter.installSegment5<OpShowVars<ImplicitRef>>(Compiler::Misc::opcodeShowVars);
            interpreter.installSegment5<OpShowVars<ExplicitRef>>(Compiler::Misc::opcodeShowVarsExplicit);
            interpreter.installSegment5<OpShow<ImplicitRef>>(Compiler::Misc::opcodeShow);
            interpreter.installSegment5<OpShow<ExplicitRef>>(Compiler::Misc::opcodeShowExplicit);
            interpreter.installSegment5<OpToggleGodMode>(Compiler::Misc::opcodeToggleGodMode);
            interpreter.installSegment5<OpToggleScripts>(Compiler::Misc::opcodeToggleScripts);
            interpreter.installSegment5<OpEnableLevitation<false>>(Compiler::Misc::opcodeDisableLevitation);
            interpreter.installSegment5<OpEnableLevitation<true>>(Compiler::Misc::opcodeEnableLevitation);
            interpreter.installSegment5<OpCast<ImplicitRef>>(Compiler::Misc::opcodeCast);
            interpreter.installSegment5<OpCast<ExplicitRef>>(Compiler::Misc::opcodeCastExplicit);
            interpreter.installSegment5<OpExplodeSpell<ImplicitRef>>(Compiler::Misc::opcodeExplodeSpell);
            interpreter.installSegment5<OpExplodeSpell<ExplicitRef>>(Compiler::Misc::opcodeExplodeSpellExplicit);
            interpreter.installSegment5<OpGetPcInJail>(Compiler::Misc::opcodeGetPcInJail);
            interpreter.installSegment5<OpGetPcTraveling>(Compiler::Misc::opcodeGetPcTraveling);
            interpreter.installSegment3<OpBetaComment<ImplicitRef>>(Compiler::Misc::opcodeBetaComment);
            interpreter.installSegment3<OpBetaComment<ExplicitRef>>(Compiler::Misc::opcodeBetaCommentExplicit);
            interpreter.installSegment5<OpAddToLevCreature>(Compiler::Misc::opcodeAddToLevCreature);
            interpreter.installSegment5<OpRemoveFromLevCreature>(Compiler::Misc::opcodeRemoveFromLevCreature);
            interpreter.installSegment5<OpAddToLevItem>(Compiler::Misc::opcodeAddToLevItem);
            interpreter.installSegment5<OpRemoveFromLevItem>(Compiler::Misc::opcodeRemoveFromLevItem);
            interpreter.installSegment3<OpShowSceneGraph<ImplicitRef>>(Compiler::Misc::opcodeShowSceneGraph);
            interpreter.installSegment3<OpShowSceneGraph<ExplicitRef>>(Compiler::Misc::opcodeShowSceneGraphExplicit);
            interpreter.installSegment5<OpToggleBorders>(Compiler::Misc::opcodeToggleBorders);
            interpreter.installSegment5<OpToggleNavMesh>(Compiler::Misc::opcodeToggleNavMesh);
            interpreter.installSegment5<OpToggleActorsPaths>(Compiler::Misc::opcodeToggleActorsPaths);
            interpreter.installSegment5<OpSetNavMeshNumberToRender>(Compiler::Misc::opcodeSetNavMeshNumberToRender);
            interpreter.installSegment5<OpRepairedOnMe<ImplicitRef>>(Compiler::Misc::opcodeRepairedOnMe);
            interpreter.installSegment5<OpRepairedOnMe<ExplicitRef>>(Compiler::Misc::opcodeRepairedOnMeExplicit);
            interpreter.installSegment5<OpToggleRecastMesh>(Compiler::Misc::opcodeToggleRecastMesh);
            interpreter.installSegment5<OpHelp>(Compiler::Misc::opcodeHelp);
            interpreter.installSegment5<OpReloadLua>(Compiler::Misc::opcodeReloadLua);
            interpreter.installSegment5<OpTestModels>(Compiler::Misc::opcodeTestModels);
            interpreter.installSegment5<OpReportActiveQuests>(Compiler::Misc::opcodeReportActiveQuests);
            interpreter.installSegment5<OpGetGlobal>(Compiler::Misc::opcodeGetGlobal);
            interpreter.installSegment5<OpGetFollowers>(Compiler::Misc::opcodeGetFollowers);
            interpreter.installSegment5<OpSetFollowers>(Compiler::Misc::opcodeSetFollowers);
            interpreter.installSegment5<OpRecoverFollowers>(Compiler::Misc::opcodeRecoverFollowers);
            interpreter.installSegment5<OpListSpells<ImplicitRef>> (Compiler::Misc::opcodeListSpells);
            interpreter.installSegment5<OpListSpells<ExplicitRef>> (Compiler::Misc::opcodeListSpellsExplicit);
        }
    }
}
