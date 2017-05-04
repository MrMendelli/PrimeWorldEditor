#include "CGameExporter.h"
#include "CGameInfo.h"
#include "CResourceIterator.h"
#include "CResourceStore.h"
#include "Core/CompressionUtil.h"
#include "Core/Resource/CWorld.h"
#include "Core/Resource/Script/CMasterTemplate.h"
#include <Common/AssertMacro.h>
#include <Common/CScopedTimer.h>
#include <Common/FileIO.h>
#include <Common/FileUtil.h>
#include <Common/Serialization/CXMLWriter.h>
#include <tinyxml2.h>

#define LOAD_PAKS 1
#define SAVE_PACKAGE_DEFINITIONS 1
#define USE_ASSET_NAME_MAP 1
#define EXPORT_COOKED 1

CGameExporter::CGameExporter(EGame Game, ERegion Region, const TString& rkGameName, const TString& rkGameID, float BuildVersion)
    : mGame(Game)
    , mRegion(Region)
    , mGameName(rkGameName)
    , mGameID(rkGameID)
    , mBuildVersion(BuildVersion)
{
    ASSERT(mGame != eUnknownGame);
    ASSERT(mRegion != eRegion_Unknown);
}

#if PUBLIC_RELEASE
#error Fix export directory being cleared!
#endif

bool CGameExporter::Export(nod::DiscBase *pDisc, const TString& rkOutputDir, CAssetNameMap *pNameMap, CGameInfo *pGameInfo)
{
    SCOPED_TIMER(ExportGame);

    mpDisc = pDisc;
    mpNameMap = pNameMap;
    mpGameInfo = pGameInfo;

    mExportDir = FileUtil::MakeAbsolute(rkOutputDir);
    mDiscDir = "Disc\\";
    mWorldsDirName = "Worlds\\";

    // Create project
    FileUtil::MakeDirectory(mExportDir);
    FileUtil::ClearDirectory(mExportDir);

    // Extract disc
    if (!ExtractDiscData())
        return false;

    // Create project
    mpProject = CGameProject::CreateProjectForExport(
                mExportDir,
                mGame,
                mRegion,
                mGameID,
                mBuildVersion,
                mDolPath,
                mApploaderPath,
                mPartitionHeaderPath,
                mFilesystemAddress);

    mpProject->SetProjectName(mGameName);
    mpStore = mpProject->ResourceStore();
    mContentDir = mpStore->RawDir(false);
    mCookedDir = mpStore->CookedDir(false);

    CResourceStore *pOldStore = gpResourceStore;
    gpResourceStore = mpStore;

    // Export game data
    LoadPaks();
    ExportCookedResources();
    mpProject->AudioManager()->LoadAssets();
    ExportResourceEditorData();

    // Export finished!
    mProjectPath = mpProject->ProjectPath();
    delete mpProject;
    if (pOldStore) gpResourceStore = pOldStore;
    return true;
}

void CGameExporter::LoadResource(const CAssetID& rkID, std::vector<u8>& rBuffer)
{
    SResourceInstance *pInst = FindResourceInstance(rkID);
    if (pInst) LoadResource(*pInst, rBuffer);
}

// ************ PROTECTED ************
bool CGameExporter::ExtractDiscData()
{
    // todo: handle dol, apploader, multiple partitions, wii ticket blob
    SCOPED_TIMER(ExtractDiscData);

    // Create Disc output folder
    TString AbsDiscDir = mExportDir + mDiscDir;
    FileUtil::MakeDirectory(AbsDiscDir);

    // Extract disc filesystem
    nod::Partition *pDataPartition = mpDisc->getDataPartition();
    nod::ExtractionContext Context;
    Context.force = false;
    Context.verbose = false;
    Context.progressCB = nullptr;
    bool Success = ExtractDiscNodeRecursive(&pDataPartition->getFSTRoot(), AbsDiscDir, Context);
    if (!Success) return false;

    // Extract dol
    mDolPath = "boot.dol";
    CFileOutStream DolFile(mExportDir + mDolPath);
    if (!DolFile.IsValid()) return false;

    std::unique_ptr<uint8_t[]> pDolBuffer = pDataPartition->getDOLBuf();
    DolFile.WriteBytes(pDolBuffer.get(), (u32) pDataPartition->getDOLSize());
    DolFile.Close();

    // Extract apploader
    mApploaderPath = "apploader.img";
    CFileOutStream ApploaderFile(mExportDir + mApploaderPath);
    if (!ApploaderFile.IsValid()) return false;

    std::unique_ptr<uint8_t[]> pApploaderBuffer = pDataPartition->getApploaderBuf();
    ApploaderFile.WriteBytes(pApploaderBuffer.get(), (u32) pDataPartition->getApploaderSize());
    ApploaderFile.Close();

    // Extract Wii partition header
    bool IsWii = (mBuildVersion >= 3.f);

    if (IsWii)
    {
        mFilesystemAddress = 0;
        mPartitionHeaderPath = "partition_header.bin";
        nod::DiscWii *pDiscWii = static_cast<nod::DiscWii*>(mpDisc);
        Success = pDiscWii->writeOutDataPartitionHeader(*TString(mExportDir + mPartitionHeaderPath).ToUTF16());
        if (!Success) return false;
    }
    else
        mFilesystemAddress = (u32) pDataPartition->getFSTMemoryAddr();

    return true;
}

bool CGameExporter::ExtractDiscNodeRecursive(const nod::Node *pkNode, const TString& rkDir, const nod::ExtractionContext& rkContext)
{
    for (nod::Node::DirectoryIterator Iter = pkNode->begin(); Iter != pkNode->end(); ++Iter)
    {
        if (Iter->getKind() == nod::Node::Kind::File)
        {
            TString FilePath = rkDir + Iter->getName();
            bool Success = Iter->extractToDirectory(*rkDir.ToUTF16(), rkContext);
            if (!Success) return false;

            if (FilePath.GetFileExtension() == "pak")
                mPaks.push_back(FilePath);
        }

        else
        {
            TString Subdir = rkDir + Iter->getName() + "\\";
            bool Success = FileUtil::MakeDirectory(Subdir);
            if (!Success) return false;

            Success = ExtractDiscNodeRecursive(&*Iter, Subdir, rkContext);
            if (!Success) return false;
        }
    }

    return true;
}

// ************ RESOURCE LOADING ************
void CGameExporter::LoadPaks()
{
#if LOAD_PAKS
    SCOPED_TIMER(LoadPaks);

    mPaks.sort([](const TString& rkLeft, const TString& rkRight) -> bool {
        return rkLeft.ToUpper() < rkRight.ToUpper();
    });

    for (auto It = mPaks.begin(); It != mPaks.end(); It++)
    {
        TString PakPath = *It;
        CFileInStream Pak(PakPath, IOUtil::eBigEndian);

        if (!Pak.IsValid())
        {
            Log::Error("Couldn't open pak: " + PakPath);
            continue;
        }

        CPackage *pPackage = new CPackage(mpProject, PakPath.GetFileName(false), FileUtil::MakeRelative(PakPath.GetFileDirectory(), mExportDir + mDiscDir));

        // MP1-MP3Proto
        if (mGame < eCorruption)
        {
            u32 PakVersion = Pak.ReadLong();
            Pak.Seek(0x4, SEEK_CUR);
            ASSERT(PakVersion == 0x00030005);

            // Echoes demo disc has a pak that ends right here.
            if (!Pak.EoF())
            {
                u32 NumNamedResources = Pak.ReadLong();
                ASSERT(NumNamedResources > 0);

                for (u32 iName = 0; iName < NumNamedResources; iName++)
                {
                    CFourCC ResType = Pak.ReadLong();
                    CAssetID ResID(Pak, mGame);
                    u32 NameLen = Pak.ReadLong();
                    TString Name = Pak.ReadString(NameLen);
                    pPackage->AddResource(Name, ResID, ResType);
                }

                u32 NumResources = Pak.ReadLong();

                // Keep track of which areas have duplicate resources
                std::set<CAssetID> PakResourceSet;
                bool AreaHasDuplicates = true; // Default to true so that first area is always considered as having duplicates

                for (u32 iRes = 0; iRes < NumResources; iRes++)
                {
                    bool Compressed = (Pak.ReadLong() == 1);
                    CFourCC ResType = Pak.ReadLong();
                    CAssetID ResID(Pak, mGame);
                    u32 ResSize = Pak.ReadLong();
                    u32 ResOffset = Pak.ReadLong();

                    if (mResourceMap.find(ResID) == mResourceMap.end())
                        mResourceMap[ResID] = SResourceInstance { PakPath, ResID, ResType, ResOffset, ResSize, Compressed, false };

                    // Check for duplicate resources
                    if (ResType == "MREA")
                    {
                        mAreaDuplicateMap[ResID] = AreaHasDuplicates;
                        AreaHasDuplicates = false;
                    }

                    else if (!AreaHasDuplicates && PakResourceSet.find(ResID) != PakResourceSet.end())
                        AreaHasDuplicates = true;

                    else
                        PakResourceSet.insert(ResID);
                }
            }
        }

        // MP3 + DKCR
        else
        {
            u32 PakVersion = Pak.ReadLong();
            u32 PakHeaderLen = Pak.ReadLong();
            Pak.Seek(PakHeaderLen - 0x8, SEEK_CUR);
            ASSERT(PakVersion == 2);

            struct SPakSection {
                CFourCC Type; u32 Size;
            };
            std::vector<SPakSection> PakSections;

            u32 NumPakSections = Pak.ReadLong();
            ASSERT(NumPakSections == 3);

            for (u32 iSec = 0; iSec < NumPakSections; iSec++)
            {
                CFourCC Type = Pak.ReadLong();
                u32 Size = Pak.ReadLong();
                PakSections.push_back(SPakSection { Type, Size });
            }
            Pak.SeekToBoundary(64);

            for (u32 iSec = 0; iSec < NumPakSections; iSec++)
            {
                u32 Next = Pak.Tell() + PakSections[iSec].Size;

                // Named Resources
                if (PakSections[iSec].Type == "STRG")
                {
                    u32 NumNamedResources = Pak.ReadLong();

                    for (u32 iName = 0; iName < NumNamedResources; iName++)
                    {
                        TString Name = Pak.ReadString();
                        CFourCC ResType = Pak.ReadLong();
                        CAssetID ResID(Pak, mGame);
                        pPackage->AddResource(Name, ResID, ResType);
                    }
                }

                else if (PakSections[iSec].Type == "RSHD")
                {
                    ASSERT(PakSections[iSec + 1].Type == "DATA");
                    u32 DataStart = Next;
                    u32 NumResources = Pak.ReadLong();

                    // Keep track of which areas have duplicate resources
                    std::set<CAssetID> PakResourceSet;
                    bool AreaHasDuplicates = true; // Default to true so that first area is always considered as having duplicates

                    for (u32 iRes = 0; iRes < NumResources; iRes++)
                    {
                        bool Compressed = (Pak.ReadLong() == 1);
                        CFourCC Type = Pak.ReadLong();
                        CAssetID ResID(Pak, mGame);
                        u32 Size = Pak.ReadLong();
                        u32 Offset = DataStart + Pak.ReadLong();

                        if (mResourceMap.find(ResID) == mResourceMap.end())
                            mResourceMap[ResID] = SResourceInstance { PakPath, ResID, Type, Offset, Size, Compressed, false };

                        // Check for duplicate resources (unnecessary for DKCR)
                        if (mGame != eReturns)
                        {
                            if (Type == "MREA")
                            {
                                mAreaDuplicateMap[ResID] = AreaHasDuplicates;
                                AreaHasDuplicates = false;
                            }

                            else if (!AreaHasDuplicates && PakResourceSet.find(ResID) != PakResourceSet.end())
                                AreaHasDuplicates = true;

                            else
                                PakResourceSet.insert(ResID);
                        }
                    }
                }

                Pak.Seek(Next, SEEK_SET);
            }
        }

        // Add package to project and save
        mpProject->AddPackage(pPackage);
#if SAVE_PACKAGE_DEFINITIONS
        bool SaveSuccess = pPackage->Save();
        ASSERT(SaveSuccess);
#endif
    }
#endif
}

void CGameExporter::LoadResource(const SResourceInstance& rkResource, std::vector<u8>& rBuffer)
{
    CFileInStream Pak(rkResource.PakFile, IOUtil::eBigEndian);

    if (Pak.IsValid())
    {
        Pak.Seek(rkResource.PakOffset, SEEK_SET);

        // Handle compression
        if (rkResource.Compressed)
        {
            bool ZlibCompressed = (mGame <= eEchoesDemo || mGame == eReturns);

            if (mGame <= eCorruptionProto)
            {
                std::vector<u8> CompressedData(rkResource.PakSize);

                u32 UncompressedSize = Pak.ReadLong();
                rBuffer.resize(UncompressedSize);
                Pak.ReadBytes(CompressedData.data(), CompressedData.size());

                if (ZlibCompressed)
                {
                    u32 TotalOut;
                    CompressionUtil::DecompressZlib(CompressedData.data(), CompressedData.size(), rBuffer.data(), rBuffer.size(), TotalOut);
                }
                else
                {
                    CompressionUtil::DecompressSegmentedData(CompressedData.data(), CompressedData.size(), rBuffer.data(), rBuffer.size());
                }
            }

            else
            {
                CFourCC Magic = Pak.ReadLong();
                ASSERT(Magic == "CMPD");

                u32 NumBlocks = Pak.ReadLong();

                struct SCompressedBlock {
                    u32 CompressedSize; u32 UncompressedSize;
                };
                std::vector<SCompressedBlock> CompressedBlocks;

                u32 TotalUncompressedSize = 0;
                for (u32 iBlock = 0; iBlock < NumBlocks; iBlock++)
                {
                    u32 CompressedSize = (Pak.ReadLong() & 0x00FFFFFF);
                    u32 UncompressedSize = Pak.ReadLong();

                    TotalUncompressedSize += UncompressedSize;
                    CompressedBlocks.push_back( SCompressedBlock { CompressedSize, UncompressedSize } );
                }

                rBuffer.resize(TotalUncompressedSize);
                u32 Offset = 0;

                for (u32 iBlock = 0; iBlock < NumBlocks; iBlock++)
                {
                    u32 CompressedSize = CompressedBlocks[iBlock].CompressedSize;
                    u32 UncompressedSize = CompressedBlocks[iBlock].UncompressedSize;

                    // Block is compressed
                    if (CompressedSize != UncompressedSize)
                    {
                        std::vector<u8> CompressedData(CompressedBlocks[iBlock].CompressedSize);
                        Pak.ReadBytes(CompressedData.data(), CompressedData.size());

                        if (ZlibCompressed)
                        {
                            u32 TotalOut;
                            CompressionUtil::DecompressZlib(CompressedData.data(), CompressedData.size(), rBuffer.data() + Offset, UncompressedSize, TotalOut);
                        }
                        else
                        {
                            CompressionUtil::DecompressSegmentedData(CompressedData.data(), CompressedData.size(), rBuffer.data() + Offset, UncompressedSize);
                        }
                    }
                    // Block is uncompressed
                    else
                        Pak.ReadBytes(rBuffer.data() + Offset, UncompressedSize);

                    Offset += UncompressedSize;
                }
            }
        }

        // Handle uncompressed
        else
        {
            rBuffer.resize(rkResource.PakSize);
            Pak.ReadBytes(rBuffer.data(), rBuffer.size());
        }
    }
}

void CGameExporter::ExportCookedResources()
{
    SCOPED_TIMER(ExportCookedResources);
    FileUtil::MakeDirectory(mCookedDir);

    for (auto It = mResourceMap.begin(); It != mResourceMap.end(); It++)
    {
        SResourceInstance& rRes = It->second;
        ExportResource(rRes);
    }
}

void CGameExporter::ExportResourceEditorData()
{
    {
        // Save raw versions of resources + resource cache data files
        // Note this has to be done after all cooked resources are exported
        // because we have to load the resource to build its dependency tree and
        // some resources will fail to load if their dependencies don't exist
        SCOPED_TIMER(SaveRawResources);

        // todo: we're wasting a ton of time loading the same resources over and over because most resources automatically
        // load all their dependencies and then we just clear it out from memory even though we'll need it again later. we
        // should really be doing this by dependency order instead of by ID order.
        for (CResourceIterator It(mpStore); It; ++It)
        {
            if (!It->IsTransient())
            {
                // Worlds need some info we can only get from the pak at export time; namely, which areas can
                // have duplicates, as well as the world's internal name.
                if (It->ResourceType() == eWorld)
                {
                    CWorld *pWorld = (CWorld*) It->Load();

                    // Set area duplicate flags
                    for (u32 iArea = 0; iArea < pWorld->NumAreas(); iArea++)
                    {
                        CAssetID AreaID = pWorld->AreaResourceID(iArea);
                        auto Find = mAreaDuplicateMap.find(AreaID);

                        if (Find != mAreaDuplicateMap.end())
                            pWorld->SetAreaAllowsPakDuplicates(iArea, Find->second);
                    }

                    // Set world name
                    TString WorldName = MakeWorldName(pWorld->ID());
                    pWorld->SetName(WorldName);
                }

                // Save raw resource + generate dependencies
                if (It->TypeInfo()->CanBeSerialized())
                    It->Save(true);
                else
                    It->UpdateDependencies();
            }
        }
    }
    {
        // All resources should have dependencies generated, so save the project files
        SCOPED_TIMER(SaveResourceDatabase);
#if EXPORT_COOKED
        mpStore->SaveResourceDatabase();
#endif
        bool SaveSuccess = mpProject->Save();
        ASSERT(SaveSuccess);
        mpStore->SaveCacheFile();
    }
}

void CGameExporter::ExportResource(SResourceInstance& rRes)
{
    if (!rRes.Exported)
    {
        std::vector<u8> ResourceData;
        LoadResource(rRes, ResourceData);

        // Register resource and write to file
        TString Directory, Name;

#if USE_ASSET_NAME_MAP
        mpNameMap->GetNameInfo(rRes.ResourceID, Directory, Name);
#else
        Directory = "Uncategorized";
        Name = rRes.ResourceID.ToString();
#endif

        CResourceEntry *pEntry = mpStore->RegisterResource(rRes.ResourceID, CResTypeInfo::TypeForCookedExtension(mGame, rRes.ResourceType)->Type(), Directory, Name);

#if EXPORT_COOKED
        // Save cooked asset
        TString OutCookedPath = pEntry->CookedAssetPath();
        FileUtil::MakeDirectory(OutCookedPath.GetFileDirectory());
        CFileOutStream Out(OutCookedPath, IOUtil::eBigEndian);

        if (Out.IsValid())
            Out.WriteBytes(ResourceData.data(), ResourceData.size());

        ASSERT(pEntry->HasCookedVersion());
#endif

        rRes.Exported = true;
    }
}

TString CGameExporter::MakeWorldName(CAssetID WorldID)
{
    CResourceEntry *pWorldEntry = mpStore->FindEntry(WorldID);
    ASSERT(pWorldEntry && pWorldEntry->ResourceType() == eWorld);

    // Find the original world name in the package resource names
    TString WorldName;

    for (u32 iPkg = 0; iPkg < mpProject->NumPackages(); iPkg++)
    {
        CPackage *pPkg = mpProject->PackageByIndex(iPkg);

        for (u32 iRes = 0; iRes < pPkg->NumNamedResources(); iRes++)
        {
            const SNamedResource& rkRes = pPkg->NamedResourceByIndex(iRes);

            if (rkRes.ID == WorldID && !rkRes.Name.EndsWith("_NODEPEND"))
            {
                WorldName = rkRes.Name;
                break;
            }
        }

        if (!WorldName.IsEmpty()) break;
    }

    // Fix up the name; remove date/time, leading exclamation points, etc
    if (!WorldName.IsEmpty())
    {
        // World names are basically formatted differently in every game...
        // MP1 demo - Remove ! from the beginning
        if (mGame == ePrimeDemo)
        {
            if (WorldName.StartsWith('!'))
                WorldName = WorldName.ChopFront(1);
        }

        // MP1 - Remove prefix characters and ending date
        else if (mGame == ePrime)
        {
            WorldName = WorldName.ChopFront(2);
            bool StartedDate = false;

            while (!WorldName.IsEmpty())
            {
                char Chr = WorldName.Back();

                if (!StartedDate && Chr >= '0' && Chr <= '9')
                    StartedDate = true;
                else if (StartedDate && Chr != '_' && (Chr < '0' || Chr > '9'))
                    break;

                WorldName = WorldName.ChopBack(1);
            }
        }

        // MP2 demo - Use text between the first and second underscores
        else if (mGame == eEchoesDemo)
        {
            u32 UnderscoreA = WorldName.IndexOf('_');
            u32 UnderscoreB = WorldName.IndexOf('_', UnderscoreA + 1);

            if (UnderscoreA != UnderscoreB && UnderscoreA != -1 && UnderscoreB != -1)
                WorldName = WorldName.SubString(UnderscoreA + 1, UnderscoreB - UnderscoreA - 1);
        }

        // MP2 - Remove text before first underscore and after last underscore, strip remaining underscores (except multiplayer maps, which have one underscore)
        else if (mGame == eEchoes)
        {
            u32 FirstUnderscore = WorldName.IndexOf('_');
            u32 LastUnderscore = WorldName.LastIndexOf('_');

            if (FirstUnderscore != LastUnderscore && FirstUnderscore != -1 && LastUnderscore != -1)
            {
                WorldName = WorldName.ChopBack(WorldName.Size() - LastUnderscore);
                WorldName = WorldName.ChopFront(FirstUnderscore + 1);
                WorldName.Remove('_');
            }
        }

        // MP3 proto - Remove ! from the beginning and all text after last underscore
        else if (mGame == eCorruptionProto)
        {
            if (WorldName.StartsWith('!'))
                WorldName = WorldName.ChopFront(1);

            u32 LastUnderscore = WorldName.LastIndexOf('_');
            WorldName = WorldName.ChopBack(WorldName.Size() - LastUnderscore);
        }

        // MP3 - Remove text after last underscore
        else if (mGame == eCorruption)
        {
            u32 LastUnderscore = WorldName.LastIndexOf('_');

            if (LastUnderscore != -1)
                WorldName = WorldName.ChopBack(WorldName.Size() - LastUnderscore);
        }

        // DKCR - Remove text after second-to-last underscore
        else if (mGame == eReturns)
        {
            u32 Underscore = WorldName.LastIndexOf('_');
            WorldName = WorldName.ChopBack(WorldName.Size() - Underscore);
            Underscore = WorldName.LastIndexOf('_');
            WorldName = WorldName.ChopBack(WorldName.Size() - Underscore);
        }
    }

    return WorldName;
}
