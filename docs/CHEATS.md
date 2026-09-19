# 金手指

在启动游戏文件旁创建同名附加 `.cheats` 的 UTF-8 无 BOM 文本，例如 `赤刃.smf.cheats`。每行一条，`#` 开头的独立行是注释。

```text
var:变量名=值
sprite:精灵名.x=100
sprite:精灵名.visible=true
frame:playing=false
```

精灵支持 x、y、depth、frame、visible、playing；movie: 可代替 sprite:。主时间轴支持 frame:goto 和 frame:playing。变量和精灵名称必须来自具体游戏，不能直接导入 CWCheat 地址码。

进入游戏按 □ → 金手指；↑↓选择，○开关并保存，△重新读取，×返回。无状态前缀的规则默认关闭；程序会将开关写为 `0` 或 `1` + Tab + 规则。启用规则持续写入，关闭不会恢复原值。

## 赤刃：脚本核对示例

```text
# 血量
var:p_hp=96
# 能量
var:p_mp=84
# 命数
var:p_life=9
```

这些变量来自中文赤刃关卡脚本：初始/复活血量 96，能量上限 84，命数存档字段一位。尚未真机验证金手指效果；满血不保证免疫掉坑或剧情死亡。仓库不附带游戏文件。
