import com.abdownloadmanager.desktop.AppComponent;
import com.abdownloadmanager.shared.pages.adddownload.AddDownloadCredentialsInUiProps;
import com.abdownloadmanager.shared.pages.adddownload.ImportOptions;
import com.abdownloadmanager.shared.pages.adddownload.single.BaseAddSingleDownloadComponent;
import ir.amirab.downloader.downloaditem.http.HttpDownloadCredentials;
import kotlin.jvm.internal.Reflection;
import org.koin.core.context.GlobalContext;

import javax.swing.SwingUtilities;
import java.awt.*;
import java.awt.event.MouseEvent;
import java.awt.event.MouseWheelEvent;
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
            case "close-add" -> app.closeAddDownloadDialog();
            case "assert-no-option-dialogs" -> {
                if (Arrays.stream(Window.getWindows()).anyMatch(w -> w.isShowing() && w instanceof Dialog))
                    throw new IllegalStateException("An option picker opened a top-level dialog instead of a local popup");
            }
            case "enable-category" -> {
                var component = (BaseAddSingleDownloadComponent) app.getOpenedAddDownloadDialogs().getValue().getFirst();
                component.setUseCategory(true);
            }
            case "invalid-add-name" -> {
                var component = (BaseAddSingleDownloadComponent) app.getOpenedAddDownloadDialogs().getValue().getFirst();
                component.setName("invalid/name");
            }
            case "invalid-add-folder" -> {
                Path blocked = root.resolve("not-a-download-directory");
                Files.writeString(blocked, "Disposable validation fixture");
                var component = (BaseAddSingleDownloadComponent) app.getOpenedAddDownloadDialogs().getValue().getFirst();
                component.setFolder(blocked.toString());
            }
            case "queues" -> app.openQueues(null);
            case "close-queues" -> app.closeQueues();
            case "about" -> app.openAboutPage();
            case "close-about" -> app.closeAbout();
            default -> {
                if (command.startsWith("add ")) {
                    var links = Arrays.stream(command.substring(4).split(" ")).map(link -> {
                        var uri = java.net.URI.create(link);
                        if (!"http".equals(uri.getScheme()) || !"127.0.0.1".equals(uri.getHost()))
                            throw new IllegalArgumentException("Use a loopback HTTP fixture for UI review");
                        return new AddDownloadCredentialsInUiProps(
                            new HttpDownloadCredentials(link, null, null, null, null, null),
                            new AddDownloadCredentialsInUiProps.Configs(null));
                    }).toList();
                    app.openAddDownloadDialog(links, new ImportOptions());
                } else if (command.startsWith("assert-add-size ")) {
                    String[] parts = command.split(" ");
                    Window window = Arrays.stream(Window.getWindows())
                        .filter(w -> w.isShowing() && w instanceof Frame f && f.getTitle().startsWith("Add Download"))
                        .findFirst().orElseThrow();
                    Dimension expected = new Dimension(Integer.parseInt(parts[1]), Integer.parseInt(parts[2]));
                    if (!window.getSize().equals(expected) || !window.getMinimumSize().equals(expected) ||
                        !window.getMaximumSize().equals(expected) || ((Frame) window).isResizable())
                        throw new IllegalStateException("Add Download bounds: " + window.getSize() +
                            ", min=" + window.getMinimumSize() + ", max=" + window.getMaximumSize());
                } else if (command.startsWith("assert-add-status ")) {
                    var component = (BaseAddSingleDownloadComponent) app.getOpenedAddDownloadDialogs().getValue().getFirst();
                    var result = component.getCanAddResult().getValue();
                    if (result == null || !result.getClass().getSimpleName().equals(command.substring(18)))
                        throw new IllegalStateException("Unexpected add status: " + result);
                } else if (command.equals("long-add-folder")) {
                    Path folder = root.resolve("Downloads");
                    for (int i = 0; i < 6; i++) folder = folder.resolve("A-long-disposable-download-folder-" + i);
                    Files.createDirectories(folder);
                    var component = (BaseAddSingleDownloadComponent) app.getOpenedAddDownloadDialogs().getValue().getFirst();
                    component.setUseCategory(false);
                    component.setFolder(folder.toString());
                } else if (command.startsWith("ui-scale ")) {
                    float scale = Float.parseFloat(command.substring(9));
                    if (scale < 0.75f || scale > 2f) throw new IllegalArgumentException("Review scale out of range");
                    app.getAppSettings().getUiScale().setValue(scale);
                } else if (command.equals("search") || command.startsWith("search ")) {
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
                } else if (command.startsWith("scroll ")) {
                    String[] parts = command.split(" ", 5);
                    int x = Integer.parseInt(parts[1]), y = Integer.parseInt(parts[2]);
                    int rotation = Integer.parseInt(parts[3]);
                    Window window = Arrays.stream(Window.getWindows())
                        .filter(w -> w.isVisible() && w instanceof Frame f && f.getTitle().startsWith(parts[4]))
                        .findFirst().orElseThrow();
                    Component target = SwingUtilities.getDeepestComponentAt(window, x, y);
                    if (target == null) throw new IllegalStateException("No component at requested position");
                    Point point = SwingUtilities.convertPoint(window, x, y, target);
                    target.dispatchEvent(new MouseWheelEvent(target, MouseEvent.MOUSE_WHEEL,
                        System.currentTimeMillis(), 0, point.x, point.y, 0, false,
                        MouseWheelEvent.WHEEL_UNIT_SCROLL, 3, rotation));
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
