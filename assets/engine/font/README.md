# 程序字体

资源选择页的“设置”提供“默认字体”和“原版经典字体”两个选项。两种字库及本目录的配置随主程序更新。

| 选项 | 文件 | 配置 ID |
| --- | --- | --- |
| 默认字体 | `font.ttf` | `default` |
| 原版经典字体 | `founder-libian.ttf` | `founder-libian` |

默认字体由程序自动加入列表，并沿用 `engine/font/font.ttf` 的原有读取规则。`fonts.ini` 的 Count 只统计额外字体，当前为 1；其中 File 路径相对 engine 根目录。共享配置 `[game] font` 保存稳定 ID，字体在进入游戏时生效。

原版经典字体基于此前字库对横竖笔画轻微加重，保留紧凑结构。6,763 个 CJK 汉字全部已调整，其余字形和字符映射保持。内部英文家族为 `Classic Game Font`，中文家族为“原版经典字体（调整版）”，PostScript 名为 `ClassicGameFont-Regular`，版本为 `Version 4.00; weight adjustment 20261106`。原版权与授权元数据保留；具体历史字库版本及本项目的内置／再分发授权链仍未核实。

加载优先级为当前 MOD 自带的 `font/font.ttf`、用户选择、程序默认字体。所选字体缺失、损坏或列表不可用时使用默认字体；所选字库缺少的单字由默认字体补齐。

默认字库的许可证保存在 `ofl.txt`。原版经典字体的来源、字形比对和授权状态见主仓库 `docs/font-selection-20261104.md`。

## 文件校验

- `font.ttf` SHA-256：`40876902a7ce25268ab710ad8fe6e2b63bc002aa4b68d22fd45fc1243726ced5`。
- `founder-libian.ttf`：6,039,736 字节；SHA-256：`8dc4695b160d8015dd58bbb9c353bb1e0b964f67297a1abd7c8e7ba472a3d72c`。
