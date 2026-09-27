"""Configure the game screens and their bindings."""
import json
import os
import unreal

ROOT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SCREENS = '/Game/UI/Screens/WBP_'
NAMES = ['MainMenu', 'Login', 'GameplayHUD', 'Dialogue', 'TicketCheck',
         'Profile', 'Leaderboard', 'Results', 'Theory', 'Tutorial']
TEXT = {
    'MainMenu': {'Status': 'connection'}, 'Login': {'Status': 'connection'},
    'GameplayHUD': {'Gauges': 'gauges', 'Timer': 'timer', 'TaskIndicators': 'tasks_compact', 'Status': 'connection',
                    **{'Slot%dLabel' % i: 'slot:%d' % i for i in range(8)}},
    'Dialogue': {'Status': 'connection', 'PassengerLine': 'task'},
    'TicketCheck': {'Passport': 'document:passport', 'Ticket': 'document:ticket', 'Terminal': 'document:terminal', 'Status': 'connection'},
    'Profile': {'ProfileSummary': 'profile.summary', 'Activity': 'profile.activity', 'StatusText': 'connection'},
    'Leaderboard': {'LeaderboardText': 'leaderboard', 'StatusText': 'connection'},
    'Results': {'ReportText': 'report', 'StatusText': 'connection'},
}
ENABLED = {
    'MainMenu': {'PlayButton': 'start'},
    'Login': {'LoginButton': 'idle', 'RegisterButton': 'idle'},
    'Dialogue': {'SendAnswer': 'action'},
    'GameplayHUD': {b: 'action' for b in ['Interact', 'OpenDocuments', 'Take'] + ['Slot%d' % i for i in range(8)]},
    'TicketCheck': {'AcceptButton': 'action', 'RejectButton': 'action'},
    'Leaderboard': {b: 'idle' for b in ['CompanyButton', 'DepotButton', 'BrigadeButton']},
}


class Screen:
    def __init__(self, name):
        self.name = name
        self.bp = unreal.load_asset(SCREENS + name)
        if not self.bp:
            raise RuntimeError('Missing screen: ' + name)
        self.tree = unreal.find_object(self.bp, 'WidgetTree')
        self.widgets = {}
        self.rects = {}
        self.buttons = []
        # Capture objects before detaching. Clear every panel to remove stale
        # references to the same object (Profile had two such references).
        for key in unreal.VSMWidgetEditorTools.list_widget_names(self.bp):
            w = unreal.find_object(self.tree, str(key))
            if not w:
                raise RuntimeError(name + ': missing template ' + str(key))
            self.widgets[str(key)] = w
        for w in self.widgets.values():
            w.modify()
            if isinstance(w, unreal.PanelWidget):
                w.clear_children()
        self.root = self.get('RootCanvas', unreal.CanvasPanel)

    def get(self, name, cls, create=False):
        w = self.widgets.get(name) or unreal.find_object(self.tree, name)
        if w is None and create:
            w = unreal.new_object(cls, outer=self.tree, name=name)
        if not isinstance(w, cls):
            raise RuntimeError(self.name + ': missing/wrong widget ' + name)
        self.widgets[name] = w
        return w

    def place(self, w, x, y, width, height):
        slot = self.root.add_child_to_canvas(w)
        slot.set_anchors(unreal.Anchors(minimum=unreal.Vector2D(x / 1280.0, y / 720.0), maximum=unreal.Vector2D((x + width) / 1280.0, (y + height) / 720.0)))
        slot.set_alignment(unreal.Vector2D(0, 0))
        slot.set_auto_size(False)
        slot.set_position(unreal.Vector2D(0, 0))
        slot.set_size(unreal.Vector2D(0, 0))
        self.rects[w.get_name()] = [x, y, width, height]
        return w

    def place_stretched(self, w):
        slot = self.root.add_child_to_canvas(w)
        slot.set_anchors(unreal.Anchors(minimum=unreal.Vector2D(0, 0), maximum=unreal.Vector2D(1, 1)))
        slot.set_alignment(unreal.Vector2D(0, 0))
        slot.set_auto_size(False)
        slot.set_position(unreal.Vector2D(0, 0))
        slot.set_size(unreal.Vector2D(0, 0))
        self.rects[w.get_name()] = [0, 0, 1280, 720]
        return w
    def text(self, name, x, y, width, height, value=None, size=22):
        w = self.get(name, unreal.TextBlock, create=True)
        if value is not None:
            w.set_text(value)
        font = w.get_editor_property('font')
        font.size = size
        w.set_font(font)
        w.set_auto_wrap_text(True)
        w.set_visibility(unreal.SlateVisibility.HIT_TEST_INVISIBLE)
        return self.place(w, x, y, width, height)

    def button(self, name, label, x, y, width=260, height=52):
        b = self.get(name, unreal.Button, create=True)
        t = self.get(name + 'Label', unreal.TextBlock, create=True)
        t.set_text(label)
        font = t.get_editor_property('font')
        font.size = 18
        t.set_font(font)
        t.set_auto_wrap_text(True)
        t.set_editor_property('justification', unreal.TextJustify.CENTER)
        t.set_visibility(unreal.SlateVisibility.HIT_TEST_INVISIBLE)
        slot = b.add_child(t)
        slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_CENTER)
        slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_CENTER)
        b.set_visibility(unreal.SlateVisibility.VISIBLE)
        self.buttons.append(name)
        return self.place(b, x, y, width, height)

    def scroll(self, name, content, x, y, width, height):
        scroll = self.get(name, unreal.ScrollBox, create=True)
        scroll.set_visibility(unreal.SlateVisibility.VISIBLE)
        self.place(scroll, x, y, width, height)
        for key in content:
            t = self.get(key, unreal.TextBlock)
            t.set_auto_wrap_text(True)
            font = t.get_editor_property('font'); font.size = 22; t.set_font(font)
            t.set_visibility(unreal.SlateVisibility.HIT_TEST_INVISIBLE)
            scroll.add_child(t)

    def finish(self):
        # Template checks catch problems that direct delegate broadcasts cannot.
        for name in self.buttons:
            b = self.widgets[name]
            assert b.get_children_count() == 1 and isinstance(b.get_child_at(0), unreal.TextBlock), (self.name, name, 'label')
            assert str(b.get_child_at(0).get_text()).strip(), (self.name, name, 'empty label')
            x, y, w, h = self.rects[name]
            assert w >= 44 and h >= 44, (self.name, name, 'too small')
        for i, a in enumerate(self.buttons):
            ax, ay, aw, ah = self.rects[a]
            for b in self.buttons[i+1:]:
                bx, by, bw, bh = self.rects[b]
                assert ax+aw <= bx or bx+bw <= ax or ay+ah <= by or by+bh <= ay, (self.name, a, b, 'overlap')
        for name, (x, y, w, h) in self.rects.items():
            assert 0 <= x and 0 <= y and x+w <= 1280 and y+h <= 720, (self.name, name, 'bounds')
        for key in TEXT.get(self.name, {}):
            assert isinstance(self.widgets[key], unreal.TextBlock) and self.widgets[key].get_parent(), (self.name, key, 'text binding')
        for key in ENABLED.get(self.name, {}):
            assert key in self.buttons, (self.name, key, 'enabled binding')
        if not unreal.VSMWidgetEditorTools.compile_screen(self.bp):
            raise RuntimeError('Compile failed: ' + self.name)
        cdo = unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class(SCREENS + self.name))
        for prop, values in [('text_bindings', TEXT.get(self.name, {})), ('enabled_bindings', ENABLED.get(self.name, {}))]:
            cdo.set_editor_property(prop, {unreal.Name(k): v for k, v in values.items()})
            assert {str(k): str(v) for k, v in cdo.get_editor_property(prop).items()} == values
        assert not unreal.VSMWidgetEditorTools.list_repeated_names(self.bp), self.name + ': repeated widgets'
        assert not unreal.VSMWidgetEditorTools.list_out_of_bounds_widgets(self.bp, 1280, 720), self.name + ': bounds'
        if not unreal.EditorAssetLibrary.save_loaded_asset(self.bp, False):
            raise RuntimeError('Save failed: ' + self.name)
        return {'buttons': self.buttons, 'rects': self.rects, 'text': TEXT.get(self.name, {}), 'enabled': ENABLED.get(self.name, {}), 'compiled': True}


report = {}
for name in NAMES:
    s = Screen(name)
    if name not in ('GameplayHUD', 'Dialogue', 'TicketCheck'):
        background = s.get('MenuBackground', unreal.Border, create=True)
        background.set_brush_color(unreal.LinearColor(0.04, 0.07, 0.12, 1.0))
        background.set_visibility(unreal.SlateVisibility.HIT_TEST_INVISIBLE)
        s.place_stretched(background)
    if name != 'GameplayHUD':
        s.text('Title', 40, 24, 1200, 64, size=30)
    if name == 'MainMenu':
        for i, (b, label) in enumerate([('PlayButton','Играть'), ('ProfileButton','Профиль'),
                                      ('LeaderboardButton','Таблица лидеров'), ('TheoryButton','Теория и справка')]):
            s.button(b, label, 240 + i%2*420, 185 + i//2*110, 380, 72)
        s.text('Status', 60, 470, 1160, 110)
    elif name == 'Login':
        for key, y in [('LoginInput', 220), ('PasswordInput', 310)]:
            field = s.get(key, unreal.EditableTextBox)
            field.set_hint_text('Логин (минимум 3 символа)' if key == 'LoginInput' else 'Пароль (минимум 8 символов)')
            s.place(field, 300, y, 680, 64)
        s.button('LoginButton','Войти',300,420,320)
        s.button('RegisterButton','Создать аккаунт',660,420,320)
        s.text('Status',80,500,1120,115)
    elif name == 'GameplayHUD':
        surface = s.get('TouchSurface', unreal.Border)
        surface.set_visibility(unreal.SlateVisibility.VISIBLE)
        surface.set_brush_color(unreal.LinearColor(0, 0, 0, 0))
        s.place_stretched(surface)
        for key, asset, box in [('JoystickBase','VirtualJoystick_Background',(70,470,180,180)),
                                ('JoystickThumb','VirtualJoystick_Thumb',(125,525,70,70))]:
            image = s.get(key, unreal.Image, create=True)
            image.set_brush_from_texture(unreal.load_asset('/Engine/MobileResources/HUD/' + asset))
            image.set_visibility(unreal.SlateVisibility.HIT_TEST_INVISIBLE)
            s.place(image,*box)
        s.text('Gauges',24,62,480,72)
        s.text('Timer',530,62,220,50)
        s.text('TaskIndicators',24,150,300,190,size=30)
        for b,label,x,y in [('Menu','В главное меню',800,20),('HelpButton','Как играть',800,88),
                             ('Interact','Поговорить',800,430),('OpenDocuments','Проверить документы',1030,430),('Take','Получить предмет',1030,498)]:
            s.button(b,label,x,y,220,56)
        s.text('Status',350,480,650,100)
        for i in range(8): s.button('Slot%d'%i,'·',300+i*116,628,108,64)
        dim = s.get('ExitDim', unreal.Border, create=True)
        dim.set_brush_color(unreal.LinearColor(0.01, 0.02, 0.04, 0.9))
        s.place_stretched(dim)
        s.text('ExitQuestion', 320, 245, 640, 72, 'Вы точно хотите завершить игру?', 26)
        s.button('ExitYes', 'Да', 400, 335, 200)
        s.button('ExitNo', 'Нет', 680, 335, 200)
        for key in ('ExitDim', 'ExitQuestion', 'ExitYes', 'ExitNo'):
            s.get(key, unreal.Widget).set_visibility(unreal.SlateVisibility.COLLAPSED)
    elif name == 'Dialogue':
        s.scroll('PassengerScroll',['PassengerLine'],40,110,1200,130)
        s.place(s.get('AnswerInput', unreal.EditableTextBox),40,270,1200,100)
        s.button('SendAnswer','Ответить',40,420,340)
        s.text('Status',40,500,1200,100)
        s.button('BackButton','Закрыть',40,640,340)
    elif name == 'TicketCheck':
        for i, (header, block, label) in enumerate([('PassportHeader','Passport','Паспорт'),('TicketHeader','Ticket','Билет'),('TerminalHeader','Terminal','Терминал')]):
            s.text(header,40+i*410,105,380,44,label)
            s.scroll(block+'Scroll',[block],40+i*410,160,380,340)
        s.text('Status',40,525,1200,76)
        s.button('AcceptButton','Принять билет',40,635,350)
        s.button('RejectButton','Отклонить билет',440,635,350)
        s.button('BackButton','Закрыть',900,635,340)
    elif name == 'Profile':
        s.scroll('ProfileScroll',['ProfileSummary'],40,120,550,380)
        s.scroll('ScrollBox',['Activity'],630,120,610,380)
        s.text('StatusText',40,525,1200,80)
        s.button('LogoutButton','Выйти / сменить аккаунт',420,635,420)
        s.button('BackButton','Назад',980,635)
    elif name == 'Leaderboard':
        s.scroll('LeaderboardScroll',['LeaderboardText'],40,120,1200,380)
        s.text('StatusText',40,525,1200,80)
        for i, (b,label) in enumerate([('CompanyButton','Компания'),('DepotButton','Депо'),('BrigadeButton','Бригада'),('BackButton','Назад')]):
            s.button(b,label,40+i*310,635,270)
    elif name == 'Results':
        s.scroll('ReportScroll',['ReportText'],40,120,1200,380)
        s.text('StatusText',40,525,1200,80)
        s.button('BackButton','В главное меню',930,635,310)
    else:
        s.scroll('InstructionsScroll',['Instructions'],40,110,1200,470)
        s.button('BackButton','Закрыть',900,635,340)
    report[name] = s.finish()

os.makedirs(os.path.join(ROOT,'Saved/Verification'), exist_ok=True)
with open(os.path.join(ROOT,'Saved/Verification/umg_layout.json'),'w',encoding='utf-8') as f:
    json.dump(report,f,ensure_ascii=False,indent=2)
hud_path = '/Game/Framework/BP_HUD'
hud = unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class(hud_path))
mapping = dict(hud.get_editor_property('screen_classes'))
for name in ('SCENARIOS', 'SETTINGS'):
    mapping.pop(getattr(unreal.VSMUIScreen, name), None)
hud.set_editor_property('screen_classes', mapping)
unreal.EditorAssetLibrary.save_asset(hud_path, False)
for name in ('Training', 'Settings'):
    path = SCREENS + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
unreal.log('VSM_UMG_VERIFIED: %d screens compiled, saved, layout and bindings checked' % len(NAMES))
