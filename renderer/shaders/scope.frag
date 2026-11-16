#version 450
layout(set = 2, binding = 0) uniform sampler2D image;
layout(set = 3, binding = 0, std140) uniform Parameters { vec4 options; } params;
layout(location = 0) in vec2 texcoord;
layout(location = 1) in vec4 foreground;
layout(location = 2) in vec4 backdrop;
layout(location = 0) out vec4 outputColor;
// STARS masks preserved from REDS renderer/shaders/stipple.frag (VICE masks).
const uint lightPattern[32] = uint[32](
    0x00000000u, 0x00000000u, 0x000C0000u, 0x000C0000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000300u,
    0x00000300u, 0x00000000u, 0x00000000u, 0x01800000u,
    0x01800000u, 0x00000000u, 0x00000000u, 0x00030000u,
    0x00030000u, 0x0000000Cu, 0x0000000Cu, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00C00000u, 0x00C00000u,
    0x00000000u, 0x00003000u, 0x00003000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0xC0000000u, 0xC0000000u
);

const uint densePattern[32] = uint[32](
    0x00000000u, 0x00000000u, 0x08000800u, 0x08000800u,
    0x00180018u, 0x40004000u, 0x40004000u, 0x01800180u,
    0x00000000u, 0x00030003u, 0x00000000u, 0x18001800u,
    0x00000000u, 0x00200020u, 0x00200020u, 0xC000C000u,
    0x00000000u, 0x00000000u, 0x08000800u, 0x08000800u,
    0x00180018u, 0x40004000u, 0x40004000u, 0x01800180u,
    0x00000000u, 0x00030003u, 0x00000000u, 0x18001800u,
    0x00000000u, 0x00200020u, 0x00200020u, 0xC000C000u
);

// Retained REDS black-weather-stipple prototype from the earlier new-palette
// implementation. The current three-color STARS presentation uses the legacy
// light/sparse mask instead, following the later 2023 operational-usability
// assessment. Keep this mask available for reference/comparison. gl_FragCoord's
// Y axis makes the row order appear vertically flipped on screen, so the source
// mask uses top-right + bottom-left 4x4 blocks to display visually as top-left +
// bottom-right in each repeating 32x32 tile.
const uint faaHFSTD010APattern[32] = uint[32](
    0x0000000Fu, 0x0000000Fu, 0x0000000Fu, 0x0000000Fu,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0xF0000000u, 0xF0000000u, 0xF0000000u, 0xF0000000u
);

// Exact restrictionAreaStipple bitmap from VICE STARS (OpenGL polygon stipple).
const uint restrictionAreaPattern[32] = uint[32](
    0x88888888u, 0x00000000u, 0x22222222u, 0x00000000u,
    0x88888888u, 0x00000000u, 0x22222222u, 0x00000000u,
    0x88888888u, 0x00000000u, 0x22222222u, 0x00000000u,
    0x88888888u, 0x00000000u, 0x22222222u, 0x00000000u,
    0x88888888u, 0x00000000u, 0x22222222u, 0x00000000u,
    0x88888888u, 0x00000000u, 0x22222222u, 0x00000000u,
    0x88888888u, 0x00000000u, 0x22222222u, 0x00000000u,
    0x88888888u, 0x00000000u, 0x22222222u, 0x00000000u
);

// Exact restrictionAreaHighDPIStipple bitmap from VICE STARS (OpenGL polygon stipple).
const uint restrictionAreaHighDPIPattern[32] = uint[32](
    0xC0C0C0C0u, 0xC0C0C0C0u, 0x00000000u, 0x00000000u,
    0x0C0C0C0Cu, 0x0C0C0C0Cu, 0x00000000u, 0x00000000u,
    0xC0C0C0C0u, 0xC0C0C0C0u, 0x00000000u, 0x00000000u,
    0x0C0C0C0Cu, 0x0C0C0C0Cu, 0x00000000u, 0x00000000u,
    0xC0C0C0C0u, 0xC0C0C0C0u, 0x00000000u, 0x00000000u,
    0x0C0C0C0Cu, 0x0C0C0C0Cu, 0x00000000u, 0x00000000u,
    0xC0C0C0C0u, 0xC0C0C0C0u, 0x00000000u, 0x00000000u,
    0x0C0C0C0Cu, 0x0C0C0C0Cu, 0x00000000u, 0x00000000u
);

void main() {
    int mode = int(params.options.x);
    if (mode >= 3) {
        int x = int(floor(gl_FragCoord.x)) & 31;
        int y = int(floor(params.options.y - gl_FragCoord.y)) & 31;
        uint row = mode == 3 ? lightPattern[y] : mode == 4 ? densePattern[y] :
                   mode == 5 ? faaHFSTD010APattern[y] : mode == 6 ? restrictionAreaPattern[y] : restrictionAreaHighDPIPattern[y];
        if ((row & (1u << uint(31 - x))) == 0u) discard;
    }
    if (mode == 1) outputColor = texture(image, texcoord) * foreground;
    else if (mode == 2) {
        float alpha = texture(image, texcoord).r;
        // Preserve REDS' bitmap compositing, including its alpha calculation.
        outputColor = mix(backdrop, vec4(foreground.rgb, alpha), alpha);
    } else outputColor = foreground;
}
