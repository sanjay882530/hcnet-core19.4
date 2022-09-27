#pragma once

// Copyright 2015 Hcnet Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/OperationFrame.h"

namespace hcnet
{

class LedgerTxnHeader;

class MergeOpFrame : public OperationFrame
{
    AccountMergeResult&
    innerResult()
    {
        return mResult.tr().accountMergeResult();
    }

    bool doApplyBeforeV16(AbstractLedgerTxn& ltx);
    bool doApplyFromV16(AbstractLedgerTxn& ltx);

    ThresholdLevel getThresholdLevel() const override;

    virtual bool isSeqnumTooFar(AbstractLedgerTxn& ltx,
                                LedgerTxnHeader const& header,
                                AccountEntry const& sourceAccount);

  public:
    MergeOpFrame(Operation const& op, OperationResult& res,
                 TransactionFrame& parentTx);

    bool doApply(AbstractLedgerTxn& ltx) override;
    bool doCheckValid(uint32_t ledgerVersion) override;

    static AccountMergeResultCode
    getInnerCode(OperationResult const& res)
    {
        return res.tr().accountMergeResult().code();
    }
};
}
