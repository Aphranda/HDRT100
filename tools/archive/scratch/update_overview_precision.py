from pathlib import Path
import sys
sys.path.insert(0,str(Path('out/report-deps').resolve()))
from bs4 import BeautifulSoup
p=Path('docs/reports/distributed-trigger/DHRT100_PROJECT_OVERVIEW_REPORT_20260919.html')
s=BeautifulSoup(p.read_text(encoding='utf-8'),'html.parser')
page=s.select('.page')[0]
chips=page.select_one('.chips')
chips.clear()
for key,value in [('典型稳态表现','±25 ns · 现场观察量级'),('已采长窗范围','±50 ns · 无外参十分钟'),('总体研发进度','70.8% · 跨分支评估'),('验证规模','NO1–NO4 四板环路')]:
    node=s.new_tag('div',attrs={'class':'chip'})
    k=s.new_tag('span',attrs={'class':'k'});k.string=key
    v=s.new_tag('span',attrs={'class':'v'});v.string=value
    node.extend([k,v]);chips.append(node)
note=s.new_tag('p',attrs={'class':'mini','style':'margin-bottom:7px;'})
note.string='±25 ns 为近期无外参约 ±20 ns 现场观察的量级概述，非整轮统计保证；±50 ns 为无外参十分钟全部已采边沿范围，未覆盖采样空档。外参短窗及捕获暂态单独评价。'
chips.insert_after(note)
p.write_text(str(s),encoding='utf-8')
