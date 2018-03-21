# SHF
Sword Heroes' Fate

# 说明

引擎使用C++编写，运行游戏需要安装VC2015运行库。

编辑器和启动器使用Delphi编写，可以使用Delphi2010以上的版本编译。
 
# 感谢
 
剑侠情缘2这个游戏是我玩的第一个武侠游戏，能够重制这款游戏一直是我埋在心里的梦想。大概3、4年前我曾经尝试使用delphi和lazarus分别做过一些，但由于水平有限同时时间也不允许，引擎只实现了显示地图，之后就搁置了。今年10月，偶像（weyl，scarsty，bt，sb500）发布了金群的C++复刻版，他在发布时说过：这个引擎有没有人使用不那么重要，重要的是完成了自己一直的梦想。这句话一下子点燃了我重拾梦想、再次开始制作剑侠情缘引擎的想法。我的编程启蒙老师是偶像，当初拿着他的pascal版代码研究学习，一字一句的研读和练习，受益良多，而此次的剑侠情缘重制，我又参考了他的C++复刻版，只能用感激涕零来表达对他的感谢！

剑侠情缘游戏的大部分资料参考了月影传说高清版作者小试刀剑发布的资料以及剑侠情缘贴吧里面的资料，感谢小试刀剑以及各位剑侠情缘贴吧吧友的分享。

这是我第一次用C++写大型的、完整的程序，代码如果存在冗余或者不合理等问题，还请见谅。

# 使用的开发库

SDL <https://www.libsdl.org/>

SDL_image <https://www.libsdl.org/projects/SDL_image/>

libpng <http://www.libpng.org/pub/png/libpng.html>

SDL_ttf <https://www.libsdl.org/projects/SDL_ttf/>

freetype <https://www.freetype.org/>

FMOD <https://www.fmod.com/>

FFmpeg <https://www.ffmpeg.org/>

libiconv <https://www.gnu.org/software/libiconv/>

lua <https://www.lua.org/>

minilzo <http://www.oberhumer.com/opensource/lzo>

ini Reader <https://github.com/benhoyt/inih>

libconvert <https://github.com/scarsty/convert>
 
# 授权 

SHF is created by Upwinded@www.upwinded.com.

Special thanks to Scarsty(SunTY, Weyl, BT, SB500), XiaoShiDaoJian, DaWuXiaLunTan(www.dawuxia.net), JianXiaQingYuanTieBa@tieba.baidu.com.

The source codes are distributed under zlib license, with two additional clauses:

1.Full right of the codes is granted if they are used in non-KYS related and non-SHF related games.

2.If the codes are used in KYS related or SHF related games, the game itself shall not involve any sort of profit making aspect.

(KYS means Kam Yung's Stories, and SHF means Sword Heroes' Fate.)

The zlib license is as followed：

http://www.zlib.net/zlib_license.html

The license only has the following points to be accounted for:

Software is used on 'as-is' basis. Authors are not liable for any damages arising from its use.

The distribution of a modified version of the software is subject to the following restrictions:

   1.The authorship of the original software must not be misrepresented,
   
   2.Altered source versions must not be misrepresented as being the original software, and
   
   3.The license notice must not be removed from source distributions.
   
The license does not require source code to be made available if distributing binary code.

# 游戏说明
## 完整游戏下载地址

https://pan.baidu.com/s/1WB8mQ4X6ocCgCjEpCpdkDg
 
## 作弊模式

在开始新游戏或读取进度之后，按shift+F12会开启\关闭作弊模式

作弊模式下：

shift+Q会补满生命、体力和内力；

shift+W会增加正在修炼的武功经验；

shift+E会增加自身经验。
 
## 引擎怎样支持mod？
引擎支持的图片格式是我自己设计的png打包格式，与金群imz格式类似，脚本文件则全部转换为了lua语法的脚本。以上图片转换和脚本转换过程是使用Editor文件夹的工具进行了转换，今后该工具还会扩展支持贴图、地图等编辑。
 
## 为什么改变了图片和脚本格式？

### 1、图片格式更改原因：
（1）原始的图片格式为MPC格式和SHD格式，这种格式只能是256色，且对半透明的支持也不是很好，扩展性不如直接使用PNG；

（2）原始MPC图片存在大面积透明像素，将其转换为SDL2的Texture时，空白部分也会占用内存，严重消耗系统资源，而将其转为PNG时，剪裁掉了透明像素，保留有效像素，缩小了图像尺寸。

### 2、脚本格式更改原因：
（1）以后的剧情设计可能会用到复杂的语法；

（2）偷懒不愿意写脚本解析……
