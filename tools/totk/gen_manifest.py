#!/usr/bin/env python3
"""Generates packages/TearsOfTheKingdom/dualscreen/manifest.json: the Tears of the Kingdom pages.

The main page is TotK Native's companion main page (P1R4N351/botw-android,
android/java/com/aure/totk/companion/CompanionView.java ready(), MIT), which is itself laid out on
Breath of the Wild's Ready grid: the same 1240x1080 rectangles, colours and wording. Every
picture is a module:totk: key, decoded at runtime from the player's own romfs by the native
module (native/modules/0100F2C0115B6000.cpp) or drawn by it from flat geometry (gen/).

    gen_manifest.py [out]
"""
import json
import os
import sys

OUT = os.path.join(os.path.dirname(__file__), '../../packages/TearsOfTheKingdom/dualscreen/manifest.json')
VERSION = '0.1.0'
BUILD_143 = '277178B7DBA1B6D4949A84778D4ABC58B31B34F5000000000000000000000000'   # 1.4.3 main's module ID
FONT = 'ninP_RodinNTLG-M_DH_003_subset:20'   # the game's Rodin, cap height 20 px = text_scale 4 at 1:1
M = 'module:totk:'

# TotK Native's palette (CompanionView.java)
INK, MUTED, GOLD, GREEN = '#FFE9E5D5', '#FFAAA997', '#FFDCC08A', '#FF77D7B1'
TEAL, TEAL_TEXT, RED = '#FF00FFC3', '#FFA8FBE6', '#FFEC4A34'
PANE, DISC, PAGE = '#7D000000', '#FF1B1A14', '#FF15140F'
CARD, CARD_LINE = '#D0121110', '#50E9E5D5'

CAP = {3: 15, 4: 20, 5: 25, 6: 30}


def scale(px):
    """A Java text size (font px) as the nearest whole text_scale (cap height 5 px per step)."""
    cap = px * 0.72
    return min(CAP, key=lambda s: abs(CAP[s] - cap))


def lyt(texture):
    return f'{M}lyt/{texture}'


def label(x, baseline, text=None, size=24, color=INK, align='center', width=0, bind=None, **kw):
    """A label placed like the Java page's label(): x is the centre (or the left edge), y the baseline."""
    s = scale(size)
    h = CAP[s] + 6
    w = width or 400
    # Eden Duo anchors a label's text on the rect's x: its left edge, centre or right edge by align.
    out = {'type': 'label', 'rect': [round(x), round(baseline - CAP[s]), round(w), h], 'text_scale': s,
           'color': color, 'align': align}
    if text is not None:
        out['text'] = text
    if bind:
        out['bind_text'] = bind
    if width:
        out['wrap_width'] = round(w)
        out['max_lines'] = 1
        out['fit_text'] = True
        out['text_min_scale'] = max(1, s - 1)
    out.update(kw)
    return out


def image(x0, y0, x1, y1, src=None, bind=None, **kw):
    out = {'type': 'image', 'rect': [round(x0), round(y0), round(x1 - x0), round(y1 - y0)]}
    if src:
        out['src'] = src
    if bind:
        out['src_bind'] = bind
    out.update(kw)
    return out


def rect(x0, y0, x1, y1, fill, line=None, frame=2, **kw):
    out = {'type': 'rect', 'rect': [round(x0), round(y0), round(x1 - x0), round(y1 - y0)], 'bg': fill}
    if line:
        out['color'] = line
        out['frame'] = frame
    else:
        out['frame'] = 0
    out.update(kw)
    return out


def disc(cx, cy, r, color, **kw):
    d = round(2 * r)
    return image(cx - r, cy - r, cx - r + d, cy - r + d, f'{M}gen/circle/{d}/{color[1:]}', **kw)


def bar(x, y, w, bind, max_bind, color=INK, **kw):
    out = {'type': 'bar', 'rect': [x, y, w, 8], 'bind': bind, 'max_bind': max_bind, 'color': color,
           'bg': '#40E9E5D5', 'frame': 0}
    out.update(kw)
    return out


def tiles(prefix):
    """The 12x10 MainField tiles as map overlays: tile (c, r) covers x -6000+1000c.., z -5000+1000r..
    (row 0 is north); map y is north-up (-z)."""
    return [{'src_bind': f'{prefix}.{r * 12 + c}', 'x0': -6000 + 1000 * c, 'x1': -5000 + 1000 * c,
             'y0': 4000 - 1000 * r, 'y1': 5000 - 1000 * r} for r in range(10) for c in range(12)]


def command_bar(widgets, gate=None):
    for i, (text, action) in enumerate([('Inventory', 'page_inventory'), ('Map', 'page_map'),
                                        ('- Map', 'press_minus'), ('+ Menu', 'press_plus')]):
        x = 252 + i * 188
        extra = {'need_bind': gate} if gate else {}
        widgets.append(rect(x, 956, x + 172, 1060, PAGE, '#90E9E5D5' if i < 2 else CARD_LINE, on_tap=action, **extra))
        widgets.append(label(x + 86, 1019, text, 30, INK if i < 2 else MUTED, **extra))


def gear_card(widgets, x, y, slot, empty, empty_icon, bar_top):
    cx, top = x + 92, y + (34 if bar_top else 16)
    g = f'totk.gear.{slot}'
    widgets.append(rect(x, y, x + 184, y + 192, CARD, CARD_LINE, on_tap='page_inventory'))
    widgets.append(image(cx - 32, y + 50, cx + 32, y + 114, lyt(empty_icon), need_bind=f'{g}.none', tint='#60FFFFFF'))
    widgets.append(label(cx, y + 156, empty, 24, MUTED, need_bind=f'{g}.none'))
    widgets.append(image(cx - 44, top, cx + 44, top + 88, bind=f'{g}.icon', need_bind=f'{g}.has', src_hold=False))
    widgets.append(bar(x + 16, y + 16 if bar_top else y + 172, 152, f'{g}.life', f'{g}.life_max',
                       need_bind=f'{g}.ok'))
    widgets.append(bar(x + 16, y + 16 if bar_top else y + 172, 152, f'{g}.life', f'{g}.life_max', RED,
                       need_bind=f'{g}.breaking'))
    if slot == 'weapon':
        widgets.append(label(cx, top + 122, 'Breaking!', 28, '#FFFF4530', need_bind=f'{g}.breaking'))
        widgets.append(label(cx, top + 116, None, 22, INK, width=168, bind=f'{g}.name', need_bind=f'{g}.ok'))
    else:
        widgets.append(label(cx, top + 116, None, 22, INK, width=168, bind=f'{g}.name', need_bind=f'{g}.has'))


def dial(widgets, cx, cy, icon, value_bind=None, extra_bind=None, value=None, **kw):
    widgets.append(disc(cx, cy, 41, DISC))
    widgets.append(image(cx - 28, cy - 28, cx + 28, cy + 28, icon, **kw))
    widgets.append(label(cx, cy + 76, value, 34, INK, bind=value_bind))
    if extra_bind:
        widgets.append(label(cx, cy + 104, None, 24, GOLD, bind=extra_bind))


def ready_page():
    w = [rect(0, 0, 1240, 1080, PAGE)]
    # the minimap: a disc in the middle, Link at its centre, north up
    w.append(disc(620, 464, 404, '#FF12110C'))
    w.append({'type': 'map', 'id': 'totk_minimap', 'area': 'TotkMini', 'rect': [220, 64, 800, 800],
              'area_label': False, 'follow_window': 2560, 'need_bind': 'totk.map.valid',
              'marker_x_bind': 'totk.map.x', 'marker_y_bind': 'totk.map.y',
              'marker_src': lyt('Nt_MapPlayerIconNew_00^s'), 'marker_size': [72, 72],
              'marker_rotate_bind': 'totk.map.heading', 'overlays': tiles('totk.mini.tile'),
              'on_map_tap': 'page_map'})
    w.append(image(212, 56, 1028, 872, f'{M}gen/disc/816/{PAGE[1:]}'))   # rounds it, with the cream ring
    w.append(disc(620, 66, 28, DISC))
    w.append(label(620, 77, 'N', 30, INK))
    # four dials round the map
    dial(w, 300, 82, lyt('NumDisplay_82x82_MinusRupee^w'), 'totk.poes_text')
    dial(w, 940, 82, lyt('BonusIcon72_Ganbari_00^s'), 'totk.stamina', 'totk.stamina_extra')
    dial(w, 300, 829, lyt('MapIconDungeon_L_Icon^s'), value='—', need_bind='totk.surface_or_sky')
    w.append(image(272, 801, 328, 857, lyt('LoadArrivalPoint_00^w'), need_bind='totk.depths'))
    dial(w, 940, 829, lyt('EnergyGaugeBigBaseFrame_00^s'), 'totk.energy', 'totk.energy_extra')
    # the gear in the corners
    gear_card(w, 16, 16, 'shield', 'Shield', 'IconShield_72x72^s', True)
    gear_card(w, 1040, 16, 'bow', 'Bow', 'IconBow_72x72^s', True)
    gear_card(w, 1040, 872, 'weapon', 'Weapon', 'IconWeapon_72x72^s', False)
    w.append(rect(16, 872, 200, 1064, CARD, CARD_LINE))
    w.append(image(60, 900, 156, 996, bind='totk.ability.icon', need_bind='totk.ability.has'))
    w.append(label(108, 1036, None, 26, INK, width=168, bind='totk.ability.name', need_bind='totk.ability.has'))
    # the arrows under the bow, the weapon's fused material above the weapon
    for cy, up in ((300, True), (776, False)):
        w.append(rect(1131, cy - 92 if up else cy + 72, 1134, cy - 72 if up else cy + 96, CARD_LINE))
        w.append(disc(1132, cy, 72, '#E01B1A14'))
    w.append(image(1092, 244, 1172, 324, bind='totk.arrow.icon', need_bind='totk.arrow.has'))
    w.append(label(1132, 358, None, 30, INK, bind='totk.arrow.count', need_bind='totk.arrow.has'))
    w.append(label(1132, 310, 'No arrows', 22, MUTED, need_bind='totk.arrow.none'))
    w.append(label(1132, 734, 'Fuse', 20, MUTED))
    w.append(image(1100, 742, 1164, 806, bind='totk.fuse.icon', need_bind='totk.fuse.has', src_hold=False))
    w.append(label(1132, 836, None, 26, INK, bind='totk.fuse.life', need_bind='totk.fuse.has'))
    w.append(image(1108, 750, 1156, 798, f'{M}icon/Obj_OneTouchBond', need_bind='totk.fuse.none', tint='#50FFFFFF'))
    w.append(label(1132, 830, 'None', 20, MUTED, need_bind='totk.fuse.none'))
    # left pager: hearts / sages (a tap or a vertical swipe turns it)
    w.append(rect(16, 232, 200, 700, PANE, on_tap='turn_left', on_swipe_up='turn_left', on_swipe_down='turn_left'))
    w.append(rect(16, 232, 22, 700, TEAL, need_bind='totk.hearts.ok'))
    w.append(rect(16, 232, 22, 700, RED, need_bind='totk.hearts.low', hide_bind='@flag:left_pane'))
    w.append(rect(16, 232, 22, 700, TEAL, need_bind='@flag:left_pane'))
    pager_dots(w, 206, 252, 'left_pane')
    hearts = {'hide_bind': '@flag:left_pane'}
    w.append(label(111, 274, 'Hearts', 28, INK, **hearts))
    for grid, cols, cell, n in (('s', 4, 40, 32), ('d', 6, 27, 60)):
        left = 28 + (166 - cols * cell) / 2
        inset = round(cell * .08)
        w.append({'type': 'image', 'rect': [round(left + inset), 296 + inset, cell - 2 * inset, cell - 2 * inset],
                  'src_bind': 'totk.heart.{i}.src', 'need_bind': 'totk.heart.{i}.' + grid, 'repeat': n,
                  'repeat_cols': cols, 'repeat_dx': cell, 'repeat_row_dy': cell, **hearts})
    w.append(label(111, 678, None, 28, INK, bind='totk.hearts.text', need_bind='totk.hearts.ok', **hearts))
    w.append(label(111, 678, None, 28, RED, bind='totk.hearts.text', need_bind='totk.hearts.low', **hearts))
    sages = {'need_bind': '@flag:left_pane'}
    w.append(label(111, 274, 'Sages', 28, INK, **sages))
    tones = [MUTED, INK, GOLD, TEAL_TEXT]
    for i, sage in enumerate(['Wind', 'Fire', 'Water', 'Thunder', 'Soul']):
        y = 298 + i * 78
        w.append(image(32, y, 88, y + 56, lyt(f'SageSoulIcon_{sage}^s'), **sages))
        w.append(rect(32, y, 88, y + 56, '#B0000000', need_bind=f'ui.sage.{i}.dim'))
        for t, color in enumerate(tones):
            w.append(label(96, y + 38, None, 22, color, align='left', width=100, bind=f'totk.sage.{i}.status',
                           need_bind=f'ui.sage.{i}.tone_{t}'))
    w.append(image(90, 720, 126, 776, lyt('Nt_RupeeIconNew_00^w')))
    w.append({'type': 'value', 'rect': [108, 830 - CAP[5], 200, CAP[5] + 6], 'bind': 'totk.rupees', 'text_scale': 5,
              'color': INK, 'align': 'center', 'group': True})
    # right pager under the layer plate: armor / meal
    w.append(rect(1032, 422, 1224, 658, PANE, on_tap='turn_right', on_swipe_up='turn_right', on_swipe_down='turn_right'))
    w.append(rect(1218, 482, 1224, 658, TEAL))
    w.append(rect(1044, 428, 1216, 478, PAGE))
    w.append(label(1130, 464, None, 30, TEAL_TEXT, width=160, bind='totk.layer_text'))
    w.append(rect(1048, 482, 1208, 483, CARD_LINE))
    pager_dots(w, 1016, 604, 'right_pane')
    armor = {'hide_bind': '@flag:right_pane'}
    w.append(label(1128, 516, 'Armor', 24, INK, **armor))
    for i in range(3):
        x = 1046 + i * 56
        w.append(image(x, 540, x + 52, 592, bind=f'totk.armor.{i}.icon', need_bind=f'totk.armor.{i}.on', **armor))
    w.append(label(1128, 576, 'None', 22, MUTED, need_bind='totk.armor.none', **armor))
    meal = {'need_bind': '@flag:right_pane'}
    w.append(label(1128, 516, 'Meal', 24, INK, **meal))
    w.append(label(1128, 566, 'Meal effect', 20, GOLD, hide_bind='totk.meal.none', **meal))
    w.append(label(1128, 620, None, 24, INK, bind='totk.meal.text', hide_bind='totk.meal.none', **meal))
    w.append(label(1128, 576, 'None', 22, MUTED, hide_bind='totk.meal.has', **meal))
    # where Link is, under the map; then the command bar
    w.append(label(620, 914, None, 28, INK, width=760, bind='totk.coords'))
    w.append(rect(216, 944, 1024, 945, CARD_LINE))
    command_bar(w)
    # the game owns the moment: a cutscene dims everything; the pause menu says so (its command bar stays)
    w.append(rect(0, 0, 1240, 1080, '#90000000', need_bind='totk.dim', input_block=True, tap_block=True))
    menu = {'need_bind': 'totk.menu'}
    w.append(rect(0, 424, 1240, 504, '#F0000000', **menu))
    w.append(rect(0, 424, 1240, 427, INK, **menu))
    w.append(rect(0, 501, 1240, 504, INK, **menu))
    w.append(label(620, 475, 'Purah Pad open on the main screen', 32, '#FFFFFEE4', **menu))
    command_bar(w, 'totk.menu')
    # before a save is loaded, another build, or no reading yet: a page of its own
    w.extend(state_cover())
    return {'id': 'ready', 'widgets': w}


def pager_dots(w, x, y, flag):
    w.append(rect(x, y, x + 10, y + 10, INK, hide_bind=f'@flag:{flag}'))
    w.append(rect(x, y, x + 10, y + 10, '#40E9E5D5', need_bind=f'@flag:{flag}'))
    w.append(rect(x, y + 22, x + 10, y + 32, '#40E9E5D5', hide_bind=f'@flag:{flag}'))
    w.append(rect(x, y + 22, x + 10, y + 32, INK, need_bind=f'@flag:{flag}'))


def state_cover():
    gate = {'need_bind': 'totk.not_ready'}
    other = {'need_bind': '!build_match'}   # another game version: the runtime does not load the module
    return [rect(0, 0, 1240, 1080, PAGE, input_block=True, tap_block=True, **gate),
            image(560, 380, 680, 500, lyt('Nt_MapPlayerIconNew_00^s'), tint='#60FFFFFF', **gate),
            label(620, 580, None, 34, INK, width=1100, bind='totk.status_text', **gate),
            rect(0, 0, 1240, 1080, PAGE, input_block=True, tap_block=True, **other),
            label(620, 520, 'This companion needs Tears of the Kingdom 1.4.3', 34, INK, width=1100, **other),
            label(620, 580, 'Update the game to version 1.4.3 to use it.', 26, MUTED, width=1100, **other)]


TABS = [('Weapons', 'IconWeapon_72x72^s'), ('Bows', 'IconBow_72x72^s'), ('Shields', 'IconShield_72x72^s'),
        ('Armor', 'IconEquipment_72x72^s'), ('Materials', 'IconMaterial_72x72^s'), ('Food', 'IconCuisine_72x72^s'),
        ('Zonai', 'IconRecycle_72x72^s'), ('Key items', 'Nt_PauseIconL_06^s')]
MAX_ROWS = 300
CELL_W, CELL_H, COLS = 196, 150, 6


def inventory_page():
    w = [rect(0, 0, 1240, 1080, PAGE)]
    for i, (name, icon) in enumerate(TABS):
        x = 16 + i * 134
        w.append(rect(x, 16, x + 126, 96, PANE, CARD_LINE, on_tap=f'inv_tab_{i}'))
        w.append(rect(x, 90, x + 126, 96, TEAL, need_bind=f'totk.inv.tab.{i}'))
        w.append(image(x + 35, 22, x + 91, 78, lyt(icon), tint='#FFE9E5D5', hide_bind=f'totk.inv.tab.{i}'))
        w.append(image(x + 35, 22, x + 91, 78, lyt(icon), tint='#FFA8FBE6', need_bind=f'totk.inv.tab.{i}'))
    w.append(rect(1100, 16, 1224, 96, PAGE, '#90E9E5D5', on_tap='page_ready'))
    w.append(label(1162, 70, 'Back', 30, INK))
    grid = {'scroll': 'inv_grid', 'repeat': MAX_ROWS, 'repeat_cols': COLS, 'repeat_dx': CELL_W + 4,
            'repeat_row_dy': CELL_H}
    x0, y0 = 16, 112
    w.append({'type': 'rect', 'rect': [x0, y0, CELL_W, CELL_H - 8], 'bg': CARD, 'color': CARD_LINE, 'frame': 2,
              'need_bind': 'totk.inv.{i}.on', 'on_tap': 'inv_sel_{i}', **grid})
    w.append({'type': 'rect', 'rect': [x0, y0, CELL_W, CELL_H - 8], 'bg': 0, 'color': TEAL, 'frame': 3,
              'need_bind': 'totk.inv.{i}.equipped', **grid})
    w.append({'type': 'rect', 'rect': [x0, y0, CELL_W, CELL_H - 8], 'bg': '#3000FFC3', 'color': '#FFFFFEE4',
              'frame': 3, 'need_bind': 'totk.inv.{i}.selected', **grid})
    w.append({'type': 'image', 'rect': [x0 + 50, y0 + 8, 96, 96], 'src_bind': 'totk.inv.{i}.icon',
              'need_bind': 'totk.inv.{i}.on', 'src_hold': False, **grid})
    w.append({'type': 'image', 'rect': [x0 + 8, y0 + 8, 40, 40], 'src_bind': 'totk.inv.{i}.fuse_icon',
              'need_bind': 'totk.inv.{i}.fused', 'src_hold': False, **grid})
    w.append({'type': 'label', 'rect': [x0 + CELL_W - 8, y0 + 106, CELL_W - 16, 21], 'bind_text': 'totk.inv.{i}.qty',
              'text_scale': 3, 'color': INK, 'align': 'right',
              'need_bind': 'totk.inv.{i}.on', **grid})
    w.append({'type': 'bar', 'rect': [x0 + 16, y0 + 124, CELL_W - 32, 8], 'bind': 'totk.inv.{i}.life',
              'max_bind': 'totk.inv.{i}.life_max', 'color': INK, 'bg': '#40E9E5D5', 'frame': 0,
              'need_bind': 'totk.inv.{i}.has_life', **grid})
    w.append(label(620, 470, 'Nothing here', 30, MUTED, need_bind='totk.inv.empty'))
    # the selected item
    w.append(rect(16, 856, 1224, 1064, PANE, CARD_LINE))
    sel = {'need_bind': 'totk.inv.sel.has'}
    w.append(label(40, 900, None, 34, INK, align='left', width=1160, bind='totk.inv.sel.name', **sel))
    w.append(label(40, 946, None, 24, GOLD, align='left', width=1160, bind='totk.inv.sel.facts', **sel))
    w.append({'type': 'label', 'rect': [40, 966, 1160, 90], 'bind_text': 'totk.inv.sel.caption', 'text_scale': 3,
              'color': MUTED, 'wrap_width': 1160, 'max_lines': 3, **sel})
    w.append(label(620, 972, 'Tap an item to see it here', 26, MUTED, need_bind='totk.inv.sel.none'))
    w.extend(state_cover())
    scroll = {'id': 'inv_grid', 'rect': [x0, y0, 1208, 732], 'count_bind': 'totk.inv.count', 'row_h': CELL_H,
              'cols': COLS, 'reset_bind': '@flag:inv_tab', 'bar': '#80E9E5D5', 'bar_track': '#20E9E5D5'}
    return {'id': 'inventory', 'widgets': w, 'scrolls': [scroll]}


def map_page():
    w = [rect(0, 0, 1240, 1080, '#FF12110C'),
         {'type': 'map', 'id': 'totk_map', 'area': 'TotkFull', 'rect': [0, 0, 1240, 1080], 'area_label': False,
          'pan_zoom': True, 'min_zoom': 1, 'max_zoom': 12, 'view_idle_ms': 0,
          'marker_x_bind': 'totk.map.x', 'marker_y_bind': 'totk.map.y', 'marker_src': lyt('Nt_MapPlayerIconNew_00^s'),
          'marker_size': [64, 64], 'marker_rotate_bind': 'totk.map.heading', 'overlays': tiles('totk.map.tile')}]
    buttons = [('Back', 'page_ready', None), ('Surface', 'map_layer_1', 'totk.map.page_layer_is.0'),
               ('Sky', 'map_layer_2', 'totk.map.page_layer_is.1'), ('Depths', 'map_layer_3', 'totk.map.page_layer_is.2'),
               ('Follow Link', 'map_layer_0', None), ('Recenter', 'map_recenter', None)]
    for i, (text, action, lit) in enumerate(buttons):
        y = 16 + i * 84 + (24 if i else 0)
        w.append(rect(16, y, 216, y + 72, '#E015140F', '#90E9E5D5', on_tap=action))
        if lit:
            w.append(rect(16, y + 66, 216, y + 72, TEAL, need_bind=lit))
        w.append(label(116, y + 48, text, 28, INK))
    w.extend(state_cover())
    return {'id': 'map', 'widgets': w}


def actions():
    page = lambda p: {'kind': 'page', 'page': p, 'transition': 'fade', 'duration_ms': 130, 'easing': 'ease_in_out'}
    a = {'page_ready': page('ready'), 'page_inventory': page('inventory'), 'page_map': page('map'),
         'press_minus': {'kind': 'button', 'button': 'Minus', 'frames': 6},
         'press_plus': {'kind': 'button', 'button': 'Plus', 'frames': 6},
         'turn_left': {'kind': 'flag', 'flag': 'left_pane'}, 'turn_right': {'kind': 'flag', 'flag': 'right_pane'},
         'map_recenter': {'kind': 'view_reset', 'view': 'totk_map'}}
    for i in range(len(TABS)):
        a[f'inv_tab_{i}'] = {'kind': 'flag', 'flag': 'inv_tab', 'value': i}
    for i in range(MAX_ROWS):
        a[f'inv_sel_{i}'] = {'kind': 'flag', 'flag': 'inv_sel', 'value': i}
    for i in range(4):
        a[f'map_layer_{i}'] = {'kind': 'flag', 'flag': 'map_layer', 'value': i}
    return a


def derived():
    # values the pages bind that are simple functions of the module's: sage tones as gates, Poes as text
    d = [{'name': f'totk.sage.{i}.tone_{t}', 'cmp': 'eq', 'a': f'totk.sage.{i}.tone', 'b': t}
         for i in range(5) for t in range(4)]
    d += [{'name': f'ui.sage.{i}.tone_{t}', 'all_nonzero': [f'totk.sage.{i}.tone_{t}', '@flag:left_pane']}
          for i in range(5) for t in range(4)]
    d += [{'name': f'ui.sage.{i}.dim', 'all_nonzero': [f'totk.sage.{i}.away', '@flag:left_pane']} for i in range(5)]
    return d


def manifest():
    world = {'min': [-6000, -5000], 'max': [6000, 5000], 'image': f'{M}gen/clear', 'no_pin': False}
    return {
        'format': 1,
        'title_id': '0100F2C0115B6000',
        'name': 'Tears of the Kingdom companion',
        'min_runtime': 22,
        'requires_module': True,
        'module': {'abi': 1, 'build_ids': [BUILD_143]},
        'canvas_w': 1240,
        'canvas_h': 1080,
        'background': PAGE,
        'font': 'file:totk_font.txt',
        'font_atlas': f'{M}font/{FONT}',
        '_note': 'Generated by tools/totk/gen_manifest.py. All game art and text are decoded at runtime from the '
                 "player's own romfs by the native module; the package ships none.",
        'flags': {'left_pane': 0, 'right_pane': 0, 'inv_tab': 0, 'inv_sel': -1, 'map_layer': 0},
        'map': {'areas': {'TotkMini': dict(world, clamp_view=False), 'TotkFull': dict(world, clamp_view=True)},
                'style': {'background': '#FF12110C'}},
        'derived': derived(),
        'actions': actions(),
        'haptics': {'enabled': True, 'respect_system': True, 'tap': 'light', 'select': 'light', 'swipe': 'light'},
        'pages': [ready_page(), inventory_page(), map_page()],
    }


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else OUT
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, 'w') as f:
        json.dump(manifest(), f, separators=(',', ':'))
        f.write('\n')
    with open(os.path.join(os.path.dirname(out), 'totk_font.txt'), 'w') as f:
        f.write("# Game font for the companion pages: <font in /Font/Font.Nin_NX_NVN.bfarc.zs>:<cap height px>.\n"
                "# The module rasterises it from the player's own romfs; nothing is shipped.\n" + FONT + '\n')
    print(f'{out}: {os.path.getsize(out)} bytes')


if __name__ == '__main__':
    main()
