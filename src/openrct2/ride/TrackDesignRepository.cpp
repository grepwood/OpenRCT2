/*****************************************************************************
 * Copyright (c) 2014-2019 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "TrackDesignRepository.h"

#include "../Context.h"
#include "../PlatformEnvironment.h"
#include "../config/Config.h"
#include "../core/Collections.hpp"
#include "../core/Console.hpp"
#include "../core/File.h"
#include "../core/FileIndex.hpp"
#include "../core/FileStream.hpp"
#include "../core/Path.hpp"
#include "../core/String.hpp"
#include "../localisation/LocalisationService.h"
#include "../object/ObjectRepository.h"
#include "../object/RideObject.h"
#include "../util/Endian.h"
#include "RideGroupManager.h"
#include "TrackDesign.h"

#include <algorithm>
#include <memory>
#include <vector>

using namespace OpenRCT2;

struct TrackRepositoryItem
{
    std::string Name;
    std::string Path;
    uint8_t RideType = RIDE_TYPE_NULL;
    std::string ObjectEntry;
    uint32_t Flags = 0;
};

enum TRACK_REPO_ITEM_FLAGS
{
    TRIF_READ_ONLY = (1 << 0),
};

std::string GetNameFromTrackPath(const std::string& path)
{
    std::string name = Path::GetFileNameWithoutExtension(path);
    // The track name should be the file name until the first instance of a dot
    name = name.substr(0, name.find_first_of('.'));
    return name;
}

class TrackDesignFileIndex final : public FileIndex<TrackRepositoryItem>
{
private:
    static constexpr uint32_t MAGIC_NUMBER = 0x58444954; // TIDX
    static constexpr uint16_t VERSION = 2;
    static constexpr auto PATTERN = "*.td4;*.td6";

public:
    explicit TrackDesignFileIndex(const IPlatformEnvironment& env)
        : FileIndex(
            "track design index", MAGIC_NUMBER, VERSION, env.GetFilePath(PATHID::CACHE_TRACKS), std::string(PATTERN),
            std::vector<std::string>({
                env.GetDirectoryPath(DIRBASE::RCT1, DIRID::TRACK),
                env.GetDirectoryPath(DIRBASE::RCT2, DIRID::TRACK),
                env.GetDirectoryPath(DIRBASE::USER, DIRID::TRACK),
            }))
    {
    }

public:
    std::tuple<bool, TrackRepositoryItem> Create(int32_t, const std::string& path) const override
    {
        auto td6 = track_design_open(path.c_str());
        if (td6 != nullptr)
        {
            TrackRepositoryItem item;
            item.Name = GetNameFromTrackPath(path);
            item.Path = path;
            item.RideType = td6->type;
            item.ObjectEntry = std::string(td6->vehicle_object.name, 8);
            item.Flags = 0;
            if (IsTrackReadOnly(path))
            {
                item.Flags |= TRIF_READ_ONLY;
            }
            return std::make_tuple(true, item);
        }
        else
        {
            return std::make_tuple(true, TrackRepositoryItem());
        }
    }

protected:
    void Serialise(IStream* stream, const TrackRepositoryItem& item) const override
    {
        stream->WriteString(item.Name);
        stream->WriteString(item.Path);
        stream->WriteValue(item.RideType);
        stream->WriteString(item.ObjectEntry);
        stream->WriteValue(item.Flags);
    }

    TrackRepositoryItem Deserialise(IStream* stream) const override
    {
        TrackRepositoryItem item;
        item.Name = stream->ReadStdString();
        item.Path = stream->ReadStdString();
        item.RideType = stream->ReadValue<uint8_t>();
        item.ObjectEntry = stream->ReadStdString();
        item.Flags = ORCT_ensure_value_is_little_endian32(stream->ReadValue<uint32_t>());
        return item;
    }

private:
    bool IsTrackReadOnly(const std::string& path) const
    {
        return String::StartsWith(path, SearchPaths[0]) || String::StartsWith(path, SearchPaths[1]);
    }
};

class TrackDesignRepository final : public ITrackDesignRepository
{
private:
    std::shared_ptr<IPlatformEnvironment> const _env;
    TrackDesignFileIndex const _fileIndex;
    std::vector<TrackRepositoryItem> _items;

public:
    explicit TrackDesignRepository(const std::shared_ptr<IPlatformEnvironment>& env)
        : _env(env)
        , _fileIndex(*env)
    {
        Guard::ArgumentNotNull(env);
    }

    size_t GetCount() const override
    {
        return _items.size();
    }

    /**
     *
     * @param entry The entry name to count the track list of. Leave empty to count track list for the non-separated types (e.g.
     * Hyper-Twister, Car Ride)
     */
    size_t GetCountForObjectEntry(uint8_t rideType, const std::string& entry) const override
    {
        size_t count = 0;
        const auto& repo = GetContext()->GetObjectRepository();

        for (const auto& item : _items)
        {
            if (item.RideType != rideType)
            {
                continue;
            }

            bool entryIsNotSeparate = false;
            if (entry.empty())
            {
                const ObjectRepositoryItem* ori = repo.FindObject(item.ObjectEntry.c_str());

                if (ori == nullptr || !RideTypeDescriptors[rideType].HasFlag(RIDE_TYPE_FLAG_LIST_VEHICLES_SEPARATELY))
                    entryIsNotSeparate = true;
            }

            if (entryIsNotSeparate || String::Equals(item.ObjectEntry, entry, true))
            {
                count++;
            }
        }
        return count;
    }

    size_t GetCountForRideGroup(uint8_t rideType, const RideGroup* rideGroup) const override
    {
        size_t count = 0;
        const auto& repo = GetContext()->GetObjectRepository();

        for (const auto& item : _items)
        {
            if (item.RideType != rideType)
            {
                continue;
            }

            const ObjectRepositoryItem* ori = repo.FindObject(item.ObjectEntry.c_str());
            uint8_t rideGroupIndex = (ori != nullptr) ? ori->RideInfo.RideGroupIndex : 0;
            const RideGroup* itemRideGroup = RideGroupManager::RideGroupFind(rideType, rideGroupIndex);

            if (itemRideGroup != nullptr && itemRideGroup->Equals(rideGroup))
            {
                count++;
            }
        }

        return count;
    }

    /**
     *
     * @param entry The entry name to build a track list for. Leave empty to build track list for the non-separated types (e.g.
     * Hyper-Twister, Car Ride)
     */
    std::vector<track_design_file_ref> GetItemsForObjectEntry(uint8_t rideType, const std::string& entry) const override
    {
        std::vector<track_design_file_ref> refs;
        const auto& repo = GetContext()->GetObjectRepository();

        for (const auto& item : _items)
        {
            if (item.RideType != rideType)
            {
                continue;
            }

            bool entryIsNotSeparate = false;
            if (entry.empty())
            {
                const ObjectRepositoryItem* ori = repo.FindObject(item.ObjectEntry.c_str());

                if (ori == nullptr || !RideTypeDescriptors[rideType].HasFlag(RIDE_TYPE_FLAG_LIST_VEHICLES_SEPARATELY))
                    entryIsNotSeparate = true;
            }

            if (entryIsNotSeparate || String::Equals(item.ObjectEntry, entry, true))
            {
                track_design_file_ref ref;
                ref.name = String::Duplicate(GetNameFromTrackPath(item.Path));
                ref.path = String::Duplicate(item.Path);
                refs.push_back(ref);
            }
        }

        return refs;
    }

    std::vector<track_design_file_ref> GetItemsForRideGroup(uint8_t rideType, const RideGroup* rideGroup) const override
    {
        std::vector<track_design_file_ref> refs;
        const auto& repo = GetContext()->GetObjectRepository();

        for (const auto& item : _items)
        {
            if (item.RideType != rideType)
            {
                continue;
            }

            const ObjectRepositoryItem* ori = repo.FindObject(item.ObjectEntry.c_str());
            uint8_t rideGroupIndex = (ori != nullptr) ? ori->RideInfo.RideGroupIndex : 0;
            const RideGroup* itemRideGroup = RideGroupManager::RideGroupFind(rideType, rideGroupIndex);

            if (itemRideGroup != nullptr && itemRideGroup->Equals(rideGroup))
            {
                track_design_file_ref ref;
                ref.name = String::Duplicate(GetNameFromTrackPath(item.Path));
                ref.path = String::Duplicate(item.Path);
                refs.push_back(ref);
            }
        }

        return refs;
    }

    void Scan(int32_t language) override
    {
        _items.clear();
        auto trackDesigns = _fileIndex.LoadOrBuild(language);
        for (const auto& td : trackDesigns)
        {
            _items.push_back(td);
        }

        SortItems();
    }

    bool Delete(const std::string& path) override
    {
        bool result = false;
        size_t index = GetTrackIndex(path);
        if (index != SIZE_MAX)
        {
            const TrackRepositoryItem* item = &_items[index];
            if (!(item->Flags & TRIF_READ_ONLY))
            {
                if (File::Delete(path))
                {
                    _items.erase(_items.begin() + index);
                    result = true;
                }
            }
        }
        return result;
    }

    std::string Rename(const std::string& path, const std::string& newName) override
    {
        std::string result;
        size_t index = GetTrackIndex(path);
        if (index != SIZE_MAX)
        {
            TrackRepositoryItem* item = &_items[index];
            if (!(item->Flags & TRIF_READ_ONLY))
            {
                std::string directory = Path::GetDirectory(path);
                std::string newPath = Path::Combine(directory, newName + Path::GetExtension(path));
                if (File::Move(path, newPath))
                {
                    item->Name = newName;
                    item->Path = newPath;
                    SortItems();
                    result = newPath;
                }
            }
        }
        return result;
    }

    std::string Install(const std::string& path) override
    {
        std::string result;
        std::string fileName = Path::GetFileName(path);
        std::string installDir = _env->GetDirectoryPath(DIRBASE::USER, DIRID::TRACK);

        std::string newPath = Path::Combine(installDir, fileName);
        if (File::Copy(path, newPath, false))
        {
            auto language = LocalisationService_GetCurrentLanguage();
            auto td = _fileIndex.Create(language, path);
            if (std::get<0>(td))
            {
                _items.push_back(std::get<1>(td));
                SortItems();
                result = path;
            }
        }
        return result;
    }

private:
    void SortItems()
    {
        std::sort(_items.begin(), _items.end(), [](const TrackRepositoryItem& a, const TrackRepositoryItem& b) -> bool {
            if (a.RideType != b.RideType)
            {
                return a.RideType < b.RideType;
            }
            return String::Compare(a.Name, b.Name) < 0;
        });
    }

    size_t GetTrackIndex(const std::string& path) const
    {
        for (size_t i = 0; i < _items.size(); i++)
        {
            if (Path::Equals(_items[i].Path, path))
            {
                return i;
            }
        }
        return SIZE_MAX;
    }

    TrackRepositoryItem* GetTrackItem(const std::string& path)
    {
        TrackRepositoryItem* result = nullptr;
        size_t index = GetTrackIndex(path);
        if (index != SIZE_MAX)
        {
            result = &_items[index];
        }
        return result;
    }
};

std::unique_ptr<ITrackDesignRepository> CreateTrackDesignRepository(const std::shared_ptr<IPlatformEnvironment>& env)
{
    return std::make_unique<TrackDesignRepository>(env);
}

void track_repository_scan()
{
    ITrackDesignRepository* repo = GetContext()->GetTrackDesignRepository();
    repo->Scan(LocalisationService_GetCurrentLanguage());
}

bool track_repository_delete(const utf8* path)
{
    ITrackDesignRepository* repo = GetContext()->GetTrackDesignRepository();
    return repo->Delete(path);
}

bool track_repository_rename(const utf8* path, const utf8* newName)
{
    ITrackDesignRepository* repo = GetContext()->GetTrackDesignRepository();
    std::string newPath = repo->Rename(path, newName);
    return !newPath.empty();
}

bool track_repository_install(const utf8* srcPath)
{
    ITrackDesignRepository* repo = GetContext()->GetTrackDesignRepository();
    std::string newPath = repo->Install(srcPath);
    return !newPath.empty();
}
