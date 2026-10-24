#pragma once

#include "../../../Resource/ActionReleaseFrameRatio.h"
#include "../../../Types/CommonTypes.h"
#include <algorithm>
#include <cstddef>

inline UTime calculateActionReleaseTime(UTime actionTime, std::size_t framesPerDirection,
    const ActionReleaseFrameRatio& ratio)
{
    if (actionTime == 0 || framesPerDirection == 0)
    {
        return 0;
    }
    if (ratio.numerator <= 0 || ratio.denominator <= 0 || ratio.numerator > ratio.denominator)
    {
        return actionTime;
    }
    // Preserve the native integer frame gate; reducing the fraction changes it.
    const UTime releaseFrame = std::max<UTime>(1,
        (framesPerDirection / static_cast<unsigned int>(ratio.denominator))
        * static_cast<unsigned int>(ratio.numerator));
    // IMP action durations and stored frame counts are 32-bit. Round up so the
    // effect cannot precede the corresponding frame on the admitted visual clock.
    return (actionTime * releaseFrame + framesPerDirection - 1) / framesPerDirection;
}
