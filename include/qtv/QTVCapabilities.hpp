#pragma once

#include <include/qtv/QTVBackendKind.hpp>

namespace qtv {

struct QTVCapabilities
{
    bool cpp_backend_available = false;
    bool julia_bridge_available = false;
    bool allow_cpp_fallback_from_julia = false;

    bool Supports(const QTVBackendKind backend) const noexcept
    {
        switch(backend)
        {
            case QTVBackendKind::Cpp:
                return cpp_backend_available;

            case QTVBackendKind::Julia:
                return julia_bridge_available;

            case QTVBackendKind::Null:
            default:
                return true;
        }
    }
};

} // namespace qtv
