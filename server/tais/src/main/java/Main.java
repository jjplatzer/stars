public final class Main {
    public static void main(String[] args) throws Exception {
        Recorder.run("TAIS", (spool, message, now, writer, sequence, queue) -> {
            Delivery delivery = Delivery.read(message, now, writer, sequence, queue);
            spool.append(delivery.facility(), Delivery.JSON.writeValueAsBytes(delivery), now);
        });
    }
}
