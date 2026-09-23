#pragma once

namespace renderdebug
{
// Compatibility wrapper for remaining immediate-mode submissions (post-processing).
// Batched glDrawArrays/instanced calls are counted at submission in RenderQueue.
void BeginFrame();
void BeginPrimitive(unsigned int mode);
void EndFrame();
} // namespace renderdebug
