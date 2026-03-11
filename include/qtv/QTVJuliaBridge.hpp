#pragma once

namespace qtv {

constexpr int HOOK_STATUS_OK          = 0;
constexpr int HOOK_STATUS_UNAVAILABLE = -1;

/** Function-pointer hook taking a deterministic fixture case_id and returning a status code. **/
using HookFunction = int (*)(int);

class QTVJuliaBridge
{
public:
    QTVJuliaBridge(HookFunction runFixture = nullptr, HookFunction compareParity = nullptr) noexcept
    : run_fixture_(runFixture)
    , compare_parity_(compareParity)
    {
    }

    bool available() const noexcept
    {
        return has_run_fixture() && has_compare_parity();
    }

    bool has_run_fixture() const noexcept
    {
        return run_fixture_ != nullptr;
    }

    bool has_compare_parity() const noexcept
    {
        return compare_parity_ != nullptr;
    }

    int run_fixture(const int case_id) const noexcept
    {
        return run_fixture_ != nullptr ? run_fixture_(case_id) : HOOK_STATUS_UNAVAILABLE;
    }

    int compare_parity(const int case_id) const noexcept
    {
        return compare_parity_ != nullptr ? compare_parity_(case_id) : HOOK_STATUS_UNAVAILABLE;
    }

private:
    HookFunction run_fixture_;
    HookFunction compare_parity_;
};

} // namespace qtv
