#pragma once

// Copyright 2018 Hcnet Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "crypto/ShortHash.h"
#include "ledger/InternalLedgerEntry.h"
#include "util/HashOfHash.h"
#include "xdr/Hcnet-ledger-entries.h"
#include "xdr/Hcnet-ledger.h"
#include <functional>

namespace hcnet
{

static PoolID const&
getLiquidityPoolID(Asset const& asset)
{
    throw std::runtime_error("cannot get PoolID from Asset");
}

static PoolID const&
getLiquidityPoolID(TrustLineAsset const& tlAsset)
{
    return tlAsset.liquidityPoolID();
}

static inline void
hashMix(size_t& h, size_t v)
{
    // from https://github.com/ztanml/fast-hash (MIT license)
    v ^= v >> 23;
    v *= 0x2127599bf4325c37ULL;
    v ^= v >> 47;
    h ^= v;
    h *= 0x880355f21e6d1965ULL;
}

template <typename T>
static size_t
getAssetHash(T const& asset)
{
    size_t res = asset.type();

    switch (asset.type())
    {
    case hcnet::ASSET_TYPE_NATIVE:
        break;
    case hcnet::ASSET_TYPE_CREDIT_ALPHANUM4:
    {
        auto& a4 = asset.alphaNum4();
        hashMix(res, std::hash<hcnet::uint256>()(a4.issuer.ed25519()));
        hashMix(res, hcnet::shortHash::computeHash(hcnet::ByteSlice(
                         a4.assetCode.data(), a4.assetCode.size())));
        break;
    }
    case hcnet::ASSET_TYPE_CREDIT_ALPHANUM12:
    {
        auto& a12 = asset.alphaNum12();
        hashMix(res, std::hash<hcnet::uint256>()(a12.issuer.ed25519()));
        hashMix(res, hcnet::shortHash::computeHash(hcnet::ByteSlice(
                         a12.assetCode.data(), a12.assetCode.size())));
        break;
    }
    case hcnet::ASSET_TYPE_POOL_SHARE:
    {
        hashMix(res, std::hash<hcnet::uint256>()(getLiquidityPoolID(asset)));
        break;
    }
    default:
        throw std::runtime_error("unknown Asset type");
    }
    return res;
}

}

// implements a default hasher for "LedgerKey"
namespace std
{
template <> class hash<hcnet::Asset>
{
  public:
    size_t
    operator()(hcnet::Asset const& asset) const
    {
        return hcnet::getAssetHash<hcnet::Asset>(asset);
    }
};

template <> class hash<hcnet::TrustLineAsset>
{
  public:
    size_t
    operator()(hcnet::TrustLineAsset const& asset) const
    {
        return hcnet::getAssetHash<hcnet::TrustLineAsset>(asset);
    }
};

template <> class hash<hcnet::LedgerKey>
{
  public:
    size_t
    operator()(hcnet::LedgerKey const& lk) const
    {
        size_t res = lk.type();
        switch (lk.type())
        {
        case hcnet::ACCOUNT:
            hcnet::hashMix(res, std::hash<hcnet::uint256>()(
                                      lk.account().accountID.ed25519()));
            break;
        case hcnet::TRUSTLINE:
        {
            auto& tl = lk.trustLine();
            hcnet::hashMix(
                res, std::hash<hcnet::uint256>()(tl.accountID.ed25519()));
            hcnet::hashMix(res, hash<hcnet::TrustLineAsset>()(tl.asset));
            break;
        }
        case hcnet::DATA:
            hcnet::hashMix(res, std::hash<hcnet::uint256>()(
                                      lk.data().accountID.ed25519()));
            hcnet::hashMix(
                res,
                hcnet::shortHash::computeHash(hcnet::ByteSlice(
                    lk.data().dataName.data(), lk.data().dataName.size())));
            break;
        case hcnet::OFFER:
            hcnet::hashMix(
                res, hcnet::shortHash::computeHash(hcnet::ByteSlice(
                         &lk.offer().offerID, sizeof(lk.offer().offerID))));
            break;
        case hcnet::CLAIMABLE_BALANCE:
            hcnet::hashMix(res, std::hash<hcnet::uint256>()(
                                      lk.claimableBalance().balanceID.v0()));
            break;
        case hcnet::LIQUIDITY_POOL:
            hcnet::hashMix(res, std::hash<hcnet::uint256>()(
                                      lk.liquidityPool().liquidityPoolID));
            break;
#ifdef ENABLE_NEXT_PROTOCOL_VERSION_UNSAFE_FOR_PRODUCTION
        case hcnet::CONTRACT_DATA:
            hcnet::hashMix(res, std::hash<hcnet::uint256>()(
                                      lk.contractData().contractID));
            hcnet::hashMix(
                res, hcnet::shortHash::xdrComputeHash(lk.contractData().key));
            break;
        case hcnet::CONFIG_SETTING:
            hcnet::hashMix(
                res, std::hash<int32_t>()(lk.configSetting().configSettingID));
            break;
#endif
        default:
            abort();
        }
        return res;
    }
};

template <> class hash<hcnet::InternalLedgerKey>
{
  public:
    size_t
    operator()(hcnet::InternalLedgerKey const& glk) const
    {
        return glk.hash();
    }
};
}
