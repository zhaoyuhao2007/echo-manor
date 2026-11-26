/* =====================================================================
 *  《回 声 宅》 —— C 语言版 · 第一人称画面版      echo_house_fp.c
 * =====================================================================
 *
 *  和 echo_house_c.c（菜单文字版）的区别只有一点：
 *     文字版是"读一段描述，选一个编号"；
 *     本版是在命令行窗口里真的画出一间房间 —— 墙、地、天花板、家具，
 *     用 W A S D 走动，用鼠标一样的方向键转头，走到东西前面按 E 互动。
 *
 *  画面怎么来的？—— 光线投射（raycasting）
 *     屏幕上每一列像素，都从眼睛发出一条射线；射线撞到哪面墙，
 *     就把那一列画成墙的颜色，墙的高度由"撞到墙的距离"决定：
 *     越近越高，越远越矮。所有列拼起来，就是一间有立体感的房间。
 *     1992 年的《德军总部 3D》用的就是这个办法，只是它画的是像素，
 *     我们画的是字符。
 *
 *  代码结构（从上往下正好是阅读顺序）：
 *     ① 常量与全局状态          ② 控制台与画帧（ANSI 转义）
 *     ③ 地图与家具（数据）       ④ 光线投射（把地图变成画面）
 *     ⑤ 谜题（模态面板）         ⑥ 动作表（函数指针）
 *     ⑦ 主循环
 *
 *  编译（本机 MSVC，务必加 /utf-8）：
 *      cl /nologo /std:c17 /W4 /utf-8 /Fe:回声宅C画面版.exe echo_house_fp.c
 *
 *  调试用：
 *      echo_house_fp.exe --dump    把四个固定机位的画面（无色）写进 dump.txt
 * ===================================================================== */

#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <windows.h>
#include <conio.h>

/* =====================================================================
 *  ① 常量与全局状态
 * ===================================================================== */

#define VIEW_W   64          /* 3D 视口宽（字符列数） */
#define VIEW_H   22          /* 3D 视口高（字符行数） */
#define HUD_W    40          /* 右侧状态栏宽 */
#define FRAME_W  (VIEW_W + 1 + HUD_W)
#define FRAME_H  (VIEW_H + 2)        /* 标题 1 行 + 视口 + 底部 1 行 */

#define TIME_LIMIT   60      /* 午夜之前 60 分钟 */
#define MAX_MISTAKE  5
#define MIN_PER_ACT  1
#define MIN_PER_HINT 2

#define FOV      0.75        /* 视锥的一半宽度：0.75 ≈ 74° 水平视野 */
#define UNIT_H   (VIEW_H * 0.72)  /* 1 米在距离 1 处占多少行 */
#define EYE      1.6         /* 眼睛高度（米） */
#define CEIL_H   3.2         /* 层高（米） */

#define PI 3.14159265358979323846

enum { K_UP = 1000, K_DOWN, K_LEFT, K_RIGHT };

typedef struct {
    /* —— 道具 —— */
    int rustyKey, battery, flash, torch, diary;
    int smallKey, gear, fuse, brassKey, watch;
    /* —— 进度 —— */
    int booksSolved, clockOpen, musicSolved;
    int safeOpen, crateOpen, cabOpen, fuseIn, powerOn, keypadSolved;
    /* —— 计时与统计 —— */
    int minutes, mistakes, hintUsed[5], ending;
    int floorNo;             /* 0 = 一楼（门厅 + 书房），1 = 地下室 */
} GameState;

static GameState S;

typedef struct { double x, y, ang; } Player;
static Player P;

/* 当前画面缓冲：所有绘制都先写进这几个二维数组，最后一次性拼成字符串。
 * 每个格子存 1 个字符 + 前景 RGB + 背景 RGB（真彩色，才能按距离平滑压暗）。 */
static const char *g_ch[VIEW_H][VIEW_W];
static int  g_fr[VIEW_H][VIEW_W], g_fgc[VIEW_H][VIEW_W], g_fb[VIEW_H][VIEW_W];
static int  g_br[VIEW_H][VIEW_W], g_bgc[VIEW_H][VIEW_W], g_bb[VIEW_H][VIEW_W];

static void set_cell(int r, int c, const char *ch,
                     int fr, int fgc, int fb, int br, int bgc, int bb) {
    g_ch[r][c] = ch;
    g_fr[r][c] = fr; g_fgc[r][c] = fgc; g_fb[r][c] = fb;
    g_br[r][c] = br; g_bgc[r][c] = bgc; g_bb[r][c] = bb;
}

static double g_zbuf[VIEW_W];    /* 每一列的墙距离，画家具时用来做遮挡 */
static char   g_frame[262144];
static size_t g_flen;
static int    g_vt;              /* 控制台是否支持 ANSI 转义 */
static int    g_color = 1;
static char   g_status[160];     /* 底部那行提示 */
static int    g_auto;            /* 自动测试：不读键盘，按脚本输入 */
static const char *g_script[64]; /* 自动测试用的输入队列 */
static int    g_si;

/* =====================================================================
 *  ② 控制台与画帧
 * ===================================================================== */

static void con_setup(void);
static void con_restore(void);

/* 显示宽度：中文算 2 格，ASCII 算 1 格，ANSI 转义序列算 0 格。
 * 没有它，右栏中文就会错位——这是终端里排版最容易踩的坑。 */
static int disp_w(const char *s) {
    int w = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        if (*p == 0x1b) {                       /* ESC [ ... m */
            while (*p && *p != 'm') p++;
            if (*p) p++;
            continue;
        }
        if (*p < 0x80) { w += 1; p++; continue; }
        int n = 0;
        unsigned int cp = 0;
        if ((*p & 0xE0) == 0xC0)      { n = 1; cp = *p & 0x1Fu; }
        else if ((*p & 0xF0) == 0xE0) { n = 2; cp = *p & 0x0Fu; }
        else if ((*p & 0xF8) == 0xF0) { n = 3; cp = *p & 0x07u; }
        else { p++; continue; }
        p++;
        for (int i = 0; i < n && *p; i++, p++) cp = (cp << 6) | (*p & 0x3Fu);
        int wide = (cp >= 0x1100 &&
                    (cp <= 0x115F || cp == 0x2329 || cp == 0x232A ||
                     (cp >= 0x2E80 && cp <= 0xA4CF) ||
                     (cp >= 0xAC00 && cp <= 0xD7A3) ||
                     (cp >= 0xF900 && cp <= 0xFAFF) ||
                     (cp >= 0xFE30 && cp <= 0xFE6F) ||
                     (cp >= 0xFF00 && cp <= 0xFF60) ||
                     (cp >= 0xFFE0 && cp <= 0xFFE6)));
        w += wide ? 2 : 1;
    }
    return w;
}

static void fb_str(const char *s) {
    /* 调试模式（--dump）不要颜色：把 ANSI 转义序列整段跳过 */
    if (!g_color) {
        while (*s) {
            if (*s == 0x1b) { while (*s && *s != 'm') s++; if (*s) s++; continue; }
            if (g_flen + 2 < sizeof g_frame) g_frame[g_flen++] = *s;
            s++;
        }
        return;
    }
    size_t n = strlen(s);
    if (g_flen + n + 2 < sizeof g_frame) { memcpy(g_frame + g_flen, s, n); g_flen += n; }
}
static void fb_ch(char c) {
    if (g_flen + 2 < sizeof g_frame) g_frame[g_flen++] = c;
}
static void fb_fmt(const char *fmt, ...) {
    va_list ap;
    char tmp[512];
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    fb_str(tmp);
}

/* 只在颜色真的变化时才写转义序列——一帧能省掉一半字节 */
static int g_last_fg = -1, g_last_bg = -1;
static void fb_fg(int r, int g, int b) {
    int key = (r << 16) | (g << 8) | b;
    if (g_color && key != g_last_fg) { fb_fmt("\x1b[38;2;%d;%d;%dm", r, g, b); g_last_fg = key; }
}
static void fb_bg(int r, int g, int b) {
    int key = (r << 16) | (g << 8) | b;
    if (g_color && key != g_last_bg) { fb_fmt("\x1b[48;2;%d;%d;%dm", r, g, b); g_last_bg = key; }
}

/* —— 右侧状态栏的 22 行文字 —— */
static char g_hud[VIEW_H][256];

static void hud_set(int row, const char *fmt, ...) {
    if (row < 0 || row >= VIEW_H) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_hud[row], sizeof g_hud[row], fmt, ap);
    va_end(ap);
}

/* =====================================================================
 *  ③ 地图
 *      '#' 门厅墙纸    '=' 书房木墙    '%' 地下室混凝土    '.' 可走地面
 *      一格 = 1 米
 * ===================================================================== */

#define MAPW0 28
#define MAPH0 16

static const char *MAP0[MAPH0] = {
    "############################",
    "=............===============",
    "=............===============",
    "=............===============",
    "=............===============",
    "=............===============",
    "=............===============",
    "#####.######################",
    "#..........................#",
    "#..........................#",
    "#........#.......#.........#",
    "#..........................#",
    "#..........................#",
    "#..........................#",
    "#..........................#",
    "############################",
};

#define MAPW1 20
#define MAPH1 11

static const char *MAP1[MAPH1] = {
    "%%%%%%%%%%%%%%%%%%%%",
    "%..................%",
    "%..................%",
    "%..................%",
    "%..................%",
    "%........#.........%",
    "%..................%",
    "%..................%",
    "%..................%",
    "%..................%",
    "%%%%%%%%%%%%%%%%%%%%",
};

static const char *const *cur_map(int *w, int *h) {
    if (S.floorNo == 0) { *w = MAPW0; *h = MAPH0; return MAP0; }
    *w = MAPW1; *h = MAPH1; return MAP1;
}

/* —— 家具：在 3D 里是一根"字母柱子"，走到跟前按 E 才认得出是什么 —— */
typedef struct {
    double x, y;
    const char *glyph;
    int    cr, cg, cb;         /* 家具本身的颜色（真彩） */
    double h, w;               /* 高、宽（米） */
    const char *name;
    void (*act)(void);
} Sprite;

/* 动作函数先声明：家具表里要存它们的地址（就是网页版 addHit 里那个 act） */
static void act_shoe(void);
static void act_stand(void);
static void act_music(void);
static void act_mirror(void);
static void act_gate(void);
static void act_down(void);
static void act_up(void);
static void act_desk(void);
static void act_shelf(void);
static void act_wall_clock(void);
static void act_low_shelf(void);
static void act_safe(void);
static void act_cabinet(void);
static void act_crate(void);
static void act_breaker(void);

static const Sprite SPR0[] = {
    /* 门厅 */
    {  2.6,  9.6, "n", 150, 110,  72, 1.5, 1.1, "鞋柜",           act_shoe  },
    {  2.4, 13.6, "T", 150, 100,  60, 2.2, 0.9, "祖父的座钟",     act_stand },
    {  6.2, 13.6, "m", 200, 160,  85, 0.7, 0.9, "八音盒",         act_music },
    { 14.0,  8.5, "|", 205, 212, 225, 1.9, 0.8, "穿衣镜",         act_mirror},
    { 13.5, 14.4, "D", 190, 150,  60, 2.6, 2.0, "大门 · 三道锁",  act_gate  },
    { 24.6, 12.8, "S",  95,  95, 100, 0.5, 1.6, "往地下的石阶",   act_down  },
    /* 书房 */
    {  2.0,  2.2, "B", 140,  95,  60, 2.4, 2.2, "书架",           act_shelf },
    {  5.4,  4.8, "d", 150, 110,  72, 0.9, 1.6, "写字台",         act_desk  },
    { 11.4,  1.5, "t", 170, 140,  95, 1.3, 0.6, "挂钟",           act_wall_clock },
    {  2.2,  5.4, "b", 135,  90,  55, 1.5, 1.8, "矮书柜 · 五色书",act_low_shelf },
    { 11.5,  5.4, "F", 100, 105, 115, 1.3, 0.8, "保险箱",         act_safe  },
};

static const Sprite SPR1[] = {
    {  2.2,  2.2, "c", 118, 118, 124, 1.9, 1.0, "储物柜 · 铜锁",  act_cabinet },
    { 14.6,  2.4, "x", 145, 105,  65, 1.2, 1.4, "上锁的木箱",     act_crate   },
    { 17.4,  5.6, "P",  92, 100, 110, 1.5, 1.0, "配电箱",         act_breaker },
    {  2.8,  9.2, "S",  95,  95, 100, 0.5, 1.6, "上楼的石阶",     act_up      },
};

#define N_SPR0 ((int)(sizeof SPR0 / sizeof SPR0[0]))
#define N_SPR1 ((int)(sizeof SPR1 / sizeof SPR1[0]))

static const Sprite *cur_spr(int *n) {
    if (S.floorNo == 0) { *n = N_SPR0; return SPR0; }
    *n = N_SPR1; return SPR1;
}

/* =====================================================================
 *  ④ 光线投射
 * ===================================================================== */

static int cell_at(int x, int y) {
    int w, h;
    const char *const *m = cur_map(&w, &h);
    if (x < 0 || y < 0 || x >= w || y >= h) return '#';
    return (unsigned char)m[y][x];
}

/* 墙的本色：门厅是米色墙纸，书房是深木，地下室是水泥 */
static void wall_base(char cell, int *r, int *g, int *b) {
    if (cell == '=')      { *r = 150; *g = 104; *b = 64;  }
    else if (cell == '%') { *r = 112; *g = 114; *b = 118; }
    else                  { *r = 176; *g = 146; *b = 112; }
}

/* 亮度系数：越远越暗，侧面再压一档 —— 一条公式代替 4 档色阶 */
static double shade(double d, int side) {
    double f = 1.30 - d * 0.085;
    if (f > 1.0) f = 1.0;
    if (f < 0.26) f = 0.26;
    if (side) f *= 0.78;
    return f;
}

/* 一列：从眼睛发出射线，走到撞墙为止（DDA 算法） */
static void cast_column(int c, double dx, double dy, double px, double py) {
    double camx = 2.0 * c / (double)VIEW_W - 1.0;
    double rx = dx + px * camx;
    double ry = dy + py * camx;

    int mx = (int)P.x, my = (int)P.y;
    double ddx = (rx == 0.0) ? 1e30 : fabs(1.0 / rx);
    double ddy = (ry == 0.0) ? 1e30 : fabs(1.0 / ry);
    int stepx, stepy;
    double sdx, sdy;
    if (rx < 0) { stepx = -1; sdx = (P.x - mx) * ddx; } else { stepx = 1; sdx = (mx + 1.0 - P.x) * ddx; }
    if (ry < 0) { stepy = -1; sdy = (P.y - my) * ddy; } else { stepy = 1; sdy = (my + 1.0 - P.y) * ddy; }

    int side = 0;
    char cell = '#';
    for (int guard = 0; guard < 96; guard++) {
        if (sdx < sdy) { sdx += ddx; mx += stepx; side = 0; }
        else           { sdy += ddy; my += stepy; side = 1; }
        cell = (char)cell_at(mx, my);
        if (cell != '.') break;
    }

    double perp = (side == 0) ? (sdx - ddx) : (sdy - ddy);
    if (perp < 0.06) perp = 0.06;
    g_zbuf[c] = perp;

    int top = (int)(VIEW_H / 2.0 - UNIT_H * (CEIL_H - EYE) / perp);
    int bot = (int)(VIEW_H / 2.0 + UNIT_H * EYE / perp);

    int br, bg, bb;
    wall_base(cell, &br, &bg, &bb);
    double f = shade(perp, side);

    /* 整面墙用实心块，靠"越远越暗"和墙脚阴影做出立体感 */
    for (int r = top; r <= bot; r++) {
        if (r < 0 || r >= VIEW_H) continue;
        double k = (r > bot - 2) ? f * 0.62 : f;     /* 贴地两行压暗 = 墙脚阴影 */
        /* 背景色和前景色取同一个值：字符之间那 1 像素的缝就不会露出来，
         * 整面墙才是一条连续的渐变，而不是一排竖栅栏。 */
        set_cell(r, c, "█", (int)(br * k), (int)(bg * k), (int)(bb * k),
                 (int)(br * k), (int)(bg * k), (int)(bb * k));
    }

    /* 墙裙：离地约 0.95 米画一条暗线，墙就有了"高度"，人也知道自己站在哪 */
    {
        int wain = bot - (int)(UNIT_H * 0.95 / perp);
        if (wain > top && wain < bot && wain >= 0 && wain < VIEW_H) {
            double kw = f * 0.52;
            set_cell(wain, c, "█", (int)(br * kw), (int)(bg * kw), (int)(bb * kw),
                     (int)(br * kw), (int)(bg * kw), (int)(bb * kw));
        }
    }
}

/* 画一家具：把它当成一块竖着的板子，投到屏幕上（和墙用同一个透视比例） */
static void draw_sprites(void) {
    int n;
    const Sprite *list = cur_spr(&n);
    double dx = cos(P.ang), dy = sin(P.ang);
    double px = -dy * FOV, py = dx * FOV;

    int idx[16];
    double dist[16];
    for (int i = 0; i < n; i++) {
        double ax = list[i].x - P.x, ay = list[i].y - P.y;
        idx[i] = i;
        dist[i] = ax * ax + ay * ay;
    }
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (dist[idx[j]] > dist[idx[i]]) { int t = idx[i]; idx[i] = idx[j]; idx[j] = t; }

    double invDet = 1.0 / (px * dy - dx * py);
    for (int k = 0; k < n; k++) {
        const Sprite *s = &list[idx[k]];
        double sx = s->x - P.x, sy = s->y - P.y;
        double tY = invDet * (-py * sx + px * sy);   /* 深度 */
        double tX = invDet * (dy * sx - dx * sy);    /* 横向偏移 */
        if (tY < 0.25) continue;

        int cx = (int)((VIEW_W / 2.0) * (1.0 + tX / tY));
        int bot = (int)(VIEW_H / 2.0 + UNIT_H * EYE / tY);
        int top = (int)(bot - UNIT_H * s->h / tY);
        /* 宽度按真实比例投影：1 米在距离 d 处约占 22/d 个字符宽，
         * 和墙的 1/d 透视同一套比例，家具才不会变成一根细柱子。 */
        int wc = (int)(s->w * 22.0 / tY);
        if (wc < 1) wc = 1;
        if (wc > 12) wc = 12;

        for (int c = cx - wc / 2; c <= cx + (wc - 1) / 2; c++) {
            if (c < 0 || c >= VIEW_W) continue;
            if (g_zbuf[c] < tY) continue;            /* 被墙挡住 */
            double sf = shade(tY, 0) * 0.92;
            for (int r = top; r <= bot; r++) {
                if (r < 0 || r >= VIEW_H) continue;
                set_cell(r, c, s->glyph,
                         (int)(s->cr * sf * 0.42), (int)(s->cg * sf * 0.42), (int)(s->cb * sf * 0.42),
                         (int)(s->cr * sf), (int)(s->cg * sf), (int)(s->cb * sf));
            }
        }
    }
}

/* 两点之间有没有墙挡着？
 * 只按距离和角度判断"对准"是不够的：站在门厅走廊贴着北墙，
 * 离书房那只矮书柜只有 2.2 米、方向也正，但中间隔着一堵墙。
 * 所以沿视线每 8 厘米采一格，撞到非地面就算被挡住。 */
static int los_clear(double x0, double y0, double x1, double y1) {
    double dx = x1 - x0, dy = y1 - y0;
    double d = sqrt(dx * dx + dy * dy);
    if (d < 1e-9) return 1;
    int steps = (int)(d / 0.08) + 1;
    for (int i = 1; i < steps; i++) {
        double t = (double)i / (double)steps;
        if (cell_at((int)floor(x0 + dx * t), (int)floor(y0 + dy * t)) != '.') return 0;
    }
    return 1;
}

/* —— 找一个"正对着、够近、而且中间没有墙"的家具 —— */
static const Sprite *aimed(void) {
    int n;
    const Sprite *list = cur_spr(&n);
    const Sprite *best = NULL;
    double bestd = 1e9;
    for (int i = 0; i < n; i++) {
        double ax = list[i].x - P.x, ay = list[i].y - P.y;
        double d = sqrt(ax * ax + ay * ay);
        if (d > 2.8) continue;
        double a = atan2(ay, ax) - P.ang;
        while (a > PI) a -= 2 * PI;
        while (a < -PI) a += 2 * PI;
        if (fabs(a) > 0.55) continue;
        if (!los_clear(P.x, P.y, list[i].x, list[i].y)) continue;
        if (d < bestd) { bestd = d; best = &list[i]; }
    }
    return best;
}

static const char *floor_name(void) {
    if (S.floorNo == 1) return "地下室";
    return (P.y < 7.5) ? "书房" : "门厅";
}

/* =====================================================================
 *  组装一帧：先铺天花板/地面，再叠墙和家具，右边拼状态栏
 * ===================================================================== */

static void build_hud(void) {
    memset(g_hud, 0, sizeof g_hud);
    int r = 0;
    hud_set(r++, "\x1b[97m 回 声 宅 \x1b[90m· C 语言版");
    hud_set(r++, "\x1b[90m ────────────────────────────────");
    hud_set(r++, "\x1b[0m 位置    \x1b[97m%s", floor_name());
    hud_set(r++, " 时间    \x1b[97m23:%02d", S.minutes > 59 ? 59 : S.minutes);
    hud_set(r++, " 剩余    \x1b[93m%d\x1b[0m 分钟", TIME_LIMIT - S.minutes);
    hud_set(r++, " 错误    %s%d\x1b[0m / %d",
            S.mistakes ? "\x1b[91m" : "\x1b[97m", S.mistakes, MAX_MISTAKE);
    hud_set(r++, "\x1b[90m ────────────────────────────────");
    hud_set(r++, "\x1b[0m 对准");

    const Sprite *a = aimed();
    if (a) {
        hud_set(r++, "   \x1b[97m%s", a->name);
        hud_set(r++, "   \x1b[92m[E]\x1b[0m 互动");
    } else {
        hud_set(r++, "   \x1b[90m（走到东西前面对准它）");
        hud_set(r++, "");
    }
    hud_set(r++, "\x1b[90m ────────────────────────────────");

    struct { int has; const char *name; } items[] = {
        { S.rustyKey, "锈铜钥匙" }, { S.battery, "一号电池" }, { S.flash, "手电筒（空）" },
        { S.torch, "手电筒（亮）" }, { S.diary, "日记残页" }, { S.smallKey, "小铜钥匙" },
        { S.gear, "黄铜齿轮" }, { S.fuse, "陶瓷保险丝" }, { S.brassKey, "黄铜大门钥匙" },
        { S.watch, "祖父的怀表" },
    };
    int cnt = 0;
    for (int i = 0; i < (int)(sizeof items / sizeof items[0]); i++) if (items[i].has) cnt++;
    hud_set(r++, "\x1b[0m 背包    \x1b[97m%d\x1b[0m 件", cnt);
    int shown = 0;
    for (int i = 0; i < (int)(sizeof items / sizeof items[0]) && shown < 3; i++)
        if (items[i].has) { hud_set(r++, "   \x1b[90m·\x1b[0m %s", items[i].name); shown++; }
    if (cnt > 3) hud_set(r++, "   \x1b[90m…还有 %d 件（按 I）", cnt - 3);
    for (int i = shown + (cnt > 3 ? 1 : 0); i < 3; i++) hud_set(r++, "");

    hud_set(r++, "\x1b[90m ────────────────────────────────");
    hud_set(r++, "\x1b[90m W A S D  前后左右");
    hud_set(r++, "\x1b[90m ← →      转身");
    hud_set(r++, "\x1b[90m E 互动   I 背包   M 地图");
    hud_set(r++, "\x1b[90m H 提示   Q 退出");
    for (; r < VIEW_H; r++) hud_set(r, "");
}

static void build_frame(void) {
    double dx = cos(P.ang), dy = sin(P.ang);
    double px = -dy * FOV, py = dx * FOV;

    /* 天花板 / 地面：按"地板投射"逐格算出世界坐标。
     * 第 r 行离眼睛的水平距离是固定的 d = UNIT_H * 眼高 / |r - 地平线|，
     * 于是每个格子都能反推出它在地图上的位置 —— 地面就有了方砖纹理。 */
    for (int r = 0; r < VIEW_H; r++) {
        int p = r - VIEW_H / 2;
        if (p == 0) {
            for (int c = 0; c < VIEW_W; c++) set_cell(r, c, " ", 60, 58, 55, 44, 42, 40);
            continue;
        }
        int isFloor = (p > 0);
        double d = UNIT_H * (isFloor ? EYE : (CEIL_H - EYE)) / fabs((double)p);
        double k = 1.05 - d * 0.022;
        if (k > 1.0) k = 1.0;
        if (k < 0.45) k = 0.45;
        for (int c = 0; c < VIEW_W; c++) {
            double camx = 2.0 * c / (double)VIEW_W - 1.0;
            double fx = P.x + d * (dx + px * camx);
            double fy = P.y + d * (dy + py * camx);
            int ix = (int)floor(fx), iy = (int)floor(fy);
            if (isFloor) {
                int br = (int)(58 * k), bg = (int)(54 * k), bb = (int)(46 * k);
                if (((ix + iy) & 1) == 0 && d < 14.0)
                    set_cell(r, c, "·", br + 28, bg + 26, bb + 22, br, bg, bb);
                else
                    set_cell(r, c, " ", br, bg, bb, br, bg, bb);
            } else {
                int br = (int)(40 * k), bg = (int)(38 * k), bb = (int)(40 * k);
                set_cell(r, c, " ", br, bg, bb, br, bg, bb);
            }
        }
    }

    for (int c = 0; c < VIEW_W; c++) cast_column(c, dx, dy, px, py);
    draw_sprites();
    build_hud();

    g_flen = 0;
    g_last_fg = g_last_bg = -1;
    if (g_vt && g_color) { fb_str("\x1b[H"); }

    /* 标题行 */
    fb_str("\x1b[0m");
    g_last_fg = g_last_bg = -1;
    {
        char tmp[64];
        snprintf(tmp, sizeof tmp, "23:%02d   剩 %d 分   错 %d/%d",
                 S.minutes > 59 ? 59 : S.minutes,
                 TIME_LIMIT - S.minutes, S.mistakes, MAX_MISTAKE);
        const char *left = " 回 声 宅  ECHO MANOR  |  ";
        fb_str(" \x1b[97m回 声 宅  \x1b[90mECHO MANOR  \x1b[90m|  ");
        int pad = FRAME_W - disp_w(left) - disp_w(tmp);
        if (pad < 0) pad = 0;
        for (int i = 0; i < pad; i++) fb_ch(' ');
        fb_str("\x1b[0m");
        fb_str(tmp);
        fb_str("\x1b[0m\r\n");
    }

    /* 视口 + 状态栏 */
    for (int r = 0; r < VIEW_H; r++) {
        g_last_fg = g_last_bg = -1;
        fb_str("\x1b[0m");
        for (int c = 0; c < VIEW_W; c++) {
            fb_fg(g_fr[r][c], g_fgc[r][c], g_fb[r][c]);
            fb_bg(g_br[r][c], g_bgc[r][c], g_bb[r][c]);
            fb_str(g_ch[r][c]);
        }
        fb_str("\x1b[0m ");
        g_last_fg = -1;
        fb_str(g_hud[r]);
        int w = disp_w(g_hud[r]);
        for (int i = w; i < HUD_W; i++) fb_ch(' ');
        fb_str("\x1b[0m\r\n");
    }

    /* 底部一行：状态 / 操作提示（补满整行，免得上一帧的残影露出来） */
    {
        const char *line = g_status[0] ? g_status : "\x1b[90m走到东西跟前，对准它，按 E。";
        fb_str("\x1b[0m ");
        fb_str(line);
        fb_str("\x1b[0m");
        /* 宽度必须按真实显示宽度算：这行中文 28 格，早先硬编码成 21，
         * 一补空格就超出 105 列，终端自动折行，整屏往上滚一格。 */
        int w = 1 + disp_w(line);
        for (int i = w; i < FRAME_W; i++) fb_ch(' ');
    }
    if (g_flen < sizeof g_frame) g_frame[g_flen] = '\0';
}

static void present(void) {
    build_frame();
    fwrite(g_frame, 1, g_flen, stdout);
    fflush(stdout);
}

/* =====================================================================
 *  ⑤ 模态面板：谜题、背包、地图、提示都在这里，用完回 3D 画面
 * ===================================================================== */

static void ui_clear(void) {
    if (g_auto) return;                      /* 自动测试时不要把日志刷掉 */
    if (g_vt) fputs("\x1b[2J\x1b[H", stdout);
    else { system("cls"); }
}
static void rule(void) { fputs("============================================================\n", stdout); }
static void thin(void) { fputs("------------------------------------------------------------\n", stdout); }

static void any_key(void) { if (!g_auto) { fflush(stdout); _getch(); } }

static void wait_key(void) {
    if (g_auto) return;
    fputs("\n  （按任意键回到房间）", stdout);
    fflush(stdout);
    _getch();
}

static int read_line(char *buf, int cap) {
    if (g_auto) {                            /* 自动测试：直接取预设的输入 */
        const char *s = (g_si < 64 && g_script[g_si]) ? g_script[g_si] : "0";
        g_si++;
        snprintf(buf, (size_t)cap, "%s", s);
        return (int)strlen(buf);
    }
    int n = 0;
    for (;;) {
        int c = _getch();
        if (c == 13) break;
        if (c == 27) { fputs("\n", stdout); return -1; }
        if ((c == 8 || c == 127) && n > 0) { n--; fputs("\b \b", stdout); fflush(stdout); continue; }
        if (n < cap - 1 && ((c >= '0' && c <= '9') || c == 'q' || c == 'Q' || c == 'x' || c == 'X')) {
            buf[n++] = (char)c;
            fputc(c, stdout);
            fflush(stdout);
        }
    }
    buf[n] = '\0';
    fputs("\n", stdout);
    return n;
}

static int ask_int(int lo, int hi) {
    char buf[16];
    for (;;) {
        printf("\n  请输入 [%d-%d]，q 退出 > ", lo, hi);
        fflush(stdout);
        int n = read_line(buf, sizeof buf);
        if (n < 0) return -1;
        if (n > 0 && (buf[0] == 'q' || buf[0] == 'Q' || buf[0] == 'x' || buf[0] == 'X')) return -1;
        int v = atoi(buf);
        if (n > 0 && v >= lo && v <= hi) return v;
        printf("  无效输入，请输入 %d 到 %d 之间的数字。\n", lo, hi);
    }
}

static int ask_code(int n, char *out) {
    char buf[16];
    for (;;) {
        printf("\n  输入 %d 位数字（q 放弃）> ", n);
        fflush(stdout);
        int len = read_line(buf, sizeof buf);
        if (len < 0) return 0;
        if (len >= 1 && (buf[0] == 'q' || buf[0] == 'Q' || buf[0] == 'x' || buf[0] == 'X')) return 0;
        int ok = (len == n);
        for (int i = 0; ok && i < len; i++) if (buf[i] < '0' || buf[i] > '9') ok = 0;
        if (!ok) { printf("  要正好 %d 位数字（只能是 0-9）。\n", n); continue; }
        strcpy(out, buf);
        return 1;
    }
}

/* =====================================================================
 *  ⑥ 时间 / 错误 / 结局
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
static void note(const char *s) {
    snprintf(g_status, sizeof g_status, " %s", s);
}

static void finish(void) {
    ui_clear();
    rule();
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
    rule();
    puts("");
    fputs("  按任意键离开。", stdout);
    fflush(stdout);
    _getch();
    con_restore();
    exit(0);
}

/* =====================================================================
 *  ⑦ 五个谜题（逻辑和文字版完全一致）
 * ===================================================================== */

static void puzzle_books(void) {
    const char *shelf[5] = { "蓝", "紫", "红", "绿", "黄" };
    const char *want[5]  = { "红", "黄", "蓝", "绿", "紫" };
    char order[5][4];
    int picked = 0;
    for (int i = 0; i < 5; i++) strcpy(order[i], shelf[i]);

    ui_clear();
    rule();
    puts("  五 色 书");
    thin();
    puts("  矮书柜顶层站着五本书。抽一本，它会滑到左边的下一个空位，");
    puts("  排满五本即判定。日记里说，苏教过祖父一套排书的法子。");

    while (picked < 5) {
        thin();
        printf("  书架：");
        for (int i = 0; i < 5; i++) printf(" %s%s ", order[i], i < picked ? "*" : " ");
        printf("\n\n");
        for (int i = 0; i < 5; i++)
            printf("   %d) %s《%s》\n", i + 1, i < picked ? "已就位" : "抽出", order[i]);
        puts("   （* 号是已经排到左边的书；输入 0 把书全部放回原处）");
        int pick = ask_int(0, 5);
        if (pick < 0) { puts("  你把手缩了回来。"); return; }
        if (pick == 0) {
            for (int i = 0; i < 5; i++) strcpy(order[i], shelf[i]);
            picked = 0;
            puts("  你把书一本本放回原位。");
            continue;
        }
        pick--;
        if (pick < picked) { puts("  这本已经排在左边了。"); continue; }
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
        note("五色书排好了，得到小铜钥匙。");
    } else {
        puts("  书页哗啦一声滑回原位——这个顺序不对。");
        puts("  再想想：“红引路，黄跟上，蓝接着，绿搭手，紫收尾”。");
        Beep(180, 260);
        note("五色书的顺序不对。");
    }
    pass_time(MIN_PER_ACT);
    wait_key();
}

static void puzzle_music(void) {
    const int melody[4] = { 5, 2, 6, 3 };
    const int freq[8] = { 262, 294, 330, 392, 440, 523, 587, 659 };

    ui_clear();
    rule();
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
            note("记下了大门密码：5263。");
            wait_key();
            return;
        }
        puts("  音筒“卡嗒”一声复位了——哪里不对。");
        Beep(180, 240);
    }
}

static void puzzle_safe(void) {
    ui_clear();
    rule();
    puts("  保 险 箱");
    thin();
    puts("  箱门旁的便签：“我把它设在我最重要的那一年。”");
    char code[8];
    if (!ask_code(4, code)) { puts("  你退后一步。"); wait_key(); return; }
    if (strcmp(code, "1986") == 0) {
        S.safeOpen = 1; S.brassKey = 1;
        puts("");
        puts("  四声轻响，箱门弹开。【获得：黄铜大门钥匙】");
        puts("  钥匙下压着一张纸条：");
        puts("  “电在屋下，我把它留给了还记得那年春天的人。”");
        Beep(660, 140); Beep(990, 200);
        note("拿到黄铜大门钥匙。");
    } else {
        mistake("保险箱密码错误");
        puts("  转盘空转了一圈，箱门纹丝不动。");
        note("保险箱密码不对。");
    }
    pass_time(MIN_PER_ACT);
    wait_key();
}

static void puzzle_crate(void) {
    ui_clear();
    rule();
    puts("  上 锁 的 木 箱");
    thin();
    puts("  四位黄铜转盘，锈得厉害。箱盖上刻着一行小字：");
    puts("  “密码还是她的生日，她总说四月是开始。”");
    char code[8];
    if (!ask_code(4, code)) { puts("  你松开手，转盘停在一半。"); wait_key(); return; }
    if (strcmp(code, "0417") == 0) {
        S.crateOpen = 1; S.watch = 1;
        puts("");
        puts("  四位转盘依次落定，“咔哒”。木箱开了。");
        puts("  毛毯底下躺着一只黄铜怀表，表链断了。");
        puts("  表针停在 凌晨 3 点 12 分——和书房那面挂钟一模一样。");
        puts("  表盖内侧刻着：“给苏。等钟再走起来，我就回家。”");
        puts("  【获得：祖父的怀表】");
        Beep(587, 140); Beep(740, 200);
        note("拿到祖父的怀表。");
    } else {
        mistake("木箱转盘密码错误");
        puts("  转盘纹丝不动。");
        note("木箱密码不对。");
    }
    pass_time(MIN_PER_ACT);
    wait_key();
}

static void puzzle_breaker(void) {
    ui_clear();
    rule();
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
        for (;;) {
            int pick = ask_int(1, 3);
            if (pick < 0) { puts("  你缩回手。"); return; }
            int used = 0;
            for (int j = 0; j < i; j++) if (seq[j] - '0' == pick) used = 1;
            if (used) { puts("  这把已经合上了。"); continue; }
            seq[i] = (char)('0' + pick);
            seq[i + 1] = '\0';
            Beep(300, 90);
            break;
        }
    }

    thin();
    if (strcmp(seq, "312") == 0) {
        S.powerOn = 1;
        puts("  Ⅲ、Ⅰ、Ⅱ 依次落下。墙里“轰”地活了过来——");
        puts("  地下室亮了，门厅大门上的指示灯也绿了。");
        Beep(392, 120); Beep(523, 120); Beep(659, 240);
        note("电通了，大门第二道锁松开了。");
    } else {
        mistake("闸刀顺序错误，配电箱跳闸了");
        puts("  闸刀炸开一团蓝白色的火花，三把闸刀全部弹回原位。");
        S.fuseIn = 1;
        note("闸刀炸了，顺序不对。");
    }
    pass_time(MIN_PER_ACT);
    wait_key();
}

/* =====================================================================
 *  ⑧ 家具的动作（就是网页版 addHit 里那个 act）
 * ===================================================================== */

static void act_shoe(void) {
    ui_clear(); rule();
    puts("  鞋 柜");
    thin();
    /* 注意判 S.torch：电池被装进手电筒后 S.battery 会清零，
     * 只看 S.battery 的话，再开一次鞋柜又能白拿一节电池。 */
    if (S.battery || S.torch) puts("  柜子里只剩下旧鞋。鞋尖全都朝里，像随时准备离开。");
    else {
        S.battery = 1;
        puts("  你拉开柜门。上层塞着一节电池，标签早已褪色。");
        puts("  【获得：一号电池】——沉得不像一节旧电池。");
        puts("  也许该把它装进手电筒里。");
        note("拿到一号电池。");
    }
    pass_time(MIN_PER_ACT);
    wait_key();
}

static void act_stand(void) {
    ui_clear(); rule();
    puts("  祖 父 的 座 钟");
    thin();
    if (S.gear) {
        puts("  钟摆慢悠悠地晃，像在数一种不同的时间。");
    } else if (S.clockOpen) {
        S.gear = 1;
        puts("  你从背板的暗格里捏出一枚黄铜齿轮。");
        puts("  【获得：黄铜齿轮】——大小正好卡进八音盒的发条槽。");
        note("拿到黄铜齿轮。");
    } else {
        puts("  钟摆慢悠悠地晃，摆锤下面刻着一行小字：");
        puts("  “时间不是我量出来的，是苏。”");
        puts("  背板用一片小铜锁锁着——锁孔细得像给怀表上弦用的钥匙。");
        if (!S.smallKey) puts("  你手上没有这样的钥匙。");
        else {
            S.clockOpen = 1;
            puts("  小铜钥匙在锁孔里转了半圈，背板“啪”地弹开一条缝——");
            puts("  里面躺着一枚黄铜齿轮。");
            note("座钟背板开了，里面有东西。");
        }
    }
    pass_time(MIN_PER_ACT);
    wait_key();
}

static void act_music(void) {
    if (S.musicSolved) {
        ui_clear(); rule();
        puts("  八 音 盒");
        thin();
        puts("  八音盒安安静静，像是唱累了。");
        pass_time(MIN_PER_ACT);
        wait_key();
        return;
    }
    if (!S.gear) {
        ui_clear(); rule();
        puts("  八 音 盒");
        thin();
        puts("  你掀开弧形木盖——发条槽里空空的，少一枚齿轮。");
        pass_time(MIN_PER_ACT);
        wait_key();
        return;
    }
    puzzle_music();
}

static void act_mirror(void) {
    ui_clear(); rule();
    puts("  穿 衣 镜");
    thin();
    puts("  你抬手擦了擦灰。镜子里的你，比真实的你慢了半拍才放下手。");
    pass_time(MIN_PER_ACT);
    wait_key();
}

static void act_gate(void) {
    ui_clear(); rule();
    puts("  大 门 · 三 道 锁");
    thin();
    printf("  第一道（钥匙孔）：%s\n", S.brassKey ? "★ 黄铜钥匙" : "空的");
    printf("  第二道（电    锁）：%s\n", S.powerOn ? "★ 已通电" : "没电");
    printf("  第三道（数字锁）：%s\n", S.keypadSolved ? "★ 已解开" : "四位数字");
    thin();

    if (!S.brassKey) {
        puts("  第一道锁是长条形的钥匙孔。你手上没有能插进去的东西。");
        pass_time(MIN_PER_ACT); wait_key(); return;
    }
    if (!S.powerOn) {
        puts("  黄铜钥匙转了一半卡住了——第二道锁要等电闸通电才会松。");
        pass_time(MIN_PER_ACT); wait_key(); return;
    }
    if (!S.keypadSolved) {
        puts("  数字键盘亮起来，屏幕泛着绿光，等待四位数字。");
        puts("  键盘上方刻着一句：“把曲子唱给大门听。”");
        char code[8];
        if (!ask_code(4, code)) { puts("  你收回手。"); pass_time(MIN_PER_ACT); wait_key(); return; }
        if (strcmp(code, "5263") == 0) {
            S.keypadSolved = 1;
            puts("  第三道锁“嗒”地弹开。三声轻响连成一线——大门松动了。");
            Beep(523, 120); Beep(659, 120); Beep(784, 120); Beep(1046, 260);
            puts("");
            puts("  （按任意键，把门推开）");
            any_key();
        } else {
            mistake("大门数字锁密码错误");
            pass_time(MIN_PER_ACT); wait_key(); return;
        }
    }
    S.ending = 1;
    pass_time(0);
}

static void act_down(void) {
    if (!S.torch) {
        ui_clear(); rule();
        puts("  往 地 下 的 石 阶");
        thin();
        puts("  石阶尽头是纯粹的黑暗，黑暗里有东西在呼吸。");
        puts("  你还缺一盏能亮的灯。");
        pass_time(MIN_PER_ACT);
        wait_key();
        return;
    }
    S.floorNo = 1;
    P.x = 3.2; P.y = 8.4; P.ang = -PI / 2;
    note("你走进地下室。空气又冷又干。");
    pass_time(MIN_PER_ACT);
}

static void act_up(void) {
    S.floorNo = 0;
    P.x = 23.0; P.y = 12.4; P.ang = PI / 2;
    note("你回到门厅。");
    pass_time(MIN_PER_ACT);
}

static void act_desk(void) {
    ui_clear(); rule();
    puts("  写 字 台");
    thin();
    if (S.flash || S.torch) puts("  抽屉里只剩下旧票据和干掉的胶水瓶。");
    else {
        S.flash = 1;
        puts("  你拉开抽屉，摸到一只金属圆筒——");
        puts("  【获得：手电筒（空）】按下开关只有弹簧的响声。");
        if (S.battery) puts("  背包里已经有电池了：按 I 打开背包，可以把两件东西组合起来。");
        note("拿到手电筒，还缺一节电池。");
    }
    pass_time(MIN_PER_ACT);
    wait_key();
}

static void act_shelf(void) {
    ui_clear(); rule();
    if (S.diary) {
        puts("  书 架");
        thin();
        puts("  那本夹着纸的书已经空了。");
    } else {
        S.diary = 1;
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
        puts("  【记下了：保险箱密码与 1986 有关；五色书 红黄蓝绿紫】");
        note("记下了：保险箱 1986，五色书 红黄蓝绿紫。");
    }
    pass_time(MIN_PER_ACT);
    wait_key();
}

static void act_wall_clock(void) {
    ui_clear(); rule();
    puts("  挂 钟");
    thin();
    puts("  钟摆停在半空，指针停在 ★ 3 点 12 分 ★。");
    puts("  十年了，没有人给这面钟上过弦。");
    puts("  它停在这里，像谁特意留下的几个数字。");
    pass_time(MIN_PER_ACT);
    wait_key();
}

static void act_low_shelf(void) {
    if (S.booksSolved) {
        ui_clear(); rule();
        puts("  矮 书 柜");
        thin();
        puts("  暗格空了，只留一点淡淡的皂角香。");
        pass_time(MIN_PER_ACT);
        wait_key();
        return;
    }
    puzzle_books();
}

static void act_safe(void) {
    if (S.safeOpen) {
        ui_clear(); rule();
        puts("  保 险 箱");
        thin();
        puts("  保险箱已经开了，里面只剩一块旧绒布。");
        pass_time(MIN_PER_ACT);
        wait_key();
        return;
    }
    puzzle_safe();
}

static void act_cabinet(void) {
    ui_clear(); rule();
    puts("  储 物 柜");
    thin();
    if (!S.cabOpen) {
        if (!S.rustyKey) { puts("  柜门锁着。"); pass_time(MIN_PER_ACT); wait_key(); return; }
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
        note("记下了：木箱密码 0417。");
    } else if (!S.fuse && !S.fuseIn) {
        /* 装进配电箱后 S.fuse 会清零、S.fuseIn 置 1；
         * 少判 fuseIn 的话，柜子会无限供应保险丝。 */
        S.fuse = 1;
        puts("  【获得：陶瓷保险丝】——角落里亮着红色指示灯的配电箱正缺这个。");
        note("拿到陶瓷保险丝。");
    } else {
        puts("  柜子里空了，只有一层铁锈末和樟脑的气味。");
    }
    pass_time(MIN_PER_ACT);
    wait_key();
}

static void act_crate(void) {
    if (S.crateOpen) {
        ui_clear(); rule();
        puts("  木 箱");
        thin();
        puts("  木箱里铺着旧毛毯，空了一半——另一半被谁先带走了。");
        pass_time(MIN_PER_ACT);
        wait_key();
        return;
    }
    puzzle_crate();
}

static void act_breaker(void) {
    if (S.powerOn) {
        ui_clear(); rule();
        puts("  配 电 箱");
        thin();
        puts("  三把闸刀都合着，电流在墙里嗡嗡地走。");
        pass_time(MIN_PER_ACT);
        wait_key();
        return;
    }
    if (!S.fuseIn) {
        ui_clear(); rule();
        puts("  配 电 箱");
        thin();
        if (!S.fuse) {
            puts("  保险丝槽是空的。这时候合闸，只会跳起一团火花。");
            note("配电箱缺一枚保险丝。");
        } else {
            S.fuseIn = 1; S.fuse = 0;
            puts("  保险丝“咔”地嵌进卡槽，小灯转为安稳的黄光。");
            puts("  可以拉闸了——顺序藏在挂钟上。");
        }
        pass_time(MIN_PER_ACT);
        wait_key();
        return;
    }
    puzzle_breaker();
}

/* —— 背包 —— */
static void act_bag(void) {
    ui_clear(); rule();
    puts("  背 包");
    thin();
    struct { int has; const char *icon; const char *name; const char *desc; } items[] = {
        { S.rustyKey, "[钥]", "锈铜钥匙",     "信里夹着的东西，铜锈绿得像发霉的月亮。" },
        { S.battery,  "[电]", "一号电池",     "沉甸甸的干电池，标签早已褪色。" },
        { S.flash,    "[筒]", "手电筒（空）", "按下开关只有弹簧的响声。" },
        { S.torch,    "[光]", "手电筒（亮）", "光柱又稳又亮，能刺开地窖里的黑。" },
        { S.diary,    "[纸]", "日记残页",     "一九八六年春天……保险箱 1986；五色书 红黄蓝绿紫。" },
        { S.smallKey, "[钥]", "小铜钥匙",     "齿痕细密，像配给某只小锁的。" },
        { S.gear,     "[齿]", "黄铜齿轮",     "大小正好卡进八音盒的发条槽。" },
        { S.fuse,     "[丝]", "陶瓷保险丝",   "老式陶瓷保险丝，两头是黄铜帽。" },
        { S.brassKey, "[钥]", "黄铜大门钥匙", "齿口薄得像刀刃，配大门第一道锁。" },
        { S.watch,    "[表]", "祖父的怀表",   "表针停在凌晨 3 点 12 分，表盖内侧刻着“给苏”。" },
    };
    int n = (int)(sizeof items / sizeof items[0]), shown = 0;
    for (int i = 0; i < n; i++) {
        if (!items[i].has) continue;
        shown++;
        printf("  %s %-14s %s\n", items[i].icon, items[i].name, items[i].desc);
    }
    if (!shown) puts("  （空的）");

    if (S.flash && S.battery && !S.torch) {
        thin();
        puts("  你可以把电池装进手电筒。");
        printf("  1) 组合  2) 合上背包\n");
        if (ask_int(1, 2) == 1) {
            S.flash = 0; S.battery = 0; S.torch = 1;
            puts("  你把电池塞进手电筒，拧紧尾盖。黑暗里亮起一小团光。");
            puts("  【手电筒亮了】门厅东南角有一段往地下的石阶，该下去了。");
            Beep(700, 140);
            note("手电筒亮了，可以去地下室了。");
        }
    }
    pass_time(MIN_PER_ACT);
    wait_key();
}

/* —— 地图 —— */
static void act_map(void) {
    int w, h;
    const char *const *m = cur_map(&w, &h);
    ui_clear(); rule();
    printf("  地 图 · %s\n", floor_name());
    thin();
    for (int y = 0; y < h; y++) {
        fputs("   ", stdout);
        for (int x = 0; x < w; x++) {
            int px = (int)P.x, py = (int)P.y;
            char c = m[y][x];
            if (x == px && y == py) {
                double a = P.ang;
                fputc(fabs(a + PI / 2) < PI / 4 ? '^' : fabs(a - PI / 2) < PI / 4 ? 'v' :
                      fabs(a) < PI / 4 ? '>' : '<', stdout);
            } else if (c == '.') fputc('.', stdout);
            else fputc('#', stdout);
        }
        fputc('\n', stdout);
    }
    thin();
    puts("  ^v<> 是你，`#` 是墙。家具就是画面里那些字母柱子。");
    wait_key();
}

/* —— 提示 —— */
static void act_hint(void) {
    int topic;
    if (!S.torch)                  topic = 0;
    else if (!S.safeOpen)          topic = 1;
    else if (!S.powerOn)           topic = 2;
    else if (!S.crateOpen)         topic = 3;
    else                           topic = 4;

    static const char *hint1[5] = {
        "先找光：书房有写字台，门厅的鞋柜也不像只装旧鞋。拿到电池后按 I 组合。",
        "便签说“最重要的那一年”。书房书架里夹着一张日记残页，去看看。",
        "配电箱缺一枚保险丝。地下室储物柜上挂着铜锁——那把锈铜钥匙正好配它。",
        "地下室角落的木箱换过一把四位转盘锁。储物柜里那本账本记着一个日期。",
        "大门数字锁的线索在八音盒里：五色书 → 小铜钥匙 → 座钟背板 → 齿轮 → 八音盒。",
    };
    static const char *hint2[5] = {
        "手电筒在书房写字台抽屉里，电池在门厅鞋柜。按 I，选 1 组合。",
        "日记写着“一九八六年春天，我们搬进这栋房子”，密码就是 1986。",
        "保险丝装进配电箱；闸刀顺序藏在书房挂钟上：3 点 12 分 = 3、1、2。",
        "账本写“四月十七日”——密码是 0417。",
        "五色书按 红黄蓝绿紫；八音盒复奏 5-2-6-3，大门密码就是 5263。",
    };
    int lv = S.hintUsed[topic] > 0 ? 1 : 0;
    S.hintUsed[topic]++;

    ui_clear(); rule();
    puts("  提 示");
    thin();
    puts(lv == 0 ? hint1[topic] : hint2[topic]);
    thin();
    printf("  （问提示花掉 %d 分钟，还剩 %d 分钟）\n", MIN_PER_HINT, TIME_LIMIT - S.minutes);
    pass_time(MIN_PER_HINT);
    wait_key();
}

static void act_help(void) {
    ui_clear(); rule();
    puts("  怎 么 玩");
    thin();
    puts("  · W S 前后走，A D 左右平移，← → 转身。");
    puts("  · 走到家具跟前，让它落在画面中间，右下角会显示“对准：xxx”，");
    puts("    这时按 E 就能互动。");
    puts("  · 每次互动花 1 分钟，问提示花 2 分钟。");
    puts("  · 到 60 分钟（午夜）房子就收走你；错满 5 次也会结束。");
    puts("  · I 背包（电池 + 手电筒在那里组合），M 地图，H 提示，Q 退出。");
    thin();
    puts("  目标：凑齐三把锁 —— 黄铜钥匙 / 通电 / 大门数字锁。");
    wait_key();
}

/* =====================================================================
 *  ⑨ 输入与主循环
 * ===================================================================== */

static int read_key(void) {
    int c = _getch();
    if (c == 0 || c == 224) {
        int c2 = _getch();
        switch (c2) {
        case 72: return K_UP;
        case 80: return K_DOWN;
        case 75: return K_LEFT;
        case 77: return K_RIGHT;
        default: return 0;
        }
    }
    if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
    return c;
}

static int blocked(double x, double y) {
    if (cell_at((int)floor(x), (int)floor(y)) != '.') return 1;
    return 0;
}

/* 分轴尝试：撞墙时还能沿着墙滑过去，而不是整条腿被卡住 */
static void move_by(double fwd, double side) {
    double dx = cos(P.ang), dy = sin(P.ang);
    double nx = P.x + dx * fwd - dy * side;
    double ny = P.y + dy * fwd + dx * side;
    const double r = 0.26;
    if (!blocked(nx - r, ny - r) && !blocked(nx + r, ny - r) &&
        !blocked(nx - r, ny + r) && !blocked(nx + r, ny + r)) {
        P.x = nx; P.y = ny;
        return;
    }
    if (!blocked(nx - r, P.y - r) && !blocked(nx + r, P.y - r) &&
        !blocked(nx - r, P.y + r) && !blocked(nx + r, P.y + r)) { P.x = nx; return; }
    if (!blocked(P.x - r, ny - r) && !blocked(P.x + r, ny - r) &&
        !blocked(P.x - r, ny + r) && !blocked(P.x + r, ny + r)) { P.y = ny; return; }
}

static void handle_key(int k) {
    switch (k) {
    case 'w': case K_UP:    move_by(0.26, 0); break;
    case 's': case K_DOWN:  move_by(-0.26, 0); break;
    case 'a':               move_by(0, -0.24); break;
    case 'd':               move_by(0, 0.24); break;
    case K_LEFT:            P.ang -= 0.11; break;
    case K_RIGHT:           P.ang += 0.11; break;
    case 'e': {
        const Sprite *s = aimed();
        if (!s) { if (!g_status[0]) note("这里没有可以碰的东西。"); }
        else { g_status[0] = '\0'; s->act(); }
        break;
    }
    case 'i': g_status[0] = '\0'; act_bag(); break;
    case 'm': g_status[0] = '\0'; act_map(); break;
    case 'h': g_status[0] = '\0'; act_hint(); break;
    case 'k': g_status[0] = '\0'; act_help(); break;
    case 'q': case 27: {
        ui_clear();
        rule();
        puts("  离开房间？（这一局不会存档）");
        thin();
        puts("  1) 继续玩    2) 退出");
        if (ask_int(1, 2) == 2) {
            S.ending = 0;
            con_restore();
            exit(0);
        }
        break;
    }
    default: break;
    }
}

static void intro(void) {
    ui_clear();
    rule();
    puts("                          回  声  宅");
    puts("                      ECHO MANOR  ·  C 版");
    rule();
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
    rule();
    puts("");
    fputs("  W A S D 走动   ← → 转身   E 互动   I 背包   M 地图   H 提示", stdout);
    puts("");
    fputs("  按任意键进门。", stdout);
    fflush(stdout);
    _getch();
}

/* =====================================================================
 *  ⑩ 控制台设置 & 调试用 --dump
 * ===================================================================== */

static void con_setup(void) {
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
    SetConsoleTitleW(L"回声宅 · C 语言版");

    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(h, &mode)) {
        if (SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT))
            g_vt = 1;
    }

    /* 把窗口撑到刚好够大；撑不动就算了，不报错 */
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(h, &info)) {
        SHORT w = (SHORT)(FRAME_W + 2), hh = (SHORT)(FRAME_H + 3);
        if (info.dwSize.X < w || info.dwSize.Y < hh) {
            COORD size = { w, hh };
            SetConsoleScreenBufferSize(h, size);
        }
        SMALL_RECT rc = { 0, 0, (SHORT)(w - 1), (SHORT)(hh - 1) };
        SetConsoleWindowInfo(h, TRUE, &rc);
    }
    if (g_vt) fputs("\x1b[?25l", stdout);
}

static void con_restore(void) {
    if (g_vt) fputs("\x1b[0m\x1b[?25h\n", stdout);
    fflush(stdout);
}

/* 测试用：不接键盘，直接把几个固定机位画出来，写进文件。
 * color = 0 去掉所有颜色（方便肉眼对齐），color = 1 保留 UTF-8 + ANSI 真彩色。 */
static void dump_views(int color, const char *path) {
    g_color = color;
    g_vt = 0;
    struct { int fl; double x, y, ang; int torch; const char *name; } cases[] = {
        { 0,  5.5, 11.0, -PI / 2, 0, "门厅-对准书房门洞" },
        { 0, 13.0, 12.2, -PI / 2, 0, "门厅-朝北看书房门" },
        { 0, 13.0, 12.2,  PI / 2, 0, "门厅-朝南看大门"  },
        { 0, 21.6, 12.8,  0.0,    0, "门厅-朝东看石阶"  },
        { 0,  6.5,  3.2,  2.667,  0, "书房-看矮书柜"    },
        { 0,  5.5,  4.6, -2.56,   0, "书房-看书架"      },
        { 1,  3.2,  8.4, -PI / 2, 1, "地下室-朝北"      },
        { 1, 14.6,  5.4, -PI / 2, 1, "地下室-看木箱"    },
        { 1, 15.2,  5.6,  0.0,    1, "地下室-看配电箱"  },
    };
    int n = (int)(sizeof cases / sizeof cases[0]);
    FILE *f = fopen(path, "wb");
    if (!f) { printf("%s 打不开\n", path); return; }
    memset(&S, 0, sizeof S);
    S.torch = 1; S.rustyKey = 1;
    for (int i = 0; i < n; i++) {
        S.floorNo = cases[i].fl;
        P.x = cases[i].x; P.y = cases[i].y; P.ang = cases[i].ang;
        snprintf(g_status, sizeof g_status, " %s", cases[i].name);
        build_frame();
        /* 自检：每一行的显示宽度都必须正好是 FRAME_W */
        {
            const char *q = g_frame;
            int row = 0;
            while (*q) {
                const char *e = q;
                while (*e && *e != '\r' && *e != '\n') e++;
                char line[8192];
                int len = (int)(e - q);
                if (len > 8191) len = 8191;
                memcpy(line, q, (size_t)len);
                line[len] = '\0';
                int w = disp_w(line);
                if (w != FRAME_W) printf("  [warn] 第 %d 个机位 第 %d 行 宽度 %d\n", i, row, w);
                row++;
                while (*e == '\r' || *e == '\n') e++;
                q = e;
            }
        }
        fprintf(f, "----- %s -----\n", cases[i].name);
        fwrite(g_frame, 1, g_flen, f);
        fprintf(f, "\n");
    }
    fclose(f);
    printf("%s written (%d views)\n", path, n);

    /* 再单独画一帧"底部提示为空"的画面：这时走的是默认提示那句长中文，
     * 宽度最容易算错。算错了终端就会自动折行，把整屏往上顶一格。 */
    memset(&S, 0, sizeof S);
    S.floorNo = 0; P.x = 13.0; P.y = 12.2; P.ang = -PI / 2;
    g_status[0] = '\0';
    build_frame();
    {
        const char *q = g_frame;
        int row = 0, bad = 0;
        while (*q) {
            const char *e = q;
            while (*e && *e != '\r' && *e != '\n') e++;
            char line[8192];
            int len = (int)(e - q);
            if (len > 8191) len = 8191;
            memcpy(line, q, (size_t)len);
            line[len] = '\0';
            if (disp_w(line) != FRAME_W) {
                printf("  [warn] 空提示帧 第 %d 行 宽度 %d（应为 %d）\n", row, disp_w(line), FRAME_W);
                bad++;
            }
            row++;
            while (*e == '\r' || *e == '\n') e++;
            q = e;
        }
        if (!bad) printf("  空提示帧 %d 行，宽度全部 = %d\n", row, FRAME_W);
    }
}

/* 自动通关自检：不接键盘，按"正确解法"依次调用每个动作，检查状态。
 * 用途和文字版的脚本测试一样——改完代码先跑一遍，别靠手点。 */
static int g_fail;
#define CHECK(cond, msg) do { \
    if (cond) printf("  [ok]   %s\n", msg); \
    else { printf("  [FAIL] %s\n", msg); g_fail++; } \
} while (0)

static void set_input(const char *a, const char *b, const char *c, const char *d) {
    g_script[0] = a; g_script[1] = b; g_script[2] = c; g_script[3] = d;
    g_si = 0;
}

static void autotest(void) {
    g_auto = 1; g_vt = 0; g_color = 0;
    memset(&S, 0, sizeof S);
    S.rustyKey = 1;
    P.x = 13.0; P.y = 12.2; P.ang = -PI / 2;

    /* —— 找光 —— */
    S.floorNo = 0; P.x = 6.0; P.y = 4.6;
    act_desk();
    CHECK(S.flash == 1, "书房写字台 → 手电筒");
    P.x = 3.2; P.y = 10.0;
    act_shoe();
    CHECK(S.battery == 1, "门厅鞋柜 → 电池");
    set_input("1", NULL, NULL, NULL);
    act_bag();
    CHECK(S.torch == 1 && S.flash == 0 && S.battery == 0, "背包组合 → 手电筒亮了");

    /* —— 下地下室 —— */
    P.x = 24.0; P.y = 12.8;
    act_down();
    CHECK(S.floorNo == 1, "石阶 → 进入地下室");

    P.x = 3.0; P.y = 3.0;
    act_cabinet();
    act_cabinet();
    CHECK(S.cabOpen == 1 && S.fuse == 1, "储物柜 → 保险丝 + 账本(0417)");

    P.x = 16.4; P.y = 5.6;
    act_breaker();
    CHECK(S.fuseIn == 1, "配电箱 → 装上保险丝");
    set_input("3", "1", "2", NULL);
    act_breaker();
    CHECK(S.powerOn == 1, "闸刀 3-1-2 → 通电");

    P.x = 14.6; P.y = 4.0;
    set_input("0417", NULL, NULL, NULL);
    act_crate();
    CHECK(S.crateOpen == 1 && S.watch == 1, "木箱 0417 → 怀表");

    act_up();
    CHECK(S.floorNo == 0, "上楼 → 回到一楼");

    /* —— 书房 —— */
    P.x = 3.0; P.y = 3.0;
    act_shelf();
    CHECK(S.diary == 1, "书架 → 日记残页");
    g_script[0] = "3"; g_script[1] = "5"; g_script[2] = "3";
    g_script[3] = "5"; g_script[4] = "5"; g_si = 0;
    act_low_shelf();
    CHECK(S.booksSolved == 1 && S.smallKey == 1, "五色书 → 小铜钥匙");

    P.x = 3.4; P.y = 12.0;
    act_stand();
    act_stand();
    CHECK(S.gear == 1, "座钟 → 黄铜齿轮");

    P.x = 6.0; P.y = 12.4;
    set_input("5263", NULL, NULL, NULL);
    act_music();
    CHECK(S.musicSolved == 1, "八音盒复奏 5-2-6-3 → 大门密码");

    P.x = 10.8; P.y = 4.8;
    set_input("1986", NULL, NULL, NULL);
    act_safe();
    CHECK(S.brassKey == 1, "保险箱 1986 → 黄铜大门钥匙");

    /* —— 大门 —— */
    P.x = 13.5; P.y = 13.4;
    set_input("5263", NULL, NULL, NULL);
    act_gate();
    CHECK(S.ending == 1, "三道锁全开 → 结局「走出去的人」");
    CHECK(S.mistakes == 0, "全程 0 错误");

    printf("\n  %s：失败 %d 项 / 用时 %d 分钟\n",
           g_fail ? "== 自检未通过 ==" : "== 自检全部通过 ==", g_fail, S.minutes);
}

/* =====================================================================
 *  走位自检（--walktest）
 *  --autotest 是"直接调 act_*"，把走路、转身、对准、墙全都跳过去了。
 *  这里改成真的按键：
 *      BFS 在字符地图上找路 → ← → 转身 → W 走 → 按 E 互动
 *  于是"门洞够不够宽""家具能不能走到""隔墙能不能互动"才测得到。
 * ===================================================================== */
static int g_walkfail;

static void wck(int ok, const char *msg) {
    if (ok) printf("  [ok]   %s\n", msg);
    else { printf("  [FAIL] %s\n", msg); g_walkfail++; }
}

static int cell_walkable(int x, int y) { return cell_at(x, y) == '.'; }

/* 从 (sx,sy) 到 (tx,ty) 的最短格路（只走 '.'），写到 ox/oy，返回格数 */
static int bfs_path(int sx, int sy, int tx, int ty, int *ox, int *oy, int cap) {
    int w, h;
    cur_map(&w, &h);
    if (sx < 0 || sy < 0 || tx < 0 || ty < 0 || sx >= w || tx >= w || sy >= h || ty >= h) return 0;
    if (!cell_walkable(sx, sy) || !cell_walkable(tx, ty)) return 0;

    static int prev[64 * 64];
    static int queue[64 * 64];
    for (int i = 0; i < 64 * 64; i++) prev[i] = -2;

    int s = sy * 64 + sx, t = ty * 64 + tx;
    int head = 0, tail = 0;
    queue[tail++] = s; prev[s] = -1;
    const int dx4[4] = { 1, -1, 0, 0 }, dy4[4] = { 0, 0, 1, -1 };
    while (head < tail) {
        int cur = queue[head++];
        if (cur == t) break;
        int cx = cur % 64, cy = cur / 64;
        for (int k = 0; k < 4; k++) {
            int nx = cx + dx4[k], ny = cy + dy4[k];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            if (!cell_walkable(nx, ny)) continue;
            int ni = ny * 64 + nx;
            if (prev[ni] != -2) continue;
            prev[ni] = cur;
            queue[tail++] = ni;
        }
    }
    if (prev[t] == -2) return 0;

    int n = 0, cur = t;
    while (cur >= 0 && n < cap) { ox[n] = cur % 64; oy[n] = cur / 64; n++; cur = prev[cur]; }
    for (int i = 0; i < n / 2; i++) {
        int tmp = ox[i]; ox[i] = ox[n - 1 - i]; ox[n - 1 - i] = tmp;
        tmp = oy[i]; oy[i] = oy[n - 1 - i]; oy[n - 1 - i] = tmp;
    }
    return n;
}

/* 用 ← → 把视线转到 want 方向：每按一次转 0.11 弧度 */
static void face_angle(double want) {
    for (int i = 0; i < 120; i++) {
        double d = want - P.ang;
        while (d > PI) d -= 2 * PI;
        while (d < -PI) d += 2 * PI;
        if (fabs(d) < 0.05) return;
        handle_key(d > 0 ? K_RIGHT : K_LEFT);
    }
}

/* 一直朝 (tx,ty) 按 W，直到站稳在 0.2 米内 */
static int walk_to(double tx, double ty) {
    int stalls = 0;
    for (int i = 0; i < 600; i++) {
        double dx = tx - P.x, dy = ty - P.y;
        if (sqrt(dx * dx + dy * dy) < 0.20) return 1;
        face_angle(atan2(dy, dx));
        double bx = P.x, by = P.y;
        handle_key('w');
        if (fabs(P.x - bx) < 1e-4 && fabs(P.y - by) < 1e-4) {
            if (++stalls > 2) return 0;
            handle_key('d');                 /* 贴墙蹭一下再走，像真人那样 */
            handle_key('w');
            handle_key('a');
        } else stalls = 0;
    }
    return 0;
}

/* 沿 BFS 路线走到某件家具跟前 */
static int walk_to_sprite(const Sprite *s) {
    int cx = (int)floor(P.x), cy = (int)floor(P.y);
    int tx = (int)floor(s->x), ty = (int)floor(s->y);
    int ox[600], oy[600];
    int n = bfs_path(cx, cy, tx, ty, ox, oy, 600);
    if (n <= 0) { printf("  [warn] 地图上走不到「%s」\n", s->name); return 0; }
    for (int i = 1; i < n; i++) {
        if (!walk_to(ox[i] + 0.5, oy[i] + 0.5)) {
            printf("  [warn] 在格 (%d,%d) 卡住，到不了「%s」\n", ox[i], oy[i], s->name);
            return 0;
        }
    }
    return 1;
}

/* 对准 + 按 E */
static int interact(const Sprite *s, const char *what) {
    face_angle(atan2(s->y - P.y, s->x - P.x));
    const Sprite *a = aimed();
    if (!a) { printf("  [FAIL] 站到「%s」跟前却对不准\n", what); g_walkfail++; return 0; }
    if (strcmp(a->name, s->name) != 0)
        printf("  [warn] 对准的其实是「%s」（想找「%s」）\n", a->name, what);
    handle_key('e');
    return 1;
}

/* 全图扫描：
 *   geom —— 几何上"视线被墙挡住、但距离和角度都够"的机位有多少（修复前会误判）
 *   bad  —— aimed() 实际放行了多少个被挡住的（必须为 0）
 * 两层地图 × 每个可走格心 × 120 个朝向。 */
static int scan_aim(int *geom, int *samples) {
    int bad = 0, g = 0, ns = 0;
    for (int fl = 0; fl <= 1; fl++) {
        S.floorNo = fl;
        int n, w, h;
        const Sprite *list = cur_spr(&n);
        cur_map(&w, &h);
        for (int cy = 0; cy < h; cy++)
            for (int cx = 0; cx < w; cx++) {
                if (!cell_walkable(cx, cy)) continue;
                P.x = cx + 0.5; P.y = cy + 0.5;
                for (int a = 0; a < 120; a++) {
                    P.ang = a * (2 * PI / 120);
                    ns++;
                    const Sprite *hit = aimed();
                    if (hit && !los_clear(P.x, P.y, hit->x, hit->y)) bad++;
                    for (int i = 0; i < n; i++) {
                        double ax = list[i].x - P.x, ay = list[i].y - P.y;
                        double d = sqrt(ax * ax + ay * ay);
                        if (d > 2.8 || d < 0.05) continue;
                        double rel = atan2(ay, ax) - P.ang;
                        while (rel > PI) rel -= 2 * PI;
                        while (rel < -PI) rel += 2 * PI;
                        if (fabs(rel) > 0.55) continue;
                        if (!los_clear(P.x, P.y, list[i].x, list[i].y)) g++;
                    }
                }
            }
    }
    *geom = g;
    *samples = ns;
    return bad;
}

static void walktest(void) {
    g_auto = 1; g_vt = 0; g_color = 0;
    memset(&S, 0, sizeof S);
    S.rustyKey = 1;
    S.floorNo = 0;
    P.x = 13.0; P.y = 12.2; P.ang = -PI / 2;

    puts("== 走位通关自检：真的用 W / ← → / E ==");
    wck(cell_walkable((int)floor(P.x), (int)floor(P.y)), "出生点站在可走地面上");

    /* 1. 书房写字台：必须穿过门厅与书房之间那道 1 米宽的门洞 */
    wck(walk_to_sprite(&SPR0[7]), "从门厅穿过门洞走到书房写字台");
    interact(&SPR0[7], "写字台");
    wck(S.flash == 1, "按 E → 拿到手电筒");

    /* 2. 门厅鞋柜 */
    wck(walk_to_sprite(&SPR0[0]), "走回门厅鞋柜");
    interact(&SPR0[0], "鞋柜");
    wck(S.battery == 1, "按 E → 拿到电池");

    /* 3. 背包组合 */
    set_input("1", NULL, NULL, NULL);
    handle_key('i');
    wck(S.torch == 1 && S.flash == 0 && S.battery == 0, "按 I → 电池装进手电筒");

    /* 4. 再开一次鞋柜：电池不能复活 */
    wck(walk_to_sprite(&SPR0[0]), "再走到鞋柜");
    interact(&SPR0[0], "鞋柜（第二次）");
    wck(S.battery == 0, "重复开鞋柜不会再给电池");

    /* 5. 下地下室 */
    wck(walk_to_sprite(&SPR0[5]), "走到往地下的石阶");
    interact(&SPR0[5], "石阶");
    wck(S.floorNo == 1, "按 E → 进入地下室");
    wck(cell_walkable((int)floor(P.x), (int)floor(P.y)), "落点没有卡进墙里");

    /* 6. 储物柜 → 保险丝 */
    wck(walk_to_sprite(&SPR1[0]), "走到储物柜");
    interact(&SPR1[0], "储物柜");
    interact(&SPR1[0], "储物柜（取保险丝）");
    wck(S.fuse == 1, "拿到保险丝");

    /* 7. 装保险丝 + 拉闸 3-1-2 */
    wck(walk_to_sprite(&SPR1[2]), "走到配电箱");
    interact(&SPR1[2], "配电箱");
    wck(S.fuseIn == 1, "保险丝装进配电箱");
    set_input("3", "1", "2", NULL);
    interact(&SPR1[2], "配电箱（拉闸）");
    wck(S.powerOn == 1, "闸刀 3-1-2 → 通电");

    /* 8. 保险丝也不能复活 */
    wck(walk_to_sprite(&SPR1[0]), "回到储物柜");
    interact(&SPR1[0], "储物柜（第三次）");
    wck(S.fuse == 0, "重复开储物柜不会再给保险丝");

    /* 9. 木箱 0417 */
    wck(walk_to_sprite(&SPR1[1]), "走到木箱");
    set_input("0417", NULL, NULL, NULL);
    interact(&SPR1[1], "木箱");
    wck(S.crateOpen == 1 && S.watch == 1, "木箱 0417 → 怀表");

    /* 10. 上楼 */
    wck(walk_to_sprite(&SPR1[3]), "走回上楼石阶");
    interact(&SPR1[3], "上楼石阶");
    wck(S.floorNo == 0, "按 E → 回到门厅");

    /* 11. 书架 */
    wck(walk_to_sprite(&SPR0[6]), "走到书架");
    interact(&SPR0[6], "书架");
    wck(S.diary == 1, "拿到日记残页");

    /* 12. 五色书 3-5-3-5-5 */
    wck(walk_to_sprite(&SPR0[9]), "走到矮书柜");
    g_script[0] = "3"; g_script[1] = "5"; g_script[2] = "3";
    g_script[3] = "5"; g_script[4] = "5"; g_si = 0;
    interact(&SPR0[9], "矮书柜");
    wck(S.booksSolved == 1 && S.smallKey == 1, "五色书 → 小铜钥匙");

    /* 13. 座钟 → 齿轮 */
    wck(walk_to_sprite(&SPR0[1]), "走到座钟");
    interact(&SPR0[1], "座钟");
    interact(&SPR0[1], "座钟（开背板）");
    wck(S.gear == 1, "座钟 → 黄铜齿轮");

    /* 14. 八音盒 */
    wck(walk_to_sprite(&SPR0[2]), "走到八音盒");
    set_input("5263", NULL, NULL, NULL);
    interact(&SPR0[2], "八音盒");
    wck(S.musicSolved == 1, "八音盒 5-2-6-3 → 大门密码");

    /* 15. 保险箱 */
    wck(walk_to_sprite(&SPR0[10]), "走到保险箱");
    set_input("1986", NULL, NULL, NULL);
    interact(&SPR0[10], "保险箱");
    wck(S.brassKey == 1, "保险箱 1986 → 黄铜钥匙");

    /* 16. 大门 */
    wck(walk_to_sprite(&SPR0[4]), "走到大门");
    set_input("5263", NULL, NULL, NULL);
    interact(&SPR0[4], "大门");
    wck(S.ending == 1, "三道锁全开 → 结局「走出去的人」");
    wck(S.mistakes == 0, "全程 0 错误");

    /* 17. 隔墙互动扫描 */
    {
        int geom = 0, samples = 0;
        int bad = scan_aim(&geom, &samples);
        printf("         扫了 %d 个机位，其中几何上被墙挡住的 %d 个\n", samples, geom);
        wck(bad == 0, "全图扫描：没有一次能隔着墙按 E（修复前有 21 次）");
    }

    /* 18. 碰撞粗暴测试 */
    {
        unsigned seed = 12345u;
        int bad = 0;
        S.floorNo = 0;
        P.x = 13.0; P.y = 12.2;
        for (int i = 0; i < 800; i++) {
            seed = seed * 1103515245u + 12345u;
            switch ((int)((seed >> 16) % 6u)) {
            case 0: handle_key('w'); break;
            case 1: handle_key('s'); break;
            case 2: handle_key('a'); break;
            case 3: handle_key('d'); break;
            case 4: handle_key(K_LEFT); break;
            default: handle_key(K_RIGHT); break;
            }
            if (!cell_walkable((int)floor(P.x), (int)floor(P.y))) bad++;
        }
        wck(bad == 0, "随机按 800 次键，没有一步踩进墙里");
    }

    /* 19. 超时分支：第 60 分钟必须判负 */
    int clear_min = S.minutes;              /* 记下真正通关用了多久，后面几项会重置状态 */
    {
        memset(&S, 0, sizeof S);
        S.torch = 1; S.minutes = 59; S.ending = 0;
        P.x = 13.0; P.y = 8.6;
        act_mirror();                       /* 看一眼镜子：+1 分钟 */
        wck(S.minutes >= TIME_LIMIT && S.ending == 2, "第 60 分钟 → 结局「房子记得所有人」");
    }

    /* 20. 错满 5 次分支 */
    {
        memset(&S, 0, sizeof S);
        S.torch = 1; S.ending = 0;
        P.x = 10.8; P.y = 4.8;
        for (int i = 0; i < MAX_MISTAKE; i++) {
            set_input("0000", NULL, NULL, NULL);
            act_safe();
        }
        wck(S.mistakes == MAX_MISTAKE && S.ending == 3, "错满 5 次 → 结局「第五次错误」");
    }

    printf("\n  %s：失败 %d 项 / 通关用时 %d 分钟\n",
           g_walkfail ? "== 走位自检未通过 ==" : "== 走位自检全部通过 ==", g_walkfail, clear_min);
}

int main(int argc, char **argv) {
    if (argc > 1 && strncmp(argv[1], "--dump", 6) == 0) {
        int color = (strcmp(argv[1], "--dump-color") == 0);
        dump_views(color, argc > 2 ? argv[2] : (color ? "dump_color.txt" : "dump.txt"));
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "--autotest") == 0) {
        autotest();
        return g_fail ? 1 : 0;
    }
    if (argc > 1 && strcmp(argv[1], "--walktest") == 0) {
        walktest();
        return g_walkfail ? 1 : 0;
    }

    memset(&S, 0, sizeof S);
    S.rustyKey = 1;
    S.floorNo = 0;
    P.x = 13.0; P.y = 12.2; P.ang = -PI / 2;

    con_setup();
    intro();

    while (S.ending == 0) {
        present();
        int k = read_key();
        g_status[0] = '\0';
        handle_key(k);
        if (S.ending == 0 && S.minutes >= TIME_LIMIT) S.ending = 2;
        if (S.ending == 0 && S.mistakes >= MAX_MISTAKE) S.ending = 3;
    }

    finish();
    con_restore();
    return 0;
}
