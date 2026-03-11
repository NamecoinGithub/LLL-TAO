/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <unit/catch2/catch.hpp>

#include <LLC/include/qtv_engine.h>

namespace
{
    int SuccessStatus(const int case_id)
    {
        return case_id == 1 ? LLC::QTV::HOOK_STATUS_OK : 9;
    }

    bool SuccessBool(const int case_id)
    {
        return case_id == 1;
    }
}

TEST_CASE("QTVJuliaBridge reports availability and status", "[qtv][hooks][bridge]")
{
    LLC::QTV::QTVJuliaBridge unavailable;
    REQUIRE_FALSE(unavailable.available());
    REQUIRE_FALSE(unavailable.has_run_fixture());
    REQUIRE_FALSE(unavailable.has_compare_parity());
    REQUIRE(unavailable.run_fixture(1) == LLC::QTV::HOOK_STATUS_UNAVAILABLE);
    REQUIRE(unavailable.compare_parity(1) == LLC::QTV::HOOK_STATUS_UNAVAILABLE);

    LLC::QTV::QTVJuliaBridge partial(&SuccessStatus, nullptr);
    REQUIRE_FALSE(partial.available());
    REQUIRE(partial.has_run_fixture());
    REQUIRE_FALSE(partial.has_compare_parity());
    REQUIRE(partial.run_fixture(1) == LLC::QTV::HOOK_STATUS_OK);
    REQUIRE(partial.compare_parity(1) == LLC::QTV::HOOK_STATUS_UNAVAILABLE);

    LLC::QTV::QTVJuliaBridge bridge(&SuccessStatus, &SuccessStatus);
    REQUIRE(bridge.available());
    REQUIRE(bridge.has_run_fixture());
    REQUIRE(bridge.has_compare_parity());
    REQUIRE(bridge.run_fixture(1) == LLC::QTV::HOOK_STATUS_OK);
    REQUIRE(bridge.compare_parity(0) != LLC::QTV::HOOK_STATUS_OK);
}

TEST_CASE("QTV engine backends isolate hook behavior", "[qtv][hooks][engine]")
{
    LLC::QTV::NullQTVEngine nullEngine;
    REQUIRE_FALSE(nullEngine.Available());
    REQUIRE_FALSE(nullEngine.RunFixture(1));
    REQUIRE_FALSE(nullEngine.CompareParity(1));

    LLC::QTV::CppQTVEngine cppEngine(&SuccessBool, &SuccessBool);
    REQUIRE(cppEngine.Available());
    REQUIRE(cppEngine.RunFixture(1));
    REQUIRE_FALSE(cppEngine.CompareParity(0));

    LLC::QTV::JuliaQTVEngine juliaEngine(LLC::QTV::QTVJuliaBridge(&SuccessStatus, &SuccessStatus));
    REQUIRE(juliaEngine.Available());
    REQUIRE(juliaEngine.RunFixture(1));
    REQUIRE_FALSE(juliaEngine.CompareParity(0));
}

TEST_CASE("QTV backend selection is deterministic", "[qtv][hooks][selection]")
{
    using Backend = LLC::QTV::QTVBackendKind;

    SECTION("C++ preference never promotes to Julia")
    {
        const LLC::QTV::QTVCapabilities capabilities = {false, true, true};
        REQUIRE(LLC::QTV::SelectBackend(Backend::Cpp, capabilities) == Backend::Null);
    }

    SECTION("Julia preference falls back to C++ only when explicitly allowed")
    {
        const LLC::QTV::QTVCapabilities fallback_capabilities = {true, false, true};
        REQUIRE(LLC::QTV::SelectBackend(Backend::Julia, fallback_capabilities) == Backend::Cpp);

        const LLC::QTV::QTVCapabilities no_fallback_capabilities = {true, false, false};
        REQUIRE(LLC::QTV::SelectBackend(Backend::Julia, no_fallback_capabilities) == Backend::Null);
    }

    SECTION("Available preferred backend wins")
    {
        const LLC::QTV::QTVCapabilities cpp_capabilities = {true, false, true};
        REQUIRE(LLC::QTV::SelectBackend(Backend::Cpp, cpp_capabilities) == Backend::Cpp);

        const LLC::QTV::QTVCapabilities julia_capabilities = {true, true, true};
        REQUIRE(LLC::QTV::SelectBackend(Backend::Julia, julia_capabilities) == Backend::Julia);
    }
}

TEST_CASE("QTV engine facade enforces selected backend", "[qtv][hooks][facade]")
{
    LLC::QTV::NullQTVEngine nullEngine;
    LLC::QTV::CppQTVEngine cppEngine(&SuccessBool, &SuccessBool);
    LLC::QTV::JuliaQTVEngine juliaEngine(LLC::QTV::QTVJuliaBridge(&SuccessStatus, &SuccessStatus));

    SECTION("Julia request safely falls back to C++")
    {
        LLC::QTV::QTVEngineFacade facade(
            nullEngine, cppEngine, juliaEngine, LLC::QTV::QTVBackendKind::Julia, {true, false, true});

        REQUIRE(facade.PreferredBackend() == LLC::QTV::QTVBackendKind::Julia);
        REQUIRE(facade.SelectedBackend() == LLC::QTV::QTVBackendKind::Cpp);
        REQUIRE(facade.Available());
        REQUIRE(facade.RunFixture(1));
        REQUIRE_FALSE(facade.CompareParity(0));
    }

    SECTION("Unavailable C++ request disables the facade")
    {
        LLC::QTV::QTVEngineFacade facade(
            nullEngine, cppEngine, juliaEngine, LLC::QTV::QTVBackendKind::Cpp, {false, true, true});

        REQUIRE(facade.SelectedBackend() == LLC::QTV::QTVBackendKind::Null);
        REQUIRE_FALSE(facade.Available());
        REQUIRE_FALSE(facade.RunFixture(1));
        REQUIRE_FALSE(facade.CompareParity(1));
    }
}
