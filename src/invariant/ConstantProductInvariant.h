#pragma once

// Copyright 2021 Hcnet Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "invariant/Invariant.h"
#include "ledger/LedgerTxn.h"
#include "main/Application.h"
#include "xdr/Hcnet-ledger-entries.h"
#include "xdr/Hcnet-ledger.h"

namespace hcnet
{
class ConstantProductInvariant : public Invariant
{
  public:
    explicit ConstantProductInvariant() : Invariant(true)
    {
    }

    static std::shared_ptr<Invariant> registerInvariant(Application& app);

    virtual std::string getName() const override;

    virtual std::string
    checkOnOperationApply(Operation const& operation,
                          OperationResult const& result,
                          LedgerTxnDelta const& ltxDelta) override;
};
}
