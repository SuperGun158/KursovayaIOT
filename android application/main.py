import os
import requests
import re
import kivy
from kivy.app import App
from kivy.uix.boxlayout import BoxLayout
from kivy.uix.label import Label
from kivy.uix.textinput import TextInput
from kivy.uix.button import Button

class FloatInput(TextInput):
    pat = re.compile('[^0-9]')
    def insert_text(self, substring, from_undo=False):
        pat = self.pat
        if '.' in self.text:
            s = re.sub(pat, '', substring)
        else:
            s = '.'.join(
                re.sub(pat, '', s)
                for s in substring.split('.', 1)
            )
        if substring == '-' and len(self.text) == 0:
            s = substring
        return super().insert_text(s, from_undo=from_undo)

class MyApp(App):
    font = 20
    def replace_fan(self):
        self.fan = not self.fan
        self.layout.remove_widget(self.error)
        if not self.auto:
            self.layout.remove_widget(self.btn_vikl_auto)
            self.layout.add_widget(self.btn_vikl_auto)
        else:
            self.layout.remove_widget(self.btn_vkl_auto)
            self.layout.add_widget(self.btn_vkl_auto)
        if self.fan:
            self.layout.remove_widget(self.btn_vikl)
            self.layout.add_widget(self.btn_vkl)
        else:
            self.layout.remove_widget(self.btn_vkl)
            self.layout.add_widget(self.btn_vikl)
        self.layout.add_widget(self.error)

    def replace_auto(self):
        self.auto = not self.auto
        self.layout.remove_widget(self.error)
        if self.auto:
            self.layout.remove_widget(self.btn_vikl_auto)
            self.layout.add_widget(self.btn_vkl_auto)
        else:
            self.layout.remove_widget(self.btn_vkl_auto)
            self.layout.add_widget(self.btn_vikl_auto)
        if not self.fan:
            self.layout.remove_widget(self.btn_vikl)
            self.layout.add_widget(self.btn_vikl)
        else:
            self.layout.remove_widget(self.btn_vkl)
            self.layout.add_widget(self.btn_vkl)
        self.layout.add_widget(self.error)
        
    def build(self):
        f = open('config.txt', 'r')
        z =  f.read()
        f.close()
        self.auto = True
        self.fan = True
        self.url = TextInput(text = z, hint_text="Введите IP-адресс:порт сервера", multiline=False, font_size=MyApp.font)
        self.file_path = 'notes.txt'
        self.layout = BoxLayout(orientation='vertical', padding=20, spacing=10)
        
        self.label = Label(text="Умный вентилятор", font_size=MyApp.font)
        self.error = Label(text='', font_size=MyApp.font)
        
        self.input_temp = FloatInput(hint_text="Введите предпочитаемую температуру", multiline=False, font_size=MyApp.font)

        self.btn_send = Button(text="Отправить данные", font_size=MyApp.font, background_color=(0, 255, 0, 255))

        self.btn_vkl_auto = Button(text="Включить автоматический режим", font_size=MyApp.font, background_color=(0, 255, 0, 255))
        self.btn_vikl_auto = Button(text="Отключить автоматический режим", font_size=MyApp.font, background_color=(255, 0, 0, 255))
        self.btn_vkl = Button(text="Включить вентилятор", font_size=MyApp.font, background_color=(0, 255, 0, 255))
        self.btn_vikl = Button(text="Выключить вентилятор", font_size=MyApp.font, background_color=(255, 0, 0, 255))
        
        self.btn_send.bind(on_press=self.on_button_send)
        self.btn_vkl_auto.bind(on_press=self.on_button_vkl_auto)
        self.btn_vikl_auto.bind(on_press=self.on_button_vikl_auto)
        self.btn_vkl.bind(on_press=self.on_button_vkl)
        self.btn_vikl.bind(on_press=self.on_button_vikl)
        
        self.layout.add_widget(self.label)
        self.layout.add_widget(self.url)
        self.layout.add_widget(self.input_temp)
        self.layout.add_widget(self.btn_send)
        if self.auto:
            self.layout.add_widget(self.btn_vkl_auto)
        else:
            self.layout.add_widget(self.btn_vikl_auto)
        if self.fan:
            self.layout.add_widget(self.btn_vkl)
        else:
            self.layout.add_widget(self.btn_vikl)
        self.layout.add_widget(self.error) 
        return self.layout

    def on_button_send(self, instance):
        temp = self.input_temp.text
        if temp == '':
            self.layout.remove_widget(self.error)
            self.error = Label(text='Заполните поле!', font_size=MyApp.font, color = (1, 0, 0, 1))
            self.layout.add_widget(self.error)
            return
        else:
            self.layout.remove_widget(self.error)
            self.error = Label(text='', font_size=MyApp.font)
            self.layout.add_widget(self.error)
        temp = float(temp)
        params = {'temp': temp}
        try:
            response = requests.get('http://' + self.url.text + '/float', params=params, timeout=3)
            print(f"Запрос отправлен! Код ответа: {response.status_code}")
        except requests.exceptions.Timeout:
            self.layout.remove_widget(self.error)
            self.error = Label(text='Время ожидания вышло!', font_size=MyApp.font, color = (1, 0, 0, 1))
            self.layout.add_widget(self.error)
        except requests.exceptions.ConnectionError:
            self.layout.remove_widget(self.error)
            self.error = Label(text='Нет подключения!', font_size=MyApp.font, color = (1, 0, 0, 1))
            self.layout.add_widget(self.error)
    def on_button_vkl_auto(self, instance):
        params = {'auto': 1}
        try:
            response = requests.get('http://' + self.url.text + '/auto', params=params, timeout=3)
            print(f"Запрос отправлен! Код ответа: {response.status_code}")
            self.replace_auto()
        except requests.exceptions.Timeout:
            self.layout.remove_widget(self.error)
            self.error = Label(text='Время ожидания вышло!', font_size=MyApp.font, color = (1, 0, 0, 1))
            self.layout.add_widget(self.error)
        except requests.exceptions.ConnectionError:
            self.layout.remove_widget(self.error)
            self.error = Label(text='Нет подключения!', font_size=MyApp.font, color = (1, 0, 0, 1))
            self.layout.add_widget(self.error)
        
    def on_button_vikl_auto(self, instance):
        params = {'auto': 0}
        try:
            response = requests.get('http://' + self.url.text + '/auto', params=params, timeout=3)
            print(f"Запрос отправлен! Код ответа: {response.status_code}")
            self.replace_auto()
        except requests.exceptions.Timeout:
            self.layout.remove_widget(self.error)
            self.error = Label(text='Время ожидания вышло!', font_size=MyApp.font, color = (1, 0, 0, 1))
            self.layout.add_widget(self.error)
        except requests.exceptions.ConnectionError:
            self.layout.remove_widget(self.error)
            self.error = Label(text='Нет подключения!', font_size=MyApp.font, color = (1, 0, 0, 1))
            self.layout.add_widget(self.error)

    def on_button_vkl(self, instance):
        params = {'work': 1}
        try:
            response = requests.get('http://' + self.url.text + '/work', params=params, timeout=3)
            print(f"Запрос отправлен! Код ответа: {response.status_code}")
            self.replace_fan()
        except requests.exceptions.Timeout:
            self.layout.remove_widget(self.error)
            self.error = Label(text='Время ожидания вышло!', font_size=MyApp.font, color = (1, 0, 0, 1))
            self.layout.add_widget(self.error)
        except requests.exceptions.ConnectionError:
            self.layout.remove_widget(self.error)
            self.error = Label(text='Нет подключения!', font_size=MyApp.font, color = (1, 0, 0, 1))
            self.layout.add_widget(self.error)

    def on_button_vikl(self, instance):
        params = {'work': 0}
        try:
            response = requests.get('http://' + self.url.text + '/work', params=params, timeout=3)
            print(f"Запрос отправлен! Код ответа: {response.status_code}")
            self.replace_fan()
        except requests.exceptions.Timeout:
            self.layout.remove_widget(self.error)
            self.error = Label(text='Время ожидания вышло!', font_size=MyApp.font, color = (1, 0, 0, 1))
            self.layout.add_widget(self.error)
        except requests.exceptions.ConnectionError:
            self.layout.remove_widget(self.error)
            self.error = Label(text='Нет подключения!', font_size=MyApp.font, color = (1, 0, 0, 1))
            self.layout.add_widget(self.error)
            
    def on_stop(self):
        f = open('config.txt', 'w')
        z =  f.write(self.url.text)
        f.close()
        return False
if __name__ == '__main__':
    MyApp().run()
