"""Verify FSP generation completed; never edits generated files or hardware."""
from pathlib import Path
import ast
import operator
import re
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2] / 'e2studio_CPU0'

def integer(node):
    if isinstance(node, ast.Constant) and type(node.value) is int:
        return node.value
    if isinstance(node, ast.BinOp) and type(node.op) in (ast.BitOr, ast.LShift):
        op = {ast.BitOr: operator.or_, ast.LShift: operator.lshift}[type(node.op)]
        return op(integer(node.left), integer(node.right))
    raise ValueError('Unexpected generated integer expression')

def verify():
    props = {p.attrib['id']: p.attrib.get('value')
             for p in ET.parse(ROOT / 'configuration.xml').iter('property')}
    prefix = 'config.bsp.option_setting.ofs1_sec.'
    for name, value in [('voltage_detection0.start', 'enabled'),
                        ('voltage_detection0_level', '285')]:
        assert props[prefix + name] == prefix + name + '.' + value
    header = (ROOT / 'ra_cfg/fsp_cfg/bsp/bsp_mcu_ofs_cfg.h').read_text()
    expression = re.search(r'^#define BSP_CFG_OPTION_SETTING_OFS1_SEC_NO_HOCOFRQ (.+)$',
                           header, re.M).group(1)
    value = integer(ast.parse(expression.strip(), mode='eval').body)
    assert value & 0xF == 0, 'FSP generation pending: use Generate Project Content in e2 studio'
    print('PASS FSP XML and generated OFS1_SEC: PVDAS=0, VDSEL0=000')

if __name__ == '__main__':
    verify()
