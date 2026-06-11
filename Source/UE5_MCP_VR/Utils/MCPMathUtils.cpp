#include "MCPMathUtils.h"

FVector UMCPMathUtils::ConvertToUnrealLocation(float X, float Y, float Z)
{
    // Unreal Units are cm. 
    // Assuming Input is in Meters.
    const float Scale = 100.0f;

    // Direct mapping with scale (Assuming Brain speaks UE coordinate system X/Y/Z)
    return FVector(X * Scale, Y * Scale, Z * Scale);
}
