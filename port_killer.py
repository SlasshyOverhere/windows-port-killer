"""
Windows Port Killer - GUI Application
Kill processes using specific ports on Windows
"""

import tkinter as tk
from tkinter import ttk, messagebox
import subprocess
import re
import os
import ctypes


def is_admin():
    """Check if running with admin privileges."""
    try:
        return ctypes.windll.shell32.IsUserAnAdmin()
    except:
        return False


def get_port_info():
    """Get all ports and their associated processes."""
    try:
        # Run netstat to get port information
        result = subprocess.run(
            ['netstat', '-ano'],
            capture_output=True,
            text=True,
            creationflags=subprocess.CREATE_NO_WINDOW
        )

        ports = []
        lines = result.stdout.strip().split('\n')

        for line in lines[4:]:  # Skip header lines
            parts = line.split()
            if len(parts) >= 5:
                protocol = parts[0]
                local_addr = parts[1]
                foreign_addr = parts[2]
                state = parts[3] if protocol == 'TCP' else 'N/A'
                pid = parts[-1]

                # Extract port from local address
                if ':' in local_addr:
                    port = local_addr.rsplit(':', 1)[-1]
                else:
                    port = 'N/A'

                # Get process name
                process_name = get_process_name(pid)

                ports.append({
                    'protocol': protocol,
                    'local_addr': local_addr,
                    'foreign_addr': foreign_addr,
                    'state': state,
                    'pid': pid,
                    'port': port,
                    'process_name': process_name
                })

        return ports
    except Exception as e:
        messagebox.showerror("Error", f"Failed to get port info: {e}")
        return []


def get_process_name(pid):
    """Get process name from PID."""
    try:
        result = subprocess.run(
            ['tasklist', '/FI', f'PID eq {pid}', '/FO', 'CSV', '/NH'],
            capture_output=True,
            text=True,
            creationflags=subprocess.CREATE_NO_WINDOW
        )
        output = result.stdout.strip()
        if output and 'No tasks' not in output:
            # Parse CSV format: "process.exe","PID","Session Name","Session#","Mem Usage"
            match = re.match(r'"([^"]+)"', output)
            if match:
                return match.group(1)
    except:
        pass
    return "Unknown"


def kill_process(pid):
    """Kill a process by PID."""
    try:
        result = subprocess.run(
            ['taskkill', '/F', '/PID', str(pid)],
            capture_output=True,
            text=True,
            creationflags=subprocess.CREATE_NO_WINDOW
        )
        if result.returncode == 0:
            return True, "Process killed successfully"
        else:
            return False, result.stderr.strip()
    except Exception as e:
        return False, str(e)


class PortKillerApp:
    def __init__(self, root):
        self.root = root
        self.root.title("Port Killer - Windows Port Manager")
        self.root.geometry("1000x600")
        self.root.minsize(800, 500)

        # Configure style
        self.style = ttk.Style()
        self.style.theme_use('clam')

        # Configure colors
        self.root.configure(bg='#1e1e1e')
        self.style.configure('TFrame', background='#1e1e1e')
        self.style.configure('TLabel', background='#1e1e1e', foreground='#ffffff')
        self.style.configure('TButton', padding=6)
        self.style.configure('Treeview',
                           background='#2d2d2d',
                           foreground='#ffffff',
                           fieldbackground='#2d2d2d',
                           rowheight=25)
        self.style.configure('Treeview.Heading',
                           background='#3d3d3d',
                           foreground='#ffffff',
                           padding=5)
        self.style.map('Treeview', background=[('selected', '#0078d4')])

        self.create_widgets()
        self.refresh_ports()

    def create_widgets(self):
        # Main container
        main_frame = ttk.Frame(self.root, padding="10")
        main_frame.pack(fill=tk.BOTH, expand=True)

        # Top frame - Search and controls
        top_frame = ttk.Frame(main_frame)
        top_frame.pack(fill=tk.X, pady=(0, 10))

        # Search section
        search_frame = ttk.Frame(top_frame)
        search_frame.pack(side=tk.LEFT, fill=tk.X, expand=True)

        ttk.Label(search_frame, text="Search Port:", font=('Segoe UI', 10)).pack(side=tk.LEFT, padx=(0, 10))

        self.search_var = tk.StringVar()
        self.search_var.trace('w', lambda *args: self.filter_ports())
        self.search_entry = ttk.Entry(search_frame, textvariable=self.search_var, width=20, font=('Segoe UI', 10))
        self.search_entry.pack(side=tk.LEFT, padx=(0, 10))

        # Process name search
        ttk.Label(search_frame, text="Search Process:", font=('Segoe UI', 10)).pack(side=tk.LEFT, padx=(0, 10))

        self.process_search_var = tk.StringVar()
        self.process_search_var.trace('w', lambda *args: self.filter_ports())
        self.process_entry = ttk.Entry(search_frame, textvariable=self.process_search_var, width=20, font=('Segoe UI', 10))
        self.process_entry.pack(side=tk.LEFT, padx=(0, 10))

        # Filter by protocol
        ttk.Label(search_frame, text="Protocol:", font=('Segoe UI', 10)).pack(side=tk.LEFT, padx=(0, 5))
        self.protocol_var = tk.StringVar(value="ALL")
        protocol_combo = ttk.Combobox(search_frame, textvariable=self.protocol_var,
                                      values=["ALL", "TCP", "UDP"], width=8, state='readonly')
        protocol_combo.pack(side=tk.LEFT, padx=(0, 10))
        protocol_combo.bind('<<ComboboxSelected>>', lambda e: self.filter_ports())

        # Buttons
        btn_frame = ttk.Frame(top_frame)
        btn_frame.pack(side=tk.RIGHT)

        refresh_btn = ttk.Button(btn_frame, text="Refresh", command=self.refresh_ports)
        refresh_btn.pack(side=tk.LEFT, padx=5)

        kill_btn = ttk.Button(btn_frame, text="Kill Selected", command=self.kill_selected)
        kill_btn.pack(side=tk.LEFT, padx=5)

        # Admin status
        admin_status = "Admin" if is_admin() else "Not Admin"
        admin_color = "#00ff00" if is_admin() else "#ff6b6b"
        admin_label = tk.Label(btn_frame, text=admin_status, fg=admin_color, bg='#1e1e1e', font=('Segoe UI', 9))
        admin_label.pack(side=tk.LEFT, padx=10)

        # Treeview for ports
        tree_frame = ttk.Frame(main_frame)
        tree_frame.pack(fill=tk.BOTH, expand=True)

        # Scrollbars
        y_scroll = ttk.Scrollbar(tree_frame, orient=tk.VERTICAL)
        y_scroll.pack(side=tk.RIGHT, fill=tk.Y)

        x_scroll = ttk.Scrollbar(tree_frame, orient=tk.HORIZONTAL)
        x_scroll.pack(side=tk.BOTTOM, fill=tk.X)

        # Treeview
        columns = ('port', 'protocol', 'local_addr', 'foreign_addr', 'state', 'pid', 'process_name')
        self.tree = ttk.Treeview(tree_frame, columns=columns, show='headings',
                                 yscrollcommand=y_scroll.set, xscrollcommand=x_scroll.set)

        y_scroll.config(command=self.tree.yview)
        x_scroll.config(command=self.tree.xview)

        # Configure columns
        self.tree.heading('port', text='Port', command=lambda: self.sort_column('port', False))
        self.tree.heading('protocol', text='Protocol', command=lambda: self.sort_column('protocol', False))
        self.tree.heading('local_addr', text='Local Address', command=lambda: self.sort_column('local_addr', False))
        self.tree.heading('foreign_addr', text='Foreign Address', command=lambda: self.sort_column('foreign_addr', False))
        self.tree.heading('state', text='State', command=lambda: self.sort_column('state', False))
        self.tree.heading('pid', text='PID', command=lambda: self.sort_column('pid', False))
        self.tree.heading('process_name', text='Process Name', command=lambda: self.sort_column('process_name', False))

        self.tree.column('port', width=80, minwidth=60)
        self.tree.column('protocol', width=80, minwidth=60)
        self.tree.column('local_addr', width=180, minwidth=100)
        self.tree.column('foreign_addr', width=180, minwidth=100)
        self.tree.column('state', width=100, minwidth=80)
        self.tree.column('pid', width=80, minwidth=60)
        self.tree.column('process_name', width=200, minwidth=100)

        self.tree.pack(fill=tk.BOTH, expand=True)

        # Bind double-click to kill
        self.tree.bind('<Double-1>', lambda e: self.kill_selected())

        # Right-click context menu
        self.context_menu = tk.Menu(self.root, tearoff=0)
        self.context_menu.add_command(label="Kill Process", command=self.kill_selected)
        self.context_menu.add_command(label="Copy PID", command=self.copy_pid)
        self.context_menu.add_command(label="Copy Port", command=self.copy_port)
        self.tree.bind('<Button-3>', self.show_context_menu)

        # Status bar
        self.status_var = tk.StringVar(value="Ready")
        status_bar = tk.Label(main_frame, textvariable=self.status_var,
                             bg='#2d2d2d', fg='#888888', anchor='w', padx=10, pady=5)
        status_bar.pack(fill=tk.X, pady=(10, 0))

        # Store all port data
        self.all_ports = []

    def refresh_ports(self):
        """Refresh the port list."""
        self.status_var.set("Refreshing...")
        self.root.update()

        self.all_ports = get_port_info()
        self.filter_ports()

        self.status_var.set(f"Found {len(self.all_ports)} connections")

    def filter_ports(self):
        """Filter ports based on search criteria."""
        # Clear current items
        for item in self.tree.get_children():
            self.tree.delete(item)

        port_search = self.search_var.get().strip()
        process_search = self.process_search_var.get().strip().lower()
        protocol_filter = self.protocol_var.get()

        count = 0
        for port_info in self.all_ports:
            # Apply filters
            if port_search and port_search not in port_info['port']:
                continue
            if process_search and process_search not in port_info['process_name'].lower():
                continue
            if protocol_filter != "ALL" and port_info['protocol'] != protocol_filter:
                continue

            self.tree.insert('', tk.END, values=(
                port_info['port'],
                port_info['protocol'],
                port_info['local_addr'],
                port_info['foreign_addr'],
                port_info['state'],
                port_info['pid'],
                port_info['process_name']
            ))
            count += 1

        self.status_var.set(f"Showing {count} of {len(self.all_ports)} connections")

    def sort_column(self, col, reverse):
        """Sort treeview by column."""
        items = [(self.tree.set(item, col), item) for item in self.tree.get_children('')]

        # Try numeric sort for port and pid
        if col in ('port', 'pid'):
            try:
                items.sort(key=lambda x: int(x[0]) if x[0].isdigit() else 0, reverse=reverse)
            except:
                items.sort(reverse=reverse)
        else:
            items.sort(reverse=reverse)

        for index, (val, item) in enumerate(items):
            self.tree.move(item, '', index)

        self.tree.heading(col, command=lambda: self.sort_column(col, not reverse))

    def kill_selected(self):
        """Kill the selected process."""
        selection = self.tree.selection()
        if not selection:
            messagebox.showwarning("Warning", "Please select a connection to kill")
            return

        for item in selection:
            values = self.tree.item(item, 'values')
            pid = values[5]
            process_name = values[6]
            port = values[0]

            if pid == '0' or pid == '4':
                messagebox.showwarning("Warning", f"Cannot kill system process (PID: {pid})")
                continue

            confirm = messagebox.askyesno(
                "Confirm Kill",
                f"Kill process '{process_name}' (PID: {pid}) using port {port}?"
            )

            if confirm:
                success, message = kill_process(pid)
                if success:
                    messagebox.showinfo("Success", f"Process killed: {process_name} (PID: {pid})")
                    self.refresh_ports()
                else:
                    messagebox.showerror("Error", f"Failed to kill process: {message}\n\nTry running as Administrator.")

    def show_context_menu(self, event):
        """Show right-click context menu."""
        item = self.tree.identify_row(event.y)
        if item:
            self.tree.selection_set(item)
            self.context_menu.post(event.x_root, event.y_root)

    def copy_pid(self):
        """Copy selected PID to clipboard."""
        selection = self.tree.selection()
        if selection:
            values = self.tree.item(selection[0], 'values')
            self.root.clipboard_clear()
            self.root.clipboard_append(values[5])
            self.status_var.set(f"Copied PID: {values[5]}")

    def copy_port(self):
        """Copy selected port to clipboard."""
        selection = self.tree.selection()
        if selection:
            values = self.tree.item(selection[0], 'values')
            self.root.clipboard_clear()
            self.root.clipboard_append(values[0])
            self.status_var.set(f"Copied Port: {values[0]}")


def main():
    root = tk.Tk()
    app = PortKillerApp(root)

    # Center window
    root.update_idletasks()
    x = (root.winfo_screenwidth() - root.winfo_reqwidth()) // 2
    y = (root.winfo_screenheight() - root.winfo_reqheight()) // 2
    root.geometry(f"+{x}+{y}")

    root.mainloop()


if __name__ == "__main__":
    main()
