// Logo box demo effects: every effect draws something, clicks cycle through all of them.
#include "test.h"
#include "ui/demo_fx.h"

TEST(demo_fx_every_effect_draws) {
    static DemoFx fx;
    for (int e = 0; e < DemoFx::kCount; ++e) {
        fx.setEffect(e);
        for (double t : {0.0, 0.5, 3.0}) fx.render(t);
        int lit = 0;
        for (int i = 0; i < DemoFx::W * DemoFx::H; ++i) lit += (fx.pixels()[i] & 0xFFFFFF) > 0x202020 ? 1 : 0;
        CHECK(lit > 50);  // not an empty (black) box
    }
}

TEST(demo_fx_click_cycles_all_effects) {
    static DemoFx fx;
    fx.setEffect(0);
    for (int i = 1; i <= DemoFx::kCount; ++i) {
        fx.next();
        CHECK_EQ(fx.effect(), i % DemoFx::kCount);
    }
}
