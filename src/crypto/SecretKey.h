#pragma once

// Copyright 2014 Hcnet Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "crypto/KeyUtils.h"
#include "util/XDROperators.h"
#include "xdr/Hcnet-types.h"

#include <array>
#include <functional>
#include <ostream>

namespace hcnet
{

class ByteSlice;
struct SecretValue;
struct SignerKey;

class SecretKey
{
    using uint512 = xdr::opaque_array<64>;
    PublicKeyType mKeyType;
    uint512 mSecretKey;
    PublicKey mPublicKey;

    struct Seed
    {
        PublicKeyType mKeyType;
        uint256 mSeed;
        ~Seed();
    };

    // Get the seed portion of this secret key.
    Seed getSeed() const;

  public:
    SecretKey();
    ~SecretKey();

    // Get the public key portion of this secret key.
    PublicKey const& getPublicKey() const;

    // Get the seed portion of this secret key as a StrKey string.
    SecretValue getStrKeySeed() const;

    // Get the public key portion of this secret key as a StrKey string.
    std::string getStrKeyPublic() const;

    // Return true iff this key is all-zero.
    bool isZero() const;

    // Produce a signature of `bin` using this secret key.
    Signature sign(ByteSlice const& bin) const;

    // Create a new, random secret key.
    static SecretKey random();

    // Measure the speed of sign-and-verify ops.
    static void benchmarkOpsPerSecond(size_t& sign, size_t& verify,
                                      size_t iterations,
                                      size_t cachedVerifyPasses = 1);

#ifdef BUILD_TESTS
    // Create a new, pseudo-random secret key drawn from the global weak
    // non-cryptographic PRNG (which itself is seeded from command-line or
    // deterministically). Do not under any circumstances use this for non-test
    // key generation.
    static SecretKey pseudoRandomForTesting();

    // Same as above, but use a function-local PRNG seeded from the
    // provided number. Again: do not under any circumstances use this
    // for non-test key generation
    static SecretKey pseudoRandomForTestingFromSeed(unsigned int seed);
#endif

    // Decode a secret key from a provided StrKey seed value.
    static SecretKey fromStrKeySeed(std::string const& strKeySeed);
    static SecretKey
    fromStrKeySeed(std::string&& strKeySeed)
    {
        SecretKey ret = fromStrKeySeed(strKeySeed);
        for (std::size_t i = 0; i < strKeySeed.size(); ++i)
            strKeySeed[i] = 0;
        return ret;
    }

    // Decode a secret key from a binary seed value.
    static SecretKey fromSeed(ByteSlice const& seed);

    bool
    operator==(SecretKey const& rh) const
    {
        return (mKeyType == rh.mKeyType) && (mSecretKey == rh.mSecretKey);
    }

    bool
    operator<(SecretKey const& rh) const
    {
        if (mKeyType < rh.mKeyType)
        {
            return true;
        }
        if (mKeyType > rh.mKeyType)
        {
            return false;
        }

        return mSecretKey < rh.mSecretKey;
    }
};

template <> struct KeyFunctions<PublicKey>
{
    struct getKeyTypeEnum
    {
        using type = PublicKeyType;
    };

    static std::string getKeyTypeName();
    static bool getKeyVersionIsSupported(strKey::StrKeyVersionByte keyVersion);
    static bool
    getKeyVersionIsVariableLength(strKey::StrKeyVersionByte keyVersion);
    static PublicKeyType toKeyType(strKey::StrKeyVersionByte keyVersion);
    static strKey::StrKeyVersionByte toKeyVersion(PublicKeyType keyType);
    static uint256& getEd25519Value(PublicKey& key);
    static uint256 const& getEd25519Value(PublicKey const& key);

    static std::vector<uint8_t> getKeyValue(PublicKey const& key);
    static void setKeyValue(PublicKey& key, std::vector<uint8_t> const& data);
};

// public key utility functions
namespace PubKeyUtils
{
// Return true iff `signature` is valid for `bin` under `key`.
bool verifySig(PublicKey const& key, Signature const& signature,
               ByteSlice const& bin);

void clearVerifySigCache();
void flushVerifySigCacheCounts(uint64_t& hits, uint64_t& misses);

PublicKey random();
#ifdef BUILD_TESTS
PublicKey pseudoRandomForTesting();
#endif
}

namespace StrKeyUtils
{
// logs a key (can be a public or private key) in all
// known formats
void logKey(std::ostream& s, std::string const& key);
}

namespace HashUtils
{
Hash random();
#ifdef BUILD_TESTS
Hash pseudoRandomForTesting();
#endif
}
}

namespace std
{
template <> struct hash<hcnet::PublicKey>
{
    size_t operator()(hcnet::PublicKey const& x) const noexcept;
};
}
