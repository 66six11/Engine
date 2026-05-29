# 编码规范

## C++ 语言规则

- 使用 C++23。
- 优先使用值语义、显式移动所有权、`std::span`、`std::string_view`、`std::optional`、
  `std::expected`。
- 禁止裸 owning pointer、裸 `new`/`delete`、隐藏全局可变 GPU 状态。
- 返回错误、句柄或必须消费的对象时使用 `[[nodiscard]]`。
- 构造函数保持轻量。会因为 Vulkan、OS 或文件 I/O 失败的操作应放进显式 `create`
  函数或 factory。

## 命名

- 类型：`PascalCase`。
- 函数和变量：`camelCase`。
- 常量：`kPascalCase`。
- 私有成员：后缀下划线，例如 `device_`。
- 品牌名只用于 namespace、CMake/package/schema 边界；类型、函数、变量、字段和 attribute
  不加 `Asharia` 前缀。
- Vulkan wrapper 类型需要体现所有权，例如 `Device`、`Swapchain`、`ImageResource`、
  `CommandPool`。

## 文件组织

- 公共头文件：`engine/<module>/include/asharia/<module>/<name>.hpp` 或
  `packages/<package>/include/asharia/<package>/<name>.hpp`。
- 私有实现：`engine/<module>/src/<name>.cpp` 或 `packages/<package>/src/<name>.cpp`。
- 内部头文件：对应模块或 package 的 `src/<name>.hpp`。
- 测试：`tests/<module>/<name>_tests.cpp` 或 `packages/<package>/tests/<name>_tests.cpp`。
- shader：`shaders/<domain>/<name>.<stage>.<ext>`。

## 代码拆分与头文件边界

- 公共头文件必须是自包含的 API 契约：直接 include 自己使用的标准库和项目公共头，不依赖调用方的 include 顺序。
- 公共头文件只放类型声明、轻量值类型、常量、必要的短小 inline 函数和模板入口。复杂非模板实现、编译算法、验证逻辑、格式化输出和平台/API 交互必须放入 `src/*.cpp`。
- aggregate header 只作为兼容入口；新代码应优先 include 最窄的公共头，例如 `*_types.hpp`、`*_compile.hpp`、`*_diagnostics.hpp`。
- 私有 helper 只被一个 `.cpp` 使用时，放在该 `.cpp` 的匿名 namespace 或类外私有实现中；需要跨多个 `.cpp` 共享时，放在 package 的 `src/<name>.hpp`，不得被其他 package 或 app include。
- 一个 `.cpp` 同时承载多类职责并持续增长时，应按职责拆分，例如 `*_builder.cpp`、`*_compile.cpp`、`*_validation.cpp`、`*_diagnostics.cpp`。拆分必须同步更新 CMake source list 和 package-local tests。
- 每个新增公共窄头都要有 standalone include 测试，确认只 include 该头即可编译。不要用 aggregate header 的成功编译替代窄头自包含验证。
- backend-agnostic package 的公共头不得暴露后端 API 类型；RenderGraph/RHI 这类适配应通过独立 adapter target 和窄公共契约连接。

## 错误处理

- 不允许忽略 `VkResult`。
- Vulkan 失败需要转换成项目错误类型，并保留失败操作、`VkResult` 和诊断上下文。
- `VK_ERROR_DEVICE_LOST`、swapchain out-of-date、suboptimal swapchain、allocation
  failure、shader load failure 必须有显式路径。
- assertion 只处理程序员错误，不处理用户环境、驱动或文件系统错误。

## Vulkan 生命周期

- parent object 必须长于 child object。
- 销毁顺序必须确定，并体现在 owning type 中。
- 长生命周期 Vulkan object 在 Debug/开发构建中必须设置 debug name。
- renderer 代码不得直接散落 `VkDeviceMemory` 分配，buffer/image 统一走 VMA facade。
- `vkDeviceWaitIdle` 只允许在 shutdown、早期 MVP 简化路径或调试探针中使用；render loop
  中使用必须注释原因，并在性能阶段移除。

## 同步

- 默认使用 synchronization2。
- 资源状态转换由 render graph compiler 负责，不由单个 pass 私下处理。
- pass 通过声明式接口描述 read/write、layout、stage、access。
- queue ownership transfer 必须显式。MVP 可以只使用一个 graphics queue 来降低复杂度。

## Render Graph 风格

- pass setup 是声明式的。
- pass execution 接收 command context 和已解析资源句柄。
- swapchain image 等外部资源必须显式 import。
- graph compiler 负责 pass 排序、transient resource 生命周期、barrier 和 final layout。

## 格式化与静态检查

- 首批代码引入时同步添加 `.clang-format`。
- include 尽量窄，能降低编译耦合时使用 forward declaration。
- 新增 warning 视为缺陷。
- `clang-tidy` 重点关注 lifetime、modernize、bugprone、readability；不要选择会和 Vulkan
  惯用法冲突的规则。
