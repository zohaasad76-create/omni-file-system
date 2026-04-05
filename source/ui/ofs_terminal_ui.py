
import curses
import socket
import json
import textwrap
SERVER_HOST = "localhost"
SERVER_PORT = 8080
RECV_BUF = 16384

class OFSClient:
    def __init__(self, host=SERVER_HOST, port=SERVER_PORT):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((host, port))
    
    def send_command(self, cmd):
        """
        Send a single-line command and return a dict:
         - If server returns valid JSON -> parsed dict
         - Otherwise -> {"status":"success","data": raw_text}
         - On network error -> {"status":"error","error_message": ...}
        """
        try:
           
            self.sock.sendall((cmd + "\n").encode())
            resp = self.sock.recv(RECV_BUF).decode(errors="replace")
           
            try:
                return json.loads(resp)
            except Exception:
                return {"status": "success", "data": resp}
        except Exception as e:
            return {"status": "error", "error_message": str(e)}
    
    def close(self):
        try:
            self.sock.close()
        except:
            pass

def draw_box(win, title=""):
    h, w = win.getmaxyx()
    win.erase()
    win.box()
    if title:
        try:
            win.addstr(0, 2, f" {title} ", curses.A_BOLD | curses.color_pair(2))
        except:
            win.addstr(0, 2, f" {title} ")
    win.refresh()

def message_box(stdscr, title, msg):
    h, w = stdscr.getmaxyx()
    win_h = min(20, max(8, h-4))
    win_w = min(100, max(40, w-6))
    win = curses.newwin(win_h, win_w, h//2-win_h//2, w//2-win_w//2)
    draw_box(win, title)
    lines = []
   
    if isinstance(msg, bytes):
        msg = msg.decode(errors="replace")
    if isinstance(msg, str):
        msg = msg.replace("\\n", "\n")
        lines = msg.split("\n")
    else:
        lines = [str(msg)]
    for i, line in enumerate(lines[:win_h-4]):
        safe = line[:win_w-4]
        try:
            win.addstr(2+i, 2, safe)
        except:
            win.addstr(2+i, 2, "".join(ch for ch in safe if ord(ch) >= 32))
    win.addstr(win_h-2, 2, "Press any key to continue...", curses.color_pair(3))
    win.refresh()
    win.getch()
    win.erase()
    stdscr.touchwin()
    stdscr.refresh()

def normalize_path(path):
    path = path.strip()
    if path == "":
        return "/"
    if not path.startswith("/"):
        path = "/" + path
    while "//" in path:
        path = path.replace("//", "/")
    return path

class Button:
    def __init__(self, y, x, label, color_pair=1):
        self.y = y
        self.x = x
        self.label = label
        self.color_pair = color_pair
    
    def draw(self, win, selected=False):
        try:
            if selected:
                win.attron(curses.color_pair(self.color_pair))
                win.addstr(self.y, self.x, f"[{self.label}]")
                win.attroff(curses.color_pair(self.color_pair))
            else:
                win.addstr(self.y, self.x, f"[{self.label}]")
        except:
            win.addstr(self.y, self.x, f"[{self.label}]")

def input_box(stdscr, prompt, hidden=False):
    curses.curs_set(1)
    h, w = stdscr.getmaxyx()
    win_h, win_w = 5, min(100, max(40, w-6))
    win = curses.newwin(win_h, win_w, h//2 - win_h//2, w//2 - win_w//2)
    draw_box(win, prompt)
    buf = ""
    x, y = 2, 2
    while True:
        win.move(y, x + len(buf))
        win.refresh()
        ch = win.getch()
        if ch in [10, 13]:
            break
        elif ch in [curses.KEY_BACKSPACE, 127, 8]:
            if buf:
                buf = buf[:-1]
        elif ch == 27:
            buf = ""
            break
        elif 0 <= ch < 256:
            buf += chr(ch)
        display = '*'*len(buf) if hidden else buf
        win.addstr(y, x, " "*(win_w-4))
        try:
            win.addstr(y, x, display[:win_w-4])
        except:
            win.addstr(y, x, "".join(ch for ch in display if ord(ch) >= 32)[:win_w-4])
    curses.curs_set(0)
    win.erase()
    stdscr.touchwin()
    stdscr.refresh()
    return buf.strip()

def login_screen(stdscr, client):
    stdscr.clear()
    draw_box(stdscr, "Welcome to FileVerse 🌟  (Use keyboard arrows + Enter)")
    username = input_box(stdscr, "Username:")
    password = input_box(stdscr, "Password:", hidden=True)
    
    if username == "" and password == "":
      
        return ""
    
    resp = client.send_command(f"login {username} {password}")
    if isinstance(resp, dict) and resp.get("status") == "success":
        message_box(stdscr, "Login Successful", f"Welcome {username}!")
        return username
    else:
     
        stdscr.clear()
        draw_box(stdscr, "Authentication")
        h, w = stdscr.getmaxyx()
        stdscr.addstr(h//2, w//2 - 10, "Processing...", curses.color_pair(3))
        stdscr.refresh()
      
        while True:
            stdscr.getch()

def read_file_screen(stdscr, client, filepath, content=""):
    h, w = stdscr.getmaxyx()
    win_h, win_w = min(h-4, max(12, h-6)), min(120, w-4)
    win = curses.newwin(win_h, win_w, 2, 2)
    win.keypad(True)

    if content is None:
        content = ""
    if isinstance(content, bytes):
        content = content.decode(errors="replace")
   
    raw_lines = content.split("\n") if content else [""]
   
    lines = []
    max_line_width = win_w - 6  
    for raw_line in raw_lines:
        if len(raw_line) <= max_line_width:
            lines.append(raw_line)
        else:
          
            while len(raw_line) > max_line_width:
                lines.append(raw_line[:max_line_width])
                raw_line = raw_line[max_line_width:]
            if raw_line:  
                lines.append(raw_line)
    scroll = 0
    close_btn = Button(win_h-3, 2, "Close", color_pair=1)
    while True:
        win.erase()
        draw_box(win, f" Reading: {filepath} (Read-Only) ")
        visible_height = win_h - 6
        visible_lines = lines[scroll:scroll + visible_height]
        for i, line in enumerate(visible_lines):
            safe = line[:win_w-6]
            try:
                win.addstr(2+i, 2, safe)
            except:
                win.addstr(2+i, 2, "".join(ch for ch in safe if ord(ch) >= 32))
      
        close_btn.draw(win, True)
        win.addstr(win_h-2, 2, "Use UP/DOWN to scroll, ENTER/ESC to close", curses.color_pair(3))
        win.refresh()
        ch = win.getch()
        if ch == 27 or ch in [10, 13]:  
            return
        elif ch == curses.KEY_UP:
            scroll = max(0, scroll - 1)
        elif ch == curses.KEY_DOWN:
            scroll = min(len(lines) - visible_height, scroll + 1) if len(lines) > visible_height else scroll
        elif ch == curses.KEY_RESIZE:
            h, w = stdscr.getmaxyx()
            win_h, win_w = min(h-4, max(12, h-6)), min(120, w-4)
            win.resize(win_h, win_w)

def edit_file_screen(stdscr, client, filepath, existing_content=""):
    h, w = stdscr.getmaxyx()
    win_h, win_w = min(h-4, max(12, h-6)), min(120, w-4)
    win = curses.newwin(win_h, win_w, 2, 2)
    win.keypad(True)
    
    if existing_content is None:
        existing_content = ""
    if isinstance(existing_content, bytes):
        existing_content = existing_content.decode(errors="replace")
   
    raw_lines = existing_content.split("\n") if existing_content else [""]
    lines = []
    max_line_width = win_w - 6  
    for raw_line in raw_lines:
        if len(raw_line) <= max_line_width:
            lines.append(raw_line)
        else:
           
            while len(raw_line) > max_line_width:
                lines.append(raw_line[:max_line_width])
                raw_line = raw_line[max_line_width:]
            if raw_line:  
                lines.append(raw_line)
    cursor_y, cursor_x = 0, 0
    scroll = 0
    save_btn = Button(win_h-3, 2, "Save", color_pair=1)
    cancel_btn = Button(win_h-3, 12, "Cancel", color_pair=1)
    selected_btn = -1 
    while True:
        win.erase()
        draw_box(win, f" Editing: {filepath} ")
        visible_height = win_h - 6
        visible_lines = lines[scroll:scroll + visible_height]
        for i, line in enumerate(visible_lines):
            safe = line[:win_w-6]
            try:
                win.addstr(2+i, 2, safe)
            except:
                win.addstr(2+i, 2, "".join(ch for ch in safe if ord(ch) >= 32))
        
        save_btn.draw(win, selected_btn==0)
        cancel_btn.draw(win, selected_btn==1)
        
        if selected_btn == -1:
            win.addstr(win_h-2, 2, "ENTER: Save | ESC: Cancel | Arrows: Move ", curses.color_pair(3))
        else:
            win.addstr(win_h-2, 2, "LEFT/RIGHT: Switch | ENTER: Select | ESC: Back to editing", curses.color_pair(3))
        
        if selected_btn == -1:
            curses.curs_set(1)
            display_y = 2 + (cursor_y - scroll)
            display_x = 2 + cursor_x
            if 0 <= display_y - 2 < visible_height:
                win.move(display_y, min(display_x, win_w-3))
        else:
            curses.curs_set(0)
        win.refresh()
        ch = win.getch()
        if ch == 27:  
            curses.curs_set(0)
            return None
        elif ch == 9:  
            if selected_btn == -1:
                selected_btn = 0 
            else:
                selected_btn = -1 
        elif selected_btn >= 0:  
            if ch == curses.KEY_LEFT:
                selected_btn = 0
            elif ch == curses.KEY_RIGHT:
                selected_btn = 1
            elif ch in [10, 13]:  
                if selected_btn == 0:  
                    curses.curs_set(0)
                    
                    return "".join(lines)
                else:  
                    curses.curs_set(0)
                    return None
        else:  
            if ch in [10, 13]: 
                curses.curs_set(0)
               
                return "".join(lines)
            elif ch == ord('j') - 96: 
                current_line = lines[cursor_y]
                before_cursor = current_line[:cursor_x]
                after_cursor = current_line[cursor_x:]
                lines[cursor_y] = before_cursor
                lines.insert(cursor_y + 1, after_cursor)
                cursor_y += 1
                cursor_x = 0
                if cursor_y >= scroll + visible_height:
                    scroll += 1
            elif ch == curses.KEY_UP:
                if cursor_y > 0:
                    cursor_y -= 1
                    cursor_x = min(cursor_x, len(lines[cursor_y]))
                    if cursor_y < scroll:
                        scroll = max(0, scroll-1)
            elif ch == curses.KEY_DOWN:
                if cursor_y < len(lines)-1:
                    cursor_y += 1
                    cursor_x = min(cursor_x, len(lines[cursor_y]))
                    if cursor_y >= scroll + visible_height:
                        scroll += 1
            elif ch in [curses.KEY_BACKSPACE, 127, 8]:
                if cursor_x > 0:
                  
                    lines[cursor_y] = lines[cursor_y][:cursor_x-1] + lines[cursor_y][cursor_x:]
                    cursor_x -= 1
                elif cursor_y > 0:
                    
                    prev_len = len(lines[cursor_y-1])
                    lines[cursor_y-1] += lines[cursor_y]
                    lines.pop(cursor_y)
                    cursor_y -= 1
                    cursor_x = prev_len
                    if cursor_y < scroll:
                        scroll = max(0, scroll-1)
            elif ch == curses.KEY_LEFT:
                if cursor_x > 0:
                    cursor_x -= 1
                elif cursor_y > 0:
                    cursor_y -= 1
                    cursor_x = len(lines[cursor_y])
                    if cursor_y < scroll:
                        scroll = max(0, scroll-1)
            elif ch == curses.KEY_RIGHT:
                if cursor_x < len(lines[cursor_y]):
                    cursor_x += 1
                elif cursor_y < len(lines) - 1:
                    cursor_y += 1
                    cursor_x = 0
                    if cursor_y >= scroll + visible_height:
                        scroll += 1
            elif ch == curses.KEY_RESIZE:
                h, w = stdscr.getmaxyx()
                win_h, win_w = min(h-4, max(12, h-6)), min(120, w-4)
                win.resize(win_h, win_w)
            elif 0 <= ch < 256:
                
                char = chr(ch)
                lines[cursor_y] = lines[cursor_y][:cursor_x] + char + lines[cursor_y][cursor_x:]
                cursor_x += 1
              
                max_line_width = win_w - 6 
                if cursor_x >= max_line_width:
                   
                    current_line = lines[cursor_y]
                    before_cursor = current_line[:cursor_x]
                    after_cursor = current_line[cursor_x:]
                    lines[cursor_y] = before_cursor
                    lines.insert(cursor_y + 1, after_cursor)
                    cursor_y += 1
                    cursor_x = 0
                    if cursor_y >= scroll + visible_height:
                        scroll += 1

def list_files_flow(stdscr, client):
    dir_name = normalize_path(input_box(stdscr, "Directory name:"))
    resp = client.send_command(f"dir_list {dir_name}")
    if isinstance(resp, dict) and resp.get("status") == "success":
        data = resp.get("data", "")
        if isinstance(data, str):
            data = data.replace("\\n", "\n")
        message_box(stdscr, f"Files in {dir_name}", data or "No files")
    else:
        message_box(stdscr, "Error", resp.get("error_message", "Failed to list files"))

def create_file_flow(stdscr, client):
    dir_name = normalize_path(input_box(stdscr, "Directory name:"))
    file_name = input_box(stdscr, "File name:")
    path = f"{dir_name}/{file_name}"
    content = edit_file_screen(stdscr, client, path, "")
    if content is not None:
       
        escaped_content = content.replace('\\', '\\\\').replace('"', '\\"')
        cmd = f'create_file {path} "{escaped_content}"'
        resp = client.send_command(cmd)
        if isinstance(resp, dict):
            if resp.get("status") == "success":
                message_box(stdscr, "Success", f"File created: {path}")
            else:
                message_box(stdscr, "Error", resp.get("error_message", "Failed to create file"))
        else:
            message_box(stdscr, "Response", str(resp))
    return None

def read_file_flow(stdscr, client):
    dir_name = normalize_path(input_box(stdscr, "Directory name:"))
    file_name = input_box(stdscr, "File name:")
    path = f"{dir_name}/{file_name}"
    resp = client.send_command(f"read_file {path}")
    content = ""
    if isinstance(resp, dict) and resp.get("status") == "success":
        content = resp.get("data", "")
        read_file_screen(stdscr, client, path, content)
    else:
        message_box(stdscr, "Error", resp.get("error_message", "Failed to read file"))

def edit_file_flow(stdscr, client):
    dir_name = normalize_path(input_box(stdscr, "Directory name:"))
    file_name = input_box(stdscr, "File name:")
    path = f"{dir_name}/{file_name}"
   
    resp = client.send_command(f"read_file {path}")
    existing = ""
    if isinstance(resp, dict) and resp.get("status") == "success":
        existing = resp.get("data", "")
      
        if isinstance(existing, str):
            existing = existing.replace("\\n", "\n")
    else:
        message_box(stdscr, "Warning", f"Could not read file: {resp.get('error_message', 'Unknown error')}\nStarting with empty content.")
   
    new_content = edit_file_screen(stdscr, client, path, existing)
    
    if new_content is not None:
        
        delete_resp = client.send_command(f'delete_file {path}')
       
        escaped_content = new_content.replace('\\', '\\\\').replace('"', '\\"')
        cmd = f'create_file {path} "{escaped_content}"'
       
        resp = client.send_command(cmd)
        if isinstance(resp, dict):
            if resp.get("status") == "success":
                message_box(stdscr, "Success", f"File saved: {path}\n\nContent has been updated successfully!")
            else:
                error_msg = resp.get("error_message", "Failed to save file")
                message_box(stdscr, "Error", f"Could not save file:\n{error_msg}\n\nCommand: {cmd[:100]}...")
        else:
            message_box(stdscr, "Response", str(resp))
    else:
        message_box(stdscr, "Cancelled", "Edit cancelled. No changes were saved.")
    return None

def delete_file_flow(stdscr, client):
    dir_name = normalize_path(input_box(stdscr, "Directory name:"))
    file_name = input_box(stdscr, "File name:")
    path = f"{dir_name}/{file_name}"
    return f'delete_file {path}'

def rename_file_flow(stdscr, client):
    old_dir = normalize_path(input_box(stdscr, "Old directory name:"))
    old_file = input_box(stdscr, "Old file name:")
    new_dir = normalize_path(input_box(stdscr, "New directory name:"))
    new_file = input_box(stdscr, "New file name:")
    old_path = f"{old_dir}/{old_file}"
    new_path = f"{new_dir}/{new_file}"
    return f'rename_file {old_path} {new_path}'

def truncate_file_flow(stdscr, client):
    dir_name = normalize_path(input_box(stdscr, "Directory name:"))
    file_name = input_box(stdscr, "File name:")
    path = f"{dir_name}/{file_name}"
    size = input_box(stdscr, "New size:")
    return f'truncate_file {path} {size}'

def file_menu(stdscr, client, username):
    options = [
        "👤 List Users", "➕ Create User", "🗑 Delete User", "📂 List Files", "📄 Create File", 
        "📖 Read File", "✏ Edit File", "🗑 Delete File",
        "📁 Create Directory", "🗑 Delete Directory", "🔁 Rename File", "✂ Truncate File",
        "🛡 Set Permission", "📊 View Metadata", "🖥 Session Info", "Logout"
    ]
    def menu_screen(stdscr, title, options):
        current = 0
        while True:
            stdscr.clear()
            draw_box(stdscr, title)
            h, w = stdscr.getmaxyx()
            for i, opt in enumerate(options):
                x, y = 4, 2 + i
                display = f"> {opt}" if i == current else f"  {opt}"
                try:
                    if i == current:
                        stdscr.attron(curses.color_pair(1))
                        stdscr.addstr(y, x, display)
                        stdscr.attroff(curses.color_pair(1))
                    else:
                        stdscr.addstr(y, x, display)
                except:
                    stdscr.addstr(y, x, display[:w-8])
            stdscr.refresh()
            key = stdscr.getch()
            if key == curses.KEY_UP:
                current = (current - 1) % len(options)
            elif key == curses.KEY_DOWN:
                current = (current + 1) % len(options)
            elif key in [10, 13]:
                return options[current]
    while True:
        choice = menu_screen(stdscr, f"User: {username} - FileVerse Menu", options)
        if choice == "Logout":
            resp = client.send_command("logout")
            message_box(stdscr, "Logout", resp.get("error_message", "Logged out"))
            break
      
        action_map = {
            "👤 List Users": lambda: client.send_command("list_users"),
            "➕ Create User": lambda: client.send_command(f"create_user {input_box(stdscr,'Username:')} {input_box(stdscr,'Password:')} normal"),
            "🗑 Delete User": lambda: client.send_command(f"delete_user {input_box(stdscr,'Username:')}"),
            "📂 List Files": lambda: list_files_flow(stdscr, client),
            "📄 Create File": lambda: create_file_flow(stdscr, client),
            "📖 Read File": lambda: read_file_flow(stdscr, client),
            "✏ Edit File": lambda: edit_file_flow(stdscr, client),
            "🗑 Delete File": lambda: delete_file_flow(stdscr, client),
            "📁 Create Directory": lambda: client.send_command(f"create_dir {normalize_path(input_box(stdscr,'Directory name:'))}"),
            "🗑 Delete Directory": lambda: client.send_command(f"delete_dir {normalize_path(input_box(stdscr,'Directory name:'))}"),
            "🔁 Rename File": lambda: rename_file_flow(stdscr, client),
            "✂ Truncate File": lambda: truncate_file_flow(stdscr, client),
            "🛡 Set Permission": lambda: client.send_command(f"set_permissions {normalize_path(input_box(stdscr,'Directory name:'))}/{input_box(stdscr,'File name:')} {input_box(stdscr,'Permissions (int):')}"),
            "📊 View Metadata": lambda: client.send_command(f"get_metadata {normalize_path(input_box(stdscr,'Directory name:'))}/{input_box(stdscr,'File name:')}"),
            "🖥 Session Info": lambda: client.send_command("get_session_info")
        }
        handler = action_map.get(choice)
        if handler is None:
            message_box(stdscr, "Error", "Not implemented")
            continue
        try:
            result = handler()
            if isinstance(result, str):
                resp = client.send_command(result)
            elif result is None:
                resp = None
            else:
                resp = result
        except Exception as e:
            resp = {"status": "error", "error_message": str(e)}
        if resp is None:
            continue
        if isinstance(resp, dict):
            if resp.get("status") == "success":
                if "data" in resp and resp["data"] not in [None, ""]:
                    data = resp["data"]
                    if isinstance(data, str):
                        data = data.replace("\\n", "\n")
                    message_box(stdscr, choice, data)
                else:
                    message_box(stdscr, choice, resp.get("error_message", "Success"))
            else:
                message_box(stdscr, "Error", resp.get("error_message", "Operation failed"))
        else:
            message_box(stdscr, choice, str(resp))

def main(stdscr):
    curses.curs_set(0)
    curses.start_color()
    curses.use_default_colors()
    curses.init_pair(1, curses.COLOR_BLACK, curses.COLOR_CYAN)   # Highlight
    curses.init_pair(2, curses.COLOR_YELLOW, curses.COLOR_BLACK) # Title
    curses.init_pair(3, curses.COLOR_GREEN, curses.COLOR_BLACK)  # Info/Input
    curses.init_pair(4, curses.COLOR_MAGENTA, curses.COLOR_BLACK) # Fancy
    client = None
    try:
        client = OFSClient()
    except Exception as e:
        stdscr.clear()
        stdscr.addstr(2,2, f"Failed to connect to server {SERVER_HOST}:{SERVER_PORT} - {e}")
        stdscr.addstr(4,2, "Press any key to exit...")
        stdscr.getch()
        return
    username = login_screen(stdscr, client)
    if username == "":
        client.close()
        return
    file_menu(stdscr, client, username)
    client.close()

if __name__ == "__main__":
    curses.wrapper(main)
