import streamlit as st
import numpy as np
import socket
import json
import threading
import time
import pyaudio
import cv2
from PIL import Image

# -------------------------
# Streamlit Configuration
# -------------------------
st.set_page_config(layout="wide", page_title="PianoLens")

# -------------------------
# Constants & Config (Reused)
# -------------------------
# Visual key sizes (base)
BASE_WHITE_KEY_WIDTH = 80  # Increased for single octave visibility
BASE_WHITE_KEY_HEIGHT = 250
BASE_BLACK_KEY_WIDTH = 45
BASE_BLACK_KEY_HEIGHT = 150

# Colors (BGR for OpenCV/numpy drawing)
WHITE_KEY_COLOR = (255, 255, 255)  # White
BLACK_KEY_COLOR = (0, 0, 0)  # Black
PRESSED_WHITE_COLOR = (255, 200, 100)  # Light Blue/Pressed Color
PRESSED_BLACK_COLOR = (150, 100, 50)  # Dark Blue/Pressed Color
TEXT_COLOR = (0, 0, 255)

# Notes / octaves
# Using the full pattern, C to C will require logic to stop the pattern after 7 white keys + final C.
FULL_OCTAVE_PATTERN = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']

# MIDI Mapping (From original code)
NOTE_TO_MIDI = {
    'C': 60, 'C#': 61, 'Db': 61,
    'D': 62, 'D#': 63, 'Eb': 63,
    'E': 64,
    'F': 65, 'F#': 66, 'Gb': 66,
    'G': 67, 'G#': 68, 'Ab': 68,
    'A': 69, 'A#': 70, 'Bb': 70,
    'B': 71,
}

# Audio config (From original code)
SAMPLE_RATE = 44100
FRAME_SIZE = 1024
OUTPUT_AMPLITUDE = 0.35

# Server configuration (From original code)
HOST = '192.168.137.1'
PORT = 5050

# Global Octave to display and play
WHITE_KEYS = ['C', 'D', 'E', 'F', 'G', 'A', 'B']
BLACK_KEYS = ['C#', 'D#', 'F#', 'G#', 'A#']


# -------------------------
# Utility functions (Reused)
# -------------------------
def midi_to_freq(midi_note):
    """Convert a MIDI note number to its corresponding frequency in Hz."""
    return 440.0 * (2.0 ** ((midi_note - 69) / 12.0))

def remap_incoming_notes(white_str, black_str, base_octave):
    """
    Incoming payload uses ABSOLUTE notes, e.g. 'C4 D4' or 'C#4'.
    We remap them so that:
      - Everything in octave 4 (C4..B4) -> base_octave
      - Everything in octave 5 (C5)     -> base_octave + 1

    So hardware stays fixed at C4–C5, but UI can be C4–C5, C5–C6, etc.
    """
    notes = set()

    def parse_and_remap(token: str):
        token = token.strip()
        if not token:
            return None

        # Split into name (C, C#, D, etc.) and octave digits
        i = 0
        while i < len(token) and not token[i].isdigit():
            i += 1
        if i == 0 or i == len(token):
            return None

        name = token[:i]         # e.g. 'C' or 'C#'
        octave_str = token[i:]   # e.g. '4' or '5'

        try:
            orig_oct = int(octave_str)
        except ValueError:
            return None

        # We expect orig_oct to be 4 or 5 (C4..B4, C5)
        # Map relative to base_octave:
        #  orig_oct == 4 -> base_octave
        #  orig_oct == 5 -> base_octave + 1
        delta = orig_oct - 4
        if delta < 0:
            delta = 0  # clamp just in case
        new_oct = base_octave + delta
        return f"{name}{new_oct}"

    for s in (white_str, black_str):
        if not s:
            continue
        for tok in s.split():
            mapped = parse_and_remap(tok)
            if mapped:
                notes.add(mapped)

    return notes


# -------------------------
# Audio: ActiveNote + Mixer (Reused/Simplified)
# -------------------------
class ActiveNote:
    """A single voice with phase, envelope state, and harmonic content."""

    def __init__(self, midi_note, sample_rate=SAMPLE_RATE, duration=10.0):
        self.phase = 0.0

        # Envelope parameters (seconds) - NOW PULLING FROM SESSION STATE
        self.attack = st.session_state.get('attack', 0.005)
        self.decay = st.session_state.get('decay', 0.08)
        self.sustain_level = st.session_state.get('sustain_level', 0.7)
        self.release = st.session_state.get('release', 0.07)  # Using your recent change

        self.midi = midi_note
        self.freq = midi_to_freq(midi_note)
        self.sample_rate = sample_rate
        # internal counters (samples)
        self.total_samples_consumed = 0
        self.release_requested = False
        self.released_samples = 0
        self.is_finished = False
        self.max_duration_samples = int(duration * sample_rate)
        self.amp = 1.0

    def request_release(self):
        self.release_requested = True

    def _estimate_amplitude_at_release(self, sr, a, d):
        rel_start_sample = self.total_samples_consumed
        if rel_start_sample < a and a > 0:
            return (rel_start_sample / a) ** 2
        elif rel_start_sample < a + d and d > 0:
            frac = (rel_start_sample - a) / max(1.0, d)
            return 1.0 + (self.sustain_level - 1.0) * frac
        else:
            return self.sustain_level

    def generate(self, n_samples):
        if self.is_finished:
            return np.zeros(n_samples, dtype=np.float32), True

        sr = self.sample_rate
        idx = np.arange(n_samples, dtype=np.float32)
        t = (self.phase + idx) / sr

        # Simple harmonic-rich tone
        base = np.sin(2 * np.pi * self.freq * t)
        base += 0.45 * np.sin(2 * np.pi * self.freq * 2 * t)
        base += 0.12 * np.sin(2 * np.pi * self.freq * 3 * t)
        base += 0.06 * np.sin(2 * np.pi * self.freq * 4 * t)

        # Envelope calculation per-sample (ADSR logic)
        env = np.zeros(n_samples, dtype=np.float32)
        start_idx = self.total_samples_consumed
        sample_indices = start_idx + idx

        a = int(self.attack * sr)
        d = int(self.decay * sr)
        r = int(self.release * sr)

        if not self.release_requested:
            # Attack
            if a > 0:
                attack_mask = sample_indices < a
                if np.any(attack_mask):
                    env[attack_mask] = (sample_indices[attack_mask] / max(1.0, a)) ** 2
            # Decay
            decay_start = a
            decay_end = a + d
            if d > 0:
                decay_mask = (sample_indices >= decay_start) & (sample_indices < decay_end)
                if np.any(decay_mask):
                    frac = (sample_indices[decay_mask] - decay_start) / max(1.0, d)
                    env[decay_mask] = 1.0 + (self.sustain_level - 1.0) * frac
            # Sustain
            sustain_mask = sample_indices >= (a + d)
            if np.any(sustain_mask):
                env[sustain_mask] = self.sustain_level
        else:
            # Release requested
            amplitude_at_release = self._estimate_amplitude_at_release(sr, a, d)
            rel_offset = self.released_samples + idx
            if r > 0:
                frac = rel_offset / r
                frac = np.clip(frac, 0.0, 1.0)
                env = amplitude_at_release * (1.0 - frac ** 2)
            else:
                env = np.zeros(n_samples, dtype=np.float32)

            self.released_samples += n_samples
            if self.released_samples >= r:
                self.is_finished = True

        out = base * env * self.amp
        self.phase += n_samples
        self.total_samples_consumed += n_samples

        if self.total_samples_consumed >= self.max_duration_samples:
            self.is_finished = True

        return out.astype(np.float32), self.is_finished

class PolyphonicMixer:
    def __init__(self, sample_rate=SAMPLE_RATE, frame_size=FRAME_SIZE, amplitude=OUTPUT_AMPLITUDE):
        self.sample_rate = sample_rate
        self.frame_size = frame_size
        self.amplitude = amplitude

        self.notes = {}
        self.notes_lock = threading.Lock()
        self.p = None
        self.stream = None
        self.running = False
        self.thread = None

    def parse_note_str(self, note_str):
        note_str = note_str.strip()
        if len(note_str) < 2: return None

        if '#' in note_str:
            idx = note_str.index('#')
            note_name = note_str[:idx + 1]
            octave_str = note_str[idx + 1:]
        else:
            note_name = note_str[0]
            octave_str = note_str[1:]

        if note_name not in NOTE_TO_MIDI: return None
        try:
            octave = int(octave_str)
        except:
            return None

        base_midi = NOTE_TO_MIDI[note_name]
        midi_note = base_midi + (octave - 4) * 12
        return midi_note

    def note_on(self, note_name):
        with self.notes_lock:
            midi = self.parse_note_str(note_name)
            if midi is None: return

            if note_name in self.notes:
                note_obj = self.notes[note_name]
                if note_obj.release_requested:
                    note_obj.release_requested = False
                    note_obj.released_samples = 0
                    note_obj.is_finished = False
                return

            note_obj = ActiveNote(midi_note=midi, sample_rate=self.sample_rate, duration=10.0)
            self.notes[note_name] = note_obj

    def note_off(self, note_name):
        with self.notes_lock:
            if note_name in self.notes:
                self.notes[note_name].request_release()

    def _audio_loop(self):
        while self.running:
            buffer = np.zeros(self.frame_size, dtype=np.float32)
            finished_keys = []
            with self.notes_lock:
                for key, note in list(self.notes.items()):
                    out_chunk, finished = note.generate(self.frame_size)
                    buffer += out_chunk
                    if finished:
                        finished_keys.append(key)

                for k in finished_keys:
                    try:
                        del self.notes[k]
                    except KeyError:
                        pass

            # Normalize/limit amplitude
            peak = np.max(np.abs(buffer))
            if peak > 0:
                target = self.amplitude
                if peak > target:
                    buffer = buffer * (target / peak)
                else:
                    buffer = buffer * (target / 1.0)

            try:
                if self.stream is not None:
                    self.stream.write(buffer.tobytes())
            except Exception as e:
                # print(f"[Mixer] stream write error: {repr(e)}")
                time.sleep(0.01)

    def start_audio(self):
        if self.running: return
        self.p = pyaudio.PyAudio()
        self.stream = self.p.open(
            format=pyaudio.paFloat32,
            channels=1,
            rate=self.sample_rate,
            output=True,
            frames_per_buffer=self.frame_size
        )
        self.running = True
        self.thread = threading.Thread(target=self._audio_loop, daemon=True)
        self.thread.start()

    def cleanup(self):
        self.running = False
        if self.thread and self.thread.is_alive():
            self.thread.join(timeout=1.0)
        try:
            if self.stream:
                if self.stream.is_active(): self.stream.stop_stream()
                self.stream.close()
            if self.p:
                self.p.terminate()
        except Exception:
            pass

# -------------------------
# Piano drawing & logic (Adapted for Streamlit)
# -------------------------
class Piano:
    def __init__(self, num_octaves, scale, start_x, start_y, base_octave):
        self.num_octaves = num_octaves
        self.scale = scale
        self.start_x = start_x
        self.start_y = start_y
        self.base_octave = base_octave
        self.white_key_rects = []
        self.black_key_rects = []
        self.pressed_keys = set()
        self.previously_pressed = set()
        self.build_piano()

    def set_base_octave(self, new_octave):
        if self.base_octave != new_octave:
            self.base_octave = new_octave
            self.build_piano()

    def build_piano(self):
        self.white_key_rects = []
        self.black_key_rects = []

        white_key_width = int(BASE_WHITE_KEY_WIDTH * self.scale)
        white_key_height = int(BASE_WHITE_KEY_HEIGHT * self.scale)
        black_key_width = int(BASE_BLACK_KEY_WIDTH * self.scale)
        black_key_height = int(BASE_BLACK_KEY_HEIGHT * self.scale)

        white_key_index = 0
        current_octave = self.base_octave

        # We will iterate through the keys for one octave (C to B) and then add the final C
        for _ in range(self.num_octaves):
            for i, note in enumerate(FULL_OCTAVE_PATTERN):
                is_black = '#' in note

                if not is_black:
                    # White keys C, D, E, F, G, A, B
                    x = self.start_x + white_key_index * white_key_width
                    rect = {
                        'x': x, 'y': self.start_y, 'w': white_key_width, 'h': white_key_height,
                        'name': f"{note}{current_octave}"
                    }
                    self.white_key_rects.append(rect)
                    white_key_index += 1
                else:
                    # Black keys
                    x = self.start_x + (white_key_index - 1) * white_key_width + white_key_width - black_key_width // 2
                    rect = {
                        'x': x, 'y': self.start_y, 'w': black_key_width, 'h': black_key_height,
                        'name': f"{note}{current_octave}"
                    }
                    self.black_key_rects.append(rect)

            # --- C to C Logic ---
            # Add the final C note (C one octave up) after the B note (white_key_index is now 7)
            final_c_octave = current_octave + 1
            x = self.start_x + white_key_index * white_key_width
            rect = {
                'x': x, 'y': self.start_y, 'w': white_key_width, 'h': white_key_height,
                'name': f"C{final_c_octave}"
            }
            self.white_key_rects.append(rect)
            # white_key_index is now 8 total keys (C to C)

            # The loop only runs once since num_octaves=1
            current_octave += 1

    def update_and_sync_audio(self, mixer):
        newly_pressed = self.pressed_keys - self.previously_pressed
        released = self.previously_pressed - self.pressed_keys

        for key_name in newly_pressed:
            mixer.note_on(key_name)

        for key_name in released:
            mixer.note_off(key_name)

        self.previously_pressed = self.pressed_keys.copy()

    def draw(self, frame_size):
        frame = np.zeros((frame_size[1], frame_size[0], 3), dtype=np.uint8)
        frame[:] = (28, 17, 14)

        overlay = frame.copy()

        # Draw white keys with translucency
        for key in self.white_key_rects:
            color = PRESSED_WHITE_COLOR if key['name'] in self.pressed_keys else WHITE_KEY_COLOR
            cv2.rectangle(overlay, (key['x'], key['y']), (key['x'] + key['w'], key['y'] + key['h']), color, -1)
            cv2.rectangle(frame, (key['x'], key['y']), (key['x'] + key['w'], key['y'] + key['h']), (0, 0, 0), 2)

            # Add key label
            note_name = key['name']
            text_size = cv2.getTextSize(note_name, cv2.FONT_HERSHEY_SIMPLEX, 0.5 * self.scale, 2)[0]
            text_x = key['x'] + (key['w'] - text_size[0]) // 2
            text_y = key['y'] + key['h'] - int(20 * self.scale)
            cv2.putText(frame, note_name, (text_x, text_y), cv2.FONT_HERSHEY_SIMPLEX, 0.5 * self.scale, TEXT_COLOR, 2)

        # Blend overlay with original frame for translucency (alpha 0.6)
        cv2.addWeighted(overlay, 0.6, frame, 0.4, 0, frame)

        # Draw black keys (opaque)
        for key in self.black_key_rects:
            color = PRESSED_BLACK_COLOR if key['name'] in self.pressed_keys else BLACK_KEY_COLOR
            cv2.rectangle(frame, (key['x'], key['y']), (key['x'] + key['w'], key['y'] + key['h']), color, -1)
            cv2.rectangle(frame, (key['x'], key['y']), (key['x'] + key['w'], key['y'] + key['h']), (0, 0, 0), 2)

            # Add key label
            note_name = key['name']
            text_size = cv2.getTextSize(note_name, cv2.FONT_HERSHEY_SIMPLEX, 0.5 * self.scale, 2)[0]
            text_x = key['x'] + (key['w'] - text_size[0]) // 2
            text_y = key['y'] + key['h'] - int(20 * self.scale)
            cv2.putText(frame, note_name, (text_x, text_y), cv2.FONT_HERSHEY_SIMPLEX, 0.5 * self.scale, TEXT_COLOR, 2)

        return frame

# -------------------------
# Network Receiver (Adapted for Streamlit)
# -------------------------
class NoteReceiver:
    def __init__(self):
        self.current_notes = set()
        self.running = False
        self.connected = False
        self.server_socket = None
        self.client_socket = None
        self.lock = threading.Lock()
        self.thread = None

    def start_server(self, octave):
        if self.running: return

        self.running = True
        self.thread = threading.Thread(target=self._server_loop, args=(octave,), daemon=True)
        self.thread.start()

    def _server_loop(self, octave):
        try:
            # --- Setup listening socket ---
            self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.server_socket.settimeout(1.0)
            self.server_socket.bind((HOST, PORT))
            self.server_socket.listen(1)
            print(f"Listening for connections on {HOST}:{PORT}...")

            # --- Accept loop (with timeout so we can exit cleanly) ---
            while self.running and not self.connected:
                try:
                    self.client_socket, addr = self.server_socket.accept()
                    self.client_socket.settimeout(1.0)
                    print(f"Connected to: {addr}")
                    self.connected = True
                except socket.timeout:
                    if not self.running:
                        break
                    continue

            # --- If we never connected, just bail out ---
            if not self.connected:
                return

            # --- Receiving loop ---
            buffer = ""
            while self.running and self.connected:
                try:
                    data = self.client_socket.recv(1024)
                    if not data:
                        print("Connection closed by sender")
                        break

                    chunk = data.decode('utf-8')
                    # Debug: see exactly what came in
                    print("RX:", repr(chunk))

                    buffer += chunk

                    # 1) Handle newline-delimited JSON messages
                    while "\n" in buffer:
                        line, buffer = buffer.split("\n", 1)
                        line = line.strip()
                        if not line:
                            continue

                        try:
                            message = json.loads(line)
                        except json.JSONDecodeError as e:
                            print(f"JSON decode error (line): {e} | line={repr(line)}")
                            continue

                        black = message.get("black", "")
                        white = message.get("white", "")

                        # notes_pressed = map_indices_to_notes(white, black, octave)
                        notes_pressed = remap_incoming_notes(white, black, octave)
                        with self.lock:
                            self.current_notes = set(notes_pressed)

                    # 2) If there is no newline at all, but buffer looks like a complete JSON object
                    stripped = buffer.strip()
                    if stripped:
                        try:
                            message = json.loads(stripped)
                        except json.JSONDecodeError:
                            # Not yet a full JSON object, wait for more data
                            pass
                        else:
                            # Successfully parsed a full JSON in one chunk
                            black = message.get("black", "")
                            white = message.get("white", "")

                            # notes_pressed = map_indices_to_notes(white, black, octave)
                            notes_pressed = remap_incoming_notes(white, black, octave)
                            with self.lock:
                                self.current_notes = set(notes_pressed)

                            buffer = ""  # Clear after successful parse

                except socket.timeout:
                    # Just loop back and check self.running/self.connected again
                    continue
                except Exception as e:
                    print(f"Receiver socket error: {e}")
                    break

        except Exception as e:
            if self.running:
                print(f"Receiver error: {e}")
        finally:
            self.connected = False
            self.running = False
            self._stop_sockets()
            print("Receiver thread finished.")



    def get_notes(self):
        with self.lock:
            return self.current_notes.copy()

    def _stop_sockets(self):
        if self.client_socket:
            try:
                self.client_socket.close()
            except:
                pass
            self.client_socket = None
        if self.server_socket:
            try:
                self.server_socket.close()
            except:
                pass
            self.server_socket = None

    def stop(self):
        self.running = False
        # Create a dummy connection to unblock accept() if it's waiting
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((HOST, PORT))
            s.close()
        except socket.error:
            pass
        if self.thread and self.thread.is_alive():
            self.thread.join(timeout=1.0)
        self._stop_sockets()

# -------------------------
# Streamlit Main Application
# -------------------------
# Use Streamlit's resource caching to run the single-threaded audio mixer and server
@st.cache_resource(show_spinner=False)
def initialize_systems():
    """Initializes the audio mixer and the network receiver (only runs once)."""
    mixer = PolyphonicMixer()
    receiver = NoteReceiver()
    st.session_state.server_running = False
    return mixer, receiver


mixer, receiver = initialize_systems()

# Initialize session state for the piano settings
if 'base_octave' not in st.session_state:
    st.session_state.base_octave = 4  # Start at C4
if 'server_running' not in st.session_state:
    st.session_state.server_running = False

# --- NEW ADSR INITIALIZATION ---
if 'attack' not in st.session_state:
    st.session_state.attack = 0.005
if 'decay' not in st.session_state:
    st.session_state.decay = 0.08
if 'sustain_level' not in st.session_state:
    st.session_state.sustain_level = 0.7
if 'release' not in st.session_state:
    st.session_state.release = 0.07 # Set default release to the desired snappier value

# Cleanup function to be run on script exit/restart
def on_session_end():
    receiver.stop()
    mixer.cleanup()
    print("All systems cleaned up.")


# --- UI Layout ---
st.markdown("<h1 style='text-align: center; color: white;'>🎹 PianoLens: Camera based Virtual Piano</h1>",
            unsafe_allow_html=True)

# --- Add this line for a nice separator ---
st.divider()
# Invert columns: Piano (4) on Left, Controls (1) on Right
col_piano, col_control = st.columns([5, 2])


# --- Piano Visualization Column (LEFT) ---
with col_piano:
    # 1. Image and Size Setup (HEIGHT CORRECTED FOR PADDING)
    WINDOW_WIDTH = 700
    WINDOW_HEIGHT = 300  # <--- CORRECTED: 25 (top pad) + 250 (key height) + 25 (bottom pad)

    # Calculate key dimensions for centering
    white_key_width = int(BASE_WHITE_KEY_WIDTH * 1.0)
    # 8 white keys in C to C octave (C, D, E, F, G, A, B, C)
    total_piano_width = white_key_width * 8  # This is 640 (8 * 80)

    # Dynamic Centering Calculation
    PIANO_START_X = (WINDOW_WIDTH - total_piano_width) // 2
    PIANO_START_Y = 25

    # Instantiate or update the piano visualizer
    piano = Piano(
        num_octaves=1,
        scale=1.0,
        start_x=PIANO_START_X,
        start_y=PIANO_START_Y,
        base_octave=st.session_state.base_octave
    )

    # 3. Get Notes and Sync Audio
    if st.session_state.server_running:
        received_notes = receiver.get_notes()
    else:
        received_notes = set()

    piano.pressed_keys = received_notes
    if mixer.running:
        piano.update_and_sync_audio(mixer)

    # 4. Draw the piano keys using OpenCV
    frame = piano.draw((WINDOW_WIDTH, WINDOW_HEIGHT))

    # 5. Display the final image (The Piano)
    st.image(frame, channels="BGR", use_container_width=True)

    # 6. Display the Note Status DIRECTLY BELOW the image

    st.subheader("Server Controls")
    if st.session_state.server_running:
        st.success(f"Server ACTIVE on {HOST}:{PORT}")
        if st.button("🔴 Stop Server", use_container_width=True):
            receiver.stop()
            st.session_state.server_running = False
            st.rerun()
    else:
        st.warning(f"Server INACTIVE. Click to start.")
        if st.button("🟢 Start Server", use_container_width=True):
            mixer.start_audio()
            receiver.start_server(st.session_state.base_octave)
            st.session_state.server_running = True
            st.rerun()
    # Status Display
    if receiver.connected:
        st.info("Network: Connected (Receiving Data)", icon="🔗")
    elif st.session_state.server_running:
        st.info(f"Network: Waiting for Client on {HOST}:{PORT}", icon="⏳")
    else:
        st.info("Network: Server is Stopped", icon="🛑")

# --- Control Column (RIGHT) ---
with col_control:
    st.header("Octave Control (1-7)")
    # 2. Octave Controls
    st.subheader("Octave Control")
    st.metric("Current Octave", st.session_state.base_octave)

    def lower_octave():
        st.session_state.base_octave = max(1, st.session_state.base_octave - 1)
        # Update receiver thread with new octave if running
        if receiver.running:
            receiver.stop()
            time.sleep(0.1)
            mixer.start_audio()  # mixer might have stopped if receiver stopped and was the only active thread
            receiver.start_server(st.session_state.base_octave)

    def increase_octave():
        st.session_state.base_octave = min(8, st.session_state.base_octave + 1)
        # Update receiver thread with new octave if running
        if receiver.running:
            receiver.stop()
            time.sleep(0.1)
            mixer.start_audio()
            receiver.start_server(st.session_state.base_octave)

    col_down, col_up = st.columns(2)
    with col_down:
        st.button("Decrease Octave", on_click=lower_octave, use_container_width=True,
                  disabled=st.session_state.base_octave == 1)
    with col_up:
        st.button("Increment Octave", on_click=increase_octave, use_container_width=True,
                  disabled=st.session_state.base_octave == 7)

    # --- NEW: ADSR Sliders ---
    st.subheader("Synth Envelope (ADSR)")

    # Attack Slider (0.001s to 0.5s)
    st.slider("Attack (s)",
              min_value=0.001, max_value=0.5, value=st.session_state.attack,
              step=0.005, key='attack', format="%.3fs", help="Time taken to reach maximum volume.")

    # Decay Slider (0.01s to 1.0s)
    st.slider("Decay (s)",
              min_value=0.01, max_value=1.0, value=st.session_state.decay,
              step=0.01, key='decay', format="%.2fs", help="Time taken to drop from peak to Sustain level.")

    # Sustain Slider (0.0 to 1.0)
    st.slider("Sustain Level",
              min_value=0.0, max_value=1.0, value=st.session_state.sustain_level,
              step=0.05, key='sustain_level', help="The volume level held while the note is pressed.")

    # Release Slider (0.01s to 1.0s)
    st.slider("Release (s)",
              min_value=0.01, max_value=1.0, value=st.session_state.release,
              step=0.01, key='release', format="%.2fs", help="Time taken to fade to silence after key release.")

# -------------------------
# Periodic Update
# -------------------------
# Set up a continuous loop to check for new notes and update the screen
if st.session_state.server_running:
    time.sleep(0.05)
    st.rerun()