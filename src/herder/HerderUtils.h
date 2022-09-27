#pragma once

// Copyright 2017 Hcnet Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "xdr/Hcnet-types.h"
#include <vector>

namespace hcnet
{

struct SCPEnvelope;
struct SCPStatement;
struct HcnetValue;

std::vector<Hash> getTxSetHashes(SCPEnvelope const& envelope);
std::vector<HcnetValue> getHcnetValues(SCPStatement const& envelope);
}
