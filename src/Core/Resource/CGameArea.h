#ifndef CGAMEAREA_H
#define CGAMEAREA_H

#include "CResource.h"
#include "CCollisionMeshGroup.h"
#include "CLight.h"
#include "CMaterialSet.h"
#include "Core/Resource/Model/CModel.h"
#include "Core/Resource/Model/CStaticModel.h"
#include <Common/types.h>
#include <Math/CTransform4f.h>

#include <unordered_map>

class CScriptLayer;
class CScriptObject;

class CGameArea : public CResource
{
    DECLARE_RESOURCE_TYPE(eArea)
    friend class CAreaLoader;

    EGame mVersion;
    u32 mVertexCount;
    u32 mTriangleCount;
    bool mTerrainMerged;
    CTransform4f mTransform;
    CAABox mAABox;

    // Geometry
    CMaterialSet *mMaterialSet;
    std::vector<CModel*> mTerrainModels; // TerrainModels is the original version of each model; this is used by the editor (bounding box checks, material editing, etc)
    std::vector<CStaticModel*> mStaticTerrainModels; // StaticTerrainModels is the merged terrain for faster rendering in the world editor
    // Script
    std::vector<CScriptLayer*> mScriptLayers;
    CScriptLayer *mpGeneratorLayer;
    std::unordered_map<u32, CScriptObject*> mObjectMap;

    // Collision
    CCollisionMeshGroup *mCollision;
    // Lights
    std::vector<std::vector<CLight*>> mLightLayers;

public:
    CGameArea();
    ~CGameArea();

    void AddWorldModel(CModel *mdl);
    void MergeTerrain();
    void ClearTerrain();
    void ClearScriptLayers();

    // Getters
    EGame Version();
    CTransform4f GetTransform();
    u32 GetTerrainModelCount();
    u32 GetStaticModelCount();
    CModel* GetTerrainModel(u32 mdl);
    CStaticModel* GetStaticModel(u32 mdl);
    CCollisionMeshGroup* GetCollision();
    u32 GetScriptLayerCount();
    CScriptLayer* GetScriptLayer(u32 index);
    CScriptLayer* GetGeneratorLayer();
    CScriptObject* GetInstanceByID(u32 InstanceID);
    u32 GetLightLayerCount();
    u32 GetLightCount(u32 layer);
    CLight* GetLight(u32 layer, u32 light);
    CAABox AABox();
};

#endif // CGAMEAREA_H