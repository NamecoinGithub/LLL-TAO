#pragma once

#include <include/qtv/QTVCapabilities.hpp>
#include <include/qtv/QTVJuliaBridge.hpp>

namespace qtv {

class IQTVEngine
{
public:
    virtual ~IQTVEngine() = default;
    virtual bool Available() const noexcept = 0;
    virtual bool RunFixture(int case_id) = 0;
    virtual bool CompareParity(int case_id) = 0;
};

class NullQTVEngine final : public IQTVEngine
{
public:
    bool Available() const noexcept override
    {
        return false;
    }

    bool RunFixture(int) override
    {
        return false;
    }

    bool CompareParity(int) override
    {
        return false;
    }
};

class JuliaQTVEngine final : public IQTVEngine
{
public:
    explicit JuliaQTVEngine(QTVJuliaBridge bridge = QTVJuliaBridge()) noexcept
    : bridge_(bridge)
    {
    }

    bool Available() const noexcept override
    {
        return bridge_.available();
    }

    bool RunFixture(const int case_id) override
    {
        return bridge_.run_fixture(case_id) == HOOK_STATUS_OK;
    }

    bool CompareParity(const int case_id) override
    {
        return bridge_.compare_parity(case_id) == HOOK_STATUS_OK;
    }

private:
    QTVJuliaBridge bridge_;
};

class CppQTVEngine final : public IQTVEngine
{
public:
    using Operation = bool (*)(int);

    CppQTVEngine(Operation runFixture = nullptr, Operation compareParity = nullptr) noexcept
    : run_fixture_(runFixture)
    , compare_parity_(compareParity)
    {
    }

    bool Available() const noexcept override
    {
        return run_fixture_ != nullptr && compare_parity_ != nullptr;
    }

    bool RunFixture(const int case_id) override
    {
        return run_fixture_ != nullptr && run_fixture_(case_id);
    }

    bool CompareParity(const int case_id) override
    {
        return compare_parity_ != nullptr && compare_parity_(case_id);
    }

private:
    Operation run_fixture_;
    Operation compare_parity_;
};

inline QTVBackendKind SelectBackend(const QTVBackendKind preferred, const QTVCapabilities& capabilities) noexcept
{
    switch(preferred)
    {
        case QTVBackendKind::Cpp:
            return capabilities.cpp_backend_available ? QTVBackendKind::Cpp : QTVBackendKind::Null;

        case QTVBackendKind::Julia:
            if(capabilities.julia_bridge_available)
                return QTVBackendKind::Julia;

            if(capabilities.allow_cpp_fallback_from_julia && capabilities.cpp_backend_available)
                return QTVBackendKind::Cpp;

            return QTVBackendKind::Null;

        case QTVBackendKind::Null:
        default:
            return QTVBackendKind::Null;
    }
}

class QTVEngineFacade final : public IQTVEngine
{
public:
    QTVEngineFacade(IQTVEngine& nullEngine, IQTVEngine& cppEngine, IQTVEngine& juliaEngine,
        const QTVBackendKind preferredBackend, const QTVCapabilities capabilities) noexcept
    : preferred_backend_(preferredBackend)
    , capabilities_(capabilities)
    , selected_backend_(SelectBackend(preferredBackend, capabilities))
    , engine_(ResolveEngine(nullEngine, cppEngine, juliaEngine, selected_backend_))
    {
    }

    QTVBackendKind PreferredBackend() const noexcept
    {
        return preferred_backend_;
    }

    QTVBackendKind SelectedBackend() const noexcept
    {
        return selected_backend_;
    }

    const QTVCapabilities& Capabilities() const noexcept
    {
        return capabilities_;
    }

    bool Available() const noexcept override
    {
        return engine_.Available();
    }

    bool RunFixture(const int case_id) override
    {
        return engine_.RunFixture(case_id);
    }

    bool CompareParity(const int case_id) override
    {
        return engine_.CompareParity(case_id);
    }

private:
    static IQTVEngine& ResolveEngine(
        IQTVEngine& nullEngine, IQTVEngine& cppEngine, IQTVEngine& juliaEngine, const QTVBackendKind backend) noexcept
    {
        switch(backend)
        {
            case QTVBackendKind::Cpp:
                return cppEngine;

            case QTVBackendKind::Julia:
                return juliaEngine;

            case QTVBackendKind::Null:
            default:
                return nullEngine;
        }
    }

    QTVBackendKind preferred_backend_;
    QTVCapabilities capabilities_;
    QTVBackendKind selected_backend_;
    IQTVEngine& engine_;
};

} // namespace qtv
