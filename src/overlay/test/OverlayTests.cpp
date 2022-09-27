// Copyright 2014 Hcnet Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "crypto/KeyUtils.h"
#include "crypto/SecretKey.h"
#include "lib/catch.hpp"
#include "main/Application.h"
#include "main/Config.h"
#include "overlay/BanManager.h"
#include "overlay/OverlayManagerImpl.h"
#include "overlay/PeerManager.h"
#include "overlay/TCPPeer.h"
#include "overlay/test/LoopbackPeer.h"
#include "simulation/Simulation.h"
#include "simulation/Topologies.h"
#include "test/TestUtils.h"
#include "test/test.h"
#include "util/Logging.h"
#include "util/ProtocolVersion.h"
#include "util/Timer.h"

#include "herder/HerderImpl.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"
#include "medida/timer.h"
#include <fmt/format.h>
#include <numeric>

using namespace hcnet;

namespace
{
bool
doesNotKnow(Application& knowingApp, Application& knownApp)
{
    return !knowingApp.getOverlayManager()
                .getPeerManager()
                .load(PeerBareAddress{"127.0.0.1",
                                      knownApp.getConfig().PEER_PORT})
                .second;
}

bool
knowsAs(Application& knowingApp, Application& knownApp, PeerType peerType)
{
    auto data = knowingApp.getOverlayManager().getPeerManager().load(
        PeerBareAddress{"127.0.0.1", knownApp.getConfig().PEER_PORT});
    if (!data.second)
    {
        return false;
    }

    return data.first.mType == static_cast<int>(peerType);
}

bool
knowsAsInbound(Application& knowingApp, Application& knownApp)
{
    return knowsAs(knowingApp, knownApp, PeerType::INBOUND);
}

bool
knowsAsOutbound(Application& knowingApp, Application& knownApp)
{
    return knowsAs(knowingApp, knownApp, PeerType::OUTBOUND);
}

TEST_CASE("loopback peer hello", "[overlay][connections]")
{
    VirtualClock clock;
    Config const& cfg1 = getTestConfig(0);
    Config const& cfg2 = getTestConfig(1);
    auto app1 = createTestApplication(clock, cfg1);
    auto app2 = createTestApplication(clock, cfg2);

    LoopbackPeerConnection conn(*app1, *app2);
    testutil::crankSome(clock);

    REQUIRE(conn.getInitiator()->isAuthenticated());
    REQUIRE(conn.getAcceptor()->isAuthenticated());

    REQUIRE(knowsAsOutbound(*app1, *app2));
    REQUIRE(knowsAsInbound(*app2, *app1));

    testutil::shutdownWorkScheduler(*app2);
    testutil::shutdownWorkScheduler(*app1);
}

TEST_CASE("loopback peer with 0 port", "[overlay][connections]")
{
    VirtualClock clock;
    auto const& cfg1 = getTestConfig(0);
    auto cfg2 = getTestConfig(1);
    cfg2.PEER_PORT = 0;

    auto app1 = createTestApplication(clock, cfg1);
    auto app2 = createTestApplication(clock, cfg2);

    LoopbackPeerConnection conn(*app1, *app2);
    testutil::crankSome(clock);

    REQUIRE(!conn.getInitiator()->isAuthenticated());
    REQUIRE(!conn.getAcceptor()->isAuthenticated());

    testutil::shutdownWorkScheduler(*app2);
    testutil::shutdownWorkScheduler(*app1);
}

TEST_CASE("loopback peer send auth before hello", "[overlay][connections]")
{
    VirtualClock clock;
    auto const& cfg1 = getTestConfig(0);
    auto const& cfg2 = getTestConfig(1);
    auto app1 = createTestApplication(clock, cfg1);
    auto app2 = createTestApplication(clock, cfg2);

    LoopbackPeerConnection conn(*app1, *app2);
    conn.getInitiator()->sendAuth();
    testutil::crankSome(clock);

    REQUIRE(!conn.getInitiator()->isAuthenticated());
    REQUIRE(!conn.getAcceptor()->isAuthenticated());

    REQUIRE(doesNotKnow(*app1, *app2));
    REQUIRE(doesNotKnow(*app2, *app1));

    testutil::shutdownWorkScheduler(*app2);
    testutil::shutdownWorkScheduler(*app1);
}

TEST_CASE("loopback peer flow control activation", "[overlay][flowcontrol]")
{
    VirtualClock clock;
    auto cfg1 = getTestConfig(0);
    auto cfg2 = getTestConfig(1);

    auto runTest = [&](std::vector<Config> expectedCfgs,
                       Peer::FlowControlState expectedState,
                       bool sendIllegalSendMore = false) {
        REQUIRE(expectedState != Peer::FlowControlState::DONT_KNOW);
        auto app1 = createTestApplication(clock, expectedCfgs[0]);
        auto app2 = createTestApplication(clock, expectedCfgs[1]);

        LoopbackPeerConnection conn(*app1, *app2);
        testutil::crankSome(clock);

        if (expectedState == Peer::FlowControlState::ENABLED)
        {
            REQUIRE(conn.getInitiator()->isAuthenticated());
            REQUIRE(conn.getAcceptor()->isAuthenticated());
            REQUIRE(conn.getInitiator()->flowControlEnabled() == expectedState);
            REQUIRE(conn.getAcceptor()->flowControlEnabled() == expectedState);
            REQUIRE(conn.getInitiator()->checkCapacity(
                cfg2.PEER_FLOOD_READING_CAPACITY));
            REQUIRE(conn.getAcceptor()->checkCapacity(
                cfg1.PEER_FLOOD_READING_CAPACITY));

            if (sendIllegalSendMore)
            {
                // if flow control is enabled, ensure it can't be disabled, and
                // the misbehaving peer gets dropped
                conn.getInitiator()->sendSendMore(0);
                testutil::crankSome(clock);
                REQUIRE(!conn.getInitiator()->isConnected());
                REQUIRE(!conn.getAcceptor()->isConnected());
                REQUIRE(conn.getAcceptor()->getDropReason() ==
                        "unexpected SEND_MORE message");
            }
        }
        else
        {
            auto dropReason =
                cfg2.OVERLAY_PROTOCOL_VERSION <
                        Peer::FIRST_VERSION_SUPPORTING_FLOW_CONTROL
                    ? "wrong protocol version"
                    : "must enable flow control";
            REQUIRE(!conn.getInitiator()->isConnected());
            REQUIRE(!conn.getAcceptor()->isConnected());
            REQUIRE(conn.getAcceptor()->getDropReason() == dropReason);
        }

        testutil::shutdownWorkScheduler(*app2);
        testutil::shutdownWorkScheduler(*app1);
    };

    SECTION("both enable")
    {
        SECTION("basic")
        {
            // Successfully enabled flow control
            runTest({cfg1, cfg2}, Peer::FlowControlState::ENABLED, false);
        }
        SECTION("bad peer")
        {
            // Try to disable flow control after enabling
            runTest({cfg1, cfg2}, Peer::FlowControlState::ENABLED, true);
        }
    }
    SECTION("one disables")
    {
        // Peer tries to disable flow control during auth
        // Set capacity to 0 so that the peer sends SEND_MORE with numMessages=0
        cfg2.PEER_FLOOD_READING_CAPACITY = 0;
        runTest({cfg1, cfg2}, Peer::FlowControlState::DISABLED);
    }
    SECTION("one does not support")
    {
        // Peer is not on minimum supported version
        cfg2.OVERLAY_PROTOCOL_VERSION =
            Peer::FIRST_VERSION_SUPPORTING_FLOW_CONTROL - 1;
        runTest({cfg1, cfg2}, Peer::FlowControlState::DISABLED);
    }
}

TEST_CASE("drop peers that dont respect capacity", "[overlay][flowcontrol]")
{
    VirtualClock clock;
    Config cfg1 = getTestConfig(0);
    Config cfg2 = getTestConfig(1);

    // initiator can only accept 1 flood message at a time
    cfg1.PEER_FLOOD_READING_CAPACITY = 1;
    cfg1.FLOW_CONTROL_SEND_MORE_BATCH_SIZE = 1;
    // Set PEER_READING_CAPACITY to something higher so that the initiator will
    // read both messages right away and detect capacity violation
    cfg1.PEER_READING_CAPACITY = 2;

    auto app1 = createTestApplication(clock, cfg1);
    auto app2 = createTestApplication(clock, cfg2);

    LoopbackPeerConnection conn(*app1, *app2);
    testutil::crankSome(clock);
    REQUIRE(conn.getInitiator()->isAuthenticated());
    REQUIRE(conn.getAcceptor()->isAuthenticated());

    REQUIRE(conn.getInitiator()->flowControlEnabled() ==
            Peer::FlowControlState::ENABLED);
    REQUIRE(conn.getAcceptor()->flowControlEnabled() ==
            Peer::FlowControlState::ENABLED);

    // tx is invalid, but it doesn't matter
    HcnetMessage msg;
    msg.type(TRANSACTION);
    // Acceptor sends too many flood messages, causing initiator to drop it
    conn.getAcceptor()->sendAuthenticatedMessage(msg);
    conn.getAcceptor()->sendAuthenticatedMessage(msg);
    testutil::crankSome(clock);

    REQUIRE(!conn.getInitiator()->isConnected());
    REQUIRE(!conn.getAcceptor()->isConnected());
    REQUIRE(conn.getInitiator()->getDropReason() ==
            "unexpected flood message, peer at capacity");

    testutil::shutdownWorkScheduler(*app2);
    testutil::shutdownWorkScheduler(*app1);
}

TEST_CASE("drop idle flow-controlled peers", "[overlay][flowcontrol]")
{
    VirtualClock clock;
    Config cfg1 = getTestConfig(0);
    Config cfg2 = getTestConfig(1);

    cfg1.PEER_FLOOD_READING_CAPACITY = 1;
    cfg1.PEER_READING_CAPACITY = 1;
    // Incorrectly set batch size, so that the node does not send flood requests
    cfg1.FLOW_CONTROL_SEND_MORE_BATCH_SIZE = 2;

    auto app1 = createTestApplication(clock, cfg1);
    auto app2 = createTestApplication(clock, cfg2);

    LoopbackPeerConnection conn(*app1, *app2);
    testutil::crankSome(clock);
    REQUIRE(conn.getInitiator()->isAuthenticated());
    REQUIRE(conn.getAcceptor()->isAuthenticated());

    REQUIRE(conn.getInitiator()->flowControlEnabled() ==
            Peer::FlowControlState::ENABLED);
    REQUIRE(conn.getAcceptor()->flowControlEnabled() ==
            Peer::FlowControlState::ENABLED);

    HcnetMessage msg;
    msg.type(TRANSACTION);
    REQUIRE(conn.getAcceptor()->getOutboundCapacity() == 1);
    // Send outbound message and start the timer
    conn.getAcceptor()->sendMessage(std::make_shared<HcnetMessage>(msg),
                                    false);
    REQUIRE(conn.getAcceptor()->getOutboundCapacity() == 0);

    testutil::crankFor(clock, Peer::PEER_SEND_MODE_IDLE_TIMEOUT +
                                  std::chrono::seconds(5));

    REQUIRE(!conn.getInitiator()->isConnected());
    REQUIRE(!conn.getAcceptor()->isConnected());
    REQUIRE(conn.getAcceptor()->getDropReason() ==
            "idle timeout (no new flood requests)");

    testutil::shutdownWorkScheduler(*app2);
    testutil::shutdownWorkScheduler(*app1);
}

TEST_CASE("drop peers that overflow capacity", "[overlay][flowcontrol]")
{
    VirtualClock clock;
    Config cfg1 = getTestConfig(0);
    Config cfg2 = getTestConfig(1);

    auto app1 = createTestApplication(clock, cfg1);
    auto app2 = createTestApplication(clock, cfg2);

    LoopbackPeerConnection conn(*app1, *app2);
    testutil::crankSome(clock);
    REQUIRE(conn.getInitiator()->isAuthenticated());
    REQUIRE(conn.getAcceptor()->isAuthenticated());

    REQUIRE(conn.getInitiator()->flowControlEnabled() ==
            Peer::FlowControlState::ENABLED);
    REQUIRE(conn.getAcceptor()->flowControlEnabled() ==
            Peer::FlowControlState::ENABLED);

    // Set outbound capacity close to max on initiator
    auto& cap = conn.getInitiator()->getOutboundCapacity();
    cap = UINT64_MAX - 1;

    // Acceptor sends request for more that overflows capacity
    conn.getAcceptor()->sendSendMore(2);
    testutil::crankSome(clock);

    REQUIRE(!conn.getInitiator()->isConnected());
    REQUIRE(!conn.getAcceptor()->isConnected());
    REQUIRE(conn.getInitiator()->getDropReason() == "Peer capacity overflow");

    testutil::shutdownWorkScheduler(*app2);
    testutil::shutdownWorkScheduler(*app1);
}

TEST_CASE("failed auth", "[overlay][connections]")
{
    VirtualClock clock;
    Config const& cfg1 = getTestConfig(0);
    Config const& cfg2 = getTestConfig(1);
    auto app1 = createTestApplication(clock, cfg1);
    auto app2 = createTestApplication(clock, cfg2);

    LoopbackPeerConnection conn(*app1, *app2);
    conn.getInitiator()->setDamageAuth(true);
    testutil::crankSome(clock);

    REQUIRE(!conn.getInitiator()->isConnected());
    REQUIRE(!conn.getAcceptor()->isConnected());
    REQUIRE(conn.getInitiator()->getDropReason() == "unexpected MAC");

    REQUIRE(knowsAsOutbound(*app1, *app2));
    REQUIRE(knowsAsInbound(*app2, *app1));

    testutil::shutdownWorkScheduler(*app2);
    testutil::shutdownWorkScheduler(*app1);
}

TEST_CASE("outbound queue filtering", "[overlay][connections]")
{
    auto networkID = sha256(getTestConfig().NETWORK_PASSPHRASE);
    auto simulation = std::make_shared<Simulation>(
        Simulation::OVER_LOOPBACK, networkID, [](int i) {
            auto cfg = getTestConfig(i, Config::TESTDB_ON_DISK_SQLITE);
            cfg.MAX_SLOTS_TO_REMEMBER = 3;
            return cfg;
        });

    auto validatorAKey = SecretKey::fromSeed(sha256("validator-A"));
    auto validatorBKey = SecretKey::fromSeed(sha256("validator-B"));
    auto validatorCKey = SecretKey::fromSeed(sha256("validator-C"));

    SCPQuorumSet qset;
    qset.threshold = 3;
    qset.validators.push_back(validatorAKey.getPublicKey());
    qset.validators.push_back(validatorBKey.getPublicKey());
    qset.validators.push_back(validatorCKey.getPublicKey());

    simulation->addNode(validatorAKey, qset);
    simulation->addNode(validatorBKey, qset);
    simulation->addNode(validatorCKey, qset);

    simulation->addPendingConnection(validatorAKey.getPublicKey(),
                                     validatorCKey.getPublicKey());
    simulation->addPendingConnection(validatorAKey.getPublicKey(),
                                     validatorBKey.getPublicKey());

    simulation->startAllNodes();
    auto node = simulation->getNode(validatorCKey.getPublicKey());

    // Crank some ledgers so that we have SCP messages
    auto ledgers = node->getConfig().MAX_SLOTS_TO_REMEMBER + 1;
    simulation->crankUntil(
        [&]() { return simulation->haveAllExternalized(ledgers, 1); },
        2 * ledgers * Herder::EXP_LEDGER_TIMESPAN_SECONDS, false);

    auto conn = simulation->getLoopbackConnection(validatorAKey.getPublicKey(),
                                                  validatorCKey.getPublicKey());
    REQUIRE(conn);
    auto peer = conn->getAcceptor();

    auto& scpQueue = conn->getAcceptor()->getQueues()[0];
    auto& txQueue = conn->getAcceptor()->getQueues()[1];
    auto& demandQueue = conn->getAcceptor()->getQueues()[2];
    auto& advertQueue = conn->getAcceptor()->getQueues()[3];

    // Clear queues for testing
    scpQueue.clear();
    txQueue.clear();
    demandQueue.clear();
    advertQueue.clear();

    auto lcl = node->getLedgerManager().getLastClosedLedgerNum();
    HerderImpl& herder = *static_cast<HerderImpl*>(&node->getHerder());
    auto envs = herder.getSCP().getLatestMessagesSend(lcl);
    REQUIRE(!envs.empty());

    auto constructSCPMsg = [&](SCPEnvelope const& env) {
        HcnetMessage msg;
        msg.type(SCP_MESSAGE);
        msg.envelope() = env;
        return std::make_shared<HcnetMessage const>(msg);
    };

    SECTION("SCP messages, slot too old")
    {
        for (auto& env : envs)
        {
            env.statement.slotIndex =
                lcl - node->getConfig().MAX_SLOTS_TO_REMEMBER;
            constructSCPMsg(env);
            peer->addMsgAndMaybeTrimQueue(constructSCPMsg(env));
        }
        REQUIRE(scpQueue.empty());
    }
    SECTION("txs, limit reached")
    {
        uint32_t limit = node->getLedgerManager().getLastMaxTxSetSizeOps();
        for (uint32_t i = 0; i < limit + 10; ++i)
        {
            HcnetMessage msg;
            msg.type(TRANSACTION);
            peer->addMsgAndMaybeTrimQueue(
                std::make_shared<HcnetMessage const>(msg));
        }

        REQUIRE(txQueue.size() == limit);
    }
    SECTION("obsolete SCP messages")
    {
        SECTION("only latest messages, no trimming")
        {
            for (auto& env : envs)
            {
                peer->addMsgAndMaybeTrimQueue(constructSCPMsg(env));
            }

            // Only latest SCP messages, nothing is trimmed
            REQUIRE(scpQueue.size() == envs.size());
        }
        SECTION("trim obsolete messages")
        {
            auto injectPrepareMsgs = [&](std::vector<SCPEnvelope> envs) {
                for (auto& env : envs)
                {
                    if (env.statement.pledges.type() == SCP_ST_EXTERNALIZE)
                    {
                        // Insert a message that's guaranteed to be older
                        // (prepare vs externalize)
                        auto envCopy = env;
                        envCopy.statement.pledges.type(SCP_ST_PREPARE);

                        peer->addMsgAndMaybeTrimQueue(constructSCPMsg(envCopy));
                    }
                    peer->addMsgAndMaybeTrimQueue(constructSCPMsg(env));
                }
            };
            SECTION("trim prepare, keep nomination")
            {
                injectPrepareMsgs(envs);

                // prepare got dropped
                REQUIRE(scpQueue.size() == 2);
                REQUIRE(
                    scpQueue[0].mMessage->envelope().statement.pledges.type() ==
                    SCP_ST_NOMINATE);
                REQUIRE(
                    scpQueue[1].mMessage->envelope().statement.pledges.type() ==
                    SCP_ST_EXTERNALIZE);
            }
            SECTION("trim prepare, keep messages from other nodes")
            {
                // Get ballot protocol messages from all nodes
                auto msgs = herder.getSCP().getExternalizingState(lcl);
                auto hintMsg = msgs.back();
                injectPrepareMsgs(msgs);

                // 3 externalize messages remaining
                REQUIRE(scpQueue.size() == 3);
                REQUIRE(std::all_of(scpQueue.begin(), scpQueue.end(),
                                    [&](auto const& item) {
                                        return item.mMessage->envelope()
                                                   .statement.pledges.type() ==
                                               SCP_ST_EXTERNALIZE;
                                    }));
            }
        }
    }
    SECTION("advert demand limit reached")
    {
        uint32_t limit = node->getLedgerManager().getLastMaxTxSetSizeOps();
        for (uint32_t i = 0; i < limit + 10; ++i)
        {
            HcnetMessage adv, dem, txn;
            adv.type(FLOOD_ADVERT);
            dem.type(FLOOD_DEMAND);
            adv.floodAdvert().txHashes.push_back(xdrSha256(txn));
            dem.floodDemand().txHashes.push_back(xdrSha256(txn));
            peer->addMsgAndMaybeTrimQueue(
                std::make_shared<HcnetMessage const>(adv));
            peer->addMsgAndMaybeTrimQueue(
                std::make_shared<HcnetMessage const>(dem));
        }

        REQUIRE(advertQueue.size() == limit);
        REQUIRE(demandQueue.size() == limit);

        HcnetMessage adv, dem, txn;
        adv.type(FLOOD_ADVERT);
        dem.type(FLOOD_DEMAND);
        for (auto i = 0; i < 2; i++)
        {
            adv.floodAdvert().txHashes.push_back(xdrSha256(txn));
            dem.floodDemand().txHashes.push_back(xdrSha256(txn));
        }

        peer->addMsgAndMaybeTrimQueue(
            std::make_shared<HcnetMessage const>(adv));
        peer->addMsgAndMaybeTrimQueue(
            std::make_shared<HcnetMessage const>(dem));

        REQUIRE(advertQueue.size() == limit - 1);
        REQUIRE(demandQueue.size() == limit - 1);
    }
}

TEST_CASE("reject non preferred peer", "[overlay][connections]")
{
    VirtualClock clock;
    Config const& cfg1 = getTestConfig(0);
    Config cfg2 = getTestConfig(1);

    cfg2.PREFERRED_PEERS_ONLY = true;

    auto app1 = createTestApplication(clock, cfg1);
    auto app2 = createTestApplication(clock, cfg2);

    SECTION("inbound")
    {
        LoopbackPeerConnection conn(*app1, *app2);
        testutil::crankSome(clock);

        REQUIRE(!conn.getInitiator()->isConnected());
        REQUIRE(!conn.getAcceptor()->isConnected());
        REQUIRE(conn.getAcceptor()->getDropReason() == "peer rejected");

        REQUIRE(knowsAsOutbound(*app1, *app2));
        REQUIRE(knowsAsInbound(*app2, *app1));

        testutil::shutdownWorkScheduler(*app2);
        testutil::shutdownWorkScheduler(*app1);
    }

    SECTION("outbound")
    {
        LoopbackPeerConnection conn(*app2, *app1);
        testutil::crankSome(clock);

        REQUIRE(!conn.getInitiator()->isConnected());
        REQUIRE(!conn.getAcceptor()->isConnected());
        REQUIRE(conn.getInitiator()->getDropReason() == "peer rejected");

        REQUIRE(knowsAsInbound(*app1, *app2));
        REQUIRE(knowsAsOutbound(*app2, *app1));

        testutil::shutdownWorkScheduler(*app2);
        testutil::shutdownWorkScheduler(*app1);
    }
}

TEST_CASE("accept preferred peer even when strict", "[overlay][connections]")
{
    VirtualClock clock;
    Config const& cfg1 = getTestConfig(0);
    Config cfg2 = getTestConfig(1);

    cfg2.PREFERRED_PEERS_ONLY = true;
    cfg2.PREFERRED_PEER_KEYS.emplace(cfg1.NODE_SEED.getPublicKey());

    auto app1 = createTestApplication(clock, cfg1);
    auto app2 = createTestApplication(clock, cfg2);

    SECTION("inbound")
    {
        LoopbackPeerConnection conn(*app1, *app2);
        testutil::crankSome(clock);

        REQUIRE(conn.getInitiator()->isAuthenticated());
        REQUIRE(conn.getAcceptor()->isAuthenticated());

        REQUIRE(knowsAsOutbound(*app1, *app2));
        REQUIRE(knowsAsInbound(*app2, *app1));

        testutil::shutdownWorkScheduler(*app2);
        testutil::shutdownWorkScheduler(*app1);
    }

    SECTION("outbound")
    {
        LoopbackPeerConnection conn(*app2, *app1);
        testutil::crankSome(clock);

        REQUIRE(conn.getInitiator()->isAuthenticated());
        REQUIRE(conn.getAcceptor()->isAuthenticated());

        REQUIRE(knowsAsInbound(*app1, *app2));
        REQUIRE(knowsAsOutbound(*app2, *app1));

        testutil::shutdownWorkScheduler(*app2);
        testutil::shutdownWorkScheduler(*app1);
    }
}

TEST_CASE("reject peers beyond max", "[overlay][connections]")
{
    VirtualClock clock;
    Config const& cfg1 = getTestConfig(0);
    Config cfg2 = getTestConfig(1);
    Config const& cfg3 = getTestConfig(2);

    SECTION("inbound")
    {
        cfg2.MAX_ADDITIONAL_PEER_CONNECTIONS = 1;
        cfg2.TARGET_PEER_CONNECTIONS = 0;

        auto app1 = createTestApplication(clock, cfg1);
        auto app2 = createTestApplication(clock, cfg2);
        auto app3 = createTestApplication(clock, cfg3);

        LoopbackPeerConnection conn1(*app1, *app2);
        LoopbackPeerConnection conn2(*app3, *app2);
        testutil::crankSome(clock);

        REQUIRE(conn1.getInitiator()->isConnected());
        REQUIRE(conn1.getAcceptor()->isConnected());
        REQUIRE(!conn2.getInitiator()->isConnected());
        REQUIRE(!conn2.getAcceptor()->isConnected());
        REQUIRE(conn2.getAcceptor()->getDropReason() == "peer rejected");

        REQUIRE(knowsAsOutbound(*app1, *app2));
        REQUIRE(knowsAsInbound(*app2, *app1));
        REQUIRE(knowsAsOutbound(*app3, *app2));
        REQUIRE(knowsAsInbound(*app2, *app3));

        testutil::shutdownWorkScheduler(*app3);
        testutil::shutdownWorkScheduler(*app2);
        testutil::shutdownWorkScheduler(*app1);
    }

    SECTION("outbound")
    {
        cfg2.MAX_ADDITIONAL_PEER_CONNECTIONS = 0;
        cfg2.TARGET_PEER_CONNECTIONS = 1;

        auto app1 = createTestApplication(clock, cfg1);
        auto app2 = createTestApplication(clock, cfg2);
        auto app3 = createTestApplication(clock, cfg3);

        LoopbackPeerConnection conn1(*app2, *app1);
        LoopbackPeerConnection conn2(*app2, *app3);
        testutil::crankSome(clock);

        REQUIRE(conn1.getInitiator()->isConnected());
        REQUIRE(conn1.getAcceptor()->isConnected());
        REQUIRE(!conn2.getInitiator()->isConnected());
        REQUIRE(!conn2.getAcceptor()->isConnected());
        REQUIRE(conn2.getInitiator()->getDropReason() == "peer rejected");

        REQUIRE(knowsAsInbound(*app1, *app2));
        REQUIRE(knowsAsOutbound(*app2, *app1));
        REQUIRE(knowsAsInbound(*app3, *app2));
        REQUIRE(knowsAsOutbound(*app2, *app3));

        testutil::shutdownWorkScheduler(*app3);
        testutil::shutdownWorkScheduler(*app2);
        testutil::shutdownWorkScheduler(*app1);
    }
}

TEST_CASE("reject peers beyond max - preferred peer wins",
          "[overlay][connections]")
{
    VirtualClock clock;
    Config const& cfg1 = getTestConfig(0);
    Config cfg2 = getTestConfig(1);
    Config const& cfg3 = getTestConfig(2);

    SECTION("preferred connects first")
    {
        SECTION("inbound")
        {
            cfg2.MAX_ADDITIONAL_PEER_CONNECTIONS = 1;
            cfg2.TARGET_PEER_CONNECTIONS = 0;
            cfg2.PREFERRED_PEER_KEYS.emplace(cfg3.NODE_SEED.getPublicKey());

            auto app1 = createTestApplication(clock, cfg1);
            auto app2 = createTestApplication(clock, cfg2);
            auto app3 = createTestApplication(clock, cfg3);

            LoopbackPeerConnection conn2(*app3, *app2);
            LoopbackPeerConnection conn1(*app1, *app2);
            testutil::crankSome(clock);

            REQUIRE(!conn1.getInitiator()->isConnected());
            REQUIRE(!conn1.getAcceptor()->isConnected());
            REQUIRE(conn2.getInitiator()->isConnected());
            REQUIRE(conn2.getAcceptor()->isConnected());
            REQUIRE(conn1.getAcceptor()->getDropReason() == "peer rejected");

            REQUIRE(knowsAsOutbound(*app1, *app2));
            REQUIRE(knowsAsInbound(*app2, *app1));
            REQUIRE(knowsAsOutbound(*app3, *app2));
            REQUIRE(knowsAsInbound(*app2, *app3));

            testutil::shutdownWorkScheduler(*app3);
            testutil::shutdownWorkScheduler(*app2);
            testutil::shutdownWorkScheduler(*app1);
        }

        SECTION("outbound")
        {
            cfg2.MAX_ADDITIONAL_PEER_CONNECTIONS = 0;
            cfg2.TARGET_PEER_CONNECTIONS = 1;
            cfg2.PREFERRED_PEER_KEYS.emplace(cfg3.NODE_SEED.getPublicKey());

            auto app1 = createTestApplication(clock, cfg1);
            auto app2 = createTestApplication(clock, cfg2);
            auto app3 = createTestApplication(clock, cfg3);

            LoopbackPeerConnection conn2(*app2, *app3);
            LoopbackPeerConnection conn1(*app2, *app1);
            testutil::crankSome(clock);

            REQUIRE(!conn1.getInitiator()->isConnected());
            REQUIRE(!conn1.getAcceptor()->isConnected());
            REQUIRE(conn2.getInitiator()->isConnected());
            REQUIRE(conn2.getAcceptor()->isConnected());
            REQUIRE(conn1.getInitiator()->getDropReason() == "peer rejected");

            REQUIRE(knowsAsInbound(*app1, *app2));
            REQUIRE(knowsAsOutbound(*app2, *app1));
            REQUIRE(knowsAsInbound(*app3, *app2));
            REQUIRE(knowsAsOutbound(*app2, *app3));

            testutil::shutdownWorkScheduler(*app3);
            testutil::shutdownWorkScheduler(*app2);
            testutil::shutdownWorkScheduler(*app1);
        }
    }

    SECTION("preferred connects second")
    {
        SECTION("inbound")
        {
            cfg2.MAX_ADDITIONAL_PEER_CONNECTIONS = 1;
            cfg2.TARGET_PEER_CONNECTIONS = 0;
            cfg2.PREFERRED_PEER_KEYS.emplace(cfg3.NODE_SEED.getPublicKey());

            auto app1 = createTestApplication(clock, cfg1);
            auto app2 = createTestApplication(clock, cfg2);
            auto app3 = createTestApplication(clock, cfg3);

            LoopbackPeerConnection conn1(*app1, *app2);
            LoopbackPeerConnection conn2(*app3, *app2);
            testutil::crankSome(clock);

            REQUIRE(!conn1.getInitiator()->isConnected());
            REQUIRE(!conn1.getAcceptor()->isConnected());
            REQUIRE(conn2.getInitiator()->isConnected());
            REQUIRE(conn2.getAcceptor()->isConnected());
            REQUIRE(conn1.getAcceptor()->getDropReason() ==
                    "preferred peer selected instead");

            REQUIRE(knowsAsOutbound(*app1, *app2));
            REQUIRE(knowsAsInbound(*app2, *app1));
            REQUIRE(knowsAsOutbound(*app3, *app2));
            REQUIRE(knowsAsInbound(*app2, *app3));

            testutil::shutdownWorkScheduler(*app3);
            testutil::shutdownWorkScheduler(*app2);
            testutil::shutdownWorkScheduler(*app1);
        }

        SECTION("outbound")
        {
            cfg2.MAX_ADDITIONAL_PEER_CONNECTIONS = 0;
            cfg2.TARGET_PEER_CONNECTIONS = 1;
            cfg2.PREFERRED_PEER_KEYS.emplace(cfg3.NODE_SEED.getPublicKey());

            auto app1 = createTestApplication(clock, cfg1);
            auto app2 = createTestApplication(clock, cfg2);
            auto app3 = createTestApplication(clock, cfg3);

            LoopbackPeerConnection conn1(*app2, *app1);
            LoopbackPeerConnection conn2(*app2, *app3);
            testutil::crankSome(clock);

            REQUIRE(!conn1.getInitiator()->isConnected());
            REQUIRE(!conn1.getAcceptor()->isConnected());
            REQUIRE(conn2.getInitiator()->isConnected());
            REQUIRE(conn2.getAcceptor()->isConnected());
            REQUIRE(conn1.getInitiator()->getDropReason() ==
                    "preferred peer selected instead");

            REQUIRE(knowsAsInbound(*app1, *app2));
            REQUIRE(knowsAsOutbound(*app2, *app1));
            REQUIRE(knowsAsInbound(*app3, *app2));
            REQUIRE(knowsAsOutbound(*app2, *app3));

            testutil::shutdownWorkScheduler(*app3);
            testutil::shutdownWorkScheduler(*app2);
            testutil::shutdownWorkScheduler(*app1);
        }
    }
}

TEST_CASE("allow inbound pending peers up to max", "[overlay][connections]")
{
    VirtualClock clock;
    Config const& cfg1 = getTestConfig(0);
    Config cfg2 = getTestConfig(1);
    Config const& cfg3 = getTestConfig(2);
    Config const& cfg4 = getTestConfig(3);
    Config const& cfg5 = getTestConfig(4);

    cfg2.MAX_INBOUND_PENDING_CONNECTIONS = 3;
    cfg2.MAX_OUTBOUND_PENDING_CONNECTIONS = 3;

    auto app1 = createTestApplication(clock, cfg1);
    auto app2 = createTestApplication(clock, cfg2);
    auto app3 = createTestApplication(clock, cfg3);
    auto app4 = createTestApplication(clock, cfg4);
    auto app5 = createTestApplication(clock, cfg5);

    LoopbackPeerConnection conn1(*app1, *app2);
    REQUIRE(conn1.getInitiator()->getState() == Peer::CONNECTED);
    REQUIRE(conn1.getAcceptor()->getState() == Peer::CONNECTED);
    conn1.getInitiator()->setCorked(true);

    LoopbackPeerConnection conn2(*app3, *app2);
    REQUIRE(conn2.getInitiator()->getState() == Peer::CONNECTED);
    REQUIRE(conn2.getAcceptor()->getState() == Peer::CONNECTED);
    conn2.getInitiator()->setCorked(true);

    LoopbackPeerConnection conn3(*app4, *app2);
    REQUIRE(conn3.getInitiator()->getState() == Peer::CONNECTED);
    REQUIRE(conn3.getAcceptor()->getState() == Peer::CONNECTED);

    LoopbackPeerConnection conn4(*app5, *app2);
    REQUIRE(conn4.getInitiator()->getState() == Peer::CONNECTED);
    REQUIRE(conn4.getAcceptor()->getState() == Peer::CLOSING);

    // Must wait for RECURRENT_TIMER_PERIOD
    testutil::crankFor(clock, std::chrono::seconds(5));

    REQUIRE(conn1.getInitiator()->getState() == Peer::CLOSING);
    REQUIRE(conn1.getAcceptor()->getState() == Peer::CLOSING);
    REQUIRE(conn2.getInitiator()->getState() == Peer::CLOSING);
    REQUIRE(conn2.getAcceptor()->getState() == Peer::CLOSING);
    REQUIRE(conn3.getInitiator()->isConnected());
    REQUIRE(conn3.getAcceptor()->isConnected());
    REQUIRE(conn4.getInitiator()->getState() == Peer::CLOSING);
    REQUIRE(conn4.getAcceptor()->getState() == Peer::CLOSING);
    REQUIRE(app2->getMetrics()
                .NewMeter({"overlay", "timeout", "idle"}, "timeout")
                .count() == 2);

    REQUIRE(doesNotKnow(*app1, *app2)); // corked
    REQUIRE(doesNotKnow(*app2, *app1)); // corked
    REQUIRE(doesNotKnow(*app3, *app2)); // corked
    REQUIRE(doesNotKnow(*app2, *app3)); // corked
    REQUIRE(knowsAsOutbound(*app4, *app2));
    REQUIRE(knowsAsInbound(*app2, *app4));
    REQUIRE(doesNotKnow(*app5, *app2)); // didn't get to hello phase
    REQUIRE(doesNotKnow(*app2, *app5)); // didn't get to hello phase

    testutil::shutdownWorkScheduler(*app5);
    testutil::shutdownWorkScheduler(*app4);
    testutil::shutdownWorkScheduler(*app3);
    testutil::shutdownWorkScheduler(*app2);
    testutil::shutdownWorkScheduler(*app1);
}

TEST_CASE("allow inbound pending peers over max if possibly preferred",
          "[overlay][connections]")
{
    VirtualClock clock;
    Config const& cfg1 = getTestConfig(0);
    Config cfg2 = getTestConfig(1);
    Config const& cfg3 = getTestConfig(2);
    Config const& cfg4 = getTestConfig(3);
    Config const& cfg5 = getTestConfig(4);

    cfg2.MAX_INBOUND_PENDING_CONNECTIONS = 3;
    cfg2.MAX_OUTBOUND_PENDING_CONNECTIONS = 3;
    cfg2.PREFERRED_PEERS.emplace_back("127.0.0.1:17");

    auto app1 = createTestApplication(clock, cfg1);
    auto app2 = createTestApplication(clock, cfg2);
    auto app3 = createTestApplication(clock, cfg3);
    auto app4 = createTestApplication(clock, cfg4);
    auto app5 = createTestApplication(clock, cfg5);

    (static_cast<OverlayManagerImpl&>(app2->getOverlayManager()))
        .storeConfigPeers();

    LoopbackPeerConnection conn1(*app1, *app2);
    REQUIRE(conn1.getInitiator()->getState() == Peer::CONNECTED);
    REQUIRE(conn1.getAcceptor()->getState() == Peer::CONNECTED);
    conn1.getInitiator()->setCorked(true);

    LoopbackPeerConnection conn2(*app3, *app2);
    REQUIRE(conn2.getInitiator()->getState() == Peer::CONNECTED);
    REQUIRE(conn2.getAcceptor()->getState() == Peer::CONNECTED);
    conn2.getInitiator()->setCorked(true);

    LoopbackPeerConnection conn3(*app4, *app2);
    REQUIRE(conn3.getInitiator()->getState() == Peer::CONNECTED);
    REQUIRE(conn3.getAcceptor()->getState() == Peer::CONNECTED);

    LoopbackPeerConnection conn4(*app5, *app2);
    REQUIRE(conn4.getInitiator()->getState() == Peer::CONNECTED);
    REQUIRE(conn4.getAcceptor()->getState() == Peer::CONNECTED);

    // Must wait for RECURRENT_TIMER_PERIOD
    testutil::crankFor(clock, std::chrono::seconds(5));

    REQUIRE(conn1.getInitiator()->getState() == Peer::CLOSING);
    REQUIRE(conn1.getAcceptor()->getState() == Peer::CLOSING);
    REQUIRE(conn2.getInitiator()->getState() == Peer::CLOSING);
    REQUIRE(conn2.getAcceptor()->getState() == Peer::CLOSING);
    REQUIRE(conn3.getInitiator()->isConnected());
    REQUIRE(conn3.getAcceptor()->isConnected());
    REQUIRE(conn4.getInitiator()->isConnected());
    REQUIRE(conn4.getAcceptor()->isConnected());
    REQUIRE(app2->getMetrics()
                .NewMeter({"overlay", "timeout", "idle"}, "timeout")
                .count() == 2);
    REQUIRE(app2->getMetrics()
                .NewMeter({"overlay", "connection", "reject"}, "connection")
                .count() == 0);

    REQUIRE(doesNotKnow(*app1, *app2)); // corked
    REQUIRE(doesNotKnow(*app2, *app1)); // corked
    REQUIRE(doesNotKnow(*app3, *app2)); // corked
    REQUIRE(doesNotKnow(*app2, *app3)); // corked
    REQUIRE(knowsAsOutbound(*app4, *app2));
    REQUIRE(knowsAsInbound(*app2, *app4));
    REQUIRE(knowsAsOutbound(*app5, *app2));
    REQUIRE(knowsAsInbound(*app2, *app5));

    testutil::shutdownWorkScheduler(*app5);
    testutil::shutdownWorkScheduler(*app4);
    testutil::shutdownWorkScheduler(*app3);
    testutil::shutdownWorkScheduler(*app2);
    testutil::shutdownWorkScheduler(*app1);
}

TEST_CASE("allow outbound pending peers up to max", "[overlay][connections]")
{
    VirtualClock clock;
    Config const& cfg1 = getTestConfig(0);
    Config cfg2 = getTestConfig(1);
    Config const& cfg3 = getTestConfig(2);
    Config const& cfg4 = getTestConfig(3);
    Config const& cfg5 = getTestConfig(4);

    cfg2.MAX_INBOUND_PENDING_CONNECTIONS = 3;
    cfg2.MAX_OUTBOUND_PENDING_CONNECTIONS = 3;

    auto app1 = createTestApplication(clock, cfg1);
    auto app2 = createTestApplication(clock, cfg2);
    auto app3 = createTestApplication(clock, cfg3);
    auto app4 = createTestApplication(clock, cfg4);
    auto app5 = createTestApplication(clock, cfg5);

    LoopbackPeerConnection conn1(*app2, *app1);
    REQUIRE(conn1.getInitiator()->getState() == Peer::CONNECTED);
    REQUIRE(conn1.getAcceptor()->getState() == Peer::CONNECTED);
    conn1.getInitiator()->setCorked(true);

    LoopbackPeerConnection conn2(*app2, *app3);
    REQUIRE(conn2.getInitiator()->getState() == Peer::CONNECTED);
    REQUIRE(conn2.getAcceptor()->getState() == Peer::CONNECTED);
    conn2.getInitiator()->setCorked(true);

    LoopbackPeerConnection conn3(*app2, *app4);
    REQUIRE(conn3.getInitiator()->getState() == Peer::CONNECTED);
    REQUIRE(conn3.getAcceptor()->getState() == Peer::CONNECTED);

    LoopbackPeerConnection conn4(*app2, *app5);
    REQUIRE(conn4.getInitiator()->getState() == Peer::CLOSING);
    REQUIRE(conn4.getAcceptor()->getState() == Peer::CONNECTED);
    conn2.getInitiator()->setCorked(true);

    // Must wait for RECURRENT_TIMER_PERIOD
    testutil::crankFor(clock, std::chrono::seconds(5));

    REQUIRE(conn1.getInitiator()->getState() == Peer::CLOSING);
    REQUIRE(conn1.getAcceptor()->getState() == Peer::CLOSING);
    REQUIRE(conn2.getInitiator()->getState() == Peer::CLOSING);
    REQUIRE(conn2.getAcceptor()->getState() == Peer::CLOSING);
    REQUIRE(conn3.getInitiator()->isConnected());
    REQUIRE(conn3.getAcceptor()->isConnected());
    REQUIRE(conn4.getInitiator()->getState() == Peer::CLOSING);
    REQUIRE(conn4.getAcceptor()->getState() == Peer::CLOSING);
    REQUIRE(app2->getMetrics()
                .NewMeter({"overlay", "timeout", "idle"}, "timeout")
                .count() == 2);

    REQUIRE(doesNotKnow(*app1, *app2)); // corked
    REQUIRE(doesNotKnow(*app2, *app1)); // corked
    REQUIRE(doesNotKnow(*app3, *app2)); // corked
    REQUIRE(doesNotKnow(*app2, *app3)); // corked
    REQUIRE(knowsAsInbound(*app4, *app2));
    REQUIRE(knowsAsOutbound(*app2, *app4));
    REQUIRE(doesNotKnow(*app5, *app2)); // corked
    REQUIRE(doesNotKnow(*app2, *app5)); // corked

    testutil::shutdownWorkScheduler(*app5);
    testutil::shutdownWorkScheduler(*app4);
    testutil::shutdownWorkScheduler(*app3);
    testutil::shutdownWorkScheduler(*app2);
    testutil::shutdownWorkScheduler(*app1);
}

TEST_CASE("reject peers with differing network passphrases",
          "[overlay][connections]")
{
    VirtualClock clock;
    Config const& cfg1 = getTestConfig(0);
    Config cfg2 = getTestConfig(1);

    cfg2.NETWORK_PASSPHRASE = "nothing to see here";

    auto app1 = createTestApplication(clock, cfg1);
    auto app2 = createTestApplication(clock, cfg2);

    LoopbackPeerConnection conn(*app1, *app2);
    testutil::crankSome(clock);

    REQUIRE(!conn.getInitiator()->isConnected());
    REQUIRE(!conn.getAcceptor()->isConnected());

    REQUIRE(doesNotKnow(*app1, *app2));
    REQUIRE(doesNotKnow(*app2, *app1));

    testutil::shutdownWorkScheduler(*app2);
    testutil::shutdownWorkScheduler(*app1);
}

TEST_CASE("reject peers with invalid cert", "[overlay][connections]")
{
    VirtualClock clock;
    Config const& cfg1 = getTestConfig(0);
    Config cfg2 = getTestConfig(1);

    auto app1 = createTestApplication(clock, cfg1);
    auto app2 = createTestApplication(clock, cfg2);

    LoopbackPeerConnection conn(*app1, *app2);
    conn.getAcceptor()->setDamageCert(true);
    testutil::crankSome(clock);

    REQUIRE(!conn.getInitiator()->isConnected());
    REQUIRE(!conn.getAcceptor()->isConnected());

    REQUIRE(doesNotKnow(*app1, *app2));
    REQUIRE(knowsAsInbound(*app2, *app1));

    testutil::shutdownWorkScheduler(*app2);
    testutil::shutdownWorkScheduler(*app1);
}

TEST_CASE("reject banned peers", "[overlay][connections]")
{
    VirtualClock clock;
    Config const& cfg1 = getTestConfig(0);
    Config cfg2 = getTestConfig(1);

    auto app1 = createTestApplication(clock, cfg1);
    auto app2 = createTestApplication(clock, cfg2);
    app1->getBanManager().banNode(cfg2.NODE_SEED.getPublicKey());

    LoopbackPeerConnection conn(*app1, *app2);
    testutil::crankSome(clock);

    REQUIRE(!conn.getInitiator()->isConnected());
    REQUIRE(!conn.getAcceptor()->isConnected());

    REQUIRE(doesNotKnow(*app1, *app2));
    REQUIRE(knowsAsInbound(*app2, *app1));

    testutil::shutdownWorkScheduler(*app2);
    testutil::shutdownWorkScheduler(*app1);
}

TEST_CASE("reject peers with incompatible overlay versions",
          "[overlay][connections]")
{
    Config const& cfg1 = getTestConfig(0);

    auto doVersionCheck = [&](uint32 version) {
        VirtualClock clock;
        Config cfg2 = getTestConfig(1);

        cfg2.OVERLAY_PROTOCOL_MIN_VERSION = version;
        cfg2.OVERLAY_PROTOCOL_VERSION = version;
        auto app1 = createTestApplication(clock, cfg1);
        auto app2 = createTestApplication(clock, cfg2);

        LoopbackPeerConnection conn(*app1, *app2);
        testutil::crankSome(clock);

        REQUIRE(!conn.getInitiator()->isConnected());
        REQUIRE(!conn.getAcceptor()->isConnected());
        REQUIRE(conn.getInitiator()->getDropReason() ==
                "wrong protocol version");

        REQUIRE(doesNotKnow(*app1, *app2));
        REQUIRE(doesNotKnow(*app2, *app1));

        testutil::shutdownWorkScheduler(*app2);
        testutil::shutdownWorkScheduler(*app1);
    };
    SECTION("cfg2 above")
    {
        doVersionCheck(cfg1.OVERLAY_PROTOCOL_VERSION + 1);
    }
    SECTION("cfg2 below")
    {
        doVersionCheck(cfg1.OVERLAY_PROTOCOL_MIN_VERSION - 1);
    }
}

TEST_CASE("reject peers who dont handshake quickly", "[overlay][connections]")
{
    auto test = [](unsigned short authenticationTimeout) {
        Config cfg1 = getTestConfig(1);
        Config cfg2 = getTestConfig(2);

        cfg1.PEER_AUTHENTICATION_TIMEOUT = authenticationTimeout;
        cfg2.PEER_AUTHENTICATION_TIMEOUT = authenticationTimeout;

        auto networkID = sha256(getTestConfig().NETWORK_PASSPHRASE);
        auto sim =
            std::make_shared<Simulation>(Simulation::OVER_LOOPBACK, networkID);

        SIMULATION_CREATE_NODE(Node1);
        SIMULATION_CREATE_NODE(Node2);
        sim->addNode(vNode1SecretKey, cfg1.QUORUM_SET, &cfg1);
        sim->addNode(vNode2SecretKey, cfg2.QUORUM_SET, &cfg2);
        auto waitTime = std::chrono::seconds(authenticationTimeout + 1);
        auto padTime = std::chrono::seconds(2);

        sim->addPendingConnection(vNode1NodeID, vNode2NodeID);

        sim->startAllNodes();

        auto conn = sim->getLoopbackConnection(vNode1NodeID, vNode2NodeID);

        conn->getInitiator()->setCorked(true);

        sim->crankForAtLeast(waitTime + padTime, false);

        sim->crankUntil(
            [&]() {
                return !(conn->getInitiator()->isConnected() ||
                         conn->getAcceptor()->isConnected());
            },
            padTime, true);

        auto app1 = sim->getNode(vNode1NodeID);
        auto app2 = sim->getNode(vNode2NodeID);

        auto idle1 = app1->getMetrics()
                         .NewMeter({"overlay", "timeout", "idle"}, "timeout")
                         .count();
        auto idle2 = app2->getMetrics()
                         .NewMeter({"overlay", "timeout", "idle"}, "timeout")
                         .count();

        REQUIRE((idle1 != 0 || idle2 != 0));

        REQUIRE(doesNotKnow(*app1, *app2));
        REQUIRE(doesNotKnow(*app2, *app1));
    };

    SECTION("2 seconds timeout")
    {
        test(2);
    }

    SECTION("5 seconds timeout")
    {
        test(5);
    }
}

TEST_CASE("drop peers who straggle", "[overlay][connections][straggler]")
{
    auto test = [](unsigned short stragglerTimeout) {
        VirtualClock clock;
        Config cfg1 = getTestConfig(0);
        Config cfg2 = getTestConfig(1);

        // Straggler detection piggy-backs on the idle timer so we drive
        // the test from idle-timer-firing granularity.
        assert(cfg1.PEER_TIMEOUT == cfg2.PEER_TIMEOUT);
        assert(stragglerTimeout >= cfg1.PEER_TIMEOUT * 2);

        // Initiator (cfg1) will straggle, and acceptor (cfg2) will notice and
        // disconnect.
        cfg2.PEER_STRAGGLER_TIMEOUT = stragglerTimeout;

        auto app1 = createTestApplication(clock, cfg1);
        auto app2 = createTestApplication(clock, cfg2);
        auto waitTime = std::chrono::seconds(stragglerTimeout * 3);
        auto padTime = std::chrono::seconds(5);

        LoopbackPeerConnection conn(*app1, *app2);
        auto start = clock.now();

        testutil::crankSome(clock);
        REQUIRE(conn.getInitiator()->isAuthenticated());
        REQUIRE(conn.getAcceptor()->isAuthenticated());

        conn.getInitiator()->setStraggling(true);
        auto straggler = conn.getInitiator();
        VirtualTimer sendTimer(*app1);

        while (clock.now() < (start + waitTime) &&
               (conn.getInitiator()->isConnected() ||
                conn.getAcceptor()->isConnected()))
        {
            // Straggler keeps asking for peers once per second -- this is
            // easy traffic to fake-generate -- but not accepting response
            // messages in a timely fashion.
            std::chrono::seconds const dur{1};
            sendTimer.expires_from_now(dur);
            sendTimer.async_wait([straggler](asio::error_code const& error) {
                if (!error)
                {
                    straggler->sendGetPeers();
                }
            });
            testutil::crankFor(clock, dur);
        }
        LOG_INFO(DEFAULT_LOG, "loop complete, clock.now() = {}",
                 clock.now().time_since_epoch().count());
        REQUIRE(clock.now() < (start + waitTime + padTime));
        REQUIRE(!conn.getInitiator()->isConnected());
        REQUIRE(!conn.getAcceptor()->isConnected());
        REQUIRE(app1->getMetrics()
                    .NewMeter({"overlay", "timeout", "idle"}, "timeout")
                    .count() == 0);
        REQUIRE(app2->getMetrics()
                    .NewMeter({"overlay", "timeout", "idle"}, "timeout")
                    .count() == 0);
        REQUIRE(app2->getMetrics()
                    .NewMeter({"overlay", "timeout", "straggler"}, "timeout")
                    .count() != 0);

        testutil::shutdownWorkScheduler(*app2);
        testutil::shutdownWorkScheduler(*app1);
    };

    SECTION("60 seconds straggle timeout")
    {
        test(60);
    }

    SECTION("120 seconds straggle timeout")
    {
        test(120);
    }

    SECTION("150 seconds straggle timeout")
    {
        test(150);
    }
}

TEST_CASE("reject peers with the same nodeid", "[overlay][connections]")
{
    VirtualClock clock;
    Config const& cfg1 = getTestConfig(1);
    Config cfg2 = getTestConfig(2);

    cfg2.NODE_SEED = cfg1.NODE_SEED;

    auto app1 = createTestApplication(clock, cfg1);
    auto app2 = createTestApplication(clock, cfg2);

    SECTION("inbound")
    {
        LoopbackPeerConnection conn(*app1, *app2);
        testutil::crankSome(clock);

        REQUIRE(conn.getInitiator()->getDropReason() == "connecting to self");
    }

    SECTION("outbound")
    {
        LoopbackPeerConnection conn(*app2, *app1);
        testutil::crankSome(clock);

        REQUIRE(conn.getInitiator()->getDropReason() == "connecting to self");
    }

    testutil::shutdownWorkScheduler(*app2);
    testutil::shutdownWorkScheduler(*app1);
}

TEST_CASE("connecting to saturated nodes", "[overlay][connections][acceptance]")
{
    auto networkID = sha256(getTestConfig().NETWORK_PASSPHRASE);
    auto simulation =
        std::make_shared<Simulation>(Simulation::OVER_TCP, networkID);

    auto getConfiguration = [](int id, unsigned short targetOutboundConnections,
                               unsigned short maxInboundConnections) {
        auto cfg = getTestConfig(id);
        cfg.TARGET_PEER_CONNECTIONS = targetOutboundConnections;
        cfg.MAX_ADDITIONAL_PEER_CONNECTIONS = maxInboundConnections;
        return cfg;
    };

    auto numberOfAppConnections = [](Application& app) {
        return app.getOverlayManager().getAuthenticatedPeersCount();
    };

    auto numberOfSimulationConnections = [&]() {
        auto nodes = simulation->getNodes();
        return std::accumulate(std::begin(nodes), std::end(nodes), 0,
                               [&](int x, Application::pointer app) {
                                   return x + numberOfAppConnections(*app);
                               });
    };

    auto headCfg = getConfiguration(1, 0, 1);
    auto node1Cfg = getConfiguration(2, 1, 1);
    auto node2Cfg = getConfiguration(3, 1, 1);
    auto node3Cfg = getConfiguration(4, 1, 1);

    SIMULATION_CREATE_NODE(Head);
    SIMULATION_CREATE_NODE(Node1);
    SIMULATION_CREATE_NODE(Node2);
    SIMULATION_CREATE_NODE(Node3);

    SCPQuorumSet qSet;
    qSet.threshold = 2;
    qSet.validators.push_back(vHeadNodeID);
    qSet.validators.push_back(vNode1NodeID);
    qSet.validators.push_back(vNode2NodeID);
    qSet.validators.push_back(vNode3NodeID);

    auto headId = simulation->addNode(vHeadSecretKey, qSet, &headCfg)
                      ->getConfig()
                      .NODE_SEED.getPublicKey();

    simulation->addNode(vNode1SecretKey, qSet, &node1Cfg);

    // large timeout here as nodes may have a few bad attempts
    // (crossed connections) and we rely on jittered backoffs
    // to mitigate this

    simulation->addPendingConnection(vNode1NodeID, vHeadNodeID);
    simulation->startAllNodes();
    UNSCOPED_INFO("1 connects to h");
    simulation->crankUntil(
        [&]() { return numberOfSimulationConnections() == 2; },
        std::chrono::seconds{3}, false);

    simulation->addNode(vNode2SecretKey, qSet, &node2Cfg);
    simulation->addPendingConnection(vNode2NodeID, vHeadNodeID);
    simulation->startAllNodes();
    UNSCOPED_INFO("2 connects to 1");
    simulation->crankUntil(
        [&]() { return numberOfSimulationConnections() == 4; },
        std::chrono::seconds{20}, false);

    simulation->addNode(vNode3SecretKey, qSet, &node3Cfg);
    simulation->addPendingConnection(vNode3NodeID, vHeadNodeID);
    simulation->startAllNodes();
    UNSCOPED_INFO("3 connects to 2");
    simulation->crankUntil(
        [&]() { return numberOfSimulationConnections() == 6; },
        std::chrono::seconds{30}, false);

    simulation->removeNode(headId);
    UNSCOPED_INFO("wait for node to be disconnected");
    simulation->crankForAtLeast(std::chrono::seconds{2}, false);
    UNSCOPED_INFO("wait for 1 to connect to 3");
    simulation->crankUntil(
        [&]() { return numberOfSimulationConnections() == 6; },
        std::chrono::seconds{30}, true);
}

TEST_CASE("inbounds nodes can be promoted to ouboundvalid",
          "[overlay][acceptance]")
{
    auto networkID = sha256(getTestConfig().NETWORK_PASSPHRASE);
    auto simulation =
        std::make_shared<Simulation>(Simulation::OVER_TCP, networkID);

    SIMULATION_CREATE_NODE(Node1);
    SIMULATION_CREATE_NODE(Node2);
    SIMULATION_CREATE_NODE(Node3);

    SCPQuorumSet qSet;
    qSet.threshold = 1;
    qSet.validators.push_back(vNode1NodeID);

    auto nodes = std::vector<Application::pointer>{};
    auto configs = std::vector<Config>{};
    auto addresses = std::vector<PeerBareAddress>{};
    for (auto i = 0; i < 3; i++)
    {
        configs.push_back(getTestConfig(i + 1));
        addresses.emplace_back("127.0.0.1", configs[i].PEER_PORT);
    }

    configs[0].KNOWN_PEERS.emplace_back(
        fmt::format("127.0.0.1:{}", configs[1].PEER_PORT));
    configs[2].KNOWN_PEERS.emplace_back(
        fmt::format("127.0.0.1:{}", configs[0].PEER_PORT));

    nodes.push_back(simulation->addNode(vNode1SecretKey, qSet, &configs[0]));
    nodes.push_back(simulation->addNode(vNode2SecretKey, qSet, &configs[1]));
    nodes.push_back(simulation->addNode(vNode3SecretKey, qSet, &configs[2]));

    enum class TestPeerType
    {
        ANY,
        KNOWN,
        OUTBOUND
    };

    auto getTestPeerType = [&](size_t i, size_t j) {
        auto& node = nodes[i];
        auto peer =
            node->getOverlayManager().getPeerManager().load(addresses[j]);
        if (!peer.second)
        {
            return TestPeerType::ANY;
        }

        return peer.first.mType == static_cast<int>(PeerType::INBOUND)
                   ? TestPeerType::KNOWN
                   : TestPeerType::OUTBOUND;
    };

    using ExpectedResultType = std::vector<std::vector<TestPeerType>>;
    auto peerTypesMatch = [&](ExpectedResultType expected) {
        for (size_t i = 0; i < expected.size(); i++)
        {
            for (size_t j = 0; j < expected[i].size(); j++)
            {
                if (expected[i][j] > getTestPeerType(i, j))
                {
                    return false;
                }
            }
        }
        return true;
    };

    simulation->startAllNodes();

    // at first, nodes only know about KNOWN_PEERS
    simulation->crankUntil(
        [&] {
            return peerTypesMatch(
                {{TestPeerType::ANY, TestPeerType::KNOWN, TestPeerType::ANY},
                 {TestPeerType::ANY, TestPeerType::ANY, TestPeerType::ANY},
                 {TestPeerType::KNOWN, TestPeerType::ANY, TestPeerType::ANY}});
        },
        std::chrono::seconds(2), false);

    // then, after connection, some are made OUTBOUND
    simulation->crankUntil(
        [&] {
            return peerTypesMatch(
                {{TestPeerType::ANY, TestPeerType::OUTBOUND,
                  TestPeerType::KNOWN},
                 {TestPeerType::KNOWN, TestPeerType::ANY, TestPeerType::ANY},
                 {TestPeerType::OUTBOUND, TestPeerType::ANY,
                  TestPeerType::ANY}});
        },
        std::chrono::seconds(10), false);

    // then, after promotion, more are made OUTBOUND
    simulation->crankUntil(
        [&] {
            return peerTypesMatch(
                {{TestPeerType::ANY, TestPeerType::OUTBOUND,
                  TestPeerType::OUTBOUND},
                 {TestPeerType::OUTBOUND, TestPeerType::ANY, TestPeerType::ANY},
                 {TestPeerType::OUTBOUND, TestPeerType::ANY,
                  TestPeerType::ANY}});
        },
        std::chrono::seconds(30), false);

    // and when all connections are made, all nodes know about each other
    simulation->crankUntil(
        [&] {
            return peerTypesMatch(
                {{TestPeerType::ANY, TestPeerType::OUTBOUND,
                  TestPeerType::OUTBOUND},
                 {TestPeerType::OUTBOUND, TestPeerType::ANY,
                  TestPeerType::OUTBOUND},
                 {TestPeerType::OUTBOUND, TestPeerType::OUTBOUND,
                  TestPeerType::ANY}});
        },
        std::chrono::seconds(30), false);

    simulation->crankForAtLeast(std::chrono::seconds{3}, true);
}

TEST_CASE("overlay flow control", "[overlay][flowcontrol]")
{
    auto networkID = sha256(getTestConfig().NETWORK_PASSPHRASE);
    auto simulation =
        std::make_shared<Simulation>(Simulation::OVER_TCP, networkID);

    SIMULATION_CREATE_NODE(Node1);
    SIMULATION_CREATE_NODE(Node2);
    SIMULATION_CREATE_NODE(Node3);

    SCPQuorumSet qSet;
    qSet.threshold = 3;
    qSet.validators.push_back(vNode1NodeID);
    qSet.validators.push_back(vNode2NodeID);
    qSet.validators.push_back(vNode3NodeID);

    auto configs = std::vector<Config>{};

    for (auto i = 0; i < 3; i++)
    {
        auto cfg = getTestConfig(i + 1);

        // Set flow control parameters to something very small
        cfg.PEER_FLOOD_READING_CAPACITY = 1;
        cfg.PEER_READING_CAPACITY = 1;
        cfg.FLOW_CONTROL_SEND_MORE_BATCH_SIZE = 1;
        configs.push_back(cfg);
    }

    Application::pointer node = nullptr;
    auto setupSimulation = [&]() {
        node = simulation->addNode(vNode1SecretKey, qSet, &configs[0]);
        simulation->addNode(vNode2SecretKey, qSet, &configs[1]);
        simulation->addNode(vNode3SecretKey, qSet, &configs[2]);

        simulation->addPendingConnection(vNode1NodeID, vNode2NodeID);
        simulation->addPendingConnection(vNode2NodeID, vNode3NodeID);
        simulation->addPendingConnection(vNode3NodeID, vNode1NodeID);
        simulation->startAllNodes();
    };

    SECTION("enabled")
    {
        setupSimulation();
        simulation->crankUntil(
            [&] { return simulation->haveAllExternalized(2, 1); },
            3 * Herder::EXP_LEDGER_TIMESPAN_SECONDS, false);
        // Generate a bit of load to flood transactions, make sure nodes can
        // close ledgers properly
        auto& loadGen = node->getLoadGenerator();
        loadGen.generateLoad(LoadGenMode::CREATE, /* nAccounts */ 10, 0, 0,
                             /*txRate*/ 1,
                             /*batchSize*/ 1, std::chrono::seconds(0), 0);

        auto& loadGenDone =
            node->getMetrics().NewMeter({"loadgen", "run", "complete"}, "run");
        auto currLoadGenCount = loadGenDone.count();

        simulation->crankUntil(
            [&]() { return loadGenDone.count() > currLoadGenCount; },
            15 * Herder::EXP_LEDGER_TIMESPAN_SECONDS, false);
    }
    SECTION("one peer disables flow control")
    {
        configs[2].PEER_FLOOD_READING_CAPACITY = 0;
        setupSimulation();
        REQUIRE_THROWS_AS(
            simulation->crankUntil(
                [&] { return simulation->haveAllExternalized(2, 1); },
                3 * Herder::EXP_LEDGER_TIMESPAN_SECONDS, false),
            std::runtime_error);
    }
    SECTION("one peer doesn't support flow control")
    {
        configs[2].OVERLAY_PROTOCOL_VERSION =
            Peer::FIRST_VERSION_SUPPORTING_FLOW_CONTROL - 1;
        setupSimulation();
        REQUIRE_THROWS_AS(
            simulation->crankUntil(
                [&] { return simulation->haveAllExternalized(2, 1); },
                3 * Herder::EXP_LEDGER_TIMESPAN_SECONDS, false),
            std::runtime_error);
    }
}

PeerBareAddress
localhost(unsigned short port)
{
    return PeerBareAddress{"127.0.0.1", port};
}

TEST_CASE("database is purged at overlay start", "[overlay]")
{
    VirtualClock clock;
    auto cfg = getTestConfig();
    cfg.RUN_STANDALONE = false;
    auto app = createTestApplication(clock, cfg, true, false);
    auto& om = app->getOverlayManager();
    auto& peerManager = om.getPeerManager();
    auto record = [](size_t numFailures) {
        return PeerRecord{{}, numFailures, static_cast<int>(PeerType::INBOUND)};
    };

    peerManager.store(localhost(1), record(118), false);
    peerManager.store(localhost(2), record(119), false);
    peerManager.store(localhost(3), record(120), false);
    peerManager.store(localhost(4), record(121), false);
    peerManager.store(localhost(5), record(122), false);

    om.start();

    // Must wait 2 seconds as `OverlayManagerImpl::start()`
    // sets a 2-second timer.
    // `crankSome` may not work if other timers fire before that.
    // (e.g., pull-mode advert timer)
    testutil::crankFor(clock, std::chrono::seconds(2));

    REQUIRE(peerManager.load(localhost(1)).second);
    REQUIRE(peerManager.load(localhost(2)).second);
    REQUIRE(!peerManager.load(localhost(3)).second);
    REQUIRE(!peerManager.load(localhost(4)).second);
    REQUIRE(!peerManager.load(localhost(5)).second);
}

TEST_CASE("peer numfailures resets after good connection",
          "[overlay][acceptance]")
{
    auto networkID = sha256(getTestConfig().NETWORK_PASSPHRASE);
    auto simulation =
        std::make_shared<Simulation>(Simulation::OVER_TCP, networkID);
    auto record = [](size_t numFailures) {
        return PeerRecord{{}, numFailures, static_cast<int>(PeerType::INBOUND)};
    };

    SIMULATION_CREATE_NODE(Node1);
    SIMULATION_CREATE_NODE(Node2);

    SCPQuorumSet qSet;
    qSet.threshold = 1;
    qSet.validators.push_back(vNode1NodeID);

    Config const& cfg1 = getTestConfig(1);
    Config const& cfg2 = getTestConfig(2);

    auto app1 = simulation->addNode(vNode1SecretKey, qSet, &cfg1);
    auto app2 = simulation->addNode(vNode2SecretKey, qSet, &cfg2);

    simulation->startAllNodes();

    auto& om = app1->getOverlayManager();
    auto& peerManager = om.getPeerManager();
    peerManager.store(localhost(cfg2.PEER_PORT), record(119), false);
    REQUIRE(peerManager.load(localhost(cfg2.PEER_PORT)).second);

    simulation->crankForAtLeast(std::chrono::seconds{4}, true);

    auto r = peerManager.load(localhost(cfg2.PEER_PORT));
    REQUIRE(r.second);
    REQUIRE(r.first.mNumFailures == 0);
}

TEST_CASE("peer is purged from database after few failures",
          "[overlay][acceptance]")
{
    auto networkID = sha256(getTestConfig().NETWORK_PASSPHRASE);
    auto simulation =
        std::make_shared<Simulation>(Simulation::OVER_TCP, networkID);
    auto record = [](size_t numFailures) {
        return PeerRecord{{}, numFailures, static_cast<int>(PeerType::INBOUND)};
    };

    SIMULATION_CREATE_NODE(Node1);

    SCPQuorumSet qSet;
    qSet.threshold = 1;
    qSet.validators.push_back(vNode1NodeID);

    Config cfg1 = getTestConfig(1);
    Config cfg2 = getTestConfig(2);

    cfg1.PEER_AUTHENTICATION_TIMEOUT = 1;

    cfg2.MAX_INBOUND_PENDING_CONNECTIONS = 0;
    cfg2.MAX_OUTBOUND_PENDING_CONNECTIONS = 4; // to prevent changes in adjust()

    auto app1 = simulation->addNode(vNode1SecretKey, qSet, &cfg1);

    simulation->startAllNodes();

    auto& om = app1->getOverlayManager();
    auto& peerManager = om.getPeerManager();
    peerManager.store(localhost(cfg2.PEER_PORT), record(119), false);
    REQUIRE(peerManager.load(localhost(cfg2.PEER_PORT)).second);

    simulation->crankForAtLeast(std::chrono::seconds{5}, true);

    REQUIRE(!peerManager.load(localhost(cfg2.PEER_PORT)).second);
}

TEST_CASE("generalized tx sets are not sent to non-upgraded peers",
          "[txset][overlay]")
{
    if (protocolVersionIsBefore(Config::CURRENT_LEDGER_PROTOCOL_VERSION,
                                GENERALIZED_TX_SET_PROTOCOL_VERSION))
    {
        return;
    }
    auto runTest = [](bool hasNonUpgraded) {
        int const nonUpgradedNodeIndex = 1;
        auto networkID = sha256(getTestConfig().NETWORK_PASSPHRASE);
        auto simulation = Topologies::core(
            4, 0.75, Simulation::OVER_LOOPBACK, networkID, [&](int i) {
                auto cfg = getTestConfig(i, Config::TESTDB_ON_DISK_SQLITE);
                cfg.MAX_SLOTS_TO_REMEMBER = 10;
                cfg.TESTING_UPGRADE_LEDGER_PROTOCOL_VERSION =
                    static_cast<uint32_t>(GENERALIZED_TX_SET_PROTOCOL_VERSION);
                if (hasNonUpgraded && i == nonUpgradedNodeIndex)
                {
                    cfg.OVERLAY_PROTOCOL_VERSION =
                        Peer::FIRST_VERSION_SUPPORTING_GENERALIZED_TX_SET - 1;
                }
                return cfg;
            });

        simulation->startAllNodes();
        auto nodeIDs = simulation->getNodeIDs();
        auto node = simulation->getNode(nodeIDs[0]);

        auto root = TestAccount::createRoot(*node);

        int64_t const minBalance =
            node->getLedgerManager().getLastMinBalance(0);
        REQUIRE(node->getHerder().recvTransaction(
                    root.tx({txtest::createAccount(
                        txtest::getAccount("acc").getPublicKey(), minBalance)}),
                    false) == TransactionQueue::AddResult::ADD_STATUS_PENDING);
        simulation->crankForAtLeast(Herder::EXP_LEDGER_TIMESPAN_SECONDS, false);

        for (auto const& nodeID : simulation->getNodeIDs())
        {
            auto simNode = simulation->getNode(nodeID);
            if (hasNonUpgraded && nodeID == nodeIDs[nonUpgradedNodeIndex])
            {
                REQUIRE(simNode->getLedgerManager().getLastClosedLedgerNum() ==
                        1);
            }
            else
            {
                REQUIRE(simNode->getLedgerManager().getLastClosedLedgerNum() ==
                        2);
            }
        }
    };
    SECTION("all nodes upgraded")
    {
        runTest(false);
    }
    SECTION("non upgraded node does not externalize")
    {
        runTest(true);
    }
}

auto numDemandSent = [](std::shared_ptr<Application> app) {
    return app->getOverlayManager()
        .getOverlayMetrics()
        .mSendFloodDemandMeter.count();
};

auto numUnknownDemand = [](std::shared_ptr<Application> app) {
    return app->getMetrics()
        .NewMeter({"overlay", "flood", "unfulfilled-unknown"}, "message")
        .count();
};

auto numTxHashesAdvertised = [](std::shared_ptr<Application> app) {
    return app->getMetrics()
        .NewMeter({"overlay", "flood", "advertised"}, "message")
        .count();
};

TEST_CASE("overlay pull mode", "[overlay][pullmode]")
{
    VirtualClock clock;
    auto const numNodes = 3;
    std::vector<std::shared_ptr<Application>> apps;
    std::chrono::milliseconds const epsilon{1};

    for (auto i = 0; i < numNodes; i++)
    {
        Config cfg = getTestConfig(i);
        cfg.FLOOD_DEMAND_BACKOFF_DELAY_MS = std::chrono::milliseconds(200);
        cfg.FLOOD_DEMAND_PERIOD_MS = std::chrono::milliseconds(200);
        cfg.ENABLE_PULL_MODE = true;
        // Using a small tx set size such as 50 may lead to an unexpectedly
        // small advert/demand size limit.
        cfg.TESTING_UPGRADE_MAX_TX_SET_SIZE = 1000;
        apps.push_back(createTestApplication(clock, cfg));
    }

    std::vector<std::shared_ptr<LoopbackPeerConnection>> connections;
    for (auto i = 0; i < numNodes; i++)
    {
        connections.push_back(std::make_shared<LoopbackPeerConnection>(
            *apps[i], *apps[(i + 1) % numNodes]));
    }
    testutil::crankFor(clock, std::chrono::seconds(5));
    for (auto& conn : connections)
    {
        REQUIRE(conn->getInitiator()->isAuthenticated());
        REQUIRE(conn->getAcceptor()->isAuthenticated());
        REQUIRE(conn->getInitiator()->flowControlEnabled() ==
                Peer::FlowControlState::ENABLED);
        REQUIRE(conn->getAcceptor()->flowControlEnabled() ==
                Peer::FlowControlState::ENABLED);
    }

    auto createTxn = [](auto n) {
        HcnetMessage txn;
        txn.type(TRANSACTION);
        Memo memo(MEMO_TEXT);
        memo.text() = "tx" + std::to_string(n);
        txn.transaction().v0().tx.memo = memo;

        return std::make_shared<HcnetMessage>(txn);
    };

    auto createAdvert = [](auto txns) {
        HcnetMessage adv;
        adv.type(FLOOD_ADVERT);
        for (auto const& txn : txns)
        {
            adv.floodAdvert().txHashes.push_back(xdrSha256(txn->transaction()));
        }
        return std::make_shared<HcnetMessage>(adv);
    };

    // +-------------+------------+---------+
    // |             | Initiator  | Acceptor|
    // +-------------+------------+---------+
    // |Connection 0 |     0      |    1    |
    // |Connection 1 |     1      |    2    |
    // |Connection 2 |     2      |    0    |
    // +-------------+------------+---------+

    // `links[i][j]->sendMessage` is an easy way to send a message
    // from node `i` to node `j`.
    std::shared_ptr<LoopbackPeer> links[numNodes][numNodes];
    for (auto i = 0; i < numNodes; i++)
    {
        auto j = (i + 1) % 3;
        links[i][j] = connections[i]->getInitiator();
        links[j][i] = connections[i]->getAcceptor();
    }

    SECTION("ignore duplicated adverts")
    {
        auto tx = createTxn(0);
        auto adv =
            createAdvert(std::vector<std::shared_ptr<HcnetMessage>>{tx});

        // Node 0 advertises tx 0 to Node 2
        links[0][2]->sendMessage(adv, false);
        links[0][2]->sendMessage(adv, false);
        links[0][2]->sendMessage(adv, false);

        // Give enough time to call `demand` multiple times
        testutil::crankFor(
            clock, 3 * apps[2]->getConfig().FLOOD_DEMAND_PERIOD_MS + epsilon);

        REQUIRE(numDemandSent(apps[2]) == 1);
        REQUIRE(numUnknownDemand(apps[0]) == 1);

        // 10 seconds is long enough for a few timeouts to fire
        // but not long enough for the pending demand record to drop.
        testutil::crankFor(clock, std::chrono::seconds(10));

        links[0][2]->sendMessage(adv, false);

        // Give enough time to call `demand` multiple times
        testutil::crankFor(
            clock, 3 * apps[2]->getConfig().FLOOD_DEMAND_PERIOD_MS + epsilon);

        REQUIRE(numDemandSent(apps[2]) == 1);
        REQUIRE(numUnknownDemand(apps[0]) == 1);
    }

    SECTION("sanity check - demand")
    {
        auto tx0 = createTxn(0);
        auto tx1 = createTxn(1);
        auto adv0 =
            createAdvert(std::vector<std::shared_ptr<HcnetMessage>>{tx0});
        auto adv1 =
            createAdvert(std::vector<std::shared_ptr<HcnetMessage>>{tx1});

        // Node 0 advertises tx 0 to Node 2
        links[0][2]->sendMessage(adv0, false);
        // Node 1 advertises tx 1 to Node 2
        links[1][2]->sendMessage(adv1, false);

        // Give enough time to:
        // 1) call `demand`, and
        // 2) send the demands out.
        testutil::crankFor(clock, apps[2]->getConfig().FLOOD_DEMAND_PERIOD_MS +
                                      epsilon);

        REQUIRE(numDemandSent(apps[2]) == 2);
        REQUIRE(numUnknownDemand(apps[0]) == 1);
        REQUIRE(numUnknownDemand(apps[1]) == 1);
    }

    SECTION("exact same advert from two peers")
    {
        std::vector<std::shared_ptr<HcnetMessage>> txns;
        auto const numTxns = 5;
        txns.reserve(numTxns);
        for (auto i = 0; i < numTxns; i++)
        {
            txns.push_back(createTxn(i));
        }
        auto adv = createAdvert(txns);

        // Both Node 0 and Node 1 advertise {tx0, tx1, ..., tx5} to Node 2
        links[0][2]->sendMessage(adv, false);
        links[1][2]->sendMessage(adv, false);

        // Give enough time to:
        // 1) call `demand`, and
        // 2) send the demands out.
        testutil::crankFor(clock, apps[2]->getConfig().FLOOD_DEMAND_PERIOD_MS +
                                      epsilon);

        REQUIRE(numDemandSent(apps[2]) == 2);
        {
            // Node 2 is supposed to split the 5 demands evenly between Node 0
            // and Node 1 with no overlap.
            auto n0 = numUnknownDemand(apps[0]);
            auto n1 = numUnknownDemand(apps[1]);
            REQUIRE(std::min(n0, n1) == 2);
            REQUIRE(std::max(n0, n1) == 3);
            REQUIRE((n0 + n1) == 5);
        }

        // Wait long enough so the first round of demands expire and the second
        // round of demands get sent out.
        testutil::crankFor(
            clock, std::max(apps[2]->getConfig().FLOOD_DEMAND_BACKOFF_DELAY_MS,
                            apps[2]->getConfig().FLOOD_DEMAND_PERIOD_MS) +
                       epsilon);

        // Now both nodes should have gotten demands for all the 5 txn hashes.
        REQUIRE(numDemandSent(apps[2]) == 4);
        REQUIRE(numUnknownDemand(apps[0]) == 5);
        REQUIRE(numUnknownDemand(apps[1]) == 5);
    }

    SECTION("overlapping adverts")
    {
        auto tx0 = createTxn(0);
        auto tx1 = createTxn(1);
        auto tx2 = createTxn(2);
        auto tx3 = createTxn(3);
        auto adv0 = createAdvert(
            std::vector<std::shared_ptr<HcnetMessage>>{tx0, tx1, tx3});
        auto adv1 = createAdvert(
            std::vector<std::shared_ptr<HcnetMessage>>{tx0, tx2, tx3});

        // Node 0 advertises {tx0, tx1, tx3} to Node 2
        links[0][2]->sendMessage(adv0, false);
        // Node 1 advertises {tx0, tx2, tx3} to Node 2
        links[1][2]->sendMessage(adv1, false);

        // Give enough time to:
        // 1) call `demand`, and
        // 2) send the demands out.
        testutil::crankFor(clock, apps[2]->getConfig().FLOOD_DEMAND_PERIOD_MS +
                                      epsilon);

        REQUIRE(numDemandSent(apps[2]) == 2);

        {
            // Node 0 should get a demand for tx 1 and one of {tx 0, tx 3}.
            // Node 1 should get a demand for tx 2 and one of {tx 0, tx 3}.
            REQUIRE(numUnknownDemand(apps[0]) == 2);
            REQUIRE(numUnknownDemand(apps[1]) == 2);
        }

        // Wait long enough so the first round of demands expire and the second
        // round of demands get sent out.
        testutil::crankFor(clock,
                           apps[2]->getConfig().FLOOD_DEMAND_BACKOFF_DELAY_MS +
                               epsilon);

        // Node 0 should get a demand for the other member of {tx 0, tx 3}.
        // The same for Node 1.
        REQUIRE(numDemandSent(apps[2]) == 4);
        REQUIRE(numUnknownDemand(apps[0]) == 3);
        REQUIRE(numUnknownDemand(apps[1]) == 3);
    }

    SECTION("randomize peers")
    {
        auto peer0 = 0;
        auto peer1 = 0;
        auto const numRounds = 300;
        auto const numTxns = 5;
        for (auto i = 0; i < numRounds; i++)
        {
            std::vector<std::shared_ptr<HcnetMessage>> txns;
            txns.reserve(numTxns);
            for (auto j = 0; j < numTxns; j++)
            {
                txns.push_back(createTxn(i * numTxns + j));
            }
            auto adv = createAdvert(txns);

            // Both Node 0 and Node 1 advertise {tx0, tx1, ..., tx5} to Node 2
            links[0][2]->sendMessage(adv, false);
            links[1][2]->sendMessage(adv, false);

            // Give enough time to:
            // 1) call `demand`, and
            // 2) send the demands out.
            testutil::crankFor(
                clock, apps[2]->getConfig().FLOOD_DEMAND_PERIOD_MS + epsilon);

            REQUIRE(numDemandSent(apps[2]) == i * 4 + 2);
            {
                // Node 2 should split the 5 txn hashes
                // evenly among Node 0 and Node 1.
                auto n0 = numUnknownDemand(apps[0]);
                auto n1 = numUnknownDemand(apps[1]);
                REQUIRE(std::max(n0, n1) == i * numTxns + 3);
                REQUIRE(std::min(n0, n1) == i * numTxns + 2);
                if (n0 < n1)
                {
                    peer1++;
                }
                else
                {
                    peer0++;
                }
            }

            // Wait long enough so the first round of demands expire and the
            // second round of demands get sent out.
            testutil::crankFor(
                clock,
                apps[2]->getConfig().FLOOD_DEMAND_BACKOFF_DELAY_MS + epsilon);
            REQUIRE(numUnknownDemand(apps[0]) == (i + 1) * numTxns);
            REQUIRE(numUnknownDemand(apps[1]) == (i + 1) * numTxns);
        }

        // In each of the 300 rounds, both peer0 and peer1 have
        // a 50% chance of getting the demand with 3 txns instead of 2.
        // Statistically speaking, this is the same as coin flips.
        // After 300 flips, the chance that we have more than 200 heads
        // is 0.000000401%.
        REQUIRE(std::max(peer0, peer1) <= numRounds * 2 / 3);
    }
    for (auto& app : apps)
    {
        testutil::shutdownWorkScheduler(*app);
    }
}

TEST_CASE("pull mode enable only if both request", "[overlay][pullmode]")
{
    VirtualClock clock;
    auto test = [&](auto node1, auto node2) {
        Config cfg1 = getTestConfig(1);
        cfg1.ENABLE_PULL_MODE = node1;
        auto app1 = createTestApplication(clock, cfg1);
        Config cfg2 = getTestConfig(2);
        cfg2.ENABLE_PULL_MODE = node2;
        auto app2 = createTestApplication(clock, cfg2);

        auto conn = std::make_shared<LoopbackPeerConnection>(*app1, *app2);
        testutil::crankSome(clock);

        REQUIRE(conn->getInitiator()->isAuthenticated());
        REQUIRE(conn->getAcceptor()->isAuthenticated());
        REQUIRE(conn->getInitiator()->isPullModeEnabled() == false);
        REQUIRE(conn->getAcceptor()->isPullModeEnabled() == false);

        SECTION("peer does not follow the protocol")
        {
            HcnetMessage adv, emptyMsg;
            adv.type(FLOOD_ADVERT);
            adv.floodAdvert().txHashes.push_back(xdrSha256(emptyMsg));
            conn->getInitiator()->sendMessage(
                std::make_shared<HcnetMessage>(adv));
            testutil::crankSome(clock);

            if (node1 && node2)
            {
                REQUIRE(conn->getInitiator()->isAuthenticated());
                REQUIRE(conn->getAcceptor()->isAuthenticated());
            }
            else
            {
                REQUIRE(!conn->getInitiator()->isConnected());
                REQUIRE(!conn->getAcceptor()->isConnected());
                REQUIRE(conn->getAcceptor()->getDropReason() ==
                        "Peer sent FLOOD_ADVERT, but pull mode is disabled");
            }
        }
    };
    SECTION("acceptor disabled pull mode")
    {
        test(true, false);
    }
    SECTION("initiator disabled pull mode")
    {
        test(false, true);
    }
    SECTION("both disabled pull mode")
    {
        test(false, false);
    }
}

TEST_CASE("overlay pull mode loadgen", "[overlay][pullmode][acceptance]")
{
    auto networkID = sha256(getTestConfig().NETWORK_PASSPHRASE);
    auto simulation =
        std::make_shared<Simulation>(Simulation::OVER_TCP, networkID);

    SIMULATION_CREATE_NODE(Node1);
    SIMULATION_CREATE_NODE(Node2);

    SCPQuorumSet qSet;
    qSet.threshold = 2;
    qSet.validators.push_back(vNode1NodeID);
    qSet.validators.push_back(vNode2NodeID);

    auto configs = std::vector<Config>{};

    for (auto i = 0; i < 2; i++)
    {
        auto cfg = getTestConfig(i + 1);

        cfg.ENABLE_PULL_MODE = true;
        configs.push_back(cfg);
    }

    Application::pointer node1 =
        simulation->addNode(vNode1SecretKey, qSet, &configs[0]);
    Application::pointer node2 =
        simulation->addNode(vNode2SecretKey, qSet, &configs[1]);

    simulation->addPendingConnection(vNode1NodeID, vNode2NodeID);
    simulation->startAllNodes();

    simulation->crankUntil(
        [&] { return simulation->haveAllExternalized(2, 1); },
        3 * Herder::EXP_LEDGER_TIMESPAN_SECONDS, false);

    auto& loadGen = node1->getLoadGenerator();

    // Create 5 txns each creating one new account.
    // Set a really high tx rate so we create the txns right away.
    auto const numAccounts = 5;
    loadGen.generateLoad(LoadGenMode::CREATE, numAccounts, 0, 0,
                         /*txRate*/ 1000,
                         /*batchSize*/ 1, std::chrono::seconds(0), 0);

    // Let the network close multiple ledgers.
    // If the logic to advertise or demand incorrectly sends more than
    // they're supposed to (e.g., advertise the same txn twice),
    // then it'll likely happen within a few ledgers.
    simulation->crankUntil(
        [&] { return simulation->haveAllExternalized(5, 1); },
        10 * Herder::EXP_LEDGER_TIMESPAN_SECONDS, false);

    // Node 1 advertised 5 txn hashes to each of Node 2 and Node 3.
    REQUIRE(numTxHashesAdvertised(node1) == numAccounts);
    REQUIRE(numTxHashesAdvertised(node2) == 0);

    // As this is a "happy path", there should be no unknown demands.
    REQUIRE(numUnknownDemand(node1) == 0);
    REQUIRE(numUnknownDemand(node2) == 0);
}

TEST_CASE("overlay pull mode with many peers",
          "[overlay][pullmode][acceptance]")
{
    VirtualClock clock;

    // Defined in src/overlay/OverlayManagerImpl.h.
    auto const maxRetry = 15;

    auto const numNodes = maxRetry + 5;
    std::vector<std::shared_ptr<Application>> apps;

    for (auto i = 0; i < numNodes; i++)
    {
        Config cfg = getTestConfig(i);
        cfg.ENABLE_PULL_MODE = true;
        apps.push_back(createTestApplication(clock, cfg));
    }

    std::vector<std::shared_ptr<LoopbackPeerConnection>> connections;
    // Every node is connected to node 0.
    for (auto i = 1; i < numNodes; i++)
    {
        connections.push_back(
            std::make_shared<LoopbackPeerConnection>(*apps[i], *apps[0]));
    }

    testutil::crankFor(clock, std::chrono::seconds(5));
    for (auto& conn : connections)
    {
        REQUIRE(conn->getInitiator()->isAuthenticated());
        REQUIRE(conn->getAcceptor()->isAuthenticated());
        REQUIRE(conn->getInitiator()->flowControlEnabled() ==
                Peer::FlowControlState::ENABLED);
        REQUIRE(conn->getAcceptor()->flowControlEnabled() ==
                Peer::FlowControlState::ENABLED);
    }

    HcnetMessage adv, emptyMsg;
    adv.type(FLOOD_ADVERT);
    // As we will never fulfill the demand in this test,
    // we won't even bother hashing an actual txn envelope.
    adv.floodAdvert().txHashes.push_back(xdrSha256(emptyMsg));
    for (auto& conn : connections)
    {
        // Everyone advertises to Node 0.
        conn->getInitiator()->sendMessage(
            std::make_shared<HcnetMessage>(adv));
    }

    // Let it crank for 10 minutes.
    // If we're ever going to retry too many times,
    // it's likely that they'll happen in 10 minutes.
    testutil::crankFor(clock, std::chrono::minutes(10));

    REQUIRE(numDemandSent(apps[0]) == maxRetry);
}
}
