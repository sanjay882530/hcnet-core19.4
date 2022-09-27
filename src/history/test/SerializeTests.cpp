// Copyright 2018 Hcnet Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "history/HistoryArchive.h"
#include "lib/catch.hpp"

#include <fstream>
#include <string>

using namespace hcnet;

TEST_CASE("Serialization round trip", "[history]")
{
    std::vector<std::string> testFiles = {
        "hcnet-history.testnet.6714239.json",
        "hcnet-history.livenet.15686975.json",
        "hcnet-history.testnet.6714239.networkPassphrase.json"};
    for (size_t i = 0; i < testFiles.size(); i++)
    {
        std::string fnPath = "testdata/";
        std::string testFilePath = fnPath + testFiles[i];
        SECTION("Serialize " + testFilePath)
        {
            std::ifstream in(testFilePath);
            REQUIRE(in);
            in.exceptions(std::ios::badbit);
            std::string hasString((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());

            // Test fromString
            HistoryArchiveState has;
            has.fromString(hasString);
            REQUIRE(hasString == has.toString());

            // Test load
            HistoryArchiveState hasLoad;
            hasLoad.load(testFilePath);
            REQUIRE(hasString == hasLoad.toString());
        }
    }
}
