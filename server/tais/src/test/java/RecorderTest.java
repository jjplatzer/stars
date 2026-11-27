import com.solacesystems.jms.message.*;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;
import java.nio.file.*;
import java.time.Instant;
import java.util.*;
import static org.junit.jupiter.api.Assertions.*;

class RecorderTest {
    @TempDir Path dir;
    final Instant time = Instant.parse("2026-11-19T14:39:59Z");

    Delivery text(String xml, long seq) throws Exception {
        var message = new SolTextMessage();
        message.setText(xml);
        message.setJMSMessageID("ID:example");
        message.setJMSRedelivered(true);
        return Delivery.read(message, time, "writer", seq, "queue");
    }

    void append(Spool spool, Delivery delivery, Instant now) throws Exception {
        spool.append(delivery.facility(), Delivery.JSON.writeValueAsBytes(delivery), now);
    }

    @Test void routingAndRecovery() throws Exception {
        String xml = "<TATrackAndFlightPlan xmlns='urn:test'><srcTracon>X</srcTracon><src>n90</src>"
                + "<record><flightPlan/></record>\n<record><track/></record></TATrackAndFlightPlan>";
        var spool = new Spool(dir, 1 << 20);
        var d = text(xml, 1);
        assertEquals("N90", d.facility());
        assertTrue(d.redelivered());
        assertEquals("ID:example", d.messageId());
        append(spool, d, time);
        append(spool, text(xml, 2), time);
        append(spool, text("<Status><tracon>A80</tracon></Status>", 3), time);
        spool.rotate(time.plusSeconds(1));
        try (var files = Files.list(dir)) {
            var keys = files.map(f -> Upload.key("tais", f)).sorted().toList();
            assertEquals(2, keys.size());
            assertTrue(keys.get(0).startsWith("tais/v1/A80/2026/11/19/14/1435-"));
            assertTrue(keys.get(1).startsWith("tais/v1/N90/2026/11/19/14/1435-"));
        }
        var malformed = new SolBytesMessage();
        byte[] original = new byte[20000];
        new Random(7).nextBytes(original);
        malformed.writeBytes(original);
        malformed.reset();
        Instant next = time.plusSeconds(1);
        var raw = Delivery.read(malformed, next, "writer", 4, "queue");
        assertEquals("unknown", raw.facility());
        append(spool, raw, next);
        try (var files = Files.newDirectoryStream(dir,"*.open")) {
            Files.writeString(files.iterator().next(), "{partial", StandardOpenOption.APPEND);
        }
        new Spool(dir, 1 << 20);
        try (var files = Files.newDirectoryStream(dir, "*.ready")) {
            for (var file : files) {
                var lines = Files.readAllLines(file);
                if (Upload.key("tais", file).contains("/N90/")) {
                    assertEquals(2, lines.size());
                    assertEquals(xml, Delivery.JSON.readTree(lines.getFirst()).get("body").textValue());
                    assertEquals(2, Delivery.JSON.readTree(lines.getLast()).get("sequence").longValue());
                }
                if (Upload.key("tais", file).contains("/unknown/")) {
                    assertTrue(Upload.key("tais", file).contains("/1440-"));
                    assertEquals(1, lines.size());
                    assertArrayEquals(original, Delivery.JSON.readTree(lines.getFirst()).get("body").binaryValue());
                }
            }
        }
    }
}
