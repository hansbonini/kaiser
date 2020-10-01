#!/usr/bin/env python
import os
import re
import sys

# Import system libs
from ctypes import *
from zipfile import is_zipfile, ZipFile
from collections import OrderedDict
from collections import deque

# Import PIL libs
from PIL import Image
from PIL.ImageQt import ImageQt

# Import QT5 libs
from PyQt5 import QtCore as qt
from PyQt5 import QtWidgets as qtw
from PyQt5 import QtGui as qtg
from PyQt5 import QtMultimedia as qtm


'''
KAISER Sega Genesis/Megadrive debbuger
'''

is_windows = hasattr(sys, 'getwindowsversion')

# Import Core as DLL
core = CDLL('./core.dll' if is_windows else './core.so')

# Define default directories
screenshot_dir = './screenshots'
dumps_dir = './dumps'

# Define default Keyboard Mapping for Joypads
buttons = ['up', 'down', 'left', 'right', 'b', 'c', 'a', 'start']
keymap = OrderedDict((
    ('left', (qt.Qt.Key_Left, qt.Qt.Key_J)),
    ('right', (qt.Qt.Key_Right, qt.Qt.Key_L)),
    ('up', (qt.Qt.Key_Up, qt.Qt.Key_I)),
    ('down', (qt.Qt.Key_Down, qt.Qt.Key_K)),
    ('a', (qt.Qt.Key_A, qt.Qt.Key_F)),
    ('b', (qt.Qt.Key_S, qt.Qt.Key_G)),
    ('c', (qt.Qt.Key_D, qt.Qt.Key_H)),
    ('start', (qt.Qt.Key_Return, qt.Qt.Key_W)),
))
keymap_r = {}
for button, keys in keymap.items():
    keymap_r[keys[0]] = (button, 0)
    keymap_r[keys[1]] = (button, 1)

# Define M68K registers
registers = ['d0', 'd1', 'd2', 'd3', 'd4', 'd5', 'd6', 'd7',
             'a0', 'a1', 'a2', 'a3', 'a4', 'a5', 'a6', 'a7',
             'pc', 'sr', 'sp', 'usp']
# Define Z80 registers
z80_registers = ['af', 'bc', 'de', 'hl', 'ix', 'iy','pc','sp']

# Allocate Screen and Scaled Screen Buffers
screen_buffer = create_string_buffer(320*240*4)
scaled_buffer = create_string_buffer(320*240*4)
# Allocate Audio Stream
audio_buffer_size = 8192
audio_buffer = create_string_buffer(audio_buffer_size)
# Define cycle_counter
cycle_counter = 0
# Define status bar as global
statusbar = None
# Define Breakpoint - disabled
breakpoint = 0
breakpoint_state = False


class Cartridge(object):
    def __init__(self, filename):
        self.filename = filename
        if is_zipfile(filename):
            # if the file is a ZIP, try to open the largest file inside
            zipfile = ZipFile(filename, 'r')
            contents = [(f.file_size, f.filename) for f in zipfile.infolist()]
            contents.sort(reverse=True)
            self.dump = zipfile.read(contents[0][1])
        else:
            self.dump = open(filename, 'rb').read()

    def load(self):
        core.load_cartridge(self.dump, os.path.getsize(self.filename))


class Joypads(qtw.QLabel):
    '''
    Controller mapping help window.
    '''

    def __init__(self, parent=None):
        super(Joypads, self).__init__(parent)
        pass
        self.setWindowTitle('Joypads')


class CramDebug(qtw.QWidget):
    '''
    A window that shows the current palette on CRAM.
    '''

    def __init__(self):
        super().__init__()
        self.title = 'CRAM Debug'           # Set Window Title
        self.setWindowTitle(self.title)
        self.height = 70                    # Set Window Height
        self.width = 240                    # Set Window Width
        # Define window position and size
        self.setGeometry(410, 462, self.width, self.height)
        # Show Window
        self.show()

    def paintEvent(self, e):
        '''
        Paint event for draw current pallete in CRAM
        '''
        # Define current palette as global to reuse in another windows
        global palette
        # Start QPainter
        qp = qtg.QPainter()
        qp.begin(self)
        # Set Background Color as Black
        color = qtg.QColor(0, 0, 0, 0)
        qp.setPen(color)
        # Add 16*4 color entries do palette list
        palette = [qtg.QColor(0, 0, 0)]*(16*4)
        # Iterate over 4 palettes in CRAM (16 colors each)
        for y in range(4):
            for x in range(16):
                # Get color from CRAM Buffer on VDP
                color = core.sega3155313_get_cram(y*16+x)
                # Convert 12-bits palette to RGB
                red, green, blue = color >> 8, color >> 4, color
                red, green, blue = (blue & 15) * \
                    16, (green & 15)*16, (red & 15)*16
                # Set color in palette list
                palette[y*16+x] = qtg.QColor(red, green, blue)
                # Paint color on window
                qp.setBrush(palette[y*16+x])
                qp.drawRect(x*16, y*16, 16, 16)
        # End QPainter
        qp.end()

    def dump(self):
        '''
        Dump CRAM BUffer as Binary
        '''
        # Check if dumps folder exists
        try:
            os.mkdir(dumps_dir)
        except OSError:
            pass
        # Set dump filename
        filename = os.path.join(dumps_dir, 'cram.bin')
        # Create a dump file
        dump = open(filename, 'wb')
        # Allocate a buffer for VRAM Buffer
        cram = create_string_buffer(0x10000)
        # Get VRAM Buffer from VDP
        core.sega3155313_get_cram_raw(cram)
        # Iterate over bytes in VRAM Buffer and write in dump file
        for x in range(0, 0x10000):
            dump.write(cram[x])


class VramDebug(qtw.QWidget):
    '''
    A window that shows the current VRAM.
    '''

    def __init__(self):
        super().__init__()
        self.title = 'VRAM Debug'           # Set Window Title
        self.setWindowTitle(self.title)
        self.height = 400                   # Set Window Height
        self.width = 320                    # Set Window Width
        self.scroll_position = 0            # Set Scroller Position at 0

        # Allocate a buffer to VRAM Display
        self.vram_buffer = create_string_buffer(2048*64*4)

        # Create VRAM display with the same window size
        self.display_dump = qtw.QLabel()
        self.display_dump.maximumHeight = self.height
        self.display_dump.maximumWidth = self.width
        # Create a scrollbar
        self.s1 = qtw.QScrollBar()
        self.s1.setMinimum(0)
        self.s1.setMaximum(1736)
        self.s1.valueChanged.connect(self.pagination)
        # Set a Horizontal Layout and add VRAM Display and Scrollbar
        self.tilebox = qtw.QHBoxLayout()
        self.tilebox.addWidget(self.display_dump)
        self.tilebox.addWidget(self.s1)
        # Set a Vertical Layout to contain debug info and VRAM Display
        self.box = qtw.QVBoxLayout()
        # Embed Horizontal Layout inside Vertical Layout
        self.box.addLayout(self.tilebox)
        # Set Horizontal Layout as Window Layout
        self.setLayout(self.box)
        # Set Window position and size
        self.setGeometry(0, 0, self.width, self.height)
        # Set Window size as fixed
        self.setFixedSize(self.width, self.height)
        # Update and Show()
        self.updateGeometry()
        self.show()
        self.update()

    def update(self):
        '''
        Updates VRAM Display
        '''
        super().update()
        # Get VRAM Buffer from VDP
        core.sega3155313_get_vram(self.vram_buffer, 1)
        # Create an RGB32 Image from VRAM Buffer
        image = qtg.QImage(self.vram_buffer, 128, 1024,
                           qtg.QImage.Format_RGB32)
        # Scale Image to contain in VRAM Display
        image2 = image.scaledToWidth(256)
        # Set Image as PIXMAP
        pixmap = qtg.QPixmap.fromImage(image2)
        # Set pixmap position based on VRAM Display scrollbar
        pixmap.scroll(0, 864-self.scroll_position, pixmap.rect())
        # Set pixmap in VRAM Display
        self.display_dump.setPixmap(pixmap)

    def pagination(self):
        '''
        Control scrollbar pagination
        '''
        # Set scrollbar value as scroll position
        self.scroll_position = self.s1.value()

    def dump(self):
        '''
        Dump VRAM BUffer as Binary
        '''
        # Check if dumps folder exists
        try:
            os.mkdir(dumps_dir)
        except OSError:
            pass
        # Set dump filename
        filename = os.path.join(dumps_dir, 'vram.bin')
        # Create a dump file
        dump = open(filename, 'wb')
        # Allocate a buffer for VRAM Buffer
        vram = create_string_buffer(0x10000)
        # Get VRAM Buffer from VDP
        core.sega3155313_get_vram_raw(vram)
        # Iterate over bytes in VRAM Buffer and write in dump file
        for x in range(0, 0x10000):
            dump.write(vram[x])


class M68kDebug(qtw.QWidget):
    '''
    A window that shows the current M68k Disassembly.
    '''

    def __init__(self):
        super().__init__()
        self.title = 'M68k Debug'       # Set Widget's Title
        self.setWindowTitle(self.title)
        self.height = 400               # Set Window Width
        self.width = 320                # Set Window Height
        self.log_size = 10              # Set log size
        self.pc = 0                     # Set CPU PC as 0
        self.lines = []

        # CPU Disassembly as List
        self.m68k_debug_disassembly = qtw.QListWidget()
        self.m68k_debug_disassembly.maximumHeight = self.height
        self.m68k_debug_disassembly.maximumWidth = self.width
        self.m68k_debug_disassembly.font = qtg.QFont("Noto Sans Mono", 8)
        self.m68k_debug_disassembly.font.setStyleHint(qtg.QFont.TypeWriter)
        self.m68k_debug_disassembly.setFont(self.m68k_debug_disassembly.font)

        # CPU Register as Label
        self.m68k_regs_status = qtw.QLabel()
        self.m68k_regs_status.maximumHeight = 100
        self.m68k_regs_status.maximumWidth = self.width
        self.m68k_regs_status.font = qtg.QFont("Noto Sans Mono", 8)
        self.m68k_regs_status.font.setStyleHint(qtg.QFont.TypeWriter)
        self.m68k_regs_status.setFont(self.m68k_regs_status.font)

        # Define a Vertical Layout and add
        # CPU Disassembly List and CPU Registers
        self.box = qtw.QVBoxLayout()
        self.box.addWidget(self.m68k_debug_disassembly)
        self.box.addWidget(self.m68k_regs_status)

        # Set Vertical Layout
        self.setLayout(self.box)
        # Set Window position and size
        self.setGeometry(330, 0, self.width, self.height)
        # Set size as fixed
        self.setFixedSize(self.width, self.height)
        # Update and Show
        self.updateGeometry()
        self.show()

    def update(self):
        '''
        Update disassembly and status of the M68k CPU
        '''
        super().update()
        # Set current registers status
        self.m68k_regs_status.setText(self.registers_status())
        # Clear debug list and add new lines of disassembly
        self.m68k_debug_disassembly.clear()
        for line in self.m68k_disassembly():
            self.m68k_debug_disassembly.addItem(qtw.QListWidgetItem(line))

    def registers_status(self):
        '''
        Get M68k Registers status.
        '''
        # Define status empty
        status = ''
        # Define PC as 0
        self.pc = 0
        # Inspect M86K registers in registers list
        for reg_i, register in enumerate(registers):
            # Break an line every 3 registers
            if reg_i % 3 == 0:
                status += '\n'
            # Set register value
            value = core.m68k_get_reg(0, reg_i)
            # Format register name and value as status
            status += '{0}={1:08x} '.format(register, value & 0xffffffff)
            # Define current M68K PC
            if register == 'pc':
                self.pc = value

        return status

    def m68k_disassembly(self):
        '''
        Get M68k Disassembly at current PC.
        '''
        # Defines lines as empty list
        self.lines = []
        # Iterate over log_size n lines
        for i in range(self.log_size):
            # Allocate disassembly buffer
            disassembly = create_string_buffer(2048)
            # Define current PC as old PC for reference
            old_pc = self.pc
            # Define instruction as empty
            instruction = ''
            # Get disassembly and save current M68K PC
            self.pc += core.m68k_disassemble(disassembly, self.pc, 1)
            # Check if old PC is equal setted breakpoint and pause emulation
            # in current instruction
            if old_pc == breakpoint and breakpoint_state:
                global pause_emulation
                pause_emulation = True
                # add a flag at start of the string to identify current
                # instruction
                instruction += '> '
            # Format disassembly and PC addr as string
            instruction += '[0x{:08x}]: {}'.format(
                old_pc, disassembly.value.decode().lower())
            # Append disassembly in list
            self.lines.append(instruction)
        return self.lines

    def get_pc(self):
        '''
            Get Current PC
        '''
        return self.pc

class Z80Debug(qtw.QWidget):
    '''
    A window that shows the current Z80 Disassembly.
    '''

    def __init__(self):
        super().__init__()
        self.title = 'Z80 Debug'       # Set Widget's Title
        self.setWindowTitle(self.title)
        self.height = 400               # Set Window Width
        self.width = 320                # Set Window Height
        self.log_size = 10              # Set log size
        self.pc = 0                     # Set CPU PC as 0

        # CPU Disassembly as List
        self.z80_debug_disassembly = qtw.QListWidget()
        self.z80_debug_disassembly.maximumHeight = self.height
        self.z80_debug_disassembly.maximumWidth = self.width
        self.z80_debug_disassembly.font = qtg.QFont("Noto Sans Mono", 8)
        self.z80_debug_disassembly.font.setStyleHint(qtg.QFont.TypeWriter)
        self.z80_debug_disassembly.setFont(self.z80_debug_disassembly.font)

        # CPU Register as Label
        self.z80_regs_status = qtw.QLabel()
        self.z80_regs_status.maximumHeight = 100
        self.z80_regs_status.maximumWidth = self.width
        self.z80_regs_status.font = qtg.QFont("Noto Sans Mono", 8)
        self.z80_regs_status.font.setStyleHint(qtg.QFont.TypeWriter)
        self.z80_regs_status.setFont(self.z80_regs_status.font)

        # Define a Vertical Layout and add
        # CPU Disassembly List and CPU Registers
        self.box = qtw.QVBoxLayout()
        self.box.addWidget(self.z80_debug_disassembly)
        self.box.addWidget(self.z80_regs_status)

        # Set Vertical Layout
        self.setLayout(self.box)
        # Set Window position and size
        self.setGeometry(650, 0, self.width, self.height)
        # Set size as fixed
        self.setFixedSize(self.width, self.height)
        # Update and Show
        self.updateGeometry()
        self.show()

    def update(self):
        '''
        Update disassembly and status of the M68K CPU
        '''
        super().update()
        # Set current registers status
        self.z80_regs_status.setText(self.registers_status())
        # Clear debug list and add new lines of disassembly
        self.z80_debug_disassembly.clear()
        for line in self.z80_disassembly():
            self.z80_debug_disassembly.addItem(qtw.QListWidgetItem(line))

    def registers_status(self):
        '''
        Get Z80 Registers status.
        '''
        # Define status empty
        status = ''
        # Define PC as 0
        self.pc = 0
        # Inspect M86K registers in registers list
        for reg_i, register in enumerate(z80_registers):
            # Break an line every 3 registers
            if reg_i % 3 == 0:
                status += '\n'
            # Set register value
            value = core.z80_get_reg(reg_i)
            # Format register name and value as status
            status += '{0}={1:08x} '.format(register, value & 0xffffffff)
            # Define current M68K PC
            if register == 'pc':
                self.pc = value

        return status

    def z80_disassembly(self):
        '''
        Get Z80 Disassembly at current PC.
        '''
        # Defines lines as empty list
        lines = []
        # Iterate over log_size n lines
        for i in range(self.log_size):
            # Allocate disassembly buffer
            disassembly = create_string_buffer(2048)
            # Define current PC as old PC for reference
            old_pc = self.pc
            # Define instruction as empty
            instruction = ''
            # Get disassembly and save current Z80 PC
            self.pc += core.z80_disassemble(disassembly, self.pc)
            # Check if old PC is equal setted breakpoint and pause emulation
            # in current instruction
            # if old_pc == breakpoint and breakpoint_state:
            #     global pause_emulation
            #     pause_emulation = True
            #     # add a flag at start of the string to identify current
            #     # instruction
            #     instruction += '> '
            # Format disassembly and PC addr as string
            instruction += '[0x{:08x}]: {}'.format(
                old_pc, disassembly.value.decode().lower())
            # Append disassembly in list
            lines.append(instruction)
        return lines

    def get_pc(self):
        '''
            Get Current PC
        '''
        return self.pc



class BreakpointDebug(qtw.QWidget):
    '''
    A window that shows the current palette on CRAM.
    '''

    def __init__(self):
        super().__init__()
        self.title = 'Breakpoint Debug'     # Set Window Title
        self.setWindowTitle(self.title)
        self.height = 70                    # Set Window Height
        self.width = 240                    # Set Window Size

        # Create Breakpoint Button
        self.bp_button = qtw.QPushButton('BREAK')
        # Create Breakpoint Input for Address Value
        self.bp_input = qtw.QLineEdit()
        self.bp_button.clicked.connect(lambda: self.set_breakpoint())

        # Create Vertical Layout
        self.box = qtw.QVBoxLayout()
        # Add button and input to Vertical Layout
        self.box.addWidget(self.bp_input)
        self.box.addWidget(self.bp_button)
        # Set Window Layout as Vertical Layout
        self.setLayout(self.box)
        # Define window position and size
        self.setGeometry(410, 566, self.width, self.height)
        # Update and Show
        self.show()

    def set_breakpoint(self):
        '''
        Set PC breakpoint
        '''
        # Define breakpoint as global for external access
        global breakpoint
        global breakpoint_state
        # If breakpoint address value is not empty and setted
        if self.bp_input.text() != '':
            # Set breakpoint value and enable breakpoint
            breakpoint = int(self.bp_input.text(), 16)
            breakpoint_state = True
        else:
            # If empty set disable breakpoint
            breakpoint_state = False


class Display(qtw.QWidget):
    '''
    Screen Display

    Handle all the other windows and display screen
    '''

    def __init__(self, parent=None):
        super(Display, self).__init__(parent)

        # Set pause_emulation as disabled
        global pause_emulation
        pause_emulation = False

        # Set parent window as attr
        self.parent = parent
        # Define total frames executed as 0
        self.frames = 0
        # Define turbo as disabled
        self.turbo = False
        # Set screen buffers in VDP
        self.set_vdp_buffers()
        self.set_audio_buffer()
        # Define frame elapsed times (fps) as deque
        self.frame_times = deque([20], 1000)

        # Start a timer for run frames every 16ms
        timer = qt.QTimer(self)
        timer.timeout.connect(self.frame)
        timer.setInterval(16)
        timer.start()
        self.timer = timer

        audio_format = qtm.QAudioFormat();
        audio_format.setSampleRate(44100)
        audio_format.setChannelCount(2)
        audio_format.setSampleSize(32)
        audio_format.setCodec("audio/pcm")
        audio_format.setByteOrder(qtm.QAudioFormat.LittleEndian)
        audio_format.setSampleType(qtm.QAudioFormat.SignedInt)
        self.audio_output = qtm.QAudioOutput(audio_format, self)
        self.audio_output.setVolume(1)
        self.audio_output.setBufferSize(audio_buffer_size)
        self.sample = qt.QBuffer()
        # Define last_fps_time as current time in timer
        self.last_fps_time = qt.QTime.currentTime()

        # Initialize debugger windows
        self.cram_debug = CramDebug()
        self.vram_debug = VramDebug()
        self.m68k_debug = M68kDebug()
        self.z80_debug =  Z80Debug()
        self.bp_debug = BreakpointDebug()

        # Set display placeholder
        self.label = qtw.QLabel(
            "<h1 style='color: #03a9f4'>KAISER</h1>"
            + "<span style='color: #eceff1'>Debugger<span>")
        self.label.setAlignment(qt.Qt.AlignCenter)
        self.label.setStyleSheet(
            "background-color: #212121; color: #424242; font-family: Verdana")
        self.label.show()

        # Start Frame
        self.frame()

        # Create a Grid Layout
        self.layout = qtw.QGridLayout()
        self.layout.setRowMinimumHeight(0, 240)
        self.layout.setColumnMinimumWidth(1, 400)
        # Add Screen Display to Grid Layout
        self.layout.addWidget(self.label, 0, 1, 1, 2)
        # Set no margins on Grid Layout
        self.layout.setContentsMargins(0, 0, 0, 0)
        # Set Grid Layout as Window Layout
        self.setLayout(self.layout)
        # Adjust Window Size
        self.adjustSize()
        # Create Window Menus
        self.create_menus()

    def create_menus(self):
        '''
        Create the menu bar.
        '''
        # Create menu bar
        self.menubar = self.parent.menuBar()
        # Add main menus
        menu_file = self.menubar.addMenu('&File')
        menu_options = self.menubar.addMenu('&Options')
        menu_dump = self.menubar.addMenu('&Dump')
        menu_m68k = self.menubar.addMenu('&Debug')
        # Add child menus for File
        menu_file_open = qtw.QAction('Load Cartridge', self)
        menu_file_open.triggered.connect(self.load_cartridge)
        menu_file.addAction(menu_file_open)
        menu_file.addSeparator()
        menu_file_pause = qtw.QAction('Pause emulation', self)
        menu_file_pause.triggered.connect(self.pause_emulation)
        menu_file_pause.setShortcut(qtg.QKeySequence(qt.Qt.Key_Escape))
        menu_file.addAction(menu_file_pause)
        menu_file_reset = qtw.QAction('Reset emulation', self)
        menu_file_reset.triggered.connect(self.reset_emulation)
        menu_file_reset.setShortcut(qtg.QKeySequence.Refresh)
        menu_file.addAction(menu_file_reset)
        menu_file_turbo = qtw.QAction('Turbo', self)
        menu_file_turbo.triggered.connect(self.turbo_emulation)
        menu_file_turbo.setShortcut(qtg.QKeySequence(qt.Qt.Key_Space))
        menu_file.addAction(menu_file_turbo)
        menu_file.addSeparator()
        menu_file_quit = qtw.QAction('Quit', self)
        menu_file_quit.triggered.connect(self.quit)
        menu_file_quit.setShortcut(qtg.QKeySequence.Quit)
        menu_file.addAction(menu_file_quit)
        # Add child menus for Options
        menu_options_screenshot = qtw.QAction('Save screenshot', self)
        menu_options_screenshot.triggered.connect(self.take_screenshot)
        menu_options_screenshot.setShortcut(qtg.QKeySequence(qt.Qt.Key_Tab))
        menu_options.addAction(menu_options_screenshot)
        # Add child menus for Dumps
        menu_dump_vram = qtw.QAction('Dump VRAM', self)
        menu_dump_vram.triggered.connect(lambda: self.vram_debug.dump())
        menu_dump.addAction(menu_dump_vram)
        menu_dump_cram = qtw.QAction('Dump CRAM', self)
        menu_dump_cram.triggered.connect(lambda: self.cram_debug.dump())
        menu_dump.addAction(menu_dump_cram)
        # Add child menus for M68k Debug
        menu_m68k_step = qtw.QAction('Step Frame', self)
        menu_m68k_step.triggered.connect(lambda: self.step_frame())
        menu_m68k_step.setShortcut(qtg.QKeySequence(qt.Qt.Key_F7))
        menu_m68k.addAction(menu_m68k_step)

    @qt.pyqtSlot()
    def quit(self):
        '''
        Quit Function
        '''
        app.quit()

    @qt.pyqtSlot()
    def show_Joypads(self):
        self.Joypads_window = Joypads()
        self.Joypads_window.show()

    def set_vdp_buffers(self):
        '''
        Allocate buffer for upscaled image and set the VDP buffers.
        '''
        global scaled_buffer
        scaled_buffer = create_string_buffer(
            320*240*4)
        core.sega3155313_set_buffers(screen_buffer, scaled_buffer)

    def set_audio_buffer(self):
        '''
        Allocate buffer for audio Stream.
        '''
        global audio_buffer
        audio_buffer = create_string_buffer(1080 * 2)
        core.ym2612_set_buffer(audio_buffer)

    @qt.pyqtSlot()
    def load_cartridge(self):
        '''
        Load a Cartridge.
        '''
        global cycle_counter
        # Open File dialog
        selected_file, _ = qtw.QFileDialog.getOpenFileName(
            self, "Load Cartridge", os.getcwd(), "Sega Genesis Cartridge dump (*.bin *.gen *.zip *.md)")
        if not selected_file:
            return
        self.cartridge = Cartridge(selected_file)
        self.cartridge.load()

        # Power ON M68K CPU
        core.power_on()
        # Reset M68K CPU
        core.reset_emulation()
        # Activate Screen Display
        self.activateWindow()

    @qt.pyqtSlot()
    def reset_emulation(self):
        '''
        Perform emulation reset
        '''
        self.reset_emulation = True

    @qt.pyqtSlot()
    def pause_emulation(self):
        '''
        Perform emulation pause
        '''
        global pause_emulation
        global breakpoint_state
        pause_emulation = not pause_emulation
    
    def turbo_emulation(self):
        '''
        Increase Emulation speed
        decrementing display iteration timer value
        '''
        if pause_emulation:
            core.m68k_execute(7)
        else:
            self.turbo = not self.turbo
            self.timer.setInterval(
                4 if self.turbo else 16)

    @qt.pyqtSlot()
    def take_screenshot(self):
        '''
        Take a game screenshot
        '''
        # Check if screenshots folder exists
        try:
            os.mkdir(screenshot_dir)
        except OSError:
            pass
        # List all screenshots in screenshots folder
        file_list = os.listdir(screenshot_dir)
        max_index = 0
        # Find the screenshot with the highest index
        for fn in file_list:
            match = re.match('screenshot([0-9]+).png', fn)
            if match:
                index = int(match.group(1))
                if index > max_index:
                    max_index = index
        # Set screenshot filename with index
        filename = os.path.join(
            screenshot_dir, 'screenshot{:04d}.png'.format(max_index+1))
        # Copy raw screen buffer to QImage
        tempbuffer = scaled_buffer.raw
        image = qtg.QImage(tempbuffer, 320,
                           240, qtg.QImage.Format_RGB32).copy()
        # Save QImage as file
        image.save(filename)
        return filename

    def get_fps(self):
        '''
        Get current display FPS
        '''
        from itertools import islice
        # Define values as list
        values = []
        # Iterate over times
        for last_n in (1000, 100, 20):
            start = max(0, len(self.frame_times)-last_n)
            l = len(self.frame_times)-start
            q = islice(self.frame_times, start, None)
            values.append('{:.1f}'.format(1000.0/sum(q)*l))
        return ' '.join(values)

    def step_frame(self):
        '''
        Do a step frame.

        Perform a Megadrive frame, upscale the screen if necessary.
        Update debug information if debug is on.

        Only executes if a cartridge is loaded
        '''
        if hasattr(self, 'cartridge'):
            global cycle_counter
            self.pause_emulation = True
            core.frame()
            self.frames+=1
            # Update Debug Windows
            self.cram_debug.update()
            self.vram_debug.update()
            self.m68k_debug.update()
            self.z80_debug.update()

            # Set scale filter as None and Zoom Level 1
            core.scale_filter('None', 1)

            # Blit Screen
            blit_screen(self.label, scaled_buffer, 1)

            # Play audio
            self.audio_output.start(self.sample)

            # Adjust Display and MainWindow size
            # for the new screen buffer
            self.adjustSize()
            self.parent.adjustSize()

            # Append FPS to frame_times
            self.frame_times.append(
                self.last_fps_time.msecsTo(qt.QTime.currentTime()))
            # Get current time of FPS check
            self.last_fps_time = qt.QTime.currentTime()

            cycle_counter = core.get_cycle_counter()
            self.pause_emulation = True

    def frame(self):
        '''
        Do a single frame.

        Perform a Megadrive frame, upscale the screen if necessary.
        Update debug information if debug is on.

        Only executes if a cartridge is loaded
        '''
        if hasattr(self, 'cartridge'):
            global cycle_counter

            # If reset state is set perform reset CPU
            if self.reset_emulation:
                core.m68k_pulse_halt()
                core.reset_emulation()
                self.reset_emulation = False

            # If pause state is set perform pause CPU
            # And execute 1 frame
            if not pause_emulation:
                core.frame()
                self.frames += 1

            # Update Debug Windows
            self.cram_debug.update()
            self.vram_debug.update()
            self.m68k_debug.update()
            self.z80_debug.update()

            # Set scale filter as None and Zoom Level 1
            core.scale_filter('None', 1)

            # Blit Screen
            blit_screen(self.label, scaled_buffer, 1)

            # Play audio
            # self.sample.close()
            # self.sample.setData(audio_buffer)
            # self.sample.open(qt.QIODevice.ReadOnly)
            # self.sample.seek(0)
            # self.audio_output.start(self.sample)
            #print(audio_buffer[0:10])

            # Adjust Display and MainWindow size
            # for the new screen buffer
            self.adjustSize()
            self.parent.adjustSize()

            # Append FPS to frame_times
            self.frame_times.append(
                self.last_fps_time.msecsTo(qt.QTime.currentTime()))
            # Get current time of FPS check
            self.last_fps_time = qt.QTime.currentTime()

            cycle_counter = core.get_cycle_counter()

            # If cartridge is loaded display Status
            # with current FPS and Cycles runned
            if statusbar and hasattr(self, 'cartridge'):
                if self.frames % 2:
                    statusbar.showMessage('Frame: {} (fps: {}) (cycles: {})'.format(
                        self.frames, self.get_fps(), cycle_counter
                    ))
        else:
            pass


class MainWindow(qtw.QMainWindow):
    """
    Main Window
    """

    def __init__(self, *args, **kwargs):
        qtw.QMainWindow.__init__(self, *args, **kwargs)
        self.display = Display(self)
        self.setCentralWidget(self.display)
        self.setWindowTitle("Kaiser")
        global statusbar
        statusbar = qtw.QStatusBar(self)
        self.setStatusBar(statusbar)
        self.setGeometry(0, 460, 320, 240)

    def keyPressEvent(self, event):
        '''
        On key press on Main Window interpret as
        Joysticks and if not a joypad key map as Shortcut
        '''
        try:
            key, pad = keymap_r[event.key()]
            core.sega3155345_pad_press_button(pad, buttons.index(key))
        except KeyError:
            super(MainWindow, self).keyPressEvent(event)

    def keyReleaseEvent(self, event):
        '''
        On key release on Main Window interpret as
        Joysticks and if not a joypad key map as Shortcut
        '''
        try:
            key, pad = keymap_r[event.key()]
            core.sega3155345_pad_release_button(pad, buttons.index(key))
        except KeyError:
            super(MainWindow, self).keyReleaseEvent(event)

    def closeEvent(self, event):
        event.accept()
        app.quit()


def blit_screen(label, scaled_buffer, zoom_level):
    '''
    Blits the screen to a QLabel.

    Creates a QImage from a RGB32 formatted buffer, creates a QPixmap from the QImage
    and loads the pixmap into a QLabel.
    '''
    image = qtg.QImage(scaled_buffer, 320*zoom_level, 240 *
                       zoom_level, qtg.QImage.Format_RGB32)
    pixmap = qtg.QPixmap.fromImage(image)

    label.setPixmap(pixmap)


if __name__ == '__main__':
    '''
    Main Program
    '''

    # Parse arguments from command line

    # Starts Gui
    app = qtw.QApplication(sys.argv)
    main_window = MainWindow()
    main_window.show()
    main_window.raise_()
    app.exec_()
