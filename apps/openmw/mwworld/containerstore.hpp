#ifndef GAME_MWWORLD_CONTAINERSTORE_H
#define GAME_MWWORLD_CONTAINERSTORE_H

#include <iterator>
#include <map>
#include <memory>
#include <utility>

#include <components/esm3/loadalch.hpp>
#include <components/esm3/loadappa.hpp>
#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadbook.hpp>
#include <components/esm3/loadclot.hpp>
#include <components/esm3/loadingr.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/esm3/loadlock.hpp>
#include <components/esm3/loadmisc.hpp>
#include <components/esm3/loadprob.hpp>
#include <components/esm3/loadrepa.hpp>
#include <components/esm3/loadweap.hpp>

#include <components/misc/rng.hpp>

#include "cellreflist.hpp"
#include "ptr.hpp"

namespace ESM
{
    struct InventoryList;
    struct InventoryState;
}

namespace MWClass
{
    class Container;
}

namespace MWWorld
{
    class ContainerStore;

    template <class PtrType>
    class ContainerStoreIteratorBase;

    typedef ContainerStoreIteratorBase<Ptr> ContainerStoreIterator;
    typedef ContainerStoreIteratorBase<ConstPtr> ConstContainerStoreIterator;

    class ResolutionListener
    {
        ContainerStore& mStore;

    public:
        ResolutionListener(ContainerStore& store)
            : mStore(store)
        {
        }
        ~ResolutionListener();
    };

    class ResolutionHandle
    {
        std::shared_ptr<ResolutionListener> mListener;

    public:
        ResolutionHandle(std::shared_ptr<ResolutionListener> listener)
            : mListener(std::move(listener))
        {
        }
        ResolutionHandle() = default;
    };

    class ContainerStoreListener
    {
    public:
        virtual void itemAdded(const ConstPtr& item, int count) {}
        virtual void itemRemoved(const ConstPtr& item, int count) {}
        virtual ~ContainerStoreListener() = default;
    };

    class ContainerStore
    {
    public:
        static constexpr int Type_Potion = 0x0001;
        static constexpr int Type_Apparatus = 0x0002;
        static constexpr int Type_Armor = 0x0004;
        static constexpr int Type_Book = 0x0008;
        static constexpr int Type_Clothing = 0x0010;
        static constexpr int Type_Ingredient = 0x0020;
        static constexpr int Type_Light = 0x0040;
        static constexpr int Type_Lockpick = 0x0080;
        static constexpr int Type_Miscellaneous = 0x0100;
        static constexpr int Type_Probe = 0x0200;
        static constexpr int Type_Repair = 0x0400;
        static constexpr int Type_Weapon = 0x0800;

        static constexpr int Type_Last = Type_Weapon;

        static constexpr int Type_All = 0xffff;

        static const ESM::RefId sGoldId;

        static constexpr bool isStorableType(unsigned int t)
        {
            return t == ESM::Potion::sRecordId || t == ESM::Apparatus::sRecordId || t == ESM::Armor::sRecordId
                || t == ESM::Book::sRecordId || t == ESM::Clothing::sRecordId || t == ESM::Ingredient::sRecordId
                || t == ESM::Light::sRecordId || t == ESM::Lockpick::sRecordId || t == ESM::Miscellaneous::sRecordId
                || t == ESM::Probe::sRecordId || t == ESM::Repair::sRecordId || t == ESM::Weapon::sRecordId;
        }
        template <typename T>
        static constexpr bool isStorableType()
        {
            return isStorableType(T::sRecordId);
        }

    protected:
        ContainerStoreListener* mListener;

        // Used in clone() to unset refnums of copies.
        // (RefNum should be unique, copy can not have the same RefNum).
        void updateRefNums();

        // (item, max charge)
        typedef std::vector<std::pair<ContainerStoreIterator, float>> TRechargingItems;
        TRechargingItems mRechargingItems;

        bool mRechargingItemsUpToDate;

    private:
        MWWorld::CellRefList<ESM::Potion> potions;
        MWWorld::CellRefList<ESM::Apparatus> appas;
        MWWorld::CellRefList<ESM::Armor> armors;
        MWWorld::CellRefList<ESM::Book> books;
        MWWorld::CellRefList<ESM::Clothing> clothes;
        MWWorld::CellRefList<ESM::Ingredient> ingreds;
        MWWorld::CellRefList<ESM::Light> lights;
        MWWorld::CellRefList<ESM::Lockpick> lockpicks;
        MWWorld::CellRefList<ESM::Miscellaneous> miscItems;
        MWWorld::CellRefList<ESM::Probe> probes;
        MWWorld::CellRefList<ESM::Repair> repairs;
        MWWorld::CellRefList<ESM::Weapon> weapons;

        mutable float mCachedWeight;
        mutable bool mWeightUpToDate;

        bool mModified;
        bool mResolved;
        unsigned int mSeed;
        MWWorld::SafePtr mPtr; // Container or actor that holds this store.
        std::weak_ptr<ResolutionListener> mResolutionListener;

        ContainerStoreIterator addImp(const Ptr& ptr, int count, bool markModified = true);
        void addInitialItem(
            const ESM::RefId& id, const ESM::RefId& owner, int count, Misc::Rng::Generator* prng, bool topLevel = true);
        void addInitialItemImp(const MWWorld::Ptr& ptr, const ESM::RefId& owner, int count, Misc::Rng::Generator* prng,
            bool topLevel = true);

        template <typename T>
        ContainerStoreIterator getState(CellRefList<T>& collection, const ESM::ObjectState& state);

        template <typename T>
        void storeState(const LiveCellRef<T>& ref, ESM::ObjectState& state) const;

        template <typename T>
        void storeStates(const CellRefList<T>& collection, ESM::InventoryState& inventory, size_t& index,
            bool equipable = false) const;

        void updateRechargingItems();

        virtual void storeEquipmentState(
            const MWWorld::LiveCellRefBase& ref, size_t index, ESM::InventoryState& inventory) const;

        virtual void readEquipmentState(
            const MWWorld::ContainerStoreIterator& iter, size_t index, const ESM::InventoryState& inventory);

    public:
        ContainerStore();

        virtual ~ContainerStore() = default;

        virtual std::unique_ptr<ContainerStore> clone()
        {
            auto res = std::make_unique<ContainerStore>(*this);
            res->updateRefNums();
            return res;
        }

        // Container or actor that holds this store.
        const Ptr& getPtr() const { return mPtr.ptrOrEmpty(); }
        void setPtr(const Ptr& ptr) { mPtr = SafePtr(ptr); }

        ConstContainerStoreIterator cbegin(int mask = Type_All) const;
        ConstContainerStoreIterator cend() const;
        ConstContainerStoreIterator begin(int mask = Type_All) const;
        ConstContainerStoreIterator end() const;

        ContainerStoreIterator begin(int mask = Type_All);
        ContainerStoreIterator end();

        bool hasVisibleItems() const;

        virtual ContainerStoreIterator add(
            const Ptr& itemPtr, int count, bool allowAutoEquip = true, bool resolve = true);
        ///< Add the item pointed to by \a ptr to this container. (Stacks automatically if needed)
        ///
        /// \note The item pointed to is not required to exist beyond this function call.
        ///
        /// \attention Do not add items to an existing stack by increasing the count instead of
        /// calling this function!
        ///
        /// @return if stacking happened, return iterator to the item that was stacked against, otherwise iterator to
        /// the newly inserted item.

        ContainerStoreIterator add(const ESM::RefId& id, int count, bool allowAutoEquip = true);
        ///< Utility to construct a ManualRef and call add(ptr, count, actorPtr, true)

        int remove(const ESM::RefId& itemId, int count, bool equipReplacement = 0, bool resolve = true);
        ///< Remove \a count item(s) designated by \a itemId from this container.
        ///
        /// @return the number of items actually removed

        virtual int remove(const Ptr& item, int count, bool equipReplacement = 0, bool resolve = true);
        ///< Remove \a count item(s) designated by \a item from this inventory.
        ///
        /// @return the number of items actually removed

        void rechargeItems(float duration);
        ///< Restore charge on enchanted items. Note this should only be done for the player.

        ContainerStoreIterator unstack(const Ptr& ptr, int count = 1);
        ///< Unstack an item in this container. The item's count will be set to count, then a new stack will be added
        ///< with (origCount-count).
        ///
        /// @return an iterator to the new stack, or end() if no new stack was created.

        MWWorld::ContainerStoreIterator restack(const MWWorld::Ptr& item);
        ///< Attempt to re-stack an item in this container.
        /// If a compatible stack is found, the item's count is added to that stack, then the original is deleted.
        /// @return If the item was stacked, return the stack, otherwise return the old (untouched) item.

        int count(const ESM::RefId& id) const;
        ///< @return How many items with refID \a id are in this container?

        ContainerStoreListener* getContListener() const;
        void setContListener(ContainerStoreListener* listener);

    protected:
        ContainerStoreIterator addNewStack(const ConstPtr& ptr, int count);
        ///< Add the item to this container (do not try to stack it onto existing items)

        virtual void flagAsModified();

        /// + and - operations that can deal with negative stacks
        /// Note that negativity is infectious
        static int addItems(int count1, int count2);
        static int subtractItems(int count1, int count2);

    public:
        virtual bool stacks(const ConstPtr& ptr1, const ConstPtr& ptr2) const;
        ///< @return true if the two specified objects can stack with each other

        void fill(const ESM::InventoryList& items, const ESM::RefId& owner, Misc::Rng::Generator& seed);
        ///< Insert items into *this.

        void fillNonRandom(const ESM::InventoryList& items, const ESM::RefId& owner, unsigned int seed);
        ///< Insert items into *this, excluding leveled items

        virtual void clear();
        ///< Empty container.

        float getWeight() const;
        ///< Return total weight of the items contained in *this.

        static int getType(const ConstPtr& ptr);
        ///< This function throws an exception, if ptr does not point to an object, that can be
        /// put into a container.

        Ptr findReplacement(const ESM::RefId& id);
        ///< Returns replacement for object with given id. Prefer used items (with low durability left).

        Ptr search(const ESM::RefId& id);

        virtual void writeState(ESM::InventoryState& state) const;

        virtual void readState(const ESM::InventoryState& state);

        bool isResolved() const;

        void resolve();
        ResolutionHandle resolveTemporarily();
        void unresolve();

        friend class ContainerStoreIteratorBase<Ptr>;
        friend class ContainerStoreIteratorBase<ConstPtr>;
        friend class ResolutionListener;
        friend class MWClass::Container;
    };

    template <class PtrType>
    class ContainerStoreIteratorBase
    {
        template <class From, class To, class Dummy>
        struct IsConvertible
        {
            static constexpr bool value = true;
        };

        template <class Dummy>
        struct IsConvertible<ConstPtr, Ptr, Dummy>
        {
            static constexpr bool value = false;
        };

        template <class T, class U>
        struct IteratorTrait
        {
            typedef typename MWWorld::CellRefList<T>::List::iterator type;
        };

        template <class T>
        struct IteratorTrait<T, ConstPtr>
        {
            typedef typename MWWorld::CellRefList<T>::List::const_iterator type;
        };

        template <class T>
        struct Iterator : IteratorTrait<T, PtrType>
        {
        };

        template <class T, class Dummy>
        struct ContainerStoreTrait
        {
            typedef ContainerStore* type;
        };

        template <class Dummy>
        struct ContainerStoreTrait<ConstPtr, Dummy>
        {
            typedef const ContainerStore* type;
        };

        typedef typename ContainerStoreTrait<PtrType, void>::type ContainerStoreType;

        int mType;
        int mMask;
        ContainerStoreType mContainer;
        mutable PtrType mPtr;

        typename Iterator<ESM::Potion>::type mPotion;
        typename Iterator<ESM::Apparatus>::type mApparatus;
        typename Iterator<ESM::Armor>::type mArmor;
        typename Iterator<ESM::Book>::type mBook;
        typename Iterator<ESM::Clothing>::type mClothing;
        typename Iterator<ESM::Ingredient>::type mIngredient;
        typename Iterator<ESM::Light>::type mLight;
        typename Iterator<ESM::Lockpick>::type mLockpick;
        typename Iterator<ESM::Miscellaneous>::type mMiscellaneous;
        typename Iterator<ESM::Probe>::type mProbe;
        typename Iterator<ESM::Repair>::type mRepair;
        typename Iterator<ESM::Weapon>::type mWeapon;

        ContainerStoreIteratorBase(ContainerStoreType container);
        ///< End-iterator

        ContainerStoreIteratorBase(int mask, ContainerStoreType container);
        ///< Begin-iterator

        // construct iterator using a CellRefList iterator
        ContainerStoreIteratorBase(ContainerStoreType container, typename Iterator<ESM::Potion>::type);
        ContainerStoreIteratorBase(ContainerStoreType container, typename Iterator<ESM::Apparatus>::type);
        ContainerStoreIteratorBase(ContainerStoreType container, typename Iterator<ESM::Armor>::type);
        ContainerStoreIteratorBase(ContainerStoreType container, typename Iterator<ESM::Book>::type);
        ContainerStoreIteratorBase(ContainerStoreType container, typename Iterator<ESM::Clothing>::type);
        ContainerStoreIteratorBase(ContainerStoreType container, typename Iterator<ESM::Ingredient>::type);
        ContainerStoreIteratorBase(ContainerStoreType container, typename Iterator<ESM::Light>::type);
        ContainerStoreIteratorBase(ContainerStoreType container, typename Iterator<ESM::Lockpick>::type);
        ContainerStoreIteratorBase(ContainerStoreType container, typename Iterator<ESM::Miscellaneous>::type);
        ContainerStoreIteratorBase(ContainerStoreType container, typename Iterator<ESM::Probe>::type);
        ContainerStoreIteratorBase(ContainerStoreType container, typename Iterator<ESM::Repair>::type);
        ContainerStoreIteratorBase(ContainerStoreType container, typename Iterator<ESM::Weapon>::type);

        template <class T>
        void copy(const ContainerStoreIteratorBase<T>& src);

        void incType();

        void nextType();

        bool resetIterator();
        ///< Reset iterator for selected type.
        ///
        /// \return Type not empty?

        bool incIterator();
        ///< Increment iterator for selected type.
        ///
        /// \return reached the end?

    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = PtrType;
        using difference_type = std::ptrdiff_t;
        using pointer = PtrType*;
        using reference = PtrType&;

        template <class T>
        ContainerStoreIteratorBase(const ContainerStoreIteratorBase<T>& other)
        {
            static_assert(IsConvertible<T, PtrType, void>::value);
            copy(other);
        }

        template <class T>
        bool isEqual(const ContainerStoreIteratorBase<T>& other) const;

        PtrType* operator->() const;
        PtrType operator*() const;

        ContainerStoreIteratorBase& operator++();
        ContainerStoreIteratorBase operator++(int);
        ContainerStoreIteratorBase& operator=(const ContainerStoreIteratorBase& rhs);
        ContainerStoreIteratorBase(const ContainerStoreIteratorBase& rhs) = default;

        int getType() const;
        const ContainerStore* getContainerStore() const;

        friend class ContainerStore;
        friend class ContainerStoreIteratorBase<Ptr>;
        friend class ContainerStoreIteratorBase<ConstPtr>;
    };

    template <class T, class U>
    bool operator==(const ContainerStoreIteratorBase<T>& left, const ContainerStoreIteratorBase<U>& right);
    template <class T, class U>
    bool operator!=(const ContainerStoreIteratorBase<T>& left, const ContainerStoreIteratorBase<U>& right);
}
#endif
