# 《回声宅》全局导读 · 第 1 步：这个文件夹是怎么跑起来的

> 读者设定：会一点 C，不熟悉 Web / JavaScript。
> 目标：先建立**全局地图**——不逐行读代码，但要知道"双击之后发生了什么、游戏逻辑在哪个文件、从哪进、从哪出"。

---

## 0. 一句话结论

`回声宅.exe` **不含游戏逻辑**，它只做一件事：开一个窗口，然后让系统自带的浏览器内核去加载隔壁的 `app\index.html`。
**真正的游戏（3D 场景、谜题、角色、相机）全部在这个 HTML 文件里**，用 JavaScript + Three.js 写。

---

## 1. 文件夹结构

| 文件 | 大小 | 语言 | 作用 | 你需要动它吗 |
|---|---|---|---|---|
| `回声宅.exe` | 145 KB | C++ 编译产物 | 窗口外壳：建窗口、启动内核、加载页面 | ❌ 二进制，改不了也不用改 |
| `app\index.html` | 760 KB | HTML + CSS + JavaScript | **游戏本体**，单文件、离线可用 | ✅ 想改游戏就改它 |
| `src\main.cpp` | 190 行 | C++ | 外壳源码；改外壳或重新编译时用 | 偶尔看 |
| `使用说明.txt` | 1 KB | 纯文本 | 给玩家看的操作说明 | ❌ |

三个关键数字：

```
190 行   ——  C++ 外壳（真的很少）
1852 行  ——  index.html 总行数
其中约 670 KB / 10 行  ——  内联的 Three.js 库（压缩成超长单行，不是给人读的）
```

---

## 2. 最重要的认知：这是"两层"程序

```
┌─────────────────────────────────────────────┐
│  第一层：C++ 外壳（回声宅.exe，190 行）        │
│  职责只有 3 件：                              │
│    1. 创建一个 Windows 窗口                   │
│    2. 启动 WebView2（系统自带的浏览器内核）    │
│    3. 告诉它："去加载 app\index.html"         │
└───────────────────┬─────────────────────────┘
                    │ 把页面塞进窗口里显示
┌───────────────────▼─────────────────────────┐
│  第二层：网页游戏（app\index.html）           │
│  职责：全部游戏内容                           │
│    3D 建模 / 灯光 / 碰撞 / 谜题 / 相机 / 动画  │
└─────────────────────────────────────────────┘
```

**类比**：`exe` 是相框，`index.html` 是照片，`WebView2` 是玻璃和挂钉（系统自带，Win11 默认有）。

所以：**exe 只有 145 KB 却能跑 3D 游戏，并不奇怪**——3D 引擎（Three.js）和游戏数据都在那个 760 KB 的 HTML 里。

> 为什么不用 C 写 3D？不是不行（OpenGL/DirectX 都能做），而是要自己实现渲染管线、模型、阴影、文本排版、UI 系统——工作量是现在的几十倍。用浏览器内核等于**白拿一个成熟的渲染器 + UI 引擎**。

---

## 3. 从双击到看见画面的完整链路

### 3.1 C++ 侧（`src\main.cpp`，对照行号看）

| 步骤 | 位置 | 做了什么 |
|---|---|---|
| ① Windows 找到入口 | `main.cpp:151` | `wWinMain()` —— GUI 程序的入口，相当于 C 的 `main()` |
| ② 声明"我要按像素适配 DPI" | `153-155` | 调用 `SetProcessDpiAwarenessContext`，否则高分屏会糊 |
| ③ 注册窗口类 | `157-168` | `WNDCLASSEXW`：指定窗口过程函数 `WndProc`、图标、背景色 |
| ④ 真正创建窗口 | `170-176` | `CreateWindowExW(...)` 得到一个 `HWND`（窗口句柄），标题「回声宅 · 3D」 |
| ⑤ 启动浏览器内核 | `181` | `CreateCoreWebView2EnvironmentWithOptions(..., new EnvCB())`——**异步**，结果通过回调返回 |
| ⑥ 内核就绪回调 | `main.cpp:121` `EnvCB::Invoke` | 环境建好后，紧接着创建"控制器"：`env->CreateCoreWebView2Controller(g_hwnd, new CtrlCB())` |
| ⑦ 控制器就绪回调 | `main.cpp:77` `CtrlCB::Invoke` | 拿到 `ICoreWebView2`（就是网页本身），关掉右键菜单/F12，设置尺寸与可见性 |
| ⑧ 映射本地目录 | `main.cpp:100` | `SetVirtualHostNameToFolderMapping(L"echo.local", "…\app", ALLOW)`——把 `app` 文件夹伪装成一个网站 |
| ⑨ 加载页面 | `main.cpp:103` | `Navigate("https://echo.local/index.html")` |
| ⑩ 消息循环 | `main.cpp:184` | `while (GetMessageW(...)) { Translate; Dispatch; }` —— 窗口不退出就一直在转 |

两个值得注意的点（对 C 程序员来说最陌生的地方）：

**a) 回调 = 函数指针的升级版**
C 里你会写 `qsort(arr, n, sizeof(int), cmp)`，把 `cmp` 函数指针传进去。这里 `new EnvCB()` 是传一个**对象**，内核稍后调用它的 `Invoke()` 方法——本质还是"你留个函数给我，我干完活再叫你"，只是 C++ 用虚函数 + COM 接口实现了。

**b) `WM_SIZE` 是窗口的"中断"**
`main.cpp:133` 的 `WndProc` 相当于中断服务程序：窗口被拖动改变大小时，系统发 `WM_SIZE`，我们就把网页的尺寸同步过去（`g_ctrl->put_Bounds(rc)`）。窗口关闭时收到 `WM_DESTROY` → `PostQuitMessage` → 消息循环退出 → 进程结束。

### 3.2 网页侧（`app\index.html`）

| 行号 | 内容 | 说明 |
|---|---|---|
| `1-6` | `<!DOCTYPE html>` + `<head>` | 文档声明、编码 UTF-8 |
| `7-157` | `<style>` | 全部界面样式（HUD、弹窗、按钮、摇杆） |
| `158-312` | `<body>` | 界面元素：画布 `<canvas>`、任务栏、提示、各类弹窗 |
| `296-313` | 一个普通 `<script>` | 全局错误兜底（出错时显示提示层） |
| `315-1850` | `<script type="module">` | **全部游戏代码** |
| `316-325` | 内联的 Three.js | 670 KB 压缩库，等价于"`#include` 了一个 3D 引擎"，**不用读** |
| `326-1849` | 游戏逻辑 | 建地图 → 造角色 → 输入 → 相机 → 主循环 |

---

## 4. 游戏运行时的"心跳"：主循环

这是整个游戏最核心的 40 行（`index.html:1747` `function loop(now)`）。它一秒钟被浏览器调用约 **60 次**：

```
loop()  每帧一次：
 ├─ 1. 算 dt（距上一帧过了多少秒）      ← 保证帧率高低不影响移动速度
 ├─ 2. 性能自适应：2 秒统计一次 fps，太低就降分辨率
 ├─ 3. 读输入：WASD / Shift / 摇杆 → 目标速度
 │     └ 加速度插值 + 分别检测 X/Z 轴碰撞（分轴才能"贴着墙滑行"）
 ├─ 4. 垂直方向：跳跃速度 → 重力 → 落地 or 撞天花板
 ├─ 5. 状态机：idle / walk / run / jump（HUD 和动画都用它）
 ├─ 6. updateCamera()  相机跟随、被墙挡住就拉近、滚轮距离
 ├─ 7. updateAvatar()  把状态翻译成关节角度（摆臂、屈膝、前倾）
 ├─ 8. 灯光微动、尘埃、补间动画、准星射线检测
 └─ 9. renderer.render(scene, camera)   ← 真正画一帧
```

**关键概念 `dt`（delta time）**
C 里写动画常写 `x += 1;` 每帧加固定值，帧率一变速度就变。这里所有位移都写成 `x += 速度 * dt`，所以 30fps 和 144fps 下手感一致。这是"实时程序"最该先掌握的一件事。

---

## 5. 三个核心数据结构（用 C 的思维理解）

### 5.1 `const S = { ... }` —— 全局状态结构体（`index.html:970`）

```js
const S = {
  rustyKey: true, battery: false, flash: false, torch: false,   // 背包/道具
  booksSolved: false, safeOpen: false, powerOn: false,          // 谜题进度
  mistakes: 0, elapsed: 0, penalty: 0, over: false,             // 计时与统计
  hintUse: [0,0,0,0,0,0],
};
```

等价于：

```c
struct GameState {
    bool rustyKey, battery, flash, torch;
    bool booksSolved, safeOpen, powerOn;
    int  mistakes, elapsed, penalty; bool over;
    int  hintUse[6];
};
struct GameState S;
```

游戏里几乎所有判断都在读它：`label: () => S.battery ? '鞋柜' : '查看 鞋柜…'`。

### 5.2 `interactables` —— "结构体 + 函数指针"数组（`index.html:600`）

这是全文件**最像 C 的地方**：

```js
const interactables = [];          // 相当于 struct Item items[64]; int n = 0;

function addHit(x,y,z, sx,sy,sz, item) {   // 注册一个可互动物
  ... item.hit = m;                        // 挂一个隐形的"点击盒"
  interactables.push(item);
}

addHit(3.4, .9, -2, 1.4, 1.2, .8, {       // 门厅的鞋柜（index.html:613）
  label: () => S.battery ? '鞋柜' : '查看 鞋柜（似乎有东西反光）',   // ← 函数指针 1：显示什么文字
  act() {                                                          // ← 函数指针 2：按下 F 干什么
    if (S.battery) return toast('柜子里只剩下旧鞋。');
    S.battery = true;  batteryMesh.visible = false;
    toast('【获得：一号电池】');
    refreshObj();
  }
});
```

C 版本大致是：

```c
typedef struct {
    const char* (*label)(void);   // 动态标签
    void (*act)(void);            // 交互行为
    Mesh* hit;                    // 隐形几何体，用于射线检测
} Item;
```

**看懂这一点，就懂了整个游戏的组织方式**：所有物件都是"往数组里塞一个带行为的盒子"，主循环完全不需要知道有哪些物件。

### 5.3 `colliders` —— 轴对齐包围盒（AABB）列表

```js
{ x1, z1, x2, z2, on }        // 一个矩形，on() 返回 false 时临时失效
```

`blocked(nx, nz)` 遍历它做"点是否落在矩形里"的判断（`index.html:414` 附近）。这就是一个极简的碰撞系统：**玩家被抽象成一个半径 0.35 的圆，撞到矩形就不许走**。

---

## 6. 一条完整因果链（把两边串起来）

以"拿到电池"为例，看代码是怎么流动的：

```
玩家按 F
  └─ index.html:1488   keydown 事件 → tryInteract()
       └─ :1112  tryInteract()
            ├─ 有准星目标？aimItem（由 :1078 pickAim 每帧射线算出）
            └─ 没有就用"2.6 米内最近的物件"
                 └─ 调用 item.act()  ← :613 鞋柜的 act
                      ├─ S.battery = true        改状态
                      ├─ batteryMesh.visible = false   让模型消失
                      ├─ toast('【获得：一号电池】')    底部弹提示
                      └─ refreshObj()            重算任务清单（读 S → 改 DOM）
```

再往后：

```
背包里组合电池+手电 → S.torch = true → 手电筒灯 intensity = 30
                     → 地下入口的 darkBlock.on() 变成 false → 玩家可以下楼梯
```

**整条链路没有任何"魔法"**：事件 → 改状态 → 状态驱动显示。这是所有实时应用的通用模式。

---

## 7. C ↔ 这里 的对照表

| C 里的概念 | 这个项目里的写法 |
|---|---|
| `int main()` | `wWinMain()`（GUI 程序入口，`main.cpp:151`） |
| `struct` | 对象字面量 `{ key: value }` |
| `struct` 数组 | `const interactables = []` |
| 函数指针 | `item.act` / `item.label` |
| `#include "xxx.h"` | `<script src>`／`import`（本项目已把库内联，无需联网） |
| `while (1) { ... }` 主循环 | `requestAnimationFrame(loop)`，由浏览器每帧回调 |
| `printf` | `toast('文字')` / `element.textContent = ...` |
| 全局变量 | `const S`、`const player` |
| 传指针改外部变量 | 传对象引用（JS 对象都是引用） |
| `switch` 状态机 | `player.state = 'run'` 字符串状态 + 动画插值 |
| 定时器/中断 | 事件监听（keydown、mousedown、wheel）、`setTimeout` |

---

## 8. 建议的阅读顺序（第 1 步只做这些）

按这个顺序看，**每一步都能独立看懂，不需要前后跳跃**：

1. **`src\main.cpp` 第 151-190 行**（40 行）—— 看清"程序怎么起一个窗口、怎么把网页塞进去"。
2. **`app\index.html` 第 158-312 行** —— 就是界面清单：哪些 div、哪些按钮、哪些弹窗。当成"UI 的头文件"看。
3. **`index.html` 第 1743-1849 行**（约 40 行）—— 主循环九步，先只记顺序，不深究细节。
4. **`index.html` 第 969-1050 行** —— `S` 状态表 + `OBJ` 任务清单，知道游戏"记着哪些事"。
5. **`index.html` 第 599-680 行** —— 一口气读 2~3 个 `addHit(...)`，体会"物件 = 盒子 + 两个函数"。

**这一轮可以先完全跳过的部分**：

- `316-325`：Three.js 库（670 KB 压缩代码）
- `472-598`：建地图（`wall()`、`floorCeil()` 一堆坐标，改关卡时才需要）
- `902-960`：角色模型拼装
- `1598-1740`：相机与骨骼动画的数学

---

## 9. 常见疑问

**Q：为什么 exe 不能直接跑游戏逻辑，非要套个浏览器？**
一方面 3D 渲染 + UI 现成；另一方面**改游戏不用重新编译**——改完 `index.html` 存盘、重开窗口就生效，等于"热更新"。你现在这套结构，改一个数字（比如门锁密码）只需要改 HTML 里的一个字符串。

**Q：那两个文件哪个是"主程序"？**
都算。`exe` 是**宿主**（Host），`index.html` 是**内容**（Content）。少了 exe，浏览器也能打开 HTML；少了 HTML，exe 只会弹一个黑窗口。

**Q：如果我只想改游戏内容，需要装 Visual Studio 吗？**
不需要。用记事本/VS Code 改 `index.html`，存盘，重开 exe 即可。只有当你要改**窗口行为**（尺寸、标题、禁用右键等）时，才需要动 `main.cpp` 并重新编译。

**Q：为什么 HTML 里会有 670 KB 看不懂的代码？**
那是 Three.js 3D 引擎，被压缩过（变量名都变成单字母）并内联进来，目的是**断网也能玩**。读代码时直接跳过它，从第 326 行的"游戏代码"开始。

---

## 下一步（第 2 步预告，等你确认再展开）

有了全局地图之后，下一步建议按**"一条主线"**纵向切开：
`按下 W 为什么角色会动` → 从 `keydown` 一路追到 `renderer.render`，把输入、碰撞、相机、动画四条线各走一遍。
这条路走通，剩下的谜题逻辑对你来说就只是"重复的零件拼装"了。
