/**
 * @file llfakelookat.h
 * @brief Experimental multi-target LookAt effect generator.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2026 Red2Blaze.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * $/LicenseInfo$
 */

#ifndef LL_LLFAKELOOKAT_H
#define LL_LLFAKELOOKAT_H

#include "lleventtimer.h"
#include "llframetimer.h"
#include "llhudeffectlookat.h"
#include "llpointer.h"
#include "v3math.h"

#include <string>
#include <vector>

class LLViewerObject;
class LLViewerRegion;

namespace LookAtFaker
{
    enum class Mode : S32
    {
        RandomPositions = 0,
        AllAvatars,
        RandomAvatars,
        RandomObjects,
        Mixed
    };

    enum class LabelMode : S32
    {
        ActualTargetName = 0,
        SingleCustomName,
        RandomFromList,
        SequentialFromList
    };

    struct Config
    {
        Mode mode = Mode::RandomPositions;
        S32 count = 8;
        F32 minDistance = 2.f;
        F32 maxDistance = 25.f;
        F32 refreshSeconds = 3.f;
        F32 verticalScale = 0.35f;
        F32 objectJitter = 0.25f;
        ELookAtType lookType = LOOKAT_TARGET_FOCUS;
        bool randomizeCount = false;
        bool currentRegionOnly = true;
        bool rootObjectsOnly = true;
        bool excludeAttachments = true;
        bool bypassDistanceLimit = true;
        LabelMode labelMode = LabelMode::ActualTargetName;
        std::string singleLabel;
        std::string labelList;
    };

    class Manager final : public LLEventTimer
    {
    public:
        static Manager& instance();

        void setConfig(const Config& config);
        const Config& config() const { return mConfig; }

        void start();
        void stop();
        void refreshNow();

        bool isRunning() const { return mRunning; }
        S32 activeEffectCount() const;
        S32 lastAvatarCandidateCount() const { return mLastAvatarCandidates; }
        S32 lastObjectCandidateCount() const { return mLastObjectCandidates; }

        static constexpr S32 MAX_EFFECTS = 64;

    private:
        Manager();
        bool tick() override;

        struct Target
        {
            LLPointer<LLViewerObject> object;
            LLVector3 position;
        };

        struct RetiringEffect
        {
            LLPointer<LLHUDEffectLookAt> effect;
            S32 ticksRemaining = 0;
        };

        void rebuild();
        void clearActiveEffects();
        void serviceRetiringEffects();
        void ensureEffectCount(S32 count);
        void trimEffectCount(S32 count);

        std::vector<LLPointer<LLViewerObject>> collectTargets(bool avatars);
        std::vector<LLPointer<LLViewerObject>> takeRandom(
            std::vector<LLPointer<LLViewerObject>> candidates, S32 count) const;
        std::vector<Target> buildTargets();
        Target makeRandomPositionTarget() const;
        LLVector3 makeObjectOffset(bool avatar) const;
        S32 resolveRequestedCount(S32 available = MAX_EFFECTS) const;
        std::vector<std::string> parseLabelList() const;
        std::string labelForTarget(const Target& target, S32 effect_index);

        Config mConfig;
        bool mRunning = false;
        LLFrameTimer mRefreshTimer;
        LLViewerRegion* mLastRegion = nullptr;
        S32 mLastAvatarCandidates = 0;
        S32 mLastObjectCandidates = 0;
        S32 mSequentialLabelIndex = 0;

        std::vector<LLPointer<LLHUDEffectLookAt>> mActiveEffects;
        std::vector<RetiringEffect> mRetiringEffects;
    };
}

#endif // LL_LLFAKELOOKAT_H
