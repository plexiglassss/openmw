#include "cellbindings.hpp"

#include <cstdlib>

#include <components/esm3/loadacti.hpp>
#include <components/esm3/loadalch.hpp>
#include <components/esm3/loadappa.hpp>
#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadbook.hpp>
#include <components/esm3/loadclot.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loaddoor.hpp>
#include <components/esm3/loadingr.hpp>
#include <components/esm3/loadlevlist.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/esm3/loadlock.hpp>
#include <components/esm3/loadmisc.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadprob.hpp>
#include <components/esm3/loadrepa.hpp>
#include <components/esm3/loadstat.hpp>
#include <components/esm3/loadweap.hpp>

#include <components/esm4/loadacti.hpp>
#include <components/esm4/loadalch.hpp>
#include <components/esm4/loadammo.hpp>
#include <components/esm4/loadarmo.hpp>
#include <components/esm4/loadbook.hpp>
#include <components/esm4/loadclot.hpp>
#include <components/esm4/loadcont.hpp>
#include <components/esm4/loaddoor.hpp>
#include <components/esm4/loadflor.hpp>
#include <components/esm4/loadfurn.hpp>
#include <components/esm4/loadimod.hpp>
#include <components/esm4/loadingr.hpp>
#include <components/esm4/loadligh.hpp>
#include <components/esm4/loadmisc.hpp>
#include <components/esm4/loadmstt.hpp>
#include <components/esm4/loadscol.hpp>
#include <components/esm4/loadstat.hpp>
#include <components/esm4/loadtree.hpp>
#include <components/esm4/loadweap.hpp>

#include <components/misc/combinedperformance.hpp>
#include <components/misc/convert.hpp>

#include <components/translation/translation.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/worldmodel.hpp"

#include "types/types.hpp"

namespace sol
{
    template <>
    struct is_automagical<MWLua::LCell> : std::false_type
    {
    };
    template <>
    struct is_automagical<MWLua::GCell> : std::false_type
    {
    };
    template <>
    struct is_automagical<ESM::Pathgrid> : std::false_type
    {
    };
}

namespace MWLua
{

    ObjectIdList queryCellObjects(MWWorld::CellStore& cell, std::optional<std::uint32_t> type, bool useCache)
    {
        if (type && (*type == ESM::REC_DOOR || *type == ESM::REC_NPC_) && !cell.mayHaveReferenceOfType(*type))
            return std::make_shared<std::vector<ObjectId>>();
        if (cell.getState() != MWWorld::CellStore::State_Loaded)
            cell.load();
        auto& world = *MWBase::Environment::get().getWorldModel();
        const bool combined = Misc::useCombinedPerformance();
        const bool cacheable = combined && useCache && type && *type != ESM::REC_INTERNAL_PLAYER;
        if (cacheable)
            if (auto cached = cell.getObjectIdSnapshot(*type, world.getPtrRegistryRevision()))
                return cached;

        ObjectIdList res = std::make_shared<std::vector<ObjectId>>();
        auto visitor = [&](const MWWorld::Ptr& ptr) {
            if (ptr.mRef->isDeleted())
                return true;
            world.registerPtr(ptr);
            if (getLiveCellRefType(ptr.mRef) == ptr.getType())
                res->push_back(getId(ptr));
            return true;
        };

        auto visitTyped = [&]<class T>() {
            if (combined)
                cell.forEachTypeForQuery<T>(visitor);
            else
                cell.forEachType<T>(visitor);
        };

        bool ok = true;
        if (!type.has_value())
            cell.forEach(std::move(visitor));
        else
        {
            uint32_t typeId = *type;
            switch (typeId)
            {
                case ESM::REC_INTERNAL_PLAYER:
                {
                    MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
                    if (player.getCell() == &cell)
                        res->push_back(getId(player));
                }
                break;

                case ESM::REC_CREA:
                    visitTyped.template operator()<ESM::Creature>();
                    break;
                case ESM::REC_NPC_:
                    visitTyped.template operator()<ESM::NPC>();
                    break;
                case ESM::REC_ACTI:
                    visitTyped.template operator()<ESM::Activator>();
                    break;
                case ESM::REC_DOOR:
                    visitTyped.template operator()<ESM::Door>();
                    break;
                case ESM::REC_CONT:
                    visitTyped.template operator()<ESM::Container>();
                    break;

                case ESM::REC_ALCH:
                    visitTyped.template operator()<ESM::Potion>();
                    break;
                case ESM::REC_ARMO:
                    visitTyped.template operator()<ESM::Armor>();
                    break;
                case ESM::REC_BOOK:
                    visitTyped.template operator()<ESM::Book>();
                    break;
                case ESM::REC_CLOT:
                    visitTyped.template operator()<ESM::Clothing>();
                    break;
                case ESM::REC_INGR:
                    visitTyped.template operator()<ESM::Ingredient>();
                    break;
                case ESM::REC_LIGH:
                    visitTyped.template operator()<ESM::Light>();
                    break;
                case ESM::REC_MISC:
                    visitTyped.template operator()<ESM::Miscellaneous>();
                    break;
                case ESM::REC_WEAP:
                    visitTyped.template operator()<ESM::Weapon>();
                    break;
                case ESM::REC_APPA:
                    visitTyped.template operator()<ESM::Apparatus>();
                    break;
                case ESM::REC_LOCK:
                    visitTyped.template operator()<ESM::Lockpick>();
                    break;
                case ESM::REC_PROB:
                    visitTyped.template operator()<ESM::Probe>();
                    break;
                case ESM::REC_REPA:
                    visitTyped.template operator()<ESM::Repair>();
                    break;
                case ESM::REC_STAT:
                    visitTyped.template operator()<ESM::Static>();
                    break;
                case ESM::REC_LEVC:
                    visitTyped.template operator()<ESM::CreatureLevList>();
                    break;

                case ESM::REC_ACTI4:
                    visitTyped.template operator()<ESM4::Activator>();
                    break;
                case ESM::REC_AMMO4:
                    visitTyped.template operator()<ESM4::Ammunition>();
                    break;
                case ESM::REC_ARMO4:
                    visitTyped.template operator()<ESM4::Armor>();
                    break;
                case ESM::REC_BOOK4:
                    visitTyped.template operator()<ESM4::Book>();
                    break;
                case ESM::REC_CLOT4:
                    visitTyped.template operator()<ESM4::Clothing>();
                    break;
                case ESM::REC_CONT4:
                    visitTyped.template operator()<ESM4::Container>();
                    break;
                case ESM::REC_DOOR4:
                    visitTyped.template operator()<ESM4::Door>();
                    break;
                case ESM::REC_FLOR4:
                    visitTyped.template operator()<ESM4::Flora>();
                    break;
                case ESM::REC_FURN4:
                    visitTyped.template operator()<ESM4::Furniture>();
                    break;
                case ESM::REC_IMOD4:
                    visitTyped.template operator()<ESM4::ItemMod>();
                    break;
                case ESM::REC_INGR4:
                    visitTyped.template operator()<ESM4::Ingredient>();
                    break;
                case ESM::REC_LIGH4:
                    visitTyped.template operator()<ESM4::Light>();
                    break;
                case ESM::REC_MISC4:
                    visitTyped.template operator()<ESM4::MiscItem>();
                    break;
                case ESM::REC_MSTT4:
                    visitTyped.template operator()<ESM4::MovableStatic>();
                    break;
                case ESM::REC_ALCH4:
                    visitTyped.template operator()<ESM4::Potion>();
                    break;
                case ESM::REC_SCOL4:
                    visitTyped.template operator()<ESM4::StaticCollection>();
                    break;
                case ESM::REC_STAT4:
                    visitTyped.template operator()<ESM4::Static>();
                    break;
                case ESM::REC_TREE4:
                    visitTyped.template operator()<ESM4::Tree>();
                    break;
                case ESM::REC_WEAP4:
                    visitTyped.template operator()<ESM4::Weapon>();
                    break;

                default:
                    ok = false;
            }
        }
        if (!ok)
            return {};
        if (cacheable)
            cell.setObjectIdSnapshot(*type, world.getPtrRegistryRevision(), res);
        return res;
    }

    namespace
    {
        bool useCellQueryCache()
        {
            static const bool enabled = [] {
                const char* value = std::getenv("OPENMW_LUA_CELL_QUERY_CACHE");
                return value == nullptr || std::string_view(value) != "0";
            }();
            return enabled && Misc::useCombinedPerformance();
        }
    }

    template <class CellT, class ObjectT>
    static void initCellBindings(const std::string& prefix, const Context& context)
    {
        auto view = context.sol();
        sol::usertype<CellT> cellT = view.new_usertype<CellT>(prefix + "Cell");

        cellT[sol::meta_function::equal_to] = [](const CellT& a, const CellT& b) { return a.mStore == b.mStore; };
        cellT[sol::meta_function::to_string] = [](const CellT& c) {
            auto cell = c.mStore->getCell();
            std::stringstream res;
            if (cell->isExterior())
                res << "exterior(" << cell->getGridX() << ", " << cell->getGridY() << ", "
                    << cell->getWorldSpace().toDebugString() << ")";
            else
                res << "interior(" << cell->getNameId() << ")";
            return res.str();
        };

        cellT["name"] = sol::readonly_property([](const CellT& c) { return c.mStore->getCell()->getNameId(); });
        cellT["displayName"] = sol::readonly_property([](const CellT& c) -> std::string_view {
            const auto& storage = MWBase::Environment::get().getWindowManager()->getTranslationDataStorage();
            return storage.translateCellName(c.mStore->getCell()->getNameId());
        });
        cellT["id"] = sol::readonly_property([](const CellT& c) -> ESM::RefId { return c.mStore->getCell()->getId(); });
        cellT["region"]
            = sol::readonly_property([](const CellT& c) -> ESM::RefId { return c.mStore->getCell()->getRegion(); });
        cellT["worldSpaceId"]
            = sol::readonly_property([](const CellT& c) -> ESM::RefId { return c.mStore->getCell()->getWorldSpace(); });
        cellT["gridX"] = sol::readonly_property([](const CellT& c) { return c.mStore->getCell()->getGridX(); });
        cellT["gridY"] = sol::readonly_property([](const CellT& c) { return c.mStore->getCell()->getGridY(); });
        cellT["hasWater"] = sol::readonly_property([](const CellT& c) { return c.mStore->getCell()->hasWater(); });
        cellT["hasSky"] = sol::readonly_property([](const CellT& c) {
            return c.mStore->getCell()->isExterior() || (c.mStore->getCell()->isQuasiExterior()) != 0;
        });
        cellT["isExterior"] = sol::readonly_property([](const CellT& c) { return c.mStore->isExterior(); });
        cellT["hasBeenLoaded"] = sol::readonly_property(
            [](const CellT& c) -> bool { return c.mStore->getState() == MWWorld::CellStore::State::State_Loaded; });

        // deprecated, use cell:hasTag("QuasiExterior") instead
        cellT["isQuasiExterior"]
            = sol::readonly_property([](const CellT& c) { return (c.mStore->getCell()->isQuasiExterior()) != 0; });

        cellT["hasTag"] = [](const CellT& c, std::string_view tag) -> bool {
            if (tag == "NoSleep")
                return (c.mStore->getCell()->noSleep()) != 0;
            else if (tag == "QuasiExterior")
                return (c.mStore->getCell()->isQuasiExterior()) != 0;
            return false;
        };

        cellT["isInSameSpace"] = [](const CellT& c, const ObjectT& obj) {
            const MWWorld::Ptr& ptr = obj.ptr();
            if (!ptr.isInCell())
                return false;
            MWWorld::CellStore* cell = ptr.getCell();
            return cell == c.mStore || (cell->getCell()->getWorldSpace() == c.mStore->getCell()->getWorldSpace());
        };

        cellT["waterLevel"] = sol::readonly_property([](const CellT& c) -> sol::optional<float> {
            if (c.mStore->getCell()->hasWater())
                return c.mStore->getWaterLevel();
            else
                return sol::nullopt;
        });

        cellT["pathGrid"] = sol::readonly_property([](const CellT& c) -> const ESM::Pathgrid* {
            const ESM::Pathgrid* grid
                = MWBase::Environment::get().getESMStore()->get<ESM::Pathgrid>().search(*c.mStore->getCell());
            if (grid && grid->mPoints.empty())
                return nullptr;
            return grid;
        });

        if constexpr (std::is_same_v<CellT, GCell>)
        { // only for global scripts
            cellT["getAll"] = [ids = getPackageToTypeTable(view)](const CellT& cell, sol::optional<sol::table> type) {
                std::optional<std::uint32_t> typeId;
                if (type)
                    typeId = ids[*type] == sol::nil ? 0u : ids[*type].template get<std::uint32_t>();
                ObjectIdList res = queryCellObjects(*cell.mStore, typeId, useCellQueryCache());
                if (!res)
                    throw std::runtime_error(
                        std::string("Incorrect type argument in cell:getAll: ") + LuaUtil::toString(*type));
                return GObjectList{ std::move(res) };
            };
        }

        if (context.initializeOnce("openmw_cellbindings"))
        {
            auto pathGridT = view.new_usertype<ESM::Pathgrid>("ESM3_PathGrid");
            pathGridT[sol::meta_function::to_string] = [](const ESM::Pathgrid& rec) -> std::string {
                return "ESM3_PathGrid[" + rec.mCell.toDebugString() + "]";
            };
            pathGridT["getPoints"] = [](sol::this_state lua, const ESM::Pathgrid& rec) -> sol::table {
                sol::table points(lua, sol::create);
                for (const ESM::Pathgrid::Point& point : rec.mPoints)
                {
                    sol::table table(lua, sol::create);
                    table["autoGenerated"] = point.mAutogenerated == 0;
                    table["relativePosition"] = Misc::Convert::makeOsgVec3f(point);
                    sol::table edges(lua, sol::create);
                    table["connections"] = edges;
                    points.add(table);
                }
                for (const ESM::Pathgrid::Edge& edge : rec.mEdges)
                {
                    sol::table p1 = points[edge.mV0 + 1];
                    sol::table p2 = points[edge.mV1 + 1];
                    p1.get<sol::table>("connections").add(p2);
                    p2.get<sol::table>("connections").add(p1);
                }
                return points;
            };
        }
    }

    void initCellBindingsForLocalScripts(const Context& context)
    {
        initCellBindings<LCell, LObject>("L", context);
    }

    void initCellBindingsForGlobalScripts(const Context& context)
    {
        initCellBindings<GCell, GObject>("G", context);
    }

}
