// Copyright 2022 Hcnet Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "overlay/TxAdvertQueue.h"
#include "ledger/LedgerManager.h"

namespace hcnet
{

TxAdvertQueue::TxAdvertQueue(Application& app) : mApp(app)
{
}

size_t
TxAdvertQueue::size() const
{
    return mIncomingTxHashes.size() + mTxHashesToRetry.size();
}

void
TxAdvertQueue::appendHashesToRetryAndMaybeTrim(std::list<Hash>& list)
{
    mTxHashesToRetry.splice(mTxHashesToRetry.end(), list);
    while (size() > mApp.getLedgerManager().getLastMaxTxSetSizeOps())
    {
        pop();
    }
}

void
TxAdvertQueue::queueAndMaybeTrim(TxAdvertVector const& txHashes)
{
    auto it = txHashes.begin();
    size_t const limit = mApp.getLedgerManager().getLastMaxTxSetSizeOps();
    if (txHashes.size() > limit)
    {
        // If txHashes has more than getLastMaxTxSetSizeOps txns, then
        // the first (txHashes.size() - getLastMaxTxSetSizeOps) txns will be
        // popped in the while loop below. Therefore, we won't even bother
        // pushing them.
        it += txHashes.size() - limit;
    }

    while (it != txHashes.end())
    {
        mIncomingTxHashes.emplace_back(*it);
        it++;
    }

    while (size() > limit)
    {
        pop();
    }
}

Hash
TxAdvertQueue::pop()
{
    releaseAssert(size() > 0);

    if (mTxHashesToRetry.size() > 0)
    {
        auto const h = mTxHashesToRetry.front();
        mTxHashesToRetry.pop_front();
        return h;
    }
    else
    {
        auto const h = mIncomingTxHashes.front();
        mIncomingTxHashes.pop_front();
        return h;
    }
}

}
