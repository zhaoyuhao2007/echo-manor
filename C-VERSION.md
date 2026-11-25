# 《回声宅》C 语言版 · 说明

> 这是 3D 网页版《回声宅》的 C 语言移植：同样的房间、同样的谜题、同样的三种结局，
> 把 3D 画面换成了终端文字与编号菜单。**单文件、无第三方库、可直接编译**。

---

## 一、怎么运行

| 方式 | 做法 |
|---|---|
| 直接玩 | 双击 `回声宅C版.exe` |
| 自己编译 | 见下方"编译命令" |

窗口建议用 Windows Terminal 或 PowerShell 打开（旧版 cmd 也能跑，但中文字体需要"新宋体"）。

**操作**：每回合会列出编号菜单，输入数字回车即可。输入 `q` 随时退出。

---

## 二、编译命令（本机 MSVC 已验证）

在 PowerShell 里先设好环境变量，再调 `cl.exe`：

```powershell
$msvc = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.44.35207"
$sdk  = "C:\Program Files (x86)\Windows Kits\10"
$sdkv = "10.0.26100.0"

$env:INCLUDE = "$msvc\include;$sdk\Include\$sdkv\ucrt;$sdk\Include\$sdkv\um;$sdk\Include\$sdkv\shared"
$env:LIB     = "$msvc\lib\x64;$sdk\Lib\$sdkv\ucrt\x64;$sdk\Lib\$sdkv\um\x64"
$env:PATH    = "$msvc\bin\Hostx64\x64;$env:PATH"

cl /nologo /std:c17 /W4 /utf-8 /Fe:回声宅C版.exe echo_house_c.c
```

三个要点：

- **必须加 `/utf-8`**：源码里的中文是 UTF-8，不加 MSVC 会按 GBK 解读，报 C2001 或输出乱码。
- 源文件本身保存为 **UTF-8 带 BOM**，用 VS 打开也不会乱。
- 不用 `/utf-8` 的替代办法：把文件另存为 GBK（不推荐，跨平台会坏）。

---

## 三、代码结构（从上往下就是阅读顺序）

| 区域 | 行数范围 | 内容 |
|---|---|---|
| ① 常量与状态 | 开头 ~ 100 | `TIME_LIMIT` 等宏 + `struct GameState S`（全局状态） |
| ② 输出小工具 | ~ 100 | `hr()` 分隔线、`clear_screen()`、`pause_line()` |
| ③ 时间/错误/结局 | ~ 130 | `pass_time()`、`mistake()`、`finish()` |
| ④ 输入 | ~ 230 | `ask_int()` 选编号、`ask_code()` 读 4 位数字 |
| ⑤ 五个谜题 | ~ 250 | 五色书 / 八音盒 / 保险箱 / 木箱 / 配电箱 |
| ⑥ 房间动作 | ~ 450 | 门厅、书房、地下室各自的 `act_xxx()` |
| ⑦ 菜单装配 + main | 末尾 | `Action menu[]`、`build_menu()`、主循环 |

**全文件最核心的一句**（`main()` 里）：

```c
menu[choice - 1].act();     /* 函数指针调用：这就是"物件的行为" */
```

菜单是一个数组，每个元素是"标签函数 + 行为函数"两个指针：

```c
typedef struct {
    const char *(*label)(void);   /* 当前该显示什么（随状态变化） */
    void (*act)(void);            /* 选中后执行什么 */
} Action;
```

加一个新物件 = 写一个 `act_xxx()` + 在 `build_menu()` 里 `add_action()` 一行，
主循环**完全不用改**。这正是网页版 `interactables` 数组的 C 版本。

---

## 四、和 3D 网页版的对应关系

| 3D 版 | C 版 |
|---|---|
| 走到物件前按 F | 菜单里选编号 |
| 五色书：点书 → 滑到左边 | 选编号 → 书移到左边；新增 `0` 全部放回 |
| 八音盒：听音复奏 | 用 `Beep()` 真的响四个音，再输入 4 位数字 |
| 保险箱 / 转盘 / 数字锁 | 输入 4 位数字 |
| 配电箱三把闸刀 | 依次选 3 个编号，不能重复 |
| 60 分钟倒计时（每个动作） | 每个动作 +1 分钟，提示 +2 分钟 |
| 错满 5 次 → 结局 | 同样 |
| 手电筒 = 电池 + 空手电筒 | 背包里出现"组合"选项 |

---

## 五、想改东西？改这几处就够

| 想改 | 位置 |
|---|---|
| 总时长（默认 60 分钟） | `#define TIME_LIMIT 60` |
| 容错次数（默认 5 次） | `#define MAX_MISTAKE 5` |
| 保险箱密码 | `puzzle_safe()` 里的 `"1986"` |
| 木箱密码 | `puzzle_crate()` 里的 `"0417"` |
| 大门密码 | `act_gate()` 里的 `"5263"` |
| 八音盒旋律 | `puzzle_music()` 里的 `melody[4]`（同时要改上面的大门密码，或改提示文案） |
| 闸刀顺序 | `puzzle_breaker()` 里的 `"312"` |
| 五色书答案 | `puzzle_books()` 里的 `want[5]` |
| 提示文案 | `act_hint()` 里的 `hint1[]` / `hint2[]` |
| 加一个新物件 | 写 `act_new()` + 在 `build_menu()` 里 `add_action(l_new, act_new)` |

---

## 六、已知取舍

- **没有存档**：一局 10~20 分钟，输了重开即可（和网页版一致）。
- **中文输入靠编号**：Windows 控制台输入中文要依赖输入法，容易乱码，所以做成菜单式；
  这也是个教学考虑——菜单背后正好是"函数指针表"这个 C 语言核心技巧。
- **Beep 音效**：走系统自带 API，不需要任何音频文件；某些虚拟机/无声卡环境会静音，不影响流程。
