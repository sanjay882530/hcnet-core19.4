#pragma once

// Copyright 2022 Hcnet Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "xdr/Hcnet-transaction.h"
#ifdef ENABLE_NEXT_PROTOCOL_VERSION_UNSAFE_FOR_PRODUCTION
#include "transactions/OperationFrame.h"

namespace hcnet
{
class AbstractLedgerTxn;

class InvokeHostFunctionOpFrame : public OperationFrame
{
    InvokeHostFunctionResult&
    innerResult()
    {
        return mResult.tr().invokeHostFunctionResult();
    }

    InvokeHostFunctionOp const& mInvokeHostFunction;

  public:
    InvokeHostFunctionOpFrame(Operation const& op, OperationResult& res,
                              TransactionFrame& parentTx);

    ThresholdLevel getThresholdLevel() const override;

    bool isOpSupported(LedgerHeader const& header) const override;

    bool doApply(AbstractLedgerTxn& ltx) override;
    bool doCheckValid(uint32_t ledgerVersion) override;

    static Json::Value preflight(Application& app,
                                 InvokeHostFunctionOp const& op);

    void
    insertLedgerKeysToPrefetch(UnorderedSet<LedgerKey>& keys) const override;

    static InvokeHostFunctionResultCode
    getInnerCode(OperationResult const& res)
    {
        return res.tr().invokeHostFunctionResult().code();
    }
};
}
#endif // ENABLE_NEXT_PROTOCOL_VERSION_UNSAFE_FOR_PRODUCTION
