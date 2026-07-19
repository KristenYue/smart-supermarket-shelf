package com.smartshelf;

import software.amazon.awssdk.regions.Region;

import java.net.URI;
import java.util.List;

record Config(
        Region region,
        URI iotDataEndpoint,
        String telemetryTable,
        String analysisTable,
        String apiToken,
        String host,
        int port,
        List<String> deviceIds) {

    static Config fromEnvironment() {
        String endpoint = required("AWS_IOT_DATA_ENDPOINT");
        if (!endpoint.startsWith("https://")) endpoint = "https://" + endpoint;
        return new Config(
                Region.of(env("AWS_REGION", "eu-west-2")),
                URI.create(endpoint),
                env("TELEMETRY_TABLE", "SmartShelfTelemetry"),
                env("ANALYSIS_TABLE", "SmartShelfAnalysis"),
                env("API_TOKEN", ""),
                env("SERVER_HOST", "127.0.0.1"),
                Integer.parseInt(env("SERVER_PORT", "8080")),
                List.of("smart-shelf-01", "smart-shelf-02"));
    }

    private static String required(String name) {
        String value = System.getenv(name);
        if (value == null || value.isBlank()) {
            throw new IllegalStateException("Missing required environment variable: " + name);
        }
        return value.trim();
    }

    private static String env(String name, String fallback) {
        String value = System.getenv(name);
        return value == null || value.isBlank() ? fallback : value.trim();
    }
}

