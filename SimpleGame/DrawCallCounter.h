#pragma once

namespace renderdebug
{
// Count application submissions, not driver-internal GPU commands.
void BeginFrame();
void BeginPrimitive(unsigned int mode);
void EndFrame();
} // namespace renderdebug
