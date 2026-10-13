import java.net.InetAddress;
import java.net.http.HttpClient;
import java.nio.channels.SocketChannel;
import java.nio.file.Files;
import java.nio.file.Path;
import com.sun.jna.platform.win32.Kernel32Util;
import com.sun.jna.platform.win32.Tlhelp32;
import me.zed_0xff.zombie_buddy.Agent;



public final class NativeGuardProbe {
    public static void main(String[] args) throws Exception {
        if (!Agent.isExperimental()) throw new AssertionError("Experimental option lost");
        for (String pair : new String[]{"policy=deny-new", "allow_unsigned_mods=false", "frontend=console", "config_dir=./config"}) {
            String[] parts = pair.split("=", 2);
            if (!parts[1].equals(Agent.arguments.get(parts[0]))) throw new AssertionError("Option lost: " + pair);
        }
        try {
            InetAddress.getByName("localhost");
            try (SocketChannel channel = SocketChannel.open()) {
                if (!channel.isOpen()) throw new AssertionError("Socket channel failed");
            }
            HttpClient client = HttpClient.newHttpClient();
            HttpClient.class.getMethod("close").invoke(client);
        } catch (InternalError error) {
            if (args[1].equals("reproduce") && error.getMessage().equals("platform encoding not initialized")) {
                System.out.println("REPRO original loader: real native encoding failure");
                return;
            }
            throw error;
        }
        if (args[1].equals("reproduce")) throw new AssertionError("Original failure not reproduced");
        int javaCount = 0, instrumentCount = 0;
        for (Tlhelp32.MODULEENTRY32W module : Kernel32Util.getModules((int)ProcessHandle.current().pid())) {
            String name = module.szModule();
            if (name.equalsIgnoreCase("java.dll") || name.equalsIgnoreCase("instrument.dll")) {
                Path loaded = Path.of(module.szExePath());
                if (!Files.isSameFile(loaded, Path.of(args[0], name))) throw new AssertionError("Wrong module: " + loaded);
                if (name.equalsIgnoreCase("java.dll")) javaCount++; else instrumentCount++;
                System.out.println("MODULE=" + loaded);
            }
        }
        if (javaCount != 1 || instrumentCount != 1) throw new AssertionError("Duplicate/missing JVM DLLs");
        System.out.println("PASS native guard: canonical modules, DNS, socket, HTTP client and shared options; no game session");
    }
}
