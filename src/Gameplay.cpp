#include "Gameplay.h"

namespace Gameplay {

    void ApplyNormalStagger(RE::Actor* aggressor, RE::Actor* defender) {
        if (!aggressor || !defender) {
            return;
        }

        aggressor->NotifyAnimationGraph("MCO_Recovery");
        aggressor->NotifyAnimationGraph("staggerStop");
        aggressor->NotifyAnimationGraph("recoilStop");
        aggressor->NotifyAnimationGraph("MCO_EndAnimation");
        aggressor->NotifyAnimationGraph("attackStop");
        aggressor->NotifyAnimationGraph("recoilStart");

        defender->NotifyAnimationGraph("blockStop");
        defender->NotifyAnimationGraph("Maxsu_BlockHitWinOpen");
    }

    void ApplyLargeStagger(RE::Actor* aggressor, RE::Actor* defender) {
        if (!aggressor || !defender) {
            return;
        }

        aggressor->NotifyAnimationGraph("MCO_Recovery");
        aggressor->NotifyAnimationGraph("staggerStop");
        aggressor->NotifyAnimationGraph("recoilStop");
        aggressor->NotifyAnimationGraph("MCO_EndAnimation");
        aggressor->NotifyAnimationGraph("attackStop");
        aggressor->NotifyAnimationGraph("recoilLargeStart");

        defender->NotifyAnimationGraph("blockStop");
        defender->NotifyAnimationGraph("Maxsu_BlockHitWinOpen");
    }

}