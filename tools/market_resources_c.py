"""Lay out the native market commodity text and economy tables at their image offsets.

Covers CS:00A2h..06EAh: the FF marker, the commodity-name directory and
strings, every economy CSV table and the two NEAR dispatch tables. Dispatch
entries are handler offsets resolved from the symbolic table sources.
tools/market_tables_c.py slices typed C tables from this layout.
"""
import configparser
from pathlib import Path
import struct

from module_tables import document
from resource_sources import asset, settings
from string_resources import document as string_document

ROOT=Path(__file__).resolve().parents[1]
START,END=0xa2,0x6eb
# Handler entry offsets; the C dispatcher maps exactly these offsets.
HANDLERS={'generate_illegal_commodity_flag_record':0x12dc,'generate_market_flag_record_2':0x132b,
    'generate_market_flag_record_8':0x139f,'generate_record_from_system_history':0x10b2,
    'generate_record_from_random_seed':0x118e,'generate_record_from_candidate_system':0x11ac,
    'generate_market_flag_record_4':0x135e,'generate_variant_text_record':0x14e4,
    'generate_remote_system_record':0x13cd,'generate_random_text_record':0x14d0,
    'generate_request_variant_8':0xce9,'generate_request_variant_4':0xcf4,
    'generate_request_variant_4_with_threshold':0xd0d,'generate_assassinate_request':0xbb0,
    'generate_photograph_request':0xc19,'generate_destroy_request':0xc81}
DISPATCH=((0x3a1,'market_record_dispatch.csv',32),(0x59b,'system_record_dispatch_table.csv',16))


def dispatch_table(data, name, count):
    from data_tables import dispatch_words
    return dispatch_words(Path(data) / 'economy' / name, 'slot', count, HANDLERS)


def resources(data_root=None):
    root = Path(data_root) if data_root else ROOT / 'data'
    image=bytearray(END-START)
    image[0]=0xff
    catalog = root / 'text/market/catalog.ini'
    text = string_document(settings(catalog), catalog, asset)
    entries=[bytes(e['bytes'])+b'\0' for e in text['entries']]
    relative=2*len(entries); offsets=[]
    for entry in entries: offsets.append(relative); relative+=len(entry)
    blob=struct.pack(f'<{len(offsets)}H',*offsets)+b''.join(entries)
    if text['directory_offset']!=0xa3 or 0xa3+len(blob)!=text['end_offset']: raise ValueError('market text layout changed')
    image[0xa3-START:0xa3-START+len(blob)]=blob
    filled=[(0xa2,0xa3+len(blob))]
    source=root/'economy/tables.ini'
    config=configparser.ConfigParser(); config.read(source,encoding='ascii')
    for table in document(config,source,asset,None)['tables']:
        table_bytes=b''.join(v.to_bytes(table['width'],'little') for v in table['values'])
        image[table['start']-START:table['end']-START]=table_bytes
        filled.append((table['start'],table['end']))
    for offset,name,count in DISPATCH:
        image[offset-START:offset-START+2*count]=dispatch_table(root,name,count)
        filled.append((offset,offset+2*count))
    filled.sort()
    if filled[0][0]!=START or filled[-1][1]!=END or any(a[1]!=b[0] for a,b in zip(filled,filled[1:])):
        raise ValueError('market resources must cover 00A2h..06EAh exactly')
    return bytes(image)
