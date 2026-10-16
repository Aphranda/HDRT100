from pathlib import Path
import sys
sys.path.insert(0, str(Path('out/report-deps').resolve()))
from bs4 import BeautifulSoup

path=Path('docs/reports/distributed-trigger/DHRT100_PROJECT_OVERVIEW_REPORT_20260919.html')
s=BeautifulSoup(path.read_text(encoding='utf-8'),'html.parser')
pages=s.select('.page')
def fill(node,html):
    node.clear()
    node.extend(list(BeautifulSoup(html,'html.parser').contents))
def table(head,rows):
    return '<table class="compact"><thead><tr>'+''.join('<th>'+x+'</th>' for x in head)+'</tr></thead><tbody>'+''.join('<tr>'+''.join('<td>'+x+'</td>' for x in row)+'</tr>' for row in rows)+'</tbody></table>'
def section(title,html):
    return '<div class="section"><div class="section-title">'+title+'</div>'+html+'</div>'
def named(page,title):
    return next(x for x in page.select('.section') if x.select_one('.section-title') and title in x.select_one('.section-title').get_text())

fill(named(pages[1],'外部参考'),section('外部参考与共同基准','<div class="note-box">外部 10 MHz 经板端输入整形进入 NO1，慢速修正其时间速率；NO2–NO4 通过确定性时间戳本地跟随。该路径为提高对外部基准的频率一致性和长期相干性提供基础。参数配置与保存已验证；短期边沿精度、绝对准确度及失联恢复分别评价。</div>'))
fill(named(pages[4],'十六、'),section('十六、长稳结论与范围',table(['已验证','后续补齐'],[
['无外参连续运行十分钟；120 个外部窗口和四板内部健康检查通过。','跨启动、失联恢复、未采空档的连续性及测量误差预算。'],
['全部已采边沿落在 ±50 ns 内，稀疏相位与同轮频差趋势相互支持。','外参补偿后的十分钟专项尚未完成，不能沿用无外参结果。']])) )
fill(named(pages[4],'十七、'),section('十七、如何理解频差','<p class="reading">表中 ppb 由同轮相差随时间的趋势拟合，包含闭环相位校正，描述相对 NO1 的净漂移；它不是晶振绝对误差或瞬时频偏上限。稀疏采样支持已观测窗口的长期稳定性判断，不等于逐脉冲完整覆盖。</p><div class="source">测量方法、原件、失败记录及边界见 <a href="DHRT100_DPLL_STATUS_REPORT_20260919.html">DPLL 阶段报告</a>与 VDC 任务进度。</div>'))
for src in pages[4].select('.source'):
    if 'same-trial' in src.get_text():
        src.decompose()

pages[5].select_one('h1').string='精度基础与演进方向'
pages[5].select_one('.subtitle').string='晶振容差、闭环精度与外部相干性分别评价'
fill(pages[5].select_one('.main'),
section('十八、为什么普通晶振也能实现高精度同步', '<div class="note-box">晶振 ±30 ppm 描述自由运行频率容差。DPLL 比较共同事件的时间戳，持续估计本地频差与相位偏移，再由硬件按修正后的时间执行输出。因此，原始晶振仍自由运行，板间输出却可以保持远小于其自由漂移的相对误差。</div>'+table(['层次','作用','当前阶段'],[
['统一时间底座','250 MHz / 4 ns 计时；本地模型把各板映射到共同时间。','四板实际闭环已生效。'],
['确定性传输与执行','TDMA 快速通道保全同步记录，PIO/DMA 兑现未来边沿。','持续输出和长窗观测已有证据。'],
['校准与输出补偿','路径校准处理传播与端点偏差；输出补偿居中固定边沿差。','两类参数分开维护，正式误差预算继续完善。'],
['共同外部参考','NO1 跟踪 10 MHz 基准，三从跟随 NO1。','外参测量、慢速补偿及参数持久化已验证。']]))+
section('十九、相干性与短期精度',table(['评审维度','当前判断'],[
['板间相位','无外参十分钟已采窗口在 ±50 ns 内；近期现场可观察到约 ±20 ns，工况与窗口应分别注明。'],
['外部相干性','共同频率基准有助于降低相对外部系统的长期漂移，不自动等于外参绝对相位锁定。'],
['接入后的暂态','外参短窗验证通过；现场约 ±50 ns 的波动和调频捕获暂态继续优化，不能直接归因为参考源不稳。'],
['精度声明','4 ns 是计时节拍；正式板间精度还需校准、通道误差、连续性与跨启动验证。']]))+
section('二十、后续精度空间','<div class="two-col"><div class="coord-box"><strong>当前平台：</strong>稳定已有百纳秒闭环，扩大 ±50 ns 结果的适用范围，优化外参捕获与保持恢复，并补齐 VDC 正式发布时间质量。</div><div class="todo-box"><strong>更高性能平台：</strong>FPGA 可硬件化匹配、PI/DPLL 和定时输出，结合低抖动时钟与时间细分探索 ps 量级；目标精度由器件、校准与完整误差预算决定。</div></div>')+
'<div class="bar">相对锁相、外部相干性、绝对准确度是<strong>互相关联但不同的指标</strong>。</div><div class="source">详细曲线、频差估计、外参参数对照与电气分析见同日期 DPLL 阶段报告。全部结果为当前实验配置快照。</div>')

pages[6].select_one('h1').string='研发进度与下一阶段'
pages[6].select_one('.subtitle').string='四板共同时间与单板业务并行推进，随后完成集成验证'
fill(pages[6].select_one('.main'),
section('二十一、两条主线的当前成果',table(['方向','已完成的阶段成果','当前重点'],[
['DPLL / VDC','四板本地闭环、无外参十分钟观测、外参补偿与调参保存。','外参长稳、失联恢复、时间质量一致发布。'],
['Trigger / T2','node-sequence 三模式单板流程已实测跑通；转台完成 360 位置完整扫描。','压缩流程时间，再与四板共同时间集成。'],
['交付状态','既有实现已提交；部分单板新增实现已实测但未提交。','分别核对源码提交、分支集成和系统验收。']]))+
section('二十二、下一阶段交付目标',table(['工作包','可评审的完成结果'],[
['稳定时间服务','持续跟随、参考失联与恢复有明确行为；各层时间、有效性和质量一致。'],
['业务流程提速','保持三模式功能正确，压缩每位置完整流程并以统一计时和实际边沿验证。'],
['多板业务集成','共同时间预约、准入、硬件执行与实际完成时间 T2_actual 形成完整业务链。'],
['产品级确认','补齐跨启动、校准、实时预算和资源验证，按固定范围逐切片验收。']]))+
section('二十三、进度百分比依据',table(['主域','已得分 / 四项里程碑','研发进度'],[
['HAOFV / 双核','3.5 / 4：所有权、双核、治理已落地，完整资源预算部分完成。','87.5%'],
['TDMA','3 / 4：运输与周期配置完成；特等席全链路及逐圈期限部分完成。','75.0%'],
['VDC / DPLL / SYNC','3 / 4：本地闭环、硬件长窗、外参补偿完成；恢复及正式资格未完成。','75.0%'],
['RefMem','3 / 4：基础表、缩容、发布版本完成；业务 completion/fence 待闭合。','75.0%'],
['Calibration','2.5 / 4：拓扑与矩阵已用；有向路径部分完成，正式校准待闭合。','62.5%'],
['Trigger / T2','2 / 4：设计与单板流程已落地；多板预约、T2_actual 端到端待集成。','50.0%']]))+
'<p class="mini" style="margin-top:7px">完成/部分/未完成计 1/0.5/0；六域合计 17/24，整体为 <strong>70.8%</strong>。这是跨分支研发评估，不是主线合并率或工期比例。外参纳入本版里程碑后 VDC 计分为 3/4；DPLL 专报以独立 6/8 项计 75%，两者分母不同。</p>'+
'<div class="bar">当前重点：<strong>稳定共同时间、压缩业务流程、完成多板集成。</strong></div><div class="source">DPLL 详见 <a href="DHRT100_DPLL_STATUS_REPORT_20260919.html">阶段报告</a>；任务状态以 VDC / TDMA / Trigger 域文档为准。单板进展依据 node-sequence 分支截至 2026-09-19 的记录及用户确认，未将待提交内容视为主线已集成。</div>')
for old in pages[7:]:
    old.decompose()
for i,p in enumerate(s.select('.page'),1):
    p.select('.page-footer span')[-1].string=f'DHRT100 项目综述报告 | {i} / 7'
for node in list(s.find_all(string=True)):
    if node.parent.name in ('style','script'):
        continue
    t=str(node).replace('（第 8 页）','（第 6 页）').replace('见第 8 页','见第 6 页').replace('见第 9 页','见第 7 页')
    t=t.replace('本报告 ppb 相对 NO1，且包含相位校正。','输出趋势 ppb 相对 NO1，且包含相位校正。')
    if t!=str(node):node.replace_with(t)
path.write_text(str(s),encoding='utf-8')
print('overview condensed to',len(s.select('.page')),'pages')
