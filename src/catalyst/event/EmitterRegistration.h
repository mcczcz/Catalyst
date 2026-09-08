#pragma once

#include <memory>

#include "ll/api/event/Emitter.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"

// 为一个 final 事件类型生成 Emitter 与工厂样板，把它注册到 LeviLamina 的 EventBus。
// 尾随参数是要与该 emitter 生命周期绑定的 BDS hook（存在 >=1 个监听器时安装）。
// HookRegistrar 本身是可变参数模板，故单 hook、多 hook、零 hook 均由同一展开覆盖。
//
// 修复（26.32 适配）：LL 的 Emitter<Factory, Event> 靠 inline 变量模板
// detail::emitterRegistration 在静态初始化期注册工厂，而它仅被 EBO 空基类
// StaticRegistrationAnchor<bool&> “引用”——链接器看不到实际重定位，
// MSVC /OPT:REF 会将其 COMDAT（含动态初始化器）判为死代码丢弃，导致工厂
// 静默未注册、监听器挂不上、hook 永不安装（实测 hasEvent=false）。
// 改用普通全局对象的构造函数显式注册：.CRT$XCU 对初始化函数的引用链完整，
// 实测初始化器稳定保留。
#define CATALYST_HOOKED_EMITTER(EventType, ...)                                            \
    static ::std::unique_ptr<::ll::event::EmitterBase> EventType##EmitterFactory();        \
    class EventType##Emitter final                                                         \
    : public ::ll::event::Emitter<EventType##EmitterFactory, EventType> {                  \
        [[maybe_unused]] ::ll::memory::HookRegistrar<__VA_ARGS__> hook;                     \
    };                                                                                     \
    static ::std::unique_ptr<::ll::event::EmitterBase> EventType##EmitterFactory() {        \
        return ::std::make_unique<EventType##Emitter>();                                    \
    }                                                                                      \
    namespace {                                                                            \
    struct EventType##ExplicitRegistration {                                                \
        EventType##ExplicitRegistration() {                                                 \
            ::ll::event::EventBus::getInstance().setEventEmitter<EventType>(                \
                EventType##EmitterFactory                                                  \
            );                                                                             \
        }                                                                                   \
    };                                                                                     \
    EventType##ExplicitRegistration const EventType##ExplicitRegistrationInstance;          \
    } // 匿名命名空间：每个事件翻译单元独立一份，无符号冲突

// 共享同一组 hook 的 Before/After 事件对的便捷封装。
#define CATALYST_HOOKED_EVENT_PAIR(BeforeEvent, AfterEvent, ...)                            \
    CATALYST_HOOKED_EMITTER(BeforeEvent, __VA_ARGS__)                                       \
    CATALYST_HOOKED_EMITTER(AfterEvent, __VA_ARGS__)
