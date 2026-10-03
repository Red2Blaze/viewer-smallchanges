/**
 * @file llfloaterfakelookat.cpp
 * @brief UI for the experimental Fake Look At Lab.
 */

#include "llviewerprecompiledheaders.h"

#include "llfloaterfakelookat.h"

#include "llfakelookat.h"

#include "llbutton.h"
#include "llcheckboxctrl.h"
#include "llcombobox.h"
#include "lllineeditor.h"
#include "llspinctrl.h"
#include "lltextbox.h"

#include <sstream>

LLFloaterFakeLookAt::LLFloaterFakeLookAt(const LLSD& key)
    : LLFloater(key)
{
}

bool LLFloaterFakeLookAt::postBuild()
{
    auto config_changed = [this](LLUICtrl*, const LLSD&)
    {
        applyControls(true);
    };

    getChild<LLComboBox>("mode")->setCommitCallback(config_changed);
    getChild<LLComboBox>("look_type")->setCommitCallback(config_changed);
    getChild<LLComboBox>("label_mode")->setCommitCallback(config_changed);
    getChild<LLLineEditor>("single_label")->setCommitCallback(config_changed);
    getChild<LLLineEditor>("label_list")->setCommitCallback(config_changed);

    for (const char* name : {"count", "min_distance", "max_distance", "refresh_seconds",
                             "vertical_scale", "object_jitter"})
    {
        getChild<LLSpinCtrl>(name)->setCommitCallback(config_changed);
    }

    for (const char* name : {"randomize_count", "current_region_only",
                             "root_objects_only", "exclude_attachments",
                             "bypass_distance_limit"})
    {
        getChild<LLCheckBoxCtrl>(name)->setCommitCallback(config_changed);
    }

    getChild<LLButton>("start")->setCommitCallback(
        [this](LLUICtrl*, const LLSD&) { onStart(); });
    getChild<LLButton>("stop")->setCommitCallback(
        [this](LLUICtrl*, const LLSD&) { onStop(); });
    getChild<LLButton>("refresh_now")->setCommitCallback(
        [this](LLUICtrl*, const LLSD&) { onRefreshNow(); });

    applyControls(false);
    refreshStatus();
    return true;
}

void LLFloaterFakeLookAt::draw()
{
    refreshStatus();
    LLFloater::draw();
}

void LLFloaterFakeLookAt::onClose(bool app_quitting)
{
    if (!app_quitting && getChild<LLCheckBoxCtrl>("stop_on_close")->getValue().asBoolean())
    {
        LookAtFaker::Manager::instance().stop();
    }
    LLFloater::onClose(app_quitting);
}

void LLFloaterFakeLookAt::applyControls(bool refresh_running)
{
    LookAtFaker::Config config;

    const std::string mode = getChild<LLComboBox>("mode")->getValue().asString();
    if (mode == "all_avatars")
        config.mode = LookAtFaker::Mode::AllAvatars;
    else if (mode == "random_avatars")
        config.mode = LookAtFaker::Mode::RandomAvatars;
    else if (mode == "random_objects")
        config.mode = LookAtFaker::Mode::RandomObjects;
    else if (mode == "mixed")
        config.mode = LookAtFaker::Mode::Mixed;
    else
        config.mode = LookAtFaker::Mode::RandomPositions;

    config.count = getChild<LLSpinCtrl>("count")->getValue().asInteger();
    config.minDistance = static_cast<F32>(getChild<LLSpinCtrl>("min_distance")->getValue().asReal());
    config.maxDistance = static_cast<F32>(getChild<LLSpinCtrl>("max_distance")->getValue().asReal());
    config.refreshSeconds = static_cast<F32>(getChild<LLSpinCtrl>("refresh_seconds")->getValue().asReal());
    config.verticalScale = static_cast<F32>(getChild<LLSpinCtrl>("vertical_scale")->getValue().asReal());
    config.objectJitter = static_cast<F32>(getChild<LLSpinCtrl>("object_jitter")->getValue().asReal());
    config.lookType = static_cast<ELookAtType>(
        getChild<LLComboBox>("look_type")->getValue().asInteger());

    const std::string label_mode = getChild<LLComboBox>("label_mode")->getValue().asString();
    if (label_mode == "single")
        config.labelMode = LookAtFaker::LabelMode::SingleCustomName;
    else if (label_mode == "random_list")
        config.labelMode = LookAtFaker::LabelMode::RandomFromList;
    else if (label_mode == "sequential_list")
        config.labelMode = LookAtFaker::LabelMode::SequentialFromList;
    else
        config.labelMode = LookAtFaker::LabelMode::ActualTargetName;

    config.singleLabel = getChild<LLLineEditor>("single_label")->getText();
    config.labelList = getChild<LLLineEditor>("label_list")->getText();

    config.randomizeCount = getChild<LLCheckBoxCtrl>("randomize_count")->getValue().asBoolean();
    config.currentRegionOnly = getChild<LLCheckBoxCtrl>("current_region_only")->getValue().asBoolean();
    config.rootObjectsOnly = getChild<LLCheckBoxCtrl>("root_objects_only")->getValue().asBoolean();
    config.excludeAttachments = getChild<LLCheckBoxCtrl>("exclude_attachments")->getValue().asBoolean();
    config.bypassDistanceLimit = getChild<LLCheckBoxCtrl>("bypass_distance_limit")->getValue().asBoolean();

    auto& manager = LookAtFaker::Manager::instance();
    const bool was_running = manager.isRunning();
    manager.setConfig(config);

    if (refresh_running && was_running)
    {
        manager.refreshNow();
    }
}

void LLFloaterFakeLookAt::onStart()
{
    applyControls(false);
    LookAtFaker::Manager::instance().start();
    refreshStatus();
}

void LLFloaterFakeLookAt::onStop()
{
    LookAtFaker::Manager::instance().stop();
    refreshStatus();
}

void LLFloaterFakeLookAt::onRefreshNow()
{
    applyControls(false);
    LookAtFaker::Manager::instance().refreshNow();
    refreshStatus();
}

void LLFloaterFakeLookAt::refreshStatus()
{
    auto& manager = LookAtFaker::Manager::instance();

    std::ostringstream status;
    status << (manager.isRunning() ? "RUNNING" : "Stopped")
           << " | active effects: " << manager.activeEffectCount()
           << " / " << LookAtFaker::Manager::MAX_EFFECTS
           << "\nCandidates in range: "
           << manager.lastAvatarCandidateCount() << " avatars, "
           << manager.lastObjectCandidateCount() << " world objects"
           << "\nLocal debug crosses/labels: "
           << (LLHUDEffectLookAt::sDebugLookAt ? "visible" : "hidden - enable Show Look At");

    getChild<LLTextBox>("status")->setText(status.str());
    getChild<LLButton>("stop")->setEnabled(manager.isRunning());
    getChild<LLButton>("refresh_now")->setEnabled(manager.isRunning());
    getChild<LLButton>("start")->setEnabled(!manager.isRunning());
}
