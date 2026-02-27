import com.sun.net.httpserver.HttpServer;
import com.sun.net.httpserver.HttpExchange;
import java.io.*;
import java.net.InetSocketAddress;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.*;
import java.util.*;

public class RateLimitedPaymentServer {

    // ===========================
    // Token Bucket Implementation
    // ===========================
    static class TokenBucket {
        private final long capacity;
        private final long refillRate;
        private double tokens;
        private long lastRefillTime;

        public TokenBucket(long capacity, long refillRate) {
            this.capacity = capacity;
            this.refillRate = refillRate;
            this.tokens = capacity;
            this.lastRefillTime = System.nanoTime();
        }

        public synchronized boolean allowRequest() {
            refill();
            if (tokens >= 1) {
                tokens -= 1;
                return true;
            }
            return false;
        }

        private void refill() {
            long now = System.nanoTime();
            double tokensToAdd =
                    ((now - lastRefillTime) / 1_000_000_000.0) * refillRate;

            if (tokensToAdd > 0) {
                tokens = Math.min(capacity, tokens + tokensToAdd);
                lastRefillTime = now;
            }
        }
    }

    // ===========================
    // Rate Limiter Service
    // ===========================
    static class RateLimiterService {
        private final ConcurrentHashMap<String, TokenBucket> userBuckets =
                new ConcurrentHashMap<>();

        private static final long CAPACITY = 5;      // max 5 requests
        private static final long REFILL_RATE = 5;   // 5 per second

        public boolean isAllowed(String userId) {
            userBuckets.putIfAbsent(userId,
                    new TokenBucket(CAPACITY, REFILL_RATE));
            return userBuckets.get(userId).allowRequest();
        }
    }

    private static final RateLimiterService rateLimiter =
            new RateLimiterService();

    // ===========================
    // Main Method
    // ===========================
    public static void main(String[] args) throws Exception {

        HttpServer server = HttpServer.create(new InetSocketAddress(8080), 0);

        server.createContext("/pay", exchange -> {
            if ("POST".equals(exchange.getRequestMethod())) {
                handlePayment(exchange);
            } else {
                sendResponse(exchange, 405, "Method Not Allowed");
            }
        });

        server.setExecutor(Executors.newFixedThreadPool(10));
        server.start();

        System.out.println("Server started on http://localhost:8080/pay");
    }

    // ===========================
    // Payment Handler
    // ===========================
    private static void handlePayment(HttpExchange exchange)
            throws IOException {

        String body = new String(
                exchange.getRequestBody().readAllBytes(),
                StandardCharsets.UTF_8
        );

        // Very simple JSON parsing (for demo)
        String userId = extractValue(body, "userId");
        String amount = extractValue(body, "amount");

        if (userId == null) {
            sendResponse(exchange, 400, "Invalid Request");
            return;
        }

        if (!rateLimiter.isAllowed(userId)) {
            sendResponse(exchange, 429, "Too Many Requests");
            return;
        }

        String response =
                "Payment successful for user: " +
                userId +
                " amount: " +
                amount;

        sendResponse(exchange, 200, response);
    }

    private static String extractValue(String json, String key) {
        try {
            String pattern = "\"" + key + "\"";
            int index = json.indexOf(pattern);
            if (index == -1) return null;

            int colon = json.indexOf(":", index);
            int comma = json.indexOf(",", colon);
            int end = comma != -1 ? comma : json.indexOf("}", colon);

            return json.substring(colon + 1, end)
                    .replaceAll("\"", "")
                    .trim();
        } catch (Exception e) {
            return null;
        }
    }

    private static void sendResponse(HttpExchange exchange,
                                     int statusCode,
                                     String response)
            throws IOException {

        byte[] bytes = response.getBytes(StandardCharsets.UTF_8);
        exchange.sendResponseHeaders(statusCode, bytes.length);

        OutputStream os = exchange.getResponseBody();
        os.write(bytes);
        os.close();
    }
}
