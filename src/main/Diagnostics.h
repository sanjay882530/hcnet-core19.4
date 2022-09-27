#pragma once

// Copyright 2021 Hcnet Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "overlay/HcnetXDR.h"

namespace hcnet
{
namespace diagnostics
{
void bucketStats(std::string const& filename, bool aggregateAccounts);
}
}
