"""Record the real Android UI through ADB; never modify app or firmware code."""
import argparse
import contextlib
import datetime
import json
import os
from pathlib import Path
import re
import subprocess
import time
import xml.etree.ElementTree as ET


class Demo:
    def __init__(self, args):
        self.args = args
        self.output = Path(args.output).resolve()
        self.output.mkdir(parents=True, exist_ok=False)
        self.log = self.output / "run.log"
        self.rotation = {}
        self.ui = None
        self.recording = None

    def note(self, message):
        line = f"{datetime.datetime.now().isoformat(timespec='seconds')} {message}"
        print(line, flush=True)
        with self.log.open("a", encoding="utf-8") as stream:
            stream.write(line + "\n")

    def adb(self, *args, timeout=25):
        safe_retry = (args[0] in ("get-state", "pull") or
                      args[:2] == ("shell", "uiautomator") or
                      args[:2] == ("shell", "cat") or
                      args[:2] == ("shell", "settings") or
                      args[:2] == ("shell", "wm") or
                      args[:3] == ("shell", "am", "start"))
        for attempt in range(2):
            try:
                result = subprocess.run([self.args.adb, "-s", self.args.serial, *args],
                                        capture_output=True, timeout=timeout)
                if result.returncode == 0:
                    return result.stdout.decode("utf-8", errors="replace").strip()
                error = result.stderr.decode("utf-8", errors="replace").strip()
            except subprocess.TimeoutExpired:
                error = "ADB timeout"
            if attempt == 0 and re.search(r"offline|closed|not found|timeout", error, re.I) and ":" in self.args.serial:
                self.note("Связь ADB потеряна; переподключение. Нажатия не повторяются.")
                for verb in ("disconnect", "connect"):
                    subprocess.run([self.args.adb, verb, self.args.serial],
                                   capture_output=True, timeout=10)
                if safe_retry:
                    continue
            raise RuntimeError(error)

    def dump(self):
        self.adb("shell", "uiautomator", "dump", "/sdcard/thekidlock-demo-ui.xml")
        raw = self.adb("shell", "cat", "/sdcard/thekidlock-demo-ui.xml")
        self.ui = ET.fromstring(raw)
        nodes = list(self.ui.iter("node"))
        if not any(n.get("package") == "com.blelock.app" for n in nodes):
            raise RuntimeError("Приложение не на переднем плане: разблокируйте телефон.")
        return nodes

    def find(self, pattern, *, kind=None, timeout=20):
        deadline = time.monotonic() + timeout
        while True:
            nodes = self.dump()
            found = [n for n in nodes if n.get("package") == "com.blelock.app"
                     and n.get("enabled") == "true"
                     and (kind is None or n.get("class") == kind)
                     and any(re.search(pattern, n.get(field, ""), re.I)
                             for field in ("text", "content-desc"))]
            if len(found) == 1:
                return found[0]
            if len(found) > 1:
                raise RuntimeError(f"Неоднозначный элемент: {pattern}")
            if time.monotonic() >= deadline:
                raise RuntimeError(f"Элемент не найден: {pattern}")
            time.sleep(0.4)

    @staticmethod
    def bounds(node):
        match = re.fullmatch(r"\[(\d+),(\d+)\]\[(\d+),(\d+)\]", node.get("bounds", ""))
        if not match:
            raise RuntimeError("Некорректные границы элемента")
        x1, y1, x2, y2 = map(int, match.groups())
        if x2 <= x1 or y2 <= y1:
            raise RuntimeError("Элемент не виден")
        return x1, y1, x2, y2

    def tap(self, pattern, kind=None):
        node = self.find(pattern, kind=kind)
        if node.get("clickable") != "true":
            raise RuntimeError(f"Элемент не принимает нажатие: {pattern}")
        x1, y1, x2, y2 = self.bounds(node)
        y = (y1+y2)//2
        # The existing edge-to-edge back label overlaps the status bar on this phone.
        if kind == "android.widget.TextView" and node.get("content-desc") in ("Назад", "Back"):
            y = y2 - max(4, (y2-y1)//10)
        self.adb("shell", "input", "tap", str((x1+x2)//2), str(y))

    def auto_mode(self, enabled):
        pattern = r"Автооткрытие замка|Auto-open lock|Автовідкриття замка"
        node = self.find(pattern, kind="android.widget.Switch")
        if (node.get("checked") == "true") != enabled:
            self.tap(pattern, "android.widget.Switch")
            node = self.find(pattern, kind="android.widget.Switch")
            if (node.get("checked") == "true") != enabled:
                raise RuntimeError("Переключатель автооткрытия не изменился")

    def power(self, level):
        node = self.find(r"Мощность BLE|BLE.*power|Потужність BLE", kind="android.widget.SeekBar")
        x1, y1, x2, y2 = self.bounds(node)
        density_text = self.adb("shell", "wm", "density")
        densities = re.findall(r"density:\s*(\d+)", density_text)
        if not densities:
            raise RuntimeError("Не определена плотность экрана")
        inset = 18 * int(densities[-1]) / 160
        x = round(x1 + inset + level / 3 * (x2-x1-2*inset))
        self.adb("shell", "input", "tap", str(x), str((y1+y2)//2))

    def operator(self, message):
        self.note(message)
        input("Выполните действие и нажмите Enter в консоли: ")

    @contextlib.contextmanager
    def record(self, name):
        remote = f"/sdcard/thekidlock-demo-{name}-{os.getpid()}.mp4"
        proc = subprocess.Popen([self.args.adb, "-s", self.args.serial, "shell",
                                 f"echo $$; exec screenrecord --time-limit 180 --bit-rate 6000000 {remote}"],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.recording = proc
        pid = None
        try:
            # Bound PID discovery, even if ADB loses its connection.
            import concurrent.futures
            with concurrent.futures.ThreadPoolExecutor() as pool:
                future = pool.submit(proc.stdout.readline)
                try:
                    line = future.result(timeout=10).decode().strip()
                except concurrent.futures.TimeoutError:
                    proc.kill()
                    raise RuntimeError("screenrecord не запустился")
            if not line.isdigit():
                raise RuntimeError("Не определён PID screenrecord")
            pid = line
            time.sleep(1)
            if proc.poll() is not None:
                raise RuntimeError("screenrecord завершился до записи")
            self.note(f"Запись {name}")
            yield
            if proc.poll() is not None:
                raise RuntimeError("Запись прервана или превысила 180 секунд; повторите эпизод")
        finally:
            if pid and proc.poll() is None:
                try:
                    self.adb("shell", "kill", "-2", pid)
                    proc.wait(timeout=15)
                except Exception as exc:
                    self.note(f"Ошибка остановки записи: {exc}; файл на телефоне {remote}")
                    proc.kill()
            self.recording = None
            if pid:
                try:
                    target = self.output / f"{name}.mp4"
                    self.adb("pull", remote, str(target), timeout=40)
                    if target.stat().st_size < 1000:
                        raise RuntimeError("Пустая запись")
                    self.note(f"Сохранено: {target.name}")
                    self.adb("shell", "rm", remote)
                except Exception as exc:
                    self.note(f"Не удалось получить видео: {exc}; исходник {remote}")
                    raise

    def run(self):
        if self.adb("get-state") != "device":
            raise RuntimeError("Телефон недоступен")
        for key in ("accelerometer_rotation", "user_rotation"):
            value = self.adb("shell", "settings", "get", "system", key)
            if not re.fullmatch(r"[0-3]", value):
                raise RuntimeError("Не прочитана ориентация")
            self.rotation[key] = value
        (self.output / "original-state.json").write_text(
            json.dumps({"rotation": self.rotation, "power_level": self.args.power_level}, indent=2), encoding="utf-8")
        self.adb("shell", "am", "start", "-n", "com.blelock.app/.MainActivity")
        nodes = self.dump()
        if any(n.get("class") == "android.widget.RadioButton" for n in nodes):
            self.tap(r"^Назад$|^Back$", "android.widget.TextView")
        self.find(r"^Настройки замка|^Lock settings|^Налаштування замка", kind="android.widget.Button")
        if self.args.mode == "smoke":
            with self.record("00-smoke"):
                time.sleep(4)
                self.tap(r"^Настройки приложения|^App settings|^Налаштування застосунку", "android.widget.ImageButton")
                self.find(r"Язык|Language|Мова", kind="android.widget.TextView")
                time.sleep(4)
                self.tap(r"^Назад$|^Back$", "android.widget.TextView")
                self.find(r"^Настройки замка|^Lock settings|^Налаштування замка", kind="android.widget.Button")
                time.sleep(3)
            return
        if self.args.power_level is None:
            raise RuntimeError("Для полного дубля укажите исходный уровень BLE --power-level 0..3")
        self.operator("Подготовьте замок: дверь закрыта; удержание открытого выключено. Сохраните настройки Web UI, уберите уведомления. Подтвердите исходную мощность BLE.")
        original_auto = self.find(r"Автооткрытие замка|Auto-open lock|Автовідкриття замка", kind="android.widget.Switch").get("checked") == "true"
        (self.output / "original-state.json").write_text(json.dumps({
            "rotation": self.rotation, "auto_open": original_auto, "power_level": self.args.power_level}, indent=2), encoding="utf-8")
        try:
            with self.record("01-main-auto"):
                time.sleep(4)
                self.auto_mode(True)
                time.sleep(3)
                try:
                    self.power(1)
                    time.sleep(3)
                    self.power(3)
                    time.sleep(3)
                finally:
                    self.power(self.args.power_level)
                self.auto_mode(False)
                time.sleep(3)
            self.operator("Эпизод удержания. Будет отправлена настоящая команда. Держите дверь закрытой; для чистого дубля не трогайте телефон.")
            hold_active = False
            try:
                with self.record("02-hold-open"):
                    hold_active = True
                    self.tap(r"^Оставить замок открытым|^Keep.*lock.*open|^Залишити замок відкритим", "android.widget.Button")
                    self.find(r"Команда выполнена|Command completed|Команду виконано", kind="android.widget.Button", timeout=25)
                    time.sleep(4)
                    self.tap(r"^Оставить замок открытым|^Keep.*lock.*open|^Залишити замок відкритим", "android.widget.Button")
                    self.find(r"Команда выполнена|Command completed|Команду виконано", kind="android.widget.Button", timeout=25)
                    self.find(r"^Оставить замок открытым|^Keep.*lock.*open|^Залишити замок відкритим", kind="android.widget.Button")
                    hold_active = False
                    time.sleep(3)
            finally:
                if hold_active:
                    self.operator("Удержание могло остаться включённым. Проверьте и выключите его штатной кнопкой. Не переключайте вслепую: первая команда могла не дойти.")
            self.tap(r"^Настройки замка|^Lock settings|^Налаштування замка", "android.widget.Button")
            self.operator("Web UI открывается. Перед записью прокрутите к панели «Настройки замка», чтобы телефоны и Wi-Fi не попали в кадр. Сохраните исходные значения. UIAutomator не видит элементы этого WebView: этот эпизод сопровождает оператор. Лимит 180 секунд.")
            with self.record("03-settings"):
                self.operator("Запись настроек идёт: покажите ползунки звука/задержки, первый шаг мастера без сохранения калибровки. Восстановите значения и вернитесь на главный экран.")
                self.find(r"^Настройки замка|^Lock settings|^Налаштування замка", kind="android.widget.Button")
                time.sleep(3)
            self.operator("Интернет-открытие: дождитесь завершения локального сеанса. После команды откройте и закройте дверь. Запись остановится после вашего Enter. Не ждите консольного приглашения, чтобы открыть дверь.")
            with self.record("04-internet"):
                self.tap(r"Открыть через интернет|Open.*internet|Відкрити через інтернет")
                self.operator("Команда нажата. Откройте/закройте дверь и дождитесь «Дверь закрыта и заперта» на телефоне. При ошибке нажмите Ctrl+C.")
                self.find(r"Дверь закрыта и заперта|Door.*closed.*locked|Двері зачинено та замкнено", timeout=10)
                time.sleep(4)
        finally:
            try:
                self.auto_mode(original_auto)
            except Exception:
                self.operator("Не удалось автоматически восстановить автооткрытие. Вернитесь на главный экран, проверьте связь; восстановите исходный режим вручную.")
                self.auto_mode(original_auto)
            self.operator("Проверьте восстановление настроек Web UI, закрытие и запирание двери. Верните исходный режим уведомлений.")


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description="Запись демонстрации реального TheKidLock через ADB")
    parser.add_argument("--adb", default="C:/Dev/AndroidSdk/platform-tools/adb.exe")
    parser.add_argument("--serial", default="192.168.1.65:33719")
    parser.add_argument("--mode", choices=("smoke", "full"), default="smoke")
    parser.add_argument("--power-level", type=int, choices=range(4))
    parser.add_argument("--output", default=str(root / "out" / "demo-video" / datetime.datetime.now().strftime("%Y%m%d-%H%M%S")))
    args = parser.parse_args()
    demo = Demo(args)
    success = False
    try:
        demo.run()
        success = True
    except (Exception, KeyboardInterrupt) as exc:
        demo.note(f"ОСТАНОВЛЕНО: {exc}")
        try:
            demo.adb("shell", "screencap", "-p", "/sdcard/thekidlock-demo-error.png")
            demo.adb("pull", "/sdcard/thekidlock-demo-error.png", str(demo.output / "error.png"))
            demo.adb("shell", "rm", "/sdcard/thekidlock-demo-error.png")
        except Exception:
            pass
    finally:
        for key, value in demo.rotation.items():
            try:
                demo.adb("shell", "settings", "put", "system", key, value)
                if demo.adb("shell", "settings", "get", "system", key) != value:
                    raise RuntimeError("Не восстановлено значение")
            except Exception as exc:
                demo.note(f"Не восстановлена ориентация {key}: {exc}")
                success = False
        demo.note("УСПЕШНО" if success else "Требуется проверка/повторный дубль")
    return 0 if success else 1


if __name__ == "__main__":
    raise SystemExit(main())
