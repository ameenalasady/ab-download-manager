import com.abdownloadmanager.desktop.AppComponent;
import kotlin.jvm.internal.Reflection;
import org.koin.core.context.GlobalContext;

import javax.swing.SwingUtilities;
import java.awt.*;
import java.awt.event.MouseEvent;
import java.awt.event.KeyEvent;
import java.nio.file.*;
import java.util.Arrays;

/** Developer-only UI driver. Events stay inside a disposable application's AWT tree. */
public final class UiReviewDriver {
    private static AppComponent app;
    private static Path root;

    public static void main(String[] args) throws Exception {
        root = Path.of(System.getProperty("abdm.experimental.profileRoot")).toRealPath();
        if (!Files.exists(root.resolve(".abdm-experimental-profile")) ||
            Files.exists(root.resolve(".abdm-desktop-integration.properties")) ||
            !root.toString().contains("/build/ui-review/")) {
            throw new IllegalStateException("UI review requires an isolated build/ui-review profile");
        }
        Thread driver = new Thread(() -> {
            try {
                while (true) {
                    Path status = root.resolve("session-status.json");
                    String record = Files.exists(status) ? Files.readString(status) : "";
                    String pid = Long.toString(ProcessHandle.current().pid());
                    if (GlobalContext.INSTANCE.getOrNull() != null &&
                        (record.contains("\"pid\":" + pid + ",") || record.contains("\"pid\": " + pid + ","))) break;
                    Thread.sleep(100);
                }
                app = GlobalContext.INSTANCE.get().get(Reflection.getOrCreateKotlinClass(AppComponent.class), null, null);
                Files.writeString(root.resolve("ui-ready"), ProcessHandle.current().pid() + "\n");
                Path commands = root.resolve("ui-command");
                while (true) {
                    if (!Files.exists(commands)) { Thread.sleep(100); continue; }
                    String command = Files.readString(commands).trim();
                    Files.delete(commands);
                    try {
                        SwingUtilities.invokeAndWait(() -> {
                            try { perform(command); }
                            catch (Exception e) { throw new RuntimeException(e); }
                        });
                        Thread.sleep(500);
                        Files.writeString(root.resolve("ui-result"), "OK " + command + "\n");
                    } catch (Exception e) {
                        e.printStackTrace();
                        Files.writeString(root.resolve("ui-result"), "ERROR " + command + " " + e + "\n");
                    }
                }
            } catch (Exception e) { e.printStackTrace(); }
        }, "isolated-ui-review");
        driver.setDaemon(true);
        driver.start();
        com.abdownloadmanager.desktop.experimental.NativeWaylandAppKt.main(args);
    }

    private static void perform(String command) throws Exception {
        switch (command) {
            case "settings" -> app.openSettings();
            case "close-settings" -> app.closeSettings();
            case "url" -> app.openEnterNewURLWindow();
            case "close-url" -> app.closeEnterNewURLWindow();
            case "queues" -> app.openQueues(null);
            case "close-queues" -> app.closeQueues();
            case "about" -> app.openAboutPage();
            case "close-about" -> app.closeAbout();
            default -> {
                if (command.equals("search") || command.startsWith("search ")) {
                    app.getShowHomeSlot().getValue().getChild().getInstance().getFilterState()
                        .setTextToSearch(command.length() > 6 ? command.substring(7) : "");
                } else if (command.startsWith("key ")) {
                    Component target = KeyboardFocusManager.getCurrentKeyboardFocusManager().getFocusOwner();
                    if (target == null) throw new IllegalStateException("No focused component in test application");
                    int key = Integer.parseInt(command.substring(4));
                    long time = System.currentTimeMillis();
                    KeyboardFocusManager manager = KeyboardFocusManager.getCurrentKeyboardFocusManager();
                    manager.redispatchEvent(target, new KeyEvent(target, KeyEvent.KEY_PRESSED, time, 0, key, KeyEvent.CHAR_UNDEFINED));
                    manager.redispatchEvent(target, new KeyEvent(target, KeyEvent.KEY_RELEASED, time + 20, 0, key, KeyEvent.CHAR_UNDEFINED));
                } else if (command.startsWith("resize ")) {
                    String[] parts = command.split(" ", 4);
                    Window window = Arrays.stream(Window.getWindows())
                        .filter(w -> w.isVisible() && w instanceof Frame f && f.getTitle().startsWith(parts[3]))
                        .findFirst().orElseThrow();
                    window.setSize(Integer.parseInt(parts[1]), Integer.parseInt(parts[2]));
                } else if (command.startsWith("click ") || command.startsWith("context ")) {
                    String[] parts = command.split(" ", 4);
                    int x = Integer.parseInt(parts[1]), y = Integer.parseInt(parts[2]);
                    String title = parts[3];
                    Window window = Arrays.stream(Window.getWindows())
                        .filter(w -> w.isVisible() && w instanceof Frame f && f.getTitle().startsWith(title))
                        .findFirst().orElseThrow();
                    Component target = SwingUtilities.getDeepestComponentAt(window, x, y);
                    if (target == null) throw new IllegalStateException("No component at requested position");
                    Point point = SwingUtilities.convertPoint(window, x, y, target);
                    long time = System.currentTimeMillis();
                    boolean context = command.startsWith("context ");
                    int button = context ? MouseEvent.BUTTON3 : MouseEvent.BUTTON1;
                    int mask = context ? MouseEvent.BUTTON3_DOWN_MASK : MouseEvent.BUTTON1_DOWN_MASK;
                    target.dispatchEvent(new MouseEvent(target, MouseEvent.MOUSE_MOVED, time, 0,
                        point.x, point.y, 0, false));
                    target.dispatchEvent(new MouseEvent(target, MouseEvent.MOUSE_PRESSED, time + 1,
                        mask, point.x, point.y, 1, context, button));
                    target.dispatchEvent(new MouseEvent(target, MouseEvent.MOUSE_RELEASED, time + 50, 0,
                        point.x, point.y, 1, context, button));
                } else throw new IllegalArgumentException("Unknown UI action: " + command);
            }
        }
    }
}
