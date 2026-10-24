# UAS keyboard pitch roll yaw commands

这是你确认要先完成的 **Windows 键盘指令模块**。运行后会打开一个控制窗口，显示键盘产生的 Roll、Pitch 和 Yaw 指令。代码有英文注释。

读取社团资料后确认：自制飞控任务书尚未指定处理器、接收指令的 API 或通信报文；现有 `drone_thrust.cpp` 是空文件。因此这一版完成电脑端输入和指令生成，硬件发送层留待接口确定后接入。

## Run on Windows

1. 双击此文件夹中的 `run.cmd`。脚本会使用 Visual Studio 的 C++ 编译器构建程序并检查键盘逻辑。
2. 点击标题为 **UAS Keyboard Commands** 的窗口。
3. 按住下表中的键，观察指令值；松开后该方向回到零。

这个版本使用 Windows API，直接在 Windows 运行。项目不需要 ROS 或仿真环境。

也可以在 **Windows PowerShell** 中运行：

```powershell
& 'C:\Users\1\Documents\Codex\2026-10-02\wo-x\uas_keyboard_control\run.cmd'
```

构建成功后，也可以直接双击 `bin\uas_keyboard.exe`。

## Keyboard mapping

| Key | Meaning | Command |
| --- | --- | --- |
| W | Pitch nose down | Pitch = -10 degrees |
| S | Pitch nose up | Pitch = +10 degrees |
| A | Roll left | Roll = -10 degrees |
| D | Roll right | Roll = +10 degrees |
| Q | Yaw left | Yaw rate = -30 degrees/second |
| E | Yaw right | Yaw rate = +30 degrees/second |
| Escape | Close the window | Exit |

- 同时按 `W + D + E`，可以同时发出三个方向的指令。
- 同时按 `W + S` 会相互抵消。若只松开 S，仍按住的 W 会恢复负 Pitch 指令。其他轴相同。
- 切换到其他窗口时，程序清空所有按键状态并显示零指令。返回窗口后，松开并重新按键。
- Alt 快捷键、拖动窗口和窗口菜单会清空按键状态。

这里 Roll、Pitch 是**目标角度**；Yaw 是**目标角速度**，不是目标朝向角。Yaw 的定义是绕机体 Z 轴转动的角速度。松开 Q/E 代表请求停止继续偏航，不代表把机头朝向恢复到起始方向。

## Understand the code

先看 `keyboard_logic.hpp`，再看 `keyboard_control.cpp`。

1. `Keys` 保存六个按键是否按下，类型是 `bool`。
2. `set_key(...)` 在按下或松开时更新对应状态。
3. `make_command(...)` 根据当前按键生成一个 `PyrCommand` 结构体。
4. `refresh_display(...)` 把结构体中的三个数显示在窗口中。

例如：

```cpp
const int roll_direction = static_cast<int>(keys.d) - static_cast<int>(keys.a);
command.roll_deg = roll_direction * max_tilt_deg;
```

`true` 转成整数是 1，`false` 是 0。只按 D 时为 `1 - 0 = 1`，所以 Roll = +10；只按 A 时为 `0 - 1 = -1`，所以 Roll = -10；两个都按或都松开时为零。

`make_command` 返回一个结构体，所以一次函数调用可以带回 Roll、Pitch、Yaw 三个数。这对应我们之前讨论过的“一个函数怎样提供多个结果”。

要改变大小，修改 `keyboard_logic.hpp` 顶部的两个常量，然后重新运行 `run.cmd`：

```cpp
inline constexpr double max_tilt_deg = 10.0;
inline constexpr double max_yaw_rate_deg_s = 30.0;
```

## Interface for the next stage

之后由通信层读取：

```cpp
const uas::PyrCommand command = uas::make_command(keys, window_has_focus);
// command.roll_deg
// command.pitch_deg
// command.yaw_rate_deg_s
```

本模块定义机体坐标为 X 向前、Y 向右、Z 向下。正 Roll 右倾，正 Pitch 抬头，正 Yaw 向右转。接收端需要确认坐标系、单位、控制模式与这些定义一致；角速度指令不能直接当成朝向角使用。

这版没有连接飞控，没有油门、解锁、传感器反馈、PID 或电机输出。显示的是要求飞控执行的指令，飞机不会因运行这个程序而起飞。零角度/零角速度指令也不等于实际无人机能定点悬停。

接到真实系统时，还需要确定报文格式、发送频率、指令失效时间、油门来源和飞控端失联处理。现在的窗口按键事件不是实时飞控发送循环。

## Use the existing Visual Studio project

如果你要把它放进桌面 UAS 文件夹里的 `PYRonly` 项目：

1. 把 `keyboard_control.cpp` 和 `keyboard_logic.hpp` 一起复制到该项目的源文件夹。
2. 在 Visual Studio 的 Solution Explorer 中，右键项目，选择 **Add > Existing Item**，添加这两个文件。
3. 确保项目只包含一个带 `main()` 的 `.cpp`。这里的 `keyboard_control.cpp` 已经有 `main()`；不要把 `keyboard_logic_tests.cpp` 加进同一个程序。
4. 选择 x64，按 Ctrl+F5 运行。窗口代码所需的系统库已由源代码中的 pragma 指定。

社团现有 `.vcxproj` 尚未登记任何 `.cpp`，所以只把代码放进文件夹还不够，需要执行第 2 步。

## Checks and references

`keyboard_logic_tests.cpp` 检查六个键的方向、组合、相反键抵消、松键、窗口无焦点时清零等行为。`build.cmd` 会编译并运行这些检查。

Windows 按键处理依据微软官方文档：[WM_KEYDOWN](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-keydown)、[WM_KEYUP](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-keyup)、[WM_KILLFOCUS](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-killfocus)。
