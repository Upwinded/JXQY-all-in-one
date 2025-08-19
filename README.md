# JXQY-all-in-one（old:SHF）

剑侠情缘-all-in-one（old:Sword Heroes' Fate）

## 为什么改了名字？

因为一直有一个比较遥远的目标，就是把剑侠情缘三部曲都实现，并且移植到多个平台。目前剑侠情缘2能在多平台运行。

# release说明

作者编译的版本支持如下平台：

windows 7以上， android 9以上, ubuntu20.04 以上, macos 11.3以上, ios 12.0以上
（最新版本1.4.0，除了苹果的平台外，都采用了Vulkan后端，有些平台可能不能支持）

下载地址：
百度网盘：
链接：https://pan.baidu.com/s/140vPCnQYC2DrRZKpjlb51w?pwd=juyh 
提取码：juyh 

ubuntu版本，需要20.04以上，可直接运行run.sh开始游戏

# repository说明与编译

## 依赖和运行时库

因为多平台的依赖库找起来比较麻烦，所以我把我使用的库上传到了这里，不再放在repository里面：
链接：https://pan.baidu.com/s/1NqzXZMnX0xsk0jhXNIHk_Q?pwd=60r2 
提取码：60r2 

devel文件夹里面是各平台的开发库，可根据平台选择压缩包进行解压，并将解压出来的各平台文件夹放置在repo的ThirdParty/devel/文件夹下。

bin.zip文件夹包含windows下的32位运行库，将解压出来的bin文件夹放到repo的根目录下。

## 资源文件

剑侠情缘2的资源文件可以从 https://github.com/Upwinded/jxqy2-assets 获取，把这些文件全部放入Assets文件夹。

android,iOS,macOS会自动打包assets文件夹里面的内容（如果自己修改assets内容，存档请只保留rpg0文件夹下的内容）

## windows

使用vs2022打开 win文件夹下面的 jxqy-all-in-one.sln 文件，进行编译即可。

## android

使用android studio打开android文件夹，进行编译即可。

## linux

cd linux

在ubuntu下可使用 install-dependents.sh 安装依赖库，其它系统请自行解决这部分
./install-dependents.sh

./build.sh

## macOS && iOS

进入 macos_ios/jxqy 文件夹

使用 xcode 打开 jxqy.xcodeproj 工程，选择不同Target进行编译即可。
 
# 感谢
 
剑侠情缘2这个游戏是我玩的第一个武侠游戏，能够重制这款游戏一直是我埋在心里的梦想。大概3、4年前我曾经尝试使用delphi和lazarus分别做过一些，但由于水平有限同时时间也不允许，引擎只实现了显示地图，之后就搁置了。2017年10月，偶像（weyl，scarsty，bt，sb500）发布了金群的C++复刻版，他在发布时说过：这个引擎有没有人使用不那么重要，重要的是完成了自己一直的梦想。这句话一下子点燃了我重拾梦想、再次开始制作剑侠情缘引擎的想法。我的编程启蒙老师是偶像，当初拿着他的pascal版代码研究学习，一字一句的研读和练习，受益良多，而此次的剑侠情缘重制，我又参考了他的C++复刻版，只能用感激涕零来表达对他的感谢！

剑侠情缘游戏的大部分资料参考了月影传说高清版作者小试刀剑发布的资料以及剑侠情缘贴吧里面的资料，感谢小试刀剑以及各位剑侠情缘贴吧吧友的分享。

这是我第一次用C++写大型的、完整的程序，代码如果存在冗余或者不合理等问题，还请见谅。

# 使用的开发库

SDL <https://www.libsdl.org/>

SDL_image <https://www.libsdl.org/projects/SDL_image/>

libpng <http://www.libpng.org/pub/png/libpng.html>

SDL_ttf <https://www.libsdl.org/projects/SDL_ttf/>

freetype <https://github.com/freetype/freetype>

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

(KYS means All Heroes in Kam Yung's Stories, and SHF means Sword Heroes' Fate.)

The zlib license is as followes：

http://www.zlib.net/zlib_license.html

The license only has the following points to be accounted for:

Software is used on 'as-is' basis. Authors are not liable for any damages arising from its use.

The distribution of a modified version of the software is subject to the following restrictions:

   1.The authorship of the original software must not be misrepresented,
   
   2.Altered source versions must not be misrepresented as being the original software, and
   
   3.The license notice must not be removed from source distributions.
   
The license does not require source code to be made available if distributing binary code.

# 游戏说明
 
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

# 引擎说明：

## 引擎底层：

引擎底层的实现主要封装在EngineBase类中，对外的接口则封装在Engine类中，大部分功能都已经可以使用简单的函数实现，如果没有特殊需求，此部分可以不用更改。下面对底层中一些主要的逻辑进行说明：

（1）SDL2：绘图与鼠标键盘输入检测等基本功能都是使用SDL2来实现的。初始化时默认使用硬件加速和垂直同步。引擎可以选择全屏模式和窗口模式，默认分辨率在创建时给定，为方便UI设计此分辨率一般不要更改，但画面可以进行拉抻和缩放：在全屏模式下，保持当前屏幕分辨率不变，画面直接进行拉抻铺满屏幕；在窗口模式时，根据窗口大小，画面保持长宽比例，进行缩放以适应窗口大小。这种窗体大小的自适应改变只在引擎底层中体现，而引擎对外接口是不受影响的，也就是说，在游戏上层调用时，可以认为游戏画面的分辨率始终就是不变的，鼠标的位置检测也没有任何影响。

（2）FFmpeg：用于视频解码。引擎中视频播放时间以音频为准，由于采用单线程解码，在有拖拽窗体等需等待的事件发生时，视频和音频会停止播放。视频播放还支持设置播放的位置，并且可以同时播放多个视频。另外需要注意的是，剑侠情缘2的视频格式过于古老，在解码之后还需调用sws_scale进行图像处理。

（3）FMOD：用于音频播放，需要注意的是在播放视频解码得到音频帧时，为实现效果较好的无缝衔接，需单独创建一个FMOD_SYSTEM（作者能力有限，只会这种方法）。

## Element类：

学习了偶像（Weyl、Scarsty、SunTY、BT、SB500）的kys-cpp复刻版的代码，仿造其Element类，在我的引擎中也创建了Element类。此类是游戏中的基本类，基于此类创建了游戏的UI界面控件：包括菜单、按钮、滚动条、标签、图像载体、视频播放器等等控件；还创建了游戏中的NPC类，物体类，飞行技能类等等。此类可以设置一个父节点和若干子节点，子节点具有优先级属性。类的关键函数有：run、onUpdate、onEvent、onHandleEvent、onDraw，下面具体介绍一下这些函数的功能。

（1）run函数：会使节点进入运行模式，直到其running属性为false时退出运行，运行时会不断调用onUpdate、onEvent、onDraw等函数。

（2）onUpdate函数：此函数应用来做节点每帧的状态处理，会从当前执行run的节点向父节点寻找，直到没有父节点时的节点为顶端节点，此时顶端节点以下的节点都会执行此函数，执行顺序从优先级高的子节点到优先级低的子节点再到父节点。

（3）onEvent函数：此函数应用来做节点每帧的事件处理，会以当前执行run的节点为顶端，其下的节点才会执行此函数，执行顺序从优先级高的子节点到优先级低的子节点再到父节点。

（4）onHandleEvent函数：这个函数在onEvent执行后调用，必须是消息队列中有消息时才会执行，会传入一些鼠标、键盘点击等事件，节点检测事件类型，如需处理，应在函数结束后返回true，表示此事件已处理，需要从消息队列将其删除；而不需处理则返回false，表示重新压回消息队列供其它节点处理。

（5）onDraw函数：这个函数是绘制自身，会从当前执行run的节点向上寻找，直到没有父节点或者节点的drawFullScreen属性为true时，以此节点为顶端向下执行，执行顺序与其它函数不同，先执行父节点，再执行优先级低的子节点、再执行优先级高的子节点。

## 文件管理：

引擎有一个打包文件：game\data\data.dat文件，其中包含了游戏的大部分资源文件，根据原始文件的路径和名称计算出一个文件ID，按顺序进行存储。引擎在读取文件时，优先寻找原始文件路径和名称，如果没有找到，则在打包文件中进行寻找。

# 编辑器说明：

目前编辑器尚未完成，只能用来转换文件格式、打包解包和查看转换后的图片。

## 脚本转换：

可以将剑侠情缘2的脚本转为lua语法脚本，目前有小概率可能出现错误。

## 图片转换：

可以将剑侠情缘系列的mpc、shd、asf格式图片转为游戏使用的IMG格式。半透明模式只对mpc文件有效，其它格式没有影响。图片转换之后可以进行剪裁，去掉图片周边的空白区域，但游戏界面所使用的图片不建议进行剪裁。
