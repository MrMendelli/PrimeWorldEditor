#ifndef CASSETNAMEMAP
#define CASSETNAMEMAP

#include "CResourceIterator.h"
#include "CResourceStore.h"
#include <Common/CAssetID.h>
#include <Common/Serialization/XML.h>
#include <map>
#include <memory>

const TString gkAssetMapPath = "..\\resources\\gameinfo\\AssetNameMap.xml";
const TString gkAssetMapExt = "xml";

class CAssetNameMap
{
    struct SAssetNameInfo
    {
        TWideString Name;
        TWideString Directory;
        CFourCC Type; // This is mostly just needed to verify no name conflicts

        bool operator<(const SAssetNameInfo& rkOther) const
        {
            return FullPath() < rkOther.FullPath();
        }

        TWideString FullPath() const
        {
            return Directory + Name + L'.' + Type.ToString().ToUTF16();
        }

        void Serialize(IArchive& rArc)
        {
            rArc << SERIAL_AUTO(Name) << SERIAL_AUTO(Directory) << SERIAL_AUTO(Type);
        }
    };

    std::set<SAssetNameInfo> mUsedSet; // Used to prevent name conflicts
    std::map<CAssetID, SAssetNameInfo> mMap;
    bool mIsValid;

    // Private Methods
    void Serialize(IArchive& rArc);
    void PostLoadValidate();

public:
    CAssetNameMap() : mIsValid(true) {}
    void LoadAssetNames(TString Path = gkAssetMapPath);
    void SaveAssetNames(TString Path = gkAssetMapPath);
    bool GetNameInfo(CAssetID ID, TString& rOutDirectory, TString& rOutName);
    void CopyFromStore(CResourceStore *pStore);

    inline bool IsValid() const                 { return mIsValid; }
    inline static TString DefaultNameMapPath()  { return gkAssetMapPath; }
    inline static TString GetExtension()        { return gkAssetMapExt; }
};

#endif // CASSETNAMEMAP

