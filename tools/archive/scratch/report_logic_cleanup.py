from pathlib import Path
import sys
sys.path.insert(0,str(Path('out/report-deps').resolve()))
from bs4 import BeautifulSoup
p=Path('docs/reports/distributed-trigger/DHRT100_PROJECT_OVERVIEW_REPORT_20260919.html')
s=BeautifulSoup(p.read_text(encoding='utf-8'),'html.parser')
for node in list(s.find_all(string=True)):
    if '简单 OLS 斜率标准误差' in str(node):
        node.parent.decompose()
        break
for node in list(s.find_all(string=True)):
    if node.parent.name in ('style','script'):continue
    t=str(node).replace('十九、三类 delay 必须分开','十九、延迟与调度参数分别维护')
    t=t.replace('转台完成 360 位置完整扫描。','单板模拟反馈完成 360 个角度位置序列。')
    t=t.replace('稳定已有百纳秒闭环，扩大 ±50 ns 结果的适用范围，优化外参捕获与保持恢复，并补齐 VDC 正式发布时间质量。','在已采 ±50 ns 结果上，推进稳定 ±25 ns 目标，优化外参捕获与保持恢复，并补齐 VDC 正式发布时间质量。')
    t=t.replace('稀疏相位用于长期稳定性评价，连线不代表区间内逐边沿覆盖。','内部残差包络与实际引脚相差分别观测，不能直接等同；稀疏窗口间不代表逐边沿覆盖。')
    if t!=str(node):node.replace_with(t)
p.write_text(str(s),encoding='utf-8')
