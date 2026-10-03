/**
 * @file llfakelookat.cpp
 * @brief Experimental multi-target LookAt effect generator.
 */

#include "llviewerprecompiledheaders.h"

#include "llfakelookat.h"

#include "llagent.h"
#include "llavatarnamecache.h"
#include "llhudmanager.h"
#include "llhudobject.h"
#include "llmath.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"
#include "llviewerregion.h"
#include "llvoavatarself.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace
{
    constexpr F32 TWO_PI_F = 6.28318530717958647692f;
    constexpr F32 MIN_REFRESH_SECONDS = 0.5f;

    F32 clampDistance(F32 value)
    {
        return llclamp(value, 0.f, 4096.f);
    }

    std::string trimCopy(std::string value)
    {
        auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
        value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
        return value;
    }
}

namespace LookAtFaker
{
    Manager& Manager::instance()
    {
        static Manager manager;
        return manager;
    }

    Manager::Manager()
        : LLEventTimer(0.1f)
    {
    }

    void Manager::setConfig(const Config& incoming)
    {
        Config config = incoming;
        config.count = llclamp(config.count, 0, MAX_EFFECTS);
        config.minDistance = clampDistance(config.minDistance);
        config.maxDistance = clampDistance(config.maxDistance);
        if (config.maxDistance < config.minDistance)
        {
            std::swap(config.minDistance, config.maxDistance);
        }
        config.refreshSeconds = llmax(MIN_REFRESH_SECONDS, config.refreshSeconds);
        config.verticalScale = llclamp(config.verticalScale, 0.f, 1.f);
        config.objectJitter = llclamp(config.objectJitter, 0.f, 64.f);

        const bool look_type_changed = config.lookType != mConfig.lookType;
        mConfig = config;

        // Attention priorities can prevent reusing an existing high-priority
        // effect as a lower-priority type, so rebuild the pool when needed.
        if (look_type_changed && !mActiveEffects.empty())
        {
            clearActiveEffects();
        }

        if (mRunning)
        {
            rebuild();
            mRefreshTimer.reset();
        }
    }

    void Manager::start()
    {
        mRunning = true;
        mLastRegion = gAgent.getRegion();
        rebuild();
        mRefreshTimer.reset();
    }

    void Manager::stop()
    {
        mRunning = false;
        clearActiveEffects();
        mLastAvatarCandidates = 0;
        mLastObjectCandidates = 0;
    }

    void Manager::refreshNow()
    {
        if (!mRunning)
        {
            return;
        }

        rebuild();
        mRefreshTimer.reset();
    }

    bool Manager::tick()
    {
        serviceRetiringEffects();

        if (!mRunning)
        {
            return false;
        }

        LLViewerRegion* region = gAgent.getRegion();
        if (region != mLastRegion)
        {
            mLastRegion = region;
            rebuild();
            mRefreshTimer.reset();
            return false;
        }

        if (mRefreshTimer.getElapsedTimeF32() >= mConfig.refreshSeconds)
        {
            rebuild();
            mRefreshTimer.reset();
        }
        else if (!mActiveEffects.empty() && activeEffectCount() == 0)
        {
            // Short-lived LookAt types can expire before the selected refresh.
            rebuild();
            mRefreshTimer.reset();
        }

        return false;
    }

    S32 Manager::activeEffectCount() const
    {
        S32 count = 0;
        for (const auto& effect : mActiveEffects)
        {
            if (effect.notNull() && !effect->isDead())
            {
                ++count;
            }
        }
        return count;
    }

    void Manager::serviceRetiringEffects()
    {
        for (auto it = mRetiringEffects.begin(); it != mRetiringEffects.end();)
        {
            if (it->effect.isNull() || it->effect->isDead())
            {
                it = mRetiringEffects.erase(it);
                continue;
            }

            if (--it->ticksRemaining <= 0)
            {
                it->effect->markDead();
                it = mRetiringEffects.erase(it);
                continue;
            }

            ++it;
        }
    }

    void Manager::clearActiveEffects()
    {
        for (auto& effect : mActiveEffects)
        {
            if (effect.isNull() || effect->isDead())
            {
                continue;
            }

            // Allow the normal ViewerEffect path a few timer ticks to send
            // the clear before the local HUD object is retired.
            effect->setLookAt(LOOKAT_TARGET_CLEAR, nullptr, LLVector3::zero);
            mRetiringEffects.push_back({effect, 4});
        }
        mActiveEffects.clear();
    }

    void Manager::ensureEffectCount(S32 count)
    {
        if (!isAgentAvatarValid())
        {
            return;
        }

        while (static_cast<S32>(mActiveEffects.size()) < count)
        {
            auto* effect = static_cast<LLHUDEffectLookAt*>(
                LLHUDManager::getInstance()->createViewerEffect(LLHUDObject::LL_HUD_EFFECT_LOOKAT));
            if (!effect)
            {
                break;
            }

            effect->setSourceObject(gAgentAvatarp);
            effect->setBypassDistanceLimit(mConfig.bypassDistanceLimit);
            mActiveEffects.emplace_back(effect);
        }
    }

    void Manager::trimEffectCount(S32 count)
    {
        while (static_cast<S32>(mActiveEffects.size()) > count)
        {
            auto effect = mActiveEffects.back();
            mActiveEffects.pop_back();

            if (effect.notNull() && !effect->isDead())
            {
                effect->setLookAt(LOOKAT_TARGET_CLEAR, nullptr, LLVector3::zero);
                mRetiringEffects.push_back({effect, 4});
            }
        }
    }

    std::vector<LLPointer<LLViewerObject>> Manager::collectTargets(bool avatars)
    {
        std::vector<LLPointer<LLViewerObject>> result;
        const LLVector3d agent_pos = gAgent.getPositionGlobal();
        LLViewerRegion* current_region = gAgent.getRegion();

        const S32 object_count = gObjectList.getNumObjects();
        result.reserve(avatars ? 32 : llmin(object_count, 512));

        for (S32 i = 0; i < object_count; ++i)
        {
            LLViewerObject* object = gObjectList.getObject(i);
            if (!object || object->isDead() || object == gAgentAvatarp)
            {
                continue;
            }

            if (object->isAvatar() != avatars)
            {
                continue;
            }

            if (mConfig.currentRegionOnly && current_region && object->getRegion() != current_region)
            {
                continue;
            }

            if (!avatars)
            {
                if (mConfig.excludeAttachments && object->isAttachment())
                {
                    continue;
                }

                if (mConfig.rootObjectsOnly && object->getRootEdit() != object)
                {
                    continue;
                }
            }

            const F64 distance = (object->getPositionGlobal() - agent_pos).magVec();
            if (distance < mConfig.minDistance || distance > mConfig.maxDistance)
            {
                continue;
            }

            result.emplace_back(object);
        }

        return result;
    }

    std::vector<LLPointer<LLViewerObject>> Manager::takeRandom(
        std::vector<LLPointer<LLViewerObject>> candidates, S32 count) const
    {
        std::vector<LLPointer<LLViewerObject>> result;
        count = llclamp(count, 0, llmin(static_cast<S32>(candidates.size()), MAX_EFFECTS));
        result.reserve(count);

        while (count-- > 0 && !candidates.empty())
        {
            const S32 index = ll_rand(static_cast<S32>(candidates.size()));
            result.push_back(candidates[index]);
            candidates[index] = candidates.back();
            candidates.pop_back();
        }

        return result;
    }

    S32 Manager::resolveRequestedCount(S32 available) const
    {
        const S32 capped_available = llclamp(available, 0, MAX_EFFECTS);

        if (mConfig.mode == Mode::AllAvatars && mConfig.count == 0)
        {
            return capped_available;
        }

        S32 requested = llclamp(mConfig.count, 1, MAX_EFFECTS);
        requested = llmin(requested, capped_available);

        if (mConfig.randomizeCount && requested > 1)
        {
            requested = 1 + ll_rand(requested);
        }

        return requested;
    }

    std::vector<std::string> Manager::parseLabelList() const
    {
        std::vector<std::string> labels;
        std::string current;

        auto flush = [&]()
        {
            std::string trimmed = trimCopy(current);
            if (!trimmed.empty())
            {
                labels.push_back(std::move(trimmed));
            }
            current.clear();
        };

        for (char ch : mConfig.labelList)
        {
            if (ch == ',' || ch == ';' || ch == '\n' || ch == '\r')
            {
                flush();
            }
            else
            {
                current.push_back(ch);
            }
        }
        flush();
        return labels;
    }

    std::string Manager::labelForTarget(const Target& target, S32 effect_index)
    {
        switch (mConfig.labelMode)
        {
            case LabelMode::SingleCustomName:
                return trimCopy(mConfig.singleLabel);

            case LabelMode::RandomFromList:
            {
                auto labels = parseLabelList();
                return labels.empty() ? std::string() : labels[ll_rand(static_cast<S32>(labels.size()))];
            }

            case LabelMode::SequentialFromList:
            {
                auto labels = parseLabelList();
                if (labels.empty())
                {
                    return {};
                }

                const S32 index =
                    (mSequentialLabelIndex + effect_index) % static_cast<S32>(labels.size());
                return labels[index];
            }

            case LabelMode::ActualTargetName:
            default:
            {
                if (target.object.notNull() && target.object->isAvatar())
                {
                    LLAvatarName avatar_name;
                    if (LLAvatarNameCache::get(target.object->getID(), &avatar_name))
                    {
                        return avatar_name.getCompleteName();
                    }
                }
                return {};
            }
        }
    }

    Manager::Target Manager::makeRandomPositionTarget() const
    {
        const F32 min_distance = mConfig.minDistance;
        const F32 max_distance = llmax(min_distance, mConfig.maxDistance);
        const F32 distance = min_distance + ll_frand(max_distance - min_distance);
        const F32 angle = ll_frand(TWO_PI_F);

        const F32 z_limit = distance * mConfig.verticalScale;
        const F32 z = z_limit > 0.f ? ll_frand(z_limit * 2.f) - z_limit : 0.f;
        const F32 horizontal = sqrtf(llmax(0.f, distance * distance - z * z));

        Target target;
        target.position = gAgent.getPositionAgent() +
            LLVector3(cosf(angle) * horizontal, sinf(angle) * horizontal, z);
        return target;
    }

    LLVector3 Manager::makeObjectOffset(bool avatar) const
    {
        if (avatar || mConfig.objectJitter <= 0.f)
        {
            return LLVector3::zero;
        }

        const F32 r = mConfig.objectJitter;
        return LLVector3(
            ll_frand(r * 2.f) - r,
            ll_frand(r * 2.f) - r,
            ll_frand(r * 2.f) - r);
    }

    std::vector<Manager::Target> Manager::buildTargets()
    {
        std::vector<Target> targets;
        auto avatars = collectTargets(true);
        auto objects = collectTargets(false);

        mLastAvatarCandidates = static_cast<S32>(avatars.size());
        mLastObjectCandidates = static_cast<S32>(objects.size());

        if (mConfig.mode == Mode::RandomPositions)
        {
            const S32 count = resolveRequestedCount(MAX_EFFECTS);
            targets.reserve(count);
            for (S32 i = 0; i < count; ++i)
            {
                targets.push_back(makeRandomPositionTarget());
            }
            return targets;
        }

        if (mConfig.mode == Mode::AllAvatars || mConfig.mode == Mode::RandomAvatars)
        {
            const S32 count = resolveRequestedCount(static_cast<S32>(avatars.size()));
            auto selected = takeRandom(std::move(avatars), count);
            targets.reserve(selected.size());
            for (auto& object : selected)
            {
                targets.push_back({object, makeObjectOffset(true)});
            }
            return targets;
        }

        if (mConfig.mode == Mode::RandomObjects)
        {
            const S32 count = resolveRequestedCount(static_cast<S32>(objects.size()));
            auto selected = takeRandom(std::move(objects), count);
            targets.reserve(selected.size());
            for (auto& object : selected)
            {
                targets.push_back({object, makeObjectOffset(false)});
            }
            return targets;
        }

        std::vector<LLPointer<LLViewerObject>> mixed;
        mixed.reserve(avatars.size() + objects.size());
        mixed.insert(mixed.end(), avatars.begin(), avatars.end());
        mixed.insert(mixed.end(), objects.begin(), objects.end());

        const S32 requested = resolveRequestedCount(MAX_EFFECTS);
        auto selected = takeRandom(std::move(mixed), requested);
        targets.reserve(requested);

        for (auto& object : selected)
        {
            const bool avatar = object.notNull() && object->isAvatar();
            targets.push_back({object, makeObjectOffset(avatar)});
        }

        while (static_cast<S32>(targets.size()) < requested)
        {
            targets.push_back(makeRandomPositionTarget());
        }

        return targets;
    }

    void Manager::rebuild()
    {
        if (!mRunning || !isAgentAvatarValid())
        {
            return;
        }

        auto targets = buildTargets();
        const S32 target_count = llmin(static_cast<S32>(targets.size()), MAX_EFFECTS);

        trimEffectCount(target_count);
        ensureEffectCount(target_count);

        for (S32 i = 0; i < target_count && i < static_cast<S32>(mActiveEffects.size()); ++i)
        {
            auto& effect = mActiveEffects[i];
            if (effect.isNull() || effect->isDead())
            {
                if (effect.notNull())
                {
                    effect->markDead();
                }

                auto* replacement = static_cast<LLHUDEffectLookAt*>(
                    LLHUDManager::getInstance()->createViewerEffect(LLHUDObject::LL_HUD_EFFECT_LOOKAT));
                if (!replacement)
                {
                    continue;
                }

                replacement->setSourceObject(gAgentAvatarp);
                effect = replacement;
            }

            effect->setBypassDistanceLimit(mConfig.bypassDistanceLimit);
            effect->setDebugLabelOverride(labelForTarget(targets[i], i));
            effect->setLookAt(mConfig.lookType, targets[i].object, targets[i].position);
        }

        if (mConfig.labelMode == LabelMode::SequentialFromList)
        {
            const auto labels = parseLabelList();
            if (!labels.empty())
            {
                mSequentialLabelIndex =
                    (mSequentialLabelIndex + target_count) % static_cast<S32>(labels.size());
            }
        }
    }
}
