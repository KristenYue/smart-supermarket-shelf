package com.smartshelf.alerts;

import java.util.List;
import java.util.Map;

import org.springframework.beans.factory.annotation.Value;
import org.springframework.core.ParameterizedTypeReference;
import org.springframework.stereotype.Component;
import org.springframework.web.client.RestClient;

@Component
final class HttpShelfSource implements ShelfSource {
    private static final ParameterizedTypeReference<List<Map<String, Object>>> ROWS =
            new ParameterizedTypeReference<>() {};
    private final RestClient client;

    HttpShelfSource(@Value("${shelf.source-url}") String baseUrl,
                    @Value("${shelf.source-token}") String token) {
        this.client = RestClient.builder()
                .baseUrl(baseUrl)
                .defaultHeader("X-API-Token", token)
                .build();
    }

    @Override
    public List<Map<String, Object>> analysis(String deviceId, int limit) {
        return fetch(deviceId, "analysis", limit);
    }

    @Override
    public List<Map<String, Object>> telemetry(String deviceId, int limit) {
        return fetch(deviceId, "telemetry", limit);
    }

    private List<Map<String, Object>> fetch(String deviceId, String kind, int limit) {
        List<Map<String, Object>> rows = client.get()
                .uri("/api/shelves/{deviceId}/{kind}?limit={limit}", deviceId, kind, limit)
                .retrieve()
                .body(ROWS);
        return rows == null ? List.of() : rows;
    }
}
