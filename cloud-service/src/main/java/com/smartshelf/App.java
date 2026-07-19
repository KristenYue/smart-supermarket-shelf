package com.smartshelf;

import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.ObjectMapper;
import io.javalin.Javalin;
import io.javalin.http.HttpStatus;
import io.javalin.http.staticfiles.Location;
import software.amazon.awssdk.core.SdkBytes;
import software.amazon.awssdk.services.dynamodb.DynamoDbClient;
import software.amazon.awssdk.services.iotdataplane.IotDataPlaneClient;
import software.amazon.awssdk.services.iotdataplane.model.PublishRequest;

import java.nio.charset.StandardCharsets;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;

public final class App {
    private static final Set<String> COMMANDS = Set.of("ON", "OFF", "FLASH", "AUTO", "ACK");

    private App() {}

    public static void main(String[] args) {
        Config config = Config.fromEnvironment();
        ObjectMapper json = new ObjectMapper();

        DynamoDbClient dynamo = DynamoDbClient.builder()
                .region(config.region())
                .build();
        IotDataPlaneClient iot = IotDataPlaneClient.builder()
                .region(config.region())
                .endpointOverride(config.iotDataEndpoint())
                .build();

        DynamoRepository repository = new DynamoRepository(dynamo, config);
        AnalysisService analysisService = new AnalysisService(repository, config);
        ScheduledExecutorService analyzer = Executors.newSingleThreadScheduledExecutor(runnable -> {
            Thread thread = new Thread(runnable, "shelf-analyzer");
            thread.setDaemon(true);
            return thread;
        });
        analyzer.scheduleWithFixedDelay(analysisService, 0, 2, TimeUnit.SECONDS);

        Javalin app = Javalin.create(server -> {
            server.showJavalinBanner = false;
            server.staticFiles.add(files -> {
                files.directory = "/public";
                files.location = Location.CLASSPATH;
                files.hostedPath = "/";
            });
        });

        app.before("/api/*", context -> {
            if (!config.apiToken().isBlank()
                    && !config.apiToken().equals(context.header("X-API-Token"))) {
                context.status(HttpStatus.UNAUTHORIZED).json(Map.of("error", "Invalid API token"));
                context.skipRemainingHandlers();
            }
        });

        app.get("/api/health", context -> context.json(Map.of(
                "status", "ok",
                "devices", config.deviceIds())));

        app.get("/api/shelves/{deviceId}/telemetry", context -> {
            String deviceId = requireDevice(config, context.pathParam("deviceId"));
            int limit = parseLimit(context.queryParam("limit"));
            context.json(repository.latestTelemetry(deviceId, limit));
        });

        app.get("/api/shelves/{deviceId}/analysis", context -> {
            String deviceId = requireDevice(config, context.pathParam("deviceId"));
            int limit = parseLimit(context.queryParam("limit"));
            context.json(repository.latestAnalysis(deviceId, limit));
        });

        app.post("/api/shelves/{deviceId}/commands", context -> {
            String deviceId = requireDevice(config, context.pathParam("deviceId"));
            Map<String, Object> body = json.readValue(context.body(), new TypeReference<>() {});
            String action = String.valueOf(body.getOrDefault("action", "")).trim().toUpperCase();
            if (!COMMANDS.contains(action)) {
                context.status(HttpStatus.BAD_REQUEST).json(Map.of(
                        "error", "Action must be one of " + COMMANDS));
                return;
            }

            String topic = "smartshelf/cmd/" + deviceId;
            iot.publish(PublishRequest.builder()
                    .topic(topic)
                    .qos(1)
                    .payload(SdkBytes.fromString(action, StandardCharsets.UTF_8))
                    .build());
            context.status(HttpStatus.ACCEPTED).json(Map.of(
                    "deviceID", deviceId,
                    "action", action,
                    "topic", topic));
        });

        app.exception(Exception.class, (error, context) -> {
            error.printStackTrace();
            if (!context.res().isCommitted()) {
                context.status(HttpStatus.INTERNAL_SERVER_ERROR)
                        .json(Map.of("error", error.getMessage() == null ? "Internal server error" : error.getMessage()));
            }
        });

        Runtime.getRuntime().addShutdownHook(new Thread(() -> {
            analyzer.shutdownNow();
            iot.close();
            dynamo.close();
        }));

        app.start(config.host(), config.port());
        System.out.printf("Smart Shelf dashboard: http://%s:%d%n", config.host(), config.port());
        if (config.apiToken().isBlank()) {
            System.out.println("Warning: API_TOKEN is empty; keep SERVER_HOST=127.0.0.1 for local demo use only.");
        }
    }

    private static String requireDevice(Config config, String requested) {
        if (!config.deviceIds().contains(requested)) {
            throw new IllegalArgumentException("Unknown device: " + requested);
        }
        return requested;
    }

    private static int parseLimit(String value) {
        if (value == null || value.isBlank()) return 50;
        try { return Math.max(1, Math.min(Integer.parseInt(value), 100)); }
        catch (NumberFormatException ignored) { return 50; }
    }
}

