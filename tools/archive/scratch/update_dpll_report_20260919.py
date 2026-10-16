from pathlib import Path
import copy
import sys
sys.path.insert(0, str(Path('out/report-deps').resolve()))
from bs4 import BeautifulSoup

ROOT = Path('docs/reports/distributed-trigger')
soup = BeautifulSoup((ROOT / 'DHRT100_DPLL_STATUS_REPORT_20260918.html').read_text(encoding='utf-8'), 'html.parser')
pages = soup.select('.page')

def fragment(html):
    return BeautifulSoup(html, 'html.parser')

def fill(node, html):
    node.clear()
    node.extend(list(fragment(html).contents))

def table(headings, rows):
    return '<table class="compact"><thead><tr>' + ''.join('<th>'+x+'</th>' for x in headings) + '</tr></thead><tbody>' + ''.join('<tr>'+''.join('<td>'+x+'</td>' for x in row)+'</tr>' for row in rows) + '</tbody></table>'

def section(title, body):
    return '<div class="section"><div class="section-title">'+title+'</div>'+body+'</div>'

def note(text):
    return '<div class="note-box">'+text+'</div>'

def svg(path, height):
    node = BeautifulSoup(Path(path).read_text(encoding='utf-8'), 'html.parser').find('svg')
    node['style'] = f'display:block;width:100%;height:{height}mm;'
    return str(node)

def heading(page, title, subtitle):
    page.select_one('h1').string = title
    page.select_one('.subtitle').string = subtitle

soup.title.string = 'DHRT100 DPLL 物理闭环阶段报告 · 2026-09-19'
soup.head.append(fragment('<meta name="last-updated" content="2026-09-19">').meta)
soup.style.append('\n  .main { min-height:0; }\n  .source { overflow-wrap:anywhere; }\n')
for page in pages:
    fill(page.select_one('.doc-meta'), '<strong>2026年9月19日</strong>内部研发阶段报告<br>实测与资格分开评价')

fill(pages[0].select_one('.main'), '''
<div class="chips"><div class="chip"><span class="k">验证对象</span><span class="v">NO1–NO4 实际输出</span></div><div class="chip"><span class="k">无外参长期窗口</span><span class="v">600 s / 120 窗通过</span></div><div class="chip"><span class="k">外参斜率对比</span><span class="v">100 / 50 ppb/s</span></div><div class="chip"><span class="k">验证里程碑进度</span><span class="v">75% · 本报告口径</span></div></div>
''' + note('<strong>最新结论：</strong>四板本地闭环、持续硬件输出、内部 SRAM 探针和外部稀疏采样已接通。无外参十分钟运行的全部已采边沿在 ±50 ns 内；外参补偿两组 120 s 调参窗口通过。正式锁定与 VDC 有效发布仍有独立门禁。') +
section('一、近期实测与含义', table(['证据范围','已观察结果','结论边界'], [
['无外参十分钟','NO2/3/4 相对 NO1：[-28.127,-3.705] / [-29.588,-4.961] / [-21.783,2.188] ns。','120 个窗口、四板内部 GUARD 通过；不覆盖窗口间未采边沿。'],
['同轮频差趋势','分钟分段拟合最大 |ppb|：NO2=0.133，NO3=0.215，NO4=0.137。','相差斜率换算的等效频差，含相位校正；不是瞬时上限或绝对晶振精度。'],
['外参 100 / 50 ppb/s','每组 120 s、24 窗；配置/训练/PI/delay 一致，模型均从零重建。','50 减小全段峰值，但 120 s 末态仍在追赶，尚未优于 100 的后段。'],
['近期人工观察','关闭参考约 ±20 ns，开启约 ±50 ns。','用户观察，非同窗统计；不得替代上述原始数据或直接归因于参考源。'],
['共同外部基准','NO1 IN4 接 10 MHz，慢速基线补偿；三从通过 TDMA 本地跟踪。','提高对共同基准的频率一致性与长期相干性的基础；不等于绝对相位对齐或短期抖动更小。']])) +
section('二、里程碑完成度：6 / 8 = 75%', table(['本报告等权检查项','状态'], [
['1 本地闭环；2 TIMER1/硬件持续输出；3 无外参十分钟联合窗口','已验收各自切片'],
['4 外参测量；5 调参 SCPI/Flash；6 外参两分钟斜率对比','已验收各自切片'],
['7 外参十分钟及失联恢复；8 正式精度与 VDC 发布','未完成']])) + '<p class="mini" style="margin-top:5px">百分比仅为以上八项等权阶段快照，不是代码覆盖率，也不是项目综述的跨域加权完成度。</p>' +
section('三、快速通道与当前配置', '<div class="flow">外参 → 硬件窗口 → Core1 慢速基线 + NO1 PI 残差 → committed DCO\nNO1 typed 时间戳 → TDMA 特等席 → 三从同事件匹配 + 本地 delay → DCO\n未来边沿 → SYNC_IO PIO / DMA；Core0 仅 STOP 配置、Flash 与导出</div>' + table(['参数快照','当前实验配置'], [['输出 delay / ns','NO1=0，NO2=−8，NO3=−68，NO4=−116；与 path delay 分离。'],['外参补偿默认','100 ppb/s，IIR 分母 4，测量准入限幅 10000 ppb；50 为试验候选。']])) +
'<div class="bar">当前交付：<strong>实际闭环 + 长窗观测 + 可调外参补偿；正式发布继续分项验收。</strong></div>')
pages[0].select_one('.page-footer span').string = '依据：VDC 进度 20260919-020～024；所有测量数字为实验快照'

# Preserve the original startup and voltage figures with their original experiment identity.
for index in (3,4):
    sub = pages[index].select_one('.subtitle')
    sub.string = '2026-09-18 历史实测保留 | ' + sub.get_text()
for node in list(soup.find_all(string=True)):
    if node.parent.name in ('style','script'):
        continue
    text = str(node)
    text = text.replace('当前候选 0/−28/−88/−160 ns', '9月18日图例候选 0/−28/−88/−160 ns；最新配置见第1页')
    text = text.replace('争取 ±50 ns', '稳定 ±25 ns 表现')
    text = text.replace('进一步压缩这些误差，有望在现有板卡上稳定 ±25 ns 表现，尚需跨启动与长时间实测。', '无外参十分钟已采窗口达到 ±50 ns；下一阶段以稳定 ±25 ns 为目标，补齐跨启动、连续性与外参长稳验证。')
    text = text.replace('现有板卡：从百纳秒窗口向 ±50 ns 优化', '现有板卡：从已采 ±50 ns 向稳定 ±25 ns 优化')
    text = text.replace('已采窗口达到 ±50 ns → 补齐连续性与测量预算 → FPGA 探索 ps 量级。', '已采窗口达到 ±50 ns → 目标稳定 ±25 ns → FPGA 探索 ps 量级。')
    text = text.replace('本轮 NO1 编码区间中位约 120 ns', '9月18日原件的 NO1 编码区间中位约 120 ns')
    text = text.replace('示波器用于校验实际效果，不在实时控制环内。', '示波器波形采集用于校验，不作为实时控制反馈；新增 10 MHz REF OUT 则作为外部频率输入，路径见第7页。')
    text = text.replace('共同参考：NO1 自主 PI → 编码事件时间戳', '共同参考：可选 10 MHz 慢速基线 + NO1 自主 PI → 编码事件时间戳')
    text = text.replace('两轮固定稳定窗口进入 ±100 ns。仍需长期与测量资格闭合。', '9月18日两轮固定稳定窗口进入 ±100 ns；9月19日无外参十分钟已采窗口达到 ±50 ns，仍需连续性及测量资格闭合。')
    text = text.replace('±50 ns 指各从实际边沿相对 NO1 的误差目标，不是当前结论；固定补偿无法消除随机抖动，须覆盖全部节点、多次启动和长稳窗口。', '最新无外参十分钟稀疏窗口已在 ±50 ns 内；固定补偿无法消除随机抖动，仍须核验跨启动、未采空档及完整测量误差。')
    text = text.replace('当前有限窗口 ±100 ns → 现有平台稳定 ±25 ns 表现 → FPGA 探索 ps 量级。', '已采窗口达到 ±50 ns → 目标稳定 ±25 ns → FPGA 探索 ps 量级。')
    text = text.replace('HDO4404 四通道、1× 探头；一秒记录 20 ns 样点，另有 0.2 秒 / 4 ns 网格记录。', 'HDO4404 四通道、1× 探头；历史连续图为 20 ns / 4 ns 网格，最新稀疏调参窗口为 2 ns 网格。')
    if text != str(node):
        node.replace_with(text)

heading(pages[5], '长期观测与调参接口', '无外参十分钟已通过 | 外参参数可配置、可保存、可追溯')
fill(pages[5].select_one('.main'),
note('<strong>十分钟证据：</strong>无外参 plus20-batched-600s 每五秒采样，120 个四路窗口、十个分钟检查点及内部 GUARD 通过；RUN 板端查询为零。窗口按最近周期边沿配对，绝对脉冲序号与无整周期滑移尚未证明；连线不代表空档被采到。') +
svg('out/HardwareAcceptance/20260919/dpll-delay-center-r2/plus20-batched-600s-comparison.svg', 88) +
'<p class="mini">上图为内部残差包络，下图为外部实际边沿差，两者不是同一测量量，不能用内部区间宽度判断 GPIO 超限。第1页频差趋势复算见 out/doc-audit/project-overview-20260919/same-trial-stability.json。</p>' +
section('SCPI 与 Flash：STOP 配置，ARM 锁存', table(['接口 / 参数','语义'], [
['SYST:VDC:REF:DISC:CONF 100,4,10000','依次为斜率 ppb/s、滤波分母、测量准入限幅 ppb；配置示例，非精度承诺。'],
['CONF? / ACT?','请求配置与实际锁存配置分开；ACT? 含 request、generation、CRC 和三个参数。'],
['DEFA / REC / STOR','工厂 RAM 默认 / 召回 / 显式 Flash 保存；先 STOP、关闭补偿，保存前释放参考资源。'],
['持久化结果','非默认保存、召回、重启保留、上电关闭均通过；原 Flash/RAM 已恢复 100/4/10000。']])) +
section('同源码验收与失败保留', table(['项目','结果 / 边界'], [
['Release / 四板 P3','A/B 与 Flash link 通过；P3 PASS_WITH_WARNINGS，25 INFO / 18 WARN / 0 ERROR/FATAL。'],
['软件复核','独立相关回归 155 项、共享配置回归 66 项通过；严格 TDMA 调度告警仍保留。'],
['原始失败','外参十分钟曾因示波器导出超时停止，未签发通过；Flash 首轮恢复遇 STOP 准入拒绝，后续有界 RAM 重试闭合。']])) +
'<div class="bar">Flash 已验证、默认保持 <strong>100 ppb/s</strong>；长期外参及恢复专项继续推进。</div>')
pages[5].select_one('.page-footer span').string = '原件：dpll-delay-center-r2 / reference-discipline / p3-reference-profile-r2'

heading(pages[6], '外参相干性与后续验收', '频率基准、短期相位、测量误差与正式发布分别评价')
fill(pages[6].select_one('.main'),
note('<strong>外部参考的收益：</strong>将原先仅相对 NO1 跟踪的时间体系，增加到外部共同频率基准的约束。NO1 慢速补偿后，三从跟随 NO1，有助于抑制相对外部系统的长期频率漂移、提升相干性。当前实现测频，不直接锁定外参绝对相位；相干性提升幅度仍需独立基准长期复核。') +
section('参考输入链及电气规格', table(['项目','已确认事实 / 尚待验证'], [
['10 MHz REF OUT','用户提供规格图：50 Ω 负载下 1.5 Vpp 正弦波。高阻施密特输入不等于 50 Ω 端接，实际峰值、偏置及反射须在 IN4 测量；高阻本身不是已确认故障。'],
['±10 ppm 的归属','为示波器外部参考输入可接受频率范围，不是 REF OUT 输出准确度。'],
['输入整形','板端使用 SN74LVC2G17 施密特触发器；迟滞抑制阈值附近重复翻转，但不能消除幅度/偏置/供电变化造成的过阈值时间变化。'],
['频率窗口','当前约一秒同极性边沿计数；固定过阈值延迟首尾基本抵消。需区分首尾抖动、漏周期、DMA 时间戳误差及补偿动态。'],
['独立精度','同台示波器供参考并测四路，不能据此证明参考自身绝对准确度；官网 HDO 链接现为 DHO 文档，不混用版本授予精度。']])) +
section('下一步的判别顺序', table(['顺序','动作','回答的问题'], [
['1','同接线：测量开启/补偿关闭，对比测量开启/补偿开启。','区分参考测量资源影响与调频注入暂态。'],
['2','核验施密特前后幅度、偏置与漏沿；复核频率窗口。','输入电气与计时噪声是否进入补偿。'],
['3','保留 100 ppb/s 基线；必要时延长候选收敛后再比较。','捕获速度、稳态相位与相干性如何取舍。'],
['4','外参十分钟、失联保持/恢复、跨启动复测。','持续性、回退及重新收敛。'],
['5','同事件、误差预算、质量老化与 VDC 发布。','正式 time / valid / quality / generation 一致性。']])) +
section('并行业务与证据索引', '<p class="reading">Trigger / T2 在 node-sequence 分支并行推进；用户确认三模式流程已实测跑通，当前压缩流程时间，部分代码待提交。360 位置扫描及主线集成边界详见同日期项目综述，不作为 DPLL 继续调试的前置。</p><div class="flow">20260918/dpll-extended-window-r1：第4–5页历史连续收敛/方波\n20260919/dpll-delay-center-r2/plus20-batched-600s：无外参长窗\n20260919/reference-discipline/profile-120s-100-r2、profile-120s-50-r1\n同目录 profile-comparison-r1/comparison.json、profile-flash-r2.json\n20260919/p3-reference-profile-r2：P3；build 20260919071411\n源码 SHA：d31246fd8b07f02a9e68c8680116e751420\ndaf37ac4de01a07ac013ccd55e3f2</div>') +
'<div class="bar">相干性收益与短期相位误差可以并存；<strong>原因以对照证据判定。</strong></div>')
pages[6].select_one('.page-footer span').string = '证据根：out/HardwareAcceptance/；VDC-TUNE-002 / VDC-FREQ-001 继续推进'

new_page = copy.deepcopy(pages[6])
heading(new_page, '外参补偿斜率：两分钟对比', '相同 PI / delay / 滤波 | 从零初值开始 | 每五秒同触发采样')
fill(new_page.select_one('.main'), note('<strong>只比较斜率：</strong>同固件、四板训练输入和输出 delay；参考均开启，IIR 分母为 4。每组 24 窗、每路每窗两个真实上升沿，图示窗口均值及范围。RUN 不查询板端，结束后核验实际参数 CRC、原生记录与恢复。') +
svg('out/HardwareAcceptance/20260919/reference-discipline/profile-comparison-r1/profile-comparison.svg', 137) +
section('第二分钟已采边沿峰峰值 / ns', table(['斜率','NO2 − NO1','NO3 − NO1','NO4 − NO1'], [['100 ppb/s','12.06','13.51','19.86'],['50 ppb/s','30.01','19.10','35.94']])) +
note('<strong>取舍：</strong>50 ppb/s 降低了全段波动，但末态滤波目标 4957 ppb、基线 4850 ppb，仍在限速追赶；100 组末态目标/基线均为 4852 ppb。这里是内部补偿量，不是实际输出剩余频差。单次顺序对比不能证明参考源抖动或 PI 振荡，默认保留 100 ppb/s。') +
'<p class="mini" style="margin-top:6px">采集保持 1M 点 / 2 ns，导出连续前 650000 点（约 1.3 ms），四路共用原时间轴，不拼接。两轮窗口间空档、绝对频率与跨启动因果归属未获资格。原始失败与通过记录分开保留。</p>')
soup.body.append(new_page)
for i, page in enumerate(soup.select('.page'), 1):
    page.select('.page-footer span')[-1].string = f'DHRT100 DPLL 阶段报告 | {i} / 8'
    for j, node in enumerate(page.select('.brand [id]')):
        node['id'] = f'brand-{i}-{j}'
    logo = page.select_one('.brand svg')
    logo['aria-labelledby'] = f'brand-{i}-0 brand-{i}-1'

out = ROOT / 'DHRT100_DPLL_STATUS_REPORT_20260919.html'
out.write_text(str(soup), encoding='utf-8')
print(out)
