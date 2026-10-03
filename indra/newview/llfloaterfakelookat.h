/**
 * @file llfloaterfakelookat.h
 * @brief UI for the experimental Fake Look At Lab.
 */

#ifndef LL_LLFLOATERFAKELOOKAT_H
#define LL_LLFLOATERFAKELOOKAT_H

#include "llfloater.h"

class LLFloaterFakeLookAt final : public LLFloater
{
public:
    explicit LLFloaterFakeLookAt(const LLSD& key);

    bool postBuild() override;
    void draw() override;
    void onClose(bool app_quitting) override;

private:
    void applyControls(bool refresh_running);
    void refreshStatus();
    void onStart();
    void onStop();
    void onRefreshNow();
};

#endif // LL_LLFLOATERFAKELOOKAT_H
