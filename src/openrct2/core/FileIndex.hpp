/*****************************************************************************
 * Copyright (c) 2014-2019 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "../common.h"
#include "../util/Endian.h"
#include "Console.hpp"
#include "File.h"
#include "FileScanner.h"
#include "FileStream.hpp"
#include "JobPool.hpp"
#include "Path.hpp"

#include <chrono>
#include <list>
#include <string>
#include <tuple>
#include <vector>

template<typename TItem> class FileIndex
{
private:
    struct DirectoryStats
    {
        uint32_t TotalFiles = 0;
        uint64_t TotalFileSize = 0;
        uint32_t FileDateModifiedChecksum = 0;
        uint32_t PathChecksum = 0;
    };

    struct ScanResult
    {
        DirectoryStats const Stats;
        std::vector<std::string> const Files;

        ScanResult(DirectoryStats stats, std::vector<std::string> files)
            : Stats(stats)
            , Files(files)
        {
        }
    };

    struct FileIndexHeader
    {
        uint32_t HeaderSize = sizeof(FileIndexHeader);
        uint32_t MagicNumber = 0;
        uint8_t VersionA = 0;
        uint8_t VersionB = 0;
        uint16_t LanguageId = 0;
        DirectoryStats Stats;
        uint32_t NumItems = 0;
    };

    // Index file format version which when incremented forces a rebuild
    static constexpr uint8_t FILE_INDEX_VERSION = 4;

    std::string const _name;
    uint32_t const _magicNumber;
    uint8_t const _version;
    std::string const _indexPath;
    std::string const _pattern;

public:
    std::vector<std::string> const SearchPaths;

public:
    /**
     * Creates a new FileIndex.
     * @param name Name of the index (used for logging).
     * @param magicNumber Magic number for the index (to distinguish between different index files).
     * @param version Version of the specialised index, increment this to force a rebuild.
     * @param indexPath Full path to read and write the index file to.
     * @param pattern The search pattern for indexing files.
     * @param paths A list of search directories.
     */
    FileIndex(
        std::string name, uint32_t magicNumber, uint8_t version, std::string indexPath, std::string pattern,
        std::vector<std::string> paths)
        : _name(name)
        , _magicNumber(magicNumber)
        , _version(version)
        , _indexPath(indexPath)
        , _pattern(pattern)
        , SearchPaths(paths)
    {
    }

    virtual ~FileIndex() = default;

    /**
     * Queries and directories and loads the index header. If the index is up to date,
     * the items are loaded from the index and returned, otherwise the index is rebuilt.
     */
    std::vector<TItem> LoadOrBuild(int32_t language) const
    {
        std::vector<TItem> items;
        auto scanResult = Scan();
        auto readIndexResult = ReadIndexFile(language, scanResult.Stats);
        if (std::get<0>(readIndexResult))
        {
            // Index was loaded
            items = std::get<1>(readIndexResult);
        }
        else
        {
            // Index was not loaded
            items = Build(language, scanResult);
        }
        return items;
    }

    std::vector<TItem> Rebuild(int32_t language) const
    {
        auto scanResult = Scan();
        auto items = Build(language, scanResult);
        return items;
    }

protected:
    /**
     * Loads the given file and creates the item representing the data to store in the index.
     * TODO Use std::optional when C++17 is available.
     */
    virtual std::tuple<bool, TItem> Create(int32_t language, const std::string& path) const abstract;

    /**
     * Serialises an index item to the given stream.
     */
    virtual void Serialise(IStream* stream, const TItem& item) const abstract;

    /**
     * Deserialises an index item from the given stream.
     */
    virtual TItem Deserialise(IStream* stream) const abstract;

private:
    ScanResult Scan() const
    {
        DirectoryStats stats{};
        std::vector<std::string> files;
        for (const auto& directory : SearchPaths)
        {
            auto absoluteDirectory = Path::GetAbsolute(directory);
            log_verbose("FileIndex:Scanning for %s in '%s'", _pattern.c_str(), absoluteDirectory.c_str());

            auto pattern = Path::Combine(absoluteDirectory, _pattern);
            auto scanner = Path::ScanDirectory(pattern, true);
            while (scanner->Next())
            {
                auto fileInfo = scanner->GetFileInfo();
                auto path = std::string(scanner->GetPath());

                files.push_back(path);

                stats.TotalFiles++;
                stats.TotalFileSize += fileInfo->Size;
                stats.FileDateModifiedChecksum ^= static_cast<uint32_t>(fileInfo->LastModified >> 32)
                    ^ static_cast<uint32_t>(fileInfo->LastModified & 0xFFFFFFFF);
                stats.FileDateModifiedChecksum = ror32(stats.FileDateModifiedChecksum, 5);
                stats.PathChecksum += GetPathChecksum(path);
            }
            delete scanner;
        }
        return ScanResult(stats, files);
    }

    void BuildRange(
        int32_t language, const ScanResult& scanResult, size_t rangeStart, size_t rangeEnd, std::vector<TItem>& items,
        std::atomic<size_t>& processed, std::mutex& printLock) const
    {
        items.reserve(rangeEnd - rangeStart);
        for (size_t i = rangeStart; i < rangeEnd; i++)
        {
            const auto& filePath = scanResult.Files.at(i);

            if (_log_levels[DIAGNOSTIC_LEVEL_VERBOSE])
            {
                std::lock_guard<std::mutex> lock(printLock);
                log_verbose("FileIndex:Indexing '%s'", filePath.c_str());
            }

            auto item = Create(language, filePath);
            if (std::get<0>(item))
            {
                items.push_back(std::get<1>(item));
            }

            processed++;
        }
    }

    std::vector<TItem> Build(int32_t language, const ScanResult& scanResult) const
    {
        std::vector<TItem> allItems;
        Console::WriteLine("Building %s (%zu items)", _name.c_str(), scanResult.Files.size());

        auto startTime = std::chrono::high_resolution_clock::now();

        const size_t totalCount = scanResult.Files.size();
        if (totalCount > 0)
        {
            JobPool jobPool;
            std::mutex printLock; // For verbose prints.

            std::list<std::vector<TItem>> containers;

            size_t stepSize = 100; // Handpicked, seems to work well with 4/8 cores.

            std::atomic<size_t> processed = ATOMIC_VAR_INIT(0);

            auto reportProgress = [&]() {
                const size_t completed = processed;
                Console::WriteFormat("File %5zu of %zu, done %3d%%\r", completed, totalCount, completed * 100 / totalCount);
            };

            for (size_t rangeStart = 0; rangeStart < totalCount; rangeStart += stepSize)
            {
                if (rangeStart + stepSize > totalCount)
                {
                    stepSize = totalCount - rangeStart;
                }

                auto& items = containers.emplace_back();

                jobPool.AddTask(std::bind(
                    &FileIndex<TItem>::BuildRange, this, language, std::cref(scanResult), rangeStart, rangeStart + stepSize,
                    std::ref(items), std::ref(processed), std::ref(printLock)));

                reportProgress();
            }

            jobPool.Join(reportProgress);

            for (auto&& itr : containers)
            {
                allItems.insert(allItems.end(), itr.begin(), itr.end());
            }
        }

        WriteIndexFile(language, scanResult.Stats, allItems);

        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration<float>(endTime - startTime);
        Console::WriteLine("Finished building %s in %.2f seconds.", _name.c_str(), duration.count());

        return allItems;
    }

    std::tuple<bool, std::vector<TItem>> ReadIndexFile(int32_t language, const DirectoryStats& stats) const
    {
        bool loadedItems = false;
        std::vector<TItem> items;
        if (File::Exists(_indexPath))
        {
            try
            {
                log_verbose("FileIndex:Loading index: '%s'", _indexPath.c_str());
                auto fs = FileStream(_indexPath, FILE_MODE_OPEN);

                // Read header, check if we need to re-scan
                auto header = fs.ReadValue<FileIndexHeader>();
                header.HeaderSize = ORCT_ensure_value_is_little_endian32(header.HeaderSize);
                header.MagicNumber = ORCT_ensure_value_is_little_endian32(header.MagicNumber);
                header.LanguageId = ORCT_ensure_value_is_little_endian16(header.LanguageId);
                header.NumItems = ORCT_ensure_value_is_little_endian32(header.NumItems);
                header.Stats.TotalFiles = ORCT_ensure_value_is_little_endian32(header.Stats.TotalFiles);
                header.Stats.TotalFileSize = ORCT_ensure_value_is_little_endian64(header.Stats.TotalFileSize);
                header.Stats.FileDateModifiedChecksum = ORCT_ensure_value_is_little_endian32(header.Stats.FileDateModifiedChecksum);
                header.Stats.PathChecksum = ORCT_ensure_value_is_little_endian32(header.Stats.PathChecksum);
                if (header.HeaderSize == sizeof(FileIndexHeader) && header.MagicNumber == _magicNumber
                    && header.VersionA == FILE_INDEX_VERSION && header.VersionB == _version && header.LanguageId == language
                    && header.Stats.TotalFiles == stats.TotalFiles && header.Stats.TotalFileSize == stats.TotalFileSize
                    && header.Stats.FileDateModifiedChecksum == stats.FileDateModifiedChecksum
                    && header.Stats.PathChecksum == stats.PathChecksum)
                {
                    items.reserve(header.NumItems);
                    // Directory is the same, just read the saved items
                    for (uint32_t i = 0; i < header.NumItems; i++)
                    {
                        auto item = Deserialise(&fs);
                        items.push_back(item);
                    }
                    loadedItems = true;
                }
                else
                {
                    Console::WriteLine("%s out of date", _name.c_str());
                }
            }
            catch (const std::exception& e)
            {
                Console::Error::WriteLine("Unable to load index: '%s'.", _indexPath.c_str());
                Console::Error::WriteLine("%s", e.what());
            }
        }
        return std::make_tuple(loadedItems, items);
    }

    void WriteIndexFile(int32_t language, const DirectoryStats& stats, const std::vector<TItem>& items) const
    {
        try
        {
            log_verbose("FileIndex:Writing index: '%s'", _indexPath.c_str());
            Path::CreateDirectory(Path::GetDirectory(_indexPath));
            auto fs = FileStream(_indexPath, FILE_MODE_WRITE);

            // Write header
            FileIndexHeader header;
            header.MagicNumber = _magicNumber;
            header.VersionA = FILE_INDEX_VERSION;
            header.VersionB = _version;
            header.LanguageId = language;
            header.Stats = stats;
            header.NumItems = static_cast<uint32_t>(items.size());
            fs.WriteValue(header);

            // Write items
            for (const auto& item : items)
            {
                Serialise(&fs, item);
            }
        }
        catch (const std::exception& e)
        {
            Console::Error::WriteLine("Unable to save index: '%s'.", _indexPath.c_str());
            Console::Error::WriteLine("%s", e.what());
        }
    }

    static uint32_t GetPathChecksum(const std::string& path)
    {
        uint32_t hash = 0xD8430DED;
        for (const utf8* ch = path.c_str(); *ch != '\0'; ch++)
        {
            hash += (*ch);
            hash += (hash << 10);
            hash ^= (hash >> 6);
        }
        hash += (hash << 3);
        hash ^= (hash >> 11);
        hash += (hash << 15);
        return hash;
    }
};
