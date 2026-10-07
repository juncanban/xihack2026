from docx import Document
from docx.shared import Inches, Pt, RGBColor
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.enum.table import WD_TABLE_ALIGNMENT, WD_CELL_VERTICAL_ALIGNMENT
from docx.enum.section import WD_SECTION
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.enum.style import WD_STYLE_TYPE
from pathlib import Path

OUT = Path('artifacts') / 'vibe_pet_技术说明书.docx'
OUT.parent.mkdir(parents=True, exist_ok=True)

doc = Document()
sec = doc.sections[0]
sec.top_margin = Inches(0.7)
sec.bottom_margin = Inches(0.65)
sec.left_margin = Inches(0.8)
sec.right_margin = Inches(0.8)

def set_font(run, name='Microsoft YaHei', size=10.5, bold=False, color=None):
    run.font.name = name
    run._element.get_or_add_rPr().rFonts.set(qn('w:eastAsia'), name)
    run._element.get_or_add_rPr().rFonts.set(qn('w:ascii'), name)
    run.font.size = Pt(size)
    run.bold = bold
    if color:
        run.font.color.rgb = RGBColor(*color)

def shade(cell, fill):
    tcPr = cell._tc.get_or_add_tcPr()
    shd = tcPr.find(qn('w:shd'))
    if shd is None:
        shd = OxmlElement('w:shd'); tcPr.append(shd)
    shd.set(qn('w:fill'), fill)

def borders(table, color='D9D9D9'):
    tblPr = table._tbl.tblPr
    b = tblPr.first_child_found_in('w:tblBorders')
    if b is None:
        b = OxmlElement('w:tblBorders'); tblPr.append(b)
    for edge in ('top','left','bottom','right','insideH','insideV'):
        tag = 'w:' + edge
        el = b.find(qn(tag))
        if el is None:
            el = OxmlElement(tag); b.append(el)
        el.set(qn('w:val'), 'single'); el.set(qn('w:sz'), '4'); el.set(qn('w:space'), '0'); el.set(qn('w:color'), color)

def cell_text(cell, text, bold=False, color=None, size=9.2):
    cell.text = ''
    p = cell.paragraphs[0]
    p.paragraph_format.space_after = Pt(2)
    r = p.add_run(str(text)); set_font(r, size=size, bold=bold, color=color)
    cell.vertical_alignment = WD_CELL_VERTICAL_ALIGNMENT.CENTER

def table(headers, rows, widths=None):
    t = doc.add_table(rows=1, cols=len(headers))
    t.alignment = WD_TABLE_ALIGNMENT.CENTER
    t.style = 'Table Grid'
    borders(t)
    for i,h in enumerate(headers):
        cell_text(t.rows[0].cells[i], h, True, (255,255,255), 9)
        shade(t.rows[0].cells[i], '1F4E79')
    for ridx,row in enumerate(rows):
        cells = t.add_row().cells
        for i,val in enumerate(row):
            cell_text(cells[i], val, False, None, 9)
            if ridx % 2 == 1: shade(cells[i], 'F3F6F9')
    if widths:
        for row in t.rows:
            for i,w in enumerate(widths): row.cells[i].width = Inches(w)
    doc.add_paragraph().paragraph_format.space_after = Pt(2)
    return t

def para(text='', style=None, bold_lead=None):
    p = doc.add_paragraph(style=style)
    p.paragraph_format.line_spacing = 1.15
    p.paragraph_format.space_after = Pt(6)
    if bold_lead and text.startswith(bold_lead):
        r = p.add_run(bold_lead); set_font(r, bold=True)
        r = p.add_run(text[len(bold_lead):]); set_font(r)
    else:
        r = p.add_run(text); set_font(r)
    return p

styles = doc.styles
for name in ['Normal','Title','Heading 1','Heading 2','Heading 3']:
    st = styles[name]
    st.font.name = 'Microsoft YaHei'; st._element.rPr.rFonts.set(qn('w:eastAsia'), 'Microsoft YaHei')
styles['Normal'].font.size = Pt(10.5)
styles['Title'].font.size = Pt(24); styles['Title'].font.bold = True; styles['Title'].font.color.rgb = RGBColor(31,78,121)
styles['Heading 1'].font.size = Pt(15); styles['Heading 1'].font.bold = True; styles['Heading 1'].font.color.rgb = RGBColor(31,78,121)
styles['Heading 2'].font.size = Pt(12); styles['Heading 2'].font.bold = True; styles['Heading 2'].font.color.rgb = RGBColor(47,84,150)

p = doc.add_paragraph(style='Title'); p.alignment = WD_ALIGN_PARAGRAPH.CENTER
r = p.add_run('vibe pet 小苹果桌面宠物技术说明书'); set_font(r, size=24, bold=True, color=(31,78,121))
p = doc.add_paragraph(); p.alignment = WD_ALIGN_PARAGRAPH.CENTER
r=p.add_run('设计、实现、接线、运行与测试记录'); set_font(r,size=13,color=(89,89,89))
p = doc.add_paragraph(); p.alignment = WD_ALIGN_PARAGRAPH.CENTER
r=p.add_run('项目版本：fridge-ui-v1  |  文档日期：2026-10-02  |  时区：Asia/Shanghai'); set_font(r,size=9,color=(89,89,89))
doc.add_paragraph()
para('本报告面向项目评审、复现和维护人员，说明 vibe pet 的总体设计、系统架构、AI 能力边界、硬件物料与接线方式、固件和宿主机工程、运行步骤，以及已完成的功能、性能和安全测试。项目的核心结论是：它不是摄像头识别或大模型推理设备，而是把 Claude Code 的工作事件映射成设备状态，再以串口驱动 Wio Terminal 上的小苹果动画；同时保留宠物、树木档案、日历、倒计时和冰箱库存记账功能。')

doc.add_heading('1 项目概述与设计思路', level=1)
para('项目目标是给 AI 编码代理配一只实体桌面宠物，让代理在思考、调用工具、等待批准、完成或出错时，Wio Terminal 屏幕上的小苹果呈现不同反馈。设计选择了“事件驱动、状态机、串口传输”的路线：Claude Code Hook 负责产生事件，Python 宿主机负责转发，固件负责状态解析、动画和本地业务数据。这样做的优点是协议简单、故障隔离清晰、无需云端服务，也适配初代 Wio Terminal 没有蓝牙射频的硬件条件。')
para('冰箱助手是在原桌面宠物基础上的本地业务扩展。六类食材分别保存库存、今日已吃和今日进货；操作通过统一的增量记账和差额修正逻辑完成，并使用双槽文件、序号和 CRC 校验保存。桌面挂件则复用 UDP 状态通道，在没有开发板时提供可视化演示。')

doc.add_heading('2 系统架构', level=1)
table(['层级','模块','职责','接口'],[
['事件源','Claude Code Hooks','接收 SessionStart、UserPromptSubmit、PreToolUse、PostToolUse、Notification、Stop、SessionEnd 事件','stdin JSON'],
['宿主机','host/hook_event.py','把 Hook 事件映射为 S:状态 和 D:时间报文，UDP 发往本机','UDP 127.0.0.1:9911'],
['宿主机','host/vibe_pet_host.py','监听 UDP、转发到串口、读取 READY/OK/ERR 回包','USB 串口 115200'],
['固件','firmware/src/vibe_pet.cpp','解析行协议、状态机、动画、按键和诊断命令','S/T/P/D/K/U/V/R'],
['业务数据','fridge_logic / fridge_store','食材记账、修正、撤销、CRC 双槽持久化','Flash 文件 /fridge0.dat、/fridge1.dat'],
['无开发板模式','fridge_magnet.py','Tkinter + Pillow 像素挂件，自动演示并监听 UDP','桌面窗口']
],[1.0,1.45,3.15,1.45])
para('核心数据流为：Claude Code Hook → hook_event.py → UDP 9911 → vibe_pet_host.py → COM8 USB 串口 → Wio Terminal。设备静默约 2 分钟自动回到 IDLE，静默约 10 分钟进入 SLEEP；固件侧 watchdog 不依赖宿主机心跳。')
table(['输入事件','设备状态','动画表现'],[
['UserPromptSubmit','THINKING','抬眼思考、摸头、头顶省略号'],['PreToolUse','TOOL','敲键盘并产生火花'],['Notification','WAIT','上下蹦跳、问号、摊手'],['Stop','DONE','跳跃、笑眼、撒纸屑'],['SessionStart / SessionEnd','IDLE','呼吸、偶尔眨眼'],['异常或手动状态','ERROR / SLEEP','错误摇晃或闭眼 Zzz']
],[1.8,1.35,3.9])

doc.add_heading('3 AI 能力说明与边界', level=1)
para('本作品的 AI 能力来自 Claude Code 这一外部代理：项目通过 Hook 获取代理生命周期事件，并把事件映射为人类可感知的设备反馈。项目本身不运行大语言模型、不执行视觉识别、不读取摄像头，也不对食材做图像识别。冰箱库存是用户通过按键录入的结构化数据，固件只负责业务规则和持久化。')
para('因此，“识别准确率”不适用于本作品。可验证的准确性指标是事件状态映射：7 类 Hook 事件有明确映射，未知事件只发送时间同步，不会误切换状态；协议解析采用白名单状态值，非法状态不会进入动画状态机。报告中不把事件映射准确率冒充为模型识别准确率。')

doc.add_heading('4 物料清单 BOM', level=1)
table(['序号','物料','数量','规格或作用','备注'],[
['1','Seeed Wio Terminal','1','SAMD51，320×240 LCD，五向键，USB 串口','主控与显示'],
['2','USB 数据线','1','USB 供电与串口通信','必须支持数据传输'],
['3','电脑','1','Windows，Python 3.11+；用于宿主机、烧录和 Hook','开发/运行环境'],
['4','PlatformIO Core','1','6.1.19（本机已验证）','编译与烧录'],
['5','Python 依赖','1组','pyserial>=3.5；Pillow（桌面挂件）','宿主机与桌面模式'],
['6','USB 供电','1','由电脑 USB 口提供','功耗未单独实测']
],[0.45,1.55,0.55,3.0,1.2])

doc.add_heading('5 接线与通信说明', level=1)
para('硬件接线只有一条 USB 数据线：电脑 USB 口连接 Wio Terminal USB 口。该连接同时提供 5 V 供电、固件烧录和 115200 baud 串口通信，不需要外接传感器、摄像头或继电器。串口默认 COM8，实际端口应以 PlatformIO device list 检测结果为准。')
table(['报文','方向','说明'],[
['S:IDLE|THINKING|TOOL|WAIT|DONE|ERROR|SLEEP','主机→固件','切换状态，固件返回 OK:STATE'],['T:<n>','主机→固件','更新 token 计数显示'],['P','主机→固件','心跳，固件返回 OK:PONG'],['D:<epoch>','主机→固件','按 UTC+8 主机时间同步设备软时钟，返回 OK:TIME'],['U / K:<key> / V:SCREEN / R:REBOOT','调试主机→固件','诊断、UI 按键分发、屏幕回读和重启']
],[2.7,1.1,3.1])

doc.add_heading('6 固件源码、工程文件与依赖', level=1)
para('固件工程位于 firmware/，入口为 firmware/src/vibe_pet.cpp，PlatformIO 配置为 firmware/platformio.ini。构建环境使用 atmelsam 平台、seeed_wio_terminal 开发板和 Arduino 框架；外部库为 Seeed Arduino FS ^2.1.5 与 Seeed Arduino SFUD ^2.0.2。核心源码还包括 clock.cpp（软时钟与公历换算）、screens.cpp（界面）、orchard.cpp（页面和交互）、fridge_logic.h（冰箱业务规则）、fridge_store.cpp（双槽存储）、save.cpp（通用保存）和 hanzi16.h（中文字模）。')
para('宿主机工程位于 host/：vibe_pet_host.py 是 UDP 到串口守护进程，hook_event.py 是 Hook 入口，simulate.py 是状态模拟器，device_console.py 和 verify_fridge_hardware.py 用于串口诊断和真机回归。host/requirements.txt 目前只声明 pyserial>=3.5；桌面挂件额外使用 Pillow 和 Tkinter。')
para('固件编译命令：pio run；烧录命令：pio run -t upload --upload-port COM8。运行宿主机：python host/vibe_pet_host.py --port COM8。无开发板演示：python fridge_magnet.py。')

doc.add_heading('7 运行步骤', level=1)
for s in [
'安装 PlatformIO，并用 USB 数据线连接 Wio Terminal；执行 pio device list 确认端口。',
'进入 firmware/ 编译并烧录固件，烧录完成后关闭 PlatformIO 监视器，避免占用 COM8。',
'在宿主机启动 host/vibe_pet_host.py，确认出现 serial opened 和 listening on udp://127.0.0.1:9911。',
'运行 host/simulate.py 验证 THINKING、TOOL、WAIT、DONE、ERROR 等动画。',
'启动 Claude Code 前，把 hooks/settings.snippet.json 的 hooks 字段合并到用户级 settings.json。',
'设备重启后先发送 D:<epoch> 校时；进入冰箱助手后首次通过“修正当前库存”录入真实库存。',
'如果不接开发板，直接运行 fridge_magnet.py；它会自动演示，右上角绿点表示收到过真实联动事件。'
]: para(s, bold_lead=None)

doc.add_heading('8 功能测试与安全测试记录', level=1)
para('测试记录以 2026-10-02 的真机回归为准，设备为 Wio Terminal，LCD 分辨率 320×240，串口 COM8，固件标识 fridge-ui-v1。测试通过 K: 共用按键分发和串口诊断完成，验证后已把临时数量恢复为零，并重启复核。')
table(['测试类别','测试内容','结果'],[
['核心业务','苹果进货 5、吃掉 2；库存/今日已吃/今日进货为 3/2/5','通过'],
['差额修正','今日已吃 2 改 1，库存 3 恢复为 4；今日进货 5 改 4，库存 4 调为 3','通过'],
['库存与撤销','库存改 7，今日记录不变；撤销恢复 3；撤销仅支持最近一次成功操作','通过'],
['食材隔离','菠菜与苹果独立记账；六种食材分页和单位显示正确','通过'],
['持久化','双槽文件、CRC、序号回绕、软件重启恢复','通过'],
['边界安全','数量 0–99；吃掉/进货 1–99；库存不足、结果超过 99 拒绝整笔操作','通过'],
['异常恢复','普通保存失败恢复提交前内存；跨午夜首次确认日期变化，二次确认才提交','通过'],
['兼容保留','农事数据和宠物档案 CRC 前后保持一致；倒计时 60 秒到点提醒','通过'],
['UI 回归','25 张原生绘制快照，321 个中文字模覆盖','通过']
],[1.15,4.85,0.7])
para('安全性在本项目中主要指数据完整性、误操作防护和通信边界。固件对状态和数量使用白名单/范围校验，保存采用 CRC 和双槽切换，失败时回滚内存业务状态；串口诊断命令只在本地 USB 链路使用，Hook 仅向 127.0.0.1 发送 UDP，不依赖公网。项目没有账号系统、网络服务端或远程执行入口，因此未开展网络渗透测试；这部分属于项目边界。')

doc.add_heading('9 关键性能数据与限制', level=1)
table(['指标','数据或结论','数据来源'],[
['固件 Flash','129,928 / 507,904 B，25.6%','实际固件编译记录'],
['静态 RAM','10,900 / 196,608 B，5.5%，不含原动态精灵','实际固件编译记录'],
['LCD','320×240','Wio Terminal 硬件规格/真机记录'],
['串口','115200 baud，默认 COM8','platformio.ini 与真机记录'],
['Hook 响应','脚本设计为毫秒级返回；本轮未做独立统计学基准测试','hook_event.py 设计说明'],
['动画刷新','桌面挂件约 33 ms 一帧，即约 30 FPS；固件动画按 millis 驱动','fridge_magnet.py / 固件实现'],
['识别准确率','不适用；本项目没有视觉或语音识别模型','能力边界说明'],
['功耗','未使用功耗仪实测，当前仅确认由 USB 供电稳定运行','测试记录限制']
],[1.25,3.6,1.85])
para('因此，响应时间和功耗不能被报告为未经测量的精确值。后续若需要量化，可在 Hook 入口增加高精度时间戳，在串口回包处测量端到端延迟，并使用 USB 功率计分别记录空闲、动画和屏幕回读工况。')

doc.add_heading('10 复现与维护建议', level=1)
para('复现时应先确认 COM 端口和 PlatformIO 依赖，再烧录固件；串口被其他监视器占用是最常见的启动失败原因。桌面挂件和 vibe_pet_host.py 都监听 UDP 9911，二者不要同时启动；如果需要同时显示桌面挂件并驱动真机，应只启动 fridge_magnet.py --serial COM8。修改固件页面或存储结构后，应至少重复运行 tests/fridge_logic_test.cpp、tests/orchard_test.cpp 对应的逻辑回归，并重新执行真机回归，确认临时数据清理和重启恢复。')
para('项目当前的可审计证据集中在 FRIDGE_HARDWARE.md、HARDWARE_VERIFICATION.md、DEBUG_LOG.md 和 artifacts/hardware_fridge/ 下的 build.log、upload.log、serial_log.jsonl、verification_result.json 及屏幕快照。')

footer = sec.footer.paragraphs[0]
footer.alignment = WD_ALIGN_PARAGRAPH.CENTER
r=footer.add_run('vibe pet 小苹果桌面宠物技术说明书'); set_font(r,size=8,color=(128,128,128))

doc.save(OUT)
print(OUT)
