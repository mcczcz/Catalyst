# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html)

## [v0.5.0] - 2026-09-08

### 适配

- 适配 LeviLamina 26.32.0（bedrockdata 26.32.2-server.4），全 60 个事件源文件编译、链接通过，
  并在 BDS 1.26.32.2 + LeviLamina 26.32.2 真机上完成运行时验证（事件发布、取消拦截均正常）

### 修复

- **修复全部事件静默失效的严重问题**：LL 的 `Emitter<Factory, Event>` 依赖 inline 变量模板
  `detail::emitterRegistration` 在静态初始化期向 EventBus 注册工厂，而它仅被 EBO 空基类
  `StaticRegistrationAnchor<bool&>` “引用”——MSVC /OPT:REF 看不到实际重定位，将其 COMDAT
  （含动态初始化器）判为死代码丢弃。后果：工厂从未注册 → `emplaceListener` 静默失败 →
  emitter 永不构造 → hook 永不安装，且无任何报错。修复：`CATALYST_HOOKED_EMITTER` 宏改用
  普通全局对象的构造函数显式调用 `setEventEmitter`（.CRT$XCU 引用链完整，不会被丢弃），
  并为两个手写 Emitter 的事件（`PlayerOpenContainerEvent`/`PlayerCloseContainerEvent`）补上
  同样的显式注册（`EmitterRegistration.h` 注释有完整分析）
- **修复冰融化事件不触发**：26.32 将融化逻辑内联进 `IceBlock::randomTick`，原 hook 目标
  `_getMeltedBlockAndSendEvents` 成为死代码（实测 0 次调用）。hook 迁移到 `randomTick`，
  自行复现 vanilla 融化条件后发布事件（实测标定：方块光照 ≥ 9 触发融化，天空光与昼夜
  不参与判定，与 Java 版行为不同）；被取消时跳过原调用，未取消则交还 vanilla 执行
  融化与 Fizz 音效（手工 ActorSoundIdentifier 布局全部删除）

### 主要变更（相对 26.20 的 API 变化）

- `BlockTypeRegistry::get()` → `BlockTypeRegistry::mBlockTypeRegistry().mValue`
- `ActorDamageSource::getCause()` → `damageCause()`（返回 `SharedTypes::Legacy::ActorDamageCause`）
- `ActorEventCoordinator::sendEvent` 改用 `EventRef<ActorGameplayEvent<void>>` 适配
- 漏斗/酿造台打开容器事件：`HopperBlockActor::$startOpen` 与 `BrewingStandBlockActor::$startOpen` 为同一实现（MCFOLD），改用 `BlockActor::mType` 成员区分实际类型（`VanillaBlockActor::$vftableForBlockActor` 已无符号）
- 其余 20+ 个事件文件针对波及的符号删除/签名变化逐一适配（详见 git 历史）

## [v0.4.0] - 2026-06-27

### ⚠️ 破坏性变更 (Breaking Changes)

重构事件库，为每对 `XxxBeforeEvent` / `XxxAfterEvent` 抽出共享基类 `XxxEvent`，消除字段、getter、构造函数与 `serialize()` 的重复。

对下游使用者的影响：

- 新增公开基类符号 `XxxEvent`（如 `PlayerDropItemEvent`、`BlockPistonEvent` 等）。`XxxBeforeEvent` 现派生自 `ll::event::Cancellable<XxxEvent>`，`XxxAfterEvent` 现派生自 `XxxEvent`。
- **继承链改变**：直接依赖 `XxxBeforeEvent` / `XxxAfterEvent` 继承层级或对其做静态类型断言的代码需要重新编译，可能需要调整。监听 `XxxBeforeEvent` / `XxxAfterEvent` 的常规代码无需改动（类名与字段访问接口保持不变）。
- **容器事件基类收敛**：`PlayerOpenContainerAfterEvent`、`PlayerCloseContainerAfterEvent` 的基类由 `PlayerEvent` 上移至 `ServerPlayerEvent`（向后兼容的扩宽，After 事件新增 `self()` 返回 `ServerPlayer&`）。
- 因 ABI 改变，使用本事件库的插件需随本版本一并重新编译。

未改动：`ServerPong`、`ClientLogin`、`NetherPortalCreate`、`DragonEggBlockTeleport` 等 Before/After 字段本质不同的事件保持原样；NBT 序列化输出字段保持不变。

## [v0.3.0] - 2026-04-07

- 适配LL 26.10.0

## [v0.2.1] - 2026-01-31

- 修复红石更新事件和生物放置方块事件，并恢复客户端连接事件

## [v0.2.2] - 2026-02-02

- 增加玩家盾牌格挡事件
- 为耕地退化事件增加Actor参数

## [v0.2.3] - 2026-02-02

- 增加方块被爆炸摧毁事件