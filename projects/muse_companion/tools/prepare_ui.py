"""Author the native GSP scene; runtime text and controls remain native widgets."""
import json
import math
import argparse
import hashlib
import os
from pathlib import Path
from PIL import Image, ImageDraw

root = Path(__file__).resolve().parents[1]
objects = []
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--character',help='Checked character directory, or original to restore the plush images')
parser.add_argument('--animation-quality',type=int,choices=range(1,96),metavar='1..95',
                    help='JPEG animation quality; stored in the selected UI profile')
args=parser.parse_args()
profile=root/'ui/character_profile.json'
saved=json.loads(profile.read_text(encoding='utf-8')) if profile.exists() else {}
animation_quality=args.animation_quality if args.animation_quality is not None else saved.get('animation_quality',65)
if args.character:
    selected=None if args.character=='original' else str(Path(args.character).resolve())
else:selected=saved.get('directory')
character=selected
if character:
    character=Path(character)
    if not character.is_absolute():character=root/character
    validation=json.loads((character/'preview-validation.json').read_text(encoding='utf-8'))
    if validation['source_sha256']!=hashlib.sha256((character/'muse_character.c').read_bytes()).hexdigest():
        raise RuntimeError('Character changed since its preview was checked')
    for state in ('idle','listening','thinking','speaking','sleepy','error','happy','boot'):
        asset=character/'animations'/f'{state}.gif'
        if not asset.is_file():raise RuntimeError('Missing animation: '+state)
        if validation['animations'][state]['gif_sha256']!=hashlib.sha256(asset.read_bytes()).hexdigest():
            raise RuntimeError('Animation changed since preview: '+state)
if args.character or args.animation_quality is not None:
    if character:
        selected=os.path.relpath(character,root).replace('\\','/')
    profile.write_text(json.dumps({'directory':selected,'animation_quality':animation_quality},indent=2),encoding='utf-8')

def make_icons():
    """Small code-authored icons, supersampled for the device's native UI."""
    size, scale = 32, 4
    ink = '#4E392E'
    for name in ('speaker', 'muted', 'gear', 'heart'):
        image = Image.new('RGBA', (size * scale, size * scale))
        draw = ImageDraw.Draw(image)
        def pts(values):
            return [(round(x * scale), round(y * scale)) for x, y in values]
        if name == 'heart':
            draw.ellipse((3*scale,4*scale,18*scale,19*scale),fill='#FFB4C6')
            draw.ellipse((14*scale,4*scale,29*scale,19*scale),fill='#FFB4C6')
            draw.polygon(pts([(3,13),(29,13),(16,29)]),fill='#FFB4C6')
        elif name == 'gear':
            teeth = []
            for i in range(64):
                angle = math.tau * i / 64
                radius = 14 if i % 8 in (1, 2, 3, 4) else 11
                teeth.append((16 + radius * math.cos(angle), 16 + radius * math.sin(angle)))
            draw.polygon(pts(teeth), fill=ink)
            draw.ellipse((11*scale,11*scale,21*scale,21*scale), fill=(0,0,0,0))
        else:
            draw.polygon(pts([(3,12),(9,12),(16,6),(16,26),(9,20),(3,20)]), fill=ink)
            if name == 'speaker':
                draw.arc((12*scale,8*scale,25*scale,24*scale), -60, 60, fill=ink, width=2*scale)
                draw.arc((10*scale,3*scale,31*scale,29*scale), -60, 60, fill=ink, width=2*scale)
            else:
                draw.line(pts([(21,11),(29,21)]), fill='#AD554B', width=3*scale)
                draw.line(pts([(29,11),(21,21)]), fill='#AD554B', width=3*scale)
        image.resize((size,size), Image.Resampling.LANCZOS).save(root/'ui'/'assets'/f'icon_{name}.png')

make_icons()
def add(kind, x, y, w, h, parent=-1, **fields):
    item = dict(type=kind, parent=parent, x=x, y=y, w=w, h=h, **fields)
    objects.append(item)
    return len(objects) - 1

def bind(name, target='visible', **fields):
    return dict(name=name, bind=name, bind_target=target, **fields)

def label(text, x, y, w, h, parent=-1, **fields):
    return add('label', x, y, w, h, parent, text=text,
               **dict({'font_size':20,'text_align':'center','fg_color':'#FFF3EA'}, **fields))

def button(text, name, x, y, w, h, parent=-1, color='#D0EBDD', **fields):
    return add('button', x, y, w, h, parent, text=text, name=name,
               callback=name, font_size=18, fg_color='#4E392E',
               bg_color=color, radius=min(h//2,20), **fields)

home = add('container', 0, 0, 480, 480, bg_color='#000000', **bind('home'))
add('button',0,0,480,480,home,bg_color='#00000000',name='wake',callback='wake',text='',fg_color='#000000')
label('正在连接', 24, 18, 160, 32, home, text_align='left', **bind('connection','text'))
# iPhone-style rounded battery body, capacity fill and terminal.
add('container',394,21,62,28,home,bg_color='#FFF3EA',radius=8)
add('container',458,30,4,10,home,bg_color='#AAA6A2',radius=2)
add('progress',397,24,56,22,home,bg_color='#747474',fg_color='#FFF3EA',
    radius=5,value=0,**bind('battery_fill','value'))
label('--',397,24,56,22,home,font_size=16,fg_color='#211C19',**bind('battery','text'))
for name in ['idle','listening','thinking','speaking','sleepy','error','happy','boot']:
    x={'property':'x','default':90,'min':82,'max':98} if not character else 90
    y={'property':'y','default':75,'min':59,'max':81} if not character else 75
    if character:
        image=os.path.relpath(character/'animations'/f'{name}.gif',root/'ui').replace('\\','/')
    else:image=f'assets/{"speaking" if name=="happy" else "idle" if name=="boot" else name}.png'
    add('image', x, y, 300, 300, home, image=image,
        fit='stretch' if character else 'contain', store_scale=1.0 if character else 0.54, codec='lossless',
        animation_codec='jpeg' if character else 'lossless',quality=animation_quality,max_fps=8,hidden=name!='idle', **bind(name))
add('button',130,105,220,240,home,bg_color='#00000000',fg_color='#000000',text='',name='pet',callback='pet')
for i,x in enumerate((154,300)):
    add('image',x,{'property':'y','default':105,'min':75,'max':110},30,30,home,
        image='assets/icon_heart.png',fit='contain',codec='lossless',hidden=True,**bind(f'heart_{i}'))
for i,x in enumerate((216,240,264)):
    add('container',x,100,8,8,home,bg_color='#FFE1D0',radius=4,hidden=True,**bind(f'thought_{i}'))
# Opaque animation canvases must draw before controls at the top right.
button('', 'mute', 414, 58, 48, 44, home,bind='mute',bind_target='visible')
button('', 'settings_open', 414, 58, 48, 44, home,color='#FFE1D0',hidden=True,bind='settings_open',bind_target='visible')
for name, x in [('speaker',422),('muted',422),('gear',422)]:
    add('image',x,64,32,32,home,image=f'assets/icon_{name}.png',
        fit='contain',codec='lossless',hidden=name in ('muted','gear'),**bind(f'icon_{name}'))
for i,h in enumerate([12,22,32,18,28,38,28,18,32,22,12]):
    add('container',158+i*15,340-h//2,6,h,home,
        bg_color='#B6E6CE',radius=3,hidden=True,**bind(f'wave_{i}'))
add('container',18,365,444,70,home,bg_color='#FFE1D0',radius=22,
    hidden=True,**bind('reply_card'))
label('我在，想聊点什么？',24,375,432,51,home,**bind('caption','text'))
label('',24,373,432,26,home,fg_color='#4E392E',**bind('reply_text','text'))
label('',24,400,432,26,home,fg_color='#4E392E',**bind('reply_text_2','text'))
add('container',18,190,444,245,home,bg_color='#FFE1D0',radius=22,
    hidden=True,**bind('reply_expanded_card'))
for row in range(8):
    label('',52,200+26*row,376,26,home,fg_color='#4E392E',hidden=True,text_align='left',
          **bind(f'reply_expanded_{row}','text'))
label('按住顶部 AI 键说话',40,440,400,28,home,**bind('hint','text'))
button('取消','cancel',20,300,84,40,home,color='#FFE1D0',hidden=True,**{'bind':'cancel','bind_target':'text'})
button('‹','page_prev',20,440,42,36,home,hidden=True,**{'bind':'page_prev','bind_target':'visible'})
button('›','page_next',418,440,42,36,home,hidden=True,**{'bind':'page_next','bind_target':'visible'})
button('重试朗读','retry',174,308,132,38,home,hidden=True,**{'bind':'retry','bind_target':'text'})
# Invisible-on-device test controls are not needed: the real prompt label is
# a hold-to-talk touch alternative and exercises the same model as the AI key.
add('button',80,435,320,40,home,bg_color='#000000',radius=12,
    name='talk',text='',fg_color='#000000',events=[
        {'event':'press','action':'call','target_name':'ai_press'},
        {'event':'release','action':'call','target_name':'ai_release'}])
# Draw the hint last so it stays visible above the touch button.
label('按住顶部 AI 键说话',40,440,400,28,home,**bind('talk_hint','text'))
settings=add('container',0,0,480,480,bg_color='#000000',**bind('settings'))
label('伙伴设置',130,24,220,40,settings,**bind('title','text'))
button('','settings_back',18,24,86,40,settings,color='#FFE1D0',hidden=True)
label('',18,24,86,40,settings,fg_color='#4E392E',hidden=True,**bind('back_label','text'))
for name,title,value,y,color in [('volume','音量 55%',55,88,'#B6E6CE'),('brightness','亮度 70%',70,140,'#FFD6C0')]:
    label(title,24,y,150,34,settings,text_align='left',font_size=18,**bind(name+'_text','text'))
    add('slider',190,y,256,34,settings,min=0 if name=='volume' else 5,max=100,value=value,
        track_size=10,bg_color='#252525',fg_color=color,knob_color='#FFF3EA',radius=16,
        name=name,bind=name,bind_target='value',events=[
            {'event':'value','action':'call','target_name':name+'_preview'},
            {'event':'release','action':'call','target_name':name+'_change'}])
label('语言',24,192,150,36,settings,text_align='left',font_size=18,**bind('language_text','text'))
button('','language_change',190,192,256,38,settings)
label('中文',190,192,256,38,settings,fg_color='#4E392E',font_size=18,**bind('language_value','text'))
for name,title,lo,hi,value,y,color in [
    ('sleep','自动熄屏 15秒',0,5,2,244,'#FFD6C0'),
    ('rate','语速 1.00x',75,150,100,296,'#B6E6CE'),
    ('page','翻页 7秒',3,15,7,348,'#FFD6C0')]:
    label(title,24,y,150,34,settings,text_align='left',font_size=18,**bind(name+'_text','text'))
    add('slider',190,y,256,34,settings,min=lo,max=hi,value=value,
        track_size=10,bg_color='#252525',fg_color=color,knob_color='#FFF3EA',radius=16,
        name=name,bind=name,bind_target='value',events=[
            {'event':'value','action':'call','target_name':name+'_preview'},
            {'event':'release','action':'call','target_name':name+'_change'}])
label('网络 已连接 · Muse 已配对',18,416,444,30,settings,font_size=18,**bind('pair_status','text'))
button('手机配置','phone_setup',174,448,132,30,settings,color='#D0EBDD',bind='phone_setup',bind_target='text')
charset=''.join(sorted(set(''.join(p.read_text(encoding='utf-8') for p in (root/'main').glob('*.c')))))
for item in objects:
    if item.get('bind_target')=='text': item['font_charset']=charset
common=dict(w=480,h=480,screen_bg='#000000',font='fonts/NotoSansSC-Regular.ttf',default_font_size=20,swipe=False)
home_objects=objects[1:settings]
for item in home_objects: item['parent']=-1
# Resource-owned playback mode. Append so existing binding indices are stable.
# Progress state uses its native 0..100 rail: 0=static, 100=animated.
home_objects.append(dict(type='progress',parent=-1,x=0,y=0,w=1,h=1,
    min=0,max=100,value=100 if character else 0,
    bg_color='#000000',fg_color='#000000',
    **bind('character_animated','value')))
settings_objects=objects[settings:]
for item in settings_objects:
    if item['parent']>=settings: item['parent']-=settings
(root/'ui'/'main.json').write_text(json.dumps(dict(common,screen='companion',objects=home_objects),ensure_ascii=False,indent=2),encoding='utf-8')
(root/'ui'/'settings.json').write_text(json.dumps(dict(common,screen='companion_settings',objects=settings_objects),ensure_ascii=False,indent=2),encoding='utf-8')
print('Native 480x480 companion scene authored')
