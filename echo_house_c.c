/* =====================================================================
 *  《回 声 宅》 —— C 语言版（终端）   echo_house_c.c
 * =====================================================================
 *
 *  《回声宅》的 C 语言终端版：三个房间、六道谜题、三种结局，
 *  全部用终端文字与编号菜单呈现。
 *
 *  为什么 C 版用"菜单"而不是"输入中文命令"？
 *    1. Windows 控制台里输入中文要靠输入法，容易乱码，不适合当入门项目；
 *    2. 菜单本质是一张"函数指针表"——这是 C 语言里最重要的组织技巧之一，
 *       本质就是一张"盒子 + 两个函数"的表。
 *
 *  代码结构（从上往下正好是阅读顺序）：
 *    ① 常量与全局状态        ② 输出小工具
 *    ③ 时间 / 错误 / 结局     ④ 五个谜题
 *    ⑤ 三个房间的可交互动作    ⑥ 菜单装配
 *    ⑦ main：一回合 = 显示 + 选择 + 执行
 *
 *  编译（本机 MSVC）：
 *      cl /nologo /std:c17 /W4 /utf-8 /Fe:回声宅C版.exe echo_house_c.c
 *  运行：双击 exe，或命令行里 echo_houseC版.exe
 *
 *  注意：本文件保存为 UTF-8 带 BOM；编译务必加 /utf-8，否则中文会乱码。
 * ===================================================================== */

#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>          /* Beep / Sleep / SetConsoleOutputCP */

/* =====================================================================
 *  ① 常量与全局状态
 * ===================================================================== */

#define TIME_LIMIT   60       /* 午夜之前 60 分钟 */
#define MAX_MISTAKE  5        /* 错满 5 次，房子就把你留下 */
#define MIN_PER_ACT  1        /* 每个动作花 1 分钟 */
#define MIN_PER_HINT 2        /* 问一次提示花 2 分钟 */

typedef enum { R_HALL, R_STUDY, R_CELLAR } Room;   /* 门厅 / 书房 / 地下室 */

typedef struct {
    /* —— 道具 —— */
    int rustyKey;        /* 锈铜钥匙（开局就有，信里夹着） */
    int battery;         /* 一号电池 */
    int flash;           /* 手电筒（空） */
    int torch;           /* 手电筒（已装电池） */
    int diary;           /* 日记残页 */
    int smallKey;        /* 小铜钥匙 */
    int gear;            /* 黄铜齿轮 */
    int fuse;            /* 陶瓷保险丝 */
    int brassKey;        /* 黄铜大门钥匙 */
    int watch;           /* 祖父的怀表 */

    /* —— 进度 —— */
    int booksSolved;     /* 五色书已排好 */
    int clockOpen;       /* 座钟背板已打开 */
    int musicSolved;     /* 八音盒已复奏 */
    int safeOpen;        /* 保险箱已打开 */
    int crateOpen;       /* 木箱已打开 */
    int cabOpen;         /* 储物柜已打开 */
    int fuseIn;          /* 保险丝已装进配电箱 */
    int powerOn;         /* 已通电 */
    int keypadSolved;    /* 大门数字锁已解 */

    /* —— 计时与统计 —— */
    int minutes;         /* 已过去的分钟数 */
    int mistakes;
    int hintUsed[5];     /* 每条线索问了几次，决定提示的"详细程度" */
    int ending;          /* 0 = 还在玩；1 = 逃出；2 = 超时；3 = 错误满 */
    Room room;
} GameState;

static GameState S;      /* 全局状态：相当于 struct GameState S; 所有函数共享 */

/* =====================================================================
 *  ② 输出小工具
 * ===================================================================== */

static void hr(void) { puts("============================================================"); }
static void thin(void) { puts("------------------------------------------------------------"); }

/* 清屏 / 等待：只在真正的控制台里做，输出被重定向（比如自动化测试）时跳过，
 * 否则 cls 会写进日志、回车会把脚本的输入吃掉。 */
static int is_console(DWORD which) {
    DWORD mode = 0;
    return GetConsoleMode(GetStdHandle(which), &mode) ? 1 : 0;
}
static void clear_screen(void) {
    if (is_console(STD_OUTPUT_HANDLE)) system("cls");
}

static void pause_line(void) {
    char buf[8];
    if (!is_console(STD_INPUT_HANDLE)) return;     /* 管道输入时不等回车 */
    printf("\n（回车继续）");
    fflush(stdout);
    if (!fgets(buf, sizeof buf, stdin)) exit(0);
}

/* =====================================================================
 *  ③ 时间 / 错误 / 结局
 * ===================================================================== */

static void pass_time(int m) {
    S.minutes += m;
    if (S.minutes >= TIME_LIMIT && S.ending == 0) S.ending = 2;
}

static void mistake(const char *why) {
    S.mistakes++;
    Beep(180, 220);
    printf("\n  [错误] %s（%d / %d）\n", why, S.mistakes, MAX_MISTAKE);
    if (S.mistakes >= MAX_MISTAKE && S.ending == 0) S.ending = 3;
}

static void show_clock(void) {
    int rest = TIME_LIMIT - S.minutes;
    if (rest < 0) rest = 0;
    printf("  挂钟：23:%02d   还剩 %d 分钟   错误 %d/%d\n",
           S.minutes > 59 ? 59 : S.minutes, rest, S.mistakes, MAX_MISTAKE);
}

/* 三种失败/成功文本 —— 读完直接退出程序 */
static void finish(void) {
    clear_screen();
    hr();
    switch (S.ending) {
    case 1:
        puts("  结局 · 走出去的人");
        thin();
        puts("  黄铜钥匙插进第一道锁，电闸的灯绿了，第二道锁“啪”地弹开；");
        puts("  数字键盘上，你把那支曲子最后唱了一遍——五、二、六、三。");
        puts("");
        puts("  大门向外敞开，冷风把满地浮尘吹成一片白。你回头看了一眼：");
        puts("  书房的灯还亮着，像有人坐在那里写字；然后灯泡一只一只熄灭，");
        puts("  从最里面那一间开始。");
        puts("");
        puts("  口袋里，祖父的怀表贴着胸口，停在他离开的那个时刻。");
        puts("  他没有留下遗产，只留下一栋装满他一生谜题的房子。");
        puts("  而你，是唯一看懂了他的人。");
        break;
    case 2:
        puts("  结局 · 房子记得所有人");
        thin();
        puts("  挂钟敲响了十二下。灰尘从房梁落下，墙纸开始卷曲，楼梯发出");
        puts("  很长的呻吟。你听见房子在呼吸——它收回了借出去的一切：");
        puts("  光、时间、还有你走过的那些台阶。");
        puts("");
        puts("  最后一次钟声落下时，门厅里已经没有人了。");
        puts("  只有大门上多了一道新的、很浅的手印。");
        break;
    case 3:
        puts("  结局 · 第五次错误");
        thin();
        puts("  第五次错误之后，你听见有人在你耳边叹了口气，像纸被翻过去。");
        puts("  墙上的挂钟停了。所有门同时向内合上，没有一点声音。");
        puts("  指尖触到的墙上写满了同一句话：“再想一遍。”");
        break;
    default:
        break;
    }
    thin();
    printf("  本局记录：用时 %d 分钟 / 错误 %d 次 / 提示 %d 次\n",
           S.minutes, S.mistakes,
           S.hintUsed[0] + S.hintUsed[1] + S.hintUsed[2] + S.hintUsed[3] + S.hintUsed[4]);
    hr();
    exit(0);
}

/* =====================================================================
 *  ④ 输入
 * ===================================================================== */

/* 读一个整数。返回选中的值；输入 q 直接退出游戏 */
static int ask_int(int lo, int hi) {
    char buf[64];
    for (;;) {
        printf("\n  请输入 [%d-%d]，q 退出 > ", lo, hi);
        fflush(stdout);
        if (!fgets(buf, sizeof buf, stdin)) exit(0);
        if (buf[0] == 'q' || buf[0] == 'Q') exit(0);
        int v = atoi(buf);
        if (v >= lo && v <= hi) return v;
        printf("  无效输入，请输入 %d 到 %d 之间的数字。\n", lo, hi);
    }
}

/* 读 n 位数字。成功返回 1；输入 q 返回 0（表示放弃） */
static int ask_code(int n, char *out) {
    char buf[64];
    for (;;) {
        printf("\n  输入 %d 位数字（q 放弃）> ", n);
        fflush(stdout);
        if (!fgets(buf, sizeof buf, stdin)) exit(0);
        size_t len = strcspn(buf, "\r\n");
        buf[len] = '\0';
        if (len >= 1 && (buf[0] == 'q' || buf[0] == 'Q')) return 0;
        int ok = (len == (size_t)n);
        for (size_t i = 0; ok && i < len; i++)
            if (buf[i] < '0' || buf[i] > '9') ok = 0;
        if (!ok) { printf("  要正好 %d 位数字（只能是 0-9）。\n", n); continue; }
        strcpy(out, buf);
        return 1;
    }
}

/* =====================================================================
 *  ⑤ 五个谜题
 * ===================================================================== */

/* —— 谜题 1：五色书。点一本书，它滑到左边的下一个空位 —— */
static void puzzle_books(void) {
    const char *shelf[5] = { "蓝", "紫", "红", "绿", "黄" };   /* 初始排列 */
    const char *want[5]  = { "红", "黄", "蓝", "绿", "紫" };   /* 日记里的顺序 */
    char order[5][4];
    int picked = 0;

    for (int i = 0; i < 5; i++) strcpy(order[i], shelf[i]);

    clear_screen();
    hr();
    puts("  五 色 书");
    thin();
    puts("  矮书柜顶层站着五本书。点一本，它会滑到左边的下一个空位，");
    puts("  排满五本即判定。日记里说，苏教过祖父一套排书的法子。");

    while (picked < 5) {
        thin();
        printf("  书架：");
        for (int i = 0; i < 5; i++)
            printf(" %s%s ", order[i], i < picked ? "*" : " ");   /* * 表示已就位 */
        printf("\n\n");
        for (int i = 0; i < 5; i++)
            printf("   %d) %s《%s》\n", i + 1, i < picked ? "已就位" : "拿起", order[i]);
        puts("   （* 号是已经排到左边的书；输入 0 把书全部放回原处）");
        int pick = ask_int(0, 5);

        if (pick == 0) {                       /* 0 = 全部放回，允许反悔 */
            for (int i = 0; i < 5; i++) strcpy(order[i], shelf[i]);
            picked = 0;
            puts("  你把书一本本放回原位。");
            continue;
        }
        pick--;
        if (pick < picked) { puts("  这本已经排在左边了。"); continue; }

        /* 把 order[pick] 挪到第 picked 个位置，其余顺次后移 */
        char tmp[4];
        strcpy(tmp, order[pick]);
        for (int i = pick; i > picked; i--) strcpy(order[i], order[i - 1]);
        strcpy(order[picked], tmp);
        picked++;
        Beep(392 + picked * 44, 90);
    }

    int ok = 1;
    for (int i = 0; i < 5; i++) if (strcmp(order[i], want[i]) != 0) ok = 0;

    thin();
    if (ok) {
        S.booksSolved = 1; S.smallKey = 1;
        puts("  五本书依次亮了。柜底“咔哒”弹出一只暗格——");
        puts("  【获得：小铜钥匙】齿痕细密，像配给某只小锁的。");
        Beep(660, 120); Beep(880, 160);
    } else {
        puts("  书页哗啦一声滑回原位——这个顺序不对。");
        puts("  再想想：“红引路，黄跟上，蓝接着，绿搭手，紫收尾”。");
        Beep(180, 260);
    }
    pass_time(MIN_PER_ACT);
}

/* —— 谜题 2：八音盒。听一遍，按原样复奏 —— */
static void puzzle_music(void) {
    const int melody[4] = { 5, 2, 6, 3 };            /* 音筒刻的编号 */
    const int freq[8]   = { 262, 294, 330, 392, 440, 523, 587, 659 };

    clear_screen();
    hr();
    puts("  八 音 盒");
    thin();
    puts("  黄铜音筒上刻着 1 到 8 的数字。先听一遍，再按原样复奏。");

    for (;;) {
        thin();
        printf("  ♪ 音筒唱：");
        for (int i = 0; i < 4; i++) {
            printf(" %d", melody[i]);
            fflush(stdout);
            Beep(freq[melody[i] - 1], 420);
            Sleep(160);
        }
        puts("");
        puts("   （听不清可以再听一遍）");

        char code[8];
        if (!ask_code(4, code)) { puts("  你合上了盒盖。"); return; }

        int ok = 1;
        for (int i = 0; i < 4; i++) if (code[i] - '0' != melody[i]) ok = 0;

        if (ok) {
            S.musicSolved = 1;
            thin();
            puts("  复奏正确。音筒底部“啪”地弹出一卷纸条，字迹是祖父的：");
            puts("");
            puts("  “苏走后，我把她哼过的调子刻进了音筒。");
            puts("   大门那只数字锁，密码就和这支曲子一样——");
            puts("   ★ 五、二、六、三 ★”");
            puts("");
            puts("  【记下了：大门数字锁 = 5263】");
            Beep(523, 120); Beep(659, 120); Beep(784, 240);
            pass_time(MIN_PER_ACT);
            return;
        }
        puts("  音筒“卡嗒”一声复位了——哪里不对。");
        Beep(180, 240);
    }
}

/* —— 谜题 3：保险箱（4 位数字，线索在日记里） —— */
static void puzzle_safe(void) {
    clear_screen();
    hr();
    puts("  保 险 箱");
    thin();
    puts("  箱门旁的便签：“我把它设在我最重要的那一年。”");

    char code[8];
    if (!ask_code(4, code)) { puts("  你退后一步。"); return; }

    if (strcmp(code, "1986") == 0) {
        S.safeOpen = 1; S.brassKey = 1;
        puts("");
        puts("  四声轻响，箱门弹开。【获得：黄铜大门钥匙】");
        puts("  钥匙下压着一张纸条：");
        puts("  “电在屋下，我把它留给了还记得那年春天的人。”");
        Beep(660, 140); Beep(990, 200);
    } else {
        mistake("保险箱密码错误");
        puts("  转盘空转了一圈，箱门纹丝不动。");
    }
    pass_time(MIN_PER_ACT);
}

/* —— 谜题 4：木箱四位转盘（0417，线索在账本里） —— */
static void puzzle_crate(void) {
    clear_screen();
    hr();
    puts("  上 锁 的 木 箱");
    thin();
    puts("  四位黄铜转盘，锈得厉害。箱盖上刻着一行小字：");
    puts("  “密码还是她的生日，她总说四月是开始。”");

    char code[8];
    if (!ask_code(4, code)) { puts("  你松开手，转盘停在一半。"); return; }

    if (strcmp(code, "0417") == 0) {
        S.crateOpen = 1; S.watch = 1;
        puts("");
        puts("  四位转盘依次落定，“咔哒”。木箱开了。");
        puts("  毛毯底下躺着一只黄铜怀表，表链断了。");
        puts("  表针停在 凌晨 3 点 12 分——和书房那面挂钟一模一样。");
        puts("  表盖内侧刻着：“给苏。等钟再走起来，我就回家。”");
        puts("  【获得：祖父的怀表】");
        Beep(587, 140); Beep(740, 200);
    } else {
        mistake("木箱转盘密码错误");
        puts("  转盘纹丝不动。");
    }
    pass_time(MIN_PER_ACT);
}

/* —— 谜题 5：配电箱三把闸刀（312，线索在挂钟上） —— */
static void puzzle_breaker(void) {
    clear_screen();
    hr();
    puts("  配 电 箱");
    thin();
    puts("  盖子上的粉笔字：“顺序就停在书房那只钟上。”");
    puts("  三把闸刀，编号 1 / 2 / 3，每次只能拉一把。");

    char seq[8] = "";
    for (int i = 0; i < 3; i++) {
        thin();
        printf("  第 %d 把闸刀：\n", i + 1);
        for (int k = 1; k <= 3; k++) {
            int used = 0;
            for (int j = 0; j < i; j++) if (seq[j] - '0' == k) used = 1;
            printf("   %d) 闸刀 %d%s\n", k, k, used ? "（已合上）" : "");
        }
        int pick = ask_int(1, 3);
        int used = 0;
        for (int j = 0; j < i; j++) if (seq[j] - '0' == pick) used = 1;
        if (used) { puts("  这把已经合上了。"); i--; continue; }
        seq[i] = (char)('0' + pick);
        seq[i + 1] = '\0';
        Beep(300, 90);
    }

    thin();
    if (strcmp(seq, "312") == 0) {
        S.powerOn = 1;
        puts("  Ⅲ、Ⅰ、Ⅱ 依次落下。墙里“轰”地活了过来——");
        puts("  地下室亮了，门厅大门上的指示灯也绿了。");
        Beep(392, 120); Beep(523, 120); Beep(659, 240);
    } else {
        mistake("闸刀顺序错误，配电箱跳闸了");
        puts("  闸刀炸开一团蓝白色的火花，三把闸刀全部弹回原位。");
        S.fuseIn = 1;                 /* 保险丝还在，下次还能再试 */
    }
    pass_time(MIN_PER_ACT);
}

/* =====================================================================
 *  ⑥ 三个房间的可交互动作
 *     每个动作都是 void(void)：只改全局状态 + 打印文字。
 * ===================================================================== */

/* ---------------- 门厅 ---------------- */

static void act_shoe_cabinet(void) {
    if (S.battery) { puts("  柜子里只剩下旧鞋。鞋尖全都朝里，像随时准备离开。"); pass_time(MIN_PER_ACT); return; }
    S.battery = 1;
    puts("  你拉开柜门。上层塞着一节电池，标签早已褪色。");
    puts("  【获得：一号电池】——沉得不像一节旧电池。");
    puts("  也许该把它装进手电筒里。");
    pass_time(MIN_PER_ACT);
}

static void act_clock_stand(void) {   /* 祖父的座钟：需要小铜钥匙 */
    if (S.gear) { puts("  钟摆慢悠悠地晃，像在数一种不同的时间。"); pass_time(MIN_PER_ACT); return; }
    if (S.clockOpen) {
        S.gear = 1;
        puts("  你从背板的暗格里捏出一枚黄铜齿轮。");
        puts("  【获得：黄铜齿轮】——大小正好卡进八音盒的发条槽。");
        pass_time(MIN_PER_ACT);
        return;
    }
    puts("  钟摆慢悠悠地晃，摆锤下面刻着一行小字：");
    puts("  “时间不是我量出来的，是苏。”");
    puts("  背板用一片小铜锁锁着——锁孔细得像给怀表上弦用的钥匙。");
    if (!S.smallKey) { puts("  你手上没有这样的钥匙。"); pass_time(MIN_PER_ACT); return; }
    S.clockOpen = 1;
    puts("  小铜钥匙在锁孔里转了半圈，背板“啪”地弹开一条缝——");
    puts("  里面躺着一枚黄铜齿轮。");
    pass_time(MIN_PER_ACT);
}

static void act_music_box(void) {
    if (S.musicSolved) { puts("  八音盒安安静静，像是唱累了。"); pass_time(MIN_PER_ACT); return; }
    if (!S.gear) {
        puts("  你掀开弧形木盖——发条槽里空空的，少一枚齿轮。");
        pass_time(MIN_PER_ACT);
        return;
    }
    puzzle_music();
}

static void act_mirror(void) {
    puts("  你抬手擦了擦灰。镜子里的你，比真实的你慢了半拍才放下手。");
    pass_time(MIN_PER_ACT);
}

static void act_gate(void) {
    clear_screen();
    hr();
    puts("  大 门 · 三 道 锁");
    thin();
    printf("  第一道（钥匙孔）：%s\n", S.brassKey ? "★ 黄铜钥匙" : "空的");
    printf("  第二道（电    锁）：%s\n", S.powerOn ? "★ 已通电" : "没电");
    printf("  第三道（数字锁）：%s\n", S.keypadSolved ? "★ 已解开" : "四位数字");
    thin();

    if (!S.brassKey) {
        puts("  第一道锁是长条形的钥匙孔。你手上没有能插进去的东西。");
        pass_time(MIN_PER_ACT);
        return;
    }
    if (!S.powerOn) {
        puts("  黄铜钥匙转了一半卡住了——第二道锁要等电闸通电才会松。");
        pass_time(MIN_PER_ACT);
        return;
    }
    if (!S.keypadSolved) {
        puts("  数字键盘亮起来，屏幕泛着绿光，等待四位数字。");
        puts("  键盘上方刻着一句：“把曲子唱给大门听。”");
        char code[8];
        if (!ask_code(4, code)) { puts("  你收回手。"); pass_time(MIN_PER_ACT); return; }
        if (strcmp(code, "5263") == 0) {
            S.keypadSolved = 1;
            puts("  第三道锁“嗒”地弹开。三声轻响连成一线——大门松动了。");
            Beep(523, 120); Beep(659, 120); Beep(784, 120); Beep(1046, 260);
        } else {
            mistake("大门数字锁密码错误");
            pass_time(MIN_PER_ACT);
            return;
        }
    }
    /* 三把锁都开了 */
    S.ending = 1;
    pass_time(0);
}

/* ---------------- 书房 ---------------- */

static void act_desk(void) {
    if (S.flash || S.torch) { puts("  抽屉里只剩下旧票据和干掉的胶水瓶。"); pass_time(MIN_PER_ACT); return; }
    S.flash = 1;
    puts("  你拉开抽屉，摸到一只金属圆筒——");
    puts("  【获得：手电筒（空）】按下开关只有弹簧的响声。");
    if (S.battery) puts("  背包里已经有电池了：在背包里可以把两件东西组合起来。");
    pass_time(MIN_PER_ACT);
}

static void act_bookshelf(void) {
    if (S.diary) { puts("  那本夹着纸的书已经空了。"); pass_time(MIN_PER_ACT); return; }
    S.diary = 1;
    clear_screen();
    hr();
    puts("  日 记 残 页");
    thin();
    puts("  纸很脆，边缘被水泡得发皱。祖父的字很小，很密：");
    puts("");
    puts("  “一九八六年春天，我们搬进这栋房子。");
    puts("   苏说，从今天起，所有的日子都从这里算起。");
    puts("   所以我把保险箱的转盘，也设在了这一年。”");
    puts("");
    puts("  背面还有两行更小的字：");
    puts("  “如果你要往下走，记得先带着光。”");
    puts("  “书柜里那五本书，按苏教我的法子排——");
    puts("   ★ 红引路，黄跟上，蓝接着，绿搭手，紫收尾 ★”");
    puts("");
    puts("  【记下了：保险箱密码与 1986 有关；五色书的顺序为 红黄蓝绿紫】");
    pass_time(MIN_PER_ACT);
}

static void act_wall_clock(void) {
    clear_screen();
    hr();
    puts("  挂 钟");
    thin();
    puts("  钟摆停在半空，指针停在 ★ 3 点 12 分 ★。");
    puts("  十年了，没有人给这面钟上过弦。");
    puts("  它停在这里，像谁特意留下的两个数字。");
    pass_time(MIN_PER_ACT);
}

static void act_low_shelf(void) {
    if (S.booksSolved) { puts("  暗格空了，只留一点淡淡的皂角香。"); pass_time(MIN_PER_ACT); return; }
    puzzle_books();
}

static void act_safe(void) {
    if (S.safeOpen) { puts("  保险箱已经开了，里面只剩一块旧绒布。"); pass_time(MIN_PER_ACT); return; }
    puzzle_safe();
}

/* ---------------- 地下室 ---------------- */

static void act_cabinet(void) {
    if (!S.cabOpen) {
        if (!S.rustyKey) { puts("  柜门锁着。"); pass_time(MIN_PER_ACT); return; }
        S.cabOpen = 1;
        puts("  锈铜钥匙转了半圈，“嗒”。柜门滑开——");
        puts("  里面躺着一截陶瓷保险丝，和一本浸过水的账本。");
        thin();
        puts("  账本上的字迹洇开了一半：");
        puts("  “一九八六年四月十七日。苏走后的第一个春天。");
        puts("   地窖那只木箱换了新锁——密码还是她的生日。");
        puts("   她总说，四月是开始。”");
        puts("  【记下了：木箱密码与 0417 有关】");
        Beep(520, 120);
        pass_time(MIN_PER_ACT);
        return;
    }
    if (!S.fuse) {
        S.fuse = 1;
        puts("  【获得：陶瓷保险丝】——角落里亮着红色指示灯的配电箱正缺这个。");
    } else {
        puts("  柜子里空了，只有一层铁锈末和樟脑的气味。");
    }
    pass_time(MIN_PER_ACT);
}

static void act_crate(void) {
    if (S.crateOpen) { puts("  木箱里铺着旧毛毯，空了一半——另一半被谁先带走了。"); pass_time(MIN_PER_ACT); return; }
    puzzle_crate();
}

static void act_breaker(void) {
    if (S.powerOn) { puts("  三把闸刀都合着，电流在墙里嗡嗡地走。"); pass_time(MIN_PER_ACT); return; }
    if (!S.fuseIn) {
        if (!S.fuse) {
            puts("  保险丝槽是空的。这时候合闸，只会跳起一团火花。");
            pass_time(MIN_PER_ACT);
            return;
        }
        S.fuseIn = 1; S.fuse = 0;
        puts("  保险丝“咔”地嵌进卡槽，小灯转为安稳的黄光。");
        puts("  可以拉闸了——顺序藏在挂钟上。");
        pass_time(MIN_PER_ACT);
        return;
    }
    puzzle_breaker();
}

/* ---------------- 通用动作 ---------------- */

static void act_bag(void) {
    clear_screen();
    hr();
    puts("  背 包");
    thin();
    struct { int has; const char *icon; const char *name; const char *desc; } items[] = {
        { S.rustyKey,  "[钥]", "锈铜钥匙",     "信里夹着的东西，铜锈绿得像发霉的月亮。" },
        { S.battery,   "[电]", "一号电池",     "沉甸甸的干电池，标签早已褪色。" },
        { S.flash,     "[筒]", "手电筒（空）", "按下开关只有弹簧的响声。" },
        { S.torch,     "[光]", "手电筒（亮）", "光柱又稳又亮，能刺开地窖里的黑。" },
        { S.diary,     "[纸]", "日记残页",     "一九八六年春天……保险箱设在1986；五色书 红黄蓝绿紫。" },
        { S.smallKey,  "[钥]", "小铜钥匙",     "齿痕细密，像配给某只小锁的。" },
        { S.gear,      "[齿]", "黄铜齿轮",     "大小正好卡进八音盒的发条槽。" },
        { S.fuse,      "[丝]", "陶瓷保险丝",   "老式陶瓷保险丝，两头是黄铜帽。" },
        { S.brassKey,  "[钥]", "黄铜大门钥匙", "齿口薄得像刀刃，配大门第一道锁。" },
        { S.watch,     "[表]", "祖父的怀表",   "表针停在凌晨 3 点 12 分，表盖内侧刻着“给苏”。" },
    };
    int n = (int)(sizeof items / sizeof items[0]), shown = 0;
    for (int i = 0; i < n; i++) {
        if (!items[i].has) continue;
        shown++;
        printf("  %s %-12s %s\n", items[i].icon, items[i].name, items[i].desc);
    }
    if (!shown) puts("  （空的）");

    /* 组合：电池 + 手电筒 */
    if (S.flash && S.battery && !S.torch) {
        thin();
        puts("  你可以把电池装进手电筒。");
        printf("  1) 组合  2) 合上背包\n");
        if (ask_int(1, 2) == 1) {
            S.flash = 0; S.battery = 0; S.torch = 1;
            puts("  你把电池塞进手电筒，拧紧尾盖。黑暗里亮起一小团光。");
            puts("  【手电筒亮了】楼梯井就在门厅东边，该下去了。");
            Beep(700, 140);
        }
    }
    pass_time(MIN_PER_ACT);
}

/* 提示：按当前进度给出，越问越直白（每次扣 2 分钟） */
static void act_hint(void) {
    int topic;
    if (!S.torch)                    topic = 0;
    else if (!S.safeOpen)            topic = 1;
    else if (!S.powerOn)             topic = 2;
    else if (!S.crateOpen)           topic = 3;
    else                             topic = 4;

    static const char *hint1[5] = {
        "先找光：书房里应该有能发光的东西；门厅的鞋柜，也不像只装旧鞋。",
        "便签说“最重要的那一年”。书房书架里夹着一张日记残页，去看看。",
        "配电箱缺一枚保险丝。地下室储物柜上挂着铜锁——那把锈铜钥匙正好配它。",
        "地下室角落的木箱换过一把四位转盘锁。储物柜里那本账本记着一个日期。",
        "大门数字锁的线索在八音盒里：五色书 → 小铜钥匙 → 座钟背板 → 齿轮 → 八音盒。",
    };
    static const char *hint2[5] = {
        "手电筒在书房写字台的抽屉里，电池在门厅鞋柜上层。拿到后在背包里组合。",
        "日记写着“一九八六年春天，我们搬进这栋房子”，密码就是 1986。",
        "保险丝装进配电箱的卡槽；闸刀顺序藏在挂钟上：3 点 12 分，就是 3、1、2。",
        "账本写“四月十七日”——密码是 0417。",
        "五色书按 红黄蓝绿紫 排；八音盒装上齿轮后复奏 5-2-6-3，就是大门密码 5263。",
    };

    int lv = S.hintUsed[topic] > 0 ? 1 : 0;   /* 第一次给方向，第二次直接给答案 */
    S.hintUsed[topic]++;

    clear_screen();
    hr();
    puts("  提 示");
    thin();
    puts(lv == 0 ? hint1[topic] : hint2[topic]);
    thin();
    printf("  （问提示花掉 %d 分钟，还剩 %d 分钟）\n", MIN_PER_HINT, TIME_LIMIT - S.minutes);
    pass_time(MIN_PER_HINT);
    pause_line();
}

static void act_help(void) {
    clear_screen();
    hr();
    puts("  怎 么 玩");
    thin();
    puts("  · 每回合从菜单里选一个编号：看/拿/开/去某个房间。");
    puts("  · 每个动作消耗 1 分钟；问提示消耗 2 分钟。");
    puts("  · 到 60 分钟（午夜）房子就收走你；错满 5 次也会结束。");
    puts("  · 输入 q 随时退出。");
    thin();
    puts("  目标：凑齐三把锁 —— 黄铜钥匙 / 通电 / 大门数字锁。");
    pause_line();
}

/* =====================================================================
 *  ⑦ 菜单装配：一回合 = 显示 + 选择 + 执行
 * ===================================================================== */

typedef struct {
    const char *(*label)(void);   /* 动态标签：随状态变化（函数指针） */
    void (*act)(void);            /* 选中后执行什么  （函数指针） */
} Action;

static Action menu[12];
static int menu_n;

static void add_action(const char *(*label)(void), void (*act)(void)) {
    if (menu_n < 12) { menu[menu_n].label = label; menu[menu_n].act = act; menu_n++; }
}

/* —— 下面这些 label() 各自返回一句"当前该显示什么" —— */
static const char *l_shoe(void)   { return S.battery ? "查看 鞋柜（空的）" : "查看 鞋柜（似乎有东西反光）"; }
static const char *l_stand(void)  { return S.gear ? "查看 座钟" : S.clockOpen ? "拿取 座钟里的齿轮" : "查看 座钟（背板锁着小锁）"; }
static const char *l_music(void)  { return S.musicSolved ? "收好 八音盒" : S.gear ? "打开 八音盒" : "查看 八音盒（缺一枚齿轮）"; }
static const char *l_gate(void)   { return S.keypadSolved ? "推开 大门（已解锁）" : "推开 大门（三道锁）"; }
static const char *l_desk(void)   { return (S.flash || S.torch) ? "查看 写字台" : "拉开 写字台的抽屉"; }
static const char *l_shelf2(void) { return S.diary ? "翻看 书架（空的）" : "翻看 书架（有张纸露出半个角）"; }
static const char *l_low(void)    { return S.booksSolved ? "查看 矮书柜（暗格已开）" : "翻动 矮书柜上的五色书"; }
static const char *l_safe(void)   { return S.safeOpen ? "查看 保险箱（已打开）" : "转动 保险箱的转盘"; }
static const char *l_cab(void)    { return S.cabOpen ? (S.fuse ? "查看 储物柜" : "拿走 储物柜里的保险丝") : "打开 储物柜（挂着铜锁）"; }
static const char *l_crate(void)  { return S.crateOpen ? "查看 木箱（已开）" : "查看 上锁的木箱（四位转盘）"; }
static const char *l_breaker(void){ return S.powerOn ? "查看 配电箱（运行中）" : S.fuseIn ? "拉下 闸刀（三把）" : S.fuse ? "安装 保险丝" : "查看 配电箱"; }

static const char *l_to_study(void)  { return "去 书房"; }
static const char *l_to_cellar(void) { return S.torch ? "下楼 去 地下室" : "下楼（楼梯井黑得看不见）"; }
static const char *l_to_hall(void)   { return "回 门厅"; }
static const char *l_bag(void)       { return "打开 背包"; }
static const char *l_hint(void)      { return "问 提示（-2 分钟）"; }
static const char *l_help(void)      { return "怎么玩"; }
static const char *l_mirror(void)    { return "查看 镜子"; }
static const char *l_clock(void)     { return "查看 挂钟（指针停在半空）"; }

static void go_cellar(void) {
    if (!S.torch) {
        clear_screen();
        hr();
        puts("  石阶尽头是纯粹的黑暗，黑暗里有东西在呼吸。");
        puts("  你还缺一盏能亮的灯。");
        pass_time(MIN_PER_ACT);
        pause_line();
        return;
    }
    S.room = R_CELLAR;
    pass_time(MIN_PER_ACT);
}
static void go_study(void) { S.room = R_STUDY; pass_time(MIN_PER_ACT); }
static void go_hall(void)  { S.room = R_HALL;  pass_time(MIN_PER_ACT); }

static void build_menu(void) {
    menu_n = 0;
    switch (S.room) {
    case R_HALL:
        add_action(l_shoe,    act_shoe_cabinet);
        add_action(l_stand,   act_clock_stand);
        add_action(l_music,   act_music_box);
        add_action(l_mirror,  act_mirror);
        add_action(l_gate,    act_gate);
        add_action(l_to_study, go_study);
        add_action(l_to_cellar, go_cellar);
        break;
    case R_STUDY:
        add_action(l_desk,    act_desk);
        add_action(l_shelf2,  act_bookshelf);
        add_action(l_clock,   act_wall_clock);
        add_action(l_low,     act_low_shelf);
        add_action(l_safe,    act_safe);
        add_action(l_to_hall, go_hall);
        break;
    case R_CELLAR:
        add_action(l_cab,     act_cabinet);
        add_action(l_crate,   act_crate);
        add_action(l_breaker, act_breaker);
        add_action(l_to_hall, go_hall);
        break;
    }
    add_action(l_bag,  act_bag);
    add_action(l_hint, act_hint);
    add_action(l_help, act_help);
}


static void show_room(void) {
    const char *name = S.room == R_HALL ? "门 厅" : S.room == R_STUDY ? "书 房" : "地 下 室";
    clear_screen();
    hr();
    printf("  【%s】", name);
    show_clock();
    thin();
    switch (S.room) {
    case R_HALL:
        puts("  壁纸卷着边，铜灯在头顶轻轻晃。北面是书房的门，东边一段石阶");
        puts("  往地下沉下去，南面就是那扇有三道锁的大门。");
        break;
    case R_STUDY:
        puts("  一整面墙的书，写字台的抽屉半开着，靠墙立着一面挂钟和一只");
        puts("  保险箱。空气里有旧纸和烟草的味道。");
        break;
    case R_CELLAR:
        puts("  低矮的混凝土顶，锈水管从头顶压过来。角落里堆着木箱，");
        puts("  一整面墙上挂着配电箱，红色指示灯一明一暗。");
        break;
    default:
        break;
    }
    thin();
    for (int i = 0; i < menu_n; i++)
        printf("   %d) %s\n", i + 1, menu[i].label());
    thin();
}

int main(void) {
    /* 控制台按 UTF-8 工作，中文才不会乱成方块 */
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);

    /* —— 初始状态 —— */
    memset(&S, 0, sizeof S);
    S.rustyKey = 1;                 /* 信封里那把锈钥匙 */
    S.room = R_HALL;

    /* —— 开场 —— */
    hr();
    puts("                          回  声  宅");
    puts("                      ECHO MANOR  ·  C 版");
    hr();
    puts("");
    puts("  十年前的四月，祖父在这栋房子里失踪。警察搜了三天，只带走");
    puts("  一份报告：一扇从里面锁上的门，和一只停在 3 点 12 分的挂钟。");
    puts("");
    puts("  今天凌晨，一只没有寄件人的信封落进你的信箱——里面只有一把");
    puts("  锈成绿色的钥匙，和一张字条：");
    puts("");
    puts("  “房子会在午夜收回它借出去的一切。大门上有三道锁：");
    puts("    钥匙、电、数字。钟一响，就别再回头了。”");
    puts("");
    puts("  你推开门。门在身后锁上，挂钟从 23:00 起跳。");
    puts("  你只有六十分钟。");
    hr();
    pause_line();

    /* —— 主循环：一回合 = 显示 + 选择 + 执行 —— */
    while (S.ending == 0) {
        build_menu();                    /* 每回合按房间与当前状态重新装配菜单 */
        show_room();
        int choice = ask_int(1, menu_n);
        menu[choice - 1].act();          /* ← 函数指针调用：这就是"物件的行为" */
        if (S.ending == 0 && (S.mistakes >= MAX_MISTAKE || S.minutes >= TIME_LIMIT))
            break;
    }

    if (S.ending == 0) S.ending = S.mistakes >= MAX_MISTAKE ? 3 : 2;
    finish();
    return 0;
}
