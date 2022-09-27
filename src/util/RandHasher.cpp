// Copyright 2021 Hcnet Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "RandHasher.h"
#include "Math.h"
#include "fmt/format.h"
#include <mutex>

namespace hcnet
{
namespace randHash
{

size_t gMixer{0};
bool gHaveInitialized{false};
static std::mutex gInitMutex;

void
initialize()
{
    std::lock_guard<std::mutex> guard(gInitMutex);
    if (!gHaveInitialized)
    {
        gMixer =
            hcnet::rand_uniform<size_t>(std::numeric_limits<size_t>::min(),
                                          std::numeric_limits<size_t>::max());
        gHaveInitialized = true;
    }
}
}
}