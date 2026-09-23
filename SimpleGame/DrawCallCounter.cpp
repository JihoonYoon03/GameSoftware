#include "stdafx.h"
#include "DrawCallCounter.h"
#include "Profiler.h"
#include "Dependencies/glew.h"

namespace renderdebug
{
void BeginFrame()
{
    profiling::BeginFrame("unknown");
}
void BeginPrimitive(unsigned int mode)
{
    profiling::Count(profiling::Counter::DrawCalls);
    profiling::Count(profiling::Counter::SubmittedPrimitives);
    glBegin(mode);
}
void EndFrame()
{
    profiling::EndFrame();
}
} // namespace renderdebug
