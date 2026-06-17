# Dock 手工回归清单

本文记录 `apps/studio` 当前 Dock 交互在合并前需要手工确认的场景。单元测试负责 ViewModel、命中测试和排序算法的稳定性；以下场景需要真实 Avalonia 窗口、指针输入和显示器缩放环境验证。

## Tab 排序

1. 同一个 tab 栏内拖拽排序。
   - 拖到相邻 tab 中线附近才触发排序预览。
   - 未越过触发边界时不应抖动或反复播放动画。
   - 释放后真实 tab 顺序与预览一致。

2. 从 tab 栏拖出再拖回原 tab 栏。
   - 拖出 tab 栏后源 tab 不应留下透明占位。
   - 拖回 tab 栏前不应提前触发排序动画。
   - 拖回 tab 栏后 placeholder 只出现在当前目标 index。
   - 取消拖拽后源 tab 恢复原位置，不残留 placeholder。

3. 从一个 tab 栏拖到另一个 tab 栏排序。
   - 跨 tab 栏排序行为应与本栏排序一致。
   - 进入目标 tab 栏前不应出现过早动画。
   - 释放后源 tab 从原窗口移除，目标 tab 栏顺序正确。

4. 快速来回穿过 tab 栏边缘。
   - Dock drag 与本栏 reorder 不应在边缘来回闪烁。
   - tab strip 不应残留 source ghost、placeholder 或空白槽位。

## 浮窗与跨工作区

1. 主窗口 tab 拖到浮窗中心 Float 区域。
   - 预览显示为可浮动目标。
   - 释放后新浮窗位置贴近目标工作区中的预览位置。

2. 浮窗 tab 拖到主窗口中心 Float 区域。
   - 释放后由目标 workspace 创建新浮窗。
   - 新浮窗不应明显偏移到源窗口坐标。

3. 浮窗最后一个 tab 被拖走。
   - 原浮窗关闭。
   - 新目标中的 tab 保持可见且 active 状态正确。

4. 关闭浮窗。
   - `Window/Panels` 菜单中对应 panel 的 check 状态应立即取消。
   - 重新打开菜单时不应显示已关闭 panel 的 check。

## 布局保存与恢复

1. 保存布局后重启 Studio。
   - 主窗口 split tree 恢复。
   - 浮窗恢复到可见屏幕区域。
   - active tab 与 active window 尽量恢复到保存状态。

2. 重置布局。
   - 所有浮窗关闭。
   - `Window/Panels` 菜单状态回到默认布局对应状态。
   - 不应保留关闭前的 floating panel open 状态。

## DPI 与多屏

1. Windows 缩放 100%、150%、200% 分别验证浮窗创建。
   - 新浮窗不跑出屏幕。
   - Float preview 与释放后的浮窗位置没有明显坐标系偏移。

2. 外接显示器拔插后恢复布局。
   - 浮窗位置 clamp 到当前可见工作区。
   - 宽高异常、NaN、Infinity 或过小尺寸不应导致不可见窗口。

3. 混合缩放多屏拖拽。
   - 从主屏拖到副屏附近创建浮窗时，位置不应明显越界。
   - 从浮窗拖回主窗口时，目标坐标应使用目标 workspace。

## 性能观察

1. tab reorder 时观察帧感。
   - target index 不变时不应重复播放动画。
   - 鼠标静止时不应持续触发布局或动画。

2. 快速拖拽 5 个以上 tabs。
   - 不应出现明显输入延迟。
   - tab strip 不应因 placeholder/source ghost 残留导致宽度错乱。

3. 打开多个浮窗后拖拽。
   - 跨 workspace hit-test 不应导致明显卡顿。
   - 菜单 open state 更新不应阻塞拖拽交互。

