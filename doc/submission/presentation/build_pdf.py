"""Create the viewing PDF from the inspected renders of the final PPTX.

Text and diagram editing belongs in the PPTX. Images in the viewing PDF keep
the Japanese typography and arrows identical across PDF viewers.
"""
import json
import os
from pathlib import Path
from reportlab.pdfgen import canvas
from reportlab.lib.utils import ImageReader
from pypdf import PdfReader

root = Path(__file__).resolve().parents[3]
build = Path(os.environ.get('PRESENTATION_BUILD', root / 'tmp/tron-presentation'))
output = Path(__file__).parent / 'MimamoriSense-TRON2026.pdf'
slides = json.loads((build / 'slides.json').read_text(encoding='utf-8'))
c = canvas.Canvas(str(output), pagesize=(960, 540), pageCompression=1)
c.setTitle('MimamoriSense - TRONプログラミングコンテスト2026 応募プログラム紹介資料')
c.setSubject('家庭内見守り端末の概要、処理フロー、状態遷移、実機表示、μT-Kernel構成')
for i, slide in enumerate(slides, 1):
    c.drawImage(ImageReader(str(build / f'final-{i:02}.png')), 0, 0, width=960, height=540)
    c.bookmarkPage(f'slide-{i}')
    c.addOutlineEntry(slide['title'], f'slide-{i}')
    if i == len(slides):
        c.linkURL('https://github.com/grace2riku/mimamori-sense', (48, 52, 876, 84), relative=0)
    c.showPage()
c.save()
assert len(PdfReader(output).pages) == 10
print(f'PDF: {output} ({output.stat().st_size:,} bytes, 10 pages)')
