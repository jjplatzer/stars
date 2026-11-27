import com.fasterxml.jackson.databind.ObjectMapper;
import javax.jms.*;
import javax.xml.stream.*;
import java.io.*;
import java.time.Instant;
import java.util.Locale;

record Delivery(int version, String receivedAt, long sequence, String writer, String queue,
                String messageId, long jmsTimestamp, boolean redelivered, String root,
                String facility, String encoding, Object body) {
    static final ObjectMapper JSON = new ObjectMapper();
    static final int MAX_BYTES = 10 * 1024 * 1024; // Same delivery limit as REDS.
    record Header(String root, String facility) {}

    static Header header(InputStream bytes, Reader text) {
        var factory = XMLInputFactory.newFactory();
        factory.setProperty(XMLInputFactory.SUPPORT_DTD, false);
        factory.setProperty(XMLInputFactory.IS_SUPPORTING_EXTERNAL_ENTITIES, false);
        String root = null, legacy = null;
        try {
            var xml = text == null ? factory.createXMLStreamReader(bytes) : factory.createXMLStreamReader(text);
            try {
                int depth = 0;
                while (xml.hasNext()) {
                    switch (xml.next()) {
                        case XMLStreamConstants.START_ELEMENT -> {
                            depth++;
                            String name = xml.getLocalName();
                            if (depth == 1) root = name;
                            if (depth == 2 && (name.equals("src") || name.equals("srcTracon") || name.equals("tracon"))) {
                                String id = xml.getElementText().strip().toUpperCase(Locale.ROOT);
                                depth--;
                                if (id.matches("[A-Z0-9]{1,12}")) {
                                    if (name.equals("src")) return new Header(root, id);
                                    if (legacy == null || name.equals("srcTracon")) legacy = id;
                                }
                            }
                        }
                        case XMLStreamConstants.END_ELEMENT -> depth--;
                    }
                }
            } finally { xml.close(); }
        } catch (XMLStreamException ignored) { /* Archive unreadable XML too. */ }
        return new Header(root, legacy == null ? "unknown" : legacy);
    }

    static Delivery read(Message message, Instant time, String writer, long sequence, String queue) throws JMSException, IOException {
        Payload payload = Payload.read(message, MAX_BYTES);
        Header header = payload.body() instanceof byte[] bytes ? header(new ByteArrayInputStream(bytes), null)
                : header(null, new StringReader(payload.body() == null ? "" : (String)payload.body()));
        return new Delivery(1, time.toString(), sequence, writer, queue, message.getJMSMessageID(),
                message.getJMSTimestamp(), message.getJMSRedelivered(), header.root(), header.facility(), payload.encoding(), payload.body());
    }
}
