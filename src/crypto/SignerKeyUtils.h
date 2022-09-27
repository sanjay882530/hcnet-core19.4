#pragma once

// Copyright 2016 Hcnet Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "xdr/Hcnet-types.h"

namespace hcnet
{

class ByteSlice;
class TransactionFrame;
class FeeBumpTransactionFrame;
struct SignerKey;

namespace SignerKeyUtils
{

SignerKey preAuthTxKey(TransactionFrame const& tx);
SignerKey preAuthTxKey(FeeBumpTransactionFrame const& tx);
SignerKey hashXKey(ByteSlice const& bs);
SignerKey ed25519PayloadKey(uint256 const& ed25519,
                            xdr::opaque_vec<64> const& payload);
}
}
