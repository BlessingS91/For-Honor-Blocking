#pragma once
#include <cstdint>
#include <Windows.h>

// TDM's public V5 interface, matching the user-supplied header.
// Keep the virtual method order unchanged.
namespace TDM_API {
    constexpr const char* TDMPluginName = "TrueDirectionalMovement";
    using PluginHandle = SKSE::PluginHandle;
    using ActorHandle = RE::ActorHandle;

    enum class InterfaceVersion : std::uint8_t { V1, V2, V3, V4, V5 };
    enum class APIResult : std::uint8_t {
        OK, NotOwner, MustKeep, AlreadyGiven, AlreadyTaken, BadThread
    };
    enum class DirectionalMovementMode : std::uint8_t {
        kDisabled, kVanillaStyle, kDirectional, kTargetLock
    };

    class IVTDM1 {
    public:
        [[nodiscard]] virtual unsigned long GetTDMThreadId() const noexcept = 0;
        [[nodiscard]] virtual bool GetDirectionalMovementState() const noexcept = 0;
        [[nodiscard]] virtual bool GetTargetLockState() const noexcept = 0;
        [[nodiscard]] virtual ActorHandle GetCurrentTarget() const noexcept = 0;
        [[nodiscard]] virtual APIResult RequestDisableDirectionalMovement(PluginHandle) noexcept = 0;
        [[nodiscard]] virtual APIResult RequestDisableHeadtracking(PluginHandle) noexcept = 0;
        virtual PluginHandle GetDisableDirectionalMovementOwner() const noexcept = 0;
        virtual PluginHandle GetDisableHeadtrackingOwner() const noexcept = 0;
        virtual APIResult ReleaseDisableDirectionalMovement(PluginHandle) noexcept = 0;
        virtual APIResult ReleaseDisableHeadtracking(PluginHandle) noexcept = 0;
    };
    class IVTDM2 : public IVTDM1 {
    public:
        [[nodiscard]] virtual APIResult RequestYawControl(PluginHandle, float) noexcept = 0;
        virtual APIResult SetPlayerYaw(PluginHandle, float) noexcept = 0;
        virtual APIResult ReleaseYawControl(PluginHandle) noexcept = 0;
    };
    class IVTDM3 : public IVTDM2 {
    public:
        [[nodiscard]] virtual DirectionalMovementMode GetDirectionalMovementMode() const noexcept = 0;
        [[nodiscard]] virtual RE::NiPoint2 GetActualMovementInput() const noexcept = 0;
    };
    class IVTDM4 : public IVTDM3 {
    public:
        [[nodiscard]] virtual bool IsTargetLockBehindTarget() const noexcept = 0;
    };
    class IVTDM5 : public IVTDM4 {
    public:
        [[nodiscard]] virtual APIResult RequestDisableTargetLock(PluginHandle) noexcept = 0;
        virtual APIResult ReleaseDisableTargetLock(PluginHandle) noexcept = 0;
        virtual PluginHandle GetDisableTargetLockOwner() const noexcept = 0;
    };

    using _RequestPluginAPI = void* (*)(InterfaceVersion);

    [[nodiscard]] inline IVTDM5* RequestPluginAPI() {
        // Guard the lookup; the original inline helper did not check module == nullptr.
        const auto module = GetModuleHandleA("TrueDirectionalMovement.dll");
        if (!module) return nullptr;
        const auto request = reinterpret_cast<_RequestPluginAPI>(
            GetProcAddress(module, "RequestPluginAPI"));
        return request ? static_cast<IVTDM5*>(request(InterfaceVersion::V5)) : nullptr;
    }
}
