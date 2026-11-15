#pragma once
#include "ap/base.hpp"
// Patch an explicitly reserved resource section; works from any build host.
ap::Str windowsResources(ap::Str stub, const ap::Str& icon);
