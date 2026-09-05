schema 2
shader "fixture.numeric-unlit" {
    properties {
        color tint = [1, 0, 0, 1]
    }
    pass "Forward" { fragment fragmentMain }
    slang {
        float4 fragmentMain() : SV_Target0 {
            return Material.tint;
        }
    }
}
