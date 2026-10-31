/*
    Bismuth sprite processing kernel.

    This is the single source of truth for how a sprite's on-screen position,
    transform and color are computed from the game state. It is compiled into:

      - the culling compute shader (GLSL 4.30, SPIR-V, Metal via bgfx shaderc)
      - the multithreaded CPU culling fallback (C++ with glm, included inside a
        class body, see src/renderer/CpuCuller.cpp)

    To stay valid in all of those languages, code here must:
      - not use multi-component swizzles (glm doesn't enable them), only .x .y .z .w
      - not use `inout`/`out` parameters or global variables
      - suffix float literals with `f`
      - define functions before using them

    The includer must define, before including this file:
      uint  bkColorChannel(uint id)      packed RGBA8 color of a color channel
      uvec2 bkPulseEntry(uint index)     (flags, colorOrHSV) of a pulse entry
*/

#include "bismuth_shared.sh"

#define BK_PI 3.1415926535897932384626433832795f

struct BkGameState
{
    float timer;
    float audioScale;
    vec2  cameraViewSize;
    vec2  cameraPosition;
    vec2  cameraRight;
    vec2  cameraUp;
    vec2  cameraSize;
    vec2  winSize;
    float screenRight;
    float cameraUnzoomedX;
    vec3  specialLightBGColor;
    uint  flags;
};

struct BkObjectInfo
{
    float rotationSpeed;
    uint  flags;
    float audioScaleMin;
    float audioScaleMax;
    float fadeMargin;
    float opacity;
    uint  baseColorChannel;
    uint  baseHSV;
    uint  detailColorChannel;
    uint  detailHSV;
};

// 2x2 matrices are stored as vec4 in column major order: (c0.x, c0.y, c1.x, c1.y).
struct BkGroupState
{
    vec4  positionalTransform;
    vec4  localTransform;
    vec2  offset;
    float opacity;
    uint  pulseIndex;
};

struct BkSpriteResult
{
    bool  visible;
    vec4  localTransform;
    vec4  color;
};

struct BkHSV
{
    float hue;
    float sat;
    float val;
    bool  satAdd;
    bool  valAdd;
};

//// DECODING ////

BkObjectInfo bkDecodeObjectInfo(uvec4 a, uvec4 b, uvec4 c)
{
    BkObjectInfo o;
    o.rotationSpeed      = uintBitsToFloat(a.x);
    o.flags              = a.y;
    o.audioScaleMin      = uintBitsToFloat(a.z);
    o.audioScaleMax      = uintBitsToFloat(a.w);
    o.fadeMargin         = uintBitsToFloat(b.x);
    o.opacity            = uintBitsToFloat(b.y);
    o.baseColorChannel   = b.z;
    o.baseHSV            = b.w;
    o.detailColorChannel = c.x;
    o.detailHSV          = c.y;
    return o;
}

BkGroupState bkDecodeGroupState(uvec4 a, uvec4 b, uvec4 c)
{
    BkGroupState g;
    g.positionalTransform = vec4(uintBitsToFloat(a.x), uintBitsToFloat(a.y), uintBitsToFloat(a.z), uintBitsToFloat(a.w));
    g.localTransform      = vec4(uintBitsToFloat(b.x), uintBitsToFloat(b.y), uintBitsToFloat(b.z), uintBitsToFloat(b.w));
    g.offset              = vec2(uintBitsToFloat(c.x), uintBitsToFloat(c.y));
    g.opacity             = uintBitsToFloat(c.z);
    g.pulseIndex          = c.w;
    return g;
}

vec4 bkUnpackColor(uint c)
{
    return vec4(
        float( c         & 0xffu),
        float((c >> 8u)  & 0xffu),
        float((c >> 16u) & 0xffu),
        float((c >> 24u) & 0xffu)
    ) / 255.0f;
}

uint bkPackColor(vec4 color)
{
    vec4 c = clamp(color, 0.0f, 1.0f) * 255.0f + 0.5f;
    return  uint(c.x)
         | (uint(c.y) << 8u)
         | (uint(c.z) << 16u)
         | (uint(c.w) << 24u);
}

vec4 bkColorChannelColor(uint channel)
{
    return bkUnpackColor(bkColorChannel(channel));
}

//// 2x2 MATRICES ////

vec2 bkMul(vec4 m, vec2 v)
{
    return vec2(m.x * v.x + m.z * v.y, m.y * v.x + m.w * v.y);
}

vec4 bkMatMul(vec4 a, vec4 b)
{
    vec2 c0 = bkMul(a, vec2(b.x, b.y));
    vec2 c1 = bkMul(a, vec2(b.z, b.w));
    return vec4(c0.x, c0.y, c1.x, c1.y);
}

vec4 bkRotationMatrix(float angleInRadians)
{
    float s = sin(angleInRadians);
    float c = cos(angleInRadians);
    return vec4(c, s, -s, c);
}

vec4 bkScaleMatrix(float scale)
{
    return vec4(scale, 0.0f, 0.0f, scale);
}

//// HSV (credits to Sam Hocevar for rgb2hsv / hsv2rgb) ////

vec3 bkRgb2Hsv(vec3 c)
{
    vec4 K = vec4(0.0f, -1.0f / 3.0f, 2.0f / 3.0f, -1.0f);
    vec4 p = mix(vec4(c.z, c.y, K.w, K.z), vec4(c.y, c.z, K.x, K.y), step(c.z, c.y));
    vec4 q = mix(vec4(p.x, p.y, p.w, c.x), vec4(c.x, p.y, p.z, p.x), step(p.x, c.x));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10f;
    return vec3(abs(q.z + (q.w - q.y) / (6.0f * d + e)), d / (q.x + e), q.x);
}

vec3 bkHsv2Rgb(vec3 c)
{
    vec3 p = abs(fract(vec3(c.x, c.x, c.x) + vec3(1.0f, 2.0f / 3.0f, 1.0f / 3.0f)) * 6.0f - vec3(3.0f, 3.0f, 3.0f));
    return c.z * mix(vec3(1.0f, 1.0f, 1.0f), clamp(p - vec3(1.0f, 1.0f, 1.0f), 0.0f, 1.0f), c.y);
}

vec4 bkApplyHSVStruct(BkHSV hsvValue, vec4 color)
{
    vec3 hsv = bkRgb2Hsv(vec3(color.x, color.y, color.z));

    hsv.x = mod(hsv.x + hsvValue.hue, 1.0f);

    if (hsvValue.satAdd)
        hsv.y += hsvValue.sat;
    else
        hsv.y *= hsvValue.sat;

    if (hsvValue.valAdd)
        hsv.z += hsvValue.val;
    else
        hsv.z *= hsvValue.val;

    hsv.y = clamp(hsv.y, 0.0f, 1.0f);
    hsv.z = clamp(hsv.z, 0.0f, 1.0f);

    vec3 rgb = bkHsv2Rgb(hsv);
    return vec4(rgb.x, rgb.y, rgb.z, color.w);
}

BkHSV bkReadHSV(uint hsvValue)
{
    BkHSV hsv;
    hsv.hue = float(int((hsvValue >> HSV_HUE_BIT) & HSV_HUE_MASK) - 256) / 360.0f;
    hsv.sat = float((hsvValue >> HSV_SAT_BIT) & HSV_SAT_MASK) / 127.5f;
    hsv.val = float((hsvValue >> HSV_VAL_BIT) & HSV_VAL_MASK) / 127.5f;
    hsv.satAdd = (hsvValue & HSV_SAT_ADD) != 0u;
    hsv.valAdd = (hsvValue & HSV_VAL_ADD) != 0u;

    if (hsv.satAdd) hsv.sat -= 1.0f;
    if (hsv.valAdd) hsv.val -= 1.0f;

    return hsv;
}

vec4 bkApplyHSV(uint hsvValue, vec4 color)
{
    return bkApplyHSVStruct(bkReadHSV(hsvValue), color);
}

BkHSV bkMultipliedHSV(BkHSV hsv, float factor)
{
    BkHSV ret;
    ret.hue = hsv.hue * factor;
    ret.sat = hsv.satAdd ? hsv.sat * factor : hsv.sat * factor + (1.0f - factor);
    ret.val = hsv.valAdd ? hsv.val * factor : hsv.val * factor + (1.0f - factor);
    ret.satAdd = hsv.satAdd;
    ret.valAdd = hsv.valAdd;
    return ret;
}

//// PULSE TRIGGERS ////

vec3 bkApplyPulseEntry(vec3 color, uint flags, uint colorOrHSV)
{
    float value = float(flags & PULSE_FLAG_VALUE_MASK) / 65535.0f;

    if (value <= 0.0f)
        return color;

    if ((flags & PULSE_FLAG_IS_HSV) != 0u) {
        uint colorChannel = (flags >> PULSE_FLAG_CHANNEL_SHIFT) & PULSE_FLAG_CHANNEL_MASK;
        BkHSV hsv = bkReadHSV(colorOrHSV);

        vec3 srcColor = color;
        if (colorChannel != 0u) {
            vec4 channelColor = bkColorChannelColor(colorChannel);
            srcColor = vec3(channelColor.x, channelColor.y, channelColor.z);
        }

        if (value == 1.0f) {
            vec4 full = bkApplyHSVStruct(hsv, vec4(srcColor.x, srcColor.y, srcColor.z, 1.0f));
            return vec3(full.x, full.y, full.z);
        }

        if ((flags & PULSE_FLAG_IS_DYNAMIC_HSV) != 0u)
            hsv = bkMultipliedHSV(hsv, value);

        vec4 dstColor = bkApplyHSVStruct(hsv, vec4(srcColor.x, srcColor.y, srcColor.z, 1.0f));
        return mix(color, vec3(dstColor.x, dstColor.y, dstColor.z), value);
    }

    vec4 dstColor = bkUnpackColor(colorOrHSV);
    return mix(color, vec3(dstColor.x, dstColor.y, dstColor.z), value);
}

vec3 bkApplyPulseEntries(vec3 color, uint pulseIndex, bool isBaseColor)
{
    for (uint i = 0u; i < PULSE_MAX_ENTRIES_PER_SPRITE; i++) {
        uvec2 entry = bkPulseEntry(pulseIndex + i);
        uint flags = entry.x;
        if (flags == 0u)
            break;

        // Skip entries that don't apply to this sprite. (The original shader
        // `continue`d without advancing and could loop forever here.)
        if ((flags & PULSE_FLAG_IS_BASE_ONLY) != 0u && !isBaseColor)
            continue;

        if ((flags & PULSE_FLAG_IS_DETAIL_ONLY) != 0u && isBaseColor)
            continue;

        color = bkApplyPulseEntry(color, flags, entry.y);
    }

    return color;
}

//// POSITION AND CULLING ////

vec2 bkObjectPosition(BkGroupState state, vec2 startPosition)
{
    return bkMul(state.positionalTransform, startPosition) + state.offset;
}

bool bkIsOnScreen(BkGameState gs, vec2 objectPosition)
{
    vec2 offset = objectPosition - gs.cameraPosition;
    vec2 screenPos = vec2(dot(gs.cameraRight, offset), dot(gs.cameraUp, offset));

    return screenPos.x >= -CULL_SCREEN_THRESHOLD &&
           screenPos.x <= gs.cameraSize.x + CULL_SCREEN_THRESHOLD &&
           screenPos.y >= -CULL_SCREEN_THRESHOLD &&
           screenPos.y <= gs.cameraSize.y + CULL_SCREEN_THRESHOLD;
}

//// AUDIO SCALE ////

float bkAudioScale(BkGameState gs, BkObjectInfo object)
{
    float scale;
    if ((object.flags & OBJECT_FLAG_CUSTOM_AUDIO_SCALE) != 0u)
        scale = (gs.audioScale - 0.1f) * (object.audioScaleMax - object.audioScaleMin) + object.audioScaleMin;
    else
        scale = gs.audioScale;

    if ((object.flags & OBJECT_FLAG_IS_ORB) != 0u)
        scale = min(scale + 0.3f, 1.2f);
    return scale;
}

//// INVISIBLE BLOCKS ////

float bkRelativeMod(BkGameState gs, float xPos, float left, float right, float offset)
{
    float result = gs.cameraViewSize.x * 0.5f;

    if (xPos > result + gs.cameraPosition.x)
        result = (result - (xPos - offset - gs.cameraPosition.x - result)) * right;
    else
        result = (result - (result + gs.cameraPosition.x - xPos - offset)) * left;

    return clamp(result, 0.0f, 1.0f);
}

vec2 bkInvisibleBlockOpacity(BkGameState gs, BkObjectInfo object, vec2 objectPosition)
{
    float centerLeftX  = gs.winSize.x * 0.5f - 75.0f;
    float centerRightX = centerLeftX + 110.0f;
    float someScreenLeft = gs.screenRight - centerRightX - 90.0f;

    float fadePosX = objectPosition.x;
    if (fadePosX <= gs.cameraUnzoomedX)
        fadePosX += object.fadeMargin;
    else
        fadePosX -= object.fadeMargin;

    float relMod = bkRelativeMod(gs, fadePosX, 0.02f, 0.014285714f, 0.0f);

    // NOTE (from the original shader): invisible block opacity is still inaccurate.
    float someWidth1;
    if (fadePosX <= centerRightX + gs.cameraPosition.x)
        someWidth1 = (centerLeftX + gs.cameraPosition.x - fadePosX) / max(centerLeftX - 30.0f, 1.0f);
    else
        someWidth1 = (fadePosX - gs.cameraPosition.x - centerRightX) / max(someScreenLeft, 1.0f);

    someWidth1 = clamp(someWidth1, 0.0f, 1.0f);

    return vec2(
        min(someWidth1 * 0.95f + 0.05f, relMod),
        min(someWidth1 * 0.85f + 0.15f, relMod)
    );
}

vec4 bkInvisibleBlockColor(BkGameState gs, BkObjectInfo object, uint spriteType, vec2 objectPosition, vec4 color)
{
    if ((gs.flags & GAME_STATE_IS_PLAYER_DEAD) != 0u) {
        if (spriteType == SPRITE_TYPE_GLOW)
            return vec4(gs.specialLightBGColor.x, gs.specialLightBGColor.y, gs.specialLightBGColor.z, color.w);
        return color;
    }

    vec2 opacity = bkInvisibleBlockOpacity(gs, object, objectPosition);

    if (spriteType != SPRITE_TYPE_GLOW) {
        color.w *= opacity.x;
        return color;
    }

    vec4 colorBG  = bkColorChannelColor(COLOR_CHANNEL_BG);
    vec4 colorLBG = bkColorChannelColor(COLOR_CHANNEL_LBG);

    vec3 colorB;
    if ((colorBG.x + colorBG.y + colorBG.z) >= (150.0f / 255.0f))
        colorB = vec3(1.0f, 1.0f, 1.0f);
    else
        colorB = vec3(colorLBG.x, colorLBG.y, colorLBG.z);

    vec3 glowColor;
    if (opacity.x <= 0.8f)
        glowColor = gs.specialLightBGColor;
    else
        glowColor = mix(colorB, gs.specialLightBGColor, (1.0f - (opacity.x - 0.8f) / 0.2f) * 0.3f + 0.7f);

    return vec4(glowColor.x, glowColor.y, glowColor.z, color.w * opacity.y);
}

//// SPRITE PROCESSING ////

/*
    Computes the local transform and premultiplied color of a sprite that is
    already known to be on screen. `visible` is false when fully transparent.
*/
BkSpriteResult bkProcessSprite(
    BkGameState gs,
    BkGroupState state,
    BkObjectInfo object,
    uint spriteType,
    vec2 objectPosition
) {
    BkSpriteResult result;

    //// LOCAL TRANSFORM ////
    vec4 localTransform = vec4(1.0f, 0.0f, 0.0f, 1.0f);

    if ((object.flags & OBJECT_FLAG_IS_STATIC_OBJECT) == 0u)
        localTransform = state.localTransform;

    if (object.rotationSpeed != 0.0f)
        localTransform = bkMatMul(bkRotationMatrix(-object.rotationSpeed * gs.timer / 180.0f * BK_PI), localTransform);

    if ((object.flags & OBJECT_FLAG_USES_AUDIO_SCALE) != 0u)
        localTransform = bkMatMul(bkScaleMatrix(bkAudioScale(gs, object)), localTransform);

    //// COLOR ////
    uint colorChannel = COLOR_CHANNEL_BLACK;
    if (spriteType == SPRITE_TYPE_GLOW || spriteType == SPRITE_TYPE_BASE)
        colorChannel = object.baseColorChannel;
    else if (spriteType == SPRITE_TYPE_DETAIL)
        colorChannel = object.detailColorChannel;

    vec4 color;
    if (colorChannel == COLOR_CHANNEL_LIGHTER) {
        BkHSV hsv;
        hsv.hue = 0.0f;
        hsv.sat = 0.65f;
        hsv.val = 0.15f;
        hsv.satAdd = false;
        hsv.valAdd = true;
        color = bkApplyHSVStruct(hsv, bkColorChannelColor(object.baseColorChannel));
    } else {
        color = bkColorChannelColor(colorChannel);
    }

    if (spriteType == SPRITE_TYPE_GLOW && (object.flags & OBJECT_FLAG_SPECIAL_GLOW_COLOR) != 0u)
        color = vec4(gs.specialLightBGColor.x, gs.specialLightBGColor.y, gs.specialLightBGColor.z, 1.0f);

    if (state.pulseIndex != 0u && (spriteType == SPRITE_TYPE_BASE || spriteType == SPRITE_TYPE_DETAIL)) {
        vec3 pulsed = bkApplyPulseEntries(vec3(color.x, color.y, color.z), state.pulseIndex, spriteType == SPRITE_TYPE_BASE);
        color = vec4(pulsed.x, pulsed.y, pulsed.z, color.w);
    }

    if (spriteType == SPRITE_TYPE_BASE && (object.flags & OBJECT_FLAG_HAS_BASE_HSV) != 0u)
        color = bkApplyHSV(object.baseHSV, color);
    else if (spriteType == SPRITE_TYPE_DETAIL && (object.flags & OBJECT_FLAG_HAS_DETAIL_HSV) != 0u)
        color = bkApplyHSV(object.detailHSV, color);

    if ((object.flags & OBJECT_FLAG_IS_INVISIBLE_BLOCK) != 0u)
        color = bkInvisibleBlockColor(gs, object, spriteType, objectPosition, color);

    color.w *= state.opacity * object.opacity;
    color.x *= color.w;
    color.y *= color.w;
    color.z *= color.w;

    result.visible        = color.w > 0.0f;
    result.localTransform = localTransform;
    result.color          = color;
    return result;
}
