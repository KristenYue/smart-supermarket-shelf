package com.smartshelf;

import java.time.Instant;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

final class AnalysisService implements Runnable {
    private final DynamoRepository repository;
    private final Config config;
    private final Map<String, Long> lastProcessed = new ConcurrentHashMap<>();

    AnalysisService(DynamoRepository repository, Config config) {
        this.repository = repository;
        this.config = config;
    }

    @Override
    public void run() {
        for (String deviceId : config.deviceIds()) {
            try {
                repository.latestTelemetry(deviceId, 25).stream()
                        .sorted((left, right) -> Long.compare(number(left, "timestamp", 0), number(right, "timestamp", 0)))
                        .filter(item -> number(item, "timestamp", 0) > lastProcessed.getOrDefault(deviceId, 0L))
                        .forEach(item -> process(deviceId, item));
            } catch (RuntimeException error) {
                System.err.println("Analysis poll failed for " + deviceId + ": " + error.getMessage());
            }
        }
    }

    private void process(String deviceId, Map<String, Object> telemetry) {
        long timestamp = number(telemetry, "timestamp", Instant.now().getEpochSecond());
        Map<String, Object> result = deviceId.equals("smart-shelf-01")
                ? analyzeShelf1(deviceId, timestamp, telemetry)
                : analyzeShelf2(deviceId, timestamp, telemetry);
        repository.putAnalysis(result);
        lastProcessed.put(deviceId, timestamp);
    }

    static Map<String, Object> analyzeShelf1(String deviceId, long timestamp, Map<String, Object> t) {
        double temperature = decimal(t, "temperatureC", Double.NaN);
        double humidity = decimal(t, "humidityPct", Double.NaN);
        double pressure = decimal(t, "pressureKPa", Double.NaN);
        long interactions = number(t, "interactionCount", 0);
        long visitors = number(t, "visitorCount", 0);
        boolean customerPresent = bool(t, "customerPresent", false);

        boolean temperatureAlert = !Double.isNaN(temperature) && (temperature < 2 || temperature > 8);
        boolean humidityAlert = !Double.isNaN(humidity) && (humidity < 30 || humidity > 70);
        boolean pressureWarning = !Double.isNaN(pressure) && (pressure < 100 || pressure > 103);
        boolean alert = temperatureAlert || humidityAlert;

        Map<String, Object> out = base(deviceId, timestamp, alert ? "ALERT_ENV" : "NORMAL");
        out.put("temperatureAlert", temperatureAlert);
        out.put("humidityAlert", humidityAlert);
        out.put("pressureWarning", pressureWarning);
        out.put("customerPresent", customerPresent);
        out.put("interactionCount", interactions);
        out.put("visitorCount", visitors);
        out.put("interactionRatePct", visitors == 0 ? 0.0 : interactions * 100.0 / visitors);
        out.put("errorNumber", (temperatureAlert ? 1 : 0) + (humidityAlert ? 1 : 0));
        out.put("advice", alert
                ? "Check cold-chain temperature, packaging and shelf ventilation"
                : "Environment is within the configured operating range");
        return out;
    }

    static Map<String, Object> analyzeShelf2(String deviceId, long timestamp, Map<String, Object> t) {
        double weight = decimal(t, "weightG", -1);
        long light = number(t, "lightRaw", -1);
        long sound = number(t, "soundRaw", -1);
        boolean button = bool(t, "buttonPressed", false);

        String state;
        String advice;
        if (button) {
            state = "MANUAL_ALARM";
            advice = "Customer requested staff assistance";
        } else if (light >= 0 && light < 100) {
            state = "ALERT_LIGHT";
            advice = "Check for an obstruction in front of the shelf";
        } else if (sound > 150) {
            state = "ALERT_SOUND";
            advice = "Inspect the shelf for impact or disturbance";
        } else if (weight >= 0 && weight <= 1) {
            state = "OUT_OF_STOCK";
            advice = "Restock this shelf immediately";
        } else if (weight > 1 && weight <= 25) {
            state = "LOW_STOCK";
            advice = "Plan a shelf refill";
        } else {
            state = "NORMAL";
            advice = "No action required";
        }

        Map<String, Object> out = base(deviceId, timestamp, state);
        out.put("weightG", weight);
        out.put("lightRaw", light);
        out.put("soundRaw", sound);
        out.put("buttonPressed", button);
        out.put("errorNumber", state.equals("NORMAL") ? 0 : 1);
        out.put("advice", advice);
        return out;
    }

    private static Map<String, Object> base(String deviceId, long timestamp, String state) {
        Map<String, Object> out = new LinkedHashMap<>();
        out.put("deviceID", deviceId);
        out.put("timestamp", timestamp);
        out.put("state", state);
        out.put("processedAt", Instant.now().getEpochSecond());
        return out;
    }

    private static long number(Map<String, Object> map, String key, long fallback) {
        Object value = map.get(key);
        if (value instanceof Number n) return n.longValue();
        try { return value == null ? fallback : Long.parseLong(value.toString()); }
        catch (NumberFormatException ignored) { return fallback; }
    }

    private static double decimal(Map<String, Object> map, String key, double fallback) {
        Object value = map.get(key);
        if (value instanceof Number n) return n.doubleValue();
        try { return value == null ? fallback : Double.parseDouble(value.toString()); }
        catch (NumberFormatException ignored) { return fallback; }
    }

    private static boolean bool(Map<String, Object> map, String key, boolean fallback) {
        Object value = map.get(key);
        if (value instanceof Boolean b) return b;
        return value == null ? fallback : Boolean.parseBoolean(value.toString());
    }
}
