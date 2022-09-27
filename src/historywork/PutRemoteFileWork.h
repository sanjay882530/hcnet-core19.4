// Copyright 2015 Hcnet Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#pragma once

#include "historywork/RunCommandWork.h"

namespace hcnet
{

class HistoryArchive;

class PutRemoteFileWork : public RunCommandWork
{
    std::string const mLocal;
    std::string const mRemote;
    std::shared_ptr<HistoryArchive> mArchive;
    CommandInfo getCommand() override;

  public:
    PutRemoteFileWork(Application& app, std::string const& local,
                      std::string const& remote,
                      std::shared_ptr<HistoryArchive> archive);
    ~PutRemoteFileWork() = default;
};
}
