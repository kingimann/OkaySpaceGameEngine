// Drives the engine's own engine/src/Optimize/MipBudget.cpp, compiled unchanged by okay-cc
// (std::priority_queue with a comparison class, lambdas calling a captured lambda, std::log2).
#include "okay/Optimize/MipBudget.hpp"

#include <vector>

extern "C" int printf(const char *fmt, ...);

static void show(const okay::optimize::MipPlan &plan) {
    printf("plan total %.1f desired %.1f full %.1f demotions %d over %d\n", plan.totalBytes, plan.desiredBytes,
           plan.fullBytes, plan.demotions, plan.overBudget);
    for (const okay::optimize::MipChoice &c : plan.choices)
        printf("  id %d desired %d mip %d %dx%d bytes %.1f demoted %d\n", c.id, c.desiredMip, c.mip, c.width, c.height,
               c.bytes, c.demotedForBudget);
}

int main() {
    printf("full %d %d %d\n", okay::optimize::FullMipCount(1024, 512), okay::optimize::FullMipCount(1, 1),
           okay::optimize::FullMipCount(0, 7));
    printf("chain %.1f %.1f\n", okay::optimize::MipChainBytes(256, 256, 4.0f, 0, 9),
           okay::optimize::MipChainBytes(64, 32, 0.5f, 2, 7));
    std::vector<okay::optimize::MipTexture> texs;
    for (int i = 0; i < 6; ++i) {
        okay::optimize::MipTexture t;
        t.id = 100 + i;
        t.width = 256 << (i % 3);
        t.height = 128 << (i % 2);
        t.screenPixels = (float)(40 * (i + 1));
        t.importance = 1.0f + (float)(i % 4) * 0.5f;
        t.uvScale = i == 2 ? 2.0f : 1.0f;
        t.pinned = i == 4;
        t.previousMip = i == 1 ? 2 : -1;
        texs.push_back(t);
    }
    okay::optimize::MipOptions opt;
    printf("desired %d %d %d\n", okay::optimize::DesiredMip(texs[0], opt), okay::optimize::DesiredMip(texs[1], opt),
           okay::optimize::DesiredMip(texs[2]));
    show(okay::optimize::PlanMips(texs));
    opt.budgetBytes = 600000.0f;
    opt.mipBias = 0.5f;
    show(okay::optimize::PlanMips(texs, opt));
    opt.budgetBytes = 1000.0f;
    show(okay::optimize::PlanMips(texs, opt));
    return 0;
}
