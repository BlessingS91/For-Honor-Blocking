/* PrismaUI SDK interface V1, copied from the source supplied for the port. */
#pragma once
#include <Windows.h>
#include <cstdint>
#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

typedef std::uint64_t PrismaView;
namespace PRISMA_UI_API {
    constexpr const auto PrismaUIPluginName = "PrismaUI";
    using PluginHandle = SKSE::PluginHandle;
    using ActorHandle = RE::ActorHandle;
    enum class InterfaceVersion : std::uint8_t { V1 };
    using OnDomReadyCallback = void (*)(PrismaView);
    using JSCallback = void (*)(const char*);
    using JSListenerCallback = void (*)(const char*);
    class IVPrismaUI1 {
    public:
        virtual PrismaView CreateView(const char*, OnDomReadyCallback = nullptr) noexcept = 0;
        virtual void Invoke(PrismaView, const char*, JSCallback = nullptr) noexcept = 0;
        virtual void InteropCall(PrismaView, const char*, const char*) noexcept = 0;
        virtual void RegisterJSListener(PrismaView, const char*, JSListenerCallback) noexcept = 0;
        virtual bool HasFocus(PrismaView) noexcept = 0;
        virtual bool Focus(PrismaView, bool = false, bool = false) noexcept = 0;
        virtual void Unfocus(PrismaView) noexcept = 0;
        virtual void Show(PrismaView) noexcept = 0;
        virtual void Hide(PrismaView) noexcept = 0;
        virtual bool IsHidden(PrismaView) noexcept = 0;
        virtual int GetScrollingPixelSize(PrismaView) noexcept = 0;
        virtual void SetScrollingPixelSize(PrismaView, int) noexcept = 0;
        virtual bool IsValid(PrismaView) noexcept = 0;
        virtual void Destroy(PrismaView) noexcept = 0;
        virtual void SetOrder(PrismaView, int) noexcept = 0;
        virtual int GetOrder(PrismaView) noexcept = 0;
        virtual void CreateInspectorView(PrismaView) noexcept = 0;
        virtual void SetInspectorVisibility(PrismaView, bool) noexcept = 0;
        virtual bool IsInspectorVisible(PrismaView) noexcept = 0;
        virtual void SetInspectorBounds(PrismaView, float, float, unsigned int, unsigned int) noexcept = 0;
        virtual bool HasAnyActiveFocus() noexcept = 0;
    };
    using RequestPluginAPIFunction = void* (*)(InterfaceVersion);
    [[nodiscard]] inline void* RequestPluginAPI(InterfaceVersion version = InterfaceVersion::V1) {
        const auto module = GetModuleHandleW(L"PrismaUI.dll");
        if (!module) return nullptr;
        const auto function = reinterpret_cast<RequestPluginAPIFunction>(GetProcAddress(module, "RequestPluginAPI"));
        return function ? function(version) : nullptr;
    }
}
