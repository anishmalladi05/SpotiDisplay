import time
import subprocess
import urllib.parse
import urllib.request

ESP32_IP = "http://10.0.0.224" # Make sure this matches your ESP32's IP!

def get_spotify_info():
    cmd = """
    if application "Spotify" is running then
        tell application "Spotify"
            try
                set pState to player state as string
                set trk to name of current track
                set art to artist of current track
                set pos to player position
                set dur to (duration of current track) / 1000
                return pState & "|||" & trk & "|||" & art & "|||" & pos & "|||" & dur
            on error
                return "NOT_PLAYING"
            end try
        end tell
    else
        return "NOT_RUNNING"
    end if
    """
    try:
        return subprocess.check_output(["osascript", "-e", cmd], timeout=0.5).decode("utf-8").strip()
    except Exception:
        return "ERROR"

def send_spotify_command(action):
    if action == "PLAY_PAUSE":
        cmd = 'tell application "Spotify" to playpause'
    elif action == "NEXT":
        cmd = 'tell application "Spotify" to next track'
    elif action == "PREV":
        cmd = 'tell application "Spotify" to previous track'
    elif action == "RW10":
        cmd = 'tell application "Spotify" to set player position to (player position - 10)'
    elif action == "FF10":
        cmd = 'tell application "Spotify" to set player position to (player position + 10)'
    else:
        return

    try:
        subprocess.call(["osascript", "-e", cmd], timeout=0.5)
        print(f"⚡ Executed: {action}")
    except:
        pass

def check_touch_action():
    try:
        req = urllib.request.Request(f"{ESP32_IP}/action")
        with urllib.request.urlopen(req, timeout=0.15) as response:
            action = response.read().decode('utf-8').strip()
            if action != "NONE":
                send_spotify_command(action)
    except:
        pass

print("🚀 High-Speed Spotify Display Bridge Active!\n")
last_track = ""

while True:
    # 1. Check touch actions immediately for zero latency response
    check_touch_action()
    
    # 2. Fetch current track details from macOS
    info = get_spotify_info()

    if "|||" in info:
        pState, track, artist, pos, dur = info.split("|||")
        is_playing = "true" if pState == "playing" else "false"

        # Only send full updates if something changed or every couple loops to save bandwidth
        params = urllib.parse.urlencode({
            'track': track,
            'artist': artist,
            'playing': is_playing,
            'pos': int(float(pos)),
            'dur': int(float(dur))
        })
        
        try:
            req = urllib.request.Request(f"{ESP32_IP}/update?{params}")
            urllib.request.urlopen(req, timeout=0.2)
        except:
            pass

    # Tight sleep loop for lightning-fast button response times
    time.sleep(0.1)
