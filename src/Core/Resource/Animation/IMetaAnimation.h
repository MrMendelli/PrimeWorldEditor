#ifndef IMETAANIMATION
#define IMETAANIMATION

#include "CAnimation.h"
#include "Core/Resource/TResPtr.h"
#include <Common/TString.h>

enum EMetaAnimationType
{
    eMAT_Play = 0,
    eMAT_Blend = 1,
    eMAT_PhaseBlend = 2, // note: structure shared with eMAT_Blend
    eMAT_Random = 3,
    eMAT_Sequence = 4
};

// Factory class
class CMetaAnimFactory
{
public:
    class IMetaAnimation* LoadFromStream(IInputStream& rInput);
};
extern CMetaAnimFactory gMetaAnimFactory;

// Animation primitive class
class CAnimPrimitive
{
    TResPtr<CAnimation> mpAnim;
    u32 mID;
    TString mName;

public:
    CAnimPrimitive() : mID(0) {}

    CAnimPrimitive(IInputStream& rInput)
    {
        mpAnim = gpResourceStore->LoadResource(rInput.ReadLong(), "ANIM");
        mID = rInput.ReadLong();
        mName = rInput.ReadString();
    }

    inline bool operator==(const CAnimPrimitive& rkRight) const { return mID == rkRight.mID; }
    inline bool operator< (const CAnimPrimitive& rkRight) const { return mID < rkRight.mID; }

    // Accessors
    CAnimation* Animation() const   { return mpAnim; }
    u32 ID() const                  { return mID; }
    TString Name() const            { return mName; }
};

// Base MetaAnimation interface
class IMetaAnimation
{
public:
    IMetaAnimation() {}
    virtual ~IMetaAnimation() {}
    virtual EMetaAnimationType Type() const = 0;
    virtual void GetUniquePrimitives(std::set<CAnimPrimitive>& rPrimSet) const = 0;

    // Static
    static IMetaAnimation* LoadFromStream(IInputStream& rInput);
};

// CMetaAnimPlay - plays an animation
class CMetaAnimPlay : public IMetaAnimation
{
protected:
    CAnimPrimitive mPrimitive;
    float mUnknownA;
    u32 mUnknownB;

public:
    CMetaAnimPlay(IInputStream& rInput);
    virtual EMetaAnimationType Type() const;
    virtual void GetUniquePrimitives(std::set<CAnimPrimitive>& rPrimSet) const;

    // Accessors
    inline CAnimPrimitive Primitive() const { return mPrimitive; }
    inline float UnknownA() const           { return mUnknownA; }
    inline u32 UnknownB() const             { return mUnknownB; }
};

// CMetaAnimBlend - blend between two animations
class CMetaAnimBlend : public IMetaAnimation
{
protected:
    EMetaAnimationType mType;
    IMetaAnimation *mpMetaAnimA;
    IMetaAnimation *mpMetaAnimB;
    float mUnknownA;
    bool mUnknownB;

public:
    CMetaAnimBlend(EMetaAnimationType Type, IInputStream& rInput);
    ~CMetaAnimBlend();
    virtual EMetaAnimationType Type() const;
    virtual void GetUniquePrimitives(std::set<CAnimPrimitive>& rPrimSet) const;

    // Accessors
    inline IMetaAnimation* BlendAnimationA() const  { return mpMetaAnimA; }
    inline IMetaAnimation* BlendAnimationB() const  { return mpMetaAnimB; }
    inline float UnknownA() const                   { return mUnknownA; }
    inline bool UnknownB() const                    { return mUnknownB; }
};

// SAnimProbabilityPair - structure used by CMetaAnimationRandom to associate an animation with a probability value
struct SAnimProbabilityPair
{
    IMetaAnimation *pAnim;
    u32 Probability;
};

// CMetaAnimRandom - play random animation
class CMetaAnimRandom : public IMetaAnimation
{
protected:
    std::vector<SAnimProbabilityPair> mProbabilityPairs;

public:
    CMetaAnimRandom(IInputStream& rInput);
    ~CMetaAnimRandom();
    virtual EMetaAnimationType Type() const;
    virtual void GetUniquePrimitives(std::set<CAnimPrimitive>& rPrimSet) const;
};

// CMetaAnim - play a series of animations in sequence
class CMetaAnimSequence : public IMetaAnimation
{
protected:
    std::vector<IMetaAnimation*> mAnimations;

public:
    CMetaAnimSequence(IInputStream& rInput);
    ~CMetaAnimSequence();
    virtual EMetaAnimationType Type() const;
    virtual void GetUniquePrimitives(std::set<CAnimPrimitive>& rPrimSet) const;
};

#endif // IMETAANIMATION
