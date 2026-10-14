package me.zed_0xff.zombie_buddy;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;
import static me.zed_0xff.zombie_buddy.ModFlags.*;
import static org.junit.jupiter.api.Assertions.*;

class ApprovalChoicesTest {
    @TempDir Path root;

    @Test
    void actualDialogPreservesChoicesSafetyAndGroupedAuthorTrust() throws Exception {
        // Constructor/buildLines tests use real game ImBoolean values but need no native graphics context.
        ApprovalDialogProbe.stateTests();
    }

    @Test
    void everyInitialSelectionSupportsBothResetAndOppositeChoice() {
        for (Boolean initial : new Boolean[]{null, false, true}) {
            var entry = ApprovalDialogProbe.entry("transition", initial, initial == null ? 0 : MF_PERSIST, null, null);
            var choices = new ApprovalChoices(List.of(entry));
            boolean requested = initial == null || !initial;
            assertEquals(requested, choices.toggle(0, requested));
            assertTrue(choices.shouldPersist(0, true));
            assertFalse(choices.toggle(0, requested), "Second click must restore launch-only denial");
            assertFalse(choices.shouldPersist(0, true), "Reset must never save a false decision");
            assertEquals(0xff969696, choices.denyColor(0, false, 123));
            assertEquals(!requested, choices.toggle(0, !requested), "Opposite choice switches directly");
            assertTrue(choices.shouldPersist(0, true));
        }
    }

    @Test
    void resetSurvivesProtocolAndRemovesOnlyMatchingStoredHash() throws Exception {
        var entry = ApprovalDialogProbe.entry("saved", true, MF_PERSIST, null, null);
        var choices = new ApprovalChoices(List.of(entry));
        assertFalse(choices.toggle(0, true));
        var request = choices.finish(List.of(entry), true);
        Path response = root.resolve("response.json");
        JarBatchApprovalProtocol.writeResponse(response, request);
        var received = JarBatchApprovalProtocol.readResponse(response);
        assertNull(received.get(0).decision);
        var stored = new ArrayList<ModApprovalsStore.ModEntry>();
        var keep = new ModApprovalsStore.ModEntry("keep", null, "different-hash", true, "before", null);
        stored.add(keep);
        stored.add(new ModApprovalsStore.ModEntry("saved", null, entry.sha256, true, "before", null));
        ApprovalChoices.applyClears(received, stored, id -> fail("Unsigned reset must not change author trust"));
        assertEquals(List.of(keep), stored);
        assertEquals(Boolean.FALSE, received.get(0).decision);
        assertFalse(received.get(0).flags.has(MF_PERSIST | MF_TRUST_AUTHOR));
    }

    @Test
    void signedResetRetainsExplicitAuthorUntrustProcessing() {
        var author = new SteamWorkshop.SteamID64(76561197974968019L);
        var entry = ApprovalDialogProbe.entry("signed", null, MF_PERSIST,
                new JarBatchApprovalProtocol.Entry.ZBSignature(true, author, "verified"), null);
        var removedAuthor = new AtomicReference<SteamWorkshop.SteamID64>();
        ApprovalChoices.applyClears(List.of(entry), new ArrayList<>(), removedAuthor::set);
        assertEquals(author, removedAuthor.get());
        assertEquals(Boolean.FALSE, entry.decision);
    }

    @Test
    void blockedResetCannotRemoveSavedRecordsOrTrust() {
        var entry = ApprovalDialogProbe.entry("invalid", null, MF_PERSIST,
                new JarBatchApprovalProtocol.Entry.ZBSignature(false, null, "invalid"), null);
        var old = new ModApprovalsStore.ModEntry("invalid", null, entry.sha256, false, "before", null);
        var stored = new ArrayList<>(List.of(old));
        ApprovalChoices.applyClears(List.of(entry), stored, id -> fail("Blocked row cannot revoke trust"));
        assertEquals(List.of(old), stored);
        assertEquals(Boolean.FALSE, entry.decision);
        assertFalse(entry.flags.has(MF_PERSIST));
    }
}
