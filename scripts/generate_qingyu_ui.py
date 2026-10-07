"""Write the Qingyu presentation INIs. Run from any directory; no original assets are changed."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "assets/common/ini/ui/qingyu"
PREFIX = "ini\\ui\\qingyu\\"
PAPER = "asf\\ui\\qingyu\\panel.png"
INK = "0xFF234F43"
MUTED = "0xFF766449"
ART = "asf\\ui\\qingyu\\"


def write(path, sections):
    target = ROOT / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text("\n\n".join("[" + section + "]\n" + "\n".join(
        f"{key}={value}" for key, value in values.items())
        for section, values in sections.items()) + "\n", encoding="utf-8", newline="\n")


class Menu:
    def __init__(self, folder, name, width, height, align="alRightCenter", **options):
        self.folder, self.name, self.components = folder, name, []
        if align in ("alLeftCenter", "alRightCenter"):
            options.setdefault("AlignY", -14)
        write(folder + "/window.ini", {"Init": dict(Width=width, Height=height,
            Align=align, Image=PAPER, Stretch=1, NineSlice=128,
            NineSliceWidth=16 if folder in ("top", "bottom", "column") else 24,
            FitToWindow=1, WindowMargin=64, WindowVerticalMargin=188, **options)})

    def add(self, kind, name, x, y, w, h, bind="", format="%d", **values):
        file = self.folder + "/" + name + ".ini"
        write(file, {"Init": dict(Left=x, Top=y, Width=w, Height=h, **values)})
        entry = dict(type=kind, name=name, file=PREFIX + file.replace("/", "\\"))
        if bind:
            entry.update(bind=bind, format=format)
        self.components.append(entry)
        return file

    def label(self, name, text, x, y, w, h=26, **values):
        style = dict(Font=18, Color=INK, AutoShrink=1, MinimumFont=12)
        style.update(values)
        self.add("Label", name, x, y, w, h, Text=text, **style)

    def art(self, name, image, x, y, w, h, **values):
        values.setdefault("Priority", 38)
        self.add("ImageContainer", name, x, y, w, h, Image=ART + image + ".png", Stretch=1, **values)

    def heading(self, text, width):
        self.art("headingFrame", "heading", (width - 180) // 2, 14, 180, 48)
        self.label("heading", text, (width - 142) // 2, 23, 142, 28, CenterText=1, Font=22, Color="0xFFF7EAC5")

    def button(self, name, text, x, y, w=110, h=36, kind="FlatTextButton"):
        self.add(kind, name, x, y, w, h, Text=text, Font=18, Color="0xFFF6ECD1",
                 BackgroundColor="0xFF285447", HoverBackgroundColor="0xFF386C58",
                 PressedBackgroundColor="0xFF1D4137", Image=ART + "button-trim.png", Up=0, Down=0, Stretch=1)

    def slot(self, name, x, y, size=60, kind=None):
        kind = kind or ("magic" if self.folder in ("magic", "xiulian") else "goods")
        self.add("Item", name, x, y, size, size, CenterImage=1,
                 BackImage1=ART + f"slot-{kind}.png", BackImage2=ART + f"slot-{kind}-hover.png",
                 Color="0xFFF8ECCF" if kind == "magic" else INK, Font=16)

    def scroll(self, name, x, y, length, columns=3, horizontal=False):
        w, h = (length, 18) if horizontal else (18, length)
        thumb_w, thumb_h = (24, 18) if horizontal else (18, 28)
        write(self.folder + "/" + name + "-thumb.ini", {"Init": dict(
            Left=0, Top=0, Width=thumb_w, Height=thumb_h, Flat=1)})
        self.add("Scrollbar", name, x, y, w, h, Flat=1, FitTrack=1, Style=int(not horizontal),
                 Min=0, Max=100, LineSize=columns, PageSize=12, SlideBegin=0,
                 SlideEnd=length - (thumb_w if horizontal else thumb_h),
                 SlideBtn=name + "-thumb.ini")

    def finish(self, filename=None):
        write(self.folder + "/" + (filename or self.folder) + ".menu.ini", {
            "menu": dict(name=self.name, window=PREFIX + self.folder + "\\window.ini"),
            **{f"component{i}": c for i, c in enumerate(self.components, 1)}})


write("theme.ini", {"Theme": dict(Name="青玉", Version=1)})

m = Menu("top", "TopMenu", 360, 58, "alRTCorner", AlignX=14, AlignY=10)
for i, (name, text) in enumerate([("equipBtn", "人物"), ("goodsBtn", "行囊"),
    ("magicBtn", "武功"), ("xiulianBtn", "修炼"), ("notesBtn", "纪事"), ("optionBtn", "系统")]):
    m.button(name, text, 14 + i * 56, 13, 52, 32, "CheckBox")
m.finish()

m = Menu("bottom", "BottomMenu", 532, 64, "alBottomCenter", AlignY=-36)
m.art("goodsSeal", "badge-goods", 16, 12, 40, 40)
m.art("magicSeal", "badge-magic", 220, 12, 42, 40)
m.add("Label", "goodsGroup", 18, 20, 38, 24, Text="物品", Font=14, Color="0xFFF8ECCF", CenterText=1)
m.add("Label", "magicGroup", 220, 20, 42, 24, Text="武功", Font=14, Color="0xFFF8ECCF", CenterText=1)
for i in range(8):
    x = 60 + i * 50 if i < 3 else 268 + (i - 3) * 50
    m.slot(("goodsItem" + str(i + 1)) if i < 3 else ("magicItem" + str(i - 2)), x, 10, 44, "goods" if i < 3 else "magic")
    m.art("keyCap" + str(i), "key-cap", x + 1, 10, 16, 15, Priority=32)
    m.add("Label", "key" + str(i), x + 2, 10, 14, 15, Text="ZXCASDFG"[i], Font=11, Color="0xFFF8ECCF", CenterText=1, Priority=31)
m.finish()

m = Menu("column", "ColumnMenu", 240, 70, "alLTCorner", AlignX=-18, AlignY=10)
for i, (name, text, key, maximum, color) in enumerate([
    ("Life", "命", "life", "info.lifeMax", "0xFF985C51"),
    ("Thew", "体", "thew", "info.thewMax", "0xFF98814B"),
    ("Mana", "气", "mana", "info.manaMax", "0xFF508785")]):
    m.add("ColumnImage", "column" + name, 18, 8 + i * 19, 204, 18, Horizontal=1, FillColor=color)
    m.add("Label", "value" + name, 23, 8 + i * 19, 194, 18, Font=13,
          Color="0xFFF6ECD1", AutoShrink=1, CenterText=1,
          bind=f"player.{key},player.{maximum}", format=text + "  %d / %d")
# The rage bar is hidden by ColumnMenu when the selected game has no rage system.
m.add("ColumnImage", "columnRage", 18, 64, 204, 3, Horizontal=1, FillColor="0xFFC08B45")
m.finish()

m = Menu("equip", "EquipMenu", 460, 480, "alLeftCenter")
m.heading("人物", 460)
m.label("playerName", "", 32, 64, 194, 32, CenterText=1)
m.button("equipmentTab", "装备", 242, 64, 82, 32, "CheckBox")
m.button("attributesTab", "属性", 334, 64, 94, 32, "CheckBox")
m.art("partTray", "inset", 30, 104, 400, 196, NineSlice=6, NineSliceWidth=6)
m.art("labTray", "inset", 26, 310, 408, 136, NineSlice=6, NineSliceWidth=6)
m.art("detailTray", "inset", 26, 106, 408, 309, NineSlice=6, NineSliceWidth=6)
for i, text in enumerate(["头饰", "项链", "衣甲", "披风", "武器", "护腕", "鞋履"]):
    x, y = 36 + (i % 4) * 102, 111 + (i // 4) * 99
    m.slot("item" + str(i + 1), x + 16, y, 64)
    m.label("part" + str(i), text, x, y + 66, 96, 25, CenterText=1)
for i, (text, bind, fmt) in enumerate([
    ("等级", "player.level", "%d"), ("攻击", "player.effectiveAttack", "%d"),
    ("生命", "player.life,player.info.lifeMax", "%d / %d"), ("防御", "player.effectiveDefend", "%d"),
    ("体力", "player.thew,player.info.thewMax", "%d / %d"), ("身法", "player.effectiveEvade", "%d"),
    ("内力", "player.mana,player.info.manaMax", "%d / %d"), ("经验", "player.exp,player.levelUpExp", "%d / %d")]):
    names = ["labLevel", "labAttack", "labLife", "labDefend", "labThew", "labEvade", "labMana", "labExp"]
    m.label(names[i], "", 32 + (i % 2) * 205, 318 + (i // 2) * 31, 196,
            bind=bind, format=text + "  " + fmt)
for i in range(14):
    m.label("detail" + str(i), "", 32 + i % 2 * 205, 116 + i // 2 * 42, 196, 30)
m.label("detailHint", "按当前装备与生效状态显示", 32, 425, 396, 28, CenterText=1, Font=14, Color=MUTED)
m.finish()
# Kept as a complete configuration for callers that instantiate StateMenu directly.
m.folder, m.name = "state", "StateMenu"
m.components = [c for c in m.components if not c["name"].startswith("detail")
                and c["name"] not in ("equipmentTab", "attributesTab")]
write("state/window.ini", {"Init": dict(Width=460, Height=480, Align="alLeftCenter",
    Image=PAPER, Stretch=1, NineSlice=128, NineSliceWidth=24, FitToWindow=1, WindowVerticalMargin=188, AlignY=-14)})
m.finish()

for folder, name, title in [("goods", "GoodsMenu", "行囊"), ("magic", "MagicMenu", "武功"),
                             ("buysell", "BuySellMenu", "商铺")]:
    m = Menu(folder, name, 360, 480, "alLeftCenter" if folder == "buysell" else "alRightCenter")
    m.heading(title, 360)
    if folder == "magic":
        m.button("magicTab", "武功", 32, 62, 134, 34, "CheckBox")
        m.button("talentTab", "天赋", 176, 62, 134, 34, "CheckBox")
        m.label("listCaption", "已习得的武学", 34, 70, 292, 25, Font=15, Color=MUTED, CenterText=1)
    else:
        m.label("money", "", 34, 68, 266, 26, bind="player.money", format="银两  %d")
    m.art("gridTray", "inset", 28, 104, 286, 315, NineSlice=6, NineSliceWidth=6)
    for i in range(12):
        m.slot("item" + str(i + 1), 36 + i % 3 * 94, 112 + i // 3 * 77, 66)
    m.scroll("scrollbar", 324, 112, 297)
    m.art("footerRule", "rule", 44, 420, 272, 8)
    m.label("hint", "长按查看 · 拖动整理" if folder == "goods" else "点击查看详情 · 拖动整理" if folder == "magic" else "选择货品后交易", 32, 436, 296, 22, CenterText=1, Font=14, Color=MUTED)
    if folder == "buysell":
        m.button("closeBtn", "关闭", 264, 23, 68, 31)
    if folder == "magic":
        m.art("detailIconFrame", "slot-magic", 34, 68, 62, 62)
        m.add("ImageContainer", "detailIcon", 36, 70, 58, 58, Stretch=1, KeepAspect=1)
        m.label("detailName", "", 110, 77, 214, 40, CenterText=1)
        m.art("detailBody", "inset", 26, 137, 308, 203, NineSlice=6, NineSliceWidth=6)
        m.add("MemoText", "detailText", 34, 145, 292, 194, Font=18, Color=INK, FitLines=1)
        m.button("detailPrevious", "上一页", 34, 350, 90, 32)
        m.label("detailPage", "", 128, 350, 104, 32, CenterText=1)
        m.button("detailNext", "下一页", 236, 350, 90, 32)
        m.button("detailPractice", "设为修炼", 34, 396, 138, 34)
        m.button("detailQuick", "放入快捷栏", 184, 396, 142, 34)
        m.button("detailBack", "返回列表", 104, 438, 152, 30)
    m.finish()

m = Menu("partner", "PartnerEquipMenu", 420, 480, "alLeftCenter")
m.heading("同伴装备", 420)
m.label("titleLabel", "", 36, 76, 348, 30, Font=20, CenterText=1)
m.art("attributeTray", "inset", 28, 112, 364, 126, NineSlice=6, NineSliceWidth=6)
for i, name in enumerate(["level", "life", "thew", "mana", "attack", "defend", "evade"]):
    m.label("partner" + name, "", 40 + i % 2 * 174, 122 + i // 2 * 26, 168, 24, Font=16)
for i, name in enumerate(["头饰", "项链", "衣甲", "披风", "武器", "护腕", "鞋履"]):
    x, y = 48 + i % 4 * 84, 254 + i // 4 * 82
    m.slot("item" + str(i + 1), x, y, 52)
    m.label("slotName" + str(i + 1), name, x, y + 55, 52, 22, Font=14, CenterText=1)
m.button("closeButton", "返回", 264, 427, 108, 32)
m.label("hint", "从行囊拖入装备", 32, 431, 210, 24, Font=14, Color=MUTED)
m.finish()

m = Menu("xiulian", "PracticeMenu", 420, 480, "alLeftCenter")
m.heading("修炼", 420)
m.slot("magic", 164, 86, 88)
m.label("name", "", 32, 188, 356, 34, CenterText=1)
m.label("levelCaption", "等级", 48, 237, 68)
m.label("level", "", 126, 237, 246)
m.label("expCaption", "经验", 48, 275, 68)
m.label("exp", "", 126, 275, 246)
m.label("intro", "", 48, 322, 324, 94, AutoNextLine=1)
m.label("hint", "将武功拖入此格，设为当前修炼武功", 32, 435, 356, 24, CenterText=1)
m.finish()

m = Menu("memo", "MemoMenu", 360, 480, "alRightCenter")
m.heading("江湖纪事", 360)
m.add("MemoText", "memoText", 34, 92, 272, 320, Font=18, Color=INK, LineSize=56, FitLines=1)
m.scroll("scrollbar", 322, 88, 322, 1)
m.label("hint", "滚动翻阅", 32, 430, 296, 26, CenterText=1)
m.finish()

m = Menu("system", "System", 360, 392, "alCenter")
m.heading("小憩", 360)
for i, (name, text) in enumerate([("returnBtn", "继续游历"), ("saveloadBtn", "存取进度"),
    ("optionBtn", "游戏设置"), ("quitBtn", "返回标题")]):
    m.button(name, text, 66, 86 + i * 66, 228, 44)
m.finish()

m = Menu("option", "Option", 500, 460, "alCenter", AlignY=-20)
m.heading("游戏设置", 500)
for i, (name, text) in enumerate([("music", "音乐"), ("sound", "音效"), ("speed", "速度")]):
    y = 92 + 62 * i
    m.label(name + "Caption", text, 34, y, 72)
    m.scroll(name, 116, y + 3, 236, horizontal=True)
    m.button(name + "CB", "默认" if name == "speed" else "静音", 374, y - 3, 92, 34, "CheckBox")
m.button("playerAlpha", "关闭主角半透明", 34, 284, 205, 36, "CheckBox")
m.button("dyLoad", "关闭动态加载", 253, 284, 213, 36, "CheckBox")
m.button("rtnBtn", "返回", 178, 352, 144, 38)
m.finish()

m = Menu("saveload", "SaveLoad", 680, 480, "alCenter")
m.heading("存取进度", 680)
file = m.add("ListBox", "listBox", 36, 78, 190, 330, ItemHeight=30, ItemCount=10, FitItems=1,
             Color=INK, SelColor="0xFF9B3F2F")
import configparser
saved_list = configparser.ConfigParser(interpolation=None)
saved_list.optionxform = str
saved_list.read(ROOT / file, encoding="utf-8")
write(file, {"Init": dict(saved_list["Init"]), "Items": {str(i): f"{i:02}  江湖留影" for i in range(1, 11)}})
m.add("ImageContainer", "snap", 250, 80, 388, 276, Stretch=1, KeepAspect=1)
m.button("saveBtn", "存储", 248, 406, 116, 38)
m.button("loadBtn", "读取", 382, 406, 116, 38)
m.button("exitBtn", "返回", 516, 406, 116, 38)
m.finish()

for folder in ["mapthumbnail", "littlemap"]:
    m = Menu(folder, "MapThumbnailMenu", 620, 480, "alCenter")
    m.heading("舆图", 620)
    m.label("mapNameLabel", "", 40, 28, 400)
    m.add("ImageContainer", "thumbnailContainer", 30, 76, 560, 358, Stretch=1)
    m.button("closeButton", "关闭", 503, 25, 84, 32)
    m.label("hint", "点击地图前往目的地", 32, 441, 556, 24, CenterText=1)
    m.finish()

m = Menu("tooltip", "ToolTip", 360, 382, "alNone")
m.label("name", "", 24, 24, 312, 28)
m.label("cost", "", 24, 57, 312, 24)
m.label("intro1", "", 24, 89, 312, 24, AutoNextLine=1)
m.label("intro2", "", 24, 125, 312, 24, AutoNextLine=1)
m.add("MemoText", "body", 24, 90, 312, 208, Font=18, Color=INK, FitLines=1)
m.button("previous", "上一页", 24, 302, 92, 30)
m.label("page", "", 122, 302, 116, 30, Font=14, CenterText=1)
m.button("next", "下一页", 244, 302, 92, 30)
m.button("back", "返回", 132, 341, 96, 26)
m.label("hint", "点击物品查看完整说明", 24, 341, 312, 26, Font=14, CenterText=1, Color=MUTED)
m.finish()

m = Menu("dialog", "Dialog", 960, 222, "alBottomCenter", AlignY=-46)
m.add("TalkLabel", "label", 178, 34, 604, 124, Font=21, Color=INK)
m.add("ImageContainer", "head1", 24, 26, 138, 166, Stretch=1, KeepAspect=1)
m.add("ImageContainer", "head2", 798, 26, 138, 166, Stretch=1, KeepAspect=1)
m.label("hint", "点击继续", 420, 173, 120, 26, CenterText=1)
m.finish()

m = Menu("choose", "ChooseMenu", 600, 380, "alCenter")
m.label("messageLabel", "", 40, 40, 520, 90, AutoNextLine=1)
for i, name in enumerate(["selectA", "selectB"]):
    m.add("ChooseTextButton", name, 40, 154 + i * 76, 520, 62, Font=20,
          NormalColor=INK, HoverColor="0xFF9B3F2F", PressColor="0xFF9B3F2F")
m.finish()

m = Menu("yesno", "YesNo", 480, 254, "alCenter")
m.label("label", "", 38, 38, 404, 108, AutoNextLine=1)
m.button("yes", "确定", 74, 175, 148, 42)
m.button("no", "取消", 258, 175, 148, 42)
m.finish()

m = Menu("message", "MsgBox", 520, 90, "alTopCenter", AlignY=128)
m.label("label", "", 32, 24, 456, 42, CenterText=1, AutoNextLine=1)
m.finish("msgbox")
m = Menu("timer", "TimerMenu", 140, 54, "alTopCenter", AlignY=80)
m.label("timeLabel", "", 20, 14, 100, 26, CenterText=1)
m.finish()
print(f"Qingyu: {len(list(ROOT.rglob('*.ini')))} INI files written to {ROOT}")
