"""Windows client for the opt-in gameplay test interface (Python stdlib only)."""
from __future__ import annotations

import ctypes
from ctypes import wintypes
import json
from pathlib import Path
import time


class AutomationError(RuntimeError):
    pass


def npc_attackable(target):
    """Use native attack eligibility; older engines exposed only NPC life."""
    return target.get("attackable", target.get("life", 0) > 0)


class Client:
    def __init__(self, session: str, timeout: float = 15, transcript: Path | None = None):
        self.timeout = timeout
        self.sequence = 0
        self.pending = b""
        self.transcript = transcript.open("a", encoding="utf-8") if transcript else None
        self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        class OVERLAPPED(ctypes.Structure):
            _fields_ = [("Internal", ctypes.c_size_t), ("InternalHigh", ctypes.c_size_t),
                        ("Offset", wintypes.DWORD), ("OffsetHigh", wintypes.DWORD),
                        ("hEvent", wintypes.HANDLE)]
        self.OVERLAPPED = OVERLAPPED
        signatures = {
            "CreateFileW": ([wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p,
                             wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE], wintypes.HANDLE),
            "CreateEventW": ([ctypes.c_void_p, wintypes.BOOL, wintypes.BOOL, wintypes.LPCWSTR], wintypes.HANDLE),
            "CloseHandle": ([wintypes.HANDLE], wintypes.BOOL),
            "WaitForSingleObject": ([wintypes.HANDLE, wintypes.DWORD], wintypes.DWORD),
            "CancelIoEx": ([wintypes.HANDLE, ctypes.POINTER(OVERLAPPED)], wintypes.BOOL),
            "GetOverlappedResult": ([wintypes.HANDLE, ctypes.POINTER(OVERLAPPED),
                                     ctypes.POINTER(wintypes.DWORD), wintypes.BOOL], wintypes.BOOL),
        }
        for name in ("ReadFile", "WriteFile"):
            signatures[name] = ([wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD,
                                 ctypes.POINTER(wintypes.DWORD), ctypes.POINTER(OVERLAPPED)], wintypes.BOOL)
        for name, (arguments, result) in signatures.items():
            function = getattr(self.kernel, name)
            function.argtypes, function.restype = arguments, result
        deadline = time.monotonic() + timeout
        while True:
            self.handle = self.kernel.CreateFileW(
                rf"\\.\pipe\jxqy-{session}", 0xC0000000, 0, None, 3, 0x40000000, None)
            if self.handle != ctypes.c_void_p(-1).value:
                break
            if time.monotonic() >= deadline:
                raise AutomationError(f"Cannot connect to {session}: {ctypes.get_last_error()}")
            time.sleep(0.1)

    def close(self):
        if self.handle:
            self.kernel.CloseHandle(self.handle)
            self.handle = None
        if self.transcript:
            self.transcript.close()
            self.transcript = None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def _transfer(self, data: bytes | None) -> bytes | int:
        buffer = ctypes.create_string_buffer(data, len(data)) if data is not None else ctypes.create_string_buffer(65536)
        overlap = self.OVERLAPPED()
        overlap.hEvent = self.kernel.CreateEventW(None, True, False, None)
        if not overlap.hEvent:
            raise ctypes.WinError(ctypes.get_last_error())
        count = wintypes.DWORD()
        try:
            function = self.kernel.WriteFile if data is not None else self.kernel.ReadFile
            done = function(self.handle, buffer, len(buffer), ctypes.byref(count), ctypes.byref(overlap))
            if not done:
                error = ctypes.get_last_error()
                if error != 997:
                    raise AutomationError(f"Pipe I/O failed: {error}")
                if self.kernel.WaitForSingleObject(overlap.hEvent, int(self.timeout * 1000)) != 0:
                    self.kernel.CancelIoEx(self.handle, ctypes.byref(overlap))
                    self.kernel.GetOverlappedResult(self.handle, ctypes.byref(overlap), ctypes.byref(count), True)
                    raise TimeoutError("Pipe response timed out")
                if not self.kernel.GetOverlappedResult(self.handle, ctypes.byref(overlap), ctypes.byref(count), False):
                    raise AutomationError(f"Pipe I/O failed: {ctypes.get_last_error()}")
            if count.value == 0:
                raise AutomationError("Pipe closed")
            return count.value if data is not None else buffer.raw[:count.value]
        finally:
            self.kernel.CloseHandle(overlap.hEvent)

    def request(self, command: str, **arguments):
        self.sequence += 1
        request = dict(version=1, id=self.sequence, command=command, arguments=arguments)
        data = (json.dumps(request, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8")
        if len(data) > 65536:
            raise ValueError("Request exceeds protocol limit")
        while data:
            data = data[self._transfer(data):]
        while b"\n" not in self.pending:
            self.pending += self._transfer(None)
            if len(self.pending) > 4 * 1024 * 1024:
                raise AutomationError("Response exceeds protocol limit")
        line, self.pending = self.pending.split(b"\n", 1)
        result = json.loads(line.decode("utf-8"))
        if self.transcript:
            self.transcript.write(json.dumps(dict(request=request, response=result), ensure_ascii=False) + "\n")
            self.transcript.flush()
        if result.get("version") != 1 or result.get("id") != self.sequence:
            raise AutomationError("Mismatched protocol response")
        if not result.get("ok"):
            raise AutomationError(result.get("error", "request_failed"))
        return result["data"]

    def observe(self, variables=()):
        return self.request("Observe", variables=list(variables))

    def submit(self, command: str, **arguments):
        return self.request(command, **arguments)["actionId"]

    def wait_action(self, action_id: int, timeout=60, allow_cancelled=False):
        deadline = time.monotonic() + timeout
        next_observation = time.monotonic()
        while time.monotonic() < deadline:
            status = self.request("GetActionStatus", actionId=action_id)
            if status["status"] == "succeeded" or (allow_cancelled and status["status"] == "cancelled"):
                return status
            if status["status"] in ("failed", "cancelled"):
                raise AutomationError(f"{status['command']}: {status['reason']}")
            if status["command"] == "StartCombat" and time.monotonic() >= next_observation:
                self.observe()
                next_observation = time.monotonic() + 1
            time.sleep(0.05)
        self.request("CancelAction", actionId=action_id)
        raise TimeoutError(f"Action {action_id} timed out")

    def act(self, command: str, timeout=60, **arguments):
        return self.wait_action(self.submit(command, **arguments), timeout)

    def wait_until(self, predicate, timeout=60, variables=(), description="state"):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            state = self.observe(variables)
            if predicate(state):
                return state
            time.sleep(0.2)
        raise TimeoutError(f"Waiting for {description}: {state}")

    def ui(self, action: str):
        state = self.observe()
        return self.act("SendUIAction", context=state["context"], action=action)

    def focus(self, name: str, component: str | None = None):
        state = self.observe()
        widgets = [item for item in state.get("ui", []) if item["name"] == name
                   and (component is None or item["component"] == component)]
        if len(widgets) != 1:
            raise AutomationError(f"Expected one widget {name}, found {widgets}")
        self.act("FocusUI", context=state["context"], targetId=widgets[0]["id"])

    def activate(self, name: str):
        self.focus(name)
        return self.ui("Confirm")

    def open_menu(self, menu: str):
        state = self.observe()
        return self.act("OpenMenu", context=state["context"], menu=menu)

    def move(self, x: int, y: int, running=True, timeout=60):
        state = self.observe()
        return self.act("MoveTo", timeout=timeout + 2, generation=state["generation"],
                        x=x, y=y, running=running, timeoutMs=int(timeout * 1000))

    def interact(self, target_id: int, side="primary", timeout=60):
        state = self.observe()
        return self.act("Interact", timeout=timeout + 2, generation=state["generation"],
                        targetId=target_id, side=side, running=True, timeoutMs=int(timeout * 1000))

    def snapshot(self):
        return Path(self.act("CaptureFrame", timeout=15)["reason"])

    def focus_slot(self, prefix: str, slot: int):
        """Find a logical slot using normal menu page navigation."""
        prefixes = (prefix, "integrated-magic-item-") if prefix == "magic-item-" else (prefix,)
        for _ in range(64):
            state = self.observe()
            widgets = [item for item in state.get("ui", []) if item["name"].startswith(prefixes)]
            if not widgets:
                raise AutomationError(f"Slot menu is not visible: {prefix}")
            target = next((item for item in widgets if item.get("slot") == slot), None)
            if target:
                self.act("FocusUI", context=state["context"], targetId=target["id"])
                return
            self.focus(widgets[0]["name"])
            self.ui("PagePrevious" if slot < min(item["slot"] for item in widgets) else "PageNext")
        raise AutomationError(f"Cannot reach {prefix} slot {slot}")

    def equip(self, slot: int):
        self.open_menu("Goods")
        self.focus_slot("goods-item-", slot)
        self.ui("Confirm")
        self.ui("Cancel")

    def assign_magic(self, slot: int, quick_slot: int | None = None):
        """Assign the first free quick slot, or swap with an explicit slot 0..4."""
        if quick_slot is None:
            self.open_menu("Magic")
            self.focus_slot("magic-item-", slot)
            self.ui("Confirm")  # Existing callback assigns the first free quick slot.
            self.ui("Cancel")
            return
        if not isinstance(quick_slot, int) or not 0 <= quick_slot <= 4:
            raise ValueError("Magic quick slots are 0..4")
        state = self.observe()
        before = {item["slot"]: item["file"] for item in state.get("magic", [])}
        if slot not in before:
            raise AutomationError(f"No learned magic in slot {slot}")
        quick_begin = state["layout"]["magicQuickBegin"]
        target = quick_begin + quick_slot
        if slot == target:
            return state
        if not state["worldInput"]:
            raise AutomationError("Magic assignment requires normal gameplay with menus closed")
        if slot == state["layout"]["practiceSlot"]:
            self.open_menu("Practice")
            prefix, panel = "practice-magic-", "PanelPrevious"
        elif slot < quick_begin:
            self.open_menu("Magic")
            prefix, panel = "magic-item-", "PanelNext"
        else:
            prefix, panel = "bottom-magic-quick-", None
        self.focus_slot(prefix, slot)
        self.ui("Secondary")
        if panel:
            self.ui(panel)
        self.focus_slot("bottom-magic-quick-", target)
        self.ui("Secondary")
        assigned = {item["slot"]: item["file"] for item in self.observe().get("magic", [])}
        if assigned.get(target) != before[slot] or assigned.get(slot) != before.get(target):
            raise AutomationError(f"Magic assignment did not exchange slots {slot} and {target}")
        if panel:
            self.ui("Cancel")  # Cross-pane transfers stay active until cancelled.
        self.ui("Cancel")  # Close the source menu or release quick-bar focus.
        return self.wait_until(lambda value: value["worldInput"], timeout=5,
                               description="magic assignment completion")

    def assign_practice(self, slot: int):
        """Swap a learned magic into practice through the normal transfer UI.

        A quick-bar source leaves that quick slot; this does not copy the magic.
        Return the resulting snapshot so the route can resolve its casting slots.
        """
        state = self.observe()
        source = next((item for item in state.get("magic", []) if item["slot"] == slot), None)
        if source is None:
            raise AutomationError(f"No learned magic in slot {slot}")
        practice_slot = state["layout"]["practiceSlot"]
        if slot == practice_slot:
            return state
        if not state["worldInput"]:
            raise AutomationError("Practice assignment requires normal gameplay with menus closed")
        from_list = slot < state["layout"]["magicQuickBegin"]
        if from_list:
            self.open_menu("Magic")
        self.focus_slot("magic-item-" if from_list else "bottom-magic-quick-", slot)
        self.ui("Secondary")
        self.ui("PanelPrevious" if from_list else "PanelNext")
        self.focus_slot("practice-magic-", practice_slot)
        self.ui("Secondary")
        assigned = self.observe()
        if not any(item["slot"] == practice_slot and item["file"] == source["file"]
                   for item in assigned.get("magic", [])):
            raise AutomationError(f"Practice assignment did not move {source['file']}")
        owned_prefixes = ("magic-item-", "integrated-magic-item-", "practice-magic-") if from_list else ("practice-magic-",)
        # Ending a transfer and closing its source and destination are separate
        # normal Cancel actions. Stop as soon as our windows are no longer visible.
        for _ in range(3):
            state = self.observe()
            if not any(item["name"].startswith(owned_prefixes) for item in state.get("ui", [])):
                break
            self.act("SendUIAction", context=state["context"], action="Cancel")
        return self.wait_until(lambda value: value["worldInput"], timeout=5,
                               description="practice assignment completion")

    def assign_goods(self, slot: int, quick_slot: int):
        self.open_menu("Goods")
        self.focus_slot("goods-item-", slot)
        self.ui("Secondary")
        self.ui("PanelNext")  # Switch the existing transfer session into the quick bar.
        self.focus(f"bottom-goods-quick-{quick_slot}")
        self.ui("Secondary")
        self.ui("Cancel")  # End the cross-pane transfer session.
        self.ui("Cancel")  # Close the goods window.

    def buy(self, slot: int):
        self.focus_slot("buy-sell-shop-item-", slot)
        self.ui("Confirm")

    def sell(self, slot: int):
        self.focus_slot("buy-sell-player-item-", slot)
        self.ui("Confirm")

    def save_or_load(self, slot: int, *, load=False):
        """Use the same slot selection and buttons as normal gameplay."""
        if not 0 <= slot <= 6:
            raise ValueError("Manual save slots are 0..6")
        if not any(item["name"] == "save-load" for item in self.observe().get("ui", [])):
            self.open_menu("System")
        self.activate("save-load")
        state = self.wait_until(lambda s: "saveSlot" in s, description="save/load menu")
        while state["saveSlot"] != slot:
            self.focus("load" if load else "save")
            self.ui("Down" if state["saveSlot"] < slot else "Up")
            state = self.observe()
        self.activate("load" if load else "save")
        return self.wait_until(lambda s: s["worldInput"], timeout=60, description="save/load completion")

    def exit_game(self):
        state = self.observe()
        if state["scene"] != "Title":
            if not any(item["name"] == "return-to-title" for item in state.get("ui", [])):
                self.open_menu("System")
            self.activate("return-to-title")
            self.wait_until(lambda s: s["scene"] == "Title", description="title")
        self.focus("exit")
        state = self.observe()
        # Exit destroys the server; process exit is the completion evidence.
        try:
            self.submit("SendUIAction", context=state["context"], action="Confirm")
        except AutomationError as error:
            if not any(reason in str(error) for reason in ("Pipe closed", "Pipe I/O failed: 109", "Pipe I/O failed: 233")):
                raise


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("session")
    parser.add_argument("--capture", action="store_true")
    arguments = parser.parse_args()
    with Client(arguments.session) as client:
        print(json.dumps(client.observe(), ensure_ascii=False, indent=2))
        if arguments.capture:
            print(client.snapshot())
