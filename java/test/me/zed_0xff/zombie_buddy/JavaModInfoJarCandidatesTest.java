package me.zed_0xff.zombie_buddy;

import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.*;

import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;
import org.mockito.MockedStatic;

import java.nio.file.Files;
import java.nio.file.Path;

/**
 * Covers the numbered javaJarFileN / zbVersionMinN / zbVersionMaxN mod.info keys that let a single
 * mod.info offer several JARs built against different ZombieBuddy API versions.
 */
class JavaModInfoJarCandidatesTest {

    @TempDir
    Path tempDir;

    private MockedStatic<Utils> utilsMock;
    private MockedStatic<ZombieBuddy> zbMock;

    @BeforeEach
    void setUp() {
        utilsMock = mockStatic(Utils.class, CALLS_REAL_METHODS);
        utilsMock.when(Utils::isServer).thenReturn(false);

        zbMock = mockStatic(ZombieBuddy.class, CALLS_REAL_METHODS);
    }

    @AfterEach
    void tearDown() {
        utilsMock.close();
        zbMock.close();
    }

    private void setZbVersion(String version) {
        zbMock.when(ZombieBuddy::getVersion).thenReturn(version);
    }

    private void writeModInfo(String content) throws Exception {
        Files.writeString(tempDir.resolve("mod.info"), content);
    }

    private void touchJar(String name) throws Exception {
        Files.createFile(tempDir.resolve(name));
    }

    @Test
    void bareKeysOnly_stillWorkLikeBefore() throws Exception {
        touchJar("foo.jar");
        writeModInfo("""
            javaJarFile=foo.jar
            javaPkgName=some.pkg
            zbVersionMin=1.0.0
            """);
        setZbVersion("2.0.0");

        JavaModInfo info = JavaModInfo.parse(tempDir);
        assertNotNull(info);
        assertEquals(tempDir.resolve("foo.jar"), info.jarPath());
    }

    @Test
    void numberedCandidate_selectedWhenItsRangeMatches() throws Exception {
        touchJar("old.jar");
        touchJar("new.jar");
        writeModInfo("""
            javaJarFile=old.jar
            javaPkgName=some.pkg
            zbVersionMin=1.0.0
            zbVersionMax=1.9.9

            javaJarFile2=new.jar
            zbVersionMin2=2.0.0
            """);
        setZbVersion("3.0.0");

        JavaModInfo info = JavaModInfo.parse(tempDir);
        assertNotNull(info);
        assertEquals(tempDir.resolve("new.jar"), info.jarPath());
    }

    @Test
    void numberedCandidate_lowestIndexWinsWhenMultipleMatch() throws Exception {
        touchJar("a.jar");
        touchJar("b.jar");
        writeModInfo("""
            javaPkgName=some.pkg
            javaJarFile1=a.jar
            zbVersionMin1=1.0.0

            javaJarFile2=b.jar
            zbVersionMin2=1.0.0
            """);
        setZbVersion("5.0.0");

        JavaModInfo info = JavaModInfo.parse(tempDir);
        assertNotNull(info);
        assertEquals(tempDir.resolve("a.jar"), info.jarPath());
    }

    @Test
    void orphanVersionKeyWithoutMatchingJar_isIgnoredNotFatal() throws Exception {
        touchJar("foo.jar");
        writeModInfo("""
            zbVersionMin5=1.0.0
            javaJarFile=foo.jar
            javaPkgName=some.pkg
            """);
        setZbVersion("2.0.0");

        JavaModInfo info = JavaModInfo.parse(tempDir);
        assertNotNull(info);
        assertEquals(tempDir.resolve("foo.jar"), info.jarPath());
    }

    @Test
    void noCandidateInRange_parseReturnsNull() throws Exception {
        touchJar("foo.jar");
        writeModInfo("""
            javaJarFile=foo.jar
            javaPkgName=some.pkg
            zbVersionMax=1.0.0
            """);
        setZbVersion("9.9.9");

        assertNull(JavaModInfo.parse(tempDir));
    }
}
